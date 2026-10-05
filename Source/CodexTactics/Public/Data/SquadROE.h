#pragma once

#include "CoreMinimal.h"
#include "Characters/SquadAutonomyRules.h"

class FJsonObject;

/**
 * The Commander Mode tactical ROE (Sprint 07-E, UE-only): Content/Data/AI/squad_roe.json, written by the Wave Editor's
 * «Тактика отряда (ROE)» tab (/api/squad-roe), read by the game mode at StartPlay. The file holds the 13 parameters
 * under their snake_case names (anchor_radius_meters, leash_strictness "Flexible" | "Strict", ...); a missing key keeps
 * the default. Kept out of GameBalanceConfig: that header is generated from Godot and the editor cannot write assets.
 */
namespace SquadROE
{
	/** Content/Data/AI/squad_roe.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The ROE in force (defaults until ApplyFile / Set). */
	CODEXTACTICS_API const FSquadROE& Get();
	CODEXTACTICS_API void Set(const FSquadROE& ROE);
	/** Parameters of Json over Base (unknown enum names keep the base value). */
	CODEXTACTICS_API FSquadROE FromJson(const FJsonObject& Json, const FSquadROE& Base = FSquadROE());
	CODEXTACTICS_API TSharedRef<FJsonObject> ToJson(const FSquadROE& ROE);
	/** Loads the file into Get(); false (defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool ApplyFile(const FString& Path);
}
