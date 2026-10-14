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

bool TripwireRules::TriggersOn(bool bHostileWire, bool bIsOperative)
{
	return bIsOperative || !bHostileWire;
}

bool TripwireRules::CanDisarm(bool bHostileWire, EOperativeRole Role)
{
	return bHostileWire || Role == EOperativeRole::MedicSapper;
}

float TripwireRules::GetDisarmSeconds(bool bHostileWire, EOperativeRole Role)
{
	if (!bHostileWire)
	{
		return DisarmSeconds;
	}
	switch (Role)
	{
	case EOperativeRole::MedicSapper: return HostileDisarmSecondsSapper;
	case EOperativeRole::Engineer: return HostileDisarmSecondsEngineer;
	default: return HostileDisarmSecondsOther;
	}
}

bool TripwireRules::CanSpot(float DistanceCm, EOperativeRole Role, float BaseRangeCm, float SapperBonusCm)
{
	return DistanceCm <= DeployableRules::GetSpotRangeCm(BaseRangeCm, SapperBonusCm, Role);
}

FDefusalChance TripwireRules::GetHostileDisarmChance(EOperativeRole Role, float Luck, float ColdLevel, int32 FailedAttempts, bool bWarned)
{
	return DeployableRules::CalculateDefusal(Role, EOperativeStance::Crouching, Luck, ColdLevel, FailedAttempts, bWarned);
}

bool TripwireRules::ShouldAvoidKnownTraps(bool bSquadAutonomyOn)
{
	return bSquadAutonomyOn;
}

TripwireRules::EOperativeMoveFilter TripwireRules::ChooseOperativeMoveFilter(bool bProne, bool bSquadAutonomyOn)
{
	if (ShouldAvoidKnownTraps(bSquadAutonomyOn))
	{
		return bProne ? EOperativeMoveFilter::Default : EOperativeMoveFilter::NoCrawl;
	}
	return bProne ? EOperativeMoveFilter::TrapLiteral : EOperativeMoveFilter::NoCrawlTrapLiteral;
}

bool TripwireRules::IsClickOnWire(const FVector2D& Click, const FVector2D& A, const FVector2D& B, float SnapCm)
{
	return DistanceToWire(Click, A, B) <= FMath::Max(0.f, SnapCm);
}
