// Dev-only headless check (user bug 2026-10-11) on L_PatrolTest (the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.PatrolAmbushSmoke -Map /Game/Maps/L_PatrolTest -Log Smoke-PatrolAmbush.log
// The user's L_PatrolTest holds a marksman and a hound placed in the map without patrol routes: they attacked the squad
// at once, the flow stayed in Exploration and the operatives never fired back (the commander died in exploration).
// Here a frost hound (~24 m to the side: beyond its 12 m smell, even x1.25 while searching) and a marksman (~28 m
// ahead), both without routes and facing away, are spawned and treated as placed in the map
// (ULevelEncounterSubsystem::MarkLevelEnemy). Checks: "auto" with placed enemies is an ambush level. User request
// 2026-10-11: unrouted placed enemies GUARD their post - for 6 s they neither move nor attack and no fight starts; a trap
// next to the hound sends it searching (no fight), then it walks back to its post; a squad gunshot they hear wakes them.
// Even forced to "button" (the worst case) their detection starts the real-time ambush fight once (no cutscene /
// preparation / horde intro), both enemies are the wave, and the Aggressive squad returns fire. The squad is Passive until the fight
// starts (2026-10-19: an Aggressive squad would open fire on the placed enemies itself, also on a «button» level - PostureSmoke).

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
		TArray<FVector> Posts;
		float SquadHealthAtStart = 0.f;
		float MaxDrift = 0.f;
		bool bSawCutsceneOrPreparation = false;
		bool bSquadHadTarget = false;
		bool bSawSearch = false;
	};

	float SquadHealth(USquadSubsystem& Squad)
	{
		float Sum = 0.f;
		for (const AOperativeCharacter* Member : Squad.GetMembers())
		{
			Sum += Member && Member->HealthComponent ? Member->HealthComponent->GetCurrentHealth() : 0.f;
		}
		return Sum;
	}

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
			// Semantics 2026-10-19: an AGGRESSIVE squad opens fire on a placed enemy it sees in exploration (on a «button» level too) -
			// this smoke is about the enemies starting it: the squad holds fire (Passive) until the fight is on, then Aggressive.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector R = FVector::CrossProduct(FVector::UpVector, F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			// Facing away from the squad, out of smell / footstep range: nothing to perceive while the squad stands still.
			AEnemyCharacter* Hound = SpawnPlaced(World, *Encounter, EEnemyArchetype::FrostHound, SmokeUtils::ClearPoint(World, Feet, Feet + R * 2400.f), R);
			AEnemyCharacter* Marksman = SpawnPlaced(World, *Encounter, EEnemyArchetype::Marksman, SmokeUtils::ClearPoint(World, Feet, Feet + F * 2800.f), F);
			if (!Hound || !Marksman)
			{
				Check(State, false, TEXT("hound and marksman spawned"));
				return Finish(State);
			}
			Check(State, !Hound->AssignedPatrolRoute && !Marksman->AssignedPatrolRoute, TEXT("hound and marksman without a route"));
			Check(State, Hound->IsGuardingPost() && Marksman->IsGuardingPost() && Hound->IsOnPatrol() && Marksman->IsOnPatrol(),
				TEXT("placed without a route: both guard their post (on patrol duty)"));
			State.Enemies = { Hound, Marksman };
			State.Posts = { Hound->GetActorLocation(), Marksman->GetActorLocation() };
			State.SquadHealthAtStart = SquadHealth(*Squad);
			State.HealthAtStart = { Hound->GetHealthComponent()->GetCurrentHealth(), Marksman->GetHealthComponent()->GetCurrentHealth() };
			// "auto" (the level JSON default) with enemies placed in the map: an ambush level («Start combat» hidden).
			Encounter->ClearCombatStartOverride();
			Check(State, Encounter->LevelHasPlacedEnemies() && Encounter->IsAmbushCombatStart(),
				TEXT("auto + enemies placed in the map (no routes): ambush level"));
			// Worst case: a level forced to "button" - the placed enemies' own attack must still start the fight.
			Encounter->SetCombatStartOverride(ECombatStartMode::Button);
			Check(State, !Encounter->IsAmbushCombatStart(), TEXT("forced button level"));
			State.Stage = 10;
			State.Time = 0.f;
			return true;
		}
		case 10:
		{
			// Guarding: 6 s without moving off the post, attacking or starting the fight.
			for (int32 Index = 0; Index < State.Enemies.Num(); ++Index)
			{
				if (const AEnemyCharacter* Enemy = State.Enemies[Index].Get())
				{
					State.MaxDrift = FMath::Max(State.MaxDrift, static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), State.Posts[Index])));
				}
			}
			if (State.Time < 6.f)
			{
				return true;
			}
			const AEnemyCharacter* Hound = State.Enemies[0].Get();
			const AEnemyCharacter* Marksman = State.Enemies[1].Get();
			Check(State, Hound && Marksman && Hound->IsGuardingPost() && Marksman->IsGuardingPost(), TEXT("6 s later both still guard"));
			Check(State, State.MaxDrift < 60.f, FString::Printf(TEXT("they stay at their posts (max drift %.0f cm)"), State.MaxDrift));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("no fight while they guard"));
			Check(State, SquadHealth(*Squad) >= State.SquadHealthAtStart - 0.01f, TEXT("nobody attacked the squad"));
			// A trap next to the hound (away from the squad, > 20 m from the marksman): the hound searches, no fight.
			Encounter->SetPatrolSearchSecondsOverride(2.f);
			if (Hound && Squad->GetLeader())
			{
				const FVector Side = (Hound->GetActorLocation() - Squad->GetLeader()->GetActorLocation()).GetSafeNormal2D();
				AEnemyCharacter::AlertPatrolsNearTrap(World, Hound->GetActorLocation() + Side * 500.f);
				State.bSawSearch = Hound->IsSearching();
			}
			Check(State, State.bSawSearch, TEXT("a trap sends the guarding hound searching"));
			Check(State, Marksman && !Marksman->IsSearching(), TEXT("the marksman out of the trap radius keeps guarding"));
			State.Stage = 11;
			State.Time = 0.f;
			return true;
		}
		case 11:
		{
			const AEnemyCharacter* Hound = State.Enemies[0].Get();
			const float Off = Hound ? static_cast<float>(FVector::Dist2D(Hound->GetActorLocation(), State.Posts[0])) : -1.f;
			const bool bBack = Hound && !Hound->IsSearching() && Hound->IsGuardingPost() && Off < 100.f && Hound->GetVelocity().Size2D() < 20.f;
			if (!bBack && State.Time < 15.f && Flow->GetPhase() == ECodexGamePhase::Exploration)
			{
				return true;
			}
			Check(State, bBack, FString::Printf(TEXT("search over: the hound walks back to its post (%.0f cm off after %.1f s)"), Off, State.Time));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("the trap search started no fight"));
			Encounter->SetPatrolSearchSecondsOverride(-1.f);
			// A squad gunshot both hear (hound 45 m, marksman 40 m): their perception wakes them -> the fight.
			if (const AOperativeCharacter* Leader = Squad->GetLeader())
			{
				AEnemyCharacter::NotifySquadNoise(World, Leader->GetActorLocation(), ESquadNoise::Gunshot);
			}
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
			const AEnemyCharacter* WokenHound = State.Enemies[0].Get();
			const AEnemyCharacter* WokenMarksman = State.Enemies[1].Get();
			Check(State, WokenHound && WokenMarksman && !WokenHound->IsOnPatrol() && !WokenMarksman->IsOnPatrol() && !WokenHound->IsGuardingPost(),
				TEXT("perceived the squad: both leave their posts and engage"));
			Check(State, Encounter->GetAmbushStarts() == 1 && (Trigger == EAmbushTrigger::EnemyAttack || Trigger == EAmbushTrigger::PatrolDetection),
				FString::Printf(TEXT("started once by the enemies (%d, %s)"), Encounter->GetAmbushStarts(), *UEnum::GetValueAsString(Trigger)));
			Check(State, Flow->IsAmbushFight() && Flow->GetWaveIndex() == 1 && !Flow->IsAwaitingHordeIntro() && !Flow->IsFreePause(),
				TEXT("ambush fight of wave 1, no horde intro / free pause"));
			const UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Waves && Waves->GetTotalWaveEnemies() == 2,
				FString::Printf(TEXT("the placed hound + marksman are the wave (%d, nothing spawned)"), Waves ? Waves->GetTotalWaveEnemies() : -1));
			// From now on: the squad's own fire (health when the fight started) - Aggressive again.
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
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
		TEXT("Dev check (L_PatrolTest): enemies placed in the map without routes guard their post until they perceive the squad -> the ambush fight starts -> the squad returns fire; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
