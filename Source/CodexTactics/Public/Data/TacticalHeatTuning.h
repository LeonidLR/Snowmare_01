#pragma once

#include "CoreMinimal.h"
#include "Survival/TacticalHeatRules.h"

class FJsonObject;

/**
 * Sprint 15 heat-source balance (UE-only): Content/Data/Survival/tactical_heat.json, read by the game mode at StartPlay before the
 * squad spawns. Keys are the snake_case names of FTacticalHeatConfig (heat_pack_cold_reduction, campfire_burn_seconds, ...;
 * distances in cm, times in seconds) plus "starting_loadout": { "commander": { "heat_packs", "firewood", "flares", "stoves" }, ... }.
 * A missing key keeps the default (Gemini's spec value). Kept out of GameBalanceConfig: that header is generated from Godot.
 * Console: CodexTactics.DumpTacticalHeat.
 */
namespace TacticalHeatTuning
{
	/** Content/Data/Survival/tactical_heat.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The values in force (defaults until ApplyFile / Set). */
	CODEXTACTICS_API const FTacticalHeatConfig& Get();
	CODEXTACTICS_API void Set(const FTacticalHeatConfig& Config);
	/** Values of Json over Base. */
	CODEXTACTICS_API FTacticalHeatConfig FromJson(const FJsonObject& Json, const FTacticalHeatConfig& Base = FTacticalHeatConfig());
	CODEXTACTICS_API TSharedRef<FJsonObject> ToJson(const FTacticalHeatConfig& Config);
	/** Loads the file into Get(); false (values kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool ApplyFile(const FString& Path);
}
