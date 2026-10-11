#include "Combat/SignalFlareActor.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/GrenadeRules.h"
#include "Components/PointLightComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/World.h"
#include "Interactables/HeatSourceComponent.h"
#include "NiagaraSystem.h"
#include "Survival/TacticalHeatTimerComponent.h"

ASignalFlareActor::ASignalFlareActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Placeholder: a 30 cm red tube lying on the ground; the magnesium light is pink-white and very bright.
	SetPlaceholderShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0.07f, 0.07f, 0.3f), FVector(0.f, 0.f, 15.f));
	BurningColor = FLinearColor(1.f, 0.15f, 0.12f);
	SpentColor = FLinearColor(0.12f, 0.04f, 0.04f);
	Light->SetLightColor(FLinearColor(1.f, 0.42f, 0.38f));
	Light->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	LightIntensity = 400.f;
	FadeSeconds = 5.f;
	SpentLifetime = 4.f;
	EffectAsset = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/VFX/Heat/NS_SignalFlare.NS_SignalFlare")));
	const FTacticalHeatConfig Defaults;
	HeatSource->Radius = Defaults.FlareHeatRadius;
	Light->SetAttenuationRadius(Defaults.FlareRevealRadius);
	Tags.Add(TEXT("SignalFlare"));
}

ASignalFlareActor* ASignalFlareActor::SpawnLit(UWorld* World, const FVector& GroundPoint, TSubclassOf<ASignalFlareActor> Class)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASignalFlareActor* Flare = World->SpawnActor<ASignalFlareActor>(Class ? *Class : ASignalFlareActor::StaticClass(), GroundPoint,
		FRotator::ZeroRotator, Params);
	if (Flare)
	{
		Flare->LightNow();
	}
	return Flare;
}

void ASignalFlareActor::Throw(AOperativeCharacter* InThrower, const FVector& Target, float ReleaseDelay)
{
	Thrower = InThrower;
	StartLocation = GetActorLocation();
	TargetLocation = Target;
	ReleaseLeft = FMath::Max(ReleaseDelay, 0.f);
	FlightDuration = GrenadeRules::FlightDuration(FVector::Dist2D(StartLocation, TargetLocation));
	FlightElapsed = 0.f;
	bFlying = true;
	SetActorHiddenInGame(ReleaseLeft > 0.f);
	SetActorTickEnabled(true);
}

void ASignalFlareActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFlying)
	{
		SetActorTickEnabled(false);
		return;
	}
	if (ReleaseLeft > 0.f)
	{
		// In the thrower's hand until the release point of the throw clip (like a grenade).
		ReleaseLeft -= DeltaSeconds;
		if (const AOperativeCharacter* ThrowerActor = Thrower.Get())
		{
			StartLocation = ThrowerActor->GetActorLocation() + FVector(0.f, 0.f, GrenadeRules::HandHeight);
			SetActorLocation(StartLocation);
		}
		if (ReleaseLeft <= 0.f)
		{
			SetActorHiddenInGame(false);
		}
		return;
	}
	FlightElapsed += DeltaSeconds;
	const float Progress = FlightDuration > 0.f ? FMath::Clamp(FlightElapsed / FlightDuration, 0.f, 1.f) : 1.f;
	SetActorLocation(GrenadeRules::ArcPoint(StartLocation, TargetLocation, Progress));
	if (Progress >= 1.f)
	{
		Land();
	}
}

void ASignalFlareActor::Land()
{
	bFlying = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(false);
	SetActorLocation(TargetLocation);
	LightNow();
}

void ASignalFlareActor::LightNow()
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	HeatSource->Radius = Config.FlareHeatRadius;
	HeatSource->Tolerance = Config.HeatTolerance;
	Light->SetAttenuationRadius(Config.FlareRevealRadius);
	Ignite(Config.FlareBurnSeconds, TacticalHeatRules::SecondsPerRound(Config.FlareBurnSeconds, Config.FlareTurnRounds));
	UE_LOG(LogCodexTactics, Display, TEXT("[Heat] signal flare burning at %s (%.0f s, heat %.0f cm, light %.0f cm)"),
		*GetActorLocation().ToCompactString(), Config.FlareBurnSeconds, Config.FlareHeatRadius, Config.FlareRevealRadius);
}

float ASignalFlareActor::GetSaveTimeLeft() const
{
	return bFlying ? TacticalHeatTuning::Get().FlareBurnSeconds : GetTimeLeft();
}

float ASignalFlareActor::GetSaveRoundSeconds() const
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	return bFlying ? TacticalHeatRules::SecondsPerRound(Config.FlareBurnSeconds, Config.FlareTurnRounds) : Super::GetSaveRoundSeconds();
}

void ASignalFlareActor::RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound)
{
	const FTacticalHeatConfig& Config = TacticalHeatTuning::Get();
	HeatSource->Radius = Config.FlareHeatRadius;
	HeatSource->Tolerance = Config.HeatTolerance;
	Light->SetAttenuationRadius(Config.FlareRevealRadius);
	bFlying = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(false);
	Super::RestoreHeatState(bBurning, bBurntOut, TimeLeft, SecondsPerRound);
}
