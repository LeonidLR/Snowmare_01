// Dev-only headless check of the game designer's PIE report 2026-11-08 (formation mode, a leader switch mid-move):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands LeaderSwitchMoveSmoke,LeaderSwitchLadderSmoke
// "In FORMATION mode I give the squad an order to move, e.g. onto a roof, and the squad starts moving to the point. If I then switch to another
// squad member, the squad stops, the new member becomes the commander, and the order I gave is in effect cancelled. They must FIRST FINISH their
// movement, and only from the NEXT order follow the new commander."
// Root cause: key 2 -> ACodexTacticsPlayerController::SelectMember -> USquadSubsystem::SetLeaderByIndex -> SetLeader(B): the formation's leader
// changed at once - B, walking his formation move, was stopped (SquadOrderRules::ShouldStopOnBecomingLeader), RebuildFollowers re-hung C's slot
// on the standing B, and A (on his own order) walked on alone. Fix: USquadSubsystem::SelectLeader - B is the pending leader (selected at once),
// the formation keeps A until the order is done / the player's next order (Codex.Squad.LeaderSwitchKeepsMove).
//
// LeaderSwitchMoveSmoke (L_MovementTest, exploration, formation on; real clicks through HandleWorldHit and the real number keys):
//  1. A leads, a click ~18 m ahead; half-way key 2: B is selected (GetLeader) and pending, A still leads the formation; nobody stops (each of the
//     three gets on >= 100 cm towards the goal in the 2 s after the key, the formation's leader is A all the way); all three arrive (A on the
//     clicked point, B / C round it); then B leads the formation (the hand-over at arrival, no click needed);
//  2. B leads, a click ~16 m back; half-way key 3, 1 s later a click to the side: that order is planned with C (he leads the formation at once,
//     his own move to the new point), B's old walk ends (no own order - he follows C), all three end at the new point;
//  3. A/B: Codex.Squad.LeaderSwitchKeepsMove 0 - C leads a click ~15 m away, half-way key 1: A leads the formation at once and is stopped (the
//     reported bug reproduces).
// LeaderSwitchLadderSmoke (L_Demo01_B_FrozenFactory, the wall ladder of LadderQueueSmoke): A leads a click onto the roof (formation, no box
// selection); key 2 while A is on the ladder: A keeps leading until the climb and the walk are done, never more than one on the ladder, all
// three end on the roof, then B leads.
// Godot reference: none (UE-only; the Godot squad had no formation order to keep).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderSubsystem.h"
#include "TimerManager.h"

