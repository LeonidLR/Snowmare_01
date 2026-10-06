// Dev-only console command for a headless check of combat-mode input on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CombatFlowSmoke
// Drives the player controller's Space path (SpacePressed / SpaceReleased + real-time hold) through:
// wave start -> no formation following -> tap pause -> planned move clamped to 12 m -> tap release executes it ->
// hold without enemies refused -> hold near an enemy enters turn-based -> hold again returns to full real time
// (FGameFlowConfig::bHoldExitsTurnBasedToRealTime, user request 2026-10-06; with the flag off: the Godot free pause).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Combat/EncounterQueries.h"
#include "Combat/EnemySpawnPoint.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "GameFramework/WorldSettings.h"
#include "TimerManager.h"

namespace CombatFlowSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		FVector LeaderStart = FVector::ZeroVector;
		/** The leader at the wave start: enemies spawned near it are removed. */
		FVector SquadStart = FVector::ZeroVector;
		FVector FollowerStart = FVector::ZeroVector;
		FVector Planned = FVector::ZeroVector;
		int32 ChargesBeforeTurnBased = 0;
		TArray<FString> Failures;
	};

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke check %-55s %s"), What, bOk ? TEXT("ok") : TEXT("FAIL"));
		if (!bOk)
		{
			State.Failures.Add(What);
		}
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad->GetLeader();
		AOperativeCharacter* Follower = Squad->GetMembers().Num() > 1 ? Squad->GetMembers()[1] : nullptr;
		if (!PC || !Leader || !Follower)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: controller or squad missing"));
			FPlatformMisc::RequestExit(false, TEXT("CombatFlowSmoke"));
			return false;
		}

		// The level wave spawns in batches beyond the gate: every enemy (late ones too) stays frozen there, so the checks
		// below control which enemies are near the squad (before the crowd limit was raised to 250 the late ones never
		// moved; since then a free one reached the leader and broke the run).
		// The user's L_MovementTest has its 4 spawn points 2-15 m around the squad start (spawn lane ANY): every enemy of the
		// wave, late ones too, is parked frozen 40 m away (the wave stays alive; the checks need no enemy near the squad).
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
		case 0: // Start a wave the same way the flow does after the gate.
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("wave started in real time"));
			// The level wave spawns at the spawn points beyond the gate; freeze it there so the checks below control
			// which enemies are near the squad.
			State.SquadStart = Leader->GetActorLocation();
			for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: spawn point %s at %s, %.1f m from the leader"), *It->GetName(),
					*It->GetActorLocation().ToCompactString(), FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation()) / 100.f);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
			}
			State.LeaderStart = Leader->GetActorLocation();
			State.FollowerStart = Follower->GetActorLocation();
			Leader->OrderMoveTo(State.LeaderStart + FVector(0.f, 700.f, 0.f), false);
			NextStage(State);
			break;
		case 1: // Followers act individually in combat.
			if (State.StageTime >= 3.f)
			{
				Check(State, !Squad->IsFormationActive(), TEXT("formation off in combat"));
				Check(State, FVector::Dist2D(Follower->GetActorLocation(), State.FollowerStart) < 50.f, TEXT("follower stayed put while leader moved"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		case 2: // Tap -> tactical pause.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tap started tactical pause"));
			Check(State, FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation, 0.02f, 0.001f), TEXT("world time dilation 0.02"));
			State.LeaderStart = Leader->GetActorLocation();
			State.Planned = Squad->PlanMove(Leader, State.LeaderStart + FVector(-3000.f, 0.f, 0.f), false, Flow->GetConfig().PauseOrderRadius);
			Check(State, FMath::IsNearlyEqual(FVector::Dist2D(State.Planned, State.LeaderStart), 1200.f, 1.f), TEXT("pause order clamped to 12 m"));
			Check(State, Squad->GetPlannedOrderCount() == 1, TEXT("one planned order"));
			NextStage(State);
			break;
		case 3: // Still paused: the leader has not started the planned move.
			if (State.StageTime >= 1.f)
			{
				Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) < 30.f, TEXT("planned order waits for the release"));
				PC->SpacePressed();
				NextStage(State);
			}
			break;
		case 4: // Tap again -> release executes the plan.
			PC->SpaceReleased();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("second tap released the pause"));
			NextStage(State);
			break;
		case 5:
			if (State.StageTime >= 7.f)
			{
				Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.Planned) < 80.f, TEXT("leader reached the planned point"));
				{
					float NearestEnemy = TNumericLimits<float>::Max();
					for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
					{
						NearestEnemy = FMath::Min(NearestEnemy, static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation())));
					}
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke diag: leader start %s, planned %s, now %s (%.0f cm off), moving %d, nearest enemy %.1f m"),
						*State.LeaderStart.ToCompactString(), *State.Planned.ToCompactString(), *Leader->GetActorLocation().ToCompactString(),
						FVector::Dist2D(Leader->GetActorLocation(), State.Planned), Leader->IsMoving() ? 1 : 0, NearestEnemy / 100.f);
				}
				PC->SpacePressed(); // hold without enemies
				NextStage(State);
			}
			break;
		case 6:
			if (State.StageTime >= 2.f)
			{
				PC->SpaceReleased();
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("hold without enemies is refused"));
				FActorSpawnParameters Params;
				AStaticMeshActor* Enemy = World->SpawnActor<AStaticMeshActor>(Leader->GetActorLocation() + FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
				Enemy->Tags.Add(CombatQueries::EnemyTag);
				PC->SpacePressed(); // hold near an enemy
				NextStage(State);
			}
			break;
		case 7:
			if (State.StageTime >= 2.f)
			{
				PC->SpaceReleased();
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::TurnBased, TEXT("hold near an enemy entered turn-based"));
				State.ChargesBeforeTurnBased = Flow->GetPauseCharges();
				PC->SpacePressed(); // hold again to leave
				NextStage(State);
			}
			break;
		case 8:
			if (State.StageTime >= 2.f)
			{
				PC->SpaceReleased();
				Check(State, Flow->GetPauseCharges() == State.ChargesBeforeTurnBased, TEXT("leaving turn-based spent no charge"));
				if (Flow->GetConfig().bHoldExitsTurnBasedToRealTime)
				{
					Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("hold again returned to full real time"));
					Check(State, FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation, 1.f, 0.001f), TEXT("world runs at full speed"));
				}
				else
				{
					Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("hold again returned to tactical pause"));
					// The hold fires up to ~0.5 s before this step runs, so the free pause has already started counting down.
					const float Remaining = Flow->GetPauseTimeRemaining();
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke free pause remaining %.2f s"), Remaining);
					Check(State, Remaining > Flow->GetConfig().PostTurnBasedPauseDuration - 2.f
						&& Remaining <= Flow->GetConfig().PostTurnBasedPauseDuration, TEXT("free pause lasts 20 s"));
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures.Num() == 0 ? TEXT("PASS") : TEXT("FAIL"));
				FPlatformMisc::RequestExit(false, TEXT("CombatFlowSmoke"));
				return false;
			}
			break;
		default:
			return false;
		}
		return true;
	}

	void Start(UWorld* World)
	{
		// Real-time ticker: world timers would crawl during the tactical pause (time dilation 0.02).
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && Step(W, *State);
		}), StepSeconds);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		// Test layout start; the squad settles into formation during the NavMesh warm-up.
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
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CombatFlowSmoke"),
		TEXT("Dev check: Space tap/hold, tactical pause order planning, turn-based enter/exit; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
