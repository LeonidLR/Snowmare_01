// A bypassed ambush relocates ahead of the squad by a CONCEALED ROUTE on foot (game designer answer 3, 2026-11-01; the hybrid of decision 1,
// 2026-11-01 night, docs/design/COMBAT_LAB.md). UE-only, no Godot reference. Tests: CodexTactics.Combat.AmbushRelocate.*.

#include "Combat/AmbushRelocateRules.h"

bool AmbushRelocateRules::IsBypassed(float NearestSquadCm, float AngleOffHeadingDeg, float StrikeRangeCm, const FAmbushRelocateParams& Params)
{
	if (NearestSquadCm <= StrikeRangeCm)
	{
		return false;
	}
	return FMath::Abs(AngleOffHeadingDeg) > Params.BehindAngleDeg || NearestSquadCm > Params.FarCm;
}

float AmbushRelocateRules::StepBypassedSeconds(bool bBypassed, float Seconds, float DeltaSeconds)
{
	return bBypassed ? Seconds + FMath::Max(0.f, DeltaSeconds) : 0.f;
}

EAmbushRelocateDecision AmbushRelocateRules::Decide(float BypassedSeconds, bool bSeenNow, const FAmbushRelocateParams& Params)
{
	if (!Params.bEnabled || BypassedSeconds < Params.AfterSeconds)
	{
		return EAmbushRelocateDecision::Stay;
	}
	return bSeenNow ? EAmbushRelocateDecision::WaitUnseen : EAmbushRelocateDecision::Relocate;
}

bool AmbushRelocateRules::IsGoodDestination(float AheadCos, bool bConcealed, const FAmbushRelocateParams& Params)
{
	return bConcealed && AheadCos >= Params.MinAheadCos;
}

TArray<FVector2D> AmbushRelocateRules::SamplePath(const TArray<FVector2D>& Path, float StepCm)
{
	TArray<FVector2D> Out;
	if (Path.IsEmpty())
	{
		return Out;
	}
	const float Step = FMath::Max(10.f, StepCm);
	Out.Add(Path[0]);
	float Walked = 0.f; // since the last sample
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector2D From = Path[Index - 1];
		const FVector2D Segment = Path[Index] - From;
		const float Length = static_cast<float>(Segment.Size());
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		float At = Step - Walked;
		while (At < Length)
		{
			Out.Add(From + Segment * static_cast<double>(At / Length));
			At += Step;
		}
		Walked = Length - (At - Step);
	}
	if (Path.Num() > 1 && FVector2D::Distance(Out.Last(), Path.Last()) > 1.0)
	{
		Out.Add(Path.Last());
	}
	return Out;
}

float AmbushRelocateRules::PathLength(const TArray<FVector2D>& Path)
{
	float Length = 0.f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		Length += static_cast<float>(FVector2D::Distance(Path[Index - 1], Path[Index]));
	}
	return Length;
}

bool AmbushRelocateRules::IsConcealedRoute(const FAmbushRouteCandidate& Route, const FAmbushRelocateParams& Params)
{
	return Route.bReachable && Route.SeenSamples == 0 && Route.bDestinationConcealed && Route.PathCm <= Params.MaxRouteCm;
}

int32 AmbushRelocateRules::PickRoute(const TArray<FAmbushRouteCandidate>& Routes, const FAmbushRelocateParams& Params)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Routes.Num(); ++Index)
	{
		if (!IsConcealedRoute(Routes[Index], Params))
		{
			continue;
		}
		if (Best == INDEX_NONE || Routes[Index].Score > Routes[Best].Score + KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(Routes[Index].Score, Routes[Best].Score) && Routes[Index].PathCm < Routes[Best].PathCm))
		{
			Best = Index;
		}
	}
	return Best;
}

EAmbushNoRouteOutcome AmbushRelocateRules::NoRouteOutcome(int32 FailedSearches, const FAmbushRelocateParams& Params)
{
	return Params.bNoRoutePatrol && FailedSearches >= FMath::Max(1, Params.NoRouteTries) ? EAmbushNoRouteOutcome::Patrol : EAmbushNoRouteOutcome::Stay;
}

float AmbushRelocateRules::RunLimitSeconds(float PathCm, float SpeedCmS)
{
	return 2.f * FMath::Max(0.f, PathCm) / FMath::Max(50.f, SpeedCmS) + 10.f;
}

EAmbushRunStep AmbushRelocateRules::StepRun(bool bSpotted, bool bAllArrived, float ElapsedSeconds, float LimitSeconds)
{
	if (bSpotted)
	{
		return EAmbushRunStep::Spotted;
	}
	if (bAllArrived)
	{
		return EAmbushRunStep::Arrived;
	}
	return ElapsedSeconds >= LimitSeconds ? EAmbushRunStep::TimedOut : EAmbushRunStep::Running;
}

const TCHAR* AmbushRelocateRules::RunStepName(EAmbushRunStep Step)
{
	switch (Step)
	{
	case EAmbushRunStep::Arrived: return TEXT("arrived");
	case EAmbushRunStep::Spotted: return TEXT("spotted");
	case EAmbushRunStep::TimedOut: return TEXT("timed out");
	default: return TEXT("running");
	}
}
