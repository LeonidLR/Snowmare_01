#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GrenadeSubsystem.generated.h"

class AGrenadeActor;
class AOperativeCharacter;
class UInstancedStaticMeshComponent;

/**
 * Aim indicators of a grenade throw: the range ring around the thrower (by stance), the blast ring at the aim point and
 * the arc (glowing segments, M_CombatFeedback). Godot reference: main.gd _create_grenade_aim_indicators /
 * _update_grenade_aim_preview (range ring (1, 0.72, 0.15), AoE ring (1, 0.3, 0.15), arc (1, 0.82, 0.25)).
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AGrenadeAimActor : public AActor
{
	GENERATED_BODY()

public:
	AGrenadeAimActor();

	/** Draws the rings and the arc; Thrower is the ground point under the thrower. */
	void Show(const FVector& ThrowerGround, float MaxRange, const FVector& Target, float BlastRadius, const FVector& Hand);

	int32 GetSegmentCount() const;

private:
	void SetupMaterials();
	static void AddCircle(TArray<FTransform>& Out, const FVector& Center, float Radius, int32 Segments, float Width);

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RangeRing;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BlastRing;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Arc;

	bool bMaterialsReady = false;
};

/** Result of aiming at a point (Godot _get_grenade_aim_info). */
struct CODEXTACTICS_API FGrenadeAimInfo
{
	bool bValid = false;
	FVector Target = FVector::ZeroVector;
	float MaxRange = 1200.f;
	float Distance = 0.f;
	bool bInRange = true;
};

/**
 * Hand-grenade aiming and throwing outside turn-based combat (in turn-based combat the grenade is a grid weapon).
 * G / the selector's grenade line starts the aim mode for the leader; the cursor moves the preview; a click throws
 * (range clamped by stance), RMB / Esc cancels. Entering turn-based combat cancels the aim and refunds grenades in flight.
 * Godot reference: main.gd _start_grenade_throw_mode, _cancel_grenade_throw_mode, _get_grenade_aim_info,
 * _throw_grenade_at, _handle_grenade_throw_click, _enter_gorky17 grenade refund.
 */
UCLASS()
class CODEXTACTICS_API UGrenadeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** Godot _start_grenade_throw_mode: needs grenades; posts the stance range line. */
	bool StartAim(AOperativeCharacter* Thrower);

	/** Godot _cancel_grenade_throw_mode: RevertWeapon puts the rifle (pistol, knife) back in hands. */
	void CancelAim(bool bRevertWeapon = true);

	bool IsAiming() const { return Thrower.IsValid(); }
	AOperativeCharacter* GetThrower() const { return Thrower.Get(); }

	/** Godot _get_grenade_aim_info for a cursor point on the ground. */
	FGrenadeAimInfo GetAimInfo(const FVector& CursorPoint) const;

	/** Moves the preview to the cursor point (called every frame by the controller while aiming). */
	void UpdateAim(const FVector& CursorPoint);

	/** Godot _handle_grenade_throw_click. Returns the thrown grenade. */
	AGrenadeActor* ThrowAtCursor(const FVector& CursorPoint);

	/**
	 * Godot _throw_grenade_at: spawns the grenade in the thrower's hand, plays the throw (operative event), releases it
	 * at 70 % of the animation, spends one grenade (the rifle comes back when none are left). Null in turn-based combat.
	 */
	AGrenadeActor* ThrowAt(AOperativeCharacter* InThrower, const FVector& Target);

	/** Grenade class (a Blueprint subclass with the real mesh / VFX can replace it). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Grenade")
	TSubclassOf<AGrenadeActor> GrenadeClass;

	AGrenadeAimActor* GetAimActor() const { return AimActor; }

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void Post(const FText& Speaker, const FText& Text) const;
	static void TakeFirearm(AOperativeCharacter* Operative);

	TWeakObjectPtr<AOperativeCharacter> Thrower;

	UPROPERTY(Transient)
	TObjectPtr<AGrenadeAimActor> AimActor;
};
