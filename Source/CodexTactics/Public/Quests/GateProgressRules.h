#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "Quests/QuestChain.h"
#include "GateProgressRules.generated.h"

/**
 * How the checkpoint blast gate opens once its terminal is powered (level JSON "gate_open_mode").
 */
UENUM(BlueprintType)
enum class EGateOpenMode : uint8
{
	/** "instant": the terminal opens the gate at once (the checkpoint chain up to 2026-10-11, Godot gate.gd). */
	Instant,
	/**
	 * "generator_waves" (default, user request 2026-10-11): the overloaded grid only creeps the door open while the generator
	 * runs during the wave fight; it stays impassable until the final wave is cleared, then opens fully.
	 */
	GeneratorWaves
};

/** What the blast gate is doing (HUD line, world label, HQ radio). */
UENUM(BlueprintType)
enum class EGateProgressStatus : uint8
{
	/** Terminal not powered yet. */
	Locked,
	/** Powered, generator up, but no wave fight running (before the fight, between prepared waves). */
	Idle,
	/** Creeping open: generator up, a wave fight running, below this wave's cap. */
	Advancing,
	/** At this wave's cap: the grid cannot push it further until the wave is beaten. */
	HoldingForWave,
	/** Generator down (broken / not running): progress paused, never reversed. */
	NoPower,
	/** Final wave cleared with power: the leaves slide fully open. */
	Opening,
	/** Fully open: no collision, no navmesh block. */
	Open
};

/**
 * Level data of the blast gate (level JSON keys "gate_*"; defaults = the user's rule for level_01 and the demo map).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGateProgressConfig
{
	GENERATED_BODY()

	/** "gate_open_mode": "generator_waves" (default) | "instant". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	EGateOpenMode Mode = EGateOpenMode::GeneratorWaves;

	/**
	 * "gate_progress_per_second": opening progress (0..1) gained per second of wave fight with the generator up
	 * (0.004 = 0.4 %/s: one wave's 30 % share in 75 s).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "0"))
	float ProgressPerSecond = 0.004f;

	/**
	 * "gate_max_progress_before_final": the most the door may reach before the final wave is cleared (clamped below 1).
	 * Each wave may push it up to MaxProgressBeforeFinal * wave / final wave (3 waves: 30 / 60 / 90 %).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "0", ClampMax = "0.99"))
	float MaxProgressBeforeFinal = 0.9f;

	/** "gate_full_open_after_wave": the wave whose clearing opens the door fully; 0 = the level's final wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (ClampMin = "0"))
	int32 FullOpenAfterWave = 0;
};

/** World state the gate progress depends on (one step). */
struct CODEXTACTICS_API FGateProgressInputs
{
	/** The gate terminal is powered (quest chain). */
	bool bUnlocked = false;
	/** Generator running (quest) and not broken down. */
	bool bGeneratorWorking = false;
	/** A wave fight runs in game time (real time or tactical pause; not the turn-based grid). */
	bool bFightRunning = false;
	/** Current wave, 1-based (0 before the first). */
	int32 WaveIndex = 0;
	/** Wave whose clearing opens the door (GateProgressRules::ResolveFinalWave). */
	int32 FinalWave = 1;
	/** That wave has been cleared (latched by the gate). */
	bool bFinalWaveCleared = false;
};

/** Result of one progress step. */
struct CODEXTACTICS_API FGateProgressStep
{
	float Progress = 0.f;
	EGateProgressStatus Status = EGateProgressStatus::Locked;
	/** The door must open fully now (final wave cleared with power, or the instant mode). */
	bool bOpenFully = false;
};

/**
 * Pure rules of the blast gate tied to the generator during the wave fight (user request 2026-10-11): the door creeps
 * open only while the generator works, pauses (never reverses) while it is down, stays impassable for the whole fight
 * and opens fully only after the final wave is cleared with the generator up.
 * Godot reference: design/levels/stage-01-bunker-gate.md («hold the line until the airlock fully opens»),
 * narrative DA_DialoguePrep #2 «power grid is overloaded»; Scenes/movements/gate.gd (the instant mode).
 */
namespace GateProgressRules
{
	/** "generator_waves" / "instant" (any case); false on anything else. */
	CODEXTACTICS_API bool ParseOpenMode(const FString& Text, EGateOpenMode& OutMode);
	CODEXTACTICS_API FString OpenModeToString(EGateOpenMode Mode);

	/** Wave whose clearing opens the door: FullOpenAfterWave clamped to the level's waves, 0 = the last one. */
	CODEXTACTICS_API int32 ResolveFinalWave(const FGateProgressConfig& Config, int32 TotalWaves);

	/** The door creeps only in a wave fight running in game time (real time / tactical pause), not on the turn-based grid. */
	CODEXTACTICS_API bool IsFightRunning(ECodexGamePhase Phase, ECodexCombatMode Mode);

	/** The final wave has been cleared: its victory panel, the post-combat sequence, or a later wave. */
	CODEXTACTICS_API bool IsFinalWaveCleared(ECodexGamePhase Phase, int32 WaveIndex, int32 FinalWave);

	/** Most the door may reach now: MaxProgressBeforeFinal * WaveIndex / FinalWave (< 1), or 1 once the final wave is cleared. */
	CODEXTACTICS_API float GetProgressCap(const FGateProgressConfig& Config, int32 WaveIndex, int32 FinalWave, bool bFinalWaveCleared);

	/** Advances the door by DeltaSeconds of game time. */
	CODEXTACTICS_API FGateProgressStep Step(const FGateProgressConfig& Config, float Progress, const FGateProgressInputs& Inputs, float DeltaSeconds);

	/**
	 * The warning beacon on the door spins (light on) while the opening process runs and the generator works: servos
	 * unlocked and the door not fully open yet (or sliding open), generator up. Generator down = off (user decision 2026-10-11).
	 */
	CODEXTACTICS_API bool ShouldBeaconSpin(bool bProgressUnlocked, bool bOpen, bool bSlidingOpen, bool bGeneratorWorking);

	/** The HUD / world label shows the gate line in this status. */
	CODEXTACTICS_API bool ShouldShowStatus(EGateProgressStatus Status);

	/** HUD / world label line, e.g. "BLAST DOOR 37% — GENERATOR OFFLINE". */
	CODEXTACTICS_API FString GetStatusText(EGateProgressStatus Status, float Progress);

	/** HQ radio line for a status change; false = nothing to say. bFinalWaveCleared picks the "last wave, no power" line. */
	CODEXTACTICS_API bool GetRadioLine(EGateProgressStatus Previous, EGateProgressStatus Next, bool bFinalWaveCleared, FText& OutSpeaker, FText& OutText);

	/** Objective while the paced door is powered but not open yet. */
	CODEXTACTICS_API FText GetHoldObjective();

	/**
	 * Rewrites the quest lines that promise an instantly opening gate (terminal «the blue gates swing open!», the gate
	 * «The gate is open») for the paced door. bGateOpen: the door is already fully open (the lines stay as they are).
	 */
	CODEXTACTICS_API void AdjustQuestLine(EInteractableType Type, bool bGatePowered, bool bGateOpen, FQuestInteractionResult& InOutResult);
}
