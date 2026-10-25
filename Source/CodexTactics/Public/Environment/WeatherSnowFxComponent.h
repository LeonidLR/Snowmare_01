#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Environment/WeatherSnowFxRules.h"
#include "WeatherSnowFxComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USnowFxSettings;

/**
 * Environment phase 4: the FALLING snow (docs/systems/environment-weather.md §6 / §12.b). Created by AWeatherPresenterActor; owns one Niagara
 * component built from NS_Weather_Snow (a retuned copy of the user's NS_SnowStorm, which stays untouched in /Game/Environment/Weather/Snow/Niagara)
 * that follows the player's tactical camera view.
 * The look comes from the data asset DA_SnowFX (USnowFxSettings; the C++ defaults of FWeatherSnowFxTuning when it is missing) and is written
 * every refresh to NS_Weather_Snow's User parameters (the contract: WeatherIntensity, SpawnRate, Opacity, WindStrength, WindDirection,
 * DriftOpacity, DriftSpriteSize; the look: FlakeSizeMin / Max, ..., see §12.b) and to a dynamic instance of the flake material (near fade).
 * No snow indoors: the nearest Closed / Shelter camera zones and the roof over the view focus / squad leader become Niagara kill boxes.
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

	/** The tuning in force: DA_SnowFX once loaded, else the component's own (C++ defaults); a dev override wins. */
	const FWeatherSnowFxTuning& GetActiveTuning() const;

	/** True once DA_SnowFX is loaded and drives the look. */
	bool IsUsingSettingsAsset() const;

	/** Dev / captures only: replaces the tuning until reset (e.g. the old flake size for a before / after still). */
	void SetTuningOverride(const TOptional<FWeatherSnowFxTuning>& Override);

	/** The kill boxes written last (world AABBs, nearest first; empty = none or indoors kill off). */
	const TArray<FBox>& GetKillBoxes() const { return KillBoxes; }

	/** The spawned Niagara component (null before the asset loaded / without a renderer). */
	UNiagaraComponent* GetNiagara() const { return Niagara; }

	/** The snow Niagara system is spawned and active. */
	bool IsSnowActive() const;

	/** Dev / captures only: hides the snow for a pixel A / B without stopping it (the flakes keep simulating). */
	void SetHiddenForCapture(bool bHidden);

	/** The system to spawn (default NS_Weather_Snow; a user BP presenter may override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	/** The look of the snow (default /Game/Environment/Weather/Snow/DA_SnowFX). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<USnowFxSettings> Settings;

	/** The flake material the near fade / softness / brightness go to (a dynamic instance is bound to NS_Weather_Snow's User.FlakeMaterial). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<UMaterialInterface> FlakeMaterial;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Starts the async load of SnowSystem + Settings + FlakeMaterial once (never without a renderer). */
	void RequestLoad();
	/** The Niagara component, spawned on first need once the system is loaded (null without a renderer / asset). */
	UNiagaraComponent* EnsureNiagara();
	/** Moves the system to the ground point the player's view looks at; false without a player camera. Fills LastFocus. */
	bool UpdatePlacement();
	/** Activates / softly deactivates by the smoothed intensity and writes the User parameters. */
	void Refresh();
	/** Writes the look (sizes, lifetime, gravity, spawn area, drift, material) of the active tuning. */
	void WriteLook(UNiagaraComponent& Fx, const FWeatherSnowFxParams& Params, const FWeatherSnowFxTuning& Active);
	/** Gathers the interior zones / roofs near the focus and writes the kill box user parameters. */
	void UpdateKillBoxes();

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Niagara;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlakeMaterialInstance;

	/** Fallback when DA_SnowFX is missing (C++ defaults; editable on the component). */
	UPROPERTY(EditAnywhere, Category = "Environment|Snow", meta = (AllowPrivateAccess = "true"))
	FWeatherSnowFxTuning Tuning;

	TOptional<FWeatherSnowFxTuning> TuningOverride;
	TSharedPtr<struct FStreamableHandle> LoadHandle;
	TArray<FBox> KillBoxes;
	FVector LastFocus = FVector::ZeroVector;
	FVector WindDirection = FVector::ForwardVector;
	float TargetIntensity = 0.f;
	float CurrentIntensity = 0.f;
	float WindStrength = 0.f;
	double NextKillBoxUpdate = 0.;
	bool bHasFocus = false;
	bool bVisualsEnabled = true;
	bool bLoadRequested = false;
	bool bLoggedSource = false;
};
