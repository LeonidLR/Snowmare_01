#pragma once

#include "CoreMinimal.h"
#include "Combat/KnockdownTypes.h"

/**
 * Pure Knockdown & Recovery rules (Sprint 14, TANDEM request #12; UE-only, no Godot reference — Gemini spec
 * "Knockdown & Recovery System", user-approved 2026-10-08). Tested in CodexTactics.Combat.Knockdown.*.
 *
 * Clips (/Game/Animations_KnockDown, Scripts/Editor/import_knockdown_animations.py, measured 2026-10-08):
 * Knocked_Back 0.867 s (pelvis on the ground at 0.56 s), Knocked_Front 0.933 s (0.75 s), Revive_Back 1.667 s
 * (standing at 1.44 s), Revive_Front 2.5 s (standing at 2.04 s), Death_Back 1.12 s, Death_Front 0.933 s (both lying
 * deaths). Revive_Left (2.8 s) has no matching fall clip and is not used.
 */
struct CODEXTACTICS_API FKnockdownConfig
{
	/** A single hit doing at least this much damage can knock the target down (spec: >= 40 HP; then KnockdownChance). */
	float DamageThreshold = 40.f;
	/**
	 * A qualifying heavy hit (>= DamageThreshold) knocks down only with this probability (user decision 2026-10-11: 50 %
	 * for everyone). Explosions within ExplosionRadius, pounces (hound run-up pounce, cutter leap) and the Frost Brute's
	 * heavy blow (dedicated knockdown attacks) always knock down.
	 */
	float KnockdownChance = 0.5f;
	/** An explosion closer than this knocks down (spec: < 2.5 m), cm. */
	float ExplosionRadius = 250.f;
	/** Fall phase when no clip is known, s (spec ~0.8; the clips are 0.87 / 0.93 s and play at their own length). */
	float FallSeconds = 0.8f;
	/** Lying on the ground while the recovery bar fills, s (user decision 2026-10-11: 0.6, was 1.5). */
	float DownedSeconds = 0.6f;
	/** Wanted get-up time, s (user decision 2026-10-11: 0.8, was 1.0): the Revive clip is sped up towards it, limited by GetUpMaxPlayRate. */
	float GetUpSeconds = 0.8f;
	/** Fastest the get-up clip may play (1.667 s Revive_Back -> 0.8 s needs 2.08x; Revive_Front 2.5 s is capped at 1.19 s). */
	float GetUpMaxPlayRate = 2.1f;
	/** Turn-based: getting up is an explicit action on his turn and costs this many AP; he may also stay down (spec 2). */
	int32 GetUpActionPoints = 2;
	/**
	 * Any incoming damage (ranged, melee, explosion) while he lies after a knockdown, real time and turn-based (user
	 * decision 2026-10-11: x1.33; replaces the old ranged x0.6 / melee x1.5 split).
	 */
	float DownedDamageMultiplier = 1.33f;
	/** After getting up he cannot be knocked down again for this long (no stun-lock by a pack), s. */
	float ReknockImmunitySeconds = 1.5f;
	/** Melee enemies go for a downed operative within this distance, cm. */
	float DownedTargetPreferenceCm = 600.f;
	/** ... when he is at most this much farther than the target they picked, cm. */
	float DownedTargetExtraCm = 300.f;
	/** A hound's bite counts as a pounce (knockdown) after running at full speed this long, s. */
	float HoundPounceRunUpSeconds = 1.f;
	/** A hound knocks somebody down at most this often, s. */
	float HoundPounceCooldownSeconds = 12.f;
};

/** One incoming blow, as the knockdown rules see it. */
struct FKnockdownHit
{
	EKnockdownCause Cause = EKnockdownCause::HeavyHit;
	/** Final damage of this single hit (after armour). */
	float Damage = 0.f;
	/** Critical hit (crit roll / armour-piercing headshot) — the only shot that breaks a Brute's poise. */
	bool bCritical = false;
	/** Explosion only: distance from the blast centre, cm. */
	float ExplosionDistance = 0.f;
	/**
	 * The knockdown chance roll in [0, 1) for HeavyHit (passed in so the rules stay pure; the component rolls
	 * FMath::FRand). Knocked down when ChanceRoll < KnockdownChance. Default 0 = the roll always succeeds.
	 */
	float ChanceRoll = 0.f;
};

