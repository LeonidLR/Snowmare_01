#include "Environment/WeatherRainFxComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Environment/RainFxSettings.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"

namespace WeatherRainFx
{
	const TCHAR* DefaultRainSystemPath = TEXT("/Game/Environment/Weather/Rain/Niagara/NS_Weather_Rain.NS_Weather_Rain");
	const TCHAR* DefaultPuddleSystemPath = TEXT("/Game/Environment/Weather/Rain/Niagara/NS_Weather_PuddleSplashes.NS_Weather_PuddleSplashes");
	const TCHAR* DefaultDropMaterialPath = TEXT("/Game/Environment/Weather/Rain/Niagara/MI_Weather_Raindrop.MI_Weather_Raindrop");
	const TCHAR* DefaultScreenDropsPath = TEXT("/Game/Environment/Weather/Rain/PostProcess/PPMI_Weather_RainScreenDrops.PPMI_Weather_RainScreenDrops");

	// NS_Weather_Rain User parameters (Scripts/Editor/create_weather_rain_fx.py).
	const FName ParamIntensity(TEXT("User.WeatherIntensity"));
	const FName ParamSpawnRate(TEXT("User.SpawnRate"));
	const FName ParamDropColor(TEXT("User.DropColor"));
	const FName ParamDropSize(TEXT("User.DropSize"));
	const FName ParamDropSizeRandomMin(TEXT("User.DropSizeRandomMin"));
	const FName ParamDropGravity(TEXT("User.DropGravity"));
	const FName ParamFallSpeed(TEXT("User.FallSpeed"));
	const FName ParamDropLifetime(TEXT("User.DropLifetime"));
	const FName ParamDropWindVelocity(TEXT("User.DropWindVelocity"));
	const FName ParamSpawnRadius(TEXT("User.SpawnRadius"));
	const FName ParamSpawnHeight(TEXT("User.SpawnHeight"));
	const FName ParamSplashRate(TEXT("User.SplashRate"));
	const FName ParamSplashSizeMin(TEXT("User.SplashSizeMin"));
	const FName ParamSplashSizeMax(TEXT("User.SplashSizeMax"));
	const FName ParamSplashRadius(TEXT("User.SplashRadius"));
	const FName ParamSplashTint(TEXT("User.SplashTint"));
	const FName ParamDropMaterial(TEXT("User.DropMaterial"));
	// NS_Weather_PuddleSplashes.
	const FName ParamPuddleRate(TEXT("User.SpawnRate"));
	const FName ParamPuddleSizeMin(TEXT("User.SizeMin"));
	const FName ParamPuddleSizeMax(TEXT("User.SizeMax"));
	const FName ParamPuddleRadius(TEXT("User.Radius"));
	const FName ParamPuddleTint(TEXT("User.Tint"));

	FName KillBoxOriginParam(int32 Index)
	{
		return FName(*FString::Printf(TEXT("User.KillBox%dOrigin"), Index));
	}

	FName KillBoxSizeParam(int32 Index)
	{
		return FName(*FString::Printf(TEXT("User.KillBox%dSize"), Index));
	}

	constexpr double KillBoxInterval = 0.5;

	static TAutoConsoleVariable<int32> CVarRainIndoorKill(TEXT("Codex.Env.RainIndoorKill"), 1,
		TEXT("Rain (temperate levels): 1 = no drops inside Closed / Shelter camera zones and under roofs (Niagara kill boxes, DA_RainFX.bNoRainIndoors), 0 = off (debug)."));

	static TAutoConsoleVariable<int32> CVarLightning(TEXT("Codex.Env.Lightning"), -1,
		TEXT("Storm lightning flashes (temperate levels): -1 = auto (off on automated runs: -ExecCmds=CodexTactics.*, -CodexBot), 0 = off, 1 = on."));

	bool CanShowRain()
	{
		return FApp::CanEverRender();
	}

	/** A smoke / bot / capture run (the environment's neutral-start rule): no lightning by default there. Parsed once per process. */
	bool IsAutomatedRun()
	{
		static const bool bAutomated = []()
		{
			const TCHAR* CommandLine = FCommandLine::Get();
			FString ExecCmds;
			FParse::Value(CommandLine, TEXT("-ExecCmds="), ExecCmds, /*bShouldStopOnSeparator*/ false);
			return ExecCmds.Contains(TEXT("CodexTactics.")) || FParse::Param(CommandLine, TEXT("CodexBot"));
		}();
		return bAutomated;
	}

