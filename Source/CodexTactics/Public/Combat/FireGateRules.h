#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatGroundRules.h"

/**
 * The fire gate (bugs 058c / 061, game designer 2026-11-15: "he faces the wall and his rounds fly at the enemy"; "they fire first and turn
 * after - it must be turn, raise, then the shot", EVERYWHERE). UE-only, no Godot reference. One rule for every operative fire path (the
 * automatic fire in the open / from cover / at a corner, quick fire, suppress, the targeted shot at an object; the squad and the PMC
 * fighters share AOperativeCharacter): a round leaves only when
 *   1. the barrel is on the aim point: the measured aim error (the real barrel when the pose is evaluated, else the stance model) within
 *      the aim cone, and, still in the open, the BODY within the body arc (Codex.Fire.BodyArcDeg) - the twist alone never carries a round;
 *      from cover the pose's own aim (lean / pop-up) counts, never the wall slot's facing; blind fire from cover is not aimed (only the
 *      body's back to the wall counts);
 *   2. the weapon is raised: the aim pose (open) / the cover fire stance (cover) has been up raise_seconds / cover_raise_seconds, no stance
 *      clip plays, and a wounded man's torso / hunch is up (the injured raise, InjuredRaise, also crouched and in cover);
 *   3. after a turn the barrel has stayed on the point settle_seconds.
 * A pose nobody animates (the anim instance not ticking for anim_stale_seconds) never holds a round. Tunables:
 * Content/Data/Combat/fire_gate.json "params" (the FFireGateConfig defaults mirror it: CodexTactics.Combat.FireGate.TuningFileMirrorsDefaults).
 * Pure rules, unit tests CodexTactics.Combat.FireGate.*; runtime AOperativeCharacter::CanFireAt (OperativeCharacterFireGate.cpp).
 */

/**
 * A shot over the shooter's own roof rim (G008b). The crouch muzzle sits under a low parapet; a line from just past that
 * rim is the shot the sight already allowed. Numbers: Content/Data/Combat/fire_gate.json "roof_lip".
 */
struct CODEXTACTICS_API FRoofLipConfig
{
	/** His own rim, no farther than this (cm). Past it the hit is another building. "lip_cm" */
	float LipCm = 6000.f;
	/** Step past the rim's near face (cm). 80 clears a 20 cm parapet and stays inside a 4 m wall. "past_cm" */
	float PastCm = 80.f;
	/** Feet this far above the target's feet (cm) or the retry never runs (a man on the ground keeps the wall). "min_drop_cm" */
	float MinDropCm = 180.f;
	/** The rim's top is at most this far above his feet (cm): a low parapet, not a tall wall. "band_above_cm" */
	float BandAboveCm = 120.f;
	/** ... and not this far below his feet (cm): the roof surface, not a floor underneath. "band_below_cm" */
	float BandBelowCm = 40.f;
};

struct CODEXTACTICS_API FFireGateConfig
{
	/** 1 = the gate is on (0 = the 2026-11-15 per-path checks only; A/B with the CVar Codex.Fire.Gate too). "enabled" */
	float Enabled = 1.f;
	/** The open aim pose (the anim instance's bIsAiming) up this long before the first round, s. "raise_seconds" */
	float RaiseSeconds = 0.2f;
	/** The cover fire stance (lean out / pop up / blind fire) up this long before the first round, s. "cover_raise_seconds" */
	float CoverRaiseSeconds = 0.25f;
	/** After a turn the barrel stays inside the aim cone this long before the round, s. "settle_seconds" */
	float SettleSeconds = 0.1f;
	/** No fire attempt for this long: the settle starts again on the next one (he aimed elsewhere / nowhere meanwhile), s. "eval_gap_seconds" */
	float EvalGapSeconds = 0.3f;
	/** The anim instance did not update for this long: the pose is not shown - no raise / stance hold, s. "anim_stale_seconds" */
	float AnimStaleSeconds = 0.25f;
	/**
	 * From cover: the fire stance has been up this long and its barrel still misses the point by more than the cone - the cover pose cannot
	 * reach it; he steps off the wall for an open shot (when he may expose himself), s. "cover_reach_seconds"
	 */
	float CoverReachSeconds = 0.6f;
	/** A targeted shot at an object (barrel / mine / crate) waits for the gate at most this long, then the order is dropped, s. "object_shot_timeout_seconds" */
	float ObjectShotTimeoutSeconds = 4.f;
	/**
	 * Stealthed at a wall, a shot hands the body from the stealth wall pose to the cover fire stance while the gate was asked within
	 * max(eval_gap_seconds, this) - the floor keeps the hand-off from lapsing between two attempts, s. "stealth_shot_hold_seconds"
	 */
	float StealthShotHoldSeconds = 0.5f;
	/**
	 * The CROUCHED corner fire stance at a high wall (cvr_crch_fire_idle_*: the stealth stance at a wall) aims this far past the along-wall
	 * line, deg - the base its aim offset twists from. Measured 2026-11-17 in StealthModeSmoke on the real barrel: ~64 (the standing
	 * stance's CornerAimOutwardDeg 100 left the crouched barrel ~37 deg off every target). "crouch_corner_aim_outward_deg"
	 */
	float CrouchCornerAimOutwardDeg = 64.f;

