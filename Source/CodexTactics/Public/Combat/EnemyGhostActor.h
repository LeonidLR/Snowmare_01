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
 * confirmed; a «heard» silhouette (operative within 12 m) follows the sound's source every frame and plays the enemy's
 * live pose (its walk / run), it freezes where it was last heard. The hidden enemy keeps evaluating its pose
 * (UTacticalSightSubsystem::KeepPoseWhileHidden), and a new silhouette copies the pose again on its first frames, so it
 * never shows the reference (T) pose of an enemy that was not on screen. The player can order blind fire at it (-80 %
 * accuracy).
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AEnemyGhostActor : public AActor
{
	GENERATED_BODY()

public:
	AEnemyGhostActor();

	/** Copies Enemy's visible meshes (pose and transforms) into this ghost, placed where the enemy stands now. */
	void InitFrom(AEnemyCharacter& Enemy, UMaterialInterface* Material);

	/** Moves the silhouette to the enemy and copies its pose again (the meshes are rebuilt only when they changed). */
	void SnapTo(AEnemyCharacter& Enemy);

	virtual void Tick(float DeltaSeconds) override;

	AEnemyCharacter* GetSource() const { return Source.Get(); }

	/** The enemy's feet when the silhouette was placed (the blind-fire aim point is above them). */
	FVector GetLastKnownFeet() const { return LastKnownFeet; }

	/** Point to shoot at: the profile height above the last known feet. */
	FVector GetAimPoint() const;

	bool IsHeard() const { return bHeard; }
	/** Heard: the silhouette follows the enemy and animates every frame; not heard: it freezes where it is. */
	void SetHeard(bool bInHeard);

private:
	void CopyMeshes(AEnemyCharacter& Enemy);
	/** Copies the source meshes' poses into the copies; bMove also moves the silhouette (and its feet) to the enemy. */
	void SyncFromSource(bool bMove);
	void UpdateTickEnabled();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMeshComponent>> Copies;
	/** The enemy's mesh each copy was made from (same index). */
	TArray<TWeakObjectPtr<class UMeshComponent>> CopySources;
	/** Frames the pose is still copied after placing (the hidden enemy's bones refresh from the next frame on). */
	int32 PendingPoseFrames = 0;

	TWeakObjectPtr<AEnemyCharacter> Source;
	FVector LastKnownFeet = FVector::ZeroVector;
	float ProfileHeight = 150.f;
	bool bHeard = false;
};
