#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Environment/WeatherRainFxRules.h"
#include "WeatherRainFxComponent.generated.h"

class UDirectionalLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UPostProcessComponent;
class URainFxSettings;

/** What the presenter feeds the rain each application (docs/systems/environment-weather.md section 18). */
struct CODEXTACTICS_API FWeatherRainFxInput
{
	/** GetWeatherAmount of the Rain (Snowfall) and Storm (Blizzard) rungs, 0..1 each. */
	float RainAmount = 0.f;
	float StormAmount = 0.f;
	/** The MPC values: RainIntensity (environment.json), PuddleAmount (state), ScreenDrops (0 / 1). */
	float RainIntensity = 0.f;
	float PuddleAmount = 0.f;
	float ScreenDrops = 0.f;
	/** The blended preset wind 0..1 and its world TRAVEL direction. */
	float WindStrength = 0.f;
	FVector WindDirection = FVector::ForwardVector;
	/** The environment's RNG state (FEnvironmentState::RandomSeed): the lightning schedule is derived from it, never advancing it. */
	int32 Seed = 0;
	/**
	 * Section 19: the level's climate is Temperate (set by the presenter's temperate path only). Lightning is gated on it at the source
	 * (WeatherRainFxRules::IsLightningAllowed): a default input (an arctic level, before the first application) can never flash.
	 */
	bool bTemperateClimate = false;
};

/**
 * Section 18: the RAIN of a temperate (narrative) level for the tactical camera. Created by AWeatherPresenterActor ONLY when the level's
 * climate is Temperate (an arctic level never has one: zero cost there). Mirrors UWeatherSnowFxComponent:
 * - falling rain: NS_Weather_Rain (a retuned copy of the migrated NS_Raindrops, which stays untouched) follows the tactical view (the
 *   ground point it looks at, moved upwind in a strong wind), streaks slanted by the wind, small ground splashes; async load; nothing
 *   without a renderer; the visuals toggle; no rain indoors (the nearest Closed / Shelter camera zones and the roof over the view focus /
 *   squad leader become Niagara kill boxes - the falling snow's rules, WeatherSnowFxRules);
 * - puddle splashes: NS_Weather_PuddleSplashes (a copy of NS_PuddleSplashes) on the ground near the view, density = look intensity x
 *   MPC PuddleAmount;
 * - screen drops: a dynamic instance of PPMI_Weather_RainScreenDrops as a blendable of its own unbound post-process component on the
 *   presenter, strength = DA_RainFX.ScreenDropsStrength x MPC ScreenDrops x RainIntensity (off = the component disabled, no pass);
 * - lightning (Storm): a shadowless directional flash light, strikes at deterministic random intervals derived from the environment's seed
 *   (never advancing its RNG); off on automated runs (Codex.Env.Lightning -1 auto / 0 / 1).
 * The look comes from DA_RainFX (URainFxSettings; the C++ defaults of FWeatherRainFxTuning when it is missing). Visual only.
 */
