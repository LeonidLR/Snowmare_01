#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Environment/WeatherSnowFxRules.h"
#include "WeatherSnowFxComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Environment phase 4: the FALLING snow (docs/systems/environment-weather.md §6 / §12). Created by AWeatherPresenterActor; owns one Niagara
 * component built from NS_Weather_Snow (a retuned copy of the user's NS_SnowStorm, /Game/Environment/Weather/Snow/Niagara) that follows the
 * player's tactical camera view. Writes only the Niagara User parameters of the contract (User.WeatherIntensity, User.SpawnRate,
 * User.Opacity, User.WindStrength, User.WindDirection, User.DriftOpacity, User.DriftSpriteSize); the user may retune or swap the system.
 * Visual only: never touches gameplay; inactive without a renderer (-nullrhi smokes) and while the visuals are off.
 */
UCLASS(ClassGroup = (Environment), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UWeatherSnowFxComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWeatherSnowFxComponent();

	/**
	 * Called by the presenter every apply (throttled, plus on state changes). SnowIntensity 0..1 = blended weather preset value
	 * (0 Clear / Overcast, Snowfall ~0.5, Blizzard 1); Wind 0..1 with a world direction. DeltaSeconds since the previous call.
	 */
	void ApplySnowFx(float SnowIntensity, float WindStrength, const FVector& WindDirection, float DeltaSeconds);

	/** Visuals on / off (level JSON "visuals", Codex.Env.Visuals): off deactivates and hides the system. */
	void SetVisualsEnabled(bool bEnabled);

	/** The system to spawn (default NS_Weather_Snow; a user BP presenter may override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Starts the async load of SnowSystem once (never without a renderer). */
	void RequestLoad();
	/** The Niagara component, spawned on first need once the system is loaded (null without a renderer / asset). */
	UNiagaraComponent* EnsureNiagara();
	/** Moves the system to the ground point the player's view looks at; false without a player camera. */
	bool UpdatePlacement();
	/** Activates / softly deactivates by the smoothed intensity and writes the User parameters. */
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Niagara;

	/** Intensity -> spawn rate / opacity / drift mapping and smoothing (visual only). */
	UPROPERTY(EditAnywhere, Category = "Environment|Snow", meta = (AllowPrivateAccess = "true"))
	FWeatherSnowFxTuning Tuning;

	TSharedPtr<struct FStreamableHandle> LoadHandle;
	FVector WindDirection = FVector::ForwardVector;
	float TargetIntensity = 0.f;
	float CurrentIntensity = 0.f;
	float WindStrength = 0.f;
	bool bVisualsEnabled = true;
	bool bLoadRequested = false;
};
