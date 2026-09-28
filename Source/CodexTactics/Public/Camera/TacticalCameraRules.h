#pragma once

#include "CoreMinimal.h"
#include "TacticalCameraRules.generated.h"

/**
 * Tactical camera tuning, in UE units (cm, deg, 1/s). Defaults mirror Godot Scenes/movements/camera.gd
 * and resources/balance.tres (camera_distance_* values) plus the scene camera in movements_demo.tscn
 * (perspective, vertical FOV 30 deg, isometric view: pitch -35.26 deg, yaw -45 deg, base distance 26.1 m).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTacticalCameraConfig
{
	GENERATED_BODY()

	/** Vertical field of view, degrees (Godot keeps height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View", meta = (ClampMin = "5", ClampMax = "120"))
	float VerticalFov = 30.f;

	/** Camera pitch, degrees (negative looks down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float Pitch = -35.26f;

	/** Initial camera yaw, degrees (45 deg = looking from top-left, matching Godot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View")
	float BaseYaw = 45.f;

	/** Distance from the focus in exploration, cm (Godot base distance: 26.1 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float DistanceExploration = 2600.f;

	/** Distance from the focus once combat is unlocked, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float DistanceCombat = 2800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float DistanceMin = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float DistanceMax = 4000.f;

	/** One wheel notch changes distance by this much, cm (Godot: 0.12 x base distance 26.1 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float ZoomStep = 313.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom", meta = (ClampMin = "0"))
	float ZoomSmoothSpeed = 6.f;

	/** Focus follow speed in real time, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow", meta = (ClampMin = "0"))
	float FollowSpeed = 6.f;

	/** Follow speed in turn-based combat, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow", meta = (ClampMin = "0"))
	float TacticalFollowSpeed = 2.5f;

	/** In turn-based combat the focus stays put until the leader moves this far, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow", meta = (ClampMin = "0"))
	float TacticalDeadzone = 300.f;

	/** Q/E rotation step, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	float RotationStep = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation", meta = (ClampMin = "0"))
	float RotationSmoothSpeed = 9.f;

	/** Right-mouse drag rotation, degrees per pixel (Godot 0.003 rad/px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation", meta = (ClampMin = "0"))
	float DragRotationSensitivity = 0.172f;

	/** WASD / edge-scroll pan speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float PanSpeed = 2000.f;

	/** Max pan away from the focus in exploration, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float MaxPanExploration = 3500.f;

	/** Max pan once combat is unlocked (preparation, waves, pause), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float MaxPanCombat = 8500.f;

	/** In exploration, released pan drifts back to the focus at this rate, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float PanReturnSpeed = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float PanSmoothSpeed = 6.f;

	/** Middle-mouse drag pan, cm per pixel at base distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float DragPanSensitivity = 1.5f;

	/** Middle-mouse drag release: smooth return duration in exploration, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0.01"))
	float DragPanReturnDuration = 0.85f;

	/** Cursor within this many pixels of a screen edge scrolls the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "0"))
	float EdgeScrollMargin = 30.f;

	/** Reference distance for drag-pan scaling, cm (Godot base camera distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pan", meta = (ClampMin = "1"))
	float BaseDistance = 2610.f;
};

/** Pure tactical camera math. Godot reference: Scenes/movements/camera.gd (_process, _process_pan_input). */
namespace TacticalCameraRules
{
	/** Offset from the focus point to the camera for a view yaw/pitch at a distance. */
	CODEXTACTICS_API FVector ComputeViewOffset(float YawDegrees, float PitchDegrees, float Distance);

	/** Distance after Notches wheel steps (positive = zoom out), clamped to [DistanceMin, DistanceMax]. */
	CODEXTACTICS_API float StepZoom(const FTacticalCameraConfig& Config, float CurrentDistance, float Notches);

	/** Edge-scroll input (-1..1 per axis; X right, Y forward) for a cursor position inside the viewport. */
	CODEXTACTICS_API FVector2D ComputeEdgeScroll(const FVector2D& MousePosition, const FVector2D& ViewportSize, float Margin);

	/** World ground-plane direction for a pan input (X right, Y forward) seen from a camera yaw. */
	CODEXTACTICS_API FVector ComputePanDirection(const FVector2D& Input, float YawDegrees);

	/** Clamps a pan offset to a maximum length. */
	CODEXTACTICS_API FVector ClampPan(const FVector& Pan, float MaxDistance);

	/** Cubic ease-out, 0..1. */
	CODEXTACTICS_API float CubicEaseOut(float T);

	/** Follows the target only once it leaves a deadzone around the current focus (turn-based framing). */
	CODEXTACTICS_API FVector FollowWithDeadzone(const FVector& Focus, const FVector& Target, float Deadzone, float Speed, float DeltaSeconds);
}
