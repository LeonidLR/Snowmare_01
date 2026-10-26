#include "Environment/WeatherPresenterActor.h"

#include "CodexTactics.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Environment/EnvironmentVisualSettings.h"
#include "Environment/WeatherPresetAsset.h"
#include "Environment/WeatherRainFxComponent.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"

UDirectionalLightComponent* AWeatherPresenterActor::GetDirectionalComponent(const ADirectionalLight* Light)
{
	return Light ? Cast<UDirectionalLightComponent>(Light->GetLightComponent()) : nullptr;
}

namespace WeatherPresenterLocal
{
	TAutoConsoleVariable<int32> CVarVisuals(
		TEXT("Codex.Env.Visuals"), 1,
		TEXT("Environment visuals (docs/systems/environment-weather.md): 1 = the weather presenter drives the map's sun / sky / fog / post-process ")
		TEXT("from the day cycle + weather, 0 = the map's authored lighting is restored."),
		ECVF_Default);

	const TCHAR* PresetPaths[4] = {
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Clear.DA_Weather_Clear"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Overcast.DA_Weather_Overcast"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Snowfall.DA_Weather_Snowfall"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Blizzard.DA_Weather_Blizzard"),
	};

	// Section 17: the Temperate profile's presets (Clear = SunnyDay, Snowfall = Rain, Blizzard = Storm).
	const TCHAR* TemperatePresetPaths[4] = {
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Temperate_Sunny.DA_Weather_Temperate_Sunny"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Temperate_Overcast.DA_Weather_Temperate_Overcast"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Temperate_Rain.DA_Weather_Temperate_Rain"),
		TEXT("/Game/Environment/Weather/Presets/DA_Weather_Temperate_Storm.DA_Weather_Temperate_Storm"),
	};

	int32 ClimateIndex(EClimateProfile Climate)
	{
		return Climate == EClimateProfile::Temperate ? 1 : 0;
	}

	const TCHAR* EnvironmentCollectionPath = TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment");
	const TCHAR* SnowSystemPath = TEXT("/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow.NS_Weather_Snow");
	const TCHAR* SnowOverlayPath = TEXT("/Game/Environment/Weather/Snow/Materials/M_SnowOverlay.M_SnowOverlay");

	// MPC_Environment parameter names (Scripts/Editor/create_environment_mpc.py).
	const FName ParamSnowAmount(TEXT("SnowAmount"));
	const FName ParamFrostAmount(TEXT("FrostAmount"));
	const FName ParamWindStrength(TEXT("WindStrength"));
	const FName ParamIsSnowing(TEXT("IsSnowing"));
	const FName ParamWindDirection(TEXT("WindDirection"));
	// Section 17: the rain scalars (temperate levels; the rain FX / wet materials read them).
	const FName ParamRainIntensity(TEXT("RainIntensity"));
	const FName ParamWetness(TEXT("Wetness"));
	const FName ParamPuddleAmount(TEXT("PuddleAmount"));
	const FName ParamIsRaining(TEXT("IsRaining"));
	const FName ParamClimateTemperate(TEXT("ClimateTemperate"));
	const FName ParamScreenDrops(TEXT("ScreenDrops"));
	/** A "delta" long enough for the snow FX intensity smoothing to reach its target at once (the first application after a snap). */
	constexpr float SnowJumpStartSeconds = 1000.f;
	/** Frames after a snap rendered as camera cuts: the level's first frames carry no temporal history (TAA / motion blur) of the view jump. */
	constexpr int32 SnapCameraCutFrames = 3;
	/** The start cover: at least / at most this many presenter ticks (the camera has ticked; a safety cap if the snow never gets ready). */
	constexpr int32 StartCoverMinTicks = 2;
	constexpr int32 StartCoverMaxTicks = 10;
	/** The start cover's fade colour: black with a marker the other fades never use, so the presenter only ever lifts its own. */
	const FLinearColor StartCoverColor(0.f, 0.f, 0.0005f, 1.f);

	APlayerCameraManager* LocalCameraManager(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->PlayerCameraManager.Get() : nullptr;
	}
	// Section 15: the objects' directional snow pockets (world axes), in FDirectionalSnow pocket order.
	const FName ParamObjectSnow[FDirectionalSnow::PocketCount] = { FName(TEXT("SnowTop")), FName(TEXT("SnowPX")), FName(TEXT("SnowNX")),
		FName(TEXT("SnowPY")), FName(TEXT("SnowNY")) };

	/** The soft reference names an existing package (checked first: loading a missing one logs warnings). */
	bool PackageExists(const FSoftObjectPath& Path)
	{
		return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
	}

	FString MobilityName(EComponentMobility::Type Mobility)
	{
		switch (Mobility)
		{
		case EComponentMobility::Static: return TEXT("Static");
		case EComponentMobility::Stationary: return TEXT("Stationary");
		default: return TEXT("Movable");
		}
	}

	FString NameOf(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString(TEXT("none"));
	}

	/** "SunActor" / "own" / "none" for DescribeAdopted. */
	FString PartName(const AActor* Adopted, bool bOwnUsed)
	{
		return Adopted ? Adopted->GetName() : FString(bOwnUsed ? TEXT("own") : TEXT("none"));
	}

	/**
	 * The authored values of an own-rig component: its archetype (the class default / BP_WeatherPresenter template), so values a level
	 * instance got from the editor preview (OnConstruction) never become the baseline. A level's adopted component is its own source.
	 */
	template <typename T>
	const T* AuthoredSource(const T* Component, bool bOwn)
	{
		const T* Archetype = bOwn && Component ? Cast<T>(Component->GetArchetype()) : nullptr;
		return Archetype ? Archetype : Component;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

UEnvironmentVisualSettings::UEnvironmentVisualSettings()
{
	PresenterClass = TSoftClassPtr<AWeatherPresenterActor>(FSoftObjectPath(TEXT("/Game/Environment/Weather/BP_WeatherPresenter.BP_WeatherPresenter_C")));
}

// ---------------------------------------------------------------------------------------------------------------------

AWeatherPresenterActor::AWeatherPresenterActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// A console toggle in the tactical pause still restores / re-applies; the state itself is frozen there.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(Root);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->Intensity = 0.f;
	MoonLight->bUseTemperature = false;
	MoonLight->LightSourceAngle = 0.3f;
	MoonLight->SetVisibleFlag(false);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = false;
	PostProcess->Priority = 0.f;
	PostProcess->BlendWeight = 1.f;

	// Section 16: the own rig - hidden until a level lacks that part. Defaults = the Demo 01 levels' template "Lighting" folder (the
	// remove_level_lighting.py dry-run dump, 2026-10-25), so a level without its lights looks as it did with them.
	OwnSun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("OwnSun"));
	OwnSun->SetupAttachment(Root);
	OwnSun->SetMobility(EComponentMobility::Movable);
	OwnSun->SetRelativeRotation(FRotator(-49.5f, -10.3f, 0.f)); // the factory's daylight (the look with the visuals off)
	OwnSun->Intensity = 6.f;
	OwnSun->bUseTemperature = true;
	OwnSun->Temperature = 6500.f;
	OwnSun->LightSourceAngle = 0.7357f;
	OwnSun->bAtmosphereSunLight = true;
	OwnSun->AtmosphereSunLightIndex = 0;
	OwnSun->DynamicShadowDistanceMovableLight = 20000.f;
	OwnSun->DynamicShadowCascades = 4;
	OwnSun->SetVisibleFlag(false);

	OwnSkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("OwnSkyAtmosphere"));
	OwnSkyAtmosphere->SetupAttachment(Root);
	OwnSkyAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
	OwnSkyAtmosphere->SetRelativeLocation(FVector(0.f, 0.f, -6000.f));
	OwnSkyAtmosphere->SetVisibleFlag(false);

	OwnSkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("OwnSkyLight"));
	OwnSkyLight->SetupAttachment(Root);
	OwnSkyLight->SetMobility(EComponentMobility::Movable);
	OwnSkyLight->bRealTimeCapture = true;
	OwnSkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	OwnSkyLight->Intensity = 1.f;
	OwnSkyLight->bLowerHemisphereIsBlack = false;
	OwnSkyLight->SetRelativeLocation(FVector(0.f, 0.f, 600.f));
	OwnSkyLight->SetVisibleFlag(false);

	OwnHeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("OwnHeightFog"));
	OwnHeightFog->SetupAttachment(Root);
	OwnHeightFog->SetRelativeLocation(FVector(0.f, 0.f, -6850.f));
	OwnHeightFog->FogDensity = 0.0436f;
	OwnHeightFog->FogHeightFalloff = 0.2f;
	OwnHeightFog->FogInscatteringLuminance = FLinearColor(0.f, 0.f, 0.f, 1.f);
	OwnHeightFog->StartDistance = 0.f;
	OwnHeightFog->FogMaxOpacity = 1.f;
	OwnHeightFog->bEnableVolumetricFog = false;
	OwnHeightFog->SetVisibleFlag(false);

	TSoftObjectPtr<UWeatherPresetAsset>* Refs[4] = { &PresetClear, &PresetOvercast, &PresetSnowfall, &PresetBlizzard };
	TSoftObjectPtr<UWeatherPresetAsset>* TemperateRefs[4] = { &PresetTemperateSunny, &PresetTemperateOvercast, &PresetTemperateRain, &PresetTemperateStorm };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		*Refs[Index] = TSoftObjectPtr<UWeatherPresetAsset>(FSoftObjectPath(WeatherPresenterLocal::PresetPaths[Index]));
		*TemperateRefs[Index] = TSoftObjectPtr<UWeatherPresetAsset>(FSoftObjectPath(WeatherPresenterLocal::TemperatePresetPaths[Index]));
		Presets[0][Index] = EnvironmentVisualRules::GetDefaultPreset(static_cast<EWeatherState>(Index));
		Presets[1][Index] = EnvironmentVisualRules::GetDefaultPreset(EClimateProfile::Temperate, static_cast<EWeatherState>(Index));
	}

	// Phase 4: falling snow + gradual snow cover (separate components; the presenter only feeds them) and the MPC.
	SnowFx = CreateDefaultSubobject<UWeatherSnowFxComponent>(TEXT("SnowFx"));
	SnowFx->SetupAttachment(Root);
	SnowFx->SnowSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(WeatherPresenterLocal::SnowSystemPath));
	SnowOverlay = CreateDefaultSubobject<UWeatherSnowOverlayComponent>(TEXT("SnowOverlay"));
	SnowOverlay->OverlayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherPresenterLocal::SnowOverlayPath));
	EnvironmentParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(WeatherPresenterLocal::EnvironmentCollectionPath));
}

