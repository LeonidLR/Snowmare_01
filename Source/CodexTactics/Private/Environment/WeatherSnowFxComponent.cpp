#include "Environment/WeatherSnowFxComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace WeatherSnowFx
{
	const TCHAR* DefaultSystemPath = TEXT("/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow.NS_Weather_Snow");

	// The contract User parameters (docs/systems/environment-weather.md §5).
	const FName ParamWeatherIntensity(TEXT("User.WeatherIntensity"));
	const FName ParamSpawnRate(TEXT("User.SpawnRate"));
	const FName ParamOpacity(TEXT("User.Opacity"));
	const FName ParamWindStrength(TEXT("User.WindStrength"));
	const FName ParamWindDirection(TEXT("User.WindDirection"));
	const FName ParamDriftOpacity(TEXT("User.DriftOpacity"));
	const FName ParamDriftSpriteSize(TEXT("User.DriftSpriteSize"));

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
}

void UWeatherSnowFxComponent::ApplySnowFx(float SnowIntensity, float InWindStrength, const FVector& InWindDirection, float DeltaSeconds)
{
	TargetIntensity = FMath::Clamp(SnowIntensity, 0.f, 1.f);
	WindStrength = FMath::Clamp(InWindStrength, 0.f, 1.f);
	if (!InWindDirection.IsNearlyZero())
	{
		WindDirection = InWindDirection;
	}
	CurrentIntensity = WeatherSnowFxRules::StepIntensity(CurrentIntensity, TargetIntensity, DeltaSeconds, Tuning);
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
	if (bEnabled)
	{
		Refresh();
	}
}

void UWeatherSnowFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Niagara && bVisualsEnabled && Niagara->IsActive())
	{
		UpdatePlacement();
	}
}

void UWeatherSnowFxComponent::RequestLoad()
{
	if (bLoadRequested || !WeatherSnowFx::CanShowSnow() || SnowSystem.IsNull())
	{
		return;
	}
	bLoadRequested = true;
	if (SnowSystem.IsValid())
	{
		return;
	}
	TWeakObjectPtr<UWeatherSnowFxComponent> WeakThis(this);
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(SnowSystem.ToSoftObjectPath(), FStreamableDelegate::CreateLambda([WeakThis]()
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
	// The asset spawns in a cylinder standing on the system (from the ground up past the highest camera); a strong wind moves it upwind
	// so the flakes blow across the view.
	const FWeatherSnowFxParams Params = WeatherSnowFxRules::ComputeParams(CurrentIntensity, WindStrength, WindDirection, Tuning);
	const FVector Offset = WeatherSnowFxRules::ComputeSpawnOffset(Params.WindDirection, Params.WindStrength, Tuning);
	Niagara->SetWorldLocationAndRotation(Focus + Offset, FRotator(0., CameraRotation.Yaw, 0.));
	return true;
}

void UWeatherSnowFxComponent::Refresh()
{
	if (!bVisualsEnabled || !WeatherSnowFx::CanShowSnow())
	{
		return;
	}
	const bool bWantSnow = CurrentIntensity >= Tuning.OffThreshold;
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
	const FWeatherSnowFxParams Params = WeatherSnowFxRules::ComputeParams(CurrentIntensity, WindStrength, WindDirection, Tuning);
	Fx->SetVariableFloat(WeatherSnowFx::ParamWeatherIntensity, Params.WeatherIntensity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamSpawnRate, Params.SpawnRate);
	Fx->SetVariableFloat(WeatherSnowFx::ParamOpacity, Params.Opacity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamWindStrength, Params.WindStrength);
	Fx->SetVariableVec3(WeatherSnowFx::ParamWindDirection, Params.WindDirection);
	Fx->SetVariableFloat(WeatherSnowFx::ParamDriftOpacity, Params.DriftOpacity);
	Fx->SetVariableFloat(WeatherSnowFx::ParamDriftSpriteSize, Params.DriftSpriteSize);
	if (bWantSnow && !Fx->IsActive())
	{
		UpdatePlacement();
		Fx->Activate(true);
	}
	else if (!bWantSnow && Fx->IsActive())
	{
		// Soft: no new flakes (spawn rate 0), the ones in the air land, then the system goes inactive.
		Fx->Deactivate();
	}
}
