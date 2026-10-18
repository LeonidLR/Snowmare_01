#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "StealthLureRules.generated.h"

/**
 * Stealth lure (user decisions 2026-10-18, UE-only, no Godot reference; design docs/design/STEALTH_LURE.md): any operative can
 * WHISTLE (a noise at his own position) or THROW a stone (aimed like a grenade, a noise at the impact point). Infinite, a
 * per-operative cooldown per kind; only while exploring and undetected. Every patrol / guard enemy that hears the noise
 * investigates the point through the existing patrol SEARCH (AEnemyCharacter::StartPatrolSearchWith); walls muffle the
 * noise like footsteps (PerceptionRules::OccludedRadius with the listener's HearingOcclusionPerWall). The noise is a
 * sound at a point, never a sighting: it does not reveal the operative by itself.
 * Pure rules, tested in CodexTactics.AI.StealthLure.*; numbers in Content/Data/AI/enemy_perception.json "lure".
 */

/** The two lure noises. */
UENUM(BlueprintType)
enum class EStealthLureKind : uint8
{
	/** A whistle at the operative's own position. */
	Whistle,
	/** A stone / bolt / spent cartridge thrown to a point; the noise is at its impact. */
	Throw
};

/** Why a lure is refused right now (None = available). Checked in this order. */
UENUM(BlueprintType)
enum class EStealthLureRefusal : uint8
{
	None,
	/** No operative, dead, knocked down or aboard a vehicle. */
	Unable,
	/** A fight is on (any phase but Exploration, or a combat mode: real time, tactical pause, turn-based). */
	Combat,
	/** An enemy hunts the squad (broke its patrol / knows where the squad is). */
	Detected,
	/** This operative's cooldown for this kind is still running. */
	Cooldown
};

/** Lure tuning (designer data, enemy_perception.json "lure"; distances in cm, times in seconds). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FStealthLureParams
{
	GENERATED_BODY()

	/** Open-air hearing radius of a whistle, around the whistler (walls cut it like footsteps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float WhistleRadiusCm = 1200.f;

	/** Open-air hearing radius of a thrown stone, around the impact point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float ThrowRadiusCm = 800.f;

	/** Farthest throw standing (crouched 75 %, prone 50 % - the grenade stance rule). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float ThrowRangeCm = 1500.f;

	/** Per-operative cooldown after a whistle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float WhistleCooldownSeconds = 10.f;

	/** Per-operative cooldown after a throw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float ThrowCooldownSeconds = 6.f;

	/** How long an enemy investigates the noise before it returns to its route / post. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float InvestigateSeconds = 60.f;

	/** Sweep points of the investigation lie this close to the noise (the trap search sweeps 12 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float InvestigateSweepRadiusCm = 400.f;

	/**
	 * Walk to the noise at this fraction of the enemy's normal (hunting) speed - never slower than its patrol pace, so a hound
	 * comes fast and a Brute slowly; <= 0 keeps the trap-search pace.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float ApproachSpeedFraction = 0.75f;

	/**
	 * Fix 2026-10-18 (user report: the enemies only turned on the spot): an enemy that heard the noise first stands and
	 * listens this long, turned towards it, then walks there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stealth|Lure", meta = (ClampMin = "0"))
	float ListenSeconds = 0.75f;
};

/** How far an enemy's navmesh path to a lure noise gets (StealthLureRules::ChooseInvestigatePoint). */
enum class ELureGoalPath : uint8
{
	/** No path was found (or the noise is not on the navmesh). */
	None,
	/** The path ends short of the noise: the closest reachable point (another side of a wall without a way round). */
	Partial,
	/** The noise point itself is reachable. */
	Full
};

/** What the availability check needs to know (gathered by UStealthLureSubsystem). */
struct CODEXTACTICS_API FStealthLureGate
{
	/** Alive, not knocked down, not aboard a vehicle. */
	bool bOperativeAble = true;
	ECodexGamePhase Phase = ECodexGamePhase::Exploration;
	ECodexCombatMode CombatMode = ECodexCombatMode::None;
	/** Any enemy hunts the squad (StealthLureRules::IsHuntingSquad). */
	bool bSquadDetected = false;
	/** Seconds left on this operative's cooldown for the kind asked. */
	float CooldownLeftSeconds = 0.f;
};