	bool PackageExists(const FSoftObjectPath& Path)
	{
		return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
	}

	int32 CountParticles(const UNiagaraComponent* Fx)
	{
		const FNiagaraSystemInstanceControllerConstPtr Controller = Fx ? Fx->GetSystemInstanceController() : nullptr;
		const FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		int32 Count = 0;
		if (Instance)
		{
			for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
			{
				Count += Emitter->GetNumParticles();
			}
		}
		return Count;
	}
}

UWeatherRainFxComponent::UWeatherRainFxComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the camera manager updated the view this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	RainSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(WeatherRainFx::DefaultRainSystemPath));
	PuddleSplashSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(WeatherRainFx::DefaultPuddleSystemPath));
	Settings = TSoftObjectPtr<URainFxSettings>(FSoftObjectPath(URainFxSettings::DefaultAssetPath));
	DropMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherRainFx::DefaultDropMaterialPath));
	ScreenDropsMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherRainFx::DefaultScreenDropsPath));
}

const FWeatherRainFxTuning& UWeatherRainFxComponent::GetActiveTuning() const
{
	if (TuningOverride.IsSet())
	{
		return TuningOverride.GetValue();
	}
	const URainFxSettings* Loaded = Settings.Get();
	return Loaded ? Loaded->Tuning : Tuning;
}

bool UWeatherRainFxComponent::IsUsingSettingsAsset() const
{
	return Settings.Get() != nullptr;
}

void UWeatherRainFxComponent::SetTuningOverride(const TOptional<FWeatherRainFxTuning>& Override)
{
	TuningOverride = Override;
	Refresh();
}

bool UWeatherRainFxComponent::IsRainActive() const
{
	return RainNiagara && RainNiagara->IsActive();
}

bool UWeatherRainFxComponent::ArePuddleSplashesActive() const
{
	return PuddleNiagara && PuddleNiagara->IsActive();
}

bool UWeatherRainFxComponent::AreScreenDropsOn() const
{
	return ScreenDropsPost && ScreenDropsPost->bEnabled;
}

int32 UWeatherRainFxComponent::GetParticleCount() const
{
	return WeatherRainFx::CountParticles(RainNiagara);
}

int32 UWeatherRainFxComponent::GetPuddleParticleCount() const
{
	return WeatherRainFx::CountParticles(PuddleNiagara);
}

