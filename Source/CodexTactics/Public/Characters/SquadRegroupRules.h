#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Fast regroup on B (the game designer's bug 2026-11-10, UE-only - no Godot reference). The game starts in Solo with no distance limit
 * (2026-11-09); B (Regroup / Solo off) used to switch every member into the formation at once, so a man tens of metres away walked to his
 * slot at the formation's catch-up pace (FormationRuntime::FollowerMove: his walk x move.catchup_mult = 220 x 1.25 cm/s) - the squad took
 * very long to join up. Now a member far from the formation first closes the distance as fast as he may (no formation pacing, no slot
 * shuffling) straight to the leader's live position along the nav path, and joins the formation once he is close.
 *
 * Content/Data/AI/squad_regroup.json (snake_case keys, metres where the key says _m; a missing key keeps the default), read once on the first
 * SquadRegroupTuning::Get(). USquadSubsystem arms it in ExitSoloMode and runs it in UpdateFollower (Codex.Squad.FastRegroup 0 = the old rule).
 */
struct CODEXTACTICS_API FSquadRegroupTuning
{
	/** A member farther than this from the formation (his slot, or the leader when that is nearer) regroups fast on B, cm (start_m). */
	float StartCm = 1200.f;
	/** The fast regroup ends and he joins the formation within this of it, cm (join_m). */
	float JoinCm = 600.f;
	/** His run when he may not sprint (in combat), cm/s - the formation's own run pace (run_speed_cm_s). */
	float RunSpeed = 480.f;
	/** Below this share of his max health he is critically wounded (the prone crawl) and does not regroup fast (critical_health_pct, %). */
	float CriticalHealthFraction = 0.2f;
	/** His path to the leader's live position is renewed this often, s (repath_s). */
	float RepathSeconds = 0.5f;
	/** Out of combat (no enemy aware, no fight) a fit man sprints; false = he runs there too (sprint_out_of_combat, 0 / 1). */
	bool bSprintOutOfCombat = true;
	/**
	 * A man the regroup would give a crouch / prone (the leader's copied stance) stands up to run and takes the leader's stance only when he
	 * joins; false = he keeps the leader's stance all the way (stand_up_to_run, 0 / 1).
	 */
	bool bStandUpToRun = true;
};

/** How a regrouping member moves. */
enum class ESquadRegroupPace : uint8
{
	/** Out of combat, fit, able to sprint: the sprint (MovementConfig.RunSpeed). */
	Sprint,
	/** In combat (or no sprint allowed: cold, carrying): the run (FSquadRegroupTuning::RunSpeed). */
	Run,
	/** IsWounded: never a sprint - the run scaled by his wound (MovementConfig.WoundedSpeedMultiplier). */
	WoundedRun,
	/** A HEAVY wound (feat/injured-tiers-fullbody, the game designer 2026-11-10: "no running"): he walks his own (heavy) walk back. */
	WoundedWalk,
};

namespace SquadRegroupRules
{
	/** His gap to the formation: the nearer of his slot and the leader (a slot on the far side of the leader does not count as far), cm. */
	CODEXTACTICS_API float FormationGapCm(float ToSlotCm, float ToLeaderCm);

	/** HP below CriticalHealthFraction of max (MaxHealth <= 0: never). */
	CODEXTACTICS_API bool IsCriticallyWounded(float Health, float MaxHealth, const FSquadRegroupTuning& Tuning);

	/** On B (once, with his slot known): he regroups fast - farther than StartCm from the formation and not critically wounded. */
	CODEXTACTICS_API bool ShouldStartFastRegroup(float GapCm, bool bCriticallyWounded, const FSquadRegroupTuning& Tuning);

	/**
	 * Each frame while he regroups fast: true = he keeps chasing the leader's live position (the leader is never stopped - a leader walking
	 * away only lengthens the chase); false = he joins the formation (within JoinCm), or a critical wound ends it (he crawls in formation).
	 */
	CODEXTACTICS_API bool KeepsFastRegroup(float GapCm, bool bCriticallyWounded, const FSquadRegroupTuning& Tuning);

	/**
	 * Sprint out of combat when he may; run in combat; a wounded man runs at his wounded pace (no sprint); a wounded man who may not run
	 * (bMayRun false: the heavy wound tier) walks.
	 */
	CODEXTACTICS_API ESquadRegroupPace ChoosePace(bool bCombat, bool bWounded, bool bCanSprint, const FSquadRegroupTuning& Tuning, bool bMayRun = true);

	/**
	 * His regroup speed, cm/s: Sprint = SprintSpeed (his sprint, cold included), Run = RunSpeed x ColdMultiplier, WoundedRun = RunSpeed x
	 * WoundedMultiplier x ColdMultiplier, WoundedWalk = SprintSpeed too (the caller passes his current max speed: not sprinting = his walk).
	 */
	CODEXTACTICS_API float PaceSpeed(ESquadRegroupPace Pace, float SprintSpeed, float WoundedMultiplier, float ColdMultiplier, const FSquadRegroupTuning& Tuning);

	/** Log / smoke name of a pace. */
	CODEXTACTICS_API const TCHAR* PaceName(ESquadRegroupPace Pace);
}

namespace SquadRegroupTuning
{
	/** Content/Data/AI/squad_regroup.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The tuning in force (the file on the first call, else the defaults). */
	CODEXTACTICS_API const FSquadRegroupTuning& Get();
	/** Parameters of Json over Base (clamped to sane ranges; JoinCm never above StartCm). */
	CODEXTACTICS_API FSquadRegroupTuning FromJson(const FJsonObject& Json, const FSquadRegroupTuning& Base = FSquadRegroupTuning());
	/** Re-reads the file into Get(); false (the defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool Reload();
}
