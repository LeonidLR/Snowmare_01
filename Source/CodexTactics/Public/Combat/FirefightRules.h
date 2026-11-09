#pragma once

#include "CoreMinimal.h"

/**
 * Horde #12 (game designer decisions 2026-11-02 / 2026-11-03 / 2026-11-08; UE-only, no Godot reference): a squad that runs into every
 * patrol / ambush instead of slipping past brings the HORDE. Pure rules of the firefight count (tests CodexTactics.Combat.Firefight.*);
 * UFirefightSubsystem feeds them from the world, UHordeSubsystem releases the horde.
 *
 * A FIREFIGHT = one encounter with the level's enemies (patrols, ambushers, PMC fighters - never a scripted wave or the horde itself) that
 * started by contact and in which shots were exchanged: the enemy is aware of the squad (it spotted the squad or attacks it) AND somebody
 * shot / struck (the squad hit or attacked an enemy, or an enemy attacked the squad) - or an ambush was sprung on the squad. One per
 * encounter, not per shot: the encounter starts with its first hostile contact and ends when every enemy that engaged in it is down, or
 * when there was no contact for LostContactSeconds. A silent kill (dead before it noticed anybody) is no firefight.
 * The STREAK counts firefights in a row; at the level's threshold ("horde_after_firefights", default 10 - game designer 2026-11-09) the
 * horde comes and the streak starts again; it is the ONLY horde trigger since 2026-11-09 (the 240 s real-time-fight clock is gone).
 * SLIPPING PAST resets it (confirmed by the game designer 2026-11-09): an enemy group that the squad came within its detection reach of,
 * never spotted the squad (never broke its patrol, no hostile contact), and the squad then left that reach (x SlipExitFactor) - or the
 * group went away (recycled) while the squad was inside it.
 */
struct CODEXTACTICS_API FFirefightConfig
{
	/** "firefight_lost_contact_seconds": an encounter with no contact for this long is over (lost contact). */
	float LostContactSeconds = 20.f;
	/** "firefight_contact_range_m": an engaged enemy hunting the squad (off its patrol) this close to an operative keeps the contact. */
	float ContactRangeCm = 4000.f;
	/** "firefight_slip_past_resets": slipping past an enemy group undetected resets the streak (false: only the horde resets it). */
	bool bSlipPastResets = true;
	/** "firefight_slip_min_reach_m" / "firefight_slip_max_reach_m": a group's detection reach = its members' vision range, clamped. */
	float SlipMinReachCm = 1500.f;
	float SlipMaxReachCm = 4000.f;
	/** "firefight_slip_exit_factor": the squad has left the reach once every operative is farther than reach x this from every member. */
	float SlipExitFactor = 1.25f;
	/** "firefight_warning_seconds": the HQ warning comes this many seconds (running game time) before the horde appears. */
	float WarningLeadSeconds = 10.f;
	/** "firefight_hint_before": the HQ hint "N more firefights and the horde comes" this many firefights before the threshold (0 = none). */
	int32 HintFirefightsBefore = 1;
};

/** What a hostile contact tells about the encounter. */
enum class EFirefightEvent : uint8
{
	/** An enemy noticed the squad (spotted / heard it, broke its patrol). */
	EnemyAware,
	/** An enemy attacked an operative (blow, bite, pounce, spit, shot): aware and a shot. */
	EnemyAttack,
	/** The squad shot / struck an enemy or was ordered to attack one. */
	SquadShot,
	/** A hidden ambusher struck the squad: a firefight at once. */
	AmbushSprung
};

/** Why an encounter ended. */
enum class EFirefightEnd : uint8
{
	None,
	/** Every enemy that engaged in it is dead or gone. */
	AllDown,
	/** No contact for LostContactSeconds. */
	LostContact
};

/** One encounter with the level's enemies (the subsystem keeps who engaged; the rules keep what happened and when). */
struct CODEXTACTICS_API FFirefightEncounter
{
	bool bActive = false;
	/** It qualified as a firefight (counted once in the streak). */
	bool bCounted = false;
	bool bEnemyAware = false;
	bool bShots = false;
	bool bAmbushSprung = false;
	double StartTime = 0.0;
	double LastContactTime = 0.0;
};

