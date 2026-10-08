#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "TargetedShotRules.generated.h"

/** What a Ctrl + click aims at (Godot main.gd Ctrl branch order: enemy, barrel, mine, crate, trapped object). */
UENUM(BlueprintType)
enum class ETargetedShotKind : uint8
{
	None,
	Enemy,
	Barrel,
	Mine,
	Crate,
	TrappedObject
};

/** What happens to a planned targeted shot that could not fire on the pause release (reload, misfire, frozen weapon). */
enum class EPlannedShotRetry : uint8
{
	/** Real-time fight inside the retry window: try again now. */
	Retry,
	/** Tactical pause again: keep it planned, the next release fires it. */
	Wait,
	/** Window over, turn-based combat or the wave ended: drop it. */
	GiveUp
};

/** Hit chance of a remote shot at a mine. */
struct CODEXTACTICS_API FMineShotChance
{
	/** Final chance, % (0..95). */
	float Chance = 0.f;
	/** Stance multiplier of the effective accuracy. */
	float StanceMultiplier = 0.6f;
	/** Chance lost per metre of distance, %. */
	float PenaltyPerMeter = 4.f;
	/** Godot stance label («Стоя», «Присев», «Лёжа»). */
	FText StanceName;
};

/**
 * Pure rules of the Ctrl + click targeted shots.
 * Godot reference: Scenes/movements/player.gd calculate_mine_shot_hit_chance, shoot_at_mine_object.
 */
namespace TargetedShotRules
{
	/** Cold penalty per cold % on the accuracy (Godot cold_level * 0.25). */
	constexpr float ColdAccuracyPenalty = 0.25f;
	/** Effective accuracy never drops below this, %. */
	constexpr float MinEffectiveAccuracy = 10.f;
	constexpr float MaxMineShotChance = 95.f;
	/** Standing shots beyond this distance get the «стоя на таком расстоянии не попасть» miss reason, m. */
	constexpr float StandingMissReasonDistance = 7.f;
	/** Cold above this gives the «руки дрожат от холода» miss reason, %. */
	constexpr float ColdMissReasonLevel = 40.f;

	/** clamp(max(10, Accuracy - Cold * 0.25) * StanceMult - DistanceM * PerMeter, 0, 95). */
	CODEXTACTICS_API FMineShotChance ComputeMineShotChance(float Accuracy, float ColdLevel, EOperativeStance Stance, float DistanceM);

	/** Why a mine shot missed (Godot fail_reason). */
	CODEXTACTICS_API FText GetMineMissReason(EOperativeStance Stance, float DistanceM, float ColdLevel);

	/** A deferred planned object shot is retried this long after the pause release, s (covers a reload + a misfire). */
	constexpr float PlannedShotRetrySeconds = 8.f;

	/**
	 * Bug 2026-10-08: a barrel shot ordered in the tactical pause was lost when the operative could not fire at the release
	 * (reloading after the auto-fire, misfire, frozen weapon). The squad keeps it and retries it in the real-time fight.
	 */
	CODEXTACTICS_API EPlannedShotRetry GetPlannedShotRetry(ECodexGamePhase Phase, ECodexCombatMode Mode, float SecondsSinceRelease);
}
