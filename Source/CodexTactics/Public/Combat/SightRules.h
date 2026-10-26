#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"

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
	/**
	 * The squad notices an enemy this close (2D, cm) without a line of sight (bug hunt 2026-10-18): hounds / frostbitten
	 * stop 1.65-1.75 m off (outside their bite, HANDOFF §9) - one doing so right behind a wall's corner stayed hidden,
	 * so the operative holding that corner never fired at it. Below the 2.5-3 m of the sight checks (SightSmoke).
	 */
	inline constexpr float SquadTouchRevealCm = 200.f;
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

	/**
	 * A last-known silhouette of an enemy the squad lost while EXPLORING fades after this, s (2026-10-19; the minimap's last-known
	 * enemy icon fades over the same time - UMinimapSubsystem::EnemyFadeSeconds). In a fight the silhouette stays (blind fire).
	 */
	inline constexpr float LastKnownFadeSeconds = 12.f;

	/**
	 * The squad's own sight hides what no operative sees (2026-10-19, symmetric sight: hidden from them = they are hidden from us):
	 * in a real-time / paused wave fight (the Sprint 08 rules) and - bExplorationSight, Codex.Sight.Exploration - while exploring.
	 * Never in the turn-based fight (its own rules) or the other phases (preparation, cutscene, after the fight: everything shown).
	 */
	CODEXTACTICS_API bool IsSquadSightPhase(ECodexGamePhase Phase, ECodexCombatMode CombatMode, bool bExplorationSight);

	/** An unheard last-known silhouette of an enemy lost while exploring goes after LastKnownFadeSeconds (kept in a fight). */
	CODEXTACTICS_API bool ShouldDropLastKnownGhost(bool bInFight, bool bHeard, float SecondsSinceLastKnown);

	/**
	 * An operative's field of view (game designer 2026-10-20, screenshot: "the other two crawl away in the opposite direction, not looking
	 * at the enemies - yet the enemies are fully visible"): his own eyes see only inside this planar angle round his facing, per stance, degrees
	 * (total angle, not half). Data: the Codex.Sight.Fov* console variables (ai_tuning.json "cvars"). Inside SquadTouchRevealCm everything is
	 * still noticed all round; the peek view (PeekSightRules) is its own cone and overrides this while he peeks.
	 */
	struct FSightFovConfig
	{
		float StandingDeg = 140.f;
		float CrouchingDeg = 140.f;
		float ProneDeg = 110.f;
	};

	/** The field of view of a stance, degrees (a stance beyond the three counts as standing). */
	CODEXTACTICS_API float FieldOfViewDeg(const FSightFovConfig& Config, EOperativeStance Stance);

	/**
	 * Target lies inside the field of view: the PLANAR angle between Facing and the line Eye -> Target is at most FovDeg / 2. A target
	 * straight above / below the eye (planar distance ~0) or a zero Facing counts as inside; FovDeg >= 360 sees all round.
	 */
	CODEXTACTICS_API bool IsInFieldOfView(const FVector& Eye, const FVector& Facing, const FVector& Target, float FovDeg);

	/** Observer eye height above the feet: standing 160, crouching 95, prone 25 cm. */
	CODEXTACTICS_API float EyeHeight(EOperativeStance Stance);
	/** Target profile height above the feet (the point looked at): standing 150, crouching 90, prone 25 cm. */
	CODEXTACTICS_API float ProfileHeight(EOperativeStance Stance);

	/**
	 * Height of the sight line where it crosses a cover CoverDistanceCm from the observer (planar), the target
	 * TargetDistanceCm away; heights above a common ground.
	 */
	CODEXTACTICS_API float LineHeightAt(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm);

	/**
	 * The bearings to try round a preferred one, nearest first: Preferred, +Step, -Step, +2 Step, -2 Step, ... up to 180 deg off (each
	 * normalized to (-180, 180]); a non-positive Step: just Preferred. For laying out a sight test where a line is clear (the tutorial's
	 * "standing you see over the low wall" scene, 2026-10-26: laid out straight along the leader's facing it could fall behind a 2.4 m
	 * container 5 m away - the leader's facing is whatever his last formation look left).
	 */
	CODEXTACTICS_API TArray<float> BearingsNearestFirst(float PreferredYaw, float StepDeg);

	/** The line passes over a cover of CoverTopCm at that point (flat ground model; the game traces the real geometry). */
	CODEXTACTICS_API bool ClearsCover(float EyeHeightCm, float TargetHeightCm, float CoverDistanceCm, float TargetDistanceCm,
		float CoverTopCm = CoverHeightCm);

	/** How far an enemy hears an operative in Stance, cm. */
	CODEXTACTICS_API float EnemyHearingRadius(EOperativeStance Stance);

	/** An enemy within SquadTouchRevealCm (2D) of an operative is seen whatever is in between. */
	CODEXTACTICS_API bool IsWithinTouchReveal(float DistanceCm);

	/** Blind fire hit chance: x0.2, and 0 when the enemy left its ghost's spot. */
	CODEXTACTICS_API float BlindFireHitChance(float BaseHitChance, float EnemyDistanceFromGhostCm);

	/** An enemy keeps pursuing an unperceived operative's last known spot until it searched there or the memory is too old. */
	CODEXTACTICS_API bool ShouldForget(float SecondsSinceSeen, float SecondsSearchingThere);
}
