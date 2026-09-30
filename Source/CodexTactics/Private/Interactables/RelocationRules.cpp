#include "Interactables/RelocationRules.h"

bool RelocationRules::CanRelocateNow(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bLeaderZoneSolo)
{
	return Phase != ECodexGamePhase::WaveCombat || Mode == ECodexCombatMode::TacticalPause || bLeaderZoneSolo;
}

float RelocationRules::GetPlacementRadius(ECodexGamePhase Phase, ECodexCombatMode Mode, float PauseRadius, float WorkerPlacementRadius)
{
	if (Mode == ECodexCombatMode::TacticalPause)
	{
		return PauseRadius;
	}
	if (Phase == ECodexGamePhase::Preparation)
	{
		return UnlimitedRadius;
	}
	return WorkerPlacementRadius;
}

bool RelocationRules::IsWithinRadius(const FVector& Origin, const FVector& Point, float Radius)
{
	return FVector::Dist2D(Origin, Point) <= Radius;
}

ELiftBlocker RelocationRules::GetLiftBlocker(float ColdLevel, float HealthFraction, float MaxColdToLift, float MinHealthFractionToLift)
{
	if (ColdLevel >= MaxColdToLift)
	{
		return ELiftBlocker::TooCold;
	}
	if (HealthFraction < MinHealthFractionToLift)
	{
		return ELiftBlocker::Wounded;
	}
	return ELiftBlocker::None;
}