FVector AWeatherPresenterActor::ComputeWindDirection(float TimeSeconds, float BaseYaw, float DriftDegrees, float PeriodSeconds)
{
	const float Period = FMath::Max(1.f, PeriodSeconds);
	const float Yaw = BaseYaw + FMath::Clamp(DriftDegrees, 0.f, 180.f) * FMath::Sin(UE_TWO_PI * FMath::Fmod(TimeSeconds, Period) / Period);
	return FRotator(0.f, Yaw, 0.f).Vector();
}

float AWeatherPresenterActor::ComputeIsSnowing(float SnowIntensity)
{
	return FMath::Clamp(SnowIntensity / 0.25f, 0.f, 1.f);
}

bool AWeatherPresenterActor::GetEnvironmentScalar(FName Name, float& OutValue) const
{
	UWorld* World = GetWorld();
	if (!World || !LoadedParameters)
	{
		return false;
	}
	const UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(LoadedParameters);
	return Instance && Instance->GetScalarParameterValue(Name, OutValue);
}

AWeatherPresenterActor* AWeatherPresenterActor::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AWeatherPresenterActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

AWeatherPresenterActor* AWeatherPresenterActor::SpawnForWorld(UWorld& World, bool bLevelVisuals)
{
	if (!bLevelVisuals)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] %s: level \"visuals\": false - the authored lighting stays"), *World.GetMapName());
		return nullptr;
	}
	if (AWeatherPresenterActor* Existing = Find(&World))
	{
		return Existing;
	}
	const UEnvironmentVisualSettings* Settings = GetDefault<UEnvironmentVisualSettings>();
	if (!Settings->bSpawnPresenter)
	{
		return nullptr;
	}
	UClass* Class = AWeatherPresenterActor::StaticClass();
	if (WeatherPresenterLocal::PackageExists(Settings->PresenterClass.ToSoftObjectPath()))
	{
		UClass* Loaded = Settings->PresenterClass.LoadSynchronous();
		if (Loaded && Loaded->IsChildOf(AWeatherPresenterActor::StaticClass()))
		{
			Class = Loaded;
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[EnvVisuals] presenter class %s is not an AWeatherPresenterActor; the C++ presenter is used"),
				*Settings->PresenterClass.ToString());
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AWeatherPresenterActor* Presenter = World.SpawnActor<AWeatherPresenterActor>(Class, FTransform::Identity, Params);
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] %s: presenter %s spawned"), *World.GetMapName(), *GetNameSafe(Class));
	return Presenter;
}

bool AWeatherPresenterActor::AreVisualsEnabled()
{
	return WeatherPresenterLocal::CVarVisuals.GetValueOnGameThread() != 0;
}

void AWeatherPresenterActor::SetVisualsEnabled(bool bEnabled)
{
	WeatherPresenterLocal::CVarVisuals->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
}

const FWeatherVisualPreset& AWeatherPresenterActor::GetPreset(EWeatherState Weather) const
{
	return GetPreset(GetActiveClimate(), Weather);
}

bool AWeatherPresenterActor::IsPresetFromAsset(EWeatherState Weather) const
{
	return IsPresetFromAsset(GetActiveClimate(), Weather);
}

const FWeatherVisualPreset& AWeatherPresenterActor::GetPreset(EClimateProfile Climate, EWeatherState Weather) const
{
	return Presets[WeatherPresenterLocal::ClimateIndex(Climate)][FMath::Clamp(static_cast<int32>(Weather), 0, 3)];
}

bool AWeatherPresenterActor::IsPresetFromAsset(EClimateProfile Climate, EWeatherState Weather) const
{
	return bPresetFromAsset[WeatherPresenterLocal::ClimateIndex(Climate)][FMath::Clamp(static_cast<int32>(Weather), 0, 3)];
}

EClimateProfile AWeatherPresenterActor::GetActiveClimate() const
{
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	return Environment && Environment->IsInitialised() ? Environment->GetClimate() : EClimateProfile::Arctic;
}

