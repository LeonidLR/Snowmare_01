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

	/**
	 * The MaxSettleTimeoutSeconds forced freeze waits while the ragdoll's root body is faster than this (cm/s): a body still
	 * falling is never frozen in mid-air (067: the 4.9x Brute's slow topple was frozen with its pelvis body 139 cm up at
	 * 136 cm/s). FCorpseConfig::MaxSettleSeconds (8 s) still freezes it in the end.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float TimeoutFreezeMaxSpeedCmS = 50.0f;

	/** Share of the launch speed limit the pelvis and the bodies below it get from the death impulse (headroom for limb whip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float RootLaunchSpeedFraction = 0.75f;

	/**
	 * Seconds after death during which every body's linear speed (3D) is held under the launch speed limit: the launch flight.
	 * 1.5 s (067, measured): with 0.6 s a blast target's hand reached 901 cm/s at 0.66 s on landing; the whole simulation
	 * (3.5 s) slowed the 4.9x Brute's topple (starts ~2 s after death) so much that the settle timeout froze it in mid-air
	 * (pelvis body 132 cm above the floor, 126 cm/s); a downward-free clamp let the Brute's animation hand 2904 cm/s
	 * downward to its thigh body in the first frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float LaunchClampWindowSeconds = 1.5f;

	/** Per-body angular speed cap while the ragdoll simulates (rad/s): no limb whip from the impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.1"))
	float MaxAngularSpeedRadS = 10.0f;

	/** Body mass (kg) the impulse scales are tuned for: impulse x (mass / this), delta-V = impulse / mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "1.0"))
	float ReferenceBodyMassKg = 80.0f;

	/** The settled pelvis collision body may sit this far below the floor before the freeze lifts the mesh (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float MaxPelvisSinkCm = 20.0f;

	/** Largest lift the freeze safety net applies (cm); covers the Brute's 99 cm thick pelvis body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float MaxFloorLiftCm = 100.0f;

	/** The floor trace starts this far above the pelvis bone (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float FloorTraceUpCm = 100.0f;

	/** The floor trace ends this far below the pelvis bone (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "1.0"))
	float FloorTraceDownCm = 300.0f;

	/** A floor hit must face up at least this much (impact normal Z). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FloorMinNormalZ = 0.7f;

	/** A floor hit higher than this above the pelvis bone is an overhead obstacle, not the floor (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Ragdoll", meta = (ClampMin = "0.0"))
	float FloorMaxAbovePelvisCm = 40.0f;

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