/** Firefights in a row on this level. */
struct CODEXTACTICS_API FFirefightStreak
{
	/** Firefights since the last reset (slip-past / horde). */
	int32 Count = 0;
	/** Firefights on this level. */
	int32 Total = 0;
	/** Resets by slipping past (a non-zero streak set back to 0). */
	int32 SlipResets = 0;
	/** Hordes this streak brought. */
	int32 Hordes = 0;

	/** A firefight counted: Count + 1; true when the horde is due (Count >= Threshold, min 1) - the streak then starts again from 0. */
	bool AddFirefight(int32 Threshold);
	/** The squad slipped past a group undetected: a running streak is set back to 0 (true when it was > 0). */
	bool SlipPast();
	/** Firefights left until the horde (>= 0). */
	int32 Remaining(int32 Threshold) const;
};

/** A level enemy group as the slip-past rule follows it. */
struct CODEXTACTICS_API FSlipGroupState
{
	/** An operative is (was last seen) within the group's detection reach. */
	bool bInReach = false;
	/** The group spotted / fought the squad at some point (latched: such a group never counts as slipped past). */
	bool bEngaged = false;
};

enum class ESlipVerdict : uint8
{
	None,
	/** An operative came within the reach this update. */
	EnteredReach,
	/** The squad left the reach (or the group went away) without the group ever spotting it. */
	SlippedPast
};

namespace FirefightRules
{
	/** The level's threshold when its JSON has no "horde_after_firefights" (game designer 2026-11-09: 10 firefights in a row; was 6). */
	constexpr int32 DefaultAfterFirefights = 10;

	/** Applies a contact at Now (starts the encounter if needed); true when this contact made it a firefight (once per encounter). */
	CODEXTACTICS_API bool ApplyEvent(FFirefightEncounter& Encounter, EFirefightEvent Event, double Now);

	/** The encounter is a firefight: an ambush sprung, or the enemy aware and shots exchanged. */
	CODEXTACTICS_API bool Qualifies(const FFirefightEncounter& Encounter);

	/** Refreshes the contact (an engaged enemy still hunts the squad nearby). */
	CODEXTACTICS_API void KeepContact(FFirefightEncounter& Encounter, double Now);

	/**
	 * How an active encounter stands now: AllDown when none of its engaged enemies is alive (EngagedAlive 0), LostContact when the last
	 * contact is LostContactSeconds old, else None (also for an inactive encounter).
	 */
	CODEXTACTICS_API EFirefightEnd CheckEnd(const FFirefightEncounter& Encounter, int32 EngagedAlive, double Now, const FFirefightConfig& Config);

	/** A group's detection reach: its members' largest vision range clamped to [SlipMinReachCm, SlipMaxReachCm]. */
	CODEXTACTICS_API float GroupReach(float LargestVisionRangeCm, const FFirefightConfig& Config);

	/**
	 * One update of a group: DistanceCm = the closest operative-to-member distance now, ReachCm its detection reach, bEngagedNow = a member
	 * spotted / fought the squad (latched into the state), bGroupGone = no member left (killed by the squad = engaged; recycled = gone).
	 */
	CODEXTACTICS_API ESlipVerdict UpdateGroup(FSlipGroupState& State, float DistanceCm, float ReachCm, bool bEngagedNow, bool bGroupGone,
		const FFirefightConfig& Config);

	/** The HQ hint is due right after a firefight that left Remaining firefights to the horde (HintFirefightsBefore, never at 0). */
	CODEXTACTICS_API bool ShouldHint(int32 Remaining, const FFirefightConfig& Config);

	/** The level's threshold: the console override when >= 1, else the level JSON value, at least 1. */
	CODEXTACTICS_API int32 ResolveThreshold(int32 LevelValue, int32 ConsoleValue);
}