UDirectionalLightComponent* AWeatherPresenterActor::GetActiveMoon() const
{
	return AdoptedMoon ? GetDirectionalComponent(AdoptedMoon) : MoonLight.Get();
}

float AWeatherPresenterActor::GetSecondsSinceSnap() const
{
	const UWorld* World = GetWorld();
	return World && SnapTime >= 0. ? static_cast<float>(FMath::Max(0., World->GetTimeSeconds() - SnapTime)) : -1.f;
}

bool AWeatherPresenterActor::IsExposureSnapping() const
{
	return EnvironmentStartRules::IsExposureSnapActive(GetSecondsSinceSnap(), EnvironmentStartRules::ExposureSnapSeconds);
}

FString AWeatherPresenterActor::DescribeAdopted() const
{
	using WeatherPresenterLocal::PartName;
	const UDirectionalLightComponent* SunLight = SunComp.Get();
	const USkyLightComponent* Sky = SkyLightComp.Get();
	// The arctic presets (the line every arctic level logged before section 17); a temperate level adds its own count.
	int32 AssetCount = 0;
	for (const bool bAsset : bPresetFromAsset[0])
	{
		AssetCount += bAsset ? 1 : 0;
	}
	int32 TemperateCount = 0;
	for (const bool bAsset : bPresetFromAsset[1])
	{
		TemperateCount += bAsset ? 1 : 0;
	}
	const bool bTemperate = GetActiveClimate() == EClimateProfile::Temperate;
	return FString::Printf(TEXT("rig %s: sun %s%s, moon %s, sky atmosphere %s, sky light %s%s, height fog %s, post-process %s, volumetric clouds %d%s, presets %d/4 from assets%s"),
		*EnvironmentStartRules::RigModeToString(RigPlan.Mode), *PartName(Sun, SunLight != nullptr),
		SunLight ? *FString::Printf(TEXT(" (%s)"), *WeatherPresenterLocal::MobilityName(SunLight->Mobility)) : TEXT(""),
		AdoptedMoon ? *AdoptedMoon->GetName() : TEXT("own"), *PartName(SkyAtmosphere, SkyAtmosphereComp != nullptr), *PartName(SkyLight, Sky != nullptr),
		Sky ? (Sky->bRealTimeCapture ? TEXT(" (real-time capture)") : TEXT(" (recaptured)")) : TEXT(""), *PartName(HeightFog, FogComp != nullptr),
		AdoptedPostProcess ? *AdoptedPostProcess->GetName() : TEXT("own"), Clouds.Num(), bCloudsOff ? TEXT(" off") : TEXT(""), AssetCount,
		bTemperate ? *FString::Printf(TEXT(", climate temperate: presets %d/4 from assets"), TemperateCount) : TEXT(""));
}

void AWeatherPresenterActor::BeginPlay()
{
	Super::BeginPlay();
	LoadPresets();
	if (WeatherPresenterLocal::PackageExists(EnvironmentParameters.ToSoftObjectPath()))
	{
		LoadedParameters = EnvironmentParameters.LoadSynchronous();
	}
	if (!LoadedParameters)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] %s missing: MPC_Environment is not written"), *EnvironmentParameters.ToString());
	}
	// Nothing of the snow shows until the presenter starts applying (visuals toggle).
	SetSnowVisualsEnabled(false);
	Adopt();
	if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this))
	{
		StateSetHandle = Environment->OnStateSet.AddUObject(this, &AWeatherPresenterActor::HandleStateSet);
	}
	// The level OPENS in its weather look (user feedback 2026-10-23: the lake first showed the authored daylight, then switched to the
	// snowy night after the old 4 s minimap hold): the whole state is applied synchronously here, in the same BeginPlay dispatch as the
	// map's actors (the subsystem spawns the presenter in OnWorldBeginPlay), before the first frame is rendered and before any start fade.
	// The minimap's one-off captures run on its own tick after this, so the minimap shows the weather look too. Section 16 (2026-10-25):
	// the first application snaps (exposure settles at once, the falling snow is pre-warmed and at full density on frame 1).
	if (WantsToApply())
	{
		StartApplying();
		ShowStartCover();
	}
	else
	{
		// The authored look; for the own rig its baseline (a level without lights is never left dark with the visuals off).
		RestoreAuthored();
	}
}

void AWeatherPresenterActor::ShowStartCover()
{
	APlayerCameraManager* CameraManager = WeatherPresenterLocal::LocalCameraManager(GetWorld());
	if (!FApp::CanEverRender() || !CameraManager || CameraManager->bEnableFading)
	{
		return; // headless, or another fade (e.g. the factory's start fade-in) already covers the start
	}
	CameraManager->SetManualCameraFade(1.f, WeatherPresenterLocal::StartCoverColor, false);
	bStartCoverOn = true;
	StartCoverTicks = 0;
}

void AWeatherPresenterActor::TickStartCover()
{
	if (!bStartCoverOn)
	{
		return;
	}
	++StartCoverTicks;
	const bool bSnowReady = !SnowFx || !SnowFx->IsWarmupActivationPending();
	if (StartCoverTicks < WeatherPresenterLocal::StartCoverMinTicks || (!bSnowReady && StartCoverTicks < WeatherPresenterLocal::StartCoverMaxTicks))
	{
		return;
	}
	bStartCoverOn = false;
	APlayerCameraManager* CameraManager = WeatherPresenterLocal::LocalCameraManager(GetWorld());
	// Lift only our own cover: a fade someone started meanwhile (another colour / auto-animated) is left alone.
	if (CameraManager && CameraManager->bEnableFading && CameraManager->FadeColor.Equals(WeatherPresenterLocal::StartCoverColor, 1.e-6f)
		&& FMath::IsNearlyEqual(CameraManager->FadeAmount, 1.f))
	{
		CameraManager->StopCameraFade();
		CameraManager->FadeAmount = 0.f;
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] start cover lifted after %d ticks (snow %s)"), StartCoverTicks,
			bSnowReady ? TEXT("ready") : TEXT("not ready - safety cap"));
	}
	else
	{
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] start cover: another camera fade took over, left alone"));
	}
}

void AWeatherPresenterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this))
	{
		Environment->OnStateSet.Remove(StateSetHandle);
	}
	if (bApplying)
	{
		RestoreAuthored();
		bApplying = false;
	}
	Super::EndPlay(EndPlayReason);
}

void AWeatherPresenterActor::LoadPresets()
{
	// Section 17: both profiles (four small data assets each), so a debug climate switch (Codex.Env.Climate) needs no load later.
	const TSoftObjectPtr<UWeatherPresetAsset>* Refs[2][4] = { { &PresetClear, &PresetOvercast, &PresetSnowfall, &PresetBlizzard },
		{ &PresetTemperateSunny, &PresetTemperateOvercast, &PresetTemperateRain, &PresetTemperateStorm } };
	const EClimateProfile Climates[2] = { EClimateProfile::Arctic, EClimateProfile::Temperate };
	for (int32 ClimateSlot = 0; ClimateSlot < 2; ++ClimateSlot)
	{
		const EClimateProfile Climate = Climates[ClimateSlot];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const EWeatherState Weather = static_cast<EWeatherState>(Index);
			Presets[ClimateSlot][Index] = EnvironmentVisualRules::GetDefaultPreset(Climate, Weather);
			bPresetFromAsset[ClimateSlot][Index] = false;
			if (!WeatherPresenterLocal::PackageExists(Refs[ClimateSlot][Index]->ToSoftObjectPath()))
			{
				continue;
			}
			if (const UWeatherPresetAsset* Asset = Refs[ClimateSlot][Index]->LoadSynchronous())
			{
				Presets[ClimateSlot][Index] = Asset->Visual;
				bPresetFromAsset[ClimateSlot][Index] = true;
				if (Asset->Weather != Weather || Asset->Climate != Climate)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("[EnvVisuals] preset %s says %s / %s but is used for %s / %s"), *Asset->GetName(),
						*EnvironmentRules::ClimateToString(Asset->Climate), *EnvironmentRules::WeatherToString(Asset->Weather),
						*EnvironmentRules::ClimateToString(Climate), *EnvironmentRules::WeatherToString(Weather));
				}
			}
		}
	}
}

