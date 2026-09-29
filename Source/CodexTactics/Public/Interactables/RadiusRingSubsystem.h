#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "RadiusRingSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Glowing ground ring (M_CombatFeedback segments) whose centre, radius and colour are set every frame. */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ARadiusRingActor : public AActor
{
	GENERATED_BODY()

public:
	ARadiusRingActor();

	void ShowRing(const FVector& Ground, float Radius, const FLinearColor& Color);

	float GetRadius() const { return ShownRadius; }
	FLinearColor GetColor() const { return ShownColor; }

private:
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Ring;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector ShownCenter = FVector(FLT_MAX);
	float ShownRadius = -1.f;
	FLinearColor ShownColor = FLinearColor::Transparent;
};

/** What the ring shows now. */
UENUM()
enum class ERadiusRingMode : uint8
{
	Hidden,
	/** Tactical pause: order radius around the leader's pause origin (green). */
	PauseOrders,
	/** Object relocation in the pause: the worker's radius (cyan valid / red invalid). */
	Relocation,
	/** Deployable placement in the pause: the worker's radius (green valid / red invalid). */
	Deploy
};

/**
 * The radius ring on the ground (Godot main.gd radius_ring: _update_radius_ring_position, _update_relocate_radius_ring,
 * _start_placement_mode, _set_ghost_material_valid): 12 m order radius in the tactical pause, the relocation / set-up
 * radius of the worker while placing in the pause; hidden otherwise.
 */
UCLASS()
class CODEXTACTICS_API URadiusRingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** The ring must follow the placement while the world is slowed / paused. */
	virtual bool IsTickableWhenPaused() const override { return true; }

	ERadiusRingMode GetMode() const { return Mode; }
	ARadiusRingActor* GetRing() const { return Ring; }

	/** Godot ring_color values. */
	static const FLinearColor Green;
	static const FLinearColor Cyan;
	static const FLinearColor Red;

private:
	ERadiusRingMode Mode = ERadiusRingMode::Hidden;

	UPROPERTY(Transient)
	TObjectPtr<ARadiusRingActor> Ring;
};
