#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "Quests/QuestChain.h"

/** Metadata of one save slot (Godot SaveManager.get_save_metadata). */
struct CODEXTACTICS_API FSaveSlotInfo
{
	FString SlotName;
	FString Title;
	int64 Timestamp = 0;
	FString DateTime;
	/** "manual", "quicksave" or "autosave". */
	FString SaveType;
	FString Author;
	FString StageName;
	FString SquadSummary;
	int32 Wave = 1;
	int32 SquadCount = 0;
	/** Save format version (1 = Godot-compatible original, see SaveGameRules::CurrentSaveVersion). */
	int32 Version = 1;
	/** Map the save was made on (package name, e.g. /Game/Maps/L_MovementTest); empty in version 1 saves. */
	FString MapName;
};

/** Why saving is not possible right now (save policy, user decision 2026-10-08: only outside combat). */
enum class ESaveBlockReason : uint8
{
	None,
	/** A wave / ambush fight is on (real time, tactical pause or turn-based). */
	Combat,
	/** The pre-combat cutscene runs (the fight is about to start; the autosave follows). */
	Cutscene,
	/** The mission is lost. */
	GameOver
};

/** When an automatic save is written (save policy 2026-10-08). */
enum class EAutosaveMoment : uint8
{
	None,
	/** Right before a fight starts (wave start / ambush start): written with the pre-combat phase. */
	BeforeCombat,
	/** Right after a fight ends (wave cleared / victory). */
	AfterCombat
};

/** Game flow to restore from a save (see SaveGameRules::ResolveLoadFlow). */
struct CODEXTACTICS_API FSaveLoadFlow
{
	/** Exploration or Preparation. */
	ECodexGamePhase Phase = ECodexGamePhase::Exploration;
	int32 WaveIndex = 0;
	bool bCombatUnlocked = false;
};

/**
 * Pure save-game rules. Godot reference: Scripts/managers/save_manager.gd (sanitize_slot_name, save_type by slot name,
 * suggest_next_slot_name, compute_current_stage_name, compute_squad_summary).
 */
namespace SaveGameRules
{
	/**
	 * Save format version. 1: the Godot keys (squad, game_state, quest_state, crates, piles). 2 (2026-10-08): + map,
	 * flow phase / preparation time / ambush, postures, bonus items, enemies (patrols, dead stay dead), every
	 * interactable (relocated / burning barrels, deployables, tripwires, traps, generator, hidden quest objects), patrol
	 * routes, gate, dialogue triggers, narrative elements, Susanin's rescue. Version 1 saves still load (missing
	 * sections leave the world as it is).
	 */
	constexpr int32 CurrentSaveVersion = 2;

	/** Slot of the automatic saves (the "auto" in the name makes it an autosave, Godot save_manager.gd). */
	CODEXTACTICS_API FString GetAutosaveSlotName();

	/** Save policy: Exploration / Preparation / WaveCleared / PostCombat allow saving; the fight, cutscene and game over do not. */
	CODEXTACTICS_API ESaveBlockReason GetSaveBlockReason(ECodexGamePhase Phase);

	/** Player-facing reason ("Saving is disabled during combat"); empty for None. */
	CODEXTACTICS_API FText GetSaveBlockText(ESaveBlockReason Reason);

	/** A phase change that writes an autosave: into WaveCombat from outside it = BeforeCombat, WaveCombat -> WaveCleared = AfterCombat. */
	CODEXTACTICS_API EAutosaveMoment GetAutosaveMoment(ECodexGamePhase OldPhase, ECodexGamePhase NewPhase);

	/**
	 * The flow a load resumes in. Version 1 (no phase): preparation / wave -> Preparation of the saved wave, else
	 * Exploration. Version 2: Exploration / Preparation as saved; WaveCombat (a forced save) -> Preparation of that wave
	 * (an ambush fight -> Exploration before the ambush); Cutscene -> Preparation of wave 1; WaveCleared -> Preparation of
	 * the next wave, or the finished battle (Exploration, combat locked again) after the last wave / a single ambush fight;
	 * PostCombat -> Exploration, combat locked.
	 */
	CODEXTACTICS_API FSaveLoadFlow ResolveLoadFlow(int32 Version, ECodexGamePhase SavedPhase, bool bCombatUnlocked, bool bPreparationActive,
		bool bWaveActive, int32 WaveIndex, int32 TotalWaves, bool bAmbushFight, bool bAmbushSingleFight);

	/** Phase name stored in the save ("Exploration", ...); ParsePhase reads it back (unknown -> false). */
	CODEXTACTICS_API FString PhaseToString(ECodexGamePhase Phase);
	CODEXTACTICS_API bool ParsePhase(const FString& Text, ECodexGamePhase& OutPhase);

	/** Godot sanitize_slot_name: trims, replaces \ / : * ? " < > | with _, strips leading / trailing dots; empty -> "Leonid_01". */
	CODEXTACTICS_API FString SanitizeSlotName(const FString& Raw);

	/** "autosave" if the slot name contains "auto", "quicksave" for "quick" (or the legacy Russian "быстр"), else "manual". */
	CODEXTACTICS_API FString GetSaveType(const FString& SlotName);

	/** Godot suggest_next_slot_name: Prefix_NN with NN = highest existing Prefix_NN + 1 (two digits). */
	CODEXTACTICS_API FString SuggestNextSlotName(const TArray<FString>& ExistingSlots, const FString& Prefix = TEXT("Leonid"));

	/** Godot compute_current_stage_name (wave defence / preparation / quest chain step, "[Solo]"). */
	CODEXTACTICS_API FString GetStageName(const FQuestChainState& Quests, bool bGateOpen, bool bWaveActive, bool bPreparation,
		int32 WaveIndex, bool bSoloMode);

	/** Godot compute_squad_summary: "Operatives: alive/total | HP: avg%" (average over everyone, the dead count as 0). */
	CODEXTACTICS_API FString GetSquadSummary(const TArray<TPair<float, float>>& HealthAndMax);
}
