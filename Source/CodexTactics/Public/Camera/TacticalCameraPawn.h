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

	/** Next tick places the camera on the follow target at once (no smoothing, pan cleared): after a save is loaded. */
	void SnapToFollowTarget();

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

	// --- Turn-based choreography (Godot camera.gd smooth_focus_on_target / _position, dramatic_action_cam_focus) ---

	/**
	 * Glides focus (and, with TargetDistance > 0, the distance, cm) to Target over Duration with a smoothstep; the pan
	 * offset returns to zero. The camera then follows Target.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void SmoothFocusOnTarget(AActor* Target, float Duration = 0.85f, float TargetDistance = -1.f);

	/** Same glide to a world point (the follow target is kept for afterwards). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void SmoothFocusOnPosition(const FVector& WorldPosition, float Duration = 0.85f, float TargetDistance = -1.f);

	/** Frames shooter and target together: their midpoint at TacticalCameraRules::ComputeDramaticDistance. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void DramaticActionFocus(const AActor* From, const AActor* To, float Duration = 0.4f);

	/** While a dramatic shot runs the camera holds its framing (Godot main.gd is_dramatic_shot_active). */
	void SetDramaticShotActive(bool bActive) { bDramaticShot = bActive; }

	/** Entering turn-based combat zooms to Distance (Godot 14 m); leaving restores the previous zoom. */
	void EnterTurnBasedZoom(float Distance);
	void ExitTurnBasedZoom();

	bool IsSmoothFocusing() const { return bSmoothFocusing; }
	float GetCurrentDistance() const { return CurrentDistance; }
	float GetTargetDistance() const { return TargetDistance; }
	FVector GetFocus() const { return Focus; }

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

	/** Advances a running smooth focus (Godot _update_smooth_focus). */
	void UpdateSmoothFocus(float RealDelta);
	void BeginSmoothFocus(float Duration, float TargetDist);
	bool IsFollowingLeader() const;

	bool bSmoothFocusing = false;
	float SmoothTime = 0.f;
	float SmoothDuration = 0.85f;
	FVector SmoothStartFocus = FVector::ZeroVector;
	FVector SmoothStartPan = FVector::ZeroVector;
	float SmoothStartDistance = 0.f;
	float SmoothTargetDistance = -1.f;
	bool bSmoothToPosition = false;
	FVector SmoothPosition = FVector::ZeroVector;
	bool bDramaticShot = false;
	bool bTurnBasedZoom = false;
	float PreTurnBasedDistance = 0.f;

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
