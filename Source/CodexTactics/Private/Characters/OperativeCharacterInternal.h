// Helpers shared by the translation units of AOperativeCharacter (OperativeCharacter*.cpp; UE-only, no Godot reference).
// Private to the module. Moved verbatim from OperativeCharacter.cpp (cleanup day 2026-10-14,
// docs/port/REFACTOR_OPERATIVE_2026-10.md); a named namespace so unity builds see no duplicate file-local helpers.

#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "GameFramework/Character.h"

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
		if (!IsValid(Actor) || !Actor->ActorHasTag(FName(TEXT("Enemy"))) || Actor->IsHidden())
		{
			return false;
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		return !Health || Health->IsAlive();
	}
}
