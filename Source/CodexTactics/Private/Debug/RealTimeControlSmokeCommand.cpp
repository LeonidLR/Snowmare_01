// Dev-only headless check of the RTS combat time modes on L_MovementTest (user request 2026-10-06, FCombatTimeModeRules):
//   Scripts/smoke.ps1 -Command CodexTactics.RealTimeControlSmoke -Log Smoke-RealTimeControl.log
// Drives the player controller exactly like the mouse / keyboard (HandleWorldHit for a ground click, SpacePressed /
// SpaceReleased with the real-time hold measured by PlayerTick):
//   fight starts in full real time -> a ground click moves the leader at once (no Space ever pressed) ->
//   Space tap: tactical pause, a click is only planned (marker) and the leader stays put ->
//   Space tap: the world resumes and the planned move runs ->
//   Space held 1.5 s (enemy within 15 m): turn-based fight, resolved at the threshold (the release does not also pause) ->
//   Space held 1.5 s again: back to full real time (time dilation 1).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EncounterQueries.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace RealTimeControlSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		int32 Failures = 0;
		FVector SquadStart = FVector::ZeroVector;
		FVector LeaderStart = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RealTimeControlSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** A free ground point Distance cm from the leader along Direction (clear of walls), at his feet. */
	FVector GroundTarget(UWorld* World, const AOperativeCharacter& Leader, const FVector& Direction, float Distance)
	{
		const FVector Feet = Leader.GetActorLocation() - FVector(0.f, 0.f, Leader.GetSimpleCollisionHalfHeight());
		return SmokeUtils::ClearPoint(World, Feet, Feet + Direction.GetSafeNormal2D() * Distance);
	}

	/** Left click on the ground at Point through the controller's world-click path. */
	void ClickGround(ACodexTacticsPlayerController& PC, const FVector& Point)
	{
		PC.HandleWorldHit(FHitResult(nullptr, nullptr, Point, FVector::UpVector));
	}

	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !PC || !Leader)
		{
			Check(State, false, TEXT("flow, controller and leader"));
			return Finish(State, false);
		}
		// The level wave is parked frozen 40 m away: only the checks below decide what is near the squad.
		if (State.Stage > 0)
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				if (FVector::Dist2D(It->GetActorLocation(), State.SquadStart) < 3000.f)
				{
					It->SetActorLocation(State.SquadStart + FVector(4000.f, 4000.f, 2000.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
		}

		switch (State.Stage)
		{
		case 0: // The fight starts — in full real time, Space never touched.
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			State.SquadStart = Leader->GetActorLocation();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("the fight starts in full real time"));
			Check(State, !PC->IsSpaceHeld(), TEXT("Space not pressed"));
			Check(State, !PC->IsRealTimeOrderLocked(), TEXT("real-time orders are not locked"));
			State.LeaderStart = Leader->GetActorLocation();
			State.Target = GroundTarget(World, *Leader, Leader->GetActorRightVector(), 600.f);
			ClickGround(*PC, State.Target);
			Check(State, Leader->IsMoving(), TEXT("ground click in real time: the leader moves at once"));
			Check(State, Squad->GetPlannedOrderCount() == 0, TEXT("nothing planned (executed, not queued)"));
			NextStage(State);
			break;
		case 1:
			if (State.StageTime >= 3.f)
			{
				const float Moved = FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: real-time move %.0f cm, %.0f cm from the target"), Moved,
					FVector::Dist2D(Leader->GetActorLocation(), State.Target));
				Check(State, Moved > 200.f, TEXT("the leader walked towards the clicked point while the world kept running"));
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("still real time"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		case 2: // Tap -> tactical pause; a click is planned, not executed.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("Space tap: tactical pause"));
			Check(State, World->GetWorldSettings()->TimeDilation < 0.1f, TEXT("world time near-stopped"));
			Leader->StopOperative();
			State.LeaderStart = Leader->GetActorLocation();
			State.Target = GroundTarget(World, *Leader, -Leader->GetActorRightVector(), 600.f);
			ClickGround(*PC, State.Target);
			Check(State, Squad->GetPlannedOrderCount() == 1, TEXT("the pause click is queued (planned order + marker)"));
			NextStage(State);
			break;
		case 3:
			if (State.StageTime >= 1.5f)
			{
				Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) < 30.f, TEXT("paused: the leader does not move yet"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		case 4: // Tap again -> resume; the planned move runs.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("second tap: real time again"));
			Check(State, Squad->GetPlannedOrderCount() == 0, TEXT("the plan was handed over"));
			NextStage(State);
			break;
		case 5:
			if (State.StageTime >= 3.f)
			{
				Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) > 200.f, TEXT("after the resume the queued move runs"));
				// An enemy within 15 m for the turn-based entry.
				FActorSpawnParameters Params;
				AStaticMeshActor* Enemy = World->SpawnActor<AStaticMeshActor>(Leader->GetActorLocation() + FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
				Enemy->Tags.Add(CombatQueries::EnemyTag);
				Leader->StopOperative();
				PC->SpacePressed(); // hold
				NextStage(State);
			}
			break;
		case 6:
			if (State.StageTime >= 2.f)
			{
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::TurnBased, TEXT("Space held 1.5 s: turn-based (before the release)"));
				PC->SpaceReleased();
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::TurnBased, TEXT("the release after the hold is no tap"));
				const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
				Check(State, TurnBased && TurnBased->IsActive(), TEXT("the grid fight runs"));
				PC->SpacePressed(); // hold again
				NextStage(State);
			}
			break;
		case 7:
			if (State.StageTime >= 2.f)
			{
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("Space held 1.5 s in turn-based: full real time"));
				PC->SpaceReleased();
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("the release does not pause"));
				Check(State, FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation, 1.f, 0.001f), TEXT("world at full speed"));
				const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
				Check(State, TurnBased && !TurnBased->IsActive(), TEXT("the grid fight ended"));
				// And orders run at once again.
				State.LeaderStart = Leader->GetActorLocation();
				ClickGround(*PC, GroundTarget(World, *Leader, Leader->GetActorRightVector(), 500.f));
				Check(State, Leader->IsMoving(), TEXT("back in real time a click moves at once"));
				return Finish(State, true);
			}
			break;
		default:
			return Finish(State, false);
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			// Real-time ticker: world timers crawl during the tactical pause.
			TSharedRef<FState> State = MakeShared<FState>();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && Step(W, *State);
			}), StepSeconds);
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RealTimeControlSmoke"),
		TEXT("Dev check of the RTS time modes: real-time orders without Space, tap pause queues orders, hold enters / leaves turn-based; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
