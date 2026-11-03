// Helpers shared by the translation units of AOperativeCharacter (OperativeCharacter*.cpp; UE-only, no Godot reference).
// Private to the module. Moved verbatim from OperativeCharacter.cpp (cleanup day 2026-10-14,
// docs/port/REFACTOR_OPERATIVE_2026-10.md); a named namespace so unity builds see no duplicate file-local helpers.

#pragma once

#include "CoreMinimal.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/EncounterQueries.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Interactables/TurretActor.h"
#include "WorldCollision.h"

namespace OperativeCharacterInternal
{
	/** Spitters, cryo drones and marksmen shoot from a distance; hounds, cutters, frostbitten and brutes close in. */
	inline bool IsRangedEnemyActor(const AActor* Actor)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
		if (!Enemy)
		{
			return false;
		}
		const EEnemyArchetype Type = Enemy->GetArchetype();
		return Type == EEnemyArchetype::Spitter || Type == EEnemyArchetype::CryoDrone || Type == EEnemyArchetype::Marksman;
	}

	/** The actor's feet: its location minus the capsule half height (a character), else its location. */
	inline FVector ShotFeet(const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	}

	/** A valid, shown actor tagged "Enemy" whose health component (if any) is alive. */
	inline bool IsLiveEnemy(const AActor* Actor)
	{
		if (!IsValid(Actor) || !Actor->ActorHasTag(CombatQueries::EnemyTag) || Actor->IsHidden())
		{
			return false;
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		return !Health || Health->IsAlive();
	}

	/**
	 * A live target of Shooter's side (teams, 2026-11-03): for the player's men IsLiveEnemy (the "Enemy" tag; a hostile PMC fighter carries
	 * it); for a hostile fighter an operative of another team, or a monster his team defends itself from (UPmcSquadSubsystem::IsHostileTo).
	 */
	inline bool IsLiveTargetOf(const AOperativeCharacter& Shooter, const AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsHidden())
		{
			return false;
		}
		if (const AOperativeCharacter* Other = Cast<AOperativeCharacter>(Actor))
		{
			return Other->TeamId != Shooter.TeamId && !Other->IsKilledInAction() && Other->HealthComponent && Other->HealthComponent->IsAlive();
		}
		if (Shooter.IsPlayerTeam())
		{
			return IsLiveEnemy(Actor);
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(Shooter.GetWorld());
		return (!Health || Health->IsAlive()) && Pmc && Pmc->IsHostileTo(Shooter, Actor);
	}

	/** The actors Shooter may consider firing at this frame (the registry's "Enemy" actors for the player's men, the team's seen targets for a hostile fighter). */
	inline TArray<AActor*> GatherTargetsFor(const AOperativeCharacter& Shooter)
	{
		TArray<AActor*> Out;
		if (Shooter.IsPlayerTeam())
		{
			for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(Shooter.GetWorld()))
			{
				Out.Add(Enemy);
			}
		}
		else if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(Shooter.GetWorld()))
		{
			Out = Pmc->GetTargetsFor(Shooter);
		}
		return Out;
	}

	/**
	 * Adds the squad (every AOperativeCharacter) and the turrets (Godot "allies") to a squad shot line's ignore list.
	 * EvaluateShotLine runs per candidate enemy per operative per frame: the two world scans are done once per frame
	 * and world (audit 2026-10-11 P2) and again when an actor was spawned since (the persistent level grew).
	 */
	inline void AddShotLineAllies(const UWorld* World, FCollisionQueryParams& Params)
	{
		struct FAlliesCache
		{
			TWeakObjectPtr<const UWorld> World;
			uint64 Frame = MAX_uint64;
			int32 LevelActors = -1;
			TArray<TWeakObjectPtr<const AActor>> Allies;
		};
		static FAlliesCache Cache; // game thread only (actor ticks)
		if (!World)
		{
			return;
		}
		const int32 LevelActors = World->PersistentLevel ? World->PersistentLevel->Actors.Num() : 0;
		if (!UEnemyRegistrySubsystem::IsEnabled() || Cache.World.Get() != World || Cache.Frame != GFrameCounter || Cache.LevelActors != LevelActors)
		{
			Cache.World = World;
			Cache.Frame = GFrameCounter;
			Cache.LevelActors = LevelActors;
			Cache.Allies.Reset();
			for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
			{
				Cache.Allies.Add(*It); // the squad
			}
			for (TActorIterator<ATurretActor> It(World); It; ++It)
			{
				Cache.Allies.Add(*It); // Godot "allies"
			}
		}
		for (const TWeakObjectPtr<const AActor>& Ally : Cache.Allies)
		{
			if (const AActor* Actor = Ally.Get())
			{
				Params.AddIgnoredActor(Actor);
			}
		}
	}
}
