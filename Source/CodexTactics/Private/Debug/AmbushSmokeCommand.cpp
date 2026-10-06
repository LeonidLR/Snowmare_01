// Dev-only headless check of the ambush combat start on L_MovementTest (the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.AmbushSmoke -Log Smoke-Ambush.log
// User request 2026-10-06: on a level with patrols the fight starts when the squad attacks, not by «Начать бой».
// The level's enemies are removed; L_MovementTest itself has no patrols, so "auto" keeps the button there (wave map
// unchanged). Then the level is made an ambush level (override), a frost hound patrols a two-point route ~10 m ahead of
// the squad with its senses off, and the player Ctrl + clicks it (ACodexTacticsPlayerController::IssueTargetedShot).
// Checks: the real-time fight of wave 1 starts at once (never through the cutscene / preparation), by the attack order,
// once; the hound is the wave (nothing spawned); the squad fires and the hound breaks off its patrol.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace AmbushSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		bool bSawCutsceneOrPreparation = false;
		float HoundHealthAtStart = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("AmbushSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Member->ColdLevel = 0.f;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		if (!Flow || !Encounter)
		{
			Check(State, false, TEXT("game flow and encounter subsystems"));
			return Finish(State);
		}
		State.bSawCutsceneOrPreparation |= Flow->GetPhase() == ECodexGamePhase::Cutscene || Flow->GetPhase() == ECodexGamePhase::Preparation;
		AEnemyCharacter* Hound = State.Hound.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (!Leader)
			{
				Check(State, false, TEXT("squad leader present"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			// The wave map keeps the classic start ("auto" without patrols on the map).
			Check(State, !Encounter->LevelHasPatrols() && !Encounter->IsAmbushCombatStart(), TEXT("L_MovementTest (no patrols): «Начать бой» kept"));
			Encounter->SetCombatStartOverride(ECombatStartMode::Ambush);
			Check(State, Encounter->IsAmbushCombatStart(), TEXT("ambush level: «Начать бой» hidden"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring"));

			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector R = FVector::CrossProduct(FVector::UpVector, F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const FVector A = SmokeUtils::ClearPoint(World, Feet, Feet + F * 900.f);
			const FVector B = SmokeUtils::ClearPoint(World, A, A + R * 400.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
			USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
			if (!Spline)
			{
				Check(State, false, TEXT("route spawned"));
				return Finish(State);
			}
			Spline->ClearSplinePoints(false);
			Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
			Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);
			Spline->UpdateSpline();
			Route->bPingPong = true;
			Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, A + FVector(0.f, 0.f, 80.f), (-F).Rotation());
			State.Hound = Hound;
			if (!Hound)
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State);
			}
			// Senses off: only the attack may start the fight.
			Hound->bOverridePerception = true;
			Hound->PerceptionOverride.SightRangeCm = 0.f;
			Hound->PerceptionOverride.ProximityCm = 0.f;
			Hound->PerceptionOverride.HearWalkCm = Hound->PerceptionOverride.HearRunCm = 0.f;
			Hound->PerceptionOverride.HearCrouchWalkCm = Hound->PerceptionOverride.HearCrawlCm = 0.f;
			Hound->PerceptionOverride.HearGunshotCm = Hound->PerceptionOverride.HearExplosionCm = 0.f;
			Hound->PerceptionOverride.SmellRadiusCm = 0.f;
			Hound->StartPatrol(Route, nullptr);
			Check(State, Hound->IsOnPatrol(), TEXT("hound patrols"));
			// Passive fire posture: an Aggressive squad would open fire on the hound 9 m ahead by itself (PostureSmoke
			// checks that); here only the player's attack order may start the fight — and a passive squad obeys it.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (State.Time < 2.f)
			{
				return true;
			}
			if (!Hound)
			{
				Check(State, false, TEXT("hound alive"));
				return Finish(State);
			}
			Check(State, Hound->IsOnPatrol() && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("unnoticed patrol: no fight yet"));
			ACodexTacticsPlayerController* Controller = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			if (!Controller)
			{
				Check(State, false, TEXT("player controller"));
				return Finish(State);
			}
			State.HoundHealthAtStart = Hound->GetHealthComponent()->GetCurrentHealth();
			Controller->IssueTargetedShot(Hound); // Ctrl + click on the hound
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				FString::Printf(TEXT("attack order starts the real-time fight at once (phase %d)"), static_cast<int32>(Flow->GetPhase())));
			Check(State, Flow->IsAmbushFight() && Flow->GetWaveIndex() == 1, TEXT("ambush fight, wave 1"));
			Check(State, Encounter->GetAmbushStarts() == 1 && Encounter->GetLastTrigger() == EAmbushTrigger::AttackOrder, TEXT("started once, by the attack order"));
			Controller->IssueTargetedShot(Hound);
			Check(State, Encounter->GetAmbushStarts() == 1, TEXT("a second attack order does not start it again"));
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			const bool bHurt = !Hound || Hound->IsDying() || Hound->GetHealthComponent()->GetCurrentHealth() < State.HoundHealthAtStart;
			if (!bHurt && State.Time < 6.f)
			{
				return true;
			}
			Check(State, !State.bSawCutsceneOrPreparation, TEXT("never through the cutscene / preparation («Начать бой» not needed)"));
			Check(State, Waves && Waves->GetTotalWaveEnemies() == 1, FString::Printf(TEXT("the hound is the wave (%d, nothing spawned)"),
				Waves ? Waves->GetTotalWaveEnemies() : -1));
			Check(State, bHurt, TEXT("the squad opens fire on the hound"));
			Check(State, !Hound || !Hound->IsOnPatrol(), TEXT("the hit hound breaks off its patrol"));
			return Finish(State);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.AmbushSmoke"),
		TEXT("Dev check of the ambush combat start: Ctrl + click on a patrol starts the real-time fight without «Начать бой»; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
