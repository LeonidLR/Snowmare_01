#include "Camera/TacticalCameraRules.h"

namespace TacticalCameraRules
{
	FVector ComputeViewOffset(float YawDegrees, float PitchDegrees, float Distance)
	{
		return -FRotator(PitchDegrees, YawDegrees, 0.f).Vector() * Distance;
	}

	float StepZoom(const FTacticalCameraConfig& Config, float CurrentDistance, float Notches)
	{
		return FMath::Clamp(CurrentDistance + Notches * Config.ZoomStep, GetMinZoomDistance(Config), FMath::Max(Config.DistanceMax, GetMinZoomDistance(Config)));
	}

	float GetMinSafeDistance(const FTacticalCameraConfig& Config)
	{
		return Config.MinSafeDistance > 0.f ? Config.MinSafeDistance : DefaultMinSafeDistance;
	}

	float GetMinClearanceHeight(const FTacticalCameraConfig& Config)
	{
		const float MinSine = 0.2588190f; // sin 15 deg
		return GetMinSafeDistance(Config) * FMath::Max(FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Config.Pitch))), MinSine);
	}

	float ComputeClearanceDolly(const FVector& Camera, const FVector& ViewForward, float MinDistance, float MinHeight, float ConeCos,
		TConstArrayView<FVector> Points)
	{
		const FVector Forward = ViewForward.GetSafeNormal();
		if (Forward.IsNearlyZero() || MinDistance <= 0.f)
		{
			return 0.f;
		}
		const float BackZ = -static_cast<float>(Forward.Z); // moving back by t raises the camera by t * BackZ (looking down: > 0)
		float Dolly = 0.f;
		for (const FVector& Point : Points)
		{
			const FVector ToPoint = Point - Camera;
			const float Dist = static_cast<float>(ToPoint.Size());
			const float Depth = static_cast<float>(FVector::DotProduct(ToPoint, Forward));
			const float Below = static_cast<float>(Camera.Z - Point.Z);
			if (Dist >= MinDistance || Below >= MinHeight - 0.5f)
			{
				continue; // far enough, or as far below the camera as the floor puts the focus
			}
			if (Dist > UE_KINDA_SMALL_NUMBER && (Depth <= 0.f || Depth / Dist < ConeCos))
			{
				continue; // behind the camera / out of the view: never on screen
			}
			// Moving back by t: |V + tB|^2 = Dist^2 + 2 t Depth + t^2 (B = -Forward, V = Camera - Point, V.B = Depth) reaches MinDistance at:
			const float ByDistance = -Depth + FMath::Sqrt(FMath::Max(0.f, Depth * Depth - Dist * Dist + MinDistance * MinDistance));
			const float ByHeight = BackZ > 1.e-3f ? (MinHeight - Below) / BackZ : TNumericLimits<float>::Max();
			Dolly = FMath::Max(Dolly, FMath::Min(ByDistance, ByHeight));
		}
		return FMath::Clamp(Dolly, 0.f, MinDistance);
	}

	float ComputeDetachedFocusZ(const FVector& ViewCentre, float CurrentZ, TConstArrayView<FVector> Points, float Radius)
	{
		float BestSq = FMath::Square(FMath::Max(0.f, Radius));
		float Result = CurrentZ;
		for (const FVector& Point : Points)
		{
			const float DistSq = static_cast<float>(FVector::DistSquared2D(Point, ViewCentre));
			if (DistSq <= BestSq)
			{
				BestSq = DistSq;
				Result = static_cast<float>(Point.Z);
			}
		}
		return Result;
	}

	float GetMinZoomDistance(const FTacticalCameraConfig& Config)
	{
		return FMath::Max(Config.DistanceMin, GetMinSafeDistance(Config));
	}

	float ClampMinDistance(const FTacticalCameraConfig& Config, float Distance)
	{
		return FMath::Max(Distance, GetMinSafeDistance(Config));
	}

	FVector PushOutOfCapsule(const FVector& Location, const FVector& CapsuleCenter, float HalfHeight, float Radius, float Clearance)
	{
		const float AxisHalf = FMath::Max(0.f, HalfHeight - Radius);
		const float Reach = Radius + FMath::Max(0.f, Clearance);
		const FVector Closest(CapsuleCenter.X, CapsuleCenter.Y, FMath::Clamp(Location.Z, CapsuleCenter.Z - AxisHalf, CapsuleCenter.Z + AxisHalf));
		FVector Away = Location - Closest;
		const float Dist = Away.Size();
		if (Dist >= Reach)
		{
			return Location;
		}
		Away = Dist > UE_SMALL_NUMBER ? Away / Dist : FVector::UpVector;
		return Closest + Away * Reach;
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

	FVector ClampFreePan(const FVector& Focus, const FVector& Pan, const FBox2D& LevelArea, float MarginCm)
	{
		if (!LevelArea.bIsValid)
		{
			return Pan;
		}
		FBox2D Allowed = LevelArea.ExpandBy(FMath::Max(0.f, MarginCm));
		Allowed += FVector2D(Focus.X, Focus.Y);
		const FVector2D View(Focus.X + Pan.X, Focus.Y + Pan.Y);
		const FVector2D Kept(FMath::Clamp(View.X, Allowed.Min.X, Allowed.Max.X), FMath::Clamp(View.Y, Allowed.Min.Y, Allowed.Max.Y));
		return FVector(Kept.X - Focus.X, Kept.Y - Focus.Y, Pan.Z);
	}

	float Smoothstep(float T)
	{
		const float Clamped = FMath::Clamp(T, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	float ComputeDramaticDistance(float SpanCm)
	{
		return FMath::Clamp(SpanCm * 1.35f + 400.f, 1000.f, 1900.f);
	}

	float CubicEaseOut(float T)
	{
		const float Clamped = FMath::Clamp(T, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - Clamped, 3.f);
	}

	float SpringDamp(float Current, float& Velocity, float Target, float HalfLife, float DeltaSeconds)
	{
		// Critically damped spring (exact solution over the step): y = 2 ln2 / half-life.
		const float Y = 2.f * 0.69314718f / FMath::Max(HalfLife, 0.001f);
		const float J0 = Current - Target;
		const float J1 = Velocity + J0 * Y;
		const float Decay = FMath::Exp(-Y * FMath::Max(DeltaSeconds, 0.f));
		Velocity = Decay * (Velocity - J1 * Y * DeltaSeconds);
		return Decay * (J0 + J1 * DeltaSeconds) + Target;
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
