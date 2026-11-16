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

	/** Returns maximum allowed launch speed for given damage type (Explosive vs normal). */
	static float GetMaxLaunchSpeed(EDamageType DamageType, const FDeathRagdollConfig& Config);

	/** Clamps linear velocity vector so its magnitude does not exceed MaxSpeed. */
	static FVector ClampLinearVelocity(const FVector& Velocity, float MaxSpeed);

	/**
	 * Velocity change for the death impulse so that InitialVelocity + result never exceeds MaxSpeed
	 * (a body already moving faster - killed mid-sprint - is slowed to MaxSpeed along the combined direction).
	 */
	static FVector ClampLaunchVelocityChange(const FVector& InitialVelocity, const FVector& VelocityChange, float MaxSpeed);

	/**
	 * True when a floor-trace hit under a corpse is the floor: it faces up (normal Z >= FloorMinNormalZ) and is not an
	 * overhead obstacle (no higher than FloorMaxAbovePelvisCm above the pelvis bone).
	 */
	static bool IsCorpseFloorHit(float ImpactZ, float ImpactNormalZ, float PelvisZ, const FDeathRagdollConfig& Config);

	/**
	 * Upward correction (cm) for a frozen corpse whose pelvis COLLISION BODY bottom settled more than MaxSinkCm below the floor:
	 * the sink depth, capped at MaxLiftCm; 0 when it rests on / above the floor or within MaxSinkCm.
	 * The body bottom, not the pelvis bone: the 4.9x scaled Brute's bone sits 99 cm above its own body bottom, so a bone test
	 * misses a 1 m sink.
	 */
	static float CalculatePelvisFloorCorrection(float PelvisBottomZ, float FloorZ, float MaxSinkCm, float MaxLiftCm);

	/**
	 * The forced settle timeout: BodyAge reached MaxSettleTimeoutSeconds and the root body is no faster than
	 * TimeoutFreezeMaxSpeedCmS (a body still falling waits; FCorpseConfig::MaxSettleSeconds freezes it in the end).
	 */
	static bool IsRagdollTimeoutFreeze(float BodyAge, float RootSpeedCmS, const FDeathRagdollConfig& Config);

	/**
	 * Evaluates whether a simulating ragdoll body has come to rest.
	 * True if linear & angular velocities are below thresholds, OR on the forced timeout (IsRagdollTimeoutFreeze).
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

	/** Returns default root physics bone name ('pelvis'); UCorpseSubsystem::ResolvePelvisBone falls back for skeletons without it. */
	static FName GetDefaultPelvisBoneName();

	/**
	 * Validates that a skeletal mesh possesses a valid physics asset with required rigid bodies.
	 * Pure validation logic preventing unphysical collapse or crashes.
	 */
	static bool ValidatePhysicsAssetSetup(const USkeletalMesh* Mesh, FString& OutErrorMessage);
};