FString UWeatherRainFxComponent::DescribeParticles() const
{
	FString Out;
	for (const UNiagaraComponent* Fx : { RainNiagara.Get(), PuddleNiagara.Get() })
	{
		const FNiagaraSystemInstanceControllerConstPtr Controller = Fx ? Fx->GetSystemInstanceController() : nullptr;
		const FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
		if (!Instance)
		{
			continue;
		}
		for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
		{
			Out += FString::Printf(TEXT("%s%s %d"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *Emitter->GetEmitterHandle().GetName().ToString(), Emitter->GetNumParticles());
		}
	}
	return Out.IsEmpty() ? FString(TEXT("no system instance")) : Out;
}

void UWeatherRainFxComponent::SetHiddenForCapture(bool bHidden)
{
	bHiddenForCapture = bHidden;
	for (UNiagaraComponent* Fx : { RainNiagara.Get(), PuddleNiagara.Get() })
	{
		if (Fx)
		{
			Fx->SetHiddenInGame(bHidden);
		}
	}
}

void UWeatherRainFxComponent::ApplyRainFx(const FWeatherRainFxInput& Input, float DeltaSeconds)
{
	LastInput = Input;
	const FWeatherRainFxTuning& Active = GetActiveTuning();
	TargetIntensity = WeatherRainFxRules::GetLookIntensity(Input.RainAmount, Input.StormAmount, Active);
	CurrentIntensity = WeatherRainFxRules::StepIntensity(CurrentIntensity, TargetIntensity, DeltaSeconds, Active);
	Refresh();
}

void UWeatherRainFxComponent::SetVisualsEnabled(bool bEnabled)
{
	if (bVisualsEnabled == bEnabled)
	{
		return;
	}
	bVisualsEnabled = bEnabled;
	for (UNiagaraComponent* Fx : { RainNiagara.Get(), PuddleNiagara.Get() })
	{
		if (Fx)
		{
			if (!bEnabled)
			{
				Fx->DeactivateImmediate();
			}
			Fx->SetVisibility(bEnabled);
		}
	}
	if (!bEnabled)
	{
		bActivateWithWarmup = false;
		if (ScreenDropsPost)
		{
			ScreenDropsPost->bEnabled = false;
		}
		bLightningRunning = false;
		StrikeTime = -1.;
		SetFlash(0.f, GetActiveTuning());
	}
	else
	{
		Refresh();
	}
}

void UWeatherRainFxComponent::RequestStartWarmup()
{
	if (!WeatherRainFx::CanShowRain())
	{
		return;
	}
	bStartWarmupPending = true;
	// The level start must not wait for the async load: load what is missing now.
	auto LoadNow = [](auto& Soft)
	{
		if (!Soft.IsNull() && !Soft.Get() && WeatherRainFx::PackageExists(Soft.ToSoftObjectPath()))
		{
			Soft.LoadSynchronous();
		}
	};
	LoadNow(RainSystem);
	LoadNow(PuddleSplashSystem);
	LoadNow(Settings);
	LoadNow(DropMaterial);
	LoadNow(ScreenDropsMaterial);
}

void UWeatherRainFxComponent::RequestLoad()
{
	if (bLoadRequested || !WeatherRainFx::CanShowRain())
	{
		return;
	}
	bLoadRequested = true;
	TArray<FSoftObjectPath> Paths;
	for (const FSoftObjectPath& Path : { RainSystem.ToSoftObjectPath(), PuddleSplashSystem.ToSoftObjectPath(), Settings.ToSoftObjectPath(),
			 DropMaterial.ToSoftObjectPath(), ScreenDropsMaterial.ToSoftObjectPath() })
	{
		// A missing package (assets not created yet) is skipped quietly: the rest of the rain still runs.
		if (WeatherRainFx::PackageExists(Path) && !Path.ResolveObject())
		{
			Paths.Add(Path);
		}
	}
	if (Paths.IsEmpty())
	{
		return;
	}
	TWeakObjectPtr<UWeatherRainFxComponent> WeakThis(this);
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([WeakThis]()
	{
		if (UWeatherRainFxComponent* Self = WeakThis.Get())
		{
			if (!Self->RainSystem.IsValid())
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("WeatherRainFx: %s did not load; no falling rain."), *Self->RainSystem.ToString());
			}
			Self->Refresh();
		}
	}));
}

UNiagaraComponent* UWeatherRainFxComponent::MakeNiagara(UNiagaraSystem* System, const TCHAR* Name)
{
	AActor* Owner = GetOwner();
	if (!System || !Owner)
	{
		return nullptr;
	}
	UNiagaraComponent* Fx = NewObject<UNiagaraComponent>(Owner, Name);
	Fx->SetAsset(System);
	Fx->SetAutoActivate(false);
	Fx->SetAutoDestroy(false);
	Fx->SetUsingAbsoluteLocation(true);
	Fx->SetUsingAbsoluteRotation(true);
	Fx->SetUsingAbsoluteScale(true);
	Fx->SetCastShadow(false);
	Fx->SetupAttachment(this);
	Fx->RegisterComponent();
	Fx->SetHiddenInGame(bHiddenForCapture);
	return Fx;
}

UNiagaraComponent* UWeatherRainFxComponent::EnsureRain()
{
	if (!RainNiagara)
	{
		RequestLoad();
		RainNiagara = WeatherRainFx::CanShowRain() ? MakeNiagara(RainSystem.Get(), TEXT("WeatherRainNiagara")) : nullptr;
	}
	return RainNiagara;
}

UNiagaraComponent* UWeatherRainFxComponent::EnsurePuddles()
{
	if (!PuddleNiagara)
	{
		RequestLoad();
		PuddleNiagara = WeatherRainFx::CanShowRain() ? MakeNiagara(PuddleSplashSystem.Get(), TEXT("WeatherPuddleSplashNiagara")) : nullptr;
	}
	return PuddleNiagara;
}

