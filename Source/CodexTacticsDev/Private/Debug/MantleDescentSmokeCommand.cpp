// Dev-only console command: coming DOWN off a mantle top on L_Gen_Depot_1 (designer bug report 2026-11-16, batch 2, items 5-8:
// MantlingProblem_03 "always jumps down on the side he climbed from", _05 "a strange animation, sinks into the obstacle on the way down",
// _06 "stuck on top, cannot get down at all", _07 "prone on top, ordered down -> teleported to the ground"; docs/port/MANTLING_BATCH2_REPORT.md):
//   Scripts/smoke_parallel.ps1 -Commands MantleDescentSmoke -ExtraArgs -Multiprocess
// Per case one Solo operative climbs a top from one face (TryStartMantle, as MantleAlignSmoke), settles on it (crouched; the prone cases go
// prone), then a ground click (the controller's HandleWorldHit) orders him to a point beyond the OPPOSITE face (the parapet: back on the near
// side - its far side is the canal). Every frame his capsule and torso joints are sampled. Checks (mantle.json align.* / descent.*):
//   arrive    he reaches the goal within descent.arrive_limit_s (no soft-lock on the top);
//   side      he leaves the top over the face nearer the goal (the far face; not the one he climbed);
//   clip      the torso joints below the top stay align.min_body_gap_cm outside the obstacle's footprint;
//   teleport  the capsule never moves faster than descent.max_speed_cm_s between two frames;
//   stance    (prone cases) he is standing when he leaves the top;
//   anim      the descent plays the fall states (FallStates: airborne / landing), not the vault-over clip.
// Enemies are parked far away.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationSystem.h"

