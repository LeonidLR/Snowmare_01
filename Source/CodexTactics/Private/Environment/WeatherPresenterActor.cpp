#include "Environment/WeatherPresenterActor.h"

#include "CodexTactics.h"
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
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Environment/EnvironmentVisualSettings.h"
#include "Environment/WeatherPresetAsset.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "UI/MinimapSubsystem.h"

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

	const TCHAR* EnvironmentCollectionPath = TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment");
	const TCHAR* SnowSystemPath = TEXT("/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow.NS_Weather_Snow");
	const TCHAR* SnowOverlayPath = TEXT("/Game/Environment/Weather/Snow/Materials/M_SnowOverlay.M_SnowOverlay");

	// MPC_Environment parameter names (Scripts/Editor/create_environment_mpc.py).
	const FName ParamSnowAmount(TEXT("SnowAmount"));
	const FName ParamFrostAmount(TEXT("FrostAmount"));
	const FName ParamWindStrength(TEXT("WindStrength"));
	const FName ParamIsSnowing(TEXT("IsSnowing"));
	const FName ParamWindDirection(TEXT("WindDirection"));

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

	TSoftObjectPtr<UWeatherPresetAsset>* Refs[4] = { &PresetClear, &PresetOvercast, &PresetSnowfall, &PresetBlizzard };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		*Refs[Index] = TSoftObjectPtr<UWeatherPresetAsset>(FSoftObjectPath(WeatherPresenterLocal::PresetPaths[Index]));
		Presets[Index] = EnvironmentVisualRules::GetDefaultPreset(static_cast<EWeatherState>(Index));
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
	return Presets[FMath::Clamp(static_cast<int32>(Weather), 0, 3)];
}

bool AWeatherPresenterActor::IsPresetFromAsset(EWeatherState Weather) const
{
	return bPresetFromAsset[FMath::Clamp(static_cast<int32>(Weather), 0, 3)];
}

UDirectionalLightComponent* AWeatherPresenterActor::GetActiveMoon() const
{
	return AdoptedMoon ? GetDirectionalComponent(AdoptedMoon) : MoonLight.Get();
}

FString AWeatherPresenterActor::DescribeAdopted() const
{
	const UDirectionalLightComponent* SunLight = Sun ? GetDirectionalComponent(Sun) : nullptr;
	const USkyLightComponent* Sky = SkyLight ? SkyLight->GetLightComponent() : nullptr;
	int32 AssetCount = 0;
	for (const bool bAsset : bPresetFromAsset)
	{
		AssetCount += bAsset ? 1 : 0;
	}
	return FString::Printf(TEXT("sun %s%s, moon %s, sky atmosphere %s, sky light %s%s, height fog %s, post-process %s, volumetric clouds %d%s, presets %d/4 from assets"),
		*WeatherPresenterLocal::NameOf(Sun), SunLight ? *FString::Printf(TEXT(" (%s)"), *WeatherPresenterLocal::MobilityName(SunLight->Mobility)) : TEXT(""),
		AdoptedMoon ? *AdoptedMoon->GetName() : TEXT("own"), *WeatherPresenterLocal::NameOf(SkyAtmosphere), *WeatherPresenterLocal::NameOf(SkyLight),
		Sky ? (Sky->bRealTimeCapture ? TEXT(" (real-time capture)") : TEXT(" (recaptured)")) : TEXT(""), *WeatherPresenterLocal::NameOf(HeightFog),
		AdoptedPostProcess ? *AdoptedPostProcess->GetName() : TEXT("own"), Clouds.Num(), bCloudsOff ? TEXT(" off") : TEXT(""), AssetCount);
}

