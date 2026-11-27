// Dev-only headless check of Task 144 (wall hug along a wall with windows) on L_Gen_Depot_1_X6 (B_x1_10_t7_store, street x1_10 side):
//  1. Shimmy along a hugged wall past 2 windows: crouched under both (+ margin), standing between and after them.
//  2. Window cover slot + autonomy fire modes (PAS holds, AGG fires automatically through the opening, breaking intact glass).
//  3. Window edge = corner: standing beside a window frame -> CornerAim peeks round the frame and hits target outside, returns behind wall when calm.
//  4. Blind fire under the sill: crouched under the window in BlindFire mode -> stays crouched, shots leave over the sill and hit target.
//  5. PMC reuse: hostile PMC fighter in a window slot shares the same AOperativeCharacter window cover behavior.
//
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1_X6 -game -nullrhi -nosound -LevelJson=L_Gen_Depot_1_X6.json
//     -ExecCmds="CodexTactics.WallHugWindowSmoke"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/WindowBreakage.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/WindowCoverRules.h"

namespace WallHugWindowSmoke
{
	constexpr float TimeoutSeconds = 120.f;
	constexpr float PmcStartHp = 500.f;

	enum class EStage : uint8
	{
		Setup,
		ShimmyPastWindows,
		FireModePas,
		FireModeAgg,
		WindowEdgeCorner,
		WindowEdgeReturn,
		UnderSillBlindFire,
		PmcReuse,
		Done
	};

	struct FWindowPair
	{
		bool bFound = false;
		TWeakObjectPtr<AActor> Sill1;
		TWeakObjectPtr<AActor> Sill2;
		FBox Box1;
		FBox Box2;
		/** Outward normal on the hug side (street x1_10 or interior) and opposite normal. */
		FVector HugNormal = FVector(0.f, -1.f, 0.f);
		FVector EnemySideNormal = FVector(0.f, 1.f, 0.f);
		FCoverSlot SlotBefore1;
		FCoverSlot SlotWin1;
		FCoverSlot SlotBetween;
		FCoverSlot SlotWin2;
		FCoverSlot SlotAfter2;
		FCoverSlot SlotFrameEdge1;
		FVector EnemySpot1 = FVector::ZeroVector;
		FVector EnemySpot2 = FVector::ZeroVector;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Gate;
		EStage Stage = EStage::Setup;
		float TotalTime = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bDone = false;

		FWindowPair Setup;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> Target;

		bool bCrouchedWin1 = false;
		bool bStoodBetween = false;
		bool bCrouchedWin2 = false;
		bool bShimmyStarted = false;
		float StandHeadZBefore = 0.f;
		float MinHeadZWin1 = FLT_MAX;
		float MaxHeadZBetween = -FLT_MAX;
		float MinHeadZWin2 = FLT_MAX;
		int32 PasShotsBefore = 0;
		bool bSawEdgeCornerAim = false;
		float EdgeTargetHpBefore = 0.f;
		FVector EdgeTuckedHead = FVector::ZeroVector;
		float EdgeMaxPeekReach = 0.f;
		bool bEdgeReturnedPose = false;
		float EdgeReturnWaitTime = -1.f;
		int32 BlindShotsBefore = 0;
		float BlindHpBefore = 0.f;
		float BlindMaxHeadZ = -FLT_MAX;
		float BlindMaxHandZ = -FLT_MAX;
		float BlindMaxMuzzleZ = -FLT_MAX;
		bool bBlindPoseSampled = false;
	};