void AWeatherPresenterActor::Adopt()
{
	UWorld* World = GetWorld();
	// Sun: the atmosphere sun light 0, else the brightest directional light. Moon: another one (atmosphere index 1 or "Moon" in its name).
	TArray<ADirectionalLight*> Lights;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (IsValid(*It) && GetDirectionalComponent(*It))
		{
			Lights.Add(*It);
		}
	}
	auto Score = [](const ADirectionalLight* Light)
	{
		const UDirectionalLightComponent* Component = GetDirectionalComponent(Light);
		const bool bAtmosphereSun = Component->IsUsedAsAtmosphereSunLight() && Component->GetAtmosphereSunLightIndex() == 0;
		return (bAtmosphereSun ? 1.e9f : 0.f) + Component->Intensity;
	};
	for (ADirectionalLight* Light : Lights)
	{
		if (!Sun || Score(Light) > Score(Sun))
		{
			Sun = Light;
		}
	}
	for (ADirectionalLight* Light : Lights)
	{
		if (Light != Sun && (Light->GetName().Contains(TEXT("Moon")) || GetDirectionalComponent(Light)->GetAtmosphereSunLightIndex() == 1))
		{
			AdoptedMoon = Light;
			break;
		}
	}
	for (TActorIterator<ASkyAtmosphere> It(World); It && !SkyAtmosphere; ++It)
	{
		SkyAtmosphere = IsValid(*It) && It->GetComponent() ? *It : nullptr;
	}
	for (TActorIterator<ASkyLight> It(World); It && !SkyLight; ++It)
	{
		SkyLight = IsValid(*It) && It->GetLightComponent() ? *It : nullptr;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It && !HeightFog; ++It)
	{
		HeightFog = IsValid(*It) && It->GetComponent() ? *It : nullptr;
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (IsValid(*It) && It->bUnbound && It->bEnabled && (!AdoptedPostProcess || It->Priority > AdoptedPostProcess->Priority))
		{
			AdoptedPostProcess = *It;
		}
	}
	for (TActorIterator<AVolumetricCloud> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			FWeatherCloudAuthored& Cloud = Clouds.AddDefaulted_GetRef();
			Cloud.Cloud = *It;
		}
	}

	// Section 16: the level's actors where they exist, the own rig for the missing parts.
	FLevelLightingInventory Inventory;
	Inventory.DirectionalLights = Lights.Num();
	Inventory.bSkyAtmosphere = SkyAtmosphere != nullptr;
	Inventory.bSkyLight = SkyLight != nullptr;
	Inventory.bHeightFog = HeightFog != nullptr;
	Inventory.bUnboundPostProcess = AdoptedPostProcess != nullptr;
	BindRig(Inventory, bBuildMissingLighting);
	bAdopted = true;
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] rig mode %s (%s): %s"), *EnvironmentStartRules::RigModeToString(RigPlan.Mode),
		RigPlan.Mode == EWeatherRigMode::Adopted ? TEXT("the level's own lighting actors are adopted")
			: (RigPlan.Mode == EWeatherRigMode::Own ? TEXT("the level has no lighting actors: the presenter's own rig lights it")
				: TEXT("the level's actors where they exist, the own rig for the rest")), *DescribeAdopted());
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] authored: sun %.2f %s pitch %.1f, sky light %.2f, rayleigh %.2f mie %.2f, fog %.4f falloff %.3f luminance %s volumetric %d"),
		Authored.SunIntensity, *Authored.SunColor.ToString(), Authored.SunRotation.Pitch, Authored.SkyLightIntensity, Authored.RayleighScatteringScale,
		Authored.MieScatteringScale, Authored.FogDensity, Authored.FogHeightFalloff, *Authored.FogInscattering.ToString(), Authored.bVolumetricFog ? 1 : 0);
}