/** One enemy as the lure rules see it. */
struct CODEXTACTICS_API FStealthLureListener
{
	bool bAlive = true;
	/** On patrol / guard / escort duty (searching included): only those react to a lure. */
	bool bOnPatrol = true;
	/** Frozen by a turn-based fight (never reacts). */
	bool bTurnBasedHeld = false;
	/** Already investigating (its perception is heightened: x the search perception multiplier). */
	bool bSearching = false;
	/** Distance from its ear to the noise, cm. */
	float DistanceCm = 0.f;
	/** World-blocking surfaces between its ear and the noise (counted up to PerceptionRules::MaxHearingOccluders). */
	int32 Walls = 0;
	/** Its archetype's HearingOcclusionPerWall (footsteps), 0..1. */
	float OcclusionPerWall = 0.5f;
	/**
	 * Its navmesh walking path to the noise, cm (round walls, through doorways); < 0 = not known / no full path. The noise reaches
	 * it round a wall in open air when this path fits the open-air radius (fix 2026-10-18, see StealthLureRules::Hears).
	 */
	float PathCm = -1.f;
};

/**
 * What an enemy on patrol duty does with a new noise that sends it searching (a lure, a trap blast, a campfire, a lost leader)
 * while it may already be searching / investigating (re-target rule, user report 2026-10-18: a stone first, then the whistle -
 * the whistle was lost and they kept turning at the stone).
 */
enum class ENoiseReaction : uint8
{
	/** Off patrol duty / dying (the world call only; the rule never returns it): nothing sends it searching. */
	Ignored,
	/** Was not searching: it starts the search / investigation at the noise. */
	Investigate,
	/** Was searching / investigating another point: the NEWER noise wins - the origin moves there, it listens again, walks there,
	 *  and the search clock restarts. */
	Retarget,
	/** It is already on this very noise (its patrol mate pulled it in this instant, then it heard it itself): nothing changes. */
	SameNoise,
	/** A hound following a fresh scent within ScentHoldsNoiseCm: the nose wins (it is about to find him), the noise is ignored. */
	KeepScent
};

/** The enemy's state as the re-target rule sees it. */
struct CODEXTACTICS_API FNoiseReactionInput
{
	/** Searching / investigating already (a trap search, a lure, a lost scent). */
	bool bSearching = false;
	/** Planar distance from the point it searches now (its noise) to the new noise, cm. */
	float DistanceToCurrentNoiseCm = TNumericLimits<float>::Max();
	/** Seconds since its current noise was set (world time). */
	float SecondsSinceCurrentNoise = TNumericLimits<float>::Max();
	/** A hound standing and sniffing a scent it just caught (ScentRules Sniffing). */
	bool bSniffing = false;
	/** A hound following a scent (ScentRules Following). */
	bool bFollowingScent = false;
	/** Planar distance from the hound to the point it smells the operative at, cm (Following). */
	float ScentDistanceCm = TNumericLimits<float>::Max();
};

namespace StealthLureRules
{
	/** Open-air hearing radius of Kind, cm. */
	CODEXTACTICS_API float GetRadius(const FStealthLureParams& Params, EStealthLureKind Kind);

	/** Cooldown after Kind, s. */
	CODEXTACTICS_API float GetCooldown(const FStealthLureParams& Params, EStealthLureKind Kind);

	/** Farthest throw of an operative in Stance (GrenadeRules::EffectiveRange: 100 / 75 / 50 %). */
	CODEXTACTICS_API float GetThrowRange(const FStealthLureParams& Params, EOperativeStance Stance);

	/** Seconds left of a cooldown that ends at ReadyAtSeconds (world time), never below 0. */
	CODEXTACTICS_API float CooldownLeft(double ReadyAtSeconds, double NowSeconds);

	/** The availability check (order: Unable, Combat, Detected, Cooldown). */
	CODEXTACTICS_API EStealthLureRefusal CheckAvailability(const FStealthLureGate& Gate);

