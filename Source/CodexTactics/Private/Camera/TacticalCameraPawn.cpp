#include "Camera/TacticalCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"

namespace
{
	/** Real-time delta clamp, s (Godot camera: 0.001..0.1). */
	constexpr float MinRealDelta = 0.001f;
	constexpr float MaxRealDelta = 0.1f;
}

ATacticalCameraPawn::ATacticalCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetActorEnableCollision(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
}

void ATacticalCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// Godot Vertical FOV 30 deg converts to ~51.5 deg Horizontal FOV on 16:9 displays
	const float HalfVFovRad = FMath::DegreesToRadians(Config.VerticalFov * 0.5f);
	const float HFovDeg = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(HalfVFovRad) * (16.f / 9.f)));
	Camera->SetFieldOfView(HFovDeg);
	CurrentYaw = TargetYaw = Config.BaseYaw;
	UserDistanceExploration = Config.DistanceExploration;
	UserDistanceCombat = Config.DistanceCombat;
	bCombatView = IsCombatView();
	CurrentDistance = TargetDistance = bCombatView ? UserDistanceCombat : UserDistanceExploration;

	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->OnLeaderChanged.AddDynamic(this, &ATacticalCameraPawn::HandleLeaderChanged);
		SetFollowTarget(Squad->GetLeader());
	}
}

void ATacticalCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FollowTarget.IsValid())
	{
		// The squad may spawn after the camera; pick up the leader once it exists.
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			SetFollowTarget(Squad->GetLeader());
		}
		if (!FollowTarget.IsValid())
		{
			return;
		}
	}

	const float RealDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), MinRealDelta, MaxRealDelta);
	UpdateZoomMode();
	UpdatePan(RealDelta);
	UpdateRotation(RealDelta);
	CurrentDistance = FMath::Lerp(CurrentDistance, TargetDistance, FMath::Clamp(Config.ZoomSmoothSpeed * RealDelta, 0.f, 1.f));

	Focus = ComputeFocus(RealDelta);
	const FVector Desired = Focus + TacticalCameraRules::ComputeViewOffset(CurrentYaw, Config.Pitch, CurrentDistance) + PanOffset;
	const FRotator ViewRotation(Config.Pitch, CurrentYaw, 0.f);

	if (!bInitialized)
	{
		bInitialized = true;
		SetActorLocationAndRotation(Desired, ViewRotation);
		return;
	}
	const float FollowSpeed = IsTurnBased() ? Config.TacticalFollowSpeed : Config.FollowSpeed;
	const FVector NewLocation = FMath::Lerp(GetActorLocation(), Desired, FMath::Clamp(FollowSpeed * RealDelta, 0.f, 1.f));
	SetActorLocationAndRotation(NewLocation, ViewRotation);
}

void ATacticalCameraPawn::SetFollowTarget(AActor* NewTarget)
{
	FollowTarget = NewTarget;
	if (NewTarget && !bInitialized)
	{
		Focus = NewTarget->GetActorLocation();
	}
}

void ATacticalCameraPawn::AddZoomNotches(float Notches)
{
	TargetDistance = TacticalCameraRules::StepZoom(Config, TargetDistance, Notches);
	(bCombatView ? UserDistanceCombat : UserDistanceExploration) = TargetDistance;
}

void ATacticalCameraPawn::RotateStep(int32 Direction)
{
	TargetYaw += Config.RotationStep * static_cast<float>(FMath::Sign(Direction));
}

void ATacticalCameraPawn::SetDragRotating(bool bActive)
{
	bDragRotating = bActive;
	bHasLastDragCursor = false;
}

FVector2D ATacticalCameraPawn::ConsumeCursorDelta()
{
	// Pixels, not GetInputMouseDelta: that one is raw mouse counts scaled by the input sensitivity (~0.07),
	// which made drag rotation an order of magnitude slower than Godot.
	const APlayerController* PC = GetController<APlayerController>();
	FVector2D Cursor;
	if (!PC || !PC->GetMousePosition(Cursor.X, Cursor.Y))
	{
		bHasLastDragCursor = false;
		return FVector2D::ZeroVector;
	}
	const FVector2D Delta = bHasLastDragCursor ? Cursor - LastDragCursor : FVector2D::ZeroVector;
	LastDragCursor = Cursor;
	bHasLastDragCursor = true;
	return Delta;
}

void ATacticalCameraPawn::SetDragPanning(bool bActive)
{
	bDragPanning = bActive;
	bHasLastDragCursor = false;
	if (bActive)
	{
		bPanReturning = false;
	}
	else if (!IsCombatView())
	{
		bPanReturning = true;
		PanReturnStart = PanOffset;
		PanReturnTime = 0.f;
	}
}