void AWeatherPresenterActor::BindRig(const FLevelLightingInventory& Inventory, bool bBuildMissing)
{
	RigPlan = EnvironmentStartRules::PlanRig(Inventory, bBuildMissing);
	SunComp = Sun ? GetDirectionalComponent(Sun) : (RigPlan.bOwnSun ? OwnSun.Get() : nullptr);
	SkyAtmosphereComp = SkyAtmosphere ? SkyAtmosphere->GetComponent() : (RigPlan.bOwnSkyAtmosphere ? OwnSkyAtmosphere.Get() : nullptr);
	SkyLightComp = SkyLight ? SkyLight->GetLightComponent() : (RigPlan.bOwnSkyLight ? OwnSkyLight.Get() : nullptr);
	FogComp = HeightFog ? HeightFog->GetComponent() : (RigPlan.bOwnHeightFog ? OwnHeightFog.Get() : nullptr);
	const bool bOwnSunUsed = SunComp && SunComp == OwnSun;
	const bool bOwnSkyUsed = SkyAtmosphereComp && SkyAtmosphereComp == OwnSkyAtmosphere;
	const bool bOwnSkyLightUsed = SkyLightComp && SkyLightComp == OwnSkyLight;
	const bool bOwnFogUsed = FogComp && FogComp == OwnHeightFog;
	OwnSun->SetVisibility(bOwnSunUsed);
	OwnSkyAtmosphere->SetVisibility(bOwnSkyUsed);
	OwnSkyLight->SetVisibility(bOwnSkyLightUsed);
	OwnHeightFog->SetVisibility(bOwnFogUsed);

	// Record the authored values (an own part: its class-default baseline).
	if (SunComp)
	{
		const UDirectionalLightComponent* Light = WeatherPresenterLocal::AuthoredSource<UDirectionalLightComponent>(SunComp, bOwnSunUsed);
		Authored.SunRotation = bOwnSunUsed ? (GetActorRotation().Quaternion() * Light->GetRelativeRotation().Quaternion()).Rotator() : Light->GetComponentRotation();
		Authored.SunIntensity = Light->Intensity;
		Authored.SunColor = Light->GetLightColor();
		Authored.bSunUseTemperature = Light->bUseTemperature != 0;
		Authored.SunTemperature = Light->Temperature;
		Authored.SunSourceAngle = Light->LightSourceAngle;
		Authored.bSunVisible = bOwnSunUsed || Light->IsVisible();
		bSunMovable = SunComp->Mobility == EComponentMobility::Movable;
		bSunAdjustable = SunComp->Mobility != EComponentMobility::Static;
		if (!bSunMovable)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[EnvVisuals] the sun %s is %s: it is not rotated by the hour%s (make it Movable for the day cycle)"),
				*WeatherPresenterLocal::PartName(Sun, true), *WeatherPresenterLocal::MobilityName(SunComp->Mobility), bSunAdjustable ? TEXT("") : TEXT(" nor re-lit"));
		}
	}
	if (AdoptedMoon)
	{
		const UDirectionalLightComponent* Light = GetDirectionalComponent(AdoptedMoon);
		Authored.MoonRotation = Light->GetComponentRotation();
		Authored.MoonIntensity = Light->Intensity;
		Authored.MoonColor = Light->GetLightColor();
		Authored.bMoonVisible = Light->IsVisible();
	}
	if (SkyAtmosphereComp)
	{
		const USkyAtmosphereComponent* Atmosphere = WeatherPresenterLocal::AuthoredSource<USkyAtmosphereComponent>(SkyAtmosphereComp, bOwnSkyUsed);
		Authored.RayleighScatteringScale = Atmosphere->RayleighScatteringScale;
		Authored.MieScatteringScale = Atmosphere->MieScatteringScale;
	}
	if (SkyLightComp)
	{
		const USkyLightComponent* Light = WeatherPresenterLocal::AuthoredSource<USkyLightComponent>(SkyLightComp, bOwnSkyLightUsed);
		Authored.SkyLightIntensity = Light->Intensity;
		bSkyLightAdjustable = SkyLightComp->Mobility != EComponentMobility::Static;
	}
	if (FogComp)
	{
		const UExponentialHeightFogComponent* Fog = WeatherPresenterLocal::AuthoredSource<UExponentialHeightFogComponent>(FogComp, bOwnFogUsed);
		Authored.FogDensity = Fog->FogDensity;
		Authored.FogHeightFalloff = Fog->FogHeightFalloff;
		Authored.FogInscattering = Fog->FogInscatteringLuminance;
		Authored.FogStartDistance = Fog->StartDistance;
		Authored.FogMaxOpacity = Fog->FogMaxOpacity;
		Authored.bVolumetricFog = Fog->bEnableVolumetricFog;
		Authored.VolumetricFogExtinctionScale = Fog->VolumetricFogExtinctionScale;
	}
	Authored.PostSettings = AdoptedPostProcess ? AdoptedPostProcess->Settings : FPostProcessSettings();
	for (FWeatherCloudAuthored& Cloud : Clouds)
	{
		AVolumetricCloud* Actor = Cloud.Cloud.Get();
		const UVolumetricCloudComponent* Component = Actor ? Actor->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
		Cloud.bHiddenInGame = Actor && Actor->IsHidden();
		Cloud.bComponentVisible = !Component || Component->IsVisible();
		Cloud.bComponentActive = !Component || Component->IsActive();
	}

	// The moon: the presenter's own light shares the sky atmosphere as its second light.
	if (!AdoptedMoon)
	{
		const UDirectionalLightComponent* SunLight = SunComp.Get();
		const bool bSunInAtmosphere = SunLight && SunLight->IsUsedAsAtmosphereSunLight();
		MoonLight->SetAtmosphereSunLight(true);
		MoonLight->SetAtmosphereSunLightIndex(bSunInAtmosphere && SunLight->GetAtmosphereSunLightIndex() == 0 ? 1 : 0);
	}
}

bool AWeatherPresenterActor::WantsToApply() const
{
	if (!AreVisualsEnabled())
	{
		return false;
	}
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	return Environment && Environment->IsInitialised() && Environment->GetLevelSettings().bVisuals;
}

void AWeatherPresenterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickStartCover();
	if (CameraCutFramesLeft > 0)
	{
		// Section 16: the camera's first placement (start / load) must not smear the opening frames (the flag is read when the view renders).
		--CameraCutFramesLeft;
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr)
		{
			CameraManager->SetGameCameraCutThisFrame();
		}
	}
	if (!bAdopted)
	{
		return;
	}
	const bool bWant = WantsToApply();
	if (bWant && !bApplying)
	{
		StartApplying();
		return;
	}
	if (!bWant)
	{
		if (bApplying)
		{
			StopApplying();
		}
		return;
	}
	SinceApply += DeltaSeconds;
	if (bDirty || SinceApply >= 1.f / FMath::Max(ApplyRateHz, 1.f))
	{
		Apply();
	}
	SinceTargetRefresh += DeltaSeconds;
	if (SinceTargetRefresh >= FMath::Max(0.5f, TargetRefreshInterval))
	{
		SinceTargetRefresh = 0.f;
		if (SnowOverlay)
		{
			SnowOverlay->RefreshTargets();
		}
	}
}

void AWeatherPresenterActor::HandleStateSet(const FEnvironmentState& /*State*/)
{
	bDirty = true;
	if (bApplying && WantsToApply())
	{
		// Section 16: a JUMP of the whole state (save load / SetState) snaps like the level start; ForceWeather / SetHour stay smooth.
		const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
		if (Environment && EnvironmentStartRules::ShouldSnap(false, SeenSnapSerial, Environment->GetSnapSerial()))
		{
			BeginSnap();
		}
		Apply();
	}
}

void AWeatherPresenterActor::BeginSnap()
{
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	SeenSnapSerial = Environment ? Environment->GetSnapSerial() : SeenSnapSerial;
	SnapTime = GetWorld()->GetTimeSeconds();
	CameraCutFramesLeft = WeatherPresenterLocal::SnapCameraCutFrames;
	// The next ApplySnow jumps the falling snow to its target; a snowfall that starts with it is pre-warmed (the air full on frame 1).
	LastSnowApplyTime = -1.f;
	if (SnowFx)
	{
		SnowFx->RequestStartWarmup();
	}
	// Section 18: a temperate level starting in rain shows it on the first frame too.
	LastRainApplyTime = -1.f;
	if (GetActiveClimate() == EClimateProfile::Temperate)
	{
		if (UWeatherRainFxComponent* Rain = EnsureRainFx())
		{
			Rain->RequestStartWarmup();
		}
	}
}

void AWeatherPresenterActor::ApplyNow()
{
	if (bApplying)
	{
		Apply();
	}
}

void AWeatherPresenterActor::StartApplying()
{
	bApplying = true;
	if (!AdoptedPostProcess)
	{
		PostProcess->bEnabled = true;
	}
	SetCloudsOff(bDisableVolumetricClouds);
	SetSnowVisualsEnabled(true);
	if (SnowOverlay)
	{
		SnowOverlay->RefreshTargets();
	}
	SinceTargetRefresh = 0.f;
	// The first application snaps (section 16): after the snow visuals are on, before Apply (which jumps / warms the snow).
	BeginSnap();
	Apply();
	RecaptureSkyIfNeeded(LastFrame, true);
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] applying: %s"), *DescribeAdopted());
}

void AWeatherPresenterActor::StopApplying()
{
	RestoreAuthored();
	bApplying = false;
	bDirty = true;
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] off: the authored lighting is restored"));
	OnEnvironmentVisualsRestored();
}

