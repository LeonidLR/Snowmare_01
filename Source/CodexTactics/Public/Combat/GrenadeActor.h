#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrenadeActor.generated.h"

class AOperativeCharacter;
class UStaticMeshComponent;

/**
 * Hand grenade in flight: waits for the release point of the throw animation in the thrower's hand, flies a predictable
 * arc to the landing point, then explodes after the fuse: operatives / enemies / other damageable objects in the radius
 * take the damage with a falloff to 50 % at the edge (operatives 65 %), fuel barrels explode, trapped objects go off.
 * A Blueprint subclass can replace the mesh and add VFX / sound in ReceiveLanded / ReceiveDetonated.
 * Godot reference: Scenes/weapons/grenade.gd (set_throw_delay, throw_to, _process, _land, detonate, cancel_and_refund,
 * _apply_area_effect).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AGrenadeActor : public AActor
{
	GENERATED_BODY()

public:
	AGrenadeActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Takes damage / radius / range from the thrower (Godot grenade_damage, grenade_effect_radius, grenade_throw_range). */
	void ConfigureFrom(AOperativeCharacter* InThrower, float ReleaseDelay);

	/** Starts the flight; the landing point is clamped to the thrower's effective range. Returns it. */
	FVector ThrowTo(const FVector& WorldTarget);

	/** Explodes now (after the fuse). */
	void Detonate();

	/**
	 * Godot cancel_and_refund (entering turn-based combat): the grenade disappears and goes back to the thrower.
	 * False if it already exploded.
	 */
	bool CancelAndRefund();

	bool IsFlying() const { return bFlying; }
	/** The operative who threw it (null once he is gone). */
	AOperativeCharacter* GetThrower() const { return Thrower.Get(); }
	bool HasLanded() const { return bLanded; }
	FVector GetLandingPoint() const { return TargetLocation; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Grenade", meta = (ClampMin = "0"))
	float Damage = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Grenade", meta = (ClampMin = "0"))
	float EffectRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Grenade", meta = (ClampMin = "0"))
	float ThrowRange = 1200.f;

	/** Seconds after landing (Godot fuse_time 1.2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Grenade", meta = (ClampMin = "0"))
	float FuseTime = 1.2f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Grenade")
	TObjectPtr<UStaticMeshComponent> Mesh;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Grenade", meta = (DisplayName = "On Landed"))
	void ReceiveLanded(const FVector& Location);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Grenade", meta = (DisplayName = "On Detonated"))
	void ReceiveDetonated(const FVector& Location);

private:
	void Land();
	void ApplyAreaEffect();

	TWeakObjectPtr<AOperativeCharacter> Thrower;
	FVector StartLocation = FVector::ZeroVector;
	FVector TargetLocation = FVector::ZeroVector;
	float ReleaseDelay = 0.f;
	float FlightElapsed = 0.f;
	float FlightDuration = 0.f;
	bool bFlying = false;
	bool bLanded = false;
	bool bDetonated = false;
	FTimerHandle FuseHandle;
};