UCLASS(ClassGroup = (Environment), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UWeatherRainFxComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWeatherRainFxComponent();

	/** Called by the presenter every application. DeltaSeconds since the previous call (a big value jumps the look to its target). */
	void ApplyRainFx(const FWeatherRainFxInput& Input, float DeltaSeconds);

	/** Visuals on / off (level JSON "visuals", Codex.Env.Visuals): off deactivates and hides everything at once. */
	void SetVisualsEnabled(bool bEnabled);

	/** Level start / save load: the next activation is pre-warmed (the air full of rain on the first frame), assets loaded now. */
	void RequestStartWarmup();

	/** The tuning in force: DA_RainFX once loaded, else the component's own (C++ defaults); a dev override wins. */
	const FWeatherRainFxTuning& GetActiveTuning() const;
	bool IsUsingSettingsAsset() const;
	void SetTuningOverride(const TOptional<FWeatherRainFxTuning>& Override);

	/** Smoothed look intensity in force and its target. */
	float GetCurrentIntensity() const { return CurrentIntensity; }
	float GetTargetIntensity() const { return TargetIntensity; }

	/** The rain / puddle Niagara systems are active. */
	bool IsRainActive() const;
	bool ArePuddleSplashesActive() const;
	UNiagaraComponent* GetRainNiagara() const { return RainNiagara; }
	UNiagaraComponent* GetPuddleNiagara() const { return PuddleNiagara; }

	/** The parameters written last (smokes / captures). */
	const FWeatherRainFxParams& GetLastParams() const { return LastParams; }
	float GetLastPuddleSplashRate() const { return LastPuddleSplashRate; }
	float GetLastScreenDropsStrength() const { return LastScreenDropsStrength; }
	/** The screen-drops post process is enabled (strength > 0). */
	bool AreScreenDropsOn() const;

	/** The kill boxes written last (world AABBs, nearest first). */
	const TArray<FBox>& GetKillBoxes() const { return KillBoxes; }

	/** Particles alive over all emitters of the rain (and the puddle splashes) system. */
	int32 GetParticleCount() const;
	int32 GetPuddleParticleCount() const;
	FString DescribeParticles() const;

	/** Lightning: running now (storm + not suppressed), strikes so far, the flash scale in force. */
	bool IsLightningRunning() const { return bLightningRunning; }
	int32 GetStrikeCount() const { return StrikeCount; }
	float GetFlashScale() const { return FlashScale; }
	/** Captures: a strike starts now (even when suppressed; needs a renderer). */
	void ForceStrike();

	/** Dev / captures: hides the rain systems (they keep simulating) for a pixel / GPU A/B. */
	void SetHiddenForCapture(bool bHidden);

	/** Warm-ups run (smokes). */
	int32 GetWarmupCount() const { return WarmupCount; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UNiagaraSystem> PuddleSplashSystem;

	/** The look (default /Game/Environment/Weather/Rain/DA_RainFX). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<URainFxSettings> Settings;

	/** The drop material (a dynamic instance is bound to NS_Weather_Rain's User.DropMaterial: colour, brightness, near fade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UMaterialInterface> DropMaterial;

	/** The screen-drops post-process material (a dynamic instance becomes a blendable: Strength, Tiling, Speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UMaterialInterface> ScreenDropsMaterial;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

private:
	void RequestLoad();
	UNiagaraComponent* MakeNiagara(UNiagaraSystem* System, const TCHAR* Name);
	UNiagaraComponent* EnsureRain();
	UNiagaraComponent* EnsurePuddles();
	bool UpdatePlacement();
	void UpdateKillBoxes();
	void Refresh();
	void WriteRain(UNiagaraComponent& Fx, const FWeatherRainFxTuning& Active);
	void WritePuddles(UNiagaraComponent& Fx, const FWeatherRainFxTuning& Active);
	void RefreshScreenDrops(const FWeatherRainFxTuning& Active);
	void UpdateLightning(const FWeatherRainFxTuning& Active);
	void SetFlash(float Scale, const FWeatherRainFxTuning& Active);
	void ActivateWarmedUp();

	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> RainNiagara;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> PuddleNiagara;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DropMaterialInstance;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScreenDropsInstance;
	UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> ScreenDropsPost;
	UPROPERTY(Transient) TObjectPtr<UDirectionalLightComponent> FlashLight;

	/** Fallback when DA_RainFX is missing (C++ defaults; editable on the component). */
	UPROPERTY(EditAnywhere, Category = "Environment|Rain", meta = (AllowPrivateAccess = "true"))
	FWeatherRainFxTuning Tuning;

	TOptional<FWeatherRainFxTuning> TuningOverride;
	TSharedPtr<struct FStreamableHandle> LoadHandle;
	FWeatherRainFxInput LastInput;
	FWeatherRainFxParams LastParams;
	TArray<FBox> KillBoxes;
	FVector LastFocus = FVector::ZeroVector;
	float TargetIntensity = 0.f;
	float CurrentIntensity = 0.f;
	float LastPuddleSplashRate = 0.f;
	float LastScreenDropsStrength = 0.f;
	float FlashScale = 0.f;
	double NextKillBoxUpdate = 0.;
	double NextStrikeTime = -1.;
	double StrikeTime = -1.;
	int32 LightningSeed = 0;
	int32 StrikeIndex = 0;
	int32 StrikeCount = 0;
	int32 WarmupCount = 0;
	bool bLightningRunning = false;
	bool bHasFocus = false;
	bool bVisualsEnabled = true;
	bool bLoadRequested = false;
	bool bStartWarmupPending = false;
	bool bActivateWithWarmup = false;
	bool bHiddenForCapture = false;
	bool bLoggedSource = false;
};
