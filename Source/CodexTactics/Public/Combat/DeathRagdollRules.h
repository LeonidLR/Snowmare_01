#pragma once

#include "CoreMinimal.h"
#include "Combat/DeathRagdollTypes.h"
#include "Data/CombatTypes.h"

class USkeletalMesh;
class UPhysicsAsset;

/**
 * Pure evaluation rules and physics asset validation for Character Death Ragdolls.
 * Governed by docs/port/gemini/outbox/049-ragdoll-plan.md (Task 005 Step 1).
 */
class CODEXTACTICS_API FDeathRagdollRules
{
public:
	/** Returns default configuration matching Content/Data/Combat/death_ragdoll.json. */
	static FDeathRagdollConfig GetDefaultConfig();

	/** Deserializes death ragdoll configuration from JSON string. */
	static bool LoadConfigFromJson(const FString& JsonContent, FDeathRagdollConfig& OutConfig);

	/**
	 * Calculates kinetic impulse vector to apply on lethal hit.
	 * Clamps force to MaxImpulseMagnitude and scales by damage type affinity.
	 */
	static FVector CalculateImpulse(
		const FVector& HitDirection,
		float DamageAmount,
		EDamageType DamageType,
		const FDeathRagdollConfig& Config,
		float MassScale = 1.0f
	);

	/**
	 * Evaluates whether a simulating ragdoll body has come to rest.
	 * True if linear & angular velocities are below thresholds, OR if BodyAge exceeds MaxSettleTimeoutSeconds.
	 */
	static bool HasBodySettled(
		const FVector& LinearVelocity,
		const FVector& AngularVelocity,
		float BodyAge,
		const FDeathRagdollConfig& Config
	);

	/** Checks whether another simultaneous ragdoll simulation can be started within the frame budget. */
	static bool CanSimulateRagdoll(int32 CurrentActiveRagdolls, const FDeathRagdollConfig& Config);

	/** Checks whether archetype is flagged to bypass ragdoll and use animation fallback (e.g. Spitter). */
	static bool ShouldFallbackToPose(FName ArchetypeId, const FDeathRagdollConfig& Config);

	/** Returns default root physics bone name ('pelvis'). */
	static FName GetDefaultPelvisBoneName();

	/**
	 * Validates that a skeletal mesh possesses a valid physics asset with required rigid bodies.
	 * Pure validation logic preventing unphysical collapse or crashes.
	 */
	static bool ValidatePhysicsAssetSetup(const USkeletalMesh* Mesh, FString& OutErrorMessage);
};
