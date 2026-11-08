#pragma once

#include "CoreMinimal.h"
#include "Combat/ProjectileActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "ProjectileSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnProjectileFinished, const FProjectileImpact& /*Impact*/);

/**
 * The pool of flying rounds of the shared projectile layer (the weapons framework, 2026-11-09; docs/design/WEAPONS_FRAMEWORK.md 16; new - no
 * Godot reference). Fire / FireAt take a parked AProjectileActor (or spawn one), finished rounds come back parked; OnProjectileFinished reports
 * every flight's end (the impact, the blast's victims) - the RPG-7, the mortar nests and ProjectileSmoke listen to it.
 */
UCLASS()
class CODEXTACTICS_API UProjectileSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Launches Launch (its velocity as given). Null without a world. */
	AProjectileActor* Fire(const FProjectileLaunch& Launch);

	/**
	 * Launches at Target with the launch the weapon's trajectory gives (ProjectileRules::SolveLaunch at Launch.Projectile.SpeedCmS). False (and
	 * no shot) when the target is out of reach; OutRound = the round.
	 */
	bool FireAt(FProjectileLaunch Launch, const FVector& Target, AProjectileActor** OutRound = nullptr);

	/** A round's flight ended (AProjectileActor::Finish): it goes back to the pool. */
	void Release(AProjectileActor* Round, const FProjectileImpact& Impact);

	/** The gravity the world's rounds fly under (cm/s^2, negative). */
	float GetWorldGravityZ() const;

	int32 GetActiveCount() const { return Active.Num(); }
	int32 GetPooledCount() const { return Pool.Num(); }

	FOnProjectileFinished OnProjectileFinished;

	virtual void Deinitialize() override;

private:
	UPROPERTY()
	TArray<TObjectPtr<AProjectileActor>> Pool;

	UPROPERTY()
	TArray<TObjectPtr<AProjectileActor>> Active;
};