bool UWeatherRainFxComponent::UpdatePlacement()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!CameraManager || (!RainNiagara && !PuddleNiagara))
	{
		return false;
	}
	FVector Focus;
	if (const ATacticalCameraPawn* Tactical = Cast<ATacticalCameraPawn>(CameraManager->GetViewTarget()))
	{
		Focus = Tactical->GetViewCentre();
	}
	else
	{
		// Any other view (a capture / cinematic camera): the ground point under the view ray, else a plane 15 m below the camera.
		const FVector CameraLocation = CameraManager->GetCameraLocation();
		const FVector Forward = CameraManager->GetCameraRotation().Vector();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WeatherRainFxFocus), false, CameraManager->GetViewTarget());
		Focus = World->LineTraceSingleByChannel(Hit, CameraLocation, CameraLocation + Forward * 20000., ECC_Visibility, Params)
			? Hit.ImpactPoint
			: WeatherSnowFxRules::ComputeViewFocus(CameraLocation, Forward, CameraLocation.Z - 1500., 2500.);
	}
	LastFocus = Focus;
	bHasFocus = true;
	const FWeatherRainFxTuning& Active = GetActiveTuning();
	const FVector Offset = WeatherRainFxRules::ComputeSpawnOffset(LastParams.WindDirection, LastParams.WindStrength, Active);
	if (RainNiagara)
	{
		// No rotation: the kill boxes are world axis-aligned in the system's space.
		RainNiagara->SetWorldLocationAndRotation(Focus + Offset, FRotator::ZeroRotator);
	}
	if (PuddleNiagara)
	{
		PuddleNiagara->SetWorldLocationAndRotation(Focus, FRotator::ZeroRotator);
	}
	return true;
}

void UWeatherRainFxComponent::UpdateKillBoxes()
{
	UWorld* World = GetWorld();
	if (!RainNiagara || !World)
	{
		return;
	}
	const FWeatherRainFxTuning& Active = GetActiveTuning();
	// The falling snow's indoor rules (zones + roofs), with the rain's own switches / distances.
	const FWeatherSnowFxTuning Indoor = WeatherRainFxRules::ToIndoorTuning(Active);
	TArray<FBox> Candidates;
	if (Active.bNoRainIndoors && WeatherRainFx::CVarRainIndoorKill.GetValueOnGameThread() != 0 && bHasFocus)
	{
		Candidates = UWeatherSnowFxComponent::GatherIndoorKillBoxCandidates(*World, LastFocus, Indoor);
	}
	KillBoxes = WeatherSnowFxRules::SelectKillBoxes(Candidates, LastFocus, Indoor.IndoorSearchRadius, WeatherRainFxRules::KillBoxCount);
	for (int32 Index = 0; Index < WeatherRainFxRules::KillBoxCount; ++Index)
	{
		FVector Origin, Size;
		WeatherSnowFxRules::PackKillBox(KillBoxes.IsValidIndex(Index) ? KillBoxes[Index] : FBox(ForceInit), Origin, Size);
		RainNiagara->SetVariablePosition(WeatherRainFx::KillBoxOriginParam(Index), Origin);
		RainNiagara->SetVariableVec3(WeatherRainFx::KillBoxSizeParam(Index), Size);
	}
}

