#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "DeathRagdollTypes.generated.h"

/**
 * Data configuration for character death ragdoll simulation and settlement.
 * Governed by docs/port/gemini/outbox/049-ragdoll-plan.md and Content/Data/Combat/death_ragdoll.json.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDeathRagdollConfig
{
	GENERATED_BODY()

	/** Global toggle for death ragdoll simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	bool bEnabled = true;

	/** Hard cap on simultaneously simulating ragdolls to guarantee frame-time budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSimultaneousRagdolls = 4;

	/** Delay in seconds before ragdoll engages to allow death montage blend-in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float RagdollDelaySeconds = 0.0f;

	/** Forced freeze timeout if simulation does not come to rest naturally. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.5"))
	float MaxSettleTimeoutSeconds = 3.5f;

	/** Linear velocity threshold below which ragdoll is considered settled (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float LinearSettleThresholdCmS = 15.0f;

	/** Angular velocity threshold below which ragdoll is considered settled (rad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float AngularSettleThresholdRadS = 0.25f;

	/** Maximum kinetic impulse magnitude allowed (g*cm/s) to prevent physics explosion/jitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "1000.0"))
	float MaxImpulseMagnitude = 60000.0f;

	/** Maximum launch speed allowed after death impulse for normal damage (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "100.0"))
	float MaxLaunchSpeedCmS = 600.0f;

	/** Maximum launch speed allowed after death impulse for Explosive damage (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "100.0"))
	float MaxLaunchSpeedExplosiveCmS = 900.0f;

	/** Angular damping applied during simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float AngularDamping = 1.5f;

	/** Linear damping applied during simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float LinearDamping = 0.5f;

	/** Upward directional bias added to hit impulse vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float UpwardImpulseBias = 0.25f;

	/** Minimum damage factor scaling clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float MinDamageFactor = 0.5f;

	/** Maximum damage factor scaling clamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float MaxDamageFactor = 3.0f;

	/** Reference damage amount used as divisor for damage scaling factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float ReferenceDamage = 25.0f;

	/** Fraction of capsule half-height used as floor settle threshold for pelvis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	float PelvisFloorThresholdFraction = 0.5f;

	/** Base kinetic impulse scale per damage type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	TMap<EDamageType, float> ImpulseScales;

	/** Archetypes that must bypass ragdoll and use animation fallback (e.g. Spitter, CryoDrone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll")
	TSet<FName> FallbackArchetypes;
};
