#include "Camera/TacticalCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/GodotBalanceAsset.h"
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

	if (const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>())
	{
		ShakeConfig = CameraShakeRules::ConfigFromBalance(GameMode->GameBalanceConfig.LoadSynchronous());
	}

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
	// Godot: a smooth focus with its own distance or a dramatic shot owns the zoom.
	if (!(bSmoothFocusing && SmoothTargetDistance > 0.f) && !bDramaticShot)
	{
		CurrentDistance = FMath::Lerp(CurrentDistance, TargetDistance, FMath::Clamp(Config.ZoomSmoothSpeed * RealDelta, 0.f, 1.f));
	}
	const bool bWasGliding = bSmoothFocusing || bDramaticShot;
	UpdateSmoothFocus(RealDelta);

	if (!bWasGliding)
	{
		Focus = ComputeFocus(RealDelta);
	}
	const FVector Desired = Focus + TacticalCameraRules::ComputeViewOffset(CurrentYaw, Config.Pitch, CurrentDistance) + PanOffset;
	const FRotator ViewRotation(Config.Pitch, CurrentYaw, 0.f);

	if (!bInitialized)
	{
		bInitialized = true;
		SetActorLocationAndRotation(Desired, ViewRotation);
		return;
	}
	const float FollowSpeed = IsTurnBased() ? Config.TacticalFollowSpeed : Config.FollowSpeed;
	// Godot: gliding, a dramatic shot or a turn-based non-leader target (an enemy moving) place the camera exactly.
	const bool bSnap = bWasGliding || (IsTurnBased() && !IsFollowingLeader());
	// The shake offset rides on top of the follow position (removed before smoothing so it never accumulates).
	const FVector NewLocation = bSnap ? Desired
		: FMath::Lerp(GetActorLocation() - ShakeOffset, Desired, FMath::Clamp(FollowSpeed * RealDelta, 0.f, 1.f));
	ShakeOffset = UpdateShake(RealDelta, ViewRotation);
	SetActorLocationAndRotation(NewLocation + ShakeOffset, ViewRotation);
}

void ATacticalCameraPawn::TriggerWeaponShake(const FString& WeaponType)
{
	if (IsTurnBased())
	{
		ShakeTrauma = CameraShakeRules::AddTrauma(ShakeTrauma, CameraShakeRules::GetPower(ShakeConfig, WeaponType));
	}
}

FVector ATacticalCameraPawn::UpdateShake(float RealDelta, const FRotator& ViewRotation)
{
	if (!IsTurnBased() || ShakeTrauma <= 0.f)
	{
		ShakeTrauma = 0.f;
		return FVector::ZeroVector;
	}
	ShakeTrauma = CameraShakeRules::Decay(ShakeConfig, ShakeTrauma, RealDelta);
	ShakeNoiseTime += RealDelta * ShakeConfig.Frequency;
	const float Scale = CameraShakeRules::GetOffsetScale(ShakeConfig, ShakeTrauma) * 100.f; // m -> cm
	const FRotationMatrix View(ViewRotation);
	return View.GetUnitAxis(EAxis::Y) * FMath::PerlinNoise1D(ShakeNoiseTime) * Scale
		+ View.GetUnitAxis(EAxis::Z) * FMath::PerlinNoise1D(ShakeNoiseTime + 57.3f) * Scale;
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
	// User report 2026-10-05: picking an operative (1-4, a click) centres the camera on him — a WASD / drag pan kept
	// in the fight view used to leave him off screen.
	if (!bDragPanning && !PanOffset.IsNearlyZero(1.f))
	{
		bPanReturning = true;
		PanReturnStart = PanOffset;
		PanReturnTime = 0.f;
	}
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
	// Godot: a turn-based target that is not the leader (an enemy on its turn) sits exactly in the centre.
	if (IsTurnBased() && IsFollowingLeader())
	{
		return TacticalCameraRules::FollowWithDeadzone(Focus, TargetLocation, Config.TacticalDeadzone, Config.TacticalFollowSpeed, RealDelta);
	}
	return TargetLocation;
}