void UWeatherRainFxComponent::WriteRain(UNiagaraComponent& Fx, const FWeatherRainFxTuning& Active)
{
	const FWeatherRainFxParams& P = LastParams;
	Fx.SetVariableFloat(WeatherRainFx::ParamIntensity, P.Intensity);
	Fx.SetVariableFloat(WeatherRainFx::ParamSpawnRate, P.SpawnRate);
	// Colour = tint, alpha = opacity (the material multiplies a per-drop random between OpacityRandomMin and 1).
	Fx.SetVariableLinearColor(WeatherRainFx::ParamDropColor, FLinearColor(Active.DropColor.R, Active.DropColor.G, Active.DropColor.B, P.Opacity));
	Fx.SetVariableVec2(WeatherRainFx::ParamDropSize, FVector2D(P.DropWidthMax, P.DropLengthMax));
	Fx.SetVariableFloat(WeatherRainFx::ParamDropSizeRandomMin, FMath::Clamp(P.DropLengthMin / FMath::Max(P.DropLengthMax, 1.f), 0.05f, 1.f));
	Fx.SetVariableVec3(WeatherRainFx::ParamDropGravity, P.Gravity);
	Fx.SetVariableFloat(WeatherRainFx::ParamFallSpeed, P.FallSpeed);
	Fx.SetVariableFloat(WeatherRainFx::ParamDropLifetime, P.Lifetime);
	Fx.SetVariableVec3(WeatherRainFx::ParamDropWindVelocity, P.WindDirection * P.DropWindSpeed);
	Fx.SetVariableFloat(WeatherRainFx::ParamSpawnRadius, P.SpawnRadius);
	Fx.SetVariableFloat(WeatherRainFx::ParamSpawnHeight, P.SpawnHeight);
	Fx.SetVariableFloat(WeatherRainFx::ParamSplashRate, P.SplashRate);
	Fx.SetVariableFloat(WeatherRainFx::ParamSplashSizeMin, P.SplashSizeMin);
	Fx.SetVariableFloat(WeatherRainFx::ParamSplashSizeMax, P.SplashSizeMax);
	Fx.SetVariableFloat(WeatherRainFx::ParamSplashRadius, P.SplashRadius);
	Fx.SetVariableVec3(WeatherRainFx::ParamSplashTint, FVector(P.SplashOpacity));

	// The drop material's own parameters through a dynamic instance bound to User.DropMaterial.
	if (!DropMaterialInstance)
	{
		if (UMaterialInterface* Base = DropMaterial.Get())
		{
			DropMaterialInstance = UMaterialInstanceDynamic::Create(Base, this);
			Fx.SetVariableMaterial(WeatherRainFx::ParamDropMaterial, DropMaterialInstance);
		}
	}
	if (DropMaterialInstance)
	{
		DropMaterialInstance->SetScalarParameterValue(TEXT("NearFadeStart"), Active.NearFadeStart);
		DropMaterialInstance->SetScalarParameterValue(TEXT("NearFadeLength"), FMath::Max(Active.NearFadeLength, 1.f));
		DropMaterialInstance->SetScalarParameterValue(TEXT("Brightness"), FMath::Max(Active.DropBrightness, 0.f));
		DropMaterialInstance->SetScalarParameterValue(TEXT("OpacityRandomMin"), P.OpacityRandomMin);
	}
}

void UWeatherRainFxComponent::WritePuddles(UNiagaraComponent& Fx, const FWeatherRainFxTuning& Active)
{
	Fx.SetVariableFloat(WeatherRainFx::ParamPuddleRate, LastPuddleSplashRate);
	Fx.SetVariableFloat(WeatherRainFx::ParamPuddleSizeMin, FMath::Max(Active.PuddleSplashSizeMin, 1.f));
	Fx.SetVariableFloat(WeatherRainFx::ParamPuddleSizeMax, FMath::Max(Active.PuddleSplashSizeMax, FMath::Max(Active.PuddleSplashSizeMin, 1.f)));
	Fx.SetVariableFloat(WeatherRainFx::ParamPuddleRadius, FMath::Max(Active.PuddleSplashRadius, 100.f));
	Fx.SetVariableVec3(WeatherRainFx::ParamPuddleTint, FVector(FMath::Max(Active.PuddleSplashOpacity, 0.f)));
}

void UWeatherRainFxComponent::RefreshScreenDrops(const FWeatherRainFxTuning& Active)
{
	LastScreenDropsStrength = WeatherRainFxRules::ComputeScreenDropsStrength(LastInput.ScreenDrops, LastInput.RainIntensity, Active);
	const bool bWant = bVisualsEnabled && LastScreenDropsStrength > 0.001f;
	if (!bWant && !ScreenDropsPost)
	{
		return;
	}
	if (!ScreenDropsPost && GetOwner())
	{
		UMaterialInterface* Base = ScreenDropsMaterial.Get();
		if (!Base)
		{
			RequestLoad();
			return;
		}
		ScreenDropsInstance = UMaterialInstanceDynamic::Create(Base, this);
		ScreenDropsPost = NewObject<UPostProcessComponent>(GetOwner(), TEXT("RainScreenDropsPost"));
		ScreenDropsPost->bUnbound = true;
		ScreenDropsPost->Priority = 10.f;
		ScreenDropsPost->BlendWeight = 1.f;
		ScreenDropsPost->bEnabled = false;
		ScreenDropsPost->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, ScreenDropsInstance));
		ScreenDropsPost->SetupAttachment(this);
		ScreenDropsPost->RegisterComponent();
	}
	if (ScreenDropsInstance)
	{
		ScreenDropsInstance->SetScalarParameterValue(TEXT("Strength"), LastScreenDropsStrength);
		ScreenDropsInstance->SetScalarParameterValue(TEXT("RainDropsTiling"), FMath::Max(Active.ScreenDropsTiling, 0.01f));
		ScreenDropsInstance->SetScalarParameterValue(TEXT("AnimationSpeed"), FMath::Max(Active.ScreenDropsSpeed, 0.f));
	}
	if (ScreenDropsPost)
	{
		// Off = the component disabled: no post-process pass at all (zero cost when it does not rain).
		ScreenDropsPost->bEnabled = bWant;
	}
}