/** The unit receiving the blow. */
struct FKnockdownTarget
{
	/** This unit type can fall at all (operatives, Frostbitten, Brute; not hounds / cutters / turrets). */
	bool bCanBeKnockedDown = true;
	/** Heavy poise (Frost Brute): plain shots, pounces and melee never knock him down; explosions and crits do. */
	bool bHeavyPoise = false;
	/** Already in a knockdown (any phase) or dead. */
	bool bAlreadyDown = false;
	/** Seconds since he last got up (re-knock immunity); large when never knocked down. */
	float SecondsSinceGetUp = 1000.f;
};

/** Running state of one knockdown (owned by the knockdown component). */
struct FKnockdownState
{
	EKnockdownPhase Phase = EKnockdownPhase::None;
	EKnockdownDirection Direction = EKnockdownDirection::None;
	EKnockdownCause Cause = EKnockdownCause::None;
	/** Time spent in the current phase, s (frozen while paused). */
	float PhaseElapsed = 0.f;
	float FallDuration = 0.8f;
	float DownedDuration = 1.5f;
	float GetUpDuration = 1.0f;
	/** Turn-based: the downed timer does not run out; he waits for his own turn and 2 AP. */
	bool bWaitForTurn = false;
	/** Turn-based: the downed phase is over, he may get up (an explicit action paid with AP on his turn). */
	bool bGetUpReady = false;
};

/** What happened in one Advance step. */
enum class EKnockdownStep : uint8
{
	None,
	/** Falling -> Downed (the fall clip ended, the recovery bar starts). */
	Landed,
	/** Downed -> GettingUp (play the get-up clip). */
	StartGetUp,
	/** GettingUp -> None (control and collision back, buffered orders run). */
	Recovered
};

/** Turn-based: the player ordered "get up" for the lying unit on his turn. */
struct FKnockdownTurnDecision
{
	/** He gets up now. */
	bool bGetUp = false;
	/** AP spent on it. */
	int32 ActionPointsSpent = 0;
	/** Lying, but fewer AP than GetUpActionPoints: he stays down (the turn goes on; nothing is spent). */
	bool bNotEnoughAP = false;
};

namespace KnockdownRules
{
	/** The config in force (console tunables Codex.Knockdown.* override the defaults; -1 = default). */
	CODEXTACTICS_API const FKnockdownConfig& GetConfig();

	/**
	 * Fall direction from the attack (spec: theta = angle between ActorForward and the direction to the attack source,
	 * planar): |theta| < 90° = hit from the front -> falls on his back; |theta| >= 90° -> on his face. A zero vector
	 * counts as a frontal blow. Side hits (|theta| ~ 90°) take the nearer of the two: there is no side fall clip.
	 */
	CODEXTACTICS_API EKnockdownDirection DirectionFromAttack(const FVector& ActorForward, const FVector& ToAttackSource);

	/** Same from locations (the attacker / blast centre and the victim). */
	CODEXTACTICS_API EKnockdownDirection DirectionFromLocations(const FVector& ActorForward, const FVector& VictimLocation,
		const FVector& SourceLocation);

	/** Planar angle between forward and the direction to the source, degrees 0..180. */
	CODEXTACTICS_API float ImpactAngleDeg(const FVector& ActorForward, const FVector& ToAttackSource);

	/**
	 * Does this blow knock the target down? Never when he cannot fall, is already down, or is within the re-knock
	 * immunity. Pounce / HeavyMelee (Brute blow) always (not on heavy poise); Explosion when closer than
	 * ExplosionRadius (also breaks poise); HeavyHit when Damage >= DamageThreshold (on heavy poise only when also
	 * critical) and then only when Hit.ChanceRoll < KnockdownChance (UsesKnockdownChance).
	 */
	CODEXTACTICS_API bool ShouldKnockDown(const FKnockdownConfig& Config, const FKnockdownHit& Hit, const FKnockdownTarget& Target);

	/** The cause rolls KnockdownChance (HeavyHit only); explosions, pounces and the Brute's heavy blow always knock down. */
	CODEXTACTICS_API bool UsesKnockdownChance(EKnockdownCause Cause);

	/**
	 * The one place for the downed damage modifier: any blow (ranged, melee, explosion) on a unit lying after a
	 * knockdown, x DownedDamageMultiplier; 1 when he is up. Real-time TakeHit / UHealthComponent::TakeDamage and the
	 * turn-based grid hits share it.
	 */
	CODEXTACTICS_API float DownedDamageMultiplier(const FKnockdownConfig& Config, bool bIsDown);