	void SamplePose(AOperativeCharacter& Op, FVector& OutHead, FVector& OutPelvis, FVector& OutHand)
	{
		if (USkeletalMeshComponent* Mesh = Op.GetMesh())
		{
			Mesh->RefreshBoneTransforms();
			OutHead = Mesh->GetBoneLocation(TEXT("head"));
			OutPelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
			OutHand = Mesh->GetBoneLocation(TEXT("hand_r"));
			return;
		}
		OutHead = OutPelvis = OutHand = Op.GetActorLocation();
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), (State.Failures == 0 && bComplete) ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("WallHugWindowSmoke"));
		return false;
	}

	void Next(FState& State, EStage NewStage)
	{
		State.Stage = NewStage;
		State.StageTime = 0.f;
	}

	void PlaceOnSpot(AOperativeCharacter* Op, const FVector& FeetSpot, float Yaw)
	{
		if (!Op)
		{
			return;
		}
		const float Half = Op->GetCapsuleComponent() ? Op->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
		Op->StopOperative();
		Op->TeleportTo(FVector(FeetSpot.X, FeetSpot.Y, FeetSpot.Z + Half + 2.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (USkeletalMeshComponent* Mesh = Op->GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		Op->ColdLevel = 0.f;
	}

	bool EnterSlot(AOperativeCharacter& Op, const FCoverSlot& Slot)
	{
		Op.LeaveCover(TEXT("WallHugWindowSmoke reset"));
		PlaceOnSpot(&Op, Slot.WorldLocation, static_cast<float>(Slot.WallNormal.Rotation().Yaw));
		Op.EnterCover(Slot);
		return Op.bInCover;
	}

	FWindowPair FindB_x1_10_Windows(UWorld& World, const TArray<const AActor*>& Ignored)
	{
		FWindowPair Out;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		TArray<AActor*> AllSills;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("window_sill"))))
			{
				AllSills.Add(*It);
			}
		}
		// Prefer B_x1_10_t7_store West wall (Y ~ -11701, X in [-11800, -7600], ground floor).
		TArray<AActor*> StreetSills;
		for (AActor* Sill : AllSills)
		{
			const FBox Box = Sill->GetComponentsBoundingBox(true);
			if (!Box.IsValid)
			{
				continue;
			}
			const FVector C = Box.GetCenter();
			if (C.X >= -11800.f && C.X <= -7600.f && C.Y >= -12000.f && C.Y <= -11400.f && C.Z < 350.f)
			{
				StreetSills.Add(Sill);
			}
		}
		if (StreetSills.Num() < 2)
		{
			StreetSills = AllSills;
		}
		StreetSills.Sort([](const AActor& A, const AActor& B)
		{
			return A.GetComponentsBoundingBox(true).GetCenter().X < B.GetComponentsBoundingBox(true).GetCenter().X;
		});

		for (int32 I = 0; I < StreetSills.Num(); ++I)
		{
			for (int32 J = I + 1; J < StreetSills.Num(); ++J)
			{
				AActor* A = StreetSills[I];
				AActor* B = StreetSills[J];
				const FBox BoxA = A->GetComponentsBoundingBox(true);
				const FBox BoxB = B->GetComponentsBoundingBox(true);
				const FVector CA = BoxA.GetCenter();
				const FVector CB = BoxB.GetCenter();
				if (FMath::Abs(CA.Z - CB.Z) > 100.f)
				{
					continue;
				}
				const FVector ExtentA = BoxA.GetExtent();
				const FVector Normal = ExtentA.X <= ExtentA.Y ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
				const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
				const float PlaneOff = FMath::Abs(FVector::DotProduct(CB - CA, Normal));
				const float GapAlong = FMath::Abs(FVector::DotProduct(CB - CA, Tangent));
				if (PlaneOff > 40.f || GapAlong < 260.f || GapAlong > 550.f)
				{
					continue;
				}
				// Try street side (-Y on B_x1_10_t7_store_W) and interior side (+Y) for hugging & enemy outside.
				for (const float Sign : { -1.f, 1.f })
				{
					const FVector HugNorm = Normal * Sign;
					const FVector Along = (CB - CA).GetSafeNormal2D();
					auto ProbeAt = [&](const FVector& Point, FCoverSlot& OutSlot)
					{
						return CoverTraceRules::FindCoverSlotAt(&World, Point + HugNorm * 30.f, -HugNorm, OutSlot, FCoverTraceConfig(), Ignored)
							&& OutSlot.IsValid();
					};
					FCoverSlot Before, Win1, Mid, Win2, After, Frame;
					const FVector PBefore = CA - Along * 180.f;
					const FVector PWin1 = CA;
					const FVector PMid = (CA + CB) * 0.5f;
					const FVector PWin2 = CB;
					const FVector PAfter = CB + Along * 180.f;
					const float HalfWidth1 = FMath::Abs(FVector::DotProduct(BoxA.GetExtent(), Along));
					const FVector PFrame = CA - Along * (HalfWidth1 + WindowBreakage::OpeningPadCm + 34.f);
					if (!ProbeAt(PBefore, Before) || !ProbeAt(PWin1, Win1) || !ProbeAt(PMid, Mid)
						|| !ProbeAt(PWin2, Win2) || !ProbeAt(PAfter, After) || !ProbeAt(PFrame, Frame))
					{
						continue;
					}
					if (!CoverTraceRules::IsSameWall(Before, After) || !CoverTraceRules::IsSameWall(Before, Mid))
					{
						continue;
					}
					// Find walkable nav spots on the far side of Win1 and Win2 for the enemy target.
					const FVector Far1 = CA - HugNorm * 350.f;
					const FVector Far2 = CB - HugNorm * 350.f;
					FNavLocation Nav1, Nav2;
					if (Nav && (!Nav->ProjectPointToNavigation(FVector(Far1.X, Far1.Y, Before.WorldLocation.Z + 20.f), Nav1, FVector(120.f, 120.f, 150.f))
						|| !Nav->ProjectPointToNavigation(FVector(Far2.X, Far2.Y, Before.WorldLocation.Z + 20.f), Nav2, FVector(120.f, 120.f, 150.f))))
					{
						continue;
					}
					Out.bFound = true;
					Out.Sill1 = A;
					Out.Sill2 = B;
					Out.Box1 = BoxA;
					Out.Box2 = BoxB;
					Out.HugNormal = HugNorm;
					Out.EnemySideNormal = -HugNorm;
					Out.SlotBefore1 = Before;
					Out.SlotWin1 = Win1;
					Out.SlotBetween = Mid;
					Out.SlotWin2 = Win2;
					Out.SlotAfter2 = After;
					Out.SlotFrameEdge1 = Frame;
					Out.EnemySpot1 = Nav ? Nav1.Location : FVector(Far1.X, Far1.Y, Before.WorldLocation.Z);
					Out.EnemySpot2 = Nav ? Nav2.Location : FVector(Far2.X, Far2.Y, Before.WorldLocation.Z);
					return Out;
				}
			}
		}
		return Out;
	}

	AOperativeCharacter* SpawnTargetPmc(UPmcSquadSubsystem& Pmc, const FVector& FeetSpot, int32 TeamId)
	{
		AOperativeCharacter* Fighter = Pmc.SpawnFighter(TeamId, FeetSpot, FRotator::ZeroRotator, 144, 0);
		if (Fighter && Fighter->HealthComponent)
		{
			Fighter->HealthComponent->RestoreHealth(PmcStartHp, PmcStartHp);
			Fighter->bTacticalCeaseFire = true;
			Fighter->CustomTimeDilation = 0.f;
		}
		return Fighter;
	}

	bool Step(FState& State, float DeltaTime)
	{
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		switch (State.Gate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(State, false, State.Gate.FailReason);
			return Finish(State, false);
		default: break;
		}

		State.TotalTime += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.TotalTime > TimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}

		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Squad || !Pmc || Squad->GetMembers().Num() < 1)
		{
			if (State.TotalTime < 15.f)
			{
				return true;
			}
			Check(State, false, TEXT("required subsystems and squad leader"));
			return Finish(State, false);
		}

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (State.StageTime < 2.f)
			{
				for (AOperativeCharacter* M : Squad->GetMembers())
				{
					M->bTacticalCeaseFire = true;
				}
				return true;
			}
			SmokeUtils::StopLevelPmc(World);
			if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
			{
				Pressure->Set(0, ECVF_SetByConsole);
			}
			if (IConsoleVariable* Ammo = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.InfiniteAmmo")))
			{
				Ammo->Set(0, ECVF_SetByConsole);
			}
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.Leader = Members[0];
			TArray<const AActor*> Ignored;
			for (AOperativeCharacter* M : Members)
			{
				M->bTacticalCeaseFire = true;
				M->ColdLevel = 0.f;
				Ignored.Add(M);
			}
			State.Setup = FindB_x1_10_Windows(*World, Ignored);
			Check(State, State.Setup.bFound, TEXT("found 2 adjacent windows on X6 B_x1_10 wall"));
			if (!State.Setup.bFound)
			{
				return Finish(State, false);
			}
			for (int32 I = 1; I < Members.Num(); ++I)
			{
				PlaceOnSpot(Members[I], State.Setup.SlotBefore1.WorldLocation + State.Setup.HugNormal * (300.f + 120.f * I), 0.f);
			}
			AOperativeCharacter* Leader = State.Leader.Get();
			Leader->SwitchToWeaponById(TEXT("m16"));
			Leader->CurrentClip = 30;
			Leader->ReserveAmmo = 120;
			// Ensure solid wall height (HighCover) so standing/crouching shimmy transitions are exercised cleanly.
			State.Setup.SlotBefore1.Height = ECoverHeight::HighCover;
			State.Setup.SlotAfter2.Height = ECoverHeight::HighCover;
			State.Setup.SlotFrameEdge1.Height = ECoverHeight::HighCover;
			Check(State, EnterSlot(*Leader, State.Setup.SlotBefore1), TEXT("leader entered cover on solid wall before window 1"));
			Check(State, Leader->GetStance() == EOperativeStance::Standing && !Leader->IsWindowCoverConfirmed(),
				TEXT("starts standing on solid wall before window 1"));
			FVector InitHead, InitPelvis, InitHand;
			SamplePose(*Leader, InitHead, InitPelvis, InitHand);
			State.StandHeadZBefore = static_cast<float>(InitHead.Z);
			const EOperativeOrderResult ShimmyResult = Leader->OrderShimmyTo(State.Setup.SlotAfter2);
			Check(State, ShimmyResult == EOperativeOrderResult::Accepted, TEXT("shimmy order past 2 windows accepted"));
			State.bShimmyStarted = ShimmyResult == EOperativeOrderResult::Accepted;
			Next(State, EStage::ShimmyPastWindows);
			return true;
		}
		case EStage::ShimmyPastWindows:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Leader)
			{
				return Finish(State, false);
			}
			const FVector Here = Leader->GetActorLocation();
			FVector Head, Pelvis, Hand;
			SamplePose(*Leader, Head, Pelvis, Hand);
			if (FVector::Dist2D(Here, State.Setup.SlotWin1.WorldLocation) <= 55.f && Leader->GetStance() == EOperativeStance::Crouching)
			{
				State.bCrouchedWin1 = true;
				State.MinHeadZWin1 = FMath::Min(State.MinHeadZWin1, static_cast<float>(Head.Z));
			}
			if (FVector::Dist2D(Here, State.Setup.SlotBetween.WorldLocation) <= 55.f && Leader->GetStance() == EOperativeStance::Standing)
			{
				State.bStoodBetween = true;
				State.MaxHeadZBetween = FMath::Max(State.MaxHeadZBetween, static_cast<float>(Head.Z));
			}
			if (FVector::Dist2D(Here, State.Setup.SlotWin2.WorldLocation) <= 55.f && Leader->GetStance() == EOperativeStance::Crouching)
			{
				State.bCrouchedWin2 = true;
				State.MinHeadZWin2 = FMath::Min(State.MinHeadZWin2, static_cast<float>(Head.Z));
			}
			if (!Leader->bShimmying && State.StageTime > 0.5f)
			{
				Check(State, State.bCrouchedWin1 && State.MinHeadZWin1 <= State.StandHeadZBefore - 15.f,
					FString::Printf(TEXT("crouched while shimmying under window 1 (head Z %.0f vs stand %.0f, sill %.0f)"),
						State.MinHeadZWin1, State.StandHeadZBefore, State.Setup.Box1.Max.Z));
				Check(State, State.bStoodBetween && State.MaxHeadZBetween >= State.MinHeadZWin1 + 15.f,
					FString::Printf(TEXT("stood up in solid wall gap between windows (head Z %.0f)"), State.MaxHeadZBetween));
				Check(State, State.bCrouchedWin2 && State.MinHeadZWin2 <= State.MaxHeadZBetween - 15.f,
					FString::Printf(TEXT("crouched while shimmying under window 2 (head Z %.0f, sill %.0f)"),
						State.MinHeadZWin2, State.Setup.Box2.Max.Z));
				Check(State, Leader->GetStance() == EOperativeStance::Standing && Head.Z >= State.MinHeadZWin2 + 15.f,
					FString::Printf(TEXT("stood back up after window 2 (head Z %.0f)"), Head.Z));

				// Step 2 & 3: take window 1 slot (click-as-cover check + slot entry) and test PAS -> DEF -> AGG fire modes.
				const FVector C1 = State.Setup.Box1.GetCenter();
				FHitResult WinHit(State.Setup.Sill1.Get(), nullptr, C1, State.Setup.HugNormal);
				WinHit.bBlockingHit = true;
				WinHit.TraceStart = C1 + State.Setup.HugNormal * 120.f;
				AActor* ClickedSill = nullptr;
				FVector ClickedNormal;
				bool bSlide = false;
				Check(State, ACodexTacticsPlayerController::ResolveWindowClickAsCover(World, Squad, Leader, WinHit, false, ClickedSill, ClickedNormal, bSlide)
					&& ClickedSill == State.Setup.Sill1.Get(), TEXT("clicking window while hugging wall targets cover on hug side"));

				EnterSlot(*Leader, State.Setup.SlotWin1);
				Check(State, Leader->IsWindowCoverConfirmed() && Leader->GetStance() == EOperativeStance::Crouching,
					TEXT("window slot confirmed and crouched under sill"));

				AOperativeCharacter* Target = SpawnTargetPmc(*Pmc, State.Setup.EnemySpot1, 1);
				State.Target = Target;
				Check(State, Target != nullptr, TEXT("spawned enemy outside window 1"));
				if (!Target)
				{
					return Finish(State, false);
				}
				Squad->SetAutonomousSquadCombat(true);
				Squad->SetSquadPosture(ESquadFirePosture::Passive);
				Leader->bTacticalCeaseFire = false;
				State.PasShotsBefore = Leader->GetRoundsSpent();
				Pmc->RefreshSight();
				Next(State, EStage::FireModePas);
			}
			return true;
		}
		case EStage::FireModePas:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			if (Squad->GetSquadPosture() == ESquadFirePosture::Passive)
			{
				Check(State, Leader->GetRoundsSpent() == State.PasShotsBefore, TEXT("PAS mode holds fire under window"));
				Squad->SetSquadPosture(ESquadFirePosture::Defensive);
				State.StageTime = 0.f;
				State.PasShotsBefore = Leader->GetRoundsSpent();
				return true;
			}
			Check(State, Leader->GetRoundsSpent() == State.PasShotsBefore, TEXT("unprovoked DEF mode holds fire against unaware target"));
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Pmc->RefreshSight();
			Next(State, EStage::FireModeAgg);
			return true;
		}
		case EStage::FireModeAgg:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			AOperativeCharacter* Target = State.Target.Get();
			if (!Leader || !Target || !Target->HealthComponent)
			{
				return Finish(State, false);
			}
			Pmc->RefreshSight();
			if (Target->HealthComponent->GetCurrentHealth() < PmcStartHp - 1.f)
			{
				Check(State, true, FString::Printf(TEXT("AGG mode fires through window and hits enemy (HP %.0f -> %.0f)"),
					PmcStartHp, Target->HealthComponent->GetCurrentHealth()));
				// Step 5: Window edge = corner (standing beside frame edge).
				Leader->bTacticalCeaseFire = true;
				Target->HealthComponent->RestoreHealth(PmcStartHp, PmcStartHp);
				State.EdgeTargetHpBefore = PmcStartHp;
				EnterSlot(*Leader, State.Setup.SlotFrameEdge1);
				Leader->SetCoverFireMode(ECoverFireMode::CornerLean);
				Check(State, Leader->GetStance() == EOperativeStance::Standing && !Leader->IsWindowCoverConfirmed()
					&& Leader->HasAnyExposedCoverEdge() && Leader->bAtCoverCorner,
					TEXT("standing beside window frame exposes window edge corner"));
				FVector TuckedHead, TuckedPelvis, TuckedHand;
				SamplePose(*Leader, TuckedHead, TuckedPelvis, TuckedHand);
				State.EdgeTuckedHead = TuckedHead;
				State.EdgeMaxPeekReach = 0.f;
				State.bEdgeReturnedPose = false;
				State.EdgeReturnWaitTime = -1.f;
				Leader->bTacticalCeaseFire = false;
				State.bSawEdgeCornerAim = false;
				Next(State, EStage::WindowEdgeCorner);
			}
			return true;
		}
		case EStage::WindowEdgeCorner:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			AOperativeCharacter* Target = State.Target.Get();
			if (!Leader || !Target || !Target->HealthComponent)
			{
				return Finish(State, false);
			}
			Pmc->RefreshSight();
			if (Leader->IsCornerAimActive() && Leader->IsPeekingRoundCorner())
			{
				State.bSawEdgeCornerAim = true;
				FVector PeekHead, PeekPelvis, PeekHand;
				SamplePose(*Leader, PeekHead, PeekPelvis, PeekHand);
				State.EdgeMaxPeekReach = FMath::Max(State.EdgeMaxPeekReach, static_cast<float>(FVector::Dist2D(PeekHead, State.EdgeTuckedHead)));
			}
			if (State.bSawEdgeCornerAim && Target->HealthComponent->GetCurrentHealth() < State.EdgeTargetHpBefore - 1.f)
			{
				Check(State, State.EdgeMaxPeekReach >= 5.f, FString::Printf(TEXT("window edge corner peek leans head out (shift %.1f cm) and hits target outside (HP %.0f -> %.0f)"),
					State.EdgeMaxPeekReach, State.EdgeTargetHpBefore, Target->HealthComponent->GetCurrentHealth()));
				// Clear enemy so he steps back behind the wall via the normal calm return path (no forced EndCornerAim).
				Pmc->DestroyTeam(1);
				Leader->bTacticalCeaseFire = true;
				Next(State, EStage::WindowEdgeReturn);
			}
			return true;
		}
		case EStage::WindowEdgeReturn:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			if (!Leader)
			{
				return Finish(State, false);
			}
			if (!Leader->IsCornerAimActive() && !Leader->bIsCornerLeaning)
			{
				if (State.EdgeReturnWaitTime < 0.f)
				{
					State.EdgeReturnWaitTime = State.StageTime;
				}
				if (State.StageTime - State.EdgeReturnWaitTime < 0.4f)
				{
					return true;
				}
			}
			else if (State.StageTime < 9.f)
			{
				return true;
			}
			FVector ReturnHead, ReturnPelvis, ReturnHand;
			SamplePose(*Leader, ReturnHead, ReturnPelvis, ReturnHand);
			const float ReturnDist = static_cast<float>(FVector::Dist2D(ReturnHead, State.EdgeTuckedHead));
			Check(State, !Leader->IsCornerAimActive() && !Leader->bIsCornerLeaning && ReturnDist < FMath::Max(State.EdgeMaxPeekReach, 12.f),
				FString::Printf(TEXT("calmly returns behind the solid wall when not firing (%.1f s, head offset %.1f cm vs peek %.1f cm)"),
					State.StageTime, ReturnDist, State.EdgeMaxPeekReach));

			// Step 6: Under-sill BlindFire at window 2 (keep manual BlindFire mode active; silent squadmate spots target outside).
			Squad->SetAutonomousSquadCombat(false);
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			if (Members.Num() > 1)
			{
				Members[1]->bTacticalCeaseFire = true;
				PlaceOnSpot(Members[1], State.Setup.EnemySpot1, 0.f);
			}
			EnterSlot(*Leader, State.Setup.SlotWin2);
			Leader->SetCoverFireMode(ECoverFireMode::BlindFire);
			Check(State, Leader->IsWindowCoverConfirmed() && Leader->IsCoverShotBlind() && Leader->GetStance() == EOperativeStance::Crouching,
				TEXT("under window 2 in BlindFire mode crouched"));
			AOperativeCharacter* Target2 = SpawnTargetPmc(*Pmc, State.Setup.EnemySpot2, 1);
			State.Target = Target2;
			if (!Target2)
			{
				return Finish(State, false);
			}
			State.BlindShotsBefore = Leader->GetCoverBlindShots();
			State.BlindHpBefore = PmcStartHp;
			State.BlindMaxHeadZ = -FLT_MAX;
			State.BlindMaxHandZ = -FLT_MAX;
			State.BlindMaxMuzzleZ = -FLT_MAX;
			State.bBlindPoseSampled = false;
			Leader->bTacticalCeaseFire = false;
			Pmc->RefreshSight();
			Next(State, EStage::UnderSillBlindFire);
			return true;
		}
		case EStage::UnderSillBlindFire:
		{
			AOperativeCharacter* Leader = State.Leader.Get();
			AOperativeCharacter* Target = State.Target.Get();
			if (!Leader || !Target || !Target->HealthComponent)
			{
				return Finish(State, false);
			}
			Pmc->RefreshSight();
			if (Leader->bIsBlindFiring)
			{
				FVector Head, Pelvis, Hand;
				SamplePose(*Leader, Head, Pelvis, Hand);
				const float MuzzleZ = static_cast<float>(Leader->GetWeaponMuzzleLocation().Z);
				State.BlindMaxHeadZ = FMath::Max(State.BlindMaxHeadZ, static_cast<float>(Head.Z));
				State.BlindMaxHandZ = FMath::Max(State.BlindMaxHandZ, static_cast<float>(Hand.Z));
				State.BlindMaxMuzzleZ = FMath::Max(State.BlindMaxMuzzleZ, MuzzleZ);
				State.bBlindPoseSampled = true;
			}
			const bool bStayedCrouched = Leader->GetStance() == EOperativeStance::Crouching && !Leader->IsCornerAimActive();
			const bool bHeadHidden = State.bBlindPoseSampled && State.BlindMaxHeadZ <= State.StandHeadZBefore - 20.f;
			const bool bWeaponRaised = State.bBlindPoseSampled && FMath::Max(State.BlindMaxHandZ, State.BlindMaxMuzzleZ) > State.BlindMaxHeadZ;
			const bool bOriginOverSill = Leader->GetCoverFireOrigin().Z > State.Setup.Box2.Max.Z;
			if (Leader->GetCoverBlindShots() > State.BlindShotsBefore && Target->HealthComponent->GetCurrentHealth() < State.BlindHpBefore - 1.f)
			{
				Check(State, bStayedCrouched && bHeadHidden,
					FString::Printf(TEXT("stays crouched with head hidden in BlindFire mode (head Z %.0f vs stand %.0f, sill %.0f)"),
						State.BlindMaxHeadZ, State.StandHeadZBefore, State.Setup.Box2.Max.Z));
				Check(State, bOriginOverSill && bWeaponRaised,
					FString::Printf(TEXT("blind fire raises weapon over sill (origin Z %.0f > sill %.0f, hand Z %.0f, muzzle Z %.0f > head Z %.0f) and hits target (HP %.0f -> %.0f)"),
						Leader->GetCoverFireOrigin().Z, State.Setup.Box2.Max.Z, State.BlindMaxHandZ, State.BlindMaxMuzzleZ, State.BlindMaxHeadZ,
						State.BlindHpBefore, Target->HealthComponent->GetCurrentHealth()));
				Leader->bTacticalCeaseFire = true;
				Pmc->DestroyTeam(1);
				Next(State, EStage::PmcReuse);
			}
			return true;
		}
		case EStage::PmcReuse:
		{
			AOperativeCharacter* PmcFighter = Pmc->SpawnFighter(2, State.Setup.SlotWin2.WorldLocation + FVector(0.f, 0.f, 95.f), FRotator::ZeroRotator, 244, 0);
			Check(State, PmcFighter != nullptr, TEXT("spawned PMC fighter for reuse check"));
			if (PmcFighter)
			{
				EnterSlot(*PmcFighter, State.Setup.SlotWin2);
				Check(State, PmcFighter->IsWindowCoverConfirmed() && PmcFighter->CanFireFromCover(),
					TEXT("PMC fighter reuses AOperativeCharacter window cover component"));
				Pmc->DestroyTeam(2);
			}
			return Finish(State, true);
		}
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		(void)Args;
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
		{
			return State->bDone ? false : Step(*State, DeltaTime);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WallHugWindowSmoke"),
		TEXT("Dev check (144): X6 B_x1_10 wall hug shimmy past 2 windows, window slot PAS/AGG fire, window-edge corner peek, under-sill BlindFire, and PMC reuse."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
