#pragma once

#include "CoreMinimal.h"
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
};

/**
 * Pure save-game rules. Godot reference: Scripts/managers/save_manager.gd (sanitize_slot_name, save_type by slot name,
 * suggest_next_slot_name, compute_current_stage_name, compute_squad_summary).
 */
namespace SaveGameRules
{
	/** Godot sanitize_slot_name: trims, replaces \ / : * ? " < > | with _, strips leading / trailing dots; empty -> «Леонид_01». */
	CODEXTACTICS_API FString SanitizeSlotName(const FString& Raw);

	/** "autosave" if the slot name contains "auto", "quicksave" for "quick" / «быстр», else "manual". */
	CODEXTACTICS_API FString GetSaveType(const FString& SlotName);

	/** Godot suggest_next_slot_name: Prefix_NN with NN = highest existing Prefix_NN + 1 (two digits). */
	CODEXTACTICS_API FString SuggestNextSlotName(const TArray<FString>& ExistingSlots, const FString& Prefix = TEXT("Леонид"));

	/** Godot compute_current_stage_name (wave defence / preparation / quest chain step, «[Соло]»). */
	CODEXTACTICS_API FString GetStageName(const FQuestChainState& Quests, bool bGateOpen, bool bWaveActive, bool bPreparation,
		int32 WaveIndex, bool bSoloMode);

	/** Godot compute_squad_summary: «Бойцов: alive/total | HP: avg%» (average over everyone, the dead count as 0). */
	CODEXTACTICS_API FString GetSquadSummary(const TArray<TPair<float, float>>& HealthAndMax);
}
