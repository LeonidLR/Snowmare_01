#include "AI/PatrolRouteRules.h"

int32 PatrolRouteRules::GetNextWaypointIndex(int32 NumWaypoints, int32 CurrentIndex, bool bIsLoop, bool bPingPong, bool& bInOutForward)
{
	if (NumWaypoints <= 0)
	{
		return INDEX_NONE;
	}
	if (CurrentIndex < 0 || CurrentIndex >= NumWaypoints)
	{
		bInOutForward = true;
		return 0;
	}
	if (NumWaypoints == 1)
	{
		return bIsLoop || bPingPong ? 0 : INDEX_NONE;
	}
	if (bIsLoop)
	{
		bInOutForward = true;
		return (CurrentIndex + 1) % NumWaypoints;
	}
	if (bPingPong)
	{
		if (bInOutForward && CurrentIndex >= NumWaypoints - 1)
		{
			bInOutForward = false;
		}
		else if (!bInOutForward && CurrentIndex <= 0)
		{
			bInOutForward = true;
		}
		return bInOutForward ? CurrentIndex + 1 : CurrentIndex - 1;
	}
	return CurrentIndex < NumWaypoints - 1 ? CurrentIndex + 1 : INDEX_NONE;
}

float PatrolRouteRules::GetWaitTime(const TArray<float>& PerPointWaitTime, int32 Index, float DefaultSeconds)
{
	const float Wait = PerPointWaitTime.IsValidIndex(Index) && PerPointWaitTime[Index] > 0.f ? PerPointWaitTime[Index] : DefaultSeconds;
	return FMath::Max(Wait, 0.f);
}

FEscortDecision PatrolRouteRules::EvaluateEscort(const FVector& EscortLocation, const FVector& LeaderLocation, bool bCurrentlyMoving,
	float MinCm, float MaxCm)
{
	FEscortDecision Decision;
	const float Distance = FVector::Dist2D(EscortLocation, LeaderLocation);
	const float Middle = (MinCm + MaxCm) * 0.5f;
	Decision.bShouldMove = Distance > MaxCm || (bCurrentlyMoving && Distance > Middle);
	if (Decision.bShouldMove)
	{
		FVector Side = FVector(EscortLocation.X - LeaderLocation.X, EscortLocation.Y - LeaderLocation.Y, 0.f).GetSafeNormal();
		if (Side.IsNearlyZero())
		{
			Side = FVector(-1.f, 0.f, 0.f);
		}
		Decision.Destination = FVector(LeaderLocation.X, LeaderLocation.Y, EscortLocation.Z) + Side * Middle;
	}
	else
	{
		Decision.Destination = EscortLocation;
	}
	return Decision;
}

bool PatrolRouteRules::IsTrapHeard(float DistanceCm, float RadiusCm)
{
	return DistanceCm >= 0.f && DistanceCm <= RadiusCm;
}

bool PatrolRouteRules::ShouldBreakPatrol(const FPatrolAlertInput& Input)
{
	return Input.bSeesOperative || Input.bTookDamage || Input.bPartnerAlerted || IsTrapHeard(Input.TrapDistanceCm, Input.TrapAlertRadiusCm);
}
