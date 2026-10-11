#pragma once

#include "CoreMinimal.h"
#include "Survival/TacticalHeatActor.h"
#include "SignalFlareActor.generated.h"

class AOperativeCharacter;

/**
 * Signal flare (Sprint 15, TANDEM request #14 §2.4): thrown from the grenade slot along the grenade arc (FlareThrowRange 20 m
 * standing, 15 m crouched) or lit at the thrower's feet. Once down it burns FlareBurnSeconds (2 turn-based rounds): a small warm
 * zone (FlareHeatRadius 1.8 m), a bright magnesium light that shows every enemy within FlareRevealRadius (10 m) to the squad
 * (UTacticalSightSubsystem), and an open flame the fire-fearing beasts (hounds, cutters, frostbitten) flee from — the existing
 * fire fear of every active heat source. Spent, it disappears after SpentLifetime.
 * UE-only; no Godot reference.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ASignalFlareActor : public ATacticalHeatActor
{
	GENERATED_BODY()

public:
	ASignalFlareActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Flies from the thrower's hand to Target (released after ReleaseDelay, the throw clip's release point) and ignites on landing.
	 * The flight runs on world time (it slows down in the tactical pause like a grenade).
	 */
	void Throw(AOperativeCharacter* InThrower, const FVector& Target, float ReleaseDelay);

	/** Lights the flare where it is (at the thrower's feet, or a load). */
	void LightNow();

	bool IsFlying() const { return bFlying; }
	FVector GetLandingPoint() const { return TargetLocation; }

	/** A flare still in the air is saved as burning at its landing point. */
	virtual FVector GetSaveLocation() const override { return bFlying ? TargetLocation : GetActorLocation(); }
	virtual float GetSaveTimeLeft() const override;
	virtual bool GetSaveBurning() const override { return bFlying || IsBurning(); }
	virtual float GetSaveRoundSeconds() const override;
	virtual void RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound) override;

	/** Spawns a flare lit on the ground at GroundPoint. */
	static ASignalFlareActor* SpawnLit(UWorld* World, const FVector& GroundPoint, TSubclassOf<ASignalFlareActor> Class = nullptr);

private:
	void Land();

	TWeakObjectPtr<AOperativeCharacter> Thrower;
	FVector StartLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	float ReleaseLeft = 0.f;
	float FlightElapsed = 0.f;
	float FlightDuration = 0.f;
	bool bFlying = false;
};
