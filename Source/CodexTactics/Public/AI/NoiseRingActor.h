// Copyright CodexTactics. UE-only (no Godot reference): the sound spread indicator of a loud action (designer 2026-11-22).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoiseRingActor.generated.h"

class AGrenadeAimActor;

/**
 * Sound spread indicator (designer 2026-11-22: "when a window breaks, show an indicator of how the sound they make spreads"): rings on the
 * ground grow from the noise point to its hearing radius (StealthLureRules::NoisePulseRadius: Pulses waves over Seconds) and the actor goes.
 * Drawn with the lure-style ring of AGrenadeAimActor. Spawned by UStealthLureSubsystem::ShowNoiseRing.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ANoiseRingActor : public AActor
{
	GENERATED_BODY()

public:
	ANoiseRingActor();

	/** Starts the waves at Center out to RadiusCm. */
	void Start(const FVector& Center, float RadiusCm, float Seconds, int32 Pulses);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AGrenadeAimActor> Ring;

	FVector Center = FVector::ZeroVector;
	float RadiusCm = 0.f;
	float Seconds = 1.6f;
	int32 Pulses = 3;
	float Elapsed = 0.f;
};