void AWeatherPresenterActor::BeginPlay()
{
	Super::BeginPlay();
	StartTime = GetWorld()->GetTimeSeconds();
	LoadPresets();
	if (WeatherPresenterLocal::PackageExists(EnvironmentParameters.ToSoftObjectPath()))
	{
		LoadedParameters = EnvironmentParameters.LoadSynchronous();
	}
	if (!LoadedParameters)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] %s missing: MPC_Environment is not written"), *EnvironmentParameters.ToString());
	}
	// Nothing of the snow shows until the presenter starts applying (minimap hold, visuals toggle).
	SetSnowVisualsEnabled(false);
	Adopt();
	if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(this))
	{
		StateSetHandle = Environment->OnStateSet.AddUObject(this, &AWeatherPresenterActor::HandleStateSet);
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
	const TSoftObjectPtr<UWeatherPresetAsset>* Refs[4] = { &PresetClear, &PresetOvercast, &PresetSnowfall, &PresetBlizzard };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const EWeatherState Weather = static_cast<EWeatherState>(Index);
		Presets[Index] = EnvironmentVisualRules::GetDefaultPreset(Weather);
		bPresetFromAsset[Index] = false;
		if (!WeatherPresenterLocal::PackageExists(Refs[Index]->ToSoftObjectPath()))
		{
			continue;
		}
		if (const UWeatherPresetAsset* Asset = Refs[Index]->LoadSynchronous())
		{
			Presets[Index] = Asset->Visual;
			bPresetFromAsset[Index] = true;
			if (Asset->Weather != Weather)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[EnvVisuals] preset %s says %s but is used for %s"), *Asset->GetName(),
					*EnvironmentRules::WeatherToString(Asset->Weather), *EnvironmentRules::WeatherToString(Weather));
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

	// Record the authored values.
	if (Sun)
	{
		const UDirectionalLightComponent* Light = GetDirectionalComponent(Sun);
		Authored.SunRotation = Light->GetComponentRotation();
		Authored.SunIntensity = Light->Intensity;
		Authored.SunColor = Light->GetLightColor();
		Authored.bSunUseTemperature = Light->bUseTemperature != 0;
		Authored.SunTemperature = Light->Temperature;
		Authored.SunSourceAngle = Light->LightSourceAngle;
		Authored.bSunVisible = Light->IsVisible();
		bSunMovable = Light->Mobility == EComponentMobility::Movable;
		bSunAdjustable = Light->Mobility != EComponentMobility::Static;
		if (!bSunMovable)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[EnvVisuals] the sun %s is %s: it is not rotated by the hour%s (make it Movable for the day cycle)"),
				*Sun->GetName(), *WeatherPresenterLocal::MobilityName(Light->Mobility), bSunAdjustable ? TEXT("") : TEXT(" nor re-lit"));
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
	if (SkyAtmosphere)
	{
		const USkyAtmosphereComponent* Atmosphere = SkyAtmosphere->GetComponent();
		Authored.RayleighScatteringScale = Atmosphere->RayleighScatteringScale;
		Authored.MieScatteringScale = Atmosphere->MieScatteringScale;
	}
	if (SkyLight)
	{
		const USkyLightComponent* Light = SkyLight->GetLightComponent();
		Authored.SkyLightIntensity = Light->Intensity;
		bSkyLightAdjustable = Light->Mobility != EComponentMobility::Static;
	}
	if (HeightFog)
	{
		const UExponentialHeightFogComponent* Fog = HeightFog->GetComponent();
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
		const UDirectionalLightComponent* SunLight = Sun ? GetDirectionalComponent(Sun) : nullptr;
		const bool bSunInAtmosphere = SunLight && SunLight->IsUsedAsAtmosphereSunLight();
		MoonLight->SetAtmosphereSunLight(true);
		MoonLight->SetAtmosphereSunLightIndex(bSunInAtmosphere && SunLight->GetAtmosphereSunLightIndex() == 0 ? 1 : 0);
	}
	bAdopted = true;
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] adopted: %s"), *DescribeAdopted());
	UE_LOG(LogCodexTactics, Log, TEXT("[EnvVisuals] authored: sun %.2f %s pitch %.1f, sky light %.2f, rayleigh %.2f mie %.2f, fog %.4f falloff %.3f luminance %s volumetric %d"),
		Authored.SunIntensity, *Authored.SunColor.ToString(), Authored.SunRotation.Pitch, Authored.SkyLightIntensity, Authored.RayleighScatteringScale,
		Authored.MieScatteringScale, Authored.FogDensity, Authored.FogHeightFalloff, *Authored.FogInscattering.ToString(), Authored.bVolumetricFog ? 1 : 0);
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

bool AWeatherPresenterActor::IsMinimapHoldOver() const
{
	const UWorld* World = GetWorld();
	if (!World || StartTime < 0.f || World->GetTimeSeconds() - StartTime >= MinimapHoldSeconds)
	{
		return true;
	}
	const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
	if (!Minimap)
	{
		return true;
	}
	if (!Minimap->IsReady())
	{
		return false;
	}
	for (int32 Floor = 0; Floor < Minimap->GetFloorCount(); ++Floor)
	{
		const FMinimapFloorCapture* Capture = Minimap->GetCaptureInfo(Floor);
		if (Capture && Capture->PassesLeft > 0)
		{
			return false;
		}
	}
	return true;
}

void AWeatherPresenterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAdopted)
	{
		return;
	}
	const bool bWant = WantsToApply();
	if (bWant && !bApplying)
	{
		if (IsMinimapHoldOver())
		{
			StartApplying();
		}
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
		Apply();
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
	LastSnowApplyTime = -1.f;
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
	const FWeatherVisualFrame Frame = EnvironmentVisualRules::ComputeFrame(DayCurves, GetPreset(Environment->GetWeather()),
		GetPreset(Environment->GetNextWeather()), Environment->GetWeather(), Environment->GetNextWeather(), Environment->GetTransitionBlend(),
		Environment->GetHour());
	ApplySun(Frame);
	ApplyMoon(Frame);
	ApplySky(Frame);
	ApplyFog(Frame);
	ApplyPostProcess(Frame);
	ApplySnow(Frame, Environment->GetSnowAmount());
	RecaptureSkyIfNeeded(Frame, false);
	LastFrame = Frame;
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
	UDirectionalLightComponent* Light = Sun ? GetDirectionalComponent(Sun) : nullptr;
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
	Light->SetIntensity((Sun ? Authored.SunIntensity : 10.f) * Frame.MoonIntensityScale);
	Light->SetLightColor(AdoptedMoon ? Authored.MoonColor : DayCurves.MoonColor);
	Light->SetVisibility(Frame.bMoonVisible);
}

void AWeatherPresenterActor::ApplySky(const FWeatherVisualFrame& Frame)
{
	if (USkyAtmosphereComponent* Atmosphere = SkyAtmosphere ? SkyAtmosphere->GetComponent() : nullptr)
	{
		Atmosphere->SetRayleighScatteringScale(Authored.RayleighScatteringScale * Frame.Preset.RayleighScatteringScale);
		Atmosphere->SetMieScatteringScale(Authored.MieScatteringScale * Frame.Preset.MieScatteringScale);
	}
	if (USkyLightComponent* Light = SkyLight && bSkyLightAdjustable ? SkyLight->GetLightComponent() : nullptr)
	{
		Light->SetIntensity(Authored.SkyLightIntensity * Frame.SkyLightIntensityScale);
	}
}

void AWeatherPresenterActor::ApplyFog(const FWeatherVisualFrame& Frame)
{
	UExponentialHeightFogComponent* Fog = HeightFog ? HeightFog->GetComponent() : nullptr;
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
	Settings.AutoExposureBias = Base.AutoExposureBias + Frame.ExposureBias;
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
	const float Delta = LastSnowApplyTime < 0.f ? 0.f : FMath::Max(0.f, Now - LastSnowApplyTime);
	LastSnowApplyTime = Now;
	const FWeatherVisualPreset& P = Frame.Preset;
	LastWindDirection = ComputeWindDirection(Now, WindBaseYaw, WindDriftDegrees, WindDriftPeriodSeconds);
	if (SnowFx)
	{
		SnowFx->ApplySnowFx(FMath::Clamp(P.SnowIntensity, 0.f, 1.f), FMath::Clamp(P.WindStrength, 0.f, 1.f), LastWindDirection, Delta);
	}
	if (SnowOverlay)
	{
		SnowOverlay->ApplySnowCover(FMath::Clamp(SnowAmount, 0.f, 1.f), FMath::Clamp(P.FrostAmount, 0.f, 1.f));
	}
	WriteParameters(SnowAmount, P.FrostAmount, P.WindStrength, ComputeIsSnowing(P.SnowIntensity), LastWindDirection);
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
	if (!bEnabled)
	{
		// The authored look: no weather snow / frost / wind in the materials.
		WriteParameters(0.f, 0.f, 0.f, 0.f, LastWindDirection);
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
	if (UDirectionalLightComponent* Light = Sun && bSunAdjustable ? GetDirectionalComponent(Sun) : nullptr)
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
	if (USkyAtmosphereComponent* Atmosphere = SkyAtmosphere ? SkyAtmosphere->GetComponent() : nullptr)
	{
		Atmosphere->SetRayleighScatteringScale(Authored.RayleighScatteringScale);
		Atmosphere->SetMieScatteringScale(Authored.MieScatteringScale);
	}
	if (USkyLightComponent* Light = SkyLight && bSkyLightAdjustable ? SkyLight->GetLightComponent() : nullptr)
	{
		Light->SetIntensity(Authored.SkyLightIntensity);
	}
	if (UExponentialHeightFogComponent* Fog = HeightFog ? HeightFog->GetComponent() : nullptr)
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

void AWeatherPresenterActor::RecaptureSkyIfNeeded(const FWeatherVisualFrame& Frame, bool bForce)
{
	USkyLightComponent* Light = SkyLight ? SkyLight->GetLightComponent() : nullptr;
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
