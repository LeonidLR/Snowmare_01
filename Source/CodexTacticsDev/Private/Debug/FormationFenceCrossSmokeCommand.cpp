// Dev-only console command (G026, designer 2026-11-25, X6). Three phases, one run:
//   1. Formation, three operatives 2 m from a 135 cm fence, double click 8 m past it. All three vault and stand within
//      1.5 m of the formation slots at that click inside 20 s, and nobody's path is longer than 1.5x the straight line.
//   2. One operative vaults the same fence and takes a heavy hit at 30 / 60 / 90 % of the clip (the last hit also asks
//      for a knockdown). He finishes on the far side and is never moved back toward the start by more than 20 cm.
//   3. Formation group-cover on that fence. Each operative is crouched within 0.5 s of his own arrival in his slot.
//   Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands FormationFenceCrossSmoke

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationTypes.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"

namespace FormationFenceCrossSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The squad stands this far off the face; the click is this far past the far face. G026. */
	constexpr float StandOffCm = 200.f;
	constexpr float GoalPastCm = 800.f;
	constexpr float CrossLimitSeconds = 20.f;
	constexpr float SlotRadiusCm = 150.f;
	constexpr float DetourLimit = 1.5f;
	/** Same gates as SquadSubsystemFormation.cpp WallSettleCm / WallLeaderSettleCm. */
	constexpr float FollowerArriveCm = 60.f;
	constexpr float LeaderArriveCm = 120.f;
	constexpr float CrouchLimitSeconds = 0.5f;
	constexpr float HitMarks[3] = { 0.30f, 0.60f, 0.90f };
	constexpr float HeavyHitHp = 80.f;
	constexpr float RetreatLimitCm = 20.f;

	struct FMember
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		FVector Start = FVector::ZeroVector;
		FVector Expected = FVector::ZeroVector;
		int32 MantlesBefore = 0;
		float Travel = 0.f;
		FVector Previous = FVector::ZeroVector;
		bool bPendingAtClick = false;
		float ArrivedAt = -1.f;
		float CrouchedAt = -1.f;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate PreloadGate;
		TArray<FMember> Members;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Retries = 0;
		MantleSmokeSites::FSite Site;
		FVector Goal = FVector::ZeroVector;
		FHitResult Hit;
		TSet<const AActor*> Used;
		bool bCrossJudged = false;
		bool bHitsArmed = false;
		bool bHitsClosed = false;
		int32 Hits = 0;
		float HitDamage[3] = { 0.f, 0.f, 0.f };
		float HitFraction[3] = { 0.f, 0.f, 0.f };
		FVector VaultStart = FVector::ZeroVector;
		FVector VaultFace = FVector::ZeroVector;
		FVector VaultDir = FVector::ForwardVector;
		float PeakAlong = 0.f;
		float WorstRetreat = 0.f;
		bool bSawMantle = false;
		bool bKnockAsked = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FormationFenceCrossSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	float Along(const FState& State, const FVector& Feet)
	{
		return static_cast<float>(FVector::DotProduct(Feet - State.VaultStart, State.VaultDir));
	}

	void NoteRetreat(FState& State, const FVector& Feet)
	{
		const float Here = Along(State, Feet);
		State.PeakAlong = FMath::Max(State.PeakAlong, Here);
		State.WorstRetreat = FMath::Max(State.WorstRetreat, State.PeakAlong - Here);
	}

	bool PlaceLine(UWorld* World, FState& State, const FVector& LeaderFeet, const FVector& Dir)
	{
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal2D();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float Laterals[3] = { 0.f, 140.f, -140.f };
		for (int32 Index = 0; Index < State.Members.Num() && Index < 3; ++Index)
		{
			AOperativeCharacter* Operative = State.Members[Index].Operative.Get();
			if (!Operative || !Nav)
			{
				return false;
			}
			Operative->StopOperative();
			Operative->LeaveCover(TEXT("G026 smoke setup"));
			Operative->ClearPendingCover();
			Operative->SetStance(EOperativeStance::Standing);
			const float HalfHeight = Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector Centre = LeaderFeet + FVector(0.f, 0.f, HalfHeight + 4.f);
			bool bPlaced = false;
			for (const float Lateral : { Laterals[Index], Laterals[Index] * 0.5f, 0.f })
			{
				FNavLocation OnNav;
				const FVector Want = LeaderFeet + Side * Lateral;
				if (bPlaced || !Nav->ProjectPointToNavigation(Want, OnNav, FVector(80.f, 80.f, 120.f))
					|| FMath::Abs(OnNav.Location.Z - LeaderFeet.Z) > 50.f)
				{
					continue;
				}
				Centre = OnNav.Location + FVector(0.f, 0.f, HalfHeight + 4.f);
				bPlaced = true;
			}
			Operative->TeleportTo(Centre, Dir.Rotation(), false, true);
		}
		return true;
	}

	/** A point BackCm behind Origin along -Dir, still on the near side of the fence and on the navmesh. */
	bool ProjectBack(UNavigationSystemV1& Nav, const FVector& Origin, const FVector& Dir, float BackCm, FVector& OutFeet)
	{
		FNavLocation OnNav;
		if (!Nav.ProjectPointToNavigation(Origin - Dir * BackCm, OnNav, FVector(200.f, 200.f, 200.f)))
		{
			return false;
		}
		if (FMath::Abs(OnNav.Location.Z - Origin.Z) > 80.f)
		{
			return false;
		}
		const float AlongDir = static_cast<float>(FVector::DotProduct(OnNav.Location - Origin, Dir));
		if (AlongDir > 40.f || AlongDir < -BackCm - 180.f)
		{
			return false;
		}
		OutFeet = OnNav.Location;
		return true;
	}

	/**
	 * A file on the near side: the leader at the stand-off, the others further back on a short path to him.
	 * The rear man then starts outside the covered-approach arrive distance while the leader can reach the fence.
	 * Returns the depth of the rearmost man. A blind teleport (the 10 m point was off the mesh) makes the move end as blocked.
	 */
	float PlaceColumn(UWorld* World, FState& State, const FVector& LeaderFeet, const FVector& Dir)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float WantedBack[3] = { 0.f, 450.f, 900.f };
		FVector Placed[3] = { LeaderFeet, LeaderFeet, LeaderFeet };
		float Back[3] = { 0.f, 0.f, 0.f };
		float Spread = 0.f;
		if (!Nav)
		{
			return 0.f;
		}
		for (int32 Index = 0; Index < State.Members.Num() && Index < 3; ++Index)
		{
			AOperativeCharacter* Operative = State.Members[Index].Operative.Get();
			bool bFound = false;
			for (float Try = WantedBack[Index]; Try >= 0.f && !bFound; Try -= 50.f)
			{
				FVector Feet = LeaderFeet;
				if (Try > 1.f && !ProjectBack(*Nav, LeaderFeet, Dir, Try, Feet))
				{
					continue;
				}
				bool bClose = false;
				for (int32 Prev = 0; Prev < Index; ++Prev)
				{
					bClose |= static_cast<float>(FVector::Dist2D(Feet, Placed[Prev])) < 280.f;
				}
				if (bClose)
				{
					continue;
				}
				if (Try > 40.f && Operative)
				{
					const FVector From = Feet + FVector(0.f, 0.f, 90.f);
					const FVector To = LeaderFeet + FVector(0.f, 0.f, 90.f);
					const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Operative);
					const float Straight = static_cast<float>(FVector::Dist2D(From, To));
					if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > Straight * 1.6f + 150.f)
					{
						continue;
					}
				}
				Placed[Index] = Feet;
				Back[Index] = Try;
				bFound = true;
			}
			if (Operative)
			{
				Operative->StopOperative();
				Operative->LeaveCover(TEXT("G026 smoke setup"));
				Operative->ClearPendingCover();
				Operative->SetStance(EOperativeStance::Standing);
				const float HalfHeight = Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Operative->TeleportTo(Placed[Index] + FVector(0.f, 0.f, HalfHeight + 4.f), Dir.Rotation(), false, true);
			}
			Spread = FMath::Max(Spread, Back[Index]);
			UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] column %s %.0f cm back of the stand-off"),
				Operative ? *Operative->DisplayName.ToString() : TEXT("?"), Back[Index]);
		}
		return Spread;
	}

	float MontageFraction(AOperativeCharacter& Operative)
	{
		USkeletalMeshComponent* Mesh = Operative.GetMesh();
		UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		if (!Anim || !Montage || Montage->GetPlayLength() <= KINDA_SMALL_NUMBER)
		{
			return -1.f;
		}
		return Anim->Montage_GetPosition(Montage) / Montage->GetPlayLength();
	}

	void ApplyHit(FState& State, AOperativeCharacter& Operative, int32 Index, float Fraction)
	{
		if (!Operative.HealthComponent)
		{
			return;
		}
		Operative.HealthComponent->SetMaxHealth(5000.f, true);
		FDamageSpec Spec;
		Spec.Amount = HeavyHitHp;
		Spec.ArmorPenetration = 1.f;
		Spec.AttackerSource = TEXT("G026");
		const float Dealt = Operative.HealthComponent->TakeDamage(Spec);
		State.HitDamage[Index] = Dealt;
		State.HitFraction[Index] = Fraction;
		++State.Hits;
		if (Index == 2)
		{
			if (IConsoleVariable* Chance = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
			{
				Chance->Set(1.f, ECVF_SetByConsole);
			}
			if (UKnockdownComponent* Knockdown = Operative.FindComponentByClass<UKnockdownComponent>())
			{
				State.bKnockAsked = Knockdown->TryKnockDown(EKnockdownCause::HeavyHit, Operative.GetActorLocation(), HeavyHitHp, false);
			}
		}
		NoteRetreat(State, FeetOf(Operative));
		UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] hit %d at %.0f%% of the clip: %.0f HP, mantling %d, %.0f cm along (retreat %.0f)"),
			Index + 1, Fraction * 100.f, Dealt, Operative.IsMantling() ? 1 : 0, Along(State, FeetOf(Operative)), State.WorstRetreat);
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(*State, false, State->PreloadGate.FailReason);
			return Finish(*State, false);
		default:
			break;
		}
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		if (State->Time > 180.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
			return Finish(*State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation > 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Squad || !Mantle || !PC || !Nav)
		{
			return State->Time < 60.f || Finish(*State, false);
		}
		for (FMember& Member : State->Members)
		{
			if (AOperativeCharacter* Operative = Member.Operative.Get())
			{
				Operative->ColdLevel = 0.f;
			}
		}
		AOperativeCharacter* Leader = State->Members.IsEmpty() ? nullptr : State->Members[0].Operative.Get();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad->GetLeader() || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 120.f))
			{
				return true;
			}
			SmokeUtils::StopLevelPmc(World);
			const TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Check(*State, Roster.Num() >= 3, FString::Printf(TEXT("a squad of at least 3 (%d)"), Roster.Num()));
			if (Roster.Num() < 3)
			{
				return Finish(*State, false);
			}
			State->Members.Reset();
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Roster[Index]->bTacticalCeaseFire = true;
				FMember& Member = State->Members.AddDefaulted_GetRef();
				Member.Operative = Roster[Index];
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Squad->ClearGroupSelection();
			Squad->SetLeader(State->Members[0].Operative.Get());
			Squad->SetFollowersHolding(false);
			Check(*State, USquadSubsystem::IsSquadMantleEnabled(), TEXT("Codex.Mantle.Squad is on"));
			Next(*State, 1);
			return true;
		}
		case 1:
		{
			if (!Leader)
			{
				return Finish(*State, false);
			}
			Squad->CancelSquadMantle(TEXT("G026 next site"));
			State->Site = MantleSmokeSites::FindSite(World, Leader, TEXT("fence"), 135.f, StandOffCm, Leader->GetActorLocation(), State->Used,
				true, GoalPastCm);
			if (!State->Site.bFound)
			{
				Check(*State, false, TEXT("part 1: a 135 cm fence with ground 2 m off and 8 m past it"));
				return Finish(*State, false);
			}
			const FMantleSite& Probe = State->Site.Probe;
			State->Goal = Probe.FacePoint + Probe.Dir * (Probe.Probe.DepthCm + GoalPastCm);
			State->Goal.Z = State->Site.Feet.Z;
			FNavLocation OnNav;
			const UNavigationPath* Round = Nav->ProjectPointToNavigation(State->Goal, OnNav, FVector(80.f, 80.f, 150.f))
				? Nav->FindPathToLocationSynchronously(World, State->Site.Feet, OnNav.Location, Leader, UNavFilter_OperativeNoMantle::StaticClass()) : nullptr;
			const float Direct = static_cast<float>(FVector::Dist2D(State->Site.Feet, State->Goal));
			const float RoundCm = Round && Round->IsValid() && !Round->IsPartial() ? static_cast<float>(Round->GetPathLength()) : TNumericLimits<float>::Max();
			if (!Round || RoundCm < Direct + MantleRules::GetConfig().LinkEnteringCostCm + 150.f)
			{
				State->Used.Add(State->Site.Actor.Get());
				if (++State->Retries > 12)
				{
					Check(*State, false, TEXT("part 1: a fence whose way round is longer than the mantle link"));
					return Finish(*State, false);
				}
				return true;
			}
			State->Goal = OnNav.Location;
			State->Retries = 0;
			State->Used.Add(State->Site.Actor.Get());
			PlaceLine(World, *State, State->Site.Feet, State->Site.Dir);
			Next(*State, 2);
			return true;
		}
		case 2:
		{
			if (State->StageTime < 1.5f || !Leader)
			{
				return true;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationFenceCross), false, Leader);
			for (const FMember& Member : State->Members)
			{
				Params.AddIgnoredActor(Member.Operative.Get());
			}
			const bool bHit = World->LineTraceSingleByChannel(State->Hit, State->Goal + FVector(0.f, 0.f, 200.f), State->Goal - FVector(0.f, 0.f, 400.f),
				ECC_Visibility, Params);
			if (!bHit || State->Hit.GetActor() == State->Site.Actor.Get())
			{
				Check(*State, false, FString::Printf(TEXT("part 1: the ground 8 m past the fence is clickable (hit %s)"), *GetNameSafe(State->Hit.GetActor())));
				return Finish(*State, false);
			}
			const FVector LeaderPos = FeetOf(*Leader);
			for (FMember& Member : State->Members)
			{
				AOperativeCharacter* Operative = Member.Operative.Get();
				if (!Operative)
				{
					continue;
				}
				Member.Start = FeetOf(*Operative);
				Member.Previous = Member.Start;
				Member.Travel = 0.f;
				Member.MantlesBefore = Operative->GetMantleCount();
				Member.bPendingAtClick = Operative->HasPendingCover();
				FVector Slot = FVector::ZeroVector;
				float Yaw = 0.f;
				const FVector Offset = Squad->GetFormationTarget(Operative, Slot, Yaw) ? FVector(Slot.X - LeaderPos.X, Slot.Y - LeaderPos.Y, 0.f) : FVector::ZeroVector;
				Member.Expected = State->Goal + Offset;
				Member.Expected.Z = Member.Start.Z;
				UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] %s pending cover %d, slot offset (%.0f, %.0f)"),
					*Operative->DisplayName.ToString(), Member.bPendingAtClick ? 1 : 0, Offset.X, Offset.Y);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] part 1 double click at %s (%.0f cm past the face)"),
				*State->Hit.ImpactPoint.ToCompactString(), FVector::Dist2D(State->Site.Probe.FacePoint, State->Goal));
			PC->SimulatedCursorHitForTesting = State->Hit;
			PC->HandlePlayerClick(State->Hit);
			PC->HandlePlayerClick(State->Hit);
			Next(*State, 3);
			return true;
		}
		case 3:
		{
			int32 Arrived = 0;
			for (FMember& Member : State->Members)
			{
				AOperativeCharacter* Operative = Member.Operative.Get();
				if (!Operative)
				{
					continue;
				}
				const FVector Feet = FeetOf(*Operative);
				const float StepCm = static_cast<float>(FVector::Dist2D(Feet, Member.Previous));
				if (StepCm < 800.f)
				{
					Member.Travel += StepCm;
				}
				Member.Previous = Feet;
				Arrived += FVector::Dist2D(Feet, Member.Expected) <= SlotRadiusCm && !Operative->IsMantling() ? 1 : 0;
			}
			if (Arrived < State->Members.Num() && State->StageTime < CrossLimitSeconds)
			{
				return true;
			}
			Check(*State, !Squad->IsSquadMantleActive() || Arrived == State->Members.Num(),
				FString::Printf(TEXT("part 1: squad mantle still active %d after %.1f s"), Squad->IsSquadMantleActive() ? 1 : 0, State->StageTime));
			for (FMember& Member : State->Members)
			{
				AOperativeCharacter* Operative = Member.Operative.Get();
				if (!Operative)
				{
					Check(*State, false, TEXT("part 1: operative missing"));
					continue;
				}
				// Roles are reassigned by travel during the cross, so the slot is the one he holds once the leader is at the click.
				FVector LiveSlot = Member.Expected;
				float LiveYaw = 0.f;
				if (Operative == Leader)
				{
					LiveSlot = State->Goal;
				}
				else
				{
					FVector Slot = FVector::ZeroVector;
					float Yaw = 0.f;
					if (Squad->GetFormationTarget(Operative, Slot, Yaw))
					{
						LiveSlot = Slot;
						LiveYaw = Yaw;
					}
				}
				const FVector Feet = FeetOf(*Operative);
				const float Dist = static_cast<float>(FVector::Dist2D(Feet, LiveSlot));
				const float StraightLive = static_cast<float>(FVector::Dist2D(Member.Start, LiveSlot));
				const float StraightClick = static_cast<float>(FVector::Dist2D(Member.Start, Member.Expected));
				// Places-by-travel can hand him a closer slot than the click assigned. The detour is the longer of the two straight lines.
				const float Straight = FMath::Max(100.f, FMath::Max(StraightLive, StraightClick));
				const bool bVault = Operative->GetMantleCount() > Member.MantlesBefore;
				const bool bThere = Dist <= SlotRadiusCm;
				const bool bShort = Member.Travel <= DetourLimit * Straight;
				UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] %s feet %s, live slot %s (yaw %.0f), click-time slot %s, straight live %.0f click %.0f"),
					*Operative->DisplayName.ToString(), *Feet.ToCompactString(), *LiveSlot.ToCompactString(), LiveYaw, *Member.Expected.ToCompactString(),
					StraightLive, StraightClick);
				Check(*State, bThere && bVault && bShort, FString::Printf(
					TEXT("part 1: %s within %.0f cm of his slot (%.0f), vaulted %d (mantles %+d, class %s), path %.0f cm <= %.1fx of %.0f (live %.0f, click-time %.0f), pending-at-click %d"),
					*Operative->DisplayName.ToString(), SlotRadiusCm, Dist, bVault ? 1 : 0, Operative->GetMantleCount() - Member.MantlesBefore,
					*Operative->GetLastMantleClass().ToString(), Member.Travel, DetourLimit, Straight, StraightLive, StraightClick, Member.bPendingAtClick ? 1 : 0));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] part 1 leader destination %.0f cm from the click, squad mantle done %d"),
				Leader ? FVector::Dist2D(Leader->GetLastMoveDestination(), State->Goal) : -1.f, Squad->GetSquadMantleDoneCount());
			Next(*State, 4);
			return true;
		}
		case 4:
		{
			if (!Leader)
			{
				return Finish(*State, false);
			}
			if (State->StageTime < 0.2f)
			{
				Squad->CancelSquadMantle(TEXT("G026 part 2"));
				Squad->EnterSoloMode();
				PlaceLine(World, *State, State->Site.Feet, State->Site.Dir);
				Leader->HealthComponent->SetMaxHealth(5000.f, true);
				return true;
			}
			if (State->StageTime < 1.2f)
			{
				return true;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationFenceCrossHit), false, Leader);
			Params.AddIgnoredActor(Leader);
			World->LineTraceSingleByChannel(State->Hit, State->Goal + FVector(0.f, 0.f, 200.f), State->Goal - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params);
			PC->SimulatedCursorHitForTesting = State->Hit;
			PC->HandlePlayerClick(State->Hit);
			PC->HandlePlayerClick(State->Hit);
			State->bHitsArmed = false;
			State->bHitsClosed = false;
			State->Hits = 0;
			State->bSawMantle = false;
			State->bKnockAsked = false;
			State->PeakAlong = 0.f;
			State->WorstRetreat = 0.f;
			Next(*State, 5);
			return true;
		}
		case 5:
		{
			if (!Leader)
			{
				return Finish(*State, false);
			}
			if (!State->bHitsArmed && Leader->IsMantling())
			{
				State->bHitsArmed = true;
				State->bSawMantle = true;
				State->VaultStart = FeetOf(*Leader);
				State->VaultFace = Leader->GetMantleFacePoint();
				State->VaultDir = Leader->GetMantleDir().GetSafeNormal2D();
				if (State->VaultDir.IsNearlyZero())
				{
					State->VaultDir = State->Site.Dir.GetSafeNormal2D();
				}
				State->PeakAlong = 0.f;
			}
			if (State->bHitsArmed && !State->bHitsClosed)
			{
				NoteRetreat(*State, FeetOf(*Leader));
				if (Leader->IsMantling())
				{
					const float Fraction = MontageFraction(*Leader);
					if (State->Hits < 3 && Fraction >= HitMarks[State->Hits])
					{
						ApplyHit(*State, *Leader, State->Hits, Fraction);
					}
				}
				else
				{
					State->bHitsClosed = true;
				}
			}
			if (!State->bHitsClosed && State->StageTime < 15.f)
			{
				return true;
			}
			const FVector End = FeetOf(*Leader);
			const float PastFace = static_cast<float>(FVector::DotProduct(End - State->VaultFace, State->VaultDir));
			const bool bFar = State->bSawMantle && PastFace > 15.f;
			bool bHeavy = State->Hits == 3;
			for (int32 Index = 0; Index < State->Hits && Index < 3; ++Index)
			{
				bHeavy &= State->HitDamage[Index] >= MantleRules::GetConfig().HeavyHitDamage;
			}
			Check(*State, State->bSawMantle && bHeavy && bFar && State->WorstRetreat <= RetreatLimitCm && State->bKnockAsked, FString::Printf(
				TEXT("part 2: vault %d, hits %d (%.0f/%.0f/%.0f HP at %.0f/%.0f/%.0f%%), far side %d (%.0f cm past the face), retreat %.0f cm, knockdown asked %d, cancel %d"),
				State->bSawMantle ? 1 : 0, State->Hits, State->HitDamage[0], State->HitDamage[1], State->HitDamage[2],
				State->HitFraction[0] * 100.f, State->HitFraction[1] * 100.f, State->HitFraction[2] * 100.f,
				bFar ? 1 : 0, PastFace, State->WorstRetreat, State->bKnockAsked ? 1 : 0, static_cast<int32>(Leader->GetLastMantleCancel())));
			Next(*State, 6);
			return true;
		}
		case 6:
		{
			if (!Leader || State->StageTime < 0.5f)
			{
				return true;
			}
			// Part 2 lands him and then the deferred knockdown plays. A wall order during that fall
			// ends as "the leader's next order" and the file never gets to crouch.
			if (Leader->IsKnockedDown())
			{
				if (State->StageTime < 0.55f)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] part 3 waits: the leader is still down from the vault"));
				}
				if (State->StageTime > 15.f)
				{
					Check(*State, false, TEXT("part 3: the leader is back on his feet before the cover order"));
					return Finish(*State, false);
				}
				return true;
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Squad->ClearGroupSelection();
			Squad->SetLeader(Leader);
			Squad->SetFollowersHolding(false);
			// A file on the navmesh, not a fixed 10 m teleport (that point was off the mesh and the move ended blocked).
			const float Spread = PlaceColumn(World, *State, State->Site.Feet, State->Site.Dir);
			Check(*State, Spread >= 280.f, FString::Printf(TEXT("part 3: the file is staggered on nav (rear %.0f cm back)"), Spread));
			const FVector Aim = State->Site.Probe.FacePoint + FVector(0.f, 0.f, 65.f);
			FCoverSlot Slot;
			bool bSlot = false;
			for (const FVector& TraceDir : { State->Site.Dir, -State->Site.Dir })
			{
				FCoverSlot Tried;
				if (CoverTraceRules::FindCoverSlotAt(World, Aim, TraceDir, Tried) && Tried.Height == ECoverHeight::LowCover
					&& Tried.WallActor == State->Site.Actor && CoverTraceRules::IsOnApproachSide(Tried, State->Site.Feet))
				{
					Slot = Tried;
					bSlot = true;
					break;
				}
			}
			Check(*State, bSlot, TEXT("part 3: a low-cover slot on the near face of the fence"));
			if (!bSlot)
			{
				return Finish(*State, false);
			}
			for (FMember& Member : State->Members)
			{
				Member.ArrivedAt = -1.f;
				Member.CrouchedAt = -1.f;
			}
			PC->ShowCoverPreview(Leader, Slot);
			PC->ConfirmCoverPreview(true);
			Next(*State, 7);
			return true;
		}
		case 7:
		{
			if (State->StageTime < 0.3f)
			{
				return true;
			}
			const TArray<FFormationSlot>& Slots = Squad->GetFormationWallSlots();
			bool bPending = false;
			for (FMember& Member : State->Members)
			{
				AOperativeCharacter* Operative = Member.Operative.Get();
				if (!Operative)
				{
					continue;
				}
				EFormationRole Role = EFormationRole::Commander;
				const bool bRole = Squad->GetFormationRole(Operative, Role) || Operative == Leader;
				const FFormationSlot* Slot = bRole ? Slots.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; }) : nullptr;
				if (!Slot)
				{
					bPending = true;
					continue;
				}
				const float Radius = Operative == Leader ? LeaderArriveCm : FollowerArriveCm;
				const float Dist = static_cast<float>(FVector2D::Distance(FVector2D(Operative->GetActorLocation()), Slot->Position));
				if (Member.ArrivedAt < 0.f && Dist <= Radius && !Operative->IsMoving())
				{
					Member.ArrivedAt = State->StageTime;
					UE_LOG(LogCodexTactics, Display, TEXT("[FormationFenceCross] %s arrived at his slot (%.0f cm, yaw %.0f) at %.2f s"),
						*Operative->DisplayName.ToString(), Dist, Operative->GetActorRotation().Yaw, State->StageTime);
				}
				if (Member.CrouchedAt < 0.f && Operative->GetStance() == EOperativeStance::Crouching)
				{
					Member.CrouchedAt = State->StageTime;
				}
				if (Member.ArrivedAt < 0.f || (Member.CrouchedAt < 0.f && State->StageTime < Member.ArrivedAt + CrouchLimitSeconds))
				{
					bPending = true;
				}
			}
			if ((bPending || !Squad->HasFormationWallOrder()) && State->StageTime < CrossLimitSeconds)
			{
				return true;
			}
			Check(*State, Squad->HasFormationWallOrder(), TEXT("part 3: the confirm is a formation wall order"));
			for (const FMember& Member : State->Members)
			{
				const AOperativeCharacter* Operative = Member.Operative.Get();
				if (!Operative)
				{
					Check(*State, false, TEXT("part 3: operative missing"));
					continue;
				}
				const bool bCrouched = Member.CrouchedAt >= 0.f && Member.ArrivedAt >= 0.f && Member.CrouchedAt <= Member.ArrivedAt + CrouchLimitSeconds;
				Check(*State, bCrouched, FString::Printf(TEXT("part 3: %s crouched within %.1f s of his own arrival (arrived %.2f, crouched %.2f, stance %d)"),
					*Operative->DisplayName.ToString(), CrouchLimitSeconds, Member.ArrivedAt, Member.CrouchedAt, static_cast<int32>(Operative->GetStance())));
			}
			return Finish(*State, true);
		}
		default:
			return Finish(*State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationFenceCrossSmoke"),
		TEXT("G026: formation fast-cross of an X6 fence, hits during the vault, crouch on each arrival. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
