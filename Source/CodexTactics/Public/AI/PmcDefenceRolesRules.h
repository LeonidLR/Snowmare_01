#pragma once

#include "CoreMinimal.h"
#include "AI/FiringPositionRules.h"

class FJsonObject;

/**
 * DEFENCE ROLES (smart enemies T-08, first slice; designer decisions 2026-11-20 night; docs/design/gdd/smart-enemies.md PL-03 / PL-12, F3 / F4; UE-only,
 * no Godot reference). The ONE task of a defence is destroying the attacking group; holding the point is only a means, and a zone is held only by being
 * PHYSICALLY inside it alive. The scheme on a GROUND objective:
 *   (a) the HOLDER stands inside the objective zone on a COVERED post (cover against the attackers' approach),
 *   (b) the other shooters are HAMMERS on T-05 firing positions out on the approach that destroy the group,
 *   (c) the hammers FALL BACK into the zone, one at a time (bounding), by three rules:
 *        1 zone emptying   - nobody stands in the zone (the holder is down) and attackers are near it,
 *        2 ambush bypassed - an attacker is nearer the objective than the kill zone,
 *        4 not in time     - the attackers reach the zone sooner than the hammer's nav-path time to his zone post + a margin.
 *      Never while >= finish_down_min attackers are down (finish them), and rule 2 only after the defence has opened fire. Rule 3 (position
 *      discovered / suppressed) is not in this slice.
 * This file is the PURE half (no UWorld): the numbers, the three rules, the bounding pick, the zone-post ordering and the route test.
 * UPmcSquadSubsystem gathers the facts (what the squad KNOWS, never more) and runs it. Numbers: the "defence_roles" block of
 * Content/Data/AI/pmc_commander.json (the defaults here equal the shipped file). Tested in CodexTactics.AI.PmcDefenceRoles.*.
 */

/** The fall-back rules, one bit each (a decision may carry several). Rule 3 is not built. */
enum class EPmcFallBackRule : uint8
{
	None = 0,
	/** Rule 1: nobody stands in the zone and attackers are near it. */
	ZoneEmptying = 1,
	/** Rule 2: an attacker is nearer the objective than the kill zone (the ambush was bypassed). */
	Bypassed = 2,
	/** Rule 4: the attackers reach the zone sooner than the hammer can walk to his post (+ margin). */
	NotInTime = 4
};

/** A hammer's fall-back: not started, walking to his zone post, or standing in the zone (for the rest of the round). */
enum class EPmcFallBackPhase : uint8
{
	None,
	Moving,
	InZone
};

/** Numbers of the "defence_roles" block (the defaults = the shipped JSON). Distances cm (the file has metres where the key says _m), times s. */
struct CODEXTACTICS_API FPmcDefenceRolesConfig
{
	/** "enabled" (false): the master switch of the holder + fall-back layer. Needs firing_positions.defence_use and keep_occupier (the layer works on the hammers / the occupier). */
	bool bEnabled = false;
	/** "holder_cover" (true): the occupier takes a covered post inside the zone instead of his G008 post. */
	bool bHolderCover = true;
	/** "fall_back" (true): the hammers fall back into the zone by the rules. */
	bool bFallBack = true;
	/** "zone_near_m" (25): rule 1 - an attacker "near the zone" is within this of its EDGE (objective radius). */
	float ZoneNearCm = 2500.f;
	/** "bypass_margin_m" (5): rule 2 - an attacker is past the kill zone when he is nearer the objective than the zone by MORE than this. */
	float BypassMarginCm = 500.f;
	/** "finish_down_min" (2): at this many attackers down nobody falls back (finish them; "2 of 3"). */
	int32 FinishDownMin = 2;
	/** "known_max_age_s" (3): an attacker last seen longer ago than this is not used by the rules (the squad acts on what it saw). */
	float KnownMaxAgeS = 3.f;
	/** "not_in_time_margin_s" (2): rule 4's safety margin on the hammer's walk time. */
	float NotInTimeMarginS = 2.f;
	/** "bound_max_wait_s" (6): bounding - the next hammer starts when the mover is in the zone, or after this long. */
	float BoundMaxWaitS = 6.f;
	/** "move_timeout_s" (15): a fall-back walk that has not arrived after this counts as done (unblocks the bounding). */
	float MoveTimeoutS = 15.f;
	/** "kill_zone_clear_m" (8): a fall-back route never passes within this of the kill zone (it never crosses it). */
	float KillZoneClearCm = 800.f;
	/** "zone_min_quality" (0.1: the crossroads zone has almost no wall on the approach side - sanity seeds 1-5 at 0.5 and 0.35 gave 0 posts; posts are RANKED by quality, so the best available wins): a zone post is a cover slot whose wall shields it from the approach at least this (CoverQualityAgainst). */
	float ZoneMinQuality = 0.1f;
	/** "zone_no_line_factor" (0.6): a post that cannot fire on the approach (no corner, no clear standing line) scores x this. */
	float ZoneNoLineFactor = 0.6f;
	/** "zone_dist_weight" (0.3): a post at the zone's edge scores (1 - this) of one at its centre. */
	float ZoneDistWeight = 0.3f;
	/** "zone_min_spacing_m" (2.5): two men's zone posts keep this apart when the zone has room (relaxed to "another cover object" when it has not). */
	float ZoneMinSpacingCm = 250.f;
	/** "zone_ray_reach_cm" (400): the cover-slot scan of the zone casts rays of this length from points inside it. */
	float ZoneRayReachCm = 400.f;
	/** "max_zone_posts" (24): zone posts kept per round (best first). */
	int32 MaxZonePosts = 24;
	/** "max_path_queries" (6): nav-path queries per hammer per defence refresh (his fall-back plan). */
	int32 MaxPathQueries = 6;
};

