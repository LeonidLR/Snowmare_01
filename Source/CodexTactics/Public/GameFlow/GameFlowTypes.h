#pragma once

#include "CoreMinimal.h"
#include "GameFlowTypes.generated.h"

/** Top-level phase of a mission. Godot reference: flags in Scenes/movements/main.gd. */
UENUM(BlueprintType)
enum class ECodexGamePhase : uint8
{
	/** Free real-time exploration, combat zone not yet triggered. */
	Exploration,
	/** Pre-combat cutscene after entering the combat zone. */
	Cutscene,
	/** Real-time preparation before a wave (first one, or rest between waves). */
	Preparation,
	/** A wave is in progress; see ECodexCombatMode for the sub-mode. */
	WaveCombat,
	/** Wave cleared, results shown, world time stopped. */
	WaveCleared,
	/** All waves cleared: squad return and victory dialogue. */
	PostCombat,
	/** Squad lost, world time stopped. */
	GameOver
};

/** Combat sub-mode, meaningful only in ECodexGamePhase::WaveCombat. */
UENUM(BlueprintType)
enum class ECodexCombatMode : uint8
{
	/** Not in a wave. */
	None,
	/** Normal real-time combat. */
	RealTime,
	/** Near-frozen time for issuing orders; orders run together on release. */
	TacticalPause,
	/** Gorky 17 style turn-based combat. */
	TurnBased
};

/** Outcome of a game flow request. Anything but Ok means the state did not change. */
UENUM(BlueprintType)
enum class EGameFlowResult : uint8
{
	Ok,
	/** Request is not valid in the current phase. */
	WrongPhase,
	/** Requires an active wave (WaveCombat). */
	NotInWave,
	/** Combat zone was already triggered for this mission. */
	CombatAlreadyUnlocked,
	/** Requires ECodexCombatMode::RealTime. */
	NotInRealTime,
	/** Requires ECodexCombatMode::TurnBased. */
	NotInTurnBased,
	/** All tactical pause charges for this wave are spent. */
	NoPauseCharges,
	/** Tactical pause charges are recharging. */
	PauseOnCooldown,
	/** Turn-based entries per wave limit reached. */
	TurnBasedLimitReached,
	/** No enemies close enough to start turn-based combat. */
	NoEnemiesInRange
};

/**
 * Tuning values for the game flow. Defaults mirror the Godot project:
 * main.gd constants (TACTICAL_PAUSE_*, post turn-based pause) and
 * resources/game_balance_config.gd (preparation_phase_duration, wave_rest_duration,
 * tactical_uses_per_wave, max_campaign_waves). Filled from balance data in Phase 2.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGameFlowConfig
{
	GENERATED_BODY()

	/** Real seconds of preparation before the first wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Flow", meta = (ClampMin = "0"))
	float PreparationDuration = 60.f;

	/** Real seconds of rest (preparation) before every following wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Flow", meta = (ClampMin = "0"))
	float WaveRestDuration = 20.f;

	/** Number of waves in the mission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Flow", meta = (ClampMin = "1"))
	int32 TotalWaves = 10;

	/** Real seconds a tactical pause lasts before orders run automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Pause", meta = (ClampMin = "0"))
	float TacticalPauseDuration = 30.f;

	/** Tactical pause charges per wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Pause", meta = (ClampMin = "0"))
	int32 TacticalPauseMaxCharges = 3;

	/** Real seconds to recharge all charges once they are spent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Pause", meta = (ClampMin = "0"))
	float TacticalPauseCooldown = 20.f;

	/** World time dilation while the tactical pause is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Pause", meta = (ClampMin = "0", ClampMax = "1"))
	float TacticalPauseTimeDilation = 0.02f;

	/** Real seconds of free tactical pause after leaving turn-based combat (no charge spent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn-Based", meta = (ClampMin = "0"))
	float PostTurnBasedPauseDuration = 20.f;

	/** Turn-based entries allowed per wave; 0 or negative means unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turn-Based")
	int32 TurnBasedUsesPerWave = -1;
};