	/** fire_gate.json "dead_zone" (G007): the platform dead zone, read by SquadFireRules / CombatGround. */
	FDeadZoneConfig DeadZone;
	/** fire_gate.json "ground_probe" (G007): the floor probe between two pairs of feet (dead zone edge, melee shared ground). */
	FGroundProbeConfig GroundProbe;
	/** fire_gate.json "melee_reach" (G007): the enemy's blow on uneven ground (EnemyAIRules::CanMeleeOnGround, every caller). */
	FMeleeReachConfig MeleeReach;

	/**
	 * fire_gate.json "roof_lip" (G008b). A kneeling muzzle (85 cm) on a roof hits the parapet (~100 cm) or the roof slab;
	 * the sight line already starts past that rim, so the team sees the man and the shot does not. SquadFireRules::MayClearRoofLip.
	 */
	FRoofLipConfig RoofLip;
};

/** Why the gate holds a round (EFireGateBlock::None = the round may leave). */
enum class EFireGateBlock : uint8
{
	None,
	/** The barrel / the body is not on the aim point yet (he is turning, or the cover pose aims elsewhere). */
	NotOnTarget,
	/** From cover: the fire stance is up long enough and still misses the point - the pose cannot reach it (step out / hold). */
	CoverPoseOutOfReach,
	/** Wounded: the torso / the hunch is still rising into the weapon pose (InjuredRaise). */
	WoundedRaise,
	/** A stance clip (stand <-> crouch <-> prone) is playing. */
	StanceClip,
	/** The aim pose / the cover fire stance is not up long enough yet. */
	Raising,
	/** On target, but not for SettleSeconds yet (right after a turn). */
	Settling,
	Count
};

/** One fire attempt's facts (gathered by AOperativeCharacter::CanFireAt). */
struct CODEXTACTICS_API FFireGateInput
{
	/** The aim error to the point, deg (the barrel's / the stance's; 0 for an unaimed blind shot from cover). */
	float ResidualDeg = 0.f;
	/** The aim cone the residual must be within, deg (AOperativeCharacter::AimConeDeg). */
	float ConeDeg = 30.f;
	/** The body faces the point well enough: open = the body within the body arc, cover = his back to the wall within the facing tolerance. */
	bool bBodyReady = true;
	/** He shoots from a cover pose (the cover stance raise and the reach rule apply). */
	bool bInCover = false;
	/** The barrel has been within the cone this long, s (-1 = not on target / just turned onto it this frame is 0). */
	float SecondsOnTarget = -1.f;
	/** The aim pose (open) / the cover fire stance (cover) has been up this long, s (-1 = down). */
	float SecondsRaised = -1.f;
	/** The anim instance updates (the pose is shown / evaluated); false = no raise / stance / wound hold. */
	bool bAnimLive = true;
	/** A stance transition clip plays. */
	bool bStanceClip = false;
	/** Wounded: the torso / the hunch is still rising (UOperativeAnimInstance::IsInjuredRaisePending). */
	bool bWoundedRaisePending = false;
};

namespace FireGateRules
{
	/** The values in force (Content/Data/Combat/fire_gate.json, loaded on the first call; the struct defaults mirror the file). */
	CODEXTACTICS_API const FFireGateConfig& GetConfig();
	/** Replaces the values in force (tests / smokes). */
	CODEXTACTICS_API void SetConfig(const FFireGateConfig& Config);
	/** Re-reads fire_gate.json (game start); false = the file is missing / unreadable (the values in force are kept). */
	CODEXTACTICS_API bool ReloadConfig();
	CODEXTACTICS_API FString GetDefaultPath();
	/** Every tunable key: "params" keys as they are, the other sections as "section.key" (e.g. "dead_zone.edge_drop_cm"). */
	CODEXTACTICS_API TArray<FString> KnownKeys();
	CODEXTACTICS_API bool GetValue(const FFireGateConfig& Config, const FString& Key, float& OutValue);
	/**
	 * Applies a fire_gate.json text over InOut: the "params" object and, when present, "dead_zone" / "ground_probe" / "melee_reach" /
	 * "roof_lip"; unknown / non-numeric keys go to OutUnknown ("section.key" outside "params"; a section's "comment" is text, not a
	 * key). False without "params".
	 */
	CODEXTACTICS_API bool ApplyJson(const FString& JsonText, FFireGateConfig& InOut, TArray<FString>* OutUnknown = nullptr);

	/** The gate is on (Enabled > 0.5). */
	CODEXTACTICS_API bool IsEnabled(const FFireGateConfig& Config);

	/**
	 * The verdict of one fire attempt. Order: not on target (the residual past the cone or the body not ready; from cover past
	 * CoverReachSeconds of a raised stance = out of the pose's reach) -> wounded raise -> stance clip -> raise -> settle. A dead anim
	 * (bAnimLive false) holds nothing but the aim and the settle.
	 */
	CODEXTACTICS_API EFireGateBlock Evaluate(const FFireGateInput& Input, const FFireGateConfig& Config);

	/** Open and still: the body faces the point within ArcDeg (>= 180 = any body, walking fire keeps the twist). */
	CODEXTACTICS_API bool IsBodyInArc(float BodyErrorDeg, float ArcDeg);

	/** A "since" stamp: Now when the condition turns true (kept while it holds), -1 while false. */
	CODEXTACTICS_API double StepSince(double Since, bool bCondition, double Now);

	/** Seconds since a StepSince stamp (-1 when unset). */
	CODEXTACTICS_API float SecondsSince(double Since, double Now);

	/** A log / trace name of a verdict ("fired" style reasons, static strings). */
	CODEXTACTICS_API const TCHAR* BlockName(EFireGateBlock Block);
}
