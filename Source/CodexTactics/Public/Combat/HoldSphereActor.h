#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HoldSphereActor.generated.h"

class ARadiusRingActor;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * The Space-hold charge visual (Godot Scripts/tactics/tactical_hold_sphere.gd): a translucent green dome and a ground
 * ring grow with an eased progress up to 15 m, a fixed 15 m boundary ring marks the encounter reach; everything
 * brightens towards the end of the hold.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AHoldSphereActor : public AActor
{
	GENERATED_BODY()

public:
	AHoldSphereActor();

	/** Godot update_progress: hidden below 0.05 s, radius = 15 m x eased progress (p x (2 - p)). */
	void UpdateProgress(const FVector& Ground, float HoldTime, float TargetHoldDuration, float RadiusCap = 1500.f);

	/** Godot hide_sphere. */
	void HideSphere();

	float GetCurrentRadius() const { return CurrentRadius; }
	bool IsShown() const { return bShown; }

	/** Eased radius for Progress (0..1), cm. */
	static float EasedRadius(float Progress, float MaxRadius);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Dome;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<ARadiusRingActor> GrowingRing;

	UPROPERTY(Transient)
	TObjectPtr<ARadiusRingActor> BoundaryRing;

	float CurrentRadius = 0.f;
	bool bShown = false;
};
