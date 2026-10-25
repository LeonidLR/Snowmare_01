#include "Environment/WeatherSnowFxComponent.h"

#include "Camera/CameraZoneVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentStartRules.h"
#include "Environment/SnowFxSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"

namespace WeatherSnowFx
{
	const TCHAR* DefaultSystemPath = TEXT("/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow.NS_Weather_Snow");
	const TCHAR* DefaultFlakeMaterialPath = TEXT("/Game/Environment/Weather/Snow/Niagara/MI_Weather_Snowflake.MI_Weather_Snowflake");

	// The contract User parameters (docs/systems/environment-weather.md §5).
	const FName ParamWeatherIntensity(TEXT("User.WeatherIntensity"));
	const FName ParamSpawnRate(TEXT("User.SpawnRate"));
	const FName ParamOpacity(TEXT("User.Opacity"));
	const FName ParamWindStrength(TEXT("User.WindStrength"));
	const FName ParamWindDirection(TEXT("User.WindDirection"));
	const FName ParamDriftOpacity(TEXT("User.DriftOpacity"));
	const FName ParamDriftSpriteSize(TEXT("User.DriftSpriteSize"));
	// The look (DA_SnowFX, §12.b).
	const FName ParamFlakeSizeMin(TEXT("User.FlakeSizeMin"));
	const FName ParamFlakeSizeMax(TEXT("User.FlakeSizeMax"));
	const FName ParamFlakeOpacityLow(TEXT("User.Min_Snow_Opacity"));
	const FName ParamLifetimeMin(TEXT("User.FlakeLifetimeMin"));
	const FName ParamLifetimeMax(TEXT("User.FlakeLifetimeMax"));
	const FName ParamGravity(TEXT("User.FlakeGravity"));
	const FName ParamSpeedLimit(TEXT("User.SpeedLimit"));
	const FName ParamFlakeWindSpeed(TEXT("User.FlakeWindSpeed"));
	const FName ParamSpawnRadius(TEXT("User.SpawnRadius"));
	const FName ParamSpawnHeight(TEXT("User.SpawnHeight"));
	const FName ParamDriftRate(TEXT("User.DriftRate"));
	const FName ParamDriftOpacityLow(TEXT("User.Min_SnowSmoke_Opacity"));
	const FName ParamDriftWindSpeed(TEXT("User.DriftWindSpeed"));
	const FName ParamDriftRadius(TEXT("User.DriftRadius"));
	const FName ParamDriftHeight(TEXT("User.DriftHeight"));
	const FName ParamFlakeMaterial(TEXT("User.FlakeMaterial"));

	FName KillBoxOriginParam(int32 Index)
	{
		return FName(*FString::Printf(TEXT("User.KillBox%dOrigin"), Index));
	}

	FName KillBoxSizeParam(int32 Index)
	{
		return FName(*FString::Printf(TEXT("User.KillBox%dSize"), Index));
	}

	/** Seconds between two kill box gathers (zones + roof traces) while the snow runs. */
	constexpr double KillBoxInterval = 0.5;

	static TAutoConsoleVariable<int32> CVarSnowIndoorKill(TEXT("Codex.Env.SnowIndoorKill"), 1,
		TEXT("Falling snow: 1 = no flakes / drift inside Closed / Shelter camera zones and under roofs (Niagara kill boxes, DA_SnowFX.bNoSnowIndoors), 0 = off (debug)."));

	/** Without a renderer (-nullrhi smokes, dedicated server) nothing is ever loaded or spawned. */
	bool CanShowSnow()
	{
		return FApp::CanEverRender();
	}
}

UWeatherSnowFxComponent::UWeatherSnowFxComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the camera manager updated the view this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SnowSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(WeatherSnowFx::DefaultSystemPath));
	Settings = TSoftObjectPtr<USnowFxSettings>(FSoftObjectPath(USnowFxSettings::DefaultAssetPath));
	FlakeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowFx::DefaultFlakeMaterialPath));
}

const FWeatherSnowFxTuning& UWeatherSnowFxComponent::GetActiveTuning() const
{
	if (TuningOverride.IsSet())
	{
		return TuningOverride.GetValue();
	}
	const USnowFxSettings* Loaded = Settings.Get();
	return Loaded ? Loaded->Tuning : Tuning;
}

bool UWeatherSnowFxComponent::IsUsingSettingsAsset() const
{
	return Settings.Get() != nullptr;
}

bool UWeatherSnowFxComponent::IsSnowActive() const
{
	return Niagara && Niagara->IsActive();
}

