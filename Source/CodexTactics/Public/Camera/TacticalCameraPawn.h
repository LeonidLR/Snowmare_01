#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/TacticalCameraRules.h"
#include "TacticalCameraPawn.generated.h"

class AOperativeCharacter;
class UCameraComponent;

/**
 * The player's view: perspective isometric camera that follows the squad leader.
 * Controls: WASD / screen edges pan, mouse wheel zooms, Q/E (or arrows) rotate 45 deg,
 * right-mouse drag rotates, middle-mouse drag pans. Exploration and combat keep separate zoom levels.
 * Runs on real (undilated) time so it keeps working during a tactical pause.
 * Godot reference: Scenes/movements/camera.gd (shake, smooth focus and dramatic shots come with combat).
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
	bool bPanReturning = false;
	FVector PanReturnStart = FVector::ZeroVector;
	float PanReturnTime = 0.f;
};
