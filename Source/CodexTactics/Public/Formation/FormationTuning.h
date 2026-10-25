#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

class FJsonObject;

/**
 * Formation tuning loader (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md, contract
 * docs/design/FORMATION_CONTRACT.md "Units")): Content/Data/FormationLab/formation_config.json, schema codex.formation_config/1,
 * {"params": {"<key>": {"value", "min", "max", "unit", "doc"}}}, scalars only. The file is owned by the Training Ground simulator
 * session (its tuner changes only "value", inside min..max); the FFormationConfig defaults mirror it (test
 * CodexTactics.Formation.Tuning.DefaultsMirrorFile). Read like the project's other JSON tuning (EnvironmentTuning): values over the
 * struct defaults, a missing key keeps the default, an unknown key is skipped with a warning, a value outside the file's min..max is
 * clamped with a warning. Get() loads the file lazily on its first call (nothing in the game reads it yet).
 */
namespace FormationTuning
{
	/** Content/Data/FormationLab/formation_config.json. */
	CODEXTACTICS_API FString GetDefaultPath();

	/** Every config key the struct knows, in the struct's order. */
	CODEXTACTICS_API TArray<FString> KnownKeys();

	/** The struct's value of Key; false when the key is unknown. */
	CODEXTACTICS_API bool GetValue(const FFormationConfig& Config, const FString& Key, float& OutValue);

	/**
	 * Reads a config file's text: OutValues = key -> value, OutRanges (optional) = key -> (min, max). False (OutError says why) when it
	 * is not JSON, has another schema or no "params" object; a param without a numeric value is skipped with a warning.
	 */
	CODEXTACTICS_API bool ParseConfigFile(const FString& JsonText, TMap<FString, float>& OutValues, TMap<FString, FVector2D>* OutRanges = nullptr,
		FString* OutError = nullptr);

	/** key -> number of a flat JSON object (golden vectors' "config_values"); non-numbers are skipped. */
	CODEXTACTICS_API TMap<FString, float> ReadFlatValues(const FJsonObject& Object);

	/**
	 * Values over Base; Ranges (optional) clamp them; unknown keys are listed in OutUnknown (optional) and skipped.
	 */
	CODEXTACTICS_API FFormationConfig FromValues(const TMap<FString, float>& Values, const FFormationConfig& Base = FFormationConfig(),
		const TMap<FString, FVector2D>* Ranges = nullptr, TArray<FString>* OutUnknown = nullptr);

	/** key -> value of a config. */
	CODEXTACTICS_API TMap<FString, float> ToValues(const FFormationConfig& Config);

	/** The values in force (the file on the first call; the defaults when it is missing). */
	CODEXTACTICS_API const FFormationConfig& Get();
	CODEXTACTICS_API void Set(const FFormationConfig& Config);

	/** Loads the file into Get(); false (values kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool ApplyFile(const FString& Path);
}