void UWeatherSnowFxComponent::SetHiddenForCapture(bool bHidden)
{
	if (Niagara)
	{
		Niagara->SetHiddenInGame(bHidden);
	}
}

void UWeatherSnowFxComponent::SetTuningOverride(const TOptional<FWeatherSnowFxTuning>& Override)
{
	TuningOverride = Override;
	Refresh();
}

void UWeatherSnowFxComponent::ApplySnowFx(float SnowIntensity, float InWindStrength, const FVector& InWindDirection, float DeltaSeconds)
{
	TargetIntensity = FMath::Clamp(SnowIntensity, 0.f, 1.f);
	WindStrength = FMath::Clamp(InWindStrength, 0.f, 1.f);
	if (!InWindDirection.IsNearlyZero())
	{
		WindDirection = InWindDirection;
	}
	CurrentIntensity = WeatherSnowFxRules::StepIntensity(CurrentIntensity, TargetIntensity, DeltaSeconds, GetActiveTuning());
	Refresh();
}

void UWeatherSnowFxComponent::SetVisualsEnabled(bool bEnabled)
{
	if (bVisualsEnabled == bEnabled)
	{
		return;
	}
	bVisualsEnabled = bEnabled;
	if (Niagara)
	{
		if (!bEnabled)
		{
			Niagara->DeactivateImmediate();
		}
		Niagara->SetVisibility(bEnabled);
	}
	if (!bEnabled)
	{
		bActivateWithWarmup = false;
		bWarmupWhenActive = false;
	}
	if (bEnabled)
	{
		Refresh();
	}
}

void UWeatherSnowFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bActivateWithWarmup)
	{
		bActivateWithWarmup = false;
		if (Niagara && bVisualsEnabled)
		{
			ActivateWarmedUp();
		}
	}
	else if (bWarmupWhenActive && Niagara && bVisualsEnabled && Niagara->IsActive())
	{
		bWarmupWhenActive = false;
		UpdatePlacement();
		RunWarmup();
	}
	if (Niagara && bVisualsEnabled && Niagara->IsActive())
	{
		UpdatePlacement();
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.;
		if (Now >= NextKillBoxUpdate)
		{
			NextKillBoxUpdate = Now + WeatherSnowFx::KillBoxInterval;
			UpdateKillBoxes();
		}
	}
}

void UWeatherSnowFxComponent::RequestStartWarmup()
{
	if (!WeatherSnowFx::CanShowSnow())
	{
		return;
	}
	bStartWarmupPending = true;
	// The level start must not wait for the async load (the first frame would show no snow): load what is missing now.
	if (!SnowSystem.IsNull() && !SnowSystem.Get())
	{
		SnowSystem.LoadSynchronous();
	}
	if (!Settings.IsNull() && !Settings.Get())
	{
		Settings.LoadSynchronous();
	}
	if (!FlakeMaterial.IsNull() && !FlakeMaterial.Get())
	{
		FlakeMaterial.LoadSynchronous();
	}
}

void UWeatherSnowFxComponent::ActivateWarmedUp()
{
	UpdatePlacement();
	UpdateKillBoxes();
#if WITH_EDITORONLY_DATA
	// Editor builds: a system still compiling would defer the activation by frames; the level start waits for it instead.
	Niagara->bWaitForCompilationOnActivate = true;
#endif
	Niagara->Activate(true);
#if WITH_EDITORONLY_DATA
	Niagara->bWaitForCompilationOnActivate = false;
#endif
	if (!Niagara->IsActive())
	{
		// Niagara deferred it (system / PSOs not ready): the warm-up runs on the first tick the system is active, still full at once.
		bWarmupWhenActive = true;
		UE_LOG(LogCodexTactics, Log, TEXT("WeatherSnowFx: the activation is deferred by Niagara (not ready); the start warm-up runs when it starts"));
		return;
	}
	RunWarmup();
}

void UWeatherSnowFxComponent::RunWarmup()
{
	const FWeatherSnowFxTuning& Active = GetActiveTuning();
	LastWarmupSeconds = EnvironmentStartRules::GetSnowWarmupSeconds(Active);
	LastWarmupTicks = EnvironmentStartRules::GetSnowWarmupTicks(LastWarmupSeconds, Active.StartWarmupTickSeconds);
	if (LastWarmupTicks > 0)
	{
		// The flakes and the ground drift of the last LastWarmupSeconds, simulated now: the first rendered frame shows a full snowfall.
		Niagara->AdvanceSimulation(LastWarmupTicks, LastWarmupSeconds / LastWarmupTicks);
	}
	++WarmupCount;
	UE_LOG(LogCodexTactics, Log, TEXT("WeatherSnowFx: start warm-up %.1f s (%d ticks) at intensity %.2f, focus %s, engine frame %llu: %s"), LastWarmupSeconds,
		LastWarmupTicks, CurrentIntensity, *LastFocus.ToCompactString(), static_cast<uint64>(GFrameCounter), *DescribeParticles());
}

