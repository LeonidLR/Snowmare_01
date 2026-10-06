#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Data/CombatTypes.h"
#include "PerceptionRules.generated.h"

/**
 * Enemy perception of the squad before a fight (Sprint 11 follow-up, user request 2026-10-06; UE-only, no Godot
 * reference): sight (range, field of view, stance visibility, suspicion build-up), hearing (squad movement by gait,
 * gunshots, thrown grenades) and smell (frost hounds only, no wind). Pure rules, tested in CodexTactics.AI.Perception.*;
 * the per-archetype values live in Content/Data/AI/enemy_perception.json (Data/EnemyPerception.h) and AEnemyCharacter
 * applies them on patrol / search (detection breaks the patrol into Engage). The cover rule is Sprint 08's (SightRules):
 * the world trace from the eyes to the target profile decides whether the line is clear — a prone operative behind a
 * 60 cm barricade stays unseen.
 */

/** How loud the squad member moves (input of the hearing radius). */
UENUM(BlueprintType)
enum class ESquadMovementNoise : uint8
{
	/** Standing still (any stance): no footstep noise. */
	Still,
	/** Crawling prone. */
	Crawl,
	/** Walking crouched. */
	CrouchWalk,
	/** Walking upright. */
	Walk,
	/** Sprinting. */
	Run
};

