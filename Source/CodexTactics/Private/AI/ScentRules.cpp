#include "AI/ScentRules.h"

#define LOCTEXT_NAMESPACE "ScentRules"

int32 ScentRules::CombineTraceWalls(const TArray<int32>& WallsPerTrace)
{
	if (WallsPerTrace.IsEmpty())
	{
		return 0;
	}
	int32 Clearest = TNumericLimits<int32>::Max();
	for (const int32 Walls : WallsPerTrace)
	{
		Clearest = FMath::Min(Clearest, FMath::Max(Walls, 0));
	}
	return Clearest;
}

float ScentRules::ScentReachCm(const FEnemyPerceptionParams& Params, int32 Walls)
{
	return PerceptionRules::OccludedRadius(Params.SmellRadiusCm, Walls, Params.SmellOcclusionPerWall);
}

bool ScentRules::Smells(const FEnemyPerceptionParams& Params, float DistanceCm, int32 Walls)
{
	const float Reach = ScentReachCm(Params, Walls);
	return Reach > 0.f && DistanceCm >= 0.f && DistanceCm <= Reach;
}

bool ScentRules::IsCloseDetection(const FEnemyPerceptionParams& Params, float DistanceCm, int32 Walls)
{
	return Params.SmellRadiusCm > 0.f && Walls <= 0 && DistanceCm >= 0.f && DistanceCm <= FMath::Min(Params.SmellDetectCm, Params.SmellRadiusCm);
}

EHoundScentAction ScentRules::Step(const FEnemyPerceptionParams& Params, FHoundScentState& State, const FHoundScentSense& Sense, float DeltaSeconds)
{
	const float Dt = FMath::Max(DeltaSeconds, 0.f);
	if (Sense.bClose)
	{
		State.Phase = EHoundScentPhase::None;
		State.PhaseSeconds = 0.f;
		State.SinceScentSeconds = 0.f;
		State.ScentPoint = Sense.Point;
		return EHoundScentAction::Detect;
	}
	if (State.Phase == EHoundScentPhase::None)
	{
		if (!Sense.bSmells)
		{
			return EHoundScentAction::None;
		}
		State.ScentPoint = Sense.Point;
		State.PhaseSeconds = 0.f;
		State.SinceScentSeconds = 0.f;
		if (Sense.bSkipSniff || Params.SniffSeconds <= 0.f)
		{
			State.Phase = EHoundScentPhase::Following;
			return EHoundScentAction::StartFollow;
		}
		State.Phase = EHoundScentPhase::Sniffing;
		return EHoundScentAction::StartSniff;
	}

	State.PhaseSeconds += Dt;
	if (Sense.bSmells)
	{
		State.ScentPoint = Sense.Point;
		State.SinceScentSeconds = 0.f;
	}
	else
	{
		State.SinceScentSeconds += Dt;
	}
	const bool bScentGone = State.SinceScentSeconds > FMath::Max(Params.ScentLostSeconds, 0.f);
	auto Lose = [&State]()
	{
		State.Phase = EHoundScentPhase::None;
		State.PhaseSeconds = 0.f;
		State.SinceScentSeconds = 0.f;
		return EHoundScentAction::LoseScent;
	};

	if (State.Phase == EHoundScentPhase::Sniffing)
	{
		if (State.PhaseSeconds + KINDA_SMALL_NUMBER < Params.SniffSeconds)
		{
			return EHoundScentAction::None;
		}
		if (bScentGone)
		{
			return Lose();
		}
		State.Phase = EHoundScentPhase::Following;
		State.PhaseSeconds = 0.f;
		return EHoundScentAction::StartFollow;
	}

	// Following.
	if (bScentGone || (Params.ScentFollowMaxSeconds > 0.f && State.PhaseSeconds > Params.ScentFollowMaxSeconds))
	{
		return Lose();
	}
	return EHoundScentAction::None;
}

float ScentRules::GetFollowSpeed(float PatrolWalkSpeed, float NormalSpeed, float Fraction)
{
	const float Wanted = FMath::Max(PatrolWalkSpeed, 0.f) * FMath::Max(Fraction, 0.f);
	const float Capped = NormalSpeed > 0.f ? FMath::Min(Wanted, NormalSpeed) : Wanted;
	return FMath::Max(Capped, 50.f);
}

FText ScentRules::GetCaughtScentLine()
{
	return LOCTEXT("CaughtScent", "The hound caught a scent.");
}

#undef LOCTEXT_NAMESPACE
