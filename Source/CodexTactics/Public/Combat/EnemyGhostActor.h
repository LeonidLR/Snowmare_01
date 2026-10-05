#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyGhostActor.generated.h"

class AEnemyCharacter;
class UMaterialInterface;

/**
 * Last known position silhouette of an enemy the squad lost sight of (Sprint 08-C / 8-D; UE-only). A frozen copy of
 * the enemy's meshes at the moment of the break (skeletal meshes as a posed copy) in the translucent stasis material
 * (/Game/VFX/Materials/M_TacticalStasis, the turn-based out-of-queue ghost). It stays exactly where the enemy was last
 * confirmed; a «heard» silhouette (operative within 12 m) is moved to the sound's source by UTacticalSightSubsystem.
 * The player can order blind fire at it (-80 % accuracy).
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AEnemyGhostActor : public AActor
{
	GENERATED_BODY()

public:
	AEnemyGhostActor();

	/** Copies Enemy's visible meshes (pose and transforms) into this ghost, placed where the enemy stands now. */
	void InitFrom(AEnemyCharacter& Enemy, UMaterialInterface* Material);

	/** Moves the silhouette (the enemy's pose is copied again) — only for a heard one. */
	void SnapTo(AEnemyCharacter& Enemy);

	AEnemyCharacter* GetSource() const { return Source.Get(); }

	/** The enemy's feet when the silhouette was placed (the blind-fire aim point is above them). */
	FVector GetLastKnownFeet() const { return LastKnownFeet; }

	/** Point to shoot at: the profile height above the last known feet. */
	FVector GetAimPoint() const;

	bool IsHeard() const { return bHeard; }
	void SetHeard(bool bInHeard) { bHeard = bInHeard; }

private:
	void CopyMeshes(AEnemyCharacter& Enemy);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMeshComponent>> Copies;

	TWeakObjectPtr<AEnemyCharacter> Source;
	FVector LastKnownFeet = FVector::ZeroVector;
	float ProfileHeight = 150.f;
	bool bHeard = false;
};
