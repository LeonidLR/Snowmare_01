#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/WeatherVisualTypes.h"
#include "GameFramework/Actor.h"
#include "WeatherPresenterActor.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyAtmosphere;
class ASkyLight;
class AVolumetricCloud;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialParameterCollection;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UWeatherPresetAsset;
class UWeatherSnowFxComponent;
class UWeatherSnowOverlayComponent;
struct FEnvironmentState;

/** The values the map authored on the adopted actors (recorded at adoption, written back when the visuals turn off). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeatherAuthoredLighting
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sun") FRotator SunRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") float SunIntensity = 10.f;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") FLinearColor SunColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") bool bSunUseTemperature = false;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") float SunTemperature = 6500.f;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") float SunSourceAngle = 0.5357f;
	UPROPERTY(BlueprintReadOnly, Category = "Sun") bool bSunVisible = true;

	UPROPERTY(BlueprintReadOnly, Category = "Moon") FRotator MoonRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Moon") float MoonIntensity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Moon") FLinearColor MoonColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "Moon") bool bMoonVisible = true;

	UPROPERTY(BlueprintReadOnly, Category = "Sky") float RayleighScatteringScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float MieScatteringScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float SkyLightIntensity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fog") float FogDensity = 0.02f;
	UPROPERTY(BlueprintReadOnly, Category = "Fog") float FogHeightFalloff = 0.2f;
	UPROPERTY(BlueprintReadOnly, Category = "Fog") FLinearColor FogInscattering = FLinearColor(0.447f, 0.638f, 1.f);
	UPROPERTY(BlueprintReadOnly, Category = "Fog") float FogStartDistance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Fog") float FogMaxOpacity = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Fog") bool bVolumetricFog = false;
	UPROPERTY(BlueprintReadOnly, Category = "Fog") float VolumetricFogExtinctionScale = 1.f;

	/** The adopted unbound post-process volume's settings (unused when the presenter's own component is used). */
	UPROPERTY(BlueprintReadOnly, Category = "PostProcess") FPostProcessSettings PostSettings;
};

/** The authored state of one AVolumetricCloud the presenter switched off. */
USTRUCT()
struct FWeatherCloudAuthored
{
	GENERATED_BODY()

	UPROPERTY() TWeakObjectPtr<AVolumetricCloud> Cloud;
	bool bHiddenInGame = false;
	bool bComponentVisible = true;
	bool bComponentActive = true;
};