void UWeatherRainFxComponent::SetFlash(float Scale, const FWeatherRainFxTuning& Active)
{
	FlashScale = Scale;
	if (Scale <= 0.f && !FlashLight)
	{
		return;
	}
	if (!FlashLight && GetOwner() && WeatherRainFx::CanShowRain())
	{
		FlashLight = NewObject<UDirectionalLightComponent>(GetOwner(), TEXT("RainLightningFlash"));
		FlashLight->SetMobility(EComponentMobility::Movable);
		FlashLight->SetCastShadows(false);
		// Below the sun: the sun stays THE forward-shading light (translucency, fog); equal priorities would let the flash take over.
		FlashLight->ForwardShadingPriority = -1;
		FlashLight->bAtmosphereSunLight = false;
		FlashLight->bAffectsWorld = true;
		FlashLight->SetUsingAbsoluteRotation(true);
		FlashLight->SetWorldRotation(FRotator(-72.f, 120.f, 0.f));
		FlashLight->SetIntensity(0.f);
		FlashLight->SetVisibility(false);
		FlashLight->SetupAttachment(this);
		FlashLight->RegisterComponent();
	}
	if (FlashLight)
	{
		const bool bOn = Scale > 0.001f && bVisualsEnabled;
		FlashLight->SetLightColor(Active.LightningColor);
		FlashLight->SetIntensity(bOn ? Active.LightningFlashLux * Scale : 0.f);
		if (FlashLight->IsVisible() != bOn)
		{
			FlashLight->SetVisibility(bOn);
		}
	}
}

void UWeatherRainFxComponent::ForceStrike()
{
	UWorld* World = GetWorld();
	if (!World || !WeatherRainFx::CanShowRain())
	{
		return;
	}
	StrikeTime = World->GetTimeSeconds();
	++StrikeIndex;
	++StrikeCount;
}

void UWeatherRainFxComponent::UpdateLightning(const FWeatherRainFxTuning& Active)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const bool bSuppressed = WeatherRainFxRules::IsLightningSuppressed(WeatherRainFx::CVarLightning.GetValueOnGameThread(), WeatherRainFx::IsAutomatedRun());
	const bool bActive = bVisualsEnabled && WeatherRainFx::CanShowRain()
		&& WeatherRainFxRules::IsLightningActive(LastInput.StormAmount, bSuppressed, Active);
	if (bActive && !bLightningRunning)
	{
		// A storm (re)starts: the schedule is derived from the environment's RNG state of this moment (deterministic, never advancing it).
		bLightningRunning = true;
		LightningSeed = LastInput.Seed;
		StrikeIndex = 0;
		NextStrikeTime = Now + WeatherRainFxRules::ComputeStrikeDelay(LightningSeed, StrikeIndex, Active);
	}
	else if (!bActive)
	{
		bLightningRunning = false;
	}
	if (bLightningRunning && Now >= NextStrikeTime)
	{
		StrikeTime = Now;
		++StrikeCount;
		NextStrikeTime = Now + WeatherRainFxRules::ComputeStrikeDelay(LightningSeed, ++StrikeIndex, Active);
	}
	const float Scale = StrikeTime >= 0. ? WeatherRainFxRules::ComputeFlashScale(static_cast<float>(Now - StrikeTime), LightningSeed, StrikeIndex, Active) : 0.f;
	if (StrikeTime >= 0. && Now - StrikeTime > Active.LightningFlashSeconds)
	{
		StrikeTime = -1.;
	}
	if (Scale > 0.f || FlashScale > 0.f)
	{
		SetFlash(Scale, Active);
	}
}

void UWeatherRainFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bVisualsEnabled)
	{
		return;
	}
	if (bActivateWithWarmup)
	{
		bActivateWithWarmup = false;
		ActivateWarmedUp();
	}
	if (IsRainActive() || ArePuddleSplashesActive())
	{
		UpdatePlacement();
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.;
		if (Now >= NextKillBoxUpdate)
		{
			NextKillBoxUpdate = Now + WeatherRainFx::KillBoxInterval;
			UpdateKillBoxes();
		}
	}
	UpdateLightning(GetActiveTuning());
}