namespace LeaderSwitchMoveSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float KeyHoldSeconds = 0.1f;
	/** The window after the key in which every member must get on towards the goal, s / cm. */
	constexpr float ProgressWindowSeconds = 2.f;
	constexpr float MinProgressCm = 100.f;

	struct FState
	{
		bool bLadder = false;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FKey HeldKey;
		float HeldSince = 0.f;
		bool bDone = false;
		FVector Goal = FVector::ZeroVector;
		float StartDistance = 0.f;
		float SwitchTime = -1.f;
		TArray<float> DistanceAtSwitch;
		bool bProgressChecked = false;
		bool bLeaderKept = true;
		int32 HandOversBefore = 0;
		float ArrivedAt = -1.f;
		int32 MaxOnLadder = 0;
		TWeakObjectPtr<ALadderActor> Ladder;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetKeepsMoveRule(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.LeaderSwitchKeepsMove")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetKeepsMoveRule(1);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, State.bLadder ? TEXT("LeaderSwitchLadderSmoke") : TEXT("LeaderSwitchMoveSmoke"));
		return false;
	}

	void NextStage(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[LeaderSwitch] stage %d at %.1f s"), State.Stage, State.Time);
	}

	void PressKey(APlayerController* PC, FState& State, const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		State.HeldKey = Key;
		State.HeldSince = State.Time;
	}

	FString Who(const AOperativeCharacter* Operative)
	{
		return Operative ? Operative->DisplayName.ToString() : FString(TEXT("-"));
	}

	/** A real click on the ground at Point (the order path of the game: HandleWorldHit). */
	bool ClickGround(UWorld* World, ACodexTacticsPlayerController* PC, const FVector& Point, FVector& OutHit)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LeaderSwitchClick), false);
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Member);
			}
		}
		if (!PC || !World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 300.f), Point - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutHit = Hit.ImpactPoint;
		PC->HandleWorldHit(Hit);
		return true;
	}

	float Dist(const AOperativeCharacter* Member, const FVector& Goal)
	{
		return Member ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Goal)) : 1.0e9f;
	}

	/** Arms the "nobody stops" window at the key press. */
	void ArmProgress(FState& State, const TArray<AOperativeCharacter*>& Members)
	{
		State.SwitchTime = State.Time;
		State.DistanceAtSwitch.Reset();
		for (const AOperativeCharacter* Member : Members)
		{
			State.DistanceAtSwitch.Add(Dist(Member, State.Goal));
		}
		State.bProgressChecked = false;
		State.bLeaderKept = true;
	}

	/** The window's end: each member got on towards the goal (or was already there). */
	void CheckProgress(FState& State, const TArray<AOperativeCharacter*>& Members, const TCHAR* Variant)
	{
		if (State.bProgressChecked || State.SwitchTime < 0.f || State.Time - State.SwitchTime < ProgressWindowSeconds)
		{
			return;
		}
		State.bProgressChecked = true;
		FString Line;
		bool bAll = true;
		for (int32 Index = 0; Index < Members.Num() && Index < State.DistanceAtSwitch.Num(); ++Index)
		{
			const float Before = State.DistanceAtSwitch[Index];
			const float Now = Dist(Members[Index], State.Goal);
			const bool bOk = Before - Now >= MinProgressCm || Now < 300.f;
			bAll &= bOk;
			Line += FString::Printf(TEXT(" %s %.0f -> %.0f cm (%.0f cm/s)%s;"), *Who(Members[Index]), Before, Now, Members[Index]->GetVelocity().Size2D(),
				bOk ? TEXT("") : TEXT(" STOPPED"));
		}
		Check(State, bAll, FString::Printf(TEXT("%s: nobody stopped after the key - in %.0f s each got on towards the goal:%s"), Variant, ProgressWindowSeconds, *Line));
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > (State.bLadder ? 420.f : 200.f))
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		if (!Squad || !PC || Members.Num() < 3)
		{
			if (State.Time < 10.f)
			{
				return true;
			}
			Check(State, false, FString::Printf(TEXT("squad of three and a player controller (%d)"), Members.Num()));
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Members)
		{
			Member->ColdLevel = 0.f;
		}
		if (State.HeldKey.IsValid() && State.Time - State.HeldSince >= KeyHoldSeconds)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(State.HeldKey, IE_Released, 0.f));
			State.HeldKey = FKey();
		}
		AOperativeCharacter* A = Members[0];
		AOperativeCharacter* B = Members[1];
		AOperativeCharacter* C = Members[2];
		if (State.bLadder)
		{
			int32 OnLadder = 0;
			for (const AOperativeCharacter* Member : Members)
			{
				OnLadder += Member->LadderClimb && Member->LadderClimb->IsOnLadder() ? 1 : 0;
			}
			State.MaxOnLadder = FMath::Max(State.MaxOnLadder, OnLadder);
		}

		switch (State.Stage)
		{
		case 0:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (State.StageTime < (State.bLadder ? 3.5f : NavWarmupSeconds)
				|| (State.bLadder && Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Squad->SetLeader(A);
			Check(State, Squad->IsFormationActive() && !Squad->IsSoloMode(), TEXT("formation mode (the formation follows, Solo off)"));
			if (State.bLadder)
			{
				const ULadderSubsystem* Ladders = World->GetSubsystem<ULadderSubsystem>();
				for (ALadderActor* Ladder : Ladders ? Ladders->GetLadders() : TArray<ALadderActor*>())
				{
					if (Ladder->HasWall())
					{
						State.Ladder = Ladder;
					}
				}
				ALadderActor* Ladder = State.Ladder.Get();
				if (!Ladder)
				{
					Check(State, false, TEXT("the wall ladder"));
					return Finish(State, false);
				}
				State.Goal = Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 150.f;
			}
			else
			{
				State.Goal = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() + A->GetActorForwardVector() * 1800.f);
			}
			FVector Hit;
			const bool bClicked = ClickGround(World, PC, State.Goal, Hit);
			State.Goal = A->GetLastMoveDestination();
			State.StartDistance = Dist(A, State.Goal);
			Check(State, bClicked && A->HasOwnMoveOrder() && Squad->GetFormationLeader() == A, FString::Printf(TEXT("%s: A (%s) leads a click %.0f m away (own order %d)"),
				State.bLadder ? TEXT("ladder") : TEXT("1"), *Who(A), State.StartDistance / 100.f, A->HasOwnMoveOrder() ? 1 : 0));
			State.HandOversBefore = Squad->GetPendingLeaderHandOverCount();
			NextStage(State, 1);
			return true;
		}
		case 1:
		{
			// Half-way (on the ladder for the roof variant): the real key 2.
			const bool bHalf = State.bLadder ? (A->LadderClimb && A->LadderClimb->IsClimbing())
				: Dist(A, State.Goal) <= State.StartDistance * 0.55f;
			if (!bHalf && State.StageTime < (State.bLadder ? 90.f : 20.f))
			{
				return true;
			}
			Check(State, bHalf, FString::Printf(TEXT("%s: half-way reached (%.0f of %.0f cm left%s)"), State.bLadder ? TEXT("ladder") : TEXT("1"), Dist(A, State.Goal),
				State.StartDistance, State.bLadder ? TEXT(", A on the ladder") : TEXT("")));
			PressKey(PC, State, EKeys::Two);
			ArmProgress(State, Members);
			NextStage(State, 2);
			return true;
		}
		case 2:
		{
			if (State.HeldKey.IsValid())
			{
				return true;
			}
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == B && Squad->GetPendingLeader() == B && Squad->GetFormationLeader() == A,
				FString::Printf(TEXT("key 2 mid-move: %s selected at once (GetLeader %s) and pending, the formation still led by %s (%s)"), *Who(B), *Who(Squad->GetLeader()),
					*Who(A), *Who(Squad->GetFormationLeader())));
			NextStage(State, 3);
			return true;
		}
		case 3:
		{
			if (!State.bLadder)
			{
				// (On the ladder the others wait at its foot for their turn - the roof check below is that variant's "nobody stopped".)
				CheckProgress(State, Members, TEXT("1"));
			}
			const bool bOrderRunning = A->HasOwnMoveOrder() || (A->LadderClimb && A->LadderClimb->IsBusy());
			if (bOrderRunning && Squad->GetFormationLeader() != A)
			{
				State.bLeaderKept = false;
			}
			const float Arrive = State.bLadder ? 250.f : 150.f;
			bool bAllThere = Dist(A, State.Goal) < Arrive && !bOrderRunning;
			for (const AOperativeCharacter* Member : { B, C })
			{
				bAllThere &= Dist(Member, State.Goal) < 600.f && !(Member->LadderClimb && Member->LadderClimb->IsBusy());
			}
			if (State.bLadder && State.Ladder.IsValid())
			{
				for (const AOperativeCharacter* Member : Members)
				{
					const float FeetOverTop = static_cast<float>(Member->GetActorLocation().Z) - Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
						- State.Ladder->GetTopWorldZ();
					bAllThere &= FMath::Abs(FeetOverTop) < 40.f;
				}
			}
			if (!bAllThere)
			{
				State.ArrivedAt = -1.f;
				if (State.StageTime < (State.bLadder ? 240.f : 30.f))
				{
					return true;
				}
			}
			else if (State.ArrivedAt < 0.f)
			{
				State.ArrivedAt = State.StageTime;
			}
			if (bAllThere && State.StageTime - State.ArrivedAt < 4.f && Squad->GetFormationLeader() != B)
			{
				return true; // the formation settles, then the hand-over at arrival
			}
			const TCHAR* Variant = State.bLadder ? TEXT("ladder") : TEXT("1");
			Check(State, State.bLeaderKept, FString::Printf(TEXT("%s: the formation's leader stayed %s while his order ran"), Variant, *Who(A)));
			Check(State, bAllThere, FString::Printf(TEXT("%s: all three reached the original destination (A %.0f cm, B %.0f cm, C %.0f cm off)"), Variant,
				Dist(A, State.Goal), Dist(B, State.Goal), Dist(C, State.Goal)));
			if (State.bLadder)
			{
				Check(State, State.MaxOnLadder == 1, FString::Printf(TEXT("ladder: never more than one on the ladder (max %d)"), State.MaxOnLadder));
			}
			Check(State, Squad->GetFormationLeader() == B && !Squad->GetPendingLeader() && Squad->GetPendingLeaderHandOverCount() == State.HandOversBefore + 1,
				FString::Printf(TEXT("%s: after the arrival %s leads the formation (formation leader %s, pending %s)"), Variant, *Who(B), *Who(Squad->GetFormationLeader()),
					*Who(Squad->GetPendingLeader())));
			if (State.bLadder)
			{
				return Finish(State, true);
			}
			// 2: B leads a click back; half-way key 3; then a new order.
			State.Goal = SmokeUtils::ClearPoint(World, B->GetActorLocation(), B->GetActorLocation() - B->GetActorForwardVector() * 1600.f);
			FVector Hit;
			const bool bClicked = ClickGround(World, PC, State.Goal, Hit);
			State.Goal = B->GetLastMoveDestination();
			State.StartDistance = Dist(B, State.Goal);
			Check(State, bClicked && B->HasOwnMoveOrder(), FString::Printf(TEXT("2: B leads a click %.0f m away"), State.StartDistance / 100.f));
			NextStage(State, 10);
			return true;
		}
		case 10:
		{
			if (Dist(B, State.Goal) > State.StartDistance * 0.55f && State.StageTime < 20.f)
			{
				return true;
			}
			PressKey(PC, State, EKeys::Three);
			NextStage(State, 11);
			return true;
		}
		case 11:
		{
			if (State.HeldKey.IsValid() || State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == C && Squad->GetFormationLeader() == B && B->HasOwnMoveOrder(),
				FString::Printf(TEXT("2: key 3 mid-move: %s selected, %s still leads his order"), *Who(Squad->GetLeader()), *Who(Squad->GetFormationLeader())));
			// The next order: a click to the side of C.
			const FVector Side = FVector::CrossProduct(FVector::UpVector, B->GetActorForwardVector()).GetSafeNormal2D();
			const FVector Want = SmokeUtils::ClearPoint(World, C->GetActorLocation(), C->GetActorLocation() + Side * 800.f);
			FVector Hit;
			const bool bClicked = ClickGround(World, PC, Want, Hit);
			State.Goal = C->GetLastMoveDestination();
			Check(State, bClicked && Squad->GetFormationLeader() == C && !Squad->GetPendingLeader() && C->HasOwnMoveOrder()
				&& FVector::Dist2D(C->GetLastMoveDestination(), Hit) < 200.f,
				FString::Printf(TEXT("2: the next order is planned with %s - he leads the formation at once, his own move to the click (formation leader %s, own %d, %.0f cm off the click)"),
					*Who(C), *Who(Squad->GetFormationLeader()), C->HasOwnMoveOrder() ? 1 : 0, FVector::Dist2D(C->GetLastMoveDestination(), Hit)));
			Check(State, !B->HasOwnMoveOrder(), FString::Printf(TEXT("2: the old leader %s's walk ended with it (own order %d) - he follows %s"), *Who(B), B->HasOwnMoveOrder() ? 1 : 0, *Who(C)));
			NextStage(State, 12);
			return true;
		}
		case 12:
		{
			const bool bAll = Dist(C, State.Goal) < 150.f && Dist(A, State.Goal) < 600.f && Dist(B, State.Goal) < 600.f && !C->HasOwnMoveOrder();
			if (!bAll && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("2: all three at the new order's point (C %.0f cm, A %.0f cm, B %.0f cm off)"), Dist(C, State.Goal), Dist(A, State.Goal),
				Dist(B, State.Goal)));
			// 3: the old rule.
			SetKeepsMoveRule(0);
			State.Goal = SmokeUtils::ClearPoint(World, C->GetActorLocation(), C->GetActorLocation() - C->GetActorForwardVector() * 1500.f);
			FVector Hit;
			ClickGround(World, PC, State.Goal, Hit);
			State.Goal = C->GetLastMoveDestination();
			State.StartDistance = Dist(C, State.Goal);
			NextStage(State, 20);
			return true;
		}
		case 20:
		{
			if (Dist(C, State.Goal) > State.StartDistance * 0.55f && State.StageTime < 20.f)
			{
				return true;
			}
			PressKey(PC, State, EKeys::One);
			NextStage(State, 21);
			return true;
		}
		case 21:
		{
			if (State.HeldKey.IsValid() || State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->GetFormationLeader() == A && !Squad->GetPendingLeader() && !A->HasOwnMoveOrder() && A->GetVelocity().Size2D() < 60.f,
				FString::Printf(TEXT("3: Codex.Squad.LeaderSwitchKeepsMove 0 (the old rule): key 1 mid-move makes %s the formation's leader at once and stops him (%.0f cm/s) - the reported bug reproduces"),
					*Who(A), A->GetVelocity().Size2D()));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Start(UWorld* World, bool bLadder)
	{
		if (!World)
		{
			return;
		}
		if (!bLadder)
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bLadder = bLadder;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void RunMove(const TArray<FString>&, UWorld* World)
	{
		Start(World, false);
	}

	void RunLadder(const TArray<FString>&, UWorld* World)
	{
		Start(World, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs MoveCommand(
		TEXT("CodexTactics.LeaderSwitchMoveSmoke"),
		TEXT("Dev check (the game designer's PIE report 2026-11-08, L_MovementTest): in formation mode a number key mid-move lets the squad finish the order ")
		TEXT("with the old leader (nobody stops, all arrive), the selected member leads from arrival / the next order; Codex.Squad.LeaderSwitchKeepsMove 0 ")
		TEXT("reproduces the bug. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMove));

	static FAutoConsoleCommandWithWorldAndArgs LadderCommand(
		TEXT("CodexTactics.LeaderSwitchLadderSmoke"),
		TEXT("Dev check (the game designer's PIE report 2026-11-08, L_Demo01_B_FrozenFactory): in formation mode the squad ordered onto the wall ladder's roof, ")
		TEXT("key 2 while the leader climbs: the old leader leads the climb to its end, all three on the roof, then the selected one leads. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLadder));
}

#endif // !UE_BUILD_SHIPPING