FString UWeatherSnowFxComponent::DescribeParticles() const
{
	const FNiagaraSystemInstanceControllerConstPtr Controller = Niagara ? Niagara->GetSystemInstanceController() : nullptr;
	const FNiagaraSystemInstance* Instance = Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
	if (!Instance)
	{
		return TEXT("no system instance");
	}
	FString Out;
	for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
	{
		Out += FString::Printf(TEXT("%s%s %d"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *Emitter->GetEmitterHandle().GetName().ToString(), Emitter->GetNumParticles());
	}
	return Out.IsEmpty() ? FString(TEXT("no emitters")) : Out;
}

int32 UWeatherSnowFxComponent::GetParticleCount() const
{
	const FNiagaraSystemInstanceControllerConstPtr Controller = Niagara ? Niagara->GetSystemInstanceController() : nullptr;
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

void UWeatherSnowFxComponent::RequestLoad()
{
	if (bLoadRequested || !WeatherSnowFx::CanShowSnow() || SnowSystem.IsNull())
	{
		return;
	}
	bLoadRequested = true;
	TArray<FSoftObjectPath> Paths;
	for (const FSoftObjectPath& Path : { SnowSystem.ToSoftObjectPath(), Settings.ToSoftObjectPath(), FlakeMaterial.ToSoftObjectPath() })
	{
		if (!Path.IsNull() && !Path.ResolveObject())
		{
			Paths.Add(Path);
		}
	}
	if (Paths.IsEmpty())
	{
		return;
	}
	TWeakObjectPtr<UWeatherSnowFxComponent> WeakThis(this);
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([WeakThis]()
	{
		if (UWeatherSnowFxComponent* Self = WeakThis.Get())
		{
			if (!Self->SnowSystem.IsValid())
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("WeatherSnowFx: %s did not load; no falling snow."), *Self->SnowSystem.ToString());
			}
			Self->Refresh();
		}
	}));
}

UNiagaraComponent* UWeatherSnowFxComponent::EnsureNiagara()
{
	if (Niagara)
	{
		return Niagara;
	}
	RequestLoad();
	UNiagaraSystem* System = SnowSystem.Get();
	AActor* Owner = GetOwner();
	if (!System || !Owner || !WeatherSnowFx::CanShowSnow())
	{
		return nullptr;
	}
	Niagara = NewObject<UNiagaraComponent>(Owner, TEXT("WeatherSnowNiagara"));
	Niagara->SetAsset(System);
	Niagara->SetAutoActivate(false);
	Niagara->SetAutoDestroy(false);
	Niagara->SetUsingAbsoluteLocation(true);
	Niagara->SetUsingAbsoluteRotation(true);
	Niagara->SetUsingAbsoluteScale(true);
	Niagara->SetCastShadow(false);
	Niagara->SetupAttachment(this);
	Niagara->RegisterComponent();
	return Niagara;
}

bool UWeatherSnowFxComponent::UpdatePlacement()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Niagara || !CameraManager)
	{
		return false;
	}
	const FVector CameraLocation = CameraManager->GetCameraLocation();
	const FRotator CameraRotation = CameraManager->GetCameraRotation();
	FVector Focus;
	if (const ATacticalCameraPawn* Tactical = Cast<ATacticalCameraPawn>(CameraManager->GetViewTarget()))
	{
		Focus = Tactical->GetViewCentre();
	}
	else
	{
		// Any other view (cinematic camera actor): the ground point under the view ray, else a plane 15 m below the camera.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WeatherSnowFxFocus), false, CameraManager->GetViewTarget());
		const FVector Forward = CameraRotation.Vector();
		Focus = World->LineTraceSingleByChannel(Hit, CameraLocation, CameraLocation + Forward * 20000., ECC_Visibility, Params)
			? Hit.ImpactPoint
			: WeatherSnowFxRules::ComputeViewFocus(CameraLocation, Forward, CameraLocation.Z - 1500., 2500.);
	}
	LastFocus = Focus;
	bHasFocus = true;
	// The asset spawns in a cylinder standing on the system (from the ground up past the highest camera); a strong wind moves it upwind
	// so the flakes blow across the view. No rotation: the kill boxes are world axis-aligned in the system's space.
	const FWeatherSnowFxTuning& Active = GetActiveTuning();
	const FWeatherSnowFxParams Params = WeatherSnowFxRules::ComputeParams(CurrentIntensity, WindStrength, WindDirection, Active);
	const FVector Offset = WeatherSnowFxRules::ComputeSpawnOffset(Params.WindDirection, Params.WindStrength, Active);
	Niagara->SetWorldLocationAndRotation(Focus + Offset, FRotator::ZeroRotator);
	return true;
}

