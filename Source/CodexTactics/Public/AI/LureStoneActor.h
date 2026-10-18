#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LureStoneActor.generated.h"

class AOperativeCharacter;
class UStaticMeshComponent;

/**
 * The thrown stone / bolt / spent cartridge of the stealth lure (user decisions 2026-10-18; UStealthLureSubsystem::ThrowAt).
 * Leaves the thrower's hand at the grenade clip's release point and flies the grenade arc (GrenadeRules::ArcPoint, world time:
 * it slows down with the world); on impact it makes the lure noise there (UStealthLureSubsystem::EmitNoise) - no explosion,
 * no damage, no collision. It then lies on the ground a while. Placeholder look: a small dark engine sphere (restyle in a
 * Blueprint subclass: UStealthLureSubsystem::StoneClass); a throw trail / impact puff are Gemini's (TANDEM request).
 * UE-only; no Godot reference.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ALureStoneActor : public AActor
{
	GENERATED_BODY()

public:
	ALureStoneActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Flies from the thrower's hand to Target after ReleaseDelay (the throw clip's release point); the noise at the impact. */
	void Throw(AOperativeCharacter* InThrower, const FVector& Target, float ReleaseDelay);

	bool IsFlying() const { return bFlying; }
	FVector GetLandingPoint() const { return TargetLocation; }
	AOperativeCharacter* GetThrower() const { return Thrower.Get(); }

	/** Blueprint hook on impact (a click sound / puff). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Stealth")
	void OnLanded(const FVector& ImpactPoint);

	/** Seconds the stone stays on the ground after the impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stealth", meta = (ClampMin = "0"))
	float GroundLifetime = 20.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Stealth")
	TObjectPtr<UStaticMeshComponent> Mesh;

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
