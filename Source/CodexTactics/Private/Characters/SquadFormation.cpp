#include "Characters/SquadFormation.h"

namespace SquadFormation
{
	void GetForwardRight(const FVector& Heading, FVector& OutForward, FVector& OutRight)
	{
		OutForward = FVector(Heading.X, Heading.Y, 0.f).GetSafeNormal();
		if (OutForward.IsNearlyZero())
		{
			OutForward = FVector::ForwardVector;
		}
		OutRight = FVector::CrossProduct(FVector::UpVector, OutForward).GetSafeNormal();
	}

	FVector ComputeSlotPosition(const FSquadFormationConfig& Config, const FVector& LeaderLocation,
		const FVector& FormationForward, int32 SlotIndex, bool bColumnMode)
	{
		FVector Forward, Right;
		GetForwardRight(FormationForward, Forward, Right);

		if (bColumnMode)
		{
			const float ColumnBack = Config.ColumnBaseBack + static_cast<float>(SlotIndex + 1) * Config.ColumnSpacing;
			return LeaderLocation - Forward * ColumnBack;
		}

		const float Side = (SlotIndex % 2 == 0) ? -1.f : 1.f;
		const int32 Row = SlotIndex / 2 + 1;
		return LeaderLocation - Forward * (Config.BackOffset * Row) + Right * (Config.SideOffset * Side);
	}

	bool ShouldSwapSlots(const FSquadFormationConfig& Config, const FVector& LeaderLocation,
		const FVector& FormationForward, const FVector& Slot0FollowerLocation, const FVector& Slot1FollowerLocation)
	{
		const FVector SlotLeft = ComputeSlotPosition(Config, LeaderLocation, FormationForward, 0, false);
		const FVector SlotRight = ComputeSlotPosition(Config, LeaderLocation, FormationForward, 1, false);

		const float CurrentCost = FVector::DistSquared2D(Slot0FollowerLocation, SlotLeft) + FVector::DistSquared2D(Slot1FollowerLocation, SlotRight);
		const float SwappedCost = FVector::DistSquared2D(Slot0FollowerLocation, SlotRight) + FVector::DistSquared2D(Slot1FollowerLocation, SlotLeft);
		return SwappedCost + Config.SlotSwapHysteresis < CurrentCost;
	}

	FVector SmoothHeading(const FSquadFormationConfig& Config, const FVector& CurrentHeading,
		const FVector& TargetHeading, float DeltaSeconds)
	{
		FVector Target, TargetRight;
		GetForwardRight(TargetHeading, Target, TargetRight);
		if (CurrentHeading.IsNearlyZero())
		{
			return Target;
		}
		FVector Current, CurrentRight;
		GetForwardRight(CurrentHeading, Current, CurrentRight);

		const float Alpha = FMath::Clamp(DeltaSeconds * Config.RotationSmoothing, 0.f, 1.f);
		const FQuat Full = FQuat::FindBetweenNormals(Current, Target);
		const FQuat Partial = FQuat::Slerp(FQuat::Identity, Full, Alpha);
		return Partial.RotateVector(Current).GetSafeNormal2D();
	}

	FVector ComputeWanderOffset(const FSquadFormationConfig& Config, const FVector& FormationForward,
		int32 SlotIndex, float TimeSeconds)
	{
		FVector Forward, Right;
		GetForwardRight(FormationForward, Forward, Right);

		const float T = TimeSeconds * Config.WanderFrequency;
		const float Phase = static_cast<float>(SlotIndex + 1) * 2.39996f;
		const float Side = (FMath::Sin(T + Phase) * 0.7f + FMath::Sin(T * 0.53f + Phase * 1.5f) * 0.3f) * Config.WanderSideAmplitude;
		const float Back = (FMath::Cos(T * 0.85f + Phase) * 0.7f + FMath::Cos(T * 0.41f + Phase * 1.2f) * 0.3f) * Config.WanderBackAmplitude;
		return Right * Side + Forward * Back;
	}

	FVector ClampToRadius2D(const FVector& Origin, const FVector& Target, float Radius)
	{
		const FVector2D Offset(Target.X - Origin.X, Target.Y - Origin.Y);
		if (Offset.Size() <= Radius)
		{
			return Target;
		}
		const FVector2D Clamped = Offset.GetSafeNormal() * Radius;
		return FVector(Origin.X + Clamped.X, Origin.Y + Clamped.Y, Target.Z);
	}

	float ComputeFollowerSpeed(const FSquadFormationConfig& Config, float FollowerMaxSpeed,
		float DistanceToSlot, int32 SlotIndex, float TimeSeconds, bool bLeaderMoving)
	{
		if (DistanceToSlot <= Config.StopRadius)
		{
			return 0.f;
		}

		const float Jitter = bLeaderMoving
			? 1.f + FMath::Sin(TimeSeconds * 0.9f + static_cast<float>(SlotIndex + 1) * 1.7f) * Config.SpeedJitter
			: 1.f;
		float Speed = FollowerMaxSpeed * Config.FollowerSpeedFactor * Jitter;

		if (DistanceToSlot < Config.SlowRadius)
		{
			Speed = FMath::Max(Speed * (DistanceToSlot / Config.SlowRadius), Config.MinApproachSpeed);
		}
		else if (DistanceToSlot > Config.CatchUpDistance)
		{
			Speed = FollowerMaxSpeed * Config.CatchUpMultiplier;
		}

		if (DistanceToSlot <= Config.RegroupLimitDistance)
		{
			const float RegroupCap = FMath::Max(FollowerMaxSpeed * Config.RegroupSpeedMultiplier, Config.MinApproachSpeed);
			Speed = FMath::Min(Speed, RegroupCap);
		}
		return Speed;
	}
}
