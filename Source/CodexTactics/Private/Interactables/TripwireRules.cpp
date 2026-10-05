#include "Interactables/TripwireRules.h"

bool TripwireRules::IsSpanValid(float SpanCm)
{
	return SpanCm >= MinSpanCm && SpanCm <= MaxSpanCm;
}

bool TripwireRules::TripsWire(float BodyHeightCm)
{
	return BodyHeightCm > WireHeightCm;
}

float TripwireRules::DistanceToWire(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const double LengthSq = AB.SizeSquared();
	const double T = LengthSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.0, 1.0) : 0.0;
	return static_cast<float>(FVector2D::Distance(Point, A + AB * T));
}

bool TripwireRules::TouchesWire(const FVector2D& Point, float RadiusCm, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const double LengthSq = AB.SizeSquared();
	if (LengthSq <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const double T = FVector2D::DotProduct(Point - A, AB) / LengthSq;
	return T >= 0.0 && T <= 1.0 && DistanceToWire(Point, A, B) <= RadiusCm;
}

int32 TripwireRules::GrenadesReturned(bool bFumble)
{
	return bFumble ? GrenadeCost - 1 : GrenadeCost;
}