void UWeatherSnowFxComponent::UpdateKillBoxes()
{
	UWorld* World = GetWorld();
	if (!Niagara || !World)
	{
		return;
	}
	const FWeatherSnowFxTuning& Active = GetActiveTuning();
	TArray<FBox> Candidates;
	if (Active.bNoSnowIndoors && WeatherSnowFx::CVarSnowIndoorKill.GetValueOnGameThread() != 0 && bHasFocus)
	{
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
		{
			if (It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter)
			{
				Candidates.Add(WeatherSnowFxRules::MakeZoneKillBox(It->GetZoneTransform(), It->GetZoneHalfExtent(), Active.IndoorMarginCm,
					Active.IndoorBelowCm));
			}
		}
		if (Active.bKillUnderRoofs)
		{
			TArray<FVector, TInlineAllocator<2>> Points;
			Points.Add(LastFocus);
			const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr)
			{
				Points.Add(Leader->GetActorLocation());
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WeatherSnowFxRoof), false);
			for (const FVector& Point : Points)
			{
				FHitResult Hit;
				const FVector From = Point + FVector(0., 0., 30.);
				if (World->LineTraceSingleByObjectType(Hit, From, From + FVector(0., 0., Active.RoofTraceHeight), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				{
					const UPrimitiveComponent* Roof = Hit.GetComponent();
					const FBox RoofBounds = Roof ? Roof->Bounds.GetBox() : FBox(ForceInit);
					if (WeatherSnowFxRules::IsRoofCandidate(RoofBounds, Hit.ImpactPoint.Z, Point.Z, Active))
					{
						Candidates.Add(WeatherSnowFxRules::MakeRoofKillBox(RoofBounds, Point.Z, Active.IndoorMarginCm, Active.IndoorBelowCm));
					}
				}
			}
		}
	}
	KillBoxes = WeatherSnowFxRules::SelectKillBoxes(Candidates, LastFocus, Active.IndoorSearchRadius, WeatherSnowFxRules::KillBoxCount);
	for (int32 Index = 0; Index < WeatherSnowFxRules::KillBoxCount; ++Index)
	{
		FVector Origin, Size;
		WeatherSnowFxRules::PackKillBox(KillBoxes.IsValidIndex(Index) ? KillBoxes[Index] : FBox(ForceInit), Origin, Size);
		Niagara->SetVariablePosition(WeatherSnowFx::KillBoxOriginParam(Index), Origin);
		Niagara->SetVariableVec3(WeatherSnowFx::KillBoxSizeParam(Index), Size);
	}
}

