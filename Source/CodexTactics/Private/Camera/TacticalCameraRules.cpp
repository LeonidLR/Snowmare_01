#include "Camera/TacticalCameraRules.h"

namespace TacticalCameraRules
{
	FVector ComputeViewOffset(float YawDegrees, float PitchDegrees, float Distance)
	{
		return -FRotator(PitchDegrees, YawDegrees, 0.f).Vector() * Distance;
	}

	float StepZoom(const FTacticalCameraConfig& Config, float CurrentDistance, float Notches)
	{
		return FMath::Clamp(CurrentDistance + Notches * Config.ZoomStep, Config.DistanceMin, Config.DistanceMax);
	}

	FVector2D ComputeEdgeScroll(const FVector2D& MousePosition, const FVector2D& ViewportSize, float Margin)
	{
		FVector2D Input = FVector2D::ZeroVector;
		const bool bInside = MousePosition.X >= 0.f && MousePosition.Y >= 0.f
			&& MousePosition.X <= ViewportSize.X && MousePosition.Y <= ViewportSize.Y;
		if (!bInside || ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f)
		{
			return Input;
		}
		if (MousePosition.X <= Margin)
		{
			Input.X = -1.f;
		}
		else if (MousePosition.X >= ViewportSize.X - Margin)
		{
			Input.X = 1.f;
		}
		// Screen Y grows downwards: top edge moves the view forward.
		if (MousePosition.Y <= Margin)
		{
			Input.Y = 1.f;
		}
		else if (MousePosition.Y >= ViewportSize.Y - Margin)
		{
			Input.Y = -1.f;
		}
		return Input;
	}

	FVector ComputePanDirection(const FVector2D& Input, float YawDegrees)
	{
		if (Input.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}
		const FVector2D Normalized = Input.GetSafeNormal();
		const FRotator Yaw(0.f, YawDegrees, 0.f);
		const FVector Forward = Yaw.Vector();
		const FVector Right = FRotationMatrix(Yaw).GetScaledAxis(EAxis::Y);
		return (Forward * Normalized.Y + Right * Normalized.X).GetSafeNormal2D();
	}

	FVector ClampPan(const FVector& Pan, float MaxDistance)
	{
		return Pan.Size() > MaxDistance ? Pan.GetSafeNormal() * MaxDistance : Pan;
	}

	float CubicEaseOut(float T)
	{
		const float Clamped = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - Clamped, 3.f);
	}

	FVector FollowWithDeadzone(const FVector& Focus, const FVector& Target, float Deadzone, float Speed, float DeltaSeconds)
	{
		FVector Result = Focus;
		Result.Z = Target.Z;
		if (FVector::Dist2D(Focus, Target) > Deadzone)
		{
			const float Alpha = FMath::Clamp(Speed * DeltaSeconds, 0.f, 1.f);
			Result.X = FMath::Lerp(Focus.X, Target.X, Alpha);
			Result.Y = FMath::Lerp(Focus.Y, Target.Y, Alpha);
		}
		return Result;
	}
}