/** What the squad knows when it decides about ONE hammer. */
struct CODEXTACTICS_API FPmcFallBackSituation
{
	/** A standing defender is inside the zone (the holder alive and in it). */
	bool bZoneHeld = true;
	/** The nearest KNOWN living attacker's planar distance to the objective centre, cm; < 0: none known (no rule fires). */
	float NearestAttackerFromObjectiveCm = -1.f;
	float ObjectiveRadiusCm = 600.f;
	/** The kill zone's planar distance to the objective centre, cm (0: no kill zone - rule 2 off). */
	float KillZoneFromObjectiveCm = 0.f;
	/** Attackers seen and now down / dead. */
	int32 AttackersDown = 0;
	/** The defence has fired its first shot of the round. */
	bool bDefenceHasFired = false;
	/** The attackers' pace and the hammer's, cm/s. */
	float AttackerSpeedCm = 220.f;
	float HammerSpeedCm = 725.f;
	/** The hammer's nav-path length from his firing position to his zone post, cm; < 0: unknown (rule 4 off). */
	float HammerPathCm = -1.f;
};

/** The verdict for one hammer. */
struct CODEXTACTICS_API FPmcFallBackDecision
{
	/** Bit mask of EPmcFallBackRule that hold NOW (before the finish gate). */
	uint8 Rules = 0;
	/** The hammer falls back: a rule holds and the finish gate is open. */
	bool bFallBack = false;
	/** A rule holds but >= finish_down_min attackers are down: he holds and finishes them. */
	bool bHeldByFinish = false;
	/** Rule 4's slack, s: the attackers' time to the zone edge - the hammer's walk time - the margin (999 when not computed). */
	float SlackS = 999.f;
};

/** A hammer that may start now (bounding): his walk time to the zone post. */
struct CODEXTACTICS_API FPmcMoverCandidate
{
	int32 Index = INDEX_NONE;
	float PathTimeS = 0.f;
};

namespace PmcDefenceRoles
{
	/** Reads the "defence_roles" object over Base (missing keys keep Base; values are clamped to sane ranges). */
	CODEXTACTICS_API FPmcDefenceRolesConfig ConfigFromJson(const FJsonObject& Block, const FPmcDefenceRolesConfig& Base = FPmcDefenceRolesConfig());

	/** The "defence_roles" block of Content/Data/AI/pmc_commander.json; the defaults when the file or the block is missing. */
	CODEXTACTICS_API FPmcDefenceRolesConfig LoadConfig();

	/** "zone_emptying" / "bypassed" / "not_in_time" for ONE rule bit; "none" for 0. */
	CODEXTACTICS_API const TCHAR* RuleName(EPmcFallBackRule Rule);

	/** The rules of a bit mask as "1+4" (log lines); "-" for none. */
	CODEXTACTICS_API FString RuleList(uint8 Rules);

	/**
	 * The three rules for one hammer. Rule 1: nobody holds the zone and an attacker is within ZoneNearCm of its edge. Rule 2: an attacker is nearer the objective
	 * than the kill zone by more than BypassMarginCm, and the defence has fired (before its first shot only rules 1 and 4 may force a fall-back). Rule 4: the
	 * attackers' time to the zone edge (nearest attacker, AttackerSpeedCm) is less than the hammer's path time + NotInTimeMarginS. No known attacker = no rule.
	 * The finish gate: AttackersDown >= FinishDownMin holds everybody (bHeldByFinish) whatever the rules say.
	 */
	CODEXTACTICS_API FPmcFallBackDecision DecideFallBack(const FPmcFallBackSituation& Situation, const FPmcDefenceRolesConfig& Config);

	/**
	 * Bounding: which hammer moves now. None while a mover is still on his way (bAnyMoving) and SecondsSinceMoverStart < BoundMaxWaitS; else the candidate with
	 * the LONGEST walk (he needs the most time, the nearer one keeps covering), ties to the lower Index. INDEX_NONE for no candidate.
	 */
	CODEXTACTICS_API int32 PickMover(TConstArrayView<FPmcMoverCandidate> Candidates, bool bAnyMoving, float SecondsSinceMoverStart, const FPmcDefenceRolesConfig& Config);

	/** The order score of a zone post: Quality x (no line to the approach: ZoneNoLineFactor) x (1 - ZoneDistWeight x DistFromObjectiveCm / ZoneRadiusCm). Quality <= 0 scores 0. */
	CODEXTACTICS_API float ZonePostScore(float Quality, bool bSeesApproach, float DistFromObjectiveCm, float ZoneRadiusCm, const FPmcDefenceRolesConfig& Config);

	/**
	 * The best zone post for one man: Posts best-first (their Score); not an index in Excluded; not on a claimed cover object (a non-empty key in ClaimedKeys);
	 * keeping ZoneMinSpacingCm from every claimed stand point. When none passes, the spacing is relaxed (a cover object nobody holds, however near), and then
	 * the same stand point is the only thing refused. INDEX_NONE: the zone has no free post.
	 */
	CODEXTACTICS_API int32 PickZonePost(TConstArrayView<FFiringCandidate> Posts, TConstArrayView<int32> Excluded, TConstArrayView<FVector> ClaimedStands,
		TConstArrayView<FString> ClaimedKeys, const FPmcDefenceRolesConfig& Config);

	/**
	 * A route (a nav path, points in order) crosses the kill zone when it comes within ClearCm of it (planar) - ClearCm is capped at 0.7 x the start's own distance
	 * to the zone, so a hammer who already stands near the zone is not refused for the start of his own walk. ClearCm <= 0 or fewer than two points: false.
	 */
	CODEXTACTICS_API bool PathCrossesKillZone(TConstArrayView<FVector> Path, const FVector& KillZone, float ClearCm);
}
