#include "Interactables/TrapApproachRules.h"

FVector2D TrapApproachRules::MineApproachPoint(const FVector2D& Trap, const FVector2D& From, float StandOffCm)
{
	FVector2D Direction = From - Trap;
	Direction = Direction.IsNearlyZero() ? FVector2D(1.f, 0.f) : Direction.GetSafeNormal();
	return Trap + Direction * FMath::Max(0.f, StandOffCm);
}

FVector2D TrapApproachRules::ClosestPointOnWire(const FVector2D& A, const FVector2D& B, const FVector2D& From)
{
	const FVector2D Span = B - A;
	const double LengthSquared = Span.SizeSquared();
	if (LengthSquared < KINDA_SMALL_NUMBER)
	{
		return A;
	}
	const double T = FMath::Clamp(FVector2D::DotProduct(From - A, Span) / LengthSquared, 0.0, 1.0);
	return A + Span * T;
}

FVector2D TrapApproachRules::WireApproachPoint(const FVector2D& A, const FVector2D& B, const FVector2D& From, float StandOffCm)
{
	const FVector2D Closest = ClosestPointOnWire(A, B, From);
	FVector2D Direction = From - Closest;
	if (Direction.IsNearlyZero())
	{
		const FVector2D Along = (B - A).GetSafeNormal();
		Direction = Along.IsNearlyZero() ? FVector2D(1.f, 0.f) : FVector2D(-Along.Y, Along.X);
	}
	return Closest + Direction.GetSafeNormal() * FMath::Max(0.f, StandOffCm);
}

float TrapApproachRules::GoalDistance(float StandOffCm)
{
	return FMath::Max(0.f, StandOffCm - GoalInsetCm);
}

bool TrapApproachRules::IsAtDisarmSpot(float DistanceCm, float StandOffCm, float ToleranceCm, bool bStillWalking)
{
	const float Reach = bStillWalking ? FMath::Min(WalkingReachCm, FMath::Max(0.f, ToleranceCm)) : FMath::Max(0.f, ToleranceCm);
	return DistanceCm <= FMath::Max(0.f, StandOffCm) + Reach;
}

bool TrapApproachRules::UpdateDefuserLease(bool bHasLease, bool bIsApproachingDefuser, float DistanceCm, float TriggerRadiusCm, float ExitMarginCm)
{
	if (DistanceCm > TriggerRadiusCm + FMath::Max(0.f, ExitMarginCm))
	{
		return false; // stepped out: the mine counts for him again
	}
	return bHasLease || (bIsApproachingDefuser && DistanceCm <= TriggerRadiusCm);
}