void UWeatherRainFxComponent::ActivateWarmedUp()
{
	if (!RainNiagara)
	{
		return;
	}
	UpdatePlacement();
	UpdateKillBoxes();
#if WITH_EDITORONLY_DATA
	RainNiagara->bWaitForCompilationOnActivate = true;
#endif
	RainNiagara->Activate(true);
#if WITH_EDITORONLY_DATA
	RainNiagara->bWaitForCompilationOnActivate = false;
#endif
	if (!RainNiagara->IsActive())
	{
		return; // Niagara deferred it: the rain fades in as it becomes ready (a short rain warm-up is not worth a second path)
	}
	const FWeatherRainFxTuning& Active = GetActiveTuning();
	const float Seconds = Active.StartWarmupSeconds > 0.f ? Active.StartWarmupSeconds : FMath::Max(Active.DropLifetime, 0.1f);
	const float Step = FMath::Clamp(Active.StartWarmupTickSeconds, 0.01f, 0.5f);
	const int32 Ticks = FMath::Clamp(FMath::CeilToInt(Seconds / Step), 1, 200);
	RainNiagara->AdvanceSimulation(Ticks, Seconds / Ticks);
	++WarmupCount;
	UE_LOG(LogCodexTactics, Log, TEXT("WeatherRainFx: start warm-up %.1f s (%d ticks) at intensity %.2f: %s"), Seconds, Ticks, CurrentIntensity, *DescribeParticles());
}

void UWeatherRainFxComponent::Refresh()
{
	if (!bVisualsEnabled || !WeatherRainFx::CanShowRain())
	{
		return;
	}
	const FWeatherRainFxTuning& Active = GetActiveTuning();
	LastParams = WeatherRainFxRules::ComputeParams(CurrentIntensity, LastInput.WindStrength, LastInput.WindDirection, Active);
	LastPuddleSplashRate = WeatherRainFxRules::ComputePuddleSplashRate(CurrentIntensity, LastInput.PuddleAmount, Active);
	RefreshScreenDrops(Active);
	const bool bWantRain = CurrentIntensity >= Active.OffThreshold;
	const bool bWantPuddles = LastPuddleSplashRate > 0.f;
	if (!bWantRain)
	{
		bStartWarmupPending = false;
		bActivateWithWarmup = false;
	}
	if (!bWantRain && !RainNiagara)
	{
		RequestLoad(); // so the first rain does not wait for the assets
		return;
	}
	if (!bLoggedSource && (RainNiagara || bWantRain))
	{
		bLoggedSource = true;
		UE_LOG(LogCodexTactics, Log, TEXT("WeatherRainFx: look from %s"), IsUsingSettingsAsset() ? *Settings.ToString() : TEXT("the C++ defaults (DA_RainFX not loaded)"));
	}
	if (UNiagaraComponent* Rain = EnsureRain())
	{
		WriteRain(*Rain, Active);
		if (bWantRain && !Rain->IsActive())
		{
			if (bStartWarmupPending)
			{
				bStartWarmupPending = false;
				bActivateWithWarmup = true; // activated and warmed up in this frame's tick, once the camera has its view
			}
			else
			{
				UpdatePlacement();
				UpdateKillBoxes();
				Rain->Activate(true);
			}
		}
		else if (bWantRain)
		{
			bStartWarmupPending = false;
		}
		else if (Rain->IsActive())
		{
			Rain->Deactivate(); // soft: the drops in the air land
		}
	}
	if (bWantPuddles || PuddleNiagara)
	{
		if (UNiagaraComponent* Puddles = EnsurePuddles())
		{
			WritePuddles(*Puddles, Active);
			if (bWantPuddles && !Puddles->IsActive())
			{
				UpdatePlacement();
				Puddles->Activate(true);
			}
			else if (!bWantPuddles && Puddles->IsActive())
			{
				Puddles->Deactivate();
			}
		}
	}
}

void UWeatherRainFxComponent::OnUnregister()
{
	if (ScreenDropsPost)
	{
		ScreenDropsPost->bEnabled = false;
	}
	if (FlashLight)
	{
		FlashLight->SetVisibility(false);
	}
	Super::OnUnregister();
}