	/** Exploration without a combat mode (no wave, no ambush fight, no tactical pause / turn-based). */
	CODEXTACTICS_API bool IsExploring(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	/**
	 * An enemy counts as "the squad is detected" when it is alive, off patrol duty (it broke its patrol / is a hunter) and knows
	 * of the squad: it has a target, an alarm point or the horde's knowledge of the squad.
	 */
	CODEXTACTICS_API bool IsHuntingSquad(bool bAlive, bool bOnPatrol, bool bHasTarget, bool bHasAlarmPoint, bool bKnowsSquadPosition);

	/**
	 * Listener hears a lure of open-air RadiusCm: a living enemy on patrol duty (not held by a turn-based fight), within the
	 * radius muffled by its walls (PerceptionRules::OccludedRadius, the footstep model), the radius x SearchPerceptionMultiplier
	 * while it already searches (heightened perception, like every patrol sense). EVERY such enemy reacts (no "only one").
	 */
	CODEXTACTICS_API bool Hears(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier);

	/**
	 * The noise also travels ROUND walls (fix 2026-10-18, user's L_PatrolTest: a whistle from behind a wall with a doorway was lost
	 * on enemies 7.5 / 10.8 m away - the straight line crossed the wall, the radius halved to 7.5 m - while their walk through the
	 * doorway was 10.9 / 14.1 m): Listener.PathCm >= 0 within the open-air radius (x the search multiplier) = heard as in the open.
	 * Hears() includes it; this is the path part alone.
	 */
	CODEXTACTICS_API bool HearsAlongPath(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier);

	/** Open-air radius of a lure for Listener (x the search multiplier while it searches; no walls), cm. */
	CODEXTACTICS_API float OpenRadius(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier);

	/**
	 * The re-target rule (fix 2026-10-18; every noise that starts a patrol search, not only lures). In order:
	 * a hound following a scent closer than ScentHoldsNoiseCm keeps following (KeepScent); a noise within SameNoiseCm of its current
	 * one that was set less than SameNoiseSeconds ago is the same noise (SameNoise); searching = Retarget (the newer noise wins over
	 * the point it investigates, whatever set it - lure, trap, scent); else Investigate. A sniffing hound / one following a far
	 * scent re-targets (the caller drops the scent). Stronger stimuli keep priority outside this rule: an enemy that sees / hears
	 * the squad breaks its patrol, and off patrol duty nothing sends it searching.
	 */
	CODEXTACTICS_API ENoiseReaction ReactToNoise(const FNoiseReactionInput& Input);

	/** True when the reaction moves the enemy to the new noise (Investigate / Retarget). */
	CODEXTACTICS_API bool GoesToNoise(ENoiseReaction Reaction);

	/** A hound following a scent this close to it ignores a new noise (it is about to detect / lose him), cm. */
	constexpr float ScentHoldsNoiseCm = 300.f;
	/** Same noise: this close to the current one ... */
	constexpr float SameNoiseCm = 50.f;
	/** ... and set this recently (a patrol mate's propagation in the same instant), s. */
	constexpr float SameNoiseSeconds = 0.1f;

	/** The radius Listener hears a lure at (walls and search multiplier applied), cm. */
	CODEXTACTICS_API float EffectiveRadius(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier);

	/**
	 * Walk speed to the noise: NormalSpeed x Fraction, never below the patrol pace (or the normal speed when that is lower);
	 * Fraction <= 0 = TrapSearchSpeed (the patrol search pace).
	 */
	CODEXTACTICS_API float GetApproachSpeed(float PatrolWalkSpeed, float NormalSpeed, float Fraction, float TrapSearchSpeed);

	/**
	 * Where an enemy that heard a lure walks to (fix 2026-10-18: a whistle's noise is the whistler's feet, which may lie off the
	 * enemy's navmesh or behind a wall without a way round, and the old move to the raw point failed and the enemy only turned
	 * on the spot until its leg timed out): the noise projected onto the navmesh when a full path leads there, else the end of
	 * the partial path (as close as it can get), else the projected point (the move tries, failures re-path), else the raw noise.
	 */
	CODEXTACTICS_API FVector ChooseInvestigatePoint(const FVector& Noise, bool bOnNavmesh, const FVector& OnNavmesh, ELureGoalPath Path, const FVector& PathEnd);

	/** Arrival radius of the walk to a lure noise: at least LureArriveCm (the whistler's body may stand on the point). */
	CODEXTACTICS_API float GetArriveRadius(float SearchArriveCm, bool bLureFirstLeg);

	/**
	 * A search / lure leg is over: within ArriveCm, or stuck (standing, StuckSeconds >= ArriveStuckSeconds) within ArriveCm +
	 * ArriveStuckSlackCm - two enemies walking to the same point block each other there (StealthLureSmoke F: one waited 10 s at
	 * 2.08 m before its leg timed out).
	 */
	CODEXTACTICS_API bool HasArrived(float DistanceCm, float ArriveCm, float StuckSeconds);

	constexpr float LureArriveCm = 200.f;
	constexpr float ArriveStuckSlackCm = 150.f;
	constexpr float ArriveStuckSeconds = 0.5f;
	/** Refused move requests to the noise before the goal is resolved again; before the enemy searches where it stands. */
	constexpr int32 RepathAfterFailures = 2;
	constexpr int32 GiveUpAfterFailures = 5;

	/** Short label of Kind for the action bar ("WHISTLE" / "THROW"). */
	CODEXTACTICS_API FText GetButtonName(EStealthLureKind Kind);

	/** Action-bar label: "WHISTLE\n[5]" when available, "WHISTLE\n4s" on cooldown, "WHISTLE\n--" otherwise. */
	CODEXTACTICS_API FText GetButtonLabel(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds, const FString& KeyLabel);

	/** Why it is refused, for the tooltip / the feed ("Only while exploring undetected", "Ready in 4 s"...); empty for None. */
	CODEXTACTICS_API FText GetRefusalText(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds);

	/** The feed line of a lure that happened ("Whistle." / "Stone thrown."). */
	CODEXTACTICS_API FText GetFeedLine(EStealthLureKind Kind);
}
