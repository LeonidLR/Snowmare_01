// Dev-only headless check (user bug 2026-10-11) on L_PatrolTest (the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.PatrolAmbushSmoke -Map /Game/Maps/L_PatrolTest -Log Smoke-PatrolAmbush.log
// The user's L_PatrolTest holds a marksman and a hound placed in the map without patrol routes: they attacked the squad
// at once, the flow stayed in Exploration and the operatives never fired back (the commander died in exploration).
// Here a frost hound (~12 m) and a marksman (~25 m), both without routes, are spawned in front of the squad and treated
// as placed in the map (ULevelEncounterSubsystem::MarkLevelEnemy). Checks: "auto" with placed enemies is an ambush
// level; even forced to "button" (the worst case) the enemies' own attack / detection starts the real-time ambush fight
// once (no cutscene / preparation / horde intro), both enemies are the wave, and the Aggressive squad returns fire.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace PatrolAmbushSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Enemies;
		TArray<float> HealthAtStart;
		bool bSawCutsceneOrPreparation = false;
		bool bSquadHadTarget = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PatrolAmbushSmoke"));
		return false;
	}

	AEnemyCharacter* SpawnPlaced(UWorld* World, ULevelEncounterSubsystem& Encounter, EEnemyArchetype Type, const FVector& Feet, const FVector& Facing)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Type, Feet + FVector(0.f, 0.f, 90.f), Facing.Rotation()) : nullptr;
		Encounter.MarkLevelEnemy(Enemy); // as if placed in the map (no route, no escort leader)
		return Enemy;
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
			State.bSquadHadTarget |= State.Stage >= 2 && Member->GetCurrentCombatTarget() != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		if (!Flow || !Encounter || !Squad)
		{
			Check(State, false, TEXT("game flow, encounter and squad subsystems"));
			return Finish(State);
		}
		State.bSawCutsceneOrPreparation |= Flow->GetPhase() == ECodexGamePhase::Cutscene || Flow->GetPhase() == ECodexGamePhase::Preparation;
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			if (!Leader)
			{
				Check(State, false, TEXT("squad leader present"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // a map saved with its own enemies: start from a known pair
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring"));
			Check(State, Squad->GetSquadPosture() == ESquadFirePosture::Aggressive, TEXT("default squad posture Aggressive"));
			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			AEnemyCharacter* Hound = SpawnPlaced(World, *Encounter, EEnemyArchetype::FrostHound, SmokeUtils::ClearPoint(World, Feet, Feet + F * 1200.f), -F);
			AEnemyCharacter* Marksman = SpawnPlaced(World, *Encounter, EEnemyArchetype::Marksman, SmokeUtils::ClearPoint(World, Feet, Feet + F * 2500.f), -F);
			if (!Hound || !Marksman)
			{
				Check(State, false, TEXT("hound and marksman spawned"));
				return Finish(State);
			}
			Check(State, !Hound->IsOnPatrol() || !Hound->AssignedPatrolRoute, TEXT("hound without a route"));
			State.Enemies = { Hound, Marksman };
			State.HealthAtStart = { Hound->GetHealthComponent()->GetCurrentHealth(), Marksman->GetHealthComponent()->GetCurrentHealth() };
			// "auto" (the level JSON default) with enemies placed in the map: an ambush level («Start combat» hidden).
			Encounter->ClearCombatStartOverride();
			Check(State, Encounter->LevelHasPlacedEnemies() && Encounter->IsAmbushCombatStart(),
				TEXT("auto + enemies placed in the map (no routes): ambush level"));
			// Worst case: a level forced to "button" - the placed enemies' own attack must still start the fight.
			Encounter->SetCombatStartOverride(ECombatStartMode::Button);
			Check(State, !Encounter->IsAmbushCombatStart(), TEXT("forced button level"));
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (Flow->GetPhase() == ECodexGamePhase::Exploration && State.Time < 30.f)
			{
				return true;
			}
			const EAmbushTrigger Trigger = Encounter->GetLastTrigger();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				FString::Printf(TEXT("the placed enemies' attack starts the real-time fight (phase %s after %.1f s)"),
					*UEnum::GetValueAsString(Flow->GetPhase()), State.Time));
			Check(State, Encounter->GetAmbushStarts() == 1 && (Trigger == EAmbushTrigger::EnemyAttack || Trigger == EAmbushTrigger::PatrolDetection),
				FString::Printf(TEXT("started once by the enemies (%d, %s)"), Encounter->GetAmbushStarts(), *UEnum::GetValueAsString(Trigger)));
			Check(State, Flow->IsAmbushFight() && Flow->GetWaveIndex() == 1 && !Flow->IsAwaitingHordeIntro() && !Flow->IsFreePause(),
				TEXT("ambush fight of wave 1, no horde intro / free pause"));
			const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Waves && Waves->GetTotalWaveEnemies() == 2,
				FString::Printf(TEXT("the placed hound + marksman are the wave (%d, nothing spawned)"), Waves ? Waves->GetTotalWaveEnemies() : -1));
			// From now on: the squad's own fire (health when the fight started).
			for (int32 Index = 0; Index < State.Enemies.Num(); ++Index)
			{
				const AEnemyCharacter* Enemy = State.Enemies[Index].Get();
				State.HealthAtStart[Index] = Enemy ? Enemy->GetHealthComponent()->GetCurrentHealth() : 0.f;
			}
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		default:
		{
			bool bHurt = false;
			for (int32 Index = 0; Index < State.Enemies.Num(); ++Index)
			{
				const AEnemyCharacter* Enemy = State.Enemies[Index].Get();
				bHurt |= !Enemy || Enemy->IsDying() || Enemy->GetHealthComponent()->GetCurrentHealth() < State.HealthAtStart[Index];
			}
			if (!(bHurt && State.bSquadHadTarget) && State.Time < 12.f)
			{
				return true;
			}
			Check(State, !State.bSawCutsceneOrPreparation, TEXT("never through the cutscene / preparation"));
			Check(State, bHurt && State.bSquadHadTarget, FString::Printf(TEXT("the squad returns fire (an enemy hurt after %.1f s, squad had a target %d)"),
				State.Time, State.bSquadHadTarget ? 1 : 0));
			int32 Alive = 0;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Alive += Member->HealthComponent && Member->HealthComponent->IsAlive() ? 1 : 0;
			}
			Check(State, Flow->GetPhase() != ECodexGamePhase::GameOver, FString::Printf(TEXT("no game over (%d operatives alive)"), Alive));
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
		TEXT("CodexTactics.PatrolAmbushSmoke"),
		TEXT("Dev check (L_PatrolTest): enemies placed in the map without routes attack -> the ambush fight starts -> the squad returns fire; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
