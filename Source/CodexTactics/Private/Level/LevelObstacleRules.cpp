#include "Level/LevelObstacleRules.h"

namespace BridgeRules
{
	EBridgeEntry Evaluate(int32 Occupants, int32 Capacity, bool bAlreadyOccupant)
	{
		if (bAlreadyOccupant)
		{
			return EBridgeEntry::AlreadyInside;
		}
		if (Capacity <= 0 || Occupants < Capacity)
		{
			return EBridgeEntry::Admit;
		}
		return EBridgeEntry::Refuse;
	}

	float SpanCoordinate(const FTransform& BridgeToWorld, const FVector& WorldPoint)
	{
		return static_cast<float>(BridgeToWorld.InverseTransformPosition(WorldPoint).X);
	}

	bool DidCross(float EntryCoordinate, float ExitCoordinate)
	{
		return FMath::Sign(EntryCoordinate) != FMath::Sign(ExitCoordinate) && !FMath::IsNearlyZero(EntryCoordinate) && !FMath::IsNearlyZero(ExitCoordinate);
	}

	bool ShouldRepeatRefusal(float LastRefusalTime, float Now, float CooldownSeconds)
	{
		return LastRefusalTime < 0.f || Now - LastRefusalTime >= FMath::Max(0.f, CooldownSeconds);
	}
}

namespace CrawlGapRules
{
	bool CanPassUnder(EOperativeStance Stance)
	{
		return Stance == EOperativeStance::Prone;
	}

	bool FitsUnder(float BodyHeightCm, float ClearanceCm)
	{
		return BodyHeightCm > 0.f && BodyHeightCm < ClearanceCm;
	}

	bool IsApproaching(const FVector& ToFence2D, const FVector& Velocity2D)
	{
		const FVector Velocity(Velocity2D.X, Velocity2D.Y, 0.f);
		const FVector ToFence(ToFence2D.X, ToFence2D.Y, 0.f);
		if (Velocity.Size() < ApproachSpeedThreshold || ToFence.IsNearlyZero())
		{
			return false;
		}
		return FVector::DotProduct(Velocity.GetSafeNormal(), ToFence.GetSafeNormal()) > 0.1f;
	}

	int32 Side(float LocalX)
	{
		return FMath::IsNearlyZero(LocalX, 1.f) ? 0 : (LocalX > 0.f ? 1 : -1);
	}

	bool DidPass(int32 EntrySide, int32 ExitSide)
	{
		return EntrySide != 0 && ExitSide != 0 && EntrySide != ExitSide;
	}
}
