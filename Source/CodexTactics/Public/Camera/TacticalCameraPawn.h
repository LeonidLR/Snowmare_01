#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraShakeRules.h"
#include "Camera/TacticalCameraRules.h"
#include "TacticalCameraPawn.generated.h"

class AOperativeCharacter;
class UCameraComponent;

/**
 * The player's view: perspective isometric camera that follows the squad leader.
 * Controls: WASD / screen edges pan, mouse wheel zooms, Q/E (or arrows) rotate 45 deg,
 * right-mouse drag rotates, middle-mouse drag pans. Exploration and combat keep separate zoom levels.
 * Runs on real (undilated) time so it keeps working during a tactical pause.
 * Turn-based shots shake the view (trauma, Godot trigger_weapon_shake).
 * Godot reference: Scenes/movements/camera.gd.
 */
UCLASS()
class CODEXTACTICS_API ATacticalCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ATacticalCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Actor the camera follows; null keeps the camera in place. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void SetFollowTarget(AActor* NewTarget);

	AActor* GetFollowTarget() const { return FollowTarget.Get(); }

	/** Mouse wheel: positive notches zoom out, negative zoom in. */
	void AddZoomNotches(float Notches);

	/** Q/E: rotates by Config.RotationStep times Direction (+1 / -1). */
	void RotateStep(int32 Direction);

	/** Right-mouse drag rotation on/off. */
	void SetDragRotating(bool bActive);

	/** Middle-mouse drag pan on/off; releasing in exploration returns the view to the leader. */
	void SetDragPanning(bool bActive);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	FTacticalCameraConfig Config;

	/**
	 * Godot trigger_weapon_shake: a turn-based shot adds trauma by weapon type («rifle», «pistol», «turret»); outside
	 * turn-based combat nothing happens.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void TriggerWeaponShake(const FString& WeaponType);

	/** Current shake trauma 0..1. */
	float GetShakeTrauma() const { return ShakeTrauma; }

	/** Shake tuning (DA_GameBalanceConfig camera_shake_* at BeginPlay). */
	FCameraShakeConfig ShakeConfig;

private:
	UFUNCTION()
	void HandleLeaderChanged(AOperativeCharacter* NewLeader);

	/** True once the mission left pure exploration (Godot: is_combat_phase / is_combat_phase_unlocked). */
	bool IsCombatView() const;
	bool IsTurnBased() const;
	void UpdateZoomMode();
	void UpdatePan(float RealDelta);
	void UpdateRotation(float RealDelta);
	FVector ComputeFocus(float RealDelta);
	/** Advances the trauma and returns this frame's view-plane offset, cm (Godot _process_shake h / v offsets). */
	FVector UpdateShake(float RealDelta, const FRotator& ViewRotation);

	float ShakeTrauma = 0.f;
	float ShakeNoiseTime = 0.f;
	FVector ShakeOffset = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	TWeakObjectPtr<AActor> FollowTarget;
	FVector Focus = FVector::ZeroVector;
	bool bInitialized = false;

	float CurrentYaw = 0.f;
	float TargetYaw = 0.f;

	float CurrentDistance = 0.f;
	float TargetDistance = 0.f;
	float UserDistanceExploration = 0.f;
	float UserDistanceCombat = 0.f;
	bool bCombatView = false;

	FVector PanOffset = FVector::ZeroVector;
	FVector TargetPanOffset = FVector::ZeroVector;
	bool bDragRotating = false;
	bool bDragPanning = false;
	/** Cursor position of the previous drag frame, viewport pixels (Godot event.relative is in pixels). */
	FVector2D LastDragCursor = FVector2D::ZeroVector;
	bool bHasLastDragCursor = false;

	/** Cursor movement since the previous drag frame, pixels (Y down). */
	FVector2D ConsumeCursorDelta();
	bool bPanReturning = false;
	FVector PanReturnStart = FVector::ZeroVector;
	float PanReturnTime = 0.f;
};
