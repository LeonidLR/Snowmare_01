#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

/**
 * Tactical line of sight (Sprint 08, TANDEM «SPRINT 08 DIRECTIVE: Tactical Line of Sight, Cover Occlusion & Ghost
 * Silhouette System»; UE-only, no Godot reference — Godot showed every actor all the time). Physical heights against
 * 60 cm barricades (user decision 2026-10-05: the barricade itself is 60 cm), symmetric for the squad and the enemies.
 * Pure rules, tested in CodexTactics.Combat.Sight.*; UTacticalSightSubsystem traces them in the world.
 */
namespace SightRules
{
	/** Barricade / low obstacle height, cm. */
	inline constexpr float CoverHeightCm = 60.f;
	/** Firing (muzzle flash, tracer, a blow) demasks the shooter for this long, s. */
	inline constexpr float DemaskSeconds = 2.f;
	/** Operatives hear unseen enemies (snow crunch, growls) this close, cm: a «sound» silhouette appears. */
	inline constexpr float SquadHearingCm = 1200.f;
	/** Enemies hear an operative this close (cm); a crawling (prone) one only within ProneHearingCm. */
	inline constexpr float EnemyHearingCm = 1200.f;
	inline constexpr float ProneHearingCm = 500.f;
	/** Blind fire at a ghost silhouette: hit chance x (1 - 0.8) (directive -40 %; user decision 2026-10-05 after the Jev check: -80 %). */
	inline constexpr float BlindFireAccuracyMultiplier = 0.2f;
	/** A blind shot can only hit the enemy while it is still this close to its ghost (cm). */
	inline constexpr float BlindFireHitRadiusCm = 150.f;
	/** Enemies within this distance of one that perceives an operative learn where he is (pack call), cm. */
	inline constexpr float PackAlertCm = 1500.f;
	/**
	 * An enemy forgets an operative it no longer perceives after this long, s, or once it searched his last known spot
	 * (within ForgetArrivalCm) for SearchSeconds (user decision 2026-10-05 after the Jev check: search, then give up).
	 */
	inline constexpr float ForgetSeconds = 20.f;
	inline constexpr float ForgetArrivalCm = 250.f;
	inline constexpr float SearchSeconds = 5.f;

	/** Observer eye height above the feet: standing 160, crouching 95, prone 25 cm. */
	CODEXTACTICS_API float EyeHeight(EOperativeStance Stance);
	/** Target profile height above the feet (the point looked at): standing 150, crouching 90, prone 25 cm. */
	CODEXTACTICS_API float ProfileHeight(EOperativeStance Stance);

	/**
	 * Height of the sight line where it crosses a cover CoverDistanceCm from the observer (planar), the target
	 * TargetDistanceCm away; heights above a common ground.
	 */
	CODEXTACTICS_API float LineHeightAt(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm);

	/** The line passes over a cover of CoverTopCm at that point (flat ground model; the game traces the real geometry). */
	CODEXTACTICS_API bool ClearsCover(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm,
		float CoverTopCm = CoverHeightCm);

	/** How far an enemy hears an operative in Stance, cm. */
	CODEXTACTICS_API float EnemyHearingRadius(EOperativeStance Stance);

	/** Blind fire hit chance: x0.2, and 0 when the enemy left its ghost's spot. */
	CODEXTACTICS_API float BlindFireHitChance(float BaseHitChance, float EnemyDistanceFromGhostCm);

	/** An enemy keeps pursuing an unperceived operative's last known spot until it searched there or the memory is too old. */
	CODEXTACTICS_API bool ShouldForget(float SecondsSinceSeen, float SecondsSearchingThere);
}