void UWeatherSnowFxComponent::WriteLook(UNiagaraComponent& Fx, const FWeatherSnowFxParams& Params, const FWeatherSnowFxTuning& Active)
{
	Fx.SetVariableFloat(WeatherSnowFx::ParamFlakeSizeMin, Params.FlakeSizeMin);
	Fx.SetVariableFloat(WeatherSnowFx::ParamFlakeSizeMax, Params.FlakeSizeMax);
	Fx.SetVariableFloat(WeatherSnowFx::ParamFlakeOpacityLow, Params.FlakeOpacityRandomMin);
	Fx.SetVariableFloat(WeatherSnowFx::ParamLifetimeMin, Params.LifetimeMin);
	Fx.SetVariableFloat(WeatherSnowFx::ParamLifetimeMax, Params.LifetimeMax);
	Fx.SetVariableVec3(WeatherSnowFx::ParamGravity, Params.Gravity);
	Fx.SetVariableFloat(WeatherSnowFx::ParamSpeedLimit, Params.SpeedLimit);
	Fx.SetVariableFloat(WeatherSnowFx::ParamFlakeWindSpeed, Params.FlakeWindSpeed);
	Fx.SetVariableFloat(WeatherSnowFx::ParamSpawnRadius, Params.SpawnRadius);
	Fx.SetVariableFloat(WeatherSnowFx::ParamSpawnHeight, Params.SpawnHeight);
	Fx.SetVariableFloat(WeatherSnowFx::ParamDriftRate, Params.DriftRate);
	Fx.SetVariableFloat(WeatherSnowFx::ParamDriftOpacityLow, Params.DriftOpacityRandomMin);
	Fx.SetVariableFloat(WeatherSnowFx::ParamDriftWindSpeed, Params.DriftWindSpeed);
	Fx.SetVariableFloat(WeatherSnowFx::ParamDriftRadius, Params.DriftRadius);
	Fx.SetVariableFloat(WeatherSnowFx::ParamDriftHeight, Params.DriftHeight);

	// The flake material's own parameters through a dynamic instance bound to User.FlakeMaterial.
	if (!FlakeMaterialInstance)
	{
		if (UMaterialInterface* Base = FlakeMaterial.Get())
		{
			FlakeMaterialInstance = UMaterialInstanceDynamic::Create(Base, this);
			Fx.SetVariableMaterial(WeatherSnowFx::ParamFlakeMaterial, FlakeMaterialInstance);
		}
	}
	if (FlakeMaterialInstance)
	{
		FlakeMaterialInstance->SetScalarParameterValue(TEXT("NearFadeStart"), Active.NearFadeStart);
		FlakeMaterialInstance->SetScalarParameterValue(TEXT("NearFadeLength"), FMath::Max(Active.NearFadeLength, 1.f));
		FlakeMaterialInstance->SetScalarParameterValue(TEXT("Softness"), FMath::Clamp(Active.FlakeSoftness, 0.f, 0.99f));
		FlakeMaterialInstance->SetScalarParameterValue(TEXT("Brightness"), FMath::Max(Active.FlakeBrightness, 0.f));
	}
}

void UWeatherSnowFxComponent::Refresh()
{
	if (!bVisualsEnabled || !WeatherSnowFx::CanShowSnow())
	{
		return;
	}
	const FWeatherSnowFxTuning& Active = GetActiveTuning();
	const bool bWantSnow = CurrentIntensity >= Active.OffThreshold;
	if (!bWantSnow)
	{
		// A start (or load) without snow: a later snowfall is a weather change and fades in (section 16).
		bStartWarmupPending = false;
		bActivateWithWarmup = false;
		bWarmupWhenActive = false;
	}
	if (!bWantSnow && !Niagara)
	{
		RequestLoad(); // warm up, so the first snowfall does not wait for the asset
		return;
	}
	UNiagaraComponent* Fx = EnsureNiagara();
	if (!Fx)
	{
		return;
	}
	if (!bLoggedSource)
	{
		bLoggedSource = true;
		UE_LOG(LogCodexTactics, Log, TEXT("WeatherSnowFx: look from %s"), IsUsingSettingsAsset() ? *Settings.ToString() : TEXT("the C++ defaults (DA_SnowFX not loaded)"));
	}
	const FWeatherSnowFxParams Params = WeatherSnowFxRules::ComputeParams(CurrentIntensity, WindStrength, WindDirection, Active);
	Fx->SetVariableFloat(WeatherSnowFx::ParamWeatherIntensity, Params.WeatherIntensity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamSpawnRate, Params.SpawnRate);
	Fx->SetVariableFloat(WeatherSnowFx::ParamOpacity, Params.Opacity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamWindStrength, Params.WindStrength);
	Fx->SetVariableVec3(WeatherSnowFx::ParamWindDirection, Params.WindDirection);
	Fx->SetVariableFloat(WeatherSnowFx::ParamDriftOpacity, Params.DriftOpacity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamDriftSpriteSize, Params.DriftSpriteSize);
	WriteLook(*Fx, Params, Active);
	if (bWantSnow && !Fx->IsActive())
	{
		if (bStartWarmupPending || bActivateWithWarmup)
		{
			// Level start / load: activated and warmed up in this frame's TickComponent, once the camera has its view (section 16).
			bStartWarmupPending = false;
			bActivateWithWarmup = true;
		}
		else
		{
			UpdatePlacement();
			UpdateKillBoxes();
			Fx->Activate(true);
		}
	}
	else if (bWantSnow)
	{
		// Already falling: a snap only jumps the intensity (no second warm-up over the flakes in the air).
		bStartWarmupPending = false;
	}
	else if (!bWantSnow && Fx->IsActive())
	{
		// Soft: no new flakes (spawn rate 0), the ones in the air land, then the system goes inactive.
		Fx->Deactivate();
	}
}
