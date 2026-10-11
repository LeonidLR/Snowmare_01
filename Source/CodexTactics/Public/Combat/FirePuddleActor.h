#pragma once

#include "CoreMinimal.h"
#include "Survival/TacticalHeatActor.h"
#include "FirePuddleActor.generated.h"

/**
 * Residual fire puddle (Sprint 15, TANDEM request #14 §2.5): burning napalm / the fuel trail of an exploded barrel. Two zones:
 * the core (R <= PuddleCoreRadius 1.2 m) hurts everybody standing in it with fire (EDamageType::Fire + Burning, pulses every
 * PuddleDamageIntervalSeconds); the radiant ring (up to PuddleHeatRadius 3.5 m) is a UHeatSourceComponent with its own warming
 * rate PuddleWarmthRate (12 %/s) and thaws frozen weapons like any heat source, without damage. Lives 12-15 s (1 turn-based
 * round), then the embers die and it disappears.
 * Sources: ABarrelActor::Explode (FTacticalHeatConfig::bBarrelLeavesPuddle); SpawnAt is the hook for a future flamethrower.
 * UE-only; no Godot reference.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AFirePuddleActor : public ATacticalHeatActor
{
	GENERATED_BODY()

public:
	AFirePuddleActor();

	/** Lights a puddle on the ground at GroundPoint (lifetime rolled between the min and max). SourceName goes into the damage source. */
	static AFirePuddleActor* SpawnAt(UWorld* World, const FVector& GroundPoint, const FString& SourceName,
		TSubclassOf<AFirePuddleActor> Class = nullptr);

	/** Starts burning for Lifetime seconds with the current tuning. */
	void LightPuddle(float Lifetime);

	virtual void RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound) override;

	/** Hurts whoever stands in the core now (one pulse); returns how many were hit. */
	int32 ApplyCorePulse();

	/** Damage source of the pulses ("Fire puddle", "Barrel fire"...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat")
	FString SourceName = TEXT("Fire puddle");

protected:
	virtual void HandleAdvanced(float DeltaSeconds) override;

private:
	void ApplyTuning();

	float PulseClock = 0.f;
};
