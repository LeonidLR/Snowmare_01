// Dev-only headless check of the squad formation in combat (user decision 2026-10-26) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands CombatFormationSmoke
// Game designer: "in the tactical pause I give orders to soldiers, then resume. Combat starts, and the soldiers do NOT return to the
// formation: they keep acting SOLO; pressing B several times does nothing; it ends only when the combat ends. Until I give the order to
// switch to solo mode, they must stay in formation." Root cause: USquadSubsystem::IsFormationActive was "exploration / PostCombat only"
// (Godot is_tactical_mode: individual control from the preparation to the fight's end) - in the whole fight nobody followed, so Solo on /
// off changed nothing visible. Now SquadOrderRules::IsFollowPhase (Codex.Squad.CombatFormation 1).
// On its own empty ground (SmokeUtils::FindClearArena), the wave parked far away:
//  0. the wave fight starts (real time): Solo stays off, the formation is on (the old rule, CombatFormation 0: off - the bug);
//  1. Space: the tactical pause (the formation still on); B and C (keys 2 / 3) get their own move orders 6 m to the sides, A is selected
//     again; Space: real time - B and C run their orders;
//  2. both reach their spots (their orders run to the end, the formation does not pull them back meanwhile), Solo still off;
//  3. their orders done, both rejoin the formation round the leader A, who stands still;
//  4. A walks 11 m: both follow him (the squad stays together in the fight);
//  5. B (the key, the real input path) in the fight: Solo on - A walks 8 m, the two hold where they are;
//  6. B again: Solo off (Regroup) - both come back to A. The fight runs all the time (WaveCombat).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace CombatFormationSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** The ordered spots: this far to the side of the start, cm. */
	constexpr float SideOrderCm = 600.f;
	/** "Rejoined": within this of the leader, cm (the formation's slots are 2-4 m from him). */
	constexpr float RejoinedCm = 550.f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector TargetB = FVector::ZeroVector;
		FVector TargetC = FVector::ZeroVector;
		float MinB = TNumericLimits<float>::Max();
		float MinC = TNumericLimits<float>::Max();
		FVector StartA = FVector::ZeroVector;
		FVector StartB = FVector::ZeroVector;
		FVector StartC = FVector::ZeroVector;
		FVector WalkTarget = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CombatFormationSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	void PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	void SetCombatFormation(int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.CombatFormation")))
		{
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	float Dist(const AActor* A, const FVector& Point)
	{
		return A ? static_cast<float>(FVector::Dist2D(A->GetActorLocation(), Point)) : -1.f;
	}

	FVector Ground(UWorld* World, const FVector& From, const FVector& Desired)
	{
		return SmokeUtils::ClearPoint(World, From, Desired);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Total < 3.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Flow || !Squad || !PC || Squad->GetMembers().Num() < 3 || State.Total > 150.f)
		{
			Check(State, false, FString::Printf(TEXT("flow, squad of 3, controller / time out (stage %d)"), State.Stage));
			SetCombatFormation(1);
			return Finish(State, false);
		}
		// The wave fights nobody: parked far away, frozen (the fight itself stays on).
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation != 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		AOperativeCharacter* A = State.Members.IsValidIndex(0) ? State.Members[0].Get() : nullptr;
		AOperativeCharacter* B = State.Members.IsValidIndex(1) ? State.Members[1].Get() : nullptr;
		AOperativeCharacter* C = State.Members.IsValidIndex(2) ? State.Members[2].Get() : nullptr;
		if (State.Stage > 0 && (!A || !B || !C))
		{
			Check(State, false, TEXT("the three operatives still valid"));
			return Finish(State, false);
		}
		if (State.Stage > 0 && Flow->GetPhase() != ECodexGamePhase::WaveCombat)
		{
			Check(State, false, FString::Printf(TEXT("stage %d: the fight ended by itself"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-800.f, -900.f), FVector2D(1500.f, 900.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 23 x 18 m spot for the smoke"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			const FVector Offsets[] = { FVector::ZeroVector, -State.F * 250.f - State.R * 200.f, -State.F * 250.f + State.R * 200.f };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Each = Roster[Index];
				Each->StopOperative();
				Each->SetStance(EOperativeStance::Standing);
				Each->TeleportTo(State.P + Offsets[Index] + FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
			}
			Squad->SetLeader(Roster[0]);
			SmokeUtils::StartWaveFight(Flow); // the sudden horde start, its warning dismissed: the real-time fight
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("0: the wave fight runs in real time"));
			Check(State, !Squad->IsSoloMode() && Squad->IsFormationActive(), TEXT("0: combat start keeps the squad mode - Solo off, the formation ON"));
			SetCombatFormation(0);
			Check(State, !Squad->IsFormationActive(), TEXT("0: the old rule (Codex.Squad.CombatFormation 0): no formation in the fight - the reported bug"));
			SetCombatFormation(1);
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause && Squad->IsFormationActive() && !Squad->IsSoloMode(),
				TEXT("1: Space - the tactical pause, the formation stays on"));
			const float Radius = Flow->GetPauseOrderRadius();
			State.TargetB = Ground(World, B->GetActorLocation(), State.P + State.F * 200.f + State.R * SideOrderCm);
			State.TargetC = Ground(World, C->GetActorLocation(), State.P + State.F * 200.f - State.R * SideOrderCm);
			Squad->SetLeader(B); // key 2
			State.TargetB = Squad->PlanMove(B, State.TargetB, false, Radius);
			Squad->SetLeader(C); // key 3
			State.TargetC = Squad->PlanMove(C, State.TargetC, false, Radius);
			Squad->SetLeader(A); // key 1: A leads again
			Check(State, Squad->GetPlannedOrderCount() == 2 && !Squad->IsSoloMode(), TEXT("1: B and C each got an individual order in the pause, Solo still off"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("1: Space - real time"));
			Check(State, B->HasOwnMoveOrder() && C->HasOwnMoveOrder() && Squad->IsOnOwnOrder(B) && Squad->IsOnOwnOrder(C),
				TEXT("1: B and C run their orders"));
			Next(State);
			return true;
		}
		case 2:
		{
			State.MinB = FMath::Min(State.MinB, Dist(B, State.TargetB));
			State.MinC = FMath::Min(State.MinC, Dist(C, State.TargetC));
			if ((B->HasOwnMoveOrder() || C->HasOwnMoveOrder()) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, State.MinB < 200.f && State.MinC < 200.f, FString::Printf(TEXT("2: both carried their orders out (B %.0f cm, C %.0f cm from the spot)"),
				State.MinB, State.MinC));
			Check(State, !Squad->IsSoloMode() && Squad->IsFormationActive() && Squad->GetLeader() == A, TEXT("2: Solo still off, A leads"));
			Next(State);
			return true;
		}
		case 3:
		{
			const float DB = Dist(B, A->GetActorLocation());
			const float DC = Dist(C, A->GetActorLocation());
			if ((DB > RejoinedCm || DC > RejoinedCm || B->IsMoving() || C->IsMoving()) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, DB <= RejoinedCm && DC <= RejoinedCm, FString::Printf(TEXT("3: orders done - both rejoined the formation round A (B %.0f cm, C %.0f cm) after %.1f s"),
				DB, DC, State.StageTime));
			State.StartA = A->GetActorLocation();
			State.StartB = B->GetActorLocation();
			State.StartC = C->GetActorLocation();
			State.WalkTarget = Ground(World, A->GetActorLocation(), State.P + State.F * 1100.f);
			A->OrderMoveTo(State.WalkTarget, false);
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 9.f && (Dist(A, State.WalkTarget) > 150.f || A->IsMoving() || B->IsMoving() || C->IsMoving()))
			{
				return true;
			}
			const float MovedB = Dist(B, State.StartB);
			const float MovedC = Dist(C, State.StartC);
			Check(State, Dist(A, State.WalkTarget) < 200.f, FString::Printf(TEXT("4: A walked %.0f cm"), Dist(A, State.StartA)));
			Check(State, MovedB > 500.f && MovedC > 500.f && Dist(B, A->GetActorLocation()) <= RejoinedCm && Dist(C, A->GetActorLocation()) <= RejoinedCm,
				FString::Printf(TEXT("4: in the fight the squad follows A (B moved %.0f, C %.0f cm; %.0f / %.0f cm from him)"), MovedB, MovedC,
					Dist(B, A->GetActorLocation()), Dist(C, A->GetActorLocation())));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 5:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode(), TEXT("5: B (a tap) in the fight - Solo ON"));
			State.StartA = A->GetActorLocation();
			State.StartB = B->GetActorLocation();
			State.StartC = C->GetActorLocation();
			State.WalkTarget = Ground(World, A->GetActorLocation(), A->GetActorLocation() - State.F * 800.f);
			A->OrderMoveTo(State.WalkTarget, false);
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 7.f && (Dist(A, State.WalkTarget) > 150.f || A->IsMoving()))
			{
				return true;
			}
			const float MovedB = Dist(B, State.StartB);
			const float MovedC = Dist(C, State.StartC);
			Check(State, Dist(A, State.StartA) > 600.f && MovedB < 150.f && MovedC < 150.f,
				FString::Printf(TEXT("6: Solo in the fight - A walked %.0f cm, B / C held (%.0f / %.0f cm)"), Dist(A, State.StartA), MovedB, MovedC));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 7:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode(), TEXT("7: B again in the fight - Solo OFF (Regroup)"));
			Next(State);
			return true;
		}
		case 8:
		{
			const float DB = Dist(B, A->GetActorLocation());
			const float DC = Dist(C, A->GetActorLocation());
			if ((DB > RejoinedCm || DC > RejoinedCm || B->IsMoving() || C->IsMoving()) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, DB <= RejoinedCm && DC <= RejoinedCm, FString::Printf(TEXT("8: Regroup in the fight - both back at A (B %.0f cm, C %.0f cm)"), DB, DC));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("8: the fight ran all the time"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CombatFormationSmoke"),
		TEXT("Dev check of the squad formation in combat (2026-10-26): pause orders run then rejoin, the squad follows, B toggles Solo in the fight; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
