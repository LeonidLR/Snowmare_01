// Copyright CodexTactics. UE-only (no Godot reference).
#include "AI/NoiseRingActor.h"
#include "AI/StealthLureRules.h"
#include "Combat/GrenadeSubsystem.h"
#include "Engine/World.h"

ANoiseRingActor::ANoiseRingActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetCanBeDamaged(false);
}

void ANoiseRingActor::Start(const FVector& InCenter, float InRadiusCm, float InSeconds, int32 InPulses)
{
	Center = InCenter;
	RadiusCm = FMath::Max(0.f, InRadiusCm);
	Seconds = FMath::Max(0.1f, InSeconds);
	Pulses = FMath::Max(1, InPulses);
	Elapsed = 0.f;
	if (!Ring)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ring = GetWorld()->SpawnActor<AGrenadeAimActor>(Center, FRotator::ZeroRotator, Params);
	}
	if (Ring)
	{
		Ring->SetLureStyle(true);
		Ring->SetActorHiddenInGame(false);
	}
	SetActorTickEnabled(true);
}

void ANoiseRingActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	const float Radius = StealthLureRules::NoisePulseRadius(Elapsed, Seconds, Pulses, RadiusCm);
	if (Radius < 0.f || !Ring)
	{
		Destroy();
		return;
	}
	Ring->ShowRing(Center, FMath::Max(20.f, Radius));
}

void ANoiseRingActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Ring)
	{
		Ring->Destroy();
		Ring = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