void AWeatherPresenterActor::Apply()
{
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	if (!Environment || !Environment->IsInitialised())
	{
		return;
	}
	// Section 17: the presets of the climate in force (an arctic level: DA_Weather_* exactly as before).
	const EClimateProfile Climate = Environment->GetClimate();
	const FWeatherVisualFrame Frame = EnvironmentVisualRules::ComputeFrame(DayCurves, GetPreset(Climate, Environment->GetWeather()),
		GetPreset(Climate, Environment->GetNextWeather()), Environment->GetWeather(), Environment->GetNextWeather(), Environment->GetTransitionBlend(),
		Environment->GetHour());
	ApplySun(Frame);
	ApplyMoon(Frame);
	ApplySky(Frame);
	ApplyFog(Frame);
	// Section 18: a soaked temperate level keeps its wet darkening under the auto exposure (0 on arctic: no rain component there).
	WetExposureBias = Climate == EClimateProfile::Temperate && RainFx
		? RainFx->GetActiveTuning().WetExposureBias * FMath::Clamp(Environment->GetStateRef().Wetness, 0.f, 1.f) : 0.f;
	ApplyPostProcess(Frame);
	ApplySnow(Frame, Environment->GetSnowAmount());
	WriteObjectSnow(Environment->GetStateRef().ObjectSnow);
	ApplyRain(Climate, Frame);
	RecaptureSkyIfNeeded(Frame, false);
	LastFrame = Frame;
	if (ApplyCount == 0)
	{
		FirstApplyTime = GetWorld()->GetTimeSeconds();
	}
	++ApplyCount;
	bDirty = false;
	SinceApply = 0.f;
	UE_LOG(LogCodexTactics, VeryVerbose, TEXT("[EnvVisuals] hour %.2f %s->%s %.2f: sun %.1f deg x%.2f, moon %.1f deg x%.2f, fog %.3f"), Frame.Hour,
		*EnvironmentRules::WeatherToString(Frame.Weather), *EnvironmentRules::WeatherToString(Frame.NextWeather), Frame.Blend, Frame.SunElevation,
		Frame.SunIntensityScale, Frame.MoonElevation, Frame.MoonIntensityScale, Frame.Preset.FogDensity);
	OnEnvironmentApplied(Frame);
}

void AWeatherPresenterActor::ApplySun(const FWeatherVisualFrame& Frame)
{
	UDirectionalLightComponent* Light = SunComp.Get();
	if (!Light || !bSunAdjustable)
	{
		return;
	}
	if (bSunMovable)
	{
		Light->SetWorldRotation(FRotator(-Frame.SunElevation, Frame.SunYaw, 0.f));
	}
	Light->SetIntensity(Authored.SunIntensity * Frame.SunIntensityScale);
	Light->SetLightColor(Authored.SunColor * Frame.Preset.SunTint);
	Light->SetUseTemperature(true);
	Light->SetTemperature(Frame.SunTemperature);
	Light->SetLightSourceAngle(Frame.Preset.SunSourceAngle);
	Light->SetVisibility(Frame.bSunVisible);
}

void AWeatherPresenterActor::ApplyMoon(const FWeatherVisualFrame& Frame)
{
	UDirectionalLightComponent* Light = GetActiveMoon();
	if (!Light || Light->Mobility == EComponentMobility::Static)
	{
		return;
	}
	if (Light->Mobility == EComponentMobility::Movable)
	{
		Light->SetWorldRotation(FRotator(-Frame.MoonElevation, Frame.MoonYaw, 0.f));
	}
	// Relative to the authored SUN (the presenter's own moon has no authored value); an adopted moon keeps its own colour.
	Light->SetIntensity((SunComp ? Authored.SunIntensity : 10.f) * Frame.MoonIntensityScale);
	Light->SetLightColor(AdoptedMoon ? Authored.MoonColor : DayCurves.MoonColor);
	Light->SetVisibility(Frame.bMoonVisible);
}

void AWeatherPresenterActor::ApplySky(const FWeatherVisualFrame& Frame)
{
	if (USkyAtmosphereComponent* Atmosphere = SkyAtmosphereComp.Get())
	{
		Atmosphere->SetRayleighScatteringScale(Authored.RayleighScatteringScale * Frame.Preset.RayleighScatteringScale);
		Atmosphere->SetMieScatteringScale(Authored.MieScatteringScale * Frame.Preset.MieScatteringScale);
	}
	if (USkyLightComponent* Light = bSkyLightAdjustable ? SkyLightComp.Get() : nullptr)
	{
		Light->SetIntensity(Authored.SkyLightIntensity * Frame.SkyLightIntensityScale);
	}
}

void AWeatherPresenterActor::ApplyFog(const FWeatherVisualFrame& Frame)
{
	UExponentialHeightFogComponent* Fog = FogComp.Get();
	if (!Fog)
	{
		return;
	}
	const FWeatherVisualPreset& P = Frame.Preset;
	const float AuthoredBrightness = FMath::Max(Authored.FogInscattering.GetMax(), 0.05f);
	FLinearColor Inscattering = P.FogColor * (AuthoredBrightness * Frame.FogBrightness);
	Inscattering.A = 1.f;
	Fog->SetFogDensity(P.FogDensity);
	Fog->SetFogHeightFalloff(P.FogHeightFalloff);
	Fog->SetFogInscatteringColor(Inscattering);
	Fog->SetStartDistance(P.FogStartDistance);
	Fog->SetFogMaxOpacity(P.FogMaxOpacity);
	Fog->SetVolumetricFog(P.bVolumetricFog);
	Fog->SetVolumetricFogExtinctionScale(P.VolumetricFogExtinctionScale);
}

void AWeatherPresenterActor::ApplyPostProcess(const FWeatherVisualFrame& Frame)
{
	FPostProcessSettings& Settings = AdoptedPostProcess ? AdoptedPostProcess->Settings : PostProcess->Settings;
	const FPostProcessSettings& Base = Authored.PostSettings;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = Base.AutoExposureBias + Frame.ExposureBias + WetExposureBias;
	// Level start / state jump (section 16): the eye adaptation settles on the first frame (a night level must not open over-exposed and
	// fade down); after EnvironmentStartRules::ExposureSnapSeconds the authored adaptation speeds are back.
	const bool bSnapExposure = IsExposureSnapping();
	Settings.bOverride_AutoExposureSpeedUp = bSnapExposure || Base.bOverride_AutoExposureSpeedUp;
	Settings.bOverride_AutoExposureSpeedDown = bSnapExposure || Base.bOverride_AutoExposureSpeedDown;
	Settings.AutoExposureSpeedUp = bSnapExposure ? EnvironmentStartRules::ExposureSnapSpeed : Base.AutoExposureSpeedUp;
	Settings.AutoExposureSpeedDown = bSnapExposure ? EnvironmentStartRules::ExposureSnapSpeed : Base.AutoExposureSpeedDown;
	const FVector4 BaseSaturation = Base.bOverride_ColorSaturation ? Base.ColorSaturation : FVector4(1.f, 1.f, 1.f, 1.f);
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(BaseSaturation.X, BaseSaturation.Y, BaseSaturation.Z, BaseSaturation.W * Frame.Saturation);
	const FLinearColor BaseTint = Base.bOverride_SceneColorTint ? Base.SceneColorTint : FLinearColor::White;
	Settings.bOverride_SceneColorTint = true;
	Settings.SceneColorTint = BaseTint * Frame.ColorTint;
}