void ATacticalCameraPawn::HandleLeaderChanged(AOperativeCharacter* NewLeader)
{
	SetFollowTarget(NewLeader);
}

bool ATacticalCameraPawn::IsCombatView() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetPhase() != ECodexGamePhase::Exploration;
}

bool ATacticalCameraPawn::IsTurnBased() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
}

void ATacticalCameraPawn::UpdateZoomMode()
{
	const bool bCombatNow = IsCombatView();
	if (bCombatNow != bCombatView)
	{
		bCombatView = bCombatNow;
		TargetDistance = bCombatView ? UserDistanceCombat : UserDistanceExploration;
	}
}

void ATacticalCameraPawn::UpdatePan(float RealDelta)
{
	APlayerController* PC = GetController<APlayerController>();
	const float PanAlpha = FMath::Clamp(Config.PanSmoothSpeed * RealDelta, 0.f, 1.f);

	if (bDragPanning && PC)
	{
		const FVector2D Delta = ConsumeCursorDelta();
		const FRotator Yaw(0.f, CurrentYaw, 0.f);
		const FVector Forward = Yaw.Vector();
		const FVector Right = FRotationMatrix(Yaw).GetScaledAxis(EAxis::Y);
		const float Scale = Config.DragPanSensitivity * (CurrentDistance / Config.BaseDistance);
		// Grab-and-drag: the world follows the cursor (Godot: -right * relative.x + forward * relative.y).
		TargetPanOffset += (-Right * Delta.X + Forward * Delta.Y) * Scale;
		TargetPanOffset = TacticalCameraRules::ClampPan(TargetPanOffset, Config.MaxPanCombat);
		PanOffset = FMath::Lerp(PanOffset, TargetPanOffset, PanAlpha);
		return;
	}

	if (bPanReturning)
	{
		PanReturnTime += RealDelta;
		const float T = PanReturnTime / Config.DragPanReturnDuration;
		PanOffset = FMath::Lerp(PanReturnStart, FVector::ZeroVector, TacticalCameraRules::CubicEaseOut(T));
		TargetPanOffset = PanOffset;
		if (T >= 1.f)
		{
			bPanReturning = false;
			PanOffset = TargetPanOffset = FVector::ZeroVector;
		}
		return;
	}

	FVector2D Input = FVector2D::ZeroVector;
	if (PC)
	{
		Input.Y += PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f;
		Input.Y -= PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f;
		Input.X += PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f;
		Input.X -= PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f;

		float MouseX = 0.f;
		float MouseY = 0.f;
		int32 SizeX = 0;
		int32 SizeY = 0;
		if (PC->GetMousePosition(MouseX, MouseY))
		{
			PC->GetViewportSize(SizeX, SizeY);
			Input += TacticalCameraRules::ComputeEdgeScroll(FVector2D(MouseX, MouseY), FVector2D(SizeX, SizeY), Config.EdgeScrollMargin);
		}
	}

	const bool bCombat = IsCombatView();
	if (!Input.IsNearlyZero())
	{
		TargetPanOffset += TacticalCameraRules::ComputePanDirection(Input, CurrentYaw) * Config.PanSpeed * RealDelta;
		TargetPanOffset = TacticalCameraRules::ClampPan(TargetPanOffset, bCombat ? Config.MaxPanCombat : Config.MaxPanExploration);
	}
	else if (!bCombat)
	{
		// In exploration the view drifts back to the leader; in combat it stays where the player left it.
		TargetPanOffset = FMath::Lerp(TargetPanOffset, FVector::ZeroVector, FMath::Clamp(Config.PanReturnSpeed * RealDelta, 0.f, 1.f));
	}
	PanOffset = FMath::Lerp(PanOffset, TargetPanOffset, PanAlpha);
}

void ATacticalCameraPawn::UpdateRotation(float RealDelta)
{
	if (bDragRotating)
	{
		TargetYaw += ConsumeCursorDelta().X * Config.DragRotationSensitivity;
	}
	const float Alpha = FMath::Clamp(Config.RotationSmoothSpeed * RealDelta, 0.f, 1.f);
	CurrentYaw += FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw) * Alpha;
}

FVector ATacticalCameraPawn::ComputeFocus(float RealDelta)
{
	const FVector TargetLocation = FollowTarget->GetActorLocation();
	if (IsTurnBased())
	{
		return TacticalCameraRules::FollowWithDeadzone(Focus, TargetLocation, Config.TacticalDeadzone, Config.TacticalFollowSpeed, RealDelta);
	}
	return TargetLocation;
}