/**
 * The VISUAL presenter of the day cycle + weather (environment phase 3, UE-only; docs/systems/environment-weather.md §5 / §10).
 * Spawned by UEnvironmentSubsystem into a game world that has none (never placed in a map; UEnvironmentVisualSettings::PresenterClass
 * may name the user's BP_WeatherPresenter). ADOPTS the map's lighting actors - the sun (ADirectionalLight), ASkyAtmosphere, ASkyLight,
 * AExponentialHeightFog, an unbound APostProcessVolume - records their authored values and drives them, throttled (ApplyRateHz) and
 * at once on UEnvironmentSubsystem::OnStateSet, from the hour curves (DayCurves) x the preset of the weather blended toward the next
 * one (DA_Weather_* soft refs, C++ fallbacks). Missing actors are skipped; the moon (a second directional light) and the post-process
 * are the presenter's own components unless the map has them. The map's AVolumetricCloud is switched OFF while the visuals run (user
 * decision 2026-10-22: the tactical camera never sees it). Visual only: no gameplay reads any of it; smokes stay deterministic.
 * Off -> every authored value is restored: level JSON "environment": {"visuals": false} (never spawned) or the console variable
 * Codex.Env.Visuals 0. Reference (design only): WeatherSystem_01 BP_DynamivSky.
 * Phase 4 (§12): every application also drives the falling snow (UWeatherSnowFxComponent: blended SnowIntensity, WindStrength, a slowly
 * drifting wind direction), the gradual snow cover on objects (UWeatherSnowOverlayComponent: FEnvironmentState::SnowAmount + blended
 * FrostAmount; targets re-scanned every TargetRefreshInterval) and MPC_Environment (scalars SnowAmount / FrostAmount / WindStrength /
 * IsSnowing, vector WindDirection); visuals off -> both components off and the MPC values back to 0 (the authored look).
 * Section 16 (game designer 2026-10-25): the presenter STANDS ALONE - every part of sun / sky atmosphere / sky light / height fog the
 * level lacks is its own component (OwnSun / OwnSkyAtmosphere / OwnSkyLight / OwnHeightFog, defaults = the Demo 01 levels' template
 * lighting; tune them in BP_WeatherPresenter), so a level's "Lighting" folder can be deleted (Scripts/Editor/remove_level_lighting.py);
 * a level that still has its actors keeps them adopted (EnvironmentStartRules::PlanRig, logged "[EnvVisuals] rig mode ..."). The level
 * start is shown from the first frame: applied in BeginPlay, the eye adaptation snaps, the falling snow is pre-warmed. PLACED in a level
 * without lights it is the editor preview too: OnConstruction lights the editor viewport as PreviewHour / PreviewWeather (12:00 Clear).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AWeatherPresenterActor : public AActor
{
	GENERATED_BODY()

public:
	AWeatherPresenterActor();

	/**
	 * Spawns the presenter into World unless one exists, the settings disable it or bLevelVisuals is false (the level's
	 * "environment": {"visuals": false}). Returns the presenter in World (null when none).
	 */
	static AWeatherPresenterActor* SpawnForWorld(UWorld& World, bool bLevelVisuals);

	/** The presenter of World (null without one). */
	static AWeatherPresenterActor* Find(const UWorld* World);

	/** Codex.Env.Visuals (1 = the presenters drive the lighting, 0 = authored lighting). */
	static bool AreVisualsEnabled();
	static void SetVisualsEnabled(bool bEnabled);

	/** The presenter currently drives the adopted actors (false = their authored values are in place). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	bool IsApplying() const { return bApplying; }

	/** Recomputes and applies the frame now (no-op while not applying). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void ApplyNow();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	const FWeatherVisualFrame& GetLastFrame() const { return LastFrame; }

	const FWeatherAuthoredLighting& GetAuthored() const { return Authored; }
	int32 GetApplyCount() const { return ApplyCount; }

	/** The preset in force for Weather (asset when loaded, else the C++ default). */
	const FWeatherVisualPreset& GetPreset(EWeatherState Weather) const;
	bool IsPresetFromAsset(EWeatherState Weather) const;

	/** The level's sun actor when adopted (null with the own sun: GetSunLight). */
	ADirectionalLight* GetSun() const { return Sun; }
	/** Section 16: the light components in use - the adopted actor's or the presenter's own (null when neither). */
	UDirectionalLightComponent* GetSunLight() const { return SunComp; }
	USkyAtmosphereComponent* GetSkyAtmosphereComponent() const { return SkyAtmosphereComp; }
	USkyLightComponent* GetSkyLightComponent() const { return SkyLightComp; }
	UExponentialHeightFogComponent* GetHeightFogComponent() const { return FogComp; }
	/** Section 16: adopted / own / mixed (EnvironmentStartRules::PlanRig of the level's lighting actors). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	EWeatherRigMode GetRigMode() const { return RigPlan.Mode; }
	const FWeatherRigPlan& GetRigPlan() const { return RigPlan; }
	/** Section 16: game seconds since the last snap (level start / state jump); negative before the first. */
	float GetSecondsSinceSnap() const;
	/** Section 16: the eye adaptation runs at the snap speed now. */
	bool IsExposureSnapping() const;
	/** Section 16: the game time of the first application (0 at a level start = in BeginPlay, before the world's first tick); -1 = none. */
	double GetFirstApplyGameTime() const { return FirstApplyTime; }

	/**
	 * Section 16, the start cover: the level's first rendered frames (drawn before the camera, the lighting history and the pre-warmed
	 * snow are in place - an editor -game run showed the view at the world origin with default materials) stay black; lifted on the
	 * presenter's tick after the camera ticked and the falling snow is warmed (or not snowing), at the latest StartCoverMaxTicks.
	 * Only when no other camera fade runs (the factory's start fade-in wins); not without a renderer.
	 */
	bool WasStartCoverShown() const { return StartCoverTicks >= 0; }
	bool IsStartCoverOn() const { return bStartCoverOn; }
	/** Presenter ticks the cover stayed (-1 = never shown). */
	int32 GetStartCoverTicks() const { return StartCoverTicks; }
	/** The light component of a directional light actor. ADirectionalLight::GetComponent() is editor-only data (does not compile in a game build). */
	static UDirectionalLightComponent* GetDirectionalComponent(const ADirectionalLight* Light);
	ASkyAtmosphere* GetSkyAtmosphere() const { return SkyAtmosphere; }
	ASkyLight* GetSkyLight() const { return SkyLight; }
	AExponentialHeightFog* GetHeightFog() const { return HeightFog; }
	APostProcessVolume* GetAdoptedPostProcess() const { return AdoptedPostProcess; }
	int32 GetCloudCount() const { return Clouds.Num(); }
	/** The moon light in use: the map's second directional light when adopted, else the presenter's own MoonLight. */
	UDirectionalLightComponent* GetActiveMoon() const;
	bool IsMoonOwn() const { return AdoptedMoon == nullptr; }
	UPostProcessComponent* GetOwnPostProcess() const { return PostProcess; }

	/** "sun DirectionalLight, sky ..., moon own, clouds 1 off" for logs / smokes. */
	FString DescribeAdopted() const;

	/** Hour curves of the sun / moon / sky light / fog / exposure (defaults in C++; a BP subclass may retune them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Day")
	FEnvironmentDayCurves DayCurves;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Presets")
	TSoftObjectPtr<UWeatherPresetAsset> PresetClear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Presets")
	TSoftObjectPtr<UWeatherPresetAsset> PresetOvercast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Presets")
	TSoftObjectPtr<UWeatherPresetAsset> PresetSnowfall;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Presets")
	TSoftObjectPtr<UWeatherPresetAsset> PresetBlizzard;

	/**
	 * Phase 4: MPC_Environment, written every application: scalars SnowAmount (persistent cover), FrostAmount, WindStrength, IsSnowing
	 * (0..1, ComputeIsSnowing), vector WindDirection (xyz direction, w strength); section 15: SnowTop / SnowPX / SnowNX / SnowPY / SnowNY
	 * (FEnvironmentState::ObjectSnow, world axes). Missing asset -> nothing written.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Phase4")
	TSoftObjectPtr<UMaterialParameterCollection> EnvironmentParameters;

	/** Seconds between re-scans of the snow overlay's target meshes (streaming / spawned props) while the visuals run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Phase4", meta = (ClampMin = "0.5"))
	float TargetRefreshInterval = 5.f;

	/** The wind blows from this world yaw (degrees) on average... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Phase4")
	float WindBaseYaw = 35.f;

	/** ...and drifts this many degrees either way... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Phase4", meta = (ClampMin = "0", ClampMax = "180"))
	float WindDriftDegrees = 25.f;

	/** ...over this period (game seconds). Visual only (never read by gameplay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Phase4", meta = (ClampMin = "1"))
	float WindDriftPeriodSeconds = 180.f;

	/**
	 * Horizontal unit wind TRAVEL direction at TimeSeconds (where the flakes are blown to): BaseYaw + DriftDegrees x sin(2 pi t / Period)
	 * (pure; tests). The presenter applies UEnvironmentSubsystem::GetWindDirection (this formula unless a debug override is set).
	 */
	static FVector ComputeWindDirection(float TimeSeconds, float BaseYaw, float DriftDegrees, float PeriodSeconds);

	/** MPC IsSnowing 0..1 from the blended preset SnowIntensity: smooth, 1 from SnowIntensity 0.25 up (any real snowfall). */
	static float ComputeIsSnowing(float SnowIntensity);

	/** The loaded MPC_Environment (null when the asset is missing). */
	UMaterialParameterCollection* GetEnvironmentCollection() const { return LoadedParameters; }

	/** The value this world's MPC_Environment instance holds for a scalar (false without the collection / parameter). */
	bool GetEnvironmentScalar(FName Name, float& OutValue) const;

	UWeatherSnowFxComponent* GetSnowFx() const { return SnowFx; }
	UWeatherSnowOverlayComponent* GetSnowOverlay() const { return SnowOverlay; }
	/** The wind direction of the last application. */
	const FVector& GetLastWindDirection() const { return LastWindDirection; }

	/** Updates per second while the clock runs (plus one at every OnStateSet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "1", ClampMax = "120"))
	float ApplyRateHz = 20.f;

	/** A sky light without real-time capture is recaptured at most this often (seconds), only when the look changed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.25"))
	float SkyRecaptureInterval = 2.f;

	/** Switch the map's AVolumetricCloud off while the visuals run (user decision 2026-10-22: invisible from the tactical camera). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bDisableVolumetricClouds = true;

	/**
	 * Section 16: a part of the rig the level lacks (sun, sky atmosphere, sky light, height fog) is lit by the presenter's own component.
	 * Off = the old behaviour (a missing part stays missing). The level's own actors always win while they exist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Own rig")
	bool bBuildMissingLighting = true;

	/**
	 * Section 16, editor preview: a presenter PLACED in a level lights the editor viewport (not PIE / game) with its own rig for the parts
	 * the level lacks, as this hour and weather (the C++ / DA_Weather_* look; no snow). Default 12:00 Clear = plain working light; 23 /
	 * Snowfall shows the Demo 01 lake's game look. Off = the own rig stays dark in the editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Editor preview")
	bool bEditorPreview = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Editor preview", meta = (ClampMin = "0", ClampMax = "24"))
	float PreviewHour = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Editor preview")
	EWeatherState PreviewWeather = EWeatherState::Clear;

	/** After every application: the frame that was applied (art tweaks in the BP subclass). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Environment")
	void OnEnvironmentApplied(const FWeatherVisualFrame& Frame);

	/** The authored lighting was restored (visuals turned off). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Environment")
	void OnEnvironmentVisualsRestored();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<USceneComponent> Root;

	/** The moon when the map has no second directional light (Movable, atmosphere light index 1, hidden by day). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	/** Unbound post-process (exposure / saturation / tint) when the map has no unbound post-process volume. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Phase 4: the falling snow (Niagara, follows the tactical view; NS_Weather_Snow by default). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Phase4")
	TObjectPtr<UWeatherSnowFxComponent> SnowFx;

	/** Phase 4: the gradual snow cover on outdoor meshes (overlay material M_SnowOverlay by default). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Phase4")
	TObjectPtr<UWeatherSnowOverlayComponent> SnowOverlay;

	/**
	 * Section 16, the own rig (hidden unless the level lacks that part). Its class-default values are the "authored" baseline the hour /
	 * weather multiply (like a level's own actors) and the look with the visuals off: tune them in BP_WeatherPresenter.
	 * Defaults = the Demo 01 levels' template lighting: sun 6 lux Movable (atmosphere light 0, 4 cascades to 200 m), sky atmosphere
	 * (planet top 60 m below the presenter), sky light real-time capture, height fog 0.0436 / falloff 0.2 / black inscattering at -68.5 m.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Own rig")
	TObjectPtr<UDirectionalLightComponent> OwnSun;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Own rig")
	TObjectPtr<USkyAtmosphereComponent> OwnSkyAtmosphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Own rig")
	TObjectPtr<USkyLightComponent> OwnSkyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weather|Own rig")
	TObjectPtr<UExponentialHeightFogComponent> OwnHeightFog;

private:
	void LoadPresets();
	/**
	 * Finds the level's lighting actors, plans the rig (FLevelLightingInventory -> EnvironmentStartRules::PlanRig), points the *Comp
	 * members at the adopted / own components, shows only the used own parts and records the authored values.
	 */
	void Adopt();
	/** Section 16: plans the rig for Inventory, binds the *Comp members (adopted actor's or own), shows the used own parts, records Authored. */
	void BindRig(const FLevelLightingInventory& Inventory, bool bBuildMissing);
	/** Section 16: snow jump + pre-warm + exposure snap for the next application (level start, state jump). */
	void BeginSnap();
	/** Section 16: the editor preview of a placed presenter (OnConstruction in an editor world). */
	void ApplyEditorPreview();
	/** Section 16: the start cover on (BeginPlay) / its lift check (Tick). */
	void ShowStartCover();
	void TickStartCover();
	/** Whether the presenter should drive the lighting now (console variable, the level's "visuals", the environment initialised). */
	bool WantsToApply() const;
	void StartApplying();
	void StopApplying();
	void Apply();
	void ApplySun(const FWeatherVisualFrame& Frame);
	void ApplyMoon(const FWeatherVisualFrame& Frame);
	void ApplySky(const FWeatherVisualFrame& Frame);
	void ApplyFog(const FWeatherVisualFrame& Frame);
	void ApplyPostProcess(const FWeatherVisualFrame& Frame);
	/** Phase 4: falling snow, snow cover, MPC_Environment. */
	void ApplySnow(const FWeatherVisualFrame& Frame, float SnowAmount);
	void WriteParameters(float SnowAmount, float FrostAmount, float WindStrength, float IsSnowing, const FVector& WindDirection);
	/** Section 15: MPC_Environment.SnowTop / SnowPX / SnowNX / SnowPY / SnowNY. */
	void WriteObjectSnow(const FDirectionalSnow& Pockets);
	void SetSnowVisualsEnabled(bool bEnabled);
	void SetCloudsOff(bool bOff);
	void RestoreAuthored();
	void RecaptureSkyIfNeeded(const FWeatherVisualFrame& Frame, bool bForce);
	void HandleStateSet(const FEnvironmentState& State);

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	// Section 16: the components driven (the adopted actors' or the own rig's).
	UPROPERTY(Transient) TObjectPtr<UDirectionalLightComponent> SunComp;
	UPROPERTY(Transient) TObjectPtr<USkyAtmosphereComponent> SkyAtmosphereComp;
	UPROPERTY(Transient) TObjectPtr<USkyLightComponent> SkyLightComp;
	UPROPERTY(Transient) TObjectPtr<UExponentialHeightFogComponent> FogComp;
	FWeatherRigPlan RigPlan;
	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> AdoptedMoon;
	UPROPERTY(Transient) TObjectPtr<ASkyAtmosphere> SkyAtmosphere;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLight;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> HeightFog;
	UPROPERTY(Transient) TObjectPtr<APostProcessVolume> AdoptedPostProcess;
	UPROPERTY(Transient) TArray<FWeatherCloudAuthored> Clouds;

	UPROPERTY(Transient) FWeatherAuthoredLighting Authored;
	UPROPERTY(Transient) FWeatherVisualFrame LastFrame;
	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> LoadedParameters;

	FWeatherVisualPreset Presets[4];
	bool bPresetFromAsset[4] = { false, false, false, false };

	FDelegateHandle StateSetHandle;
	float SinceApply = 0.f;
	/** Game time of the last snap (level start / state jump; the exposure snap window); -1 = none yet. */
	double SnapTime = -1.;
	double FirstApplyTime = -1.;
	/** Section 16: frames left that are flagged as camera cuts after a snap (no TAA / motion blur smear from the view's first placement). */
	int32 CameraCutFramesLeft = 0;
	/** Section 16: the start cover (a manual black camera fade of the presenter's own colour). */
	int32 StartCoverTicks = -1;
	bool bStartCoverOn = false;
	/** UEnvironmentSubsystem::GetSnapSerial seen at the last snap. */
	int32 SeenSnapSerial = -1;
	float LastRecaptureTime = -1000.f;
	float LastRecaptureSunElevation = -1000.f;
	EWeatherState LastRecaptureWeather = EWeatherState::Clear;
	int32 ApplyCount = 0;
	FVector LastWindDirection = FVector::ForwardVector;
	float LastSnowApplyTime = -1.f;
	float SinceTargetRefresh = 0.f;
	bool bAdopted = false;
	bool bApplying = false;
	bool bDirty = true;
	bool bCloudsOff = false;
	bool bSunMovable = false;
	bool bSunAdjustable = false;
	bool bSkyLightAdjustable = false;
};