void AWeatherPresenterActor::ApplySnow(const FWeatherVisualFrame& Frame, float SnowAmount)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// The first application after StartApplying jump-starts the falling snow at its target intensity (no ramp from 0 at level start).
	const float Delta = LastSnowApplyTime < 0.f ? WeatherPresenterLocal::SnowJumpStartSeconds : FMath::Max(0.f, Now - LastSnowApplyTime);
	LastSnowApplyTime = Now;
	const FWeatherVisualPreset& P = Frame.Preset;
	// Section 15: one wind for the flakes, the MPC and the directional snow (the subsystem adds the debug override).
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	LastWindDirection = Environment ? Environment->GetWindDirection() : ComputeWindDirection(Now, WindBaseYaw, WindDriftDegrees, WindDriftPeriodSeconds);
	// The overlay shows while any object pocket (or the ground) holds snow: a top melted to 0 with snow on a windward side still shows.
	const float ObjectSnowMax = Environment ? Environment->GetStateRef().ObjectSnow.GetMax() : 0.f;
	// Section 17: a temperate level shows no falling snow (environment.json temperate.falling_snow) whatever its preset says.
	const bool bSnowAllowed = !Environment || EnvironmentRules::IsFallingSnowAllowed(Environment->GetConfig(), Environment->GetClimate());
	const float SnowIntensity = bSnowAllowed ? FMath::Clamp(P.SnowIntensity, 0.f, 1.f) : 0.f;
	if (SnowFx)
	{
		SnowFx->ApplySnowFx(SnowIntensity, FMath::Clamp(P.WindStrength, 0.f, 1.f), LastWindDirection, Delta);
	}
	if (SnowOverlay)
	{
		if (Environment && Environment->GetClimate() == EClimateProfile::Temperate)
		{
			// Section 18: a temperate level has no snow; the same overlay targets show the wet look (wetness, puddles on the ground slabs).
			const FEnvironmentState& State = Environment->GetStateRef();
			SnowOverlay->ApplyWetCover(State.Wetness, State.PuddleAmount);
		}
		else
		{
			SnowOverlay->ApplySnowCover(FMath::Clamp(FMath::Max(SnowAmount, ObjectSnowMax), 0.f, 1.f), FMath::Clamp(P.FrostAmount, 0.f, 1.f));
		}
	}
	WriteParameters(SnowAmount, P.FrostAmount, P.WindStrength, ComputeIsSnowing(SnowIntensity), LastWindDirection);
}

UWeatherRainFxComponent* AWeatherPresenterActor::EnsureRainFx()
{
	if (!RainFx && FApp::CanEverRender() && GetWorld() && GetWorld()->IsGameWorld())
	{
		RainFx = NewObject<UWeatherRainFxComponent>(this, TEXT("RainFx"));
		RainFx->SetupAttachment(Root);
		RainFx->RegisterComponent();
		RainFx->SetVisualsEnabled(bApplying);
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] rain FX component created (temperate climate)"));
	}
	return RainFx;
}

void AWeatherPresenterActor::ApplyRain(EClimateProfile Climate, const FWeatherVisualFrame& Frame)
{
	const UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this);
	if (!Environment || Climate != EClimateProfile::Temperate)
	{
		// Section 18: after a debug switch back to arctic the rain stops at once (an arctic level never created it).
		if (RainFx)
		{
			RainFx->ApplyRainFx(FWeatherRainFxInput(), WeatherPresenterLocal::SnowJumpStartSeconds);
		}
		LastRainApplyTime = -1.f;
		// An arctic level never writes them (they keep the MPC default 0); after a debug switch back from temperate: zeros once.
		if (bRainParamsWritten)
		{
			WriteRainParameters(0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			bRainParamsWritten = false;
		}
		return;
	}
	const FEnvironmentConfig& Config = Environment->GetConfig();
	const FEnvironmentState& State = Environment->GetStateRef();
	const float Rain = EnvironmentRules::GetRainIntensity(Config, State);
	const float ScreenDrops = EnvironmentRules::ShouldShowScreenDrops(Config, State) ? 1.f : 0.f;
	WriteRainParameters(Rain, State.Wetness, State.PuddleAmount, EnvironmentRules::ComputeIsRaining(Rain), 1.f, ScreenDrops);
	bRainParamsWritten = true;

	// Section 18: the rain visuals (the look per rung from DA_RainFX; the amounts blend through a transition).
	if (UWeatherRainFxComponent* RainComponent = EnsureRainFx())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		const float Delta = LastRainApplyTime < 0.f ? WeatherPresenterLocal::SnowJumpStartSeconds : FMath::Max(0.f, Now - LastRainApplyTime);
		LastRainApplyTime = Now;
		FWeatherRainFxInput Input;
		Input.RainAmount = EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Snowfall);
		Input.StormAmount = EnvironmentRules::GetWeatherAmount(Config, State, EWeatherState::Blizzard);
		Input.RainIntensity = Rain;
		Input.PuddleAmount = State.PuddleAmount;
		Input.ScreenDrops = ScreenDrops;
		Input.WindStrength = FMath::Clamp(Frame.Preset.WindStrength, 0.f, 1.f);
		Input.WindDirection = LastWindDirection;
		Input.Seed = State.RandomSeed;
		RainComponent->ApplyRainFx(Input, Delta);
	}
}

void AWeatherPresenterActor::WriteRainParameters(float RainIntensity, float Wetness, float PuddleAmount, float IsRaining, float ClimateTemperate, float ScreenDrops)
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World && LoadedParameters ? World->GetParameterCollectionInstance(LoadedParameters) : nullptr;
	if (!Instance)
	{
		return;
	}
	// A missing parameter (an MPC from before section 17) is skipped by the engine; the others are still written.
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamRainIntensity, FMath::Clamp(RainIntensity, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamWetness, FMath::Clamp(Wetness, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamPuddleAmount, FMath::Clamp(PuddleAmount, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamIsRaining, FMath::Clamp(IsRaining, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamClimateTemperate, FMath::Clamp(ClimateTemperate, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamScreenDrops, FMath::Clamp(ScreenDrops, 0.f, 1.f));
}

void AWeatherPresenterActor::WriteParameters(float SnowAmount, float FrostAmount, float WindStrength, float IsSnowing, const FVector& WindDirection)
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World && LoadedParameters ? World->GetParameterCollectionInstance(LoadedParameters) : nullptr;
	if (!Instance)
	{
		return;
	}
	const float Wind = FMath::Clamp(WindStrength, 0.f, 1.f);
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamSnowAmount, FMath::Clamp(SnowAmount, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamFrostAmount, FMath::Clamp(FrostAmount, 0.f, 1.f));
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamWindStrength, Wind);
	Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamIsSnowing, FMath::Clamp(IsSnowing, 0.f, 1.f));
	Instance->SetVectorParameterValue(WeatherPresenterLocal::ParamWindDirection, FLinearColor(WindDirection.X, WindDirection.Y, WindDirection.Z, Wind));
}

void AWeatherPresenterActor::WriteObjectSnow(const FDirectionalSnow& Pockets)
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World && LoadedParameters ? World->GetParameterCollectionInstance(LoadedParameters) : nullptr;
	if (!Instance)
	{
		return;
	}
	for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
	{
		// A missing parameter (an MPC from before section 15) is skipped by the engine (returns false); the others are still written.
		Instance->SetScalarParameterValue(WeatherPresenterLocal::ParamObjectSnow[Index], FMath::Clamp(Pockets.Get(Index), 0.f, 1.f));
	}
}