bool ATacticalCameraPawn::IsFollowingLeader() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return !Squad || FollowTarget.Get() == Squad->GetLeader();
}

void ATacticalCameraPawn::BeginSmoothFocus(float Duration, float TargetDist)
{
	bSmoothFocusing = true;
	SmoothTime = 0.f;
	SmoothDuration = FMath::Max(0.1f, Duration);
	SmoothStartFocus = Focus;
	SmoothStartPan = PanOffset;
	SmoothStartDistance = CurrentDistance;
	SmoothTargetDistance = TargetDist;
	TargetPanOffset = FVector::ZeroVector;
	bDragPanning = false;
	bPanReturning = false;
}

void ATacticalCameraPawn::SmoothFocusOnTarget(AActor* Target, float Duration, float TargetDist)
{
	if (!IsValid(Target))
	{
		return;
	}
	FollowTarget = Target;
	bSmoothToPosition = false;
	BeginSmoothFocus(Duration, TargetDist);
}

void ATacticalCameraPawn::SmoothFocusOnPosition(const FVector& WorldPosition, float Duration, float TargetDist)
{
	bSmoothToPosition = true;
	SmoothPosition = WorldPosition;
	BeginSmoothFocus(Duration, TargetDist);
}

void ATacticalCameraPawn::DramaticActionFocus(const AActor* From, const AActor* To, float Duration)
{
	if (!IsValid(From) || !IsValid(To))
	{
		return;
	}
	const FVector A = From->GetActorLocation();
	const FVector B = To->GetActorLocation();
	SmoothFocusOnPosition((A + B) * 0.5f, Duration, TacticalCameraRules::ComputeDramaticDistance(FVector::Dist(A, B)));
}

void ATacticalCameraPawn::EnterTurnBasedZoom(float Distance)
{
	if (!bTurnBasedZoom)
	{
		PreTurnBasedDistance = TargetDistance;
		bTurnBasedZoom = true;
	}
	TargetDistance = Distance;
}

void ATacticalCameraPawn::ExitTurnBasedZoom()
{
	bDramaticShot = false;
	if (bTurnBasedZoom)
	{
		TargetDistance = PreTurnBasedDistance;
		bTurnBasedZoom = false;
	}
	// Godot: the camera goes back to the leader after the fight.
	if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		SetFollowTarget(Squad->GetLeader());
	}
}

void ATacticalCameraPawn::UpdateSmoothFocus(float RealDelta)
{
	if (!bSmoothFocusing)
	{
		return;
	}
	FVector End;
	if (bSmoothToPosition)
	{
		End = SmoothPosition;
	}
	else if (FollowTarget.IsValid())
	{
		End = FollowTarget->GetActorLocation();
	}
	else
	{
		bSmoothFocusing = false;
		return;
	}
	SmoothTime += RealDelta;
	const float T = FMath::Clamp(SmoothTime / SmoothDuration, 0.f, 1.f);
	const float Ease = TacticalCameraRules::Smoothstep(T);
	Focus = FMath::Lerp(SmoothStartFocus, End, Ease);
	PanOffset = FMath::Lerp(SmoothStartPan, FVector::ZeroVector, Ease);
	TargetPanOffset = PanOffset;
	if (SmoothTargetDistance > 0.f)
	{
		CurrentDistance = FMath::Lerp(SmoothStartDistance, SmoothTargetDistance, Ease);
		TargetDistance = SmoothTargetDistance;
	}
	if (T >= 1.f)
	{
		bSmoothFocusing = false;
		Focus = End;
		bSmoothToPosition = false;
		PanOffset = TargetPanOffset = FVector::ZeroVector;
		if (SmoothTargetDistance > 0.f)
		{
			CurrentDistance = TargetDistance = SmoothTargetDistance;
			SmoothTargetDistance = -1.f;
		}
	}
}
