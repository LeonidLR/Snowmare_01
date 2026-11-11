#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The squad walks at its most wounded member's pace (the game designer's bug + decision "a" 2026-11-11, UE-only - no Godot reference). A
 * wounded operative who is NOT the leader but walks in the formation fell far behind: the formation's slots move with the leader's patrol walk
 * (his walk x pace.patrol 0.8 = 176 cm/s) while the wounded man may walk only x0.65 (143 cm/s) or, badly wounded, x0.45 (99 cm/s) - and the
 * follow law pushed him past his own speed (his walk x move.catchup_mult 1.25), so his injured clip looked hurried, he lagged beyond
 * move.rear_turn_lag and (as the rear guard) turned round facing his run instead of his look.
 *
 * Now, while a wounded member (light / heavy tier) is in the formation: the leader's pace = min(pace.patrol x his walk, PaceShare x the slowest
 * wounded member's allowed walk) - the formation walks at the same share of the slowest man's walk as it walks at of the leader's when everyone
 * is fit, so the wounded man has the healthy men's margin (x1.25 forward, x1.125 walking backwards) to keep his slot; a wounded follower's
 * formation move never exceeds his allowed walk (no catch-up multiplier above it); the leader's sprint ends (the squad walks). The critically
 * wounded (HP < 20 %, down, crawling) never sets the pace - the squad would crawl - he crawls after his slot at his own crawl speed. A member
 * who is not in the formation (Solo, a fast regroup on B, an order of his own, panicking / knocked down / aboard) does not set it either.
 *
 * Content/Data/AI/squad_wound_pace.json (snake_case keys; a missing key keeps the default), read once on the first SquadWoundPaceTuning::Get();
 * console Codex.Squad.WoundPace (-1 = the file's "enabled", 0 = off - the old pacing, 1 = on). USquadSubsystem::UpdateFormationSectors applies it.
 */
struct CODEXTACTICS_API FSquadWoundPaceTuning
{
	/** The rule is on (enabled, 0 / 1). Off = the old pacing everywhere (the follow law's catch-up for the wounded too). */
	bool bEnabled = true;
	/** The squad walks at this share of the slowest wounded member's allowed walk (pace_share; = pace.patrol: the fit squad's own share). */
	float PaceShare = 0.8f;
	/** The leader's sprint ends while a wounded member sets the pace - the whole squad walks (limit_sprint, 0 / 1). */
	bool bLimitSprint = true;
	/** A wounded follower's formation move is capped at his allowed walk (cap_wounded_follower, 0 / 1). */
	bool bCapWoundedFollower = true;
};

/** One squad member as the pace rule sees him. */
struct FSquadWoundPaceMember
{
	/** His allowed walk now, cm/s (AOperativeCharacter::GetAllowedWalkSpeed: stance, wound tier, cold; never the sprint). */
	float AllowedSpeed = 0.f;
	/** IsWounded (light, heavy or critical tier). */
	bool bWounded = false;
	/** The critical wound (down, crawling only): never sets the pace. */
	bool bCritical = false;
	/** He leads or follows the formation this frame (not Solo / a fast regroup / an order of his own / panic / knockdown / aboard). */
	bool bInFormation = true;
};

/** The pace the formation walks at while a wounded member is in it (SquadPace). */
struct FSquadWoundPace
{
	/** The formation's walking speed cap, cm/s (PaceShare x the slowest wounded member's allowed walk). */
	float CapSpeed = 0.f;
	/** Index (into the members given to SquadPace) of the member who sets it. */
	int32 SlowestIndex = INDEX_NONE;
};

namespace SquadWoundPaceRules
{
	/** The rule in force: console Codex.Squad.WoundPace (0 / 1) over the tuning's bEnabled (-1, the default). */
	CODEXTACTICS_API bool IsEnabled(const FSquadWoundPaceTuning& Tuning);

	/**
	 * The formation's pace: unset = nobody wounded in the formation (or every wounded one critical / out of it, or the rule off) - the old pace.
	 * Else CapSpeed = PaceShare x the lowest AllowedSpeed among the wounded, non-critical members in the formation (the leader included).
	 */
	CODEXTACTICS_API TOptional<FSquadWoundPace> SquadPace(TArrayView<const FSquadWoundPaceMember> Members, const FSquadWoundPaceTuning& Tuning, bool bEnabled);

	/**
	 * The leader's walk multiplier (AOperativeCharacter::SetFormationPaceMultiplier): BasePace (pace.patrol) without a pace cap; with one
	 * min(BasePace, CapSpeed / LeaderWalkSpeed) - a wounded leader already walking slower than the cap keeps his own pace. In (0, BasePace].
	 */
	CODEXTACTICS_API float LeaderPaceMultiplier(float BasePace, float LeaderWalkSpeed, const TOptional<FSquadWoundPace>& Pace);

	/**
	 * The top speed a follower's formation move is asked with (FormationRuntime::FollowerMove multiplies it by move.catchup_mult): his MaxSpeed,
	 * or - wounded, with the rule on and bCapWoundedFollower - MaxSpeed / CatchupMult, so the move never exceeds his allowed walk.
	 */
	CODEXTACTICS_API float FollowerTopSpeed(float MaxSpeed, bool bWounded, float CatchupMult, const FSquadWoundPaceTuning& Tuning, bool bEnabled);

	/** A fixed formation speed (the walk to a slot on another level) for him: at most his MaxSpeed when wounded and the rule is on. */
	CODEXTACTICS_API float CapFixedSpeed(float Speed, float MaxSpeed, bool bWounded, const FSquadWoundPaceTuning& Tuning, bool bEnabled);

	/** The leader's sprint ends: he sprints, a wounded member sets the pace, bLimitSprint. */
	CODEXTACTICS_API bool EndsLeaderSprint(bool bLeaderSprinting, const TOptional<FSquadWoundPace>& Pace, const FSquadWoundPaceTuning& Tuning);
}

namespace SquadWoundPaceTuning
{
	/** Content/Data/AI/squad_wound_pace.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The tuning in force (the file on the first call, else the defaults). */
	CODEXTACTICS_API const FSquadWoundPaceTuning& Get();
	/** Parameters of Json over Base (PaceShare clamped to [0.1, 1]). */
	CODEXTACTICS_API FSquadWoundPaceTuning FromJson(const FJsonObject& Json, const FSquadWoundPaceTuning& Base = FSquadWoundPaceTuning());
	/** Re-reads the file into Get(); false (the defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool Reload();
}