void AWeatherPresenterActor::SetSnowVisualsEnabled(bool bEnabled)
{
	if (SnowFx)
	{
		SnowFx->SetVisualsEnabled(bEnabled);
	}
	if (SnowOverlay)
	{
		SnowOverlay->SetVisualsEnabled(bEnabled);
	}
	if (RainFx)
	{
		RainFx->SetVisualsEnabled(bEnabled);
	}
	if (!bEnabled)
	{
		// The authored look: no weather snow / frost / wind in the materials.
		WriteParameters(0.f, 0.f, 0.f, 0.f, LastWindDirection);
		WriteObjectSnow(FDirectionalSnow());
		// Section 17: no rain either (only ever written on a temperate level).
		if (bRainParamsWritten)
		{
			WriteRainParameters(0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			bRainParamsWritten = false;
		}
	}
}

void AWeatherPresenterActor::SetCloudsOff(bool bOff)
{
	if (bCloudsOff == bOff)
	{
		return;
	}
	bCloudsOff = bOff;
	for (const FWeatherCloudAuthored& Cloud : Clouds)
	{
		AVolumetricCloud* Actor = Cloud.Cloud.Get();
		if (!Actor)
		{
			continue;
		}
		UVolumetricCloudComponent* Component = Actor->FindComponentByClass<UVolumetricCloudComponent>();
		Actor->SetActorHiddenInGame(bOff ? true : Cloud.bHiddenInGame);
		if (Component)
		{
			Component->SetVisibility(bOff ? false : Cloud.bComponentVisible);
			if (bOff || !Cloud.bComponentActive)
			{
				Component->Deactivate();
			}
			else
			{
				Component->Activate();
			}
		}
	}
}

void AWeatherPresenterActor::RestoreAuthored()
{
	if (UDirectionalLightComponent* Light = bSunAdjustable ? SunComp.Get() : nullptr)
	{
		if (bSunMovable)
		{
			Light->SetWorldRotation(Authored.SunRotation);
		}
		Light->SetIntensity(Authored.SunIntensity);
		Light->SetLightColor(Authored.SunColor);
		Light->SetUseTemperature(Authored.bSunUseTemperature);
		Light->SetTemperature(Authored.SunTemperature);
		Light->SetLightSourceAngle(Authored.SunSourceAngle);
		Light->SetVisibility(Authored.bSunVisible);
	}
	if (AdoptedMoon)
	{
		UDirectionalLightComponent* Light = GetDirectionalComponent(AdoptedMoon);
		if (Light->Mobility == EComponentMobility::Movable)
		{
			Light->SetWorldRotation(Authored.MoonRotation);
		}
		if (Light->Mobility != EComponentMobility::Static)
		{
			Light->SetIntensity(Authored.MoonIntensity);
			Light->SetLightColor(Authored.MoonColor);
			Light->SetVisibility(Authored.bMoonVisible);
		}
	}
	MoonLight->SetVisibility(false);
	if (USkyAtmosphereComponent* Atmosphere = SkyAtmosphereComp.Get())
	{
		Atmosphere->SetRayleighScatteringScale(Authored.RayleighScatteringScale);
		Atmosphere->SetMieScatteringScale(Authored.MieScatteringScale);
	}
	if (USkyLightComponent* Light = bSkyLightAdjustable ? SkyLightComp.Get() : nullptr)
	{
		Light->SetIntensity(Authored.SkyLightIntensity);
	}
	if (UExponentialHeightFogComponent* Fog = FogComp.Get())
	{
		Fog->SetFogDensity(Authored.FogDensity);
		Fog->SetFogHeightFalloff(Authored.FogHeightFalloff);
		Fog->SetFogInscatteringColor(Authored.FogInscattering);
		Fog->SetStartDistance(Authored.FogStartDistance);
		Fog->SetFogMaxOpacity(Authored.FogMaxOpacity);
		Fog->SetVolumetricFog(Authored.bVolumetricFog);
		Fog->SetVolumetricFogExtinctionScale(Authored.VolumetricFogExtinctionScale);
	}
	if (AdoptedPostProcess)
	{
		AdoptedPostProcess->Settings = Authored.PostSettings;
	}
	PostProcess->bEnabled = false;
	SetCloudsOff(false);
	SetSnowVisualsEnabled(false);
	RecaptureSkyIfNeeded(LastFrame, true);
}

void AWeatherPresenterActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITOR
	// Section 16: a presenter PLACED in a level lights the editor viewport where the level has no lights (never in PIE / game).
	const UWorld* World = GetWorld();
	if (World && World->WorldType == EWorldType::Editor && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		ApplyEditorPreview();
	}
#endif
}

void AWeatherPresenterActor::ApplyEditorPreview()
{
	// Only the presenter's own components change: the level's lighting actors (and so its package) are never touched in the editor.
	UWorld* World = GetWorld();
	FLevelLightingInventory Inventory;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		Inventory.DirectionalLights += IsValid(*It) ? 1 : 0;
	}
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		Inventory.bSkyAtmosphere |= IsValid(*It);
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		Inventory.bSkyLight |= IsValid(*It);
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		Inventory.bHeightFog |= IsValid(*It);
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		Inventory.bUnboundPostProcess |= IsValid(*It) && It->bUnbound && It->bEnabled;
	}
	// Bind only own parts (the level's actors stay unbound here: nothing below may write them).
	Sun = nullptr;
	AdoptedMoon = nullptr;
	SkyAtmosphere = nullptr;
	SkyLight = nullptr;
	HeightFog = nullptr;
	AdoptedPostProcess = nullptr;
	Clouds.Reset();
	BindRig(Inventory, bBuildMissingLighting && bEditorPreview);
	MoonLight->SetVisibility(false);
	PostProcess->bEnabled = false;
	if (!bEditorPreview || RigPlan.Mode == EWeatherRigMode::Adopted)
	{
		return; // the level's own lights show their authored look
	}
	LoadPresets();
	const float Hour = FMath::Fmod(FMath::Max(PreviewHour, 0.f), 24.f);
	const FWeatherVisualFrame Frame = EnvironmentVisualRules::ComputeFrame(DayCurves, GetPreset(PreviewClimate, PreviewWeather),
		GetPreset(PreviewClimate, PreviewWeather), PreviewWeather, PreviewWeather, 0.f, Hour);
	ApplySun(Frame);
	if (RigPlan.bOwnMoon && Inventory.DirectionalLights == 0)
	{
		ApplyMoon(Frame);
	}
	ApplySky(Frame);
	ApplyFog(Frame);
	if (RigPlan.bOwnPostProcess)
	{
		PostProcess->bEnabled = true;
		ApplyPostProcess(Frame);
	}
	LastFrame = Frame;
}

void AWeatherPresenterActor::RecaptureSkyIfNeeded(const FWeatherVisualFrame& Frame, bool bForce)
{
	USkyLightComponent* Light = SkyLightComp.Get();
	if (!Light || Light->bRealTimeCapture || Light->Mobility == EComponentMobility::Static || !FApp::CanEverRender())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// One number that moves with the sun, the moon and the weather blend: recapture only when it moved noticeably.
	const float Signature = Frame.SunElevation + Frame.MoonElevation * (Frame.bMoonVisible ? 1.f : 0.f) + Frame.Blend * 30.f;
	const bool bChanged = FMath::Abs(Signature - LastRecaptureSunElevation) > 3.f || Frame.Weather != LastRecaptureWeather;
	if (!bForce && (!bChanged || Now - LastRecaptureTime < SkyRecaptureInterval))
	{
		return;
	}
	Light->RecaptureSky();
	LastRecaptureTime = Now;
	LastRecaptureSunElevation = Signature;
	LastRecaptureWeather = Frame.Weather;
}
