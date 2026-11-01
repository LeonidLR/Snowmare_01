// A bypassed ambush relocates ahead of the squad, unseen (game designer answer 3, 2026-11-01, docs/design/COMBAT_LAB.md).
// UE-only, no Godot reference. Tests: CodexTactics.Combat.AmbushRelocate.*.

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
