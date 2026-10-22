#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnvironmentVisualSettings.generated.h"

class AWeatherPresenterActor;

/**
 * Project Settings > Game > Codex Environment Visuals (environment phase 3, docs/systems/environment-weather.md §10): which presenter
 * UEnvironmentSubsystem spawns into a game world that has none. PresenterClass defaults to the user's BP subclass
 * /Game/Environment/Weather/BP_WeatherPresenter; while that asset does not exist the C++ AWeatherPresenterActor is spawned.
 * The defaults live here in C++ (nothing is written to an .ini unless the user saves the settings page).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Codex Environment Visuals"))
class CODEXTACTICS_API UEnvironmentVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEnvironmentVisualSettings();

	/** Spawn the weather presenter automatically (a level JSON "environment": {"visuals": false} still opts a level out). */
	UPROPERTY(Config, EditAnywhere, Category = "Presenter")
	bool bSpawnPresenter = true;

	/** The presenter class (a BP child of AWeatherPresenterActor for art tweaks); missing / invalid -> AWeatherPresenterActor. */
	UPROPERTY(Config, EditAnywhere, Category = "Presenter")
	TSoftClassPtr<AWeatherPresenterActor> PresenterClass;
};
