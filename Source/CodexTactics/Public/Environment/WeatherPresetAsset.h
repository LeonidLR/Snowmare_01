#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/WeatherVisualTypes.h"
#include "WeatherPresetAsset.generated.h"

class UNiagaraSystem;

/**
 * The look of one weather state (environment phase 3, UE-only; docs/systems/environment-weather.md §5 / §10). Instances:
 * /Game/Environment/Weather/Presets/DA_Weather_Clear / _Overcast / _Snowfall / _Blizzard (Scripts/Editor/create_weather_presets.py),
 * read by AWeatherPresenterActor through soft references; a missing asset falls back to EnvironmentVisualRules::GetDefaultPreset.
 * Reference (design only): the WeatherSystem_01 S_* preset structs / DA_Snowy / DA_Sunny. The Niagara layers and the MPC values
 * are data for phase 4 (the Niagara contract of §5) - the presenter does not spawn / write them yet.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UWeatherPresetAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The weather state this preset shows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	EWeatherState Weather = EWeatherState::Clear;

	/** Light / sky / fog / post-process targets (multipliers of the map's authored values where noted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ShowOnlyInnerProperties))
	FWeatherVisualPreset Visual;

	/** Phase 4: the Niagara layers of this weather (falling snow, ground drift, frost sparkle, blizzard sheets); code writes only the User.* contract. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase4")
	TArray<TSoftObjectPtr<UNiagaraSystem>> NiagaraLayers;

	/** Resets Visual to the C++ default of Weather (EnvironmentVisualRules::GetDefaultPreset). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Weather")
	void ResetToDefaults();
};