/** Detection parameters of one enemy archetype (designer data, enemy_perception.json). Distances in cm. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemyPerceptionParams
{
	GENERATED_BODY()

	/** Sees a standing operative this far (a crouched / prone one: times the stance visibility), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float SightRangeCm = 2500.f;

	/** Field of view half-angle around the body's facing, degrees (180 = all round). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0", ClampMax = "180"))
	float SightHalfAngleDeg = 50.f;

	/** Sight range multipliers by the target's stance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float StandingVisibility = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float CrouchingVisibility = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float ProneVisibility = 0.4f;

	/** Within this distance it notices a visible operative whatever its facing (peripheral vision), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float ProximityCm = 300.f;

	/** Seconds of continuous sight at the edge of its range until it is sure (3x faster point-blank); 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float TimeToDetectSeconds = 0.8f;

	/** Suspicion lost per second without sight (0..1 scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Sight", meta = (ClampMin = "0"))
	float SuspicionDecayPerSecond = 0.5f;

	/**
	 * Hears squad footsteps this far, by gait, cm (no line of sight needed; walls in between cut it, see
	 * HearingOcclusionPerWall). Lowered 2026-10-06 after the user's playtest (enemies heard running operatives from
	 * 18-25 m through walls).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearWalkCm = 550.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearRunCm = 1100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearCrouchWalkCm = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearCrawlCm = 100.f;

	/** Footstep hearing radius x this for every wall (world-blocking hit) between the ear and the feet (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0", ClampMax = "1"))
	float HearingOcclusionPerWall = 0.5f;

	/** Hears a squad gunshot this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearGunshotCm = 4000.f;

	/** Hears a squad grenade explode this far, cm (placed charges / traps use the trap search radius instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Hearing", meta = (ClampMin = "0"))
	float HearExplosionCm = 6000.f;

	/** Smells an operative this far, cm (frost hounds only — forced to 0 for every other archetype). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Smell", meta = (ClampMin = "0"))
	float SmellRadiusCm = 0.f;
};

/** The patrol search after a trap / placed charge went off (user amendment 2026-10-06). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FPatrolSearchParams
{
	GENERATED_BODY()

	/** Seconds the patrol hunts for the squad before it returns to its route (a level JSON can override it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float DurationSeconds = 60.f;

	/** Random sweep points lie within this radius of the blast, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float SweepRadiusCm = 1200.f;

	/** Search pace = patrol walk speed x this (never above its normal speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float SpeedMultiplier = 1.6f;

	/** Sight / hearing / smell ranges x this while searching (heightened perception). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float PerceptionMultiplier = 1.25f;

	/** Looks around this long at every sweep point, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float LookAroundSeconds = 2.f;

	/** At a sweep point this close (planar), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float ArriveCm = 150.f;

	/** Gives a sweep leg up after this long (blocked, unreachable), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Perception|Search", meta = (ClampMin = "0"))
	float LegTimeoutSeconds = 10.f;
};

namespace PerceptionRules
{
	/** Built-in defaults per archetype (used when enemy_perception.json lacks an entry). */
	CODEXTACTICS_API FEnemyPerceptionParams GetArchetypeDefaults(EEnemyArchetype Archetype);

	/** Only frost hounds track by smell. */
	CODEXTACTICS_API bool CanSmell(EEnemyArchetype Archetype);

	/** Params with smell cleared for archetypes that cannot smell. */
	CODEXTACTICS_API FEnemyPerceptionParams Sanitize(EEnemyArchetype Archetype, const FEnemyPerceptionParams& Params);

	/** Every range / radius x Multiplier (the field of view and timings stay). */
	CODEXTACTICS_API FEnemyPerceptionParams Scaled(const FEnemyPerceptionParams& Params, float Multiplier);

	/** Sight range multiplier of a target in Stance. */
	CODEXTACTICS_API float StanceVisibility(const FEnemyPerceptionParams& Params, EOperativeStance Stance);

	/** How far it sees a target in Stance, cm. */
	CODEXTACTICS_API float EffectiveSightRange(const FEnemyPerceptionParams& Params, EOperativeStance Stance);

	/** Target within the field of view (planar angle between Forward and the direction to it). */
	CODEXTACTICS_API bool IsInFieldOfView(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target);

	/**
	 * Sees the target: within the stance range, inside the field of view (or within the proximity radius), and the
	 * world line from its eyes to the target's profile is clear (bLineClear — Sprint 08 cover rule).
	 */
	CODEXTACTICS_API bool CanSee(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target,
		EOperativeStance TargetStance, bool bLineClear);

	/**
	 * Flat-ground cover check of the Sprint 08 rule (SightRules::ClearsCover) from a standing enemy's eyes to a target in
	 * TargetStance behind a cover CoverDistanceCm from the enemy: a prone target behind 60 cm stays hidden.
	 */
	CODEXTACTICS_API bool IsLineClearOverCover(EOperativeStance TargetStance, float CoverDistanceCm, float TargetDistanceCm,
		float CoverTopCm = 60.f, float ObserverEyeHeightCm = 160.f);

	/** Gait of an operative: still below 20 cm/s, else by stance (standing: sprinting = run). */
	CODEXTACTICS_API ESquadMovementNoise ClassifyMovement(EOperativeStance Stance, float SpeedCmS, bool bSprinting);

	/** Hearing radius of a gait, cm (Still = 0). */
	CODEXTACTICS_API float HearingRadius(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise);

	CODEXTACTICS_API bool HearsMovement(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm);

	/** Most walls counted between the ear and the feet (more change nothing audible). */
	constexpr int32 MaxHearingOccluders = 3;

	/** Radius x PerWallFactor^Walls (Walls clamped to 0..MaxHearingOccluders, the factor to 0..1). */
	CODEXTACTICS_API float OccludedRadius(float RadiusCm, int32 Walls, float PerWallFactor);

	/** Footsteps heard DistanceCm away with Walls world-blocking hits between the ear and the feet. */
	CODEXTACTICS_API bool HearsMovementThroughWalls(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm, int32 Walls);
	CODEXTACTICS_API bool HearsGunshot(const FEnemyPerceptionParams& Params, float DistanceCm);
	CODEXTACTICS_API bool HearsExplosion(const FEnemyPerceptionParams& Params, float DistanceCm);

	/** Smells an operative DistanceCm away (smell radius > 0 only for hounds after Sanitize). */
	CODEXTACTICS_API bool Smells(const FEnemyPerceptionParams& Params, float DistanceCm);

	/**
	 * Suspicion meter 0..1 after DeltaSeconds: while it sees someone DistanceCm away (EffectiveRangeCm its stance range)
	 * it fills at 1 / TimeToDetect, up to 3x faster point-blank; without sight it decays. 1 = detected.
	 */
	CODEXTACTICS_API float StepSuspicion(const FEnemyPerceptionParams& Params, float Current, float DeltaSeconds, bool bSeesSomeone,
		float DistanceCm, float EffectiveRangeCm);
}