	/** Play rate that brings a clip of ClipSeconds to TargetSeconds, never slower than 1, at most MaxRate. */
	CODEXTACTICS_API float ClipPlayRate(float ClipSeconds, float TargetSeconds, float MaxRate);

	/**
	 * Starts a knockdown: phase Falling, durations from the clips (fall = clip length, <= 0 -> FallSeconds; get-up =
	 * clip length / ClipPlayRate, <= 0 -> GetUpSeconds), downed = DownedSeconds. bTurnBased: the downed phase waits for
	 * his turn instead of the timer.
	 */
	CODEXTACTICS_API FKnockdownState Start(const FKnockdownConfig& Config, EKnockdownDirection Direction, EKnockdownCause Cause,
		float FallClipSeconds, float GetUpClipSeconds, bool bTurnBased);

	/**
	 * A heavier, slower fall (APC ram on a Brute, 2026-10-12): the fall and the get-up clips both play at the absolute
	 * PlayRate (< 1 = slower than the clip; the fall normally plays at 1, the get-up at the compressed Start rate, so
	 * their durations become clip length / PlayRate) and the downed time is scaled by DownedTimeScale. InOutGetUpPlayRate
	 * is the get-up clip's rate from Start (becomes PlayRate). Values <= 0 count as 1.
	 */
	CODEXTACTICS_API void ScaleTimeline(FKnockdownState& State, float& InOutGetUpPlayRate, float PlayRate, float DownedTimeScale);

	/** Timers stop in the tactical pause and while the dialogue / narrative AI pause holds the world. */
	CODEXTACTICS_API bool AreTimersFrozen(bool bTacticalPause, bool bDialoguePause);

	/**
	 * Advances the state by DeltaSeconds (nothing when frozen) and returns the transition it crossed (at most one per
	 * call; the overshoot is kept for the next phase). A turn-based downed unit stops at bGetUpReady until
	 * BeginTurnGetUp.
	 */
	CODEXTACTICS_API EKnockdownStep Advance(FKnockdownState& State, float DeltaSeconds, bool bFrozen);

	/** Switches the downed wait mode when the combat mode changes (turn-based enter / exit). */
	CODEXTACTICS_API void SetTurnBased(FKnockdownState& State, bool bTurnBased);

	/**
	 * Turn-based "get up" order on his turn (never automatic: he may stay down and act / end the turn instead):
	 * lying with AP >= GetUpActionPoints -> get up and pay; fewer AP -> bNotEnoughAP, he stays down. Not lying (up /
	 * still falling / already getting up): nothing.
	 */
	CODEXTACTICS_API FKnockdownTurnDecision DecideTurnGetUp(const FKnockdownConfig& Config, const FKnockdownState& State, int32 ActionPoints);

	/** Turn-based get-up paid: Downed -> GettingUp. Returns false when not downed. */
	CODEXTACTICS_API bool BeginTurnGetUp(FKnockdownState& State);

	/** Recovery bar 0..1: 0 while falling, the downed timer's share while lying (turn-based: 1 once due), 1 getting up. */
	CODEXTACTICS_API float RecoveryFraction(const FKnockdownState& State);

	/** He is down (any phase): no orders, no aim, no fire; orders are buffered. */
	CODEXTACTICS_API bool IsDown(const FKnockdownState& State);

	/**
	 * Enemy targeting: a melee attacker switches to a downed operative within DownedTargetPreferenceCm that is at most
	 * DownedTargetExtraCm farther than its current pick (he takes x DownedDamageMultiplier and cannot strike back);
	 * ranged attackers keep their pick.
	 */
	CODEXTACTICS_API bool PreferDownedTarget(const FKnockdownConfig& Config, bool bMeleeAttacker, float DistanceToDowned, float DistanceToCurrent);

	/** Hound bite = pounce: ran at full speed for HoundPounceRunUpSeconds and its pounce cooldown is over. */
	CODEXTACTICS_API bool IsHoundPounce(const FKnockdownConfig& Config, float RunUpSeconds, float SecondsSinceLastPounce);

	/** Clip names of the direction (for logs / smokes): Knocked_*, Revive_*, Death_*. */
	CODEXTACTICS_API const TCHAR* FallClipName(EKnockdownDirection Direction);
	CODEXTACTICS_API const TCHAR* GetUpClipName(EKnockdownDirection Direction);
	CODEXTACTICS_API const TCHAR* DeathClipName(EKnockdownDirection Direction);
}
