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

	/** The radius Listener hears a lure at (walls and search multiplier applied), cm. */
	CODEXTACTICS_API float EffectiveRadius(const FStealthLureListener& Listener, float RadiusCm, float SearchPerceptionMultiplier);

	/**
	 * Walk speed to the noise: NormalSpeed x Fraction, never below the patrol pace (or the normal speed when that is lower);
	 * Fraction <= 0 = TrapSearchSpeed (the patrol search pace).
	 */
	CODEXTACTICS_API float GetApproachSpeed(float PatrolWalkSpeed, float NormalSpeed, float Fraction, float TrapSearchSpeed);

	/** Short label of Kind for the action bar ("WHISTLE" / "THROW"). */
	CODEXTACTICS_API FText GetButtonName(EStealthLureKind Kind);

	/** Action-bar label: "WHISTLE\n[5]" when available, "WHISTLE\n4s" on cooldown, "WHISTLE\n--" otherwise. */
	CODEXTACTICS_API FText GetButtonLabel(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds, const FString& KeyLabel);

	/** Why it is refused, for the tooltip / the feed ("Only while exploring undetected", "Ready in 4 s"...); empty for None. */
	CODEXTACTICS_API FText GetRefusalText(EStealthLureKind Kind, EStealthLureRefusal Refusal, float CooldownLeftSeconds);

	/** The feed line of a lure that happened ("Whistle." / "Stone thrown."). */
	CODEXTACTICS_API FText GetFeedLine(EStealthLureKind Kind);
}
