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

int32 RelocationRules::ChooseNearestWorker(const TArray<FVector>& Positions, const TArray<bool>& Available, const FVector& Target)
{
	int32 Best = INDEX_NONE;
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		const double Distance = FVector::Dist2D(Positions[Index], Target);
		if (Available.IsValidIndex(Index) && Available[Index] && Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
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