namespace MantleDescentSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** He stands this far out from the face he climbs (the click order's base point). */
	constexpr float ClimbStandOffCm = 80.f;
	/** The goal lies this far out from the face it is meant to be reached over. */
	constexpr float GoalOffCm = 250.f;
	/** Settling on the top after the climb / after going prone, s. */
	constexpr float SettleSeconds = 1.5f;
	/** The feet this far under the top = he has left it. */
	constexpr float LeftTopCm = 30.f;

	struct FCase
	{
		const TCHAR* Label;
		const TCHAR* Kind;
		float HeightCm;
		/** The goal beyond the opposite face (else back on the climbed face's side). */
		bool bFarSide;
		bool bProne;
		/** Mantle links of this obstacle switched off once he is on top: 0 none, 1 those over the FAR face (its only way down on the navmesh is
		 *  the climbed face's down link: MantlingProblem_03), 2 all (an island top: MantlingProblem_06 / 07). */
		int32 LinksOff = 0;
	};

	static const FCase Cases[] = {
		{ TEXT("85 cm crate"), TEXT("crate"), 85.f, true, false },
		{ TEXT("140 cm crate"), TEXT("crate"), 140.f, true, false },
		{ TEXT("140 cm wreck"), TEXT("wreck"), 140.f, true, false },
		{ TEXT("140 cm crate, prone on top"), TEXT("crate"), 140.f, true, true },
		{ TEXT("85 cm crate, prone on top"), TEXT("crate"), 85.f, true, true },
		{ TEXT("230 cm parapet, back down"), TEXT("cut_wall"), 230.f, false, false },
		{ TEXT("140 cm wreck, far-face links off"), TEXT("wreck"), 140.f, true, false, 1 },
		{ TEXT("140 cm wreck, an island top (its links off)"), TEXT("wreck"), 140.f, true, false, 2 },
		{ TEXT("140 cm wreck island, prone on top"), TEXT("wreck"), 140.f, true, true, 2 },
	};

	struct FSample
	{
		float Time = 0.f;
		FVector Feet = FVector::ZeroVector;
		TArray<FVector> Torso;
		EOperativeStance Stance = EOperativeStance::Standing;
		bool bFallAnim = false;
		bool bVaulting = false;
		bool bMantling = false;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Case = 0;
		int32 Failures = 0;
		MantleSmokeSites::FSite Site;
		FVector Goal = FVector::ZeroVector;
		int32 VaultClipsBefore = 0;
		bool bSampling = false;
		float SampleClock = 0.f;
		float ArrivedAt = -1.f;
		TArray<FSample> Samples;
		TSet<const AActor*> Used;
		FTSTicker::FDelegateHandle SampleTicker;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(State.SampleTicker);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleDescentSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector FeetOf(const AOperativeCharacter& Leader)
	{
		return Leader.GetActorLocation() - FVector(0.f, 0.f, Leader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	bool Sample(TSharedRef<FState> State, float DeltaTime)
	{
		AOperativeCharacter* Leader = State->Leader.Get();
		if (!State->bSampling || !Leader)
		{
			return true;
		}
		FSample Each;
		Each.Time = State->SampleClock;
		State->SampleClock += DeltaTime;
		Each.Feet = FeetOf(*Leader);
		Each.Torso = MantleSmokeSites::TorsoJoints(*Leader->GetMesh());
		Each.Stance = Leader->GetStance();
		Each.bVaulting = Leader->IsVaulting();
		Each.bMantling = Leader->IsMantling();
		if (const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()))
		{
			Each.bFallAnim = Anim->bFallAirborne || Anim->bFallLanding;
		}
		State->Samples.Add(Each);
		return true;
	}

	/** A ground click at Point through the controller's dispatcher (the mouse's path after the cursor trace). */
	bool ClickGround(UWorld* World, const FVector& Point)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		FHitResult Hit;
		if (!PC || !World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 150.f), Point - FVector(0.f, 0.f, 150.f), ECC_Visibility))
		{
			return false;
		}
		PC->HandleWorldHit(Hit);
		return true;
	}

	void Measure(FState& State, const FCase& Case, const AOperativeCharacter& Leader)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		const AActor* Obstacle = State.Site.Actor.Get();
		const TArray<FSample>& Samples = State.Samples;
		if (!Obstacle || Samples.Num() < 3)
		{
			Check(State, false, FString::Printf(TEXT("%s: sampled the descent (%d frames)"), Case.Label, Samples.Num()));
			return;
		}
		const float TopZ = MantleSmokeSites::TopOf(*Obstacle);
		FVector Centre;
		FVector Extent;
		Obstacle->GetActorBounds(false, Centre, Extent);
		// The trace (every 6th frame): t, feet (along the climb from the obstacle's centre, z under the top), stance, fall anim / vault / mantle.
		UE_LOG(LogCodexTactics, Display, TEXT("[MantleDescentSmoke] %s trace (t | feet along z-top | torso gap | stance | fall vault mantle):"), Case.Label);
		for (int32 Index = 0; Index < Samples.Num(); Index += 6)
		{
			const FSample& S = Samples[Index];
			float Gap = TNumericLimits<float>::Max();
			for (const FVector& Joint : S.Torso)
			{
				Gap = FMath::Min(Gap, MantleSmokeSites::FootprintGap(*Obstacle, Joint, Config.AlignGapAboveMarginCm));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleDescentSmoke]   %.2f | %6.1f %6.1f | %6.1f | %d | %d %d %d"), S.Time,
				FVector::DotProduct(S.Feet - Centre, State.Site.Dir), S.Feet.Z - TopZ, Gap >= TNumericLimits<float>::Max() ? 999.f : Gap,
				static_cast<int32>(S.Stance), S.bFallAnim ? 1 : 0, S.bVaulting ? 1 : 0, S.bMantling ? 1 : 0);
		}
		// 1. Arrival.
		Check(State, State.ArrivedAt >= 0.f, FString::Printf(TEXT("%s: the order from the top reaches its ground goal (%s, %.1f cm off at the end; limit %.0f s)"),
			Case.Label, State.ArrivedAt >= 0.f ? *FString::Printf(TEXT("in %.1f s"), State.ArrivedAt) : TEXT("NOT reached"),
			FVector::Dist2D(Samples.Last().Feet, State.Goal), Config.DescentArriveLimitSeconds));
		// 2. The side: where his feet first are under the top.
		const int32 Left = Samples.IndexOfByPredicate([TopZ](const FSample& S) { return S.Feet.Z < TopZ - LeftTopCm; });
		if (Left == INDEX_NONE)
		{
			Check(State, false, FString::Printf(TEXT("%s: he left the top"), Case.Label));
		}
		else
		{
			const float Along = static_cast<float>(FVector::DotProduct(Samples[Left].Feet - Centre, State.Site.Dir));
			const bool bFar = Along > 0.f;
			Check(State, bFar == Case.bFarSide, FString::Printf(TEXT("%s: he comes down over the face nearer the goal (%s side, %.0f cm along the climb from the centre at %.2f s; wanted the %s side)"),
				Case.Label, bFar ? TEXT("far") : TEXT("climbed"), Along, Samples[Left].Time, Case.bFarSide ? TEXT("far") : TEXT("climbed")));
			if (Case.bProne)
			{
				// The stance as he leaves: the last sample still on the top.
				const EOperativeStance AtEdge = Samples[FMath::Max(0, Left - 1)].Stance;
				Check(State, AtEdge == EOperativeStance::Standing, FString::Printf(TEXT("%s: he stands up before he comes down (stance at the edge %s)"), Case.Label,
					*UEnum::GetValueAsString(AtEdge)));
			}
		}
		// 3. Clipping.
		float WorstGap = TNumericLimits<float>::Max();
		float WorstAt = 0.f;
		for (const FSample& S : Samples)
		{
			for (const FVector& Joint : S.Torso)
			{
				const float Gap = MantleSmokeSites::FootprintGap(*Obstacle, Joint, Config.AlignGapAboveMarginCm);
				if (Gap < WorstGap)
				{
					WorstGap = Gap;
					WorstAt = S.Time;
				}
			}
		}
		Check(State, WorstGap >= Config.AlignMinBodyGapCm, FString::Printf(
			TEXT("%s: no clipping on the way down - the torso's smallest gap to the footprint below the top %.1f cm (at %.2f s; minimum %.0f)"), Case.Label,
			WorstGap >= TNumericLimits<float>::Max() ? 999.f : WorstGap, WorstAt, Config.AlignMinBodyGapCm));
		// 4. No teleport.
		float Fastest = 0.f;
		float FastestAt = 0.f;
		for (int32 Index = 1; Index < Samples.Num(); ++Index)
		{
			const float Dt = Samples[Index].Time - Samples[Index - 1].Time;
			if (Dt <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const float Speed = static_cast<float>(FVector::Dist(Samples[Index].Feet, Samples[Index - 1].Feet)) / Dt;
			if (Speed > Fastest)
			{
				Fastest = Speed;
				FastestAt = Samples[Index].Time;
			}
		}
		Check(State, Fastest <= Config.DescentMaxSpeedCmPerSecond, FString::Printf(TEXT("%s: no teleport - the fastest frame %.0f cm/s (at %.2f s; limit %.0f)"),
			Case.Label, Fastest, FastestAt, Config.DescentMaxSpeedCmPerSecond));
		// 5. The descent's animation: the fall states, not the vault-over clip.
		const bool bFallAnim = Samples.ContainsByPredicate([](const FSample& S) { return S.bFallAnim; });
		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader.GetMesh()->GetAnimInstance());
		const int32 VaultsPlayed = Anim ? Anim->GetVaultClipsPlayed() - State.VaultClipsBefore : 0;
		Check(State, bFallAnim && VaultsPlayed == 0, FString::Printf(TEXT("%s: the descent plays the fall states (%s), not the vault-over clip (%d played)"), Case.Label,
			bFallAnim ? TEXT("seen") : TEXT("NOT seen"), VaultsPlayed));
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
		if (State->Time > 500.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d, case %d"), State->Stage, State->Case));
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
		AOperativeCharacter* Leader = State->Leader.Get();
		const FMantleConfig& Config = MantleRules::GetConfig();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 90.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Leader = State->Leader.Get();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			Leader->bTacticalCeaseFire = true;
			Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State->SampleTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime) { return Sample(State, DeltaTime); }));
			Next(*State, 1);
			return true;
		}
		case 1: // a site: place him in front of its face
		{
			if (State->Case >= UE_ARRAY_COUNT(Cases))
			{
				return Finish(*State, true);
			}
			const FCase& Case = Cases[State->Case];
			State->Site = MantleSmokeSites::FindSite(World, Leader, Case.Kind, Case.HeightCm, ClimbStandOffCm, Leader->GetActorLocation(), State->Used,
				Case.bFarSide, GoalOffCm);
			if (!State->Site.bFound)
			{
				Check(*State, false, FString::Printf(TEXT("%s: a test site on the map"), Case.Label));
				++State->Case;
				return true;
			}
			State->Used.Add(State->Site.Actor.Get());
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			Leader->TeleportTo(State->Site.Feet + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), State->Site.Dir.Rotation(),
				false, true);
			Next(*State, 2);
			return true;
		}
		case 2: // climb up
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			const FCase& Case = Cases[State->Case];
			EMantleRefusal Refusal = EMantleRefusal::None;
			State->ArrivedAt = -1.f;
			const bool bStarted = Leader->TryStartMantle(State->Site.Dir, &Refusal, true);
			Check(*State, bStarted, FString::Printf(TEXT("%s: the climb up starts (%s, %s)"), Case.Label, *GetNameSafe(State->Site.Actor.Get()),
				bStarted ? *Leader->GetLastMantleClip() : MantleRules::RefusalName(Refusal)));
			if (!bStarted)
			{
				++State->Case;
				Next(*State, 1);
				return true;
			}
			Next(*State, 3);
			return true;
		}
		case 3: // on top: settle (prone cases go prone)
		{
			const FCase& Case = Cases[State->Case];
			if (Leader->IsMantling() && State->StageTime < 12.f)
			{
				return true;
			}
			if (State->ArrivedAt < 0.f)
			{
				State->ArrivedAt = State->StageTime; // (reused: the climb's end)
			}
			if (State->StageTime - State->ArrivedAt < SettleSeconds)
			{
				return true;
			}
			if (Case.LinksOff > 0 && Mantle)
			{
				int32 Off = 0;
				for (int32 Index = 0; Index < Mantle->GetLinks().Num(); ++Index)
				{
					const FMantleLink& Link = Mantle->GetLinks()[Index];
					if (Link.Obstacle.Get() != State->Site.Actor.Get())
					{
						continue;
					}
					// The far face: climbed from the far side (up, into the obstacle against our climb) or left over it (down, outward along it).
					const float Along = static_cast<float>(FVector::DotProduct(Link.Dir, State->Site.Dir));
					const bool bFarFace = Link.bDown ? Along > 0.7f : Along < -0.7f;
					if (Case.LinksOff == 2 || bFarFace)
					{
						Mantle->DisableLink(Index, TEXT("MantleDescentSmoke"));
						++Off;
					}
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[MantleDescentSmoke] %s: %d links of %s switched off"), Case.Label, Off, *GetNameSafe(State->Site.Actor.Get()));
			}
			if (Case.bProne && Leader->GetStance() != EOperativeStance::Prone)
			{
				Leader->SetStance(EOperativeStance::Prone);
				Next(*State, 4);
				return true;
			}
			Next(*State, 5);
			return true;
		}
		case 4: // prone: settle
		{
			if (State->StageTime < SettleSeconds)
			{
				return true;
			}
			Next(*State, 5);
			return true;
		}
		case 5: // the ground click beyond the opposite face (or back on the near side)
		{
			const FCase& Case = Cases[State->Case];
			const FMantleSite& Probe = State->Site.Probe;
			const FVector Feet = FeetOf(*Leader);
			Check(*State, Feet.Z > MantleSmokeSites::TopOf(*State->Site.Actor.Get()) - LeftTopCm, FString::Printf(TEXT("%s: on the top before the order (feet %.0f cm under it, stance %s)"),
				Case.Label, MantleSmokeSites::TopOf(*State->Site.Actor.Get()) - Feet.Z, *UEnum::GetValueAsString(Leader->GetStance())));
			State->Goal = Case.bFarSide ? Probe.FacePoint + Probe.Dir * (Probe.Probe.DepthCm + GoalOffCm) : Probe.FacePoint - Probe.Dir * GoalOffCm;
			State->Goal.Z = State->Site.Feet.Z;
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				FNavLocation OnNav;
				if (Nav->ProjectPointToNavigation(State->Goal, OnNav, FVector(60.f, 60.f, 100.f)))
				{
					State->Goal = OnNav.Location;
				}
			}
			State->Samples.Reset();
			State->SampleClock = 0.f;
			State->ArrivedAt = -1.f;
			State->bSampling = true;
			if (const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()))
			{
				State->VaultClipsBefore = Anim->GetVaultClipsPlayed();
			}
			const bool bClicked = ClickGround(World, State->Goal);
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleDescentSmoke] %s: on %s, a ground click at %s (%s), he has an own move order: %d"), Case.Label,
				*GetNameSafe(State->Site.Actor.Get()), *State->Goal.ToCompactString(), bClicked ? TEXT("clicked") : TEXT("NO ground under it"), Leader->HasOwnMoveOrder() ? 1 : 0);
			Next(*State, 6);
			return true;
		}
		case 6: // until he arrives (or the limit)
		{
			const FVector Feet = FeetOf(*Leader);
			const bool bThere = FVector::Dist2D(Feet, State->Goal) <= Config.DescentArriveRadiusCm && FMath::Abs(Feet.Z - State->Goal.Z) < 60.f;
			if (bThere && State->ArrivedAt < 0.f)
			{
				State->ArrivedAt = State->StageTime;
			}
			if ((State->ArrivedAt < 0.f || State->StageTime - State->ArrivedAt < 0.5f) && State->StageTime < Config.DescentArriveLimitSeconds)
			{
				return true;
			}
			State->bSampling = false;
			Measure(*State, Cases[State->Case], *Leader);
			++State->Case;
			Next(*State, 1);
			return true;
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
		TEXT("CodexTactics.MantleDescentSmoke"),
		TEXT("Dev check (mantle descent, designer bug report 2026-11-16 batch 2, L_Gen_Depot_1): from the top of an 85 / 140 crate, a 140 wreck (also prone) and ")
		TEXT("the 230 parapet a ground click is reached in time, over the face nearer the goal, without clipping, teleport or the vault clip; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
