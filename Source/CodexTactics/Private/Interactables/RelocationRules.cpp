#include "Interactables/RelocationRules.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRelocationRealTimeCombat(TEXT("Codex.Relocation.RealTimeCombat"), 1,
		TEXT("1 (default, user decision 2026-10-26): objects / barricades can be moved in the real-time fight too; 0: the old rule (only in the tactical pause)."));
}

bool RelocationRules::IsRealTimeCombatRelocationEnabled()
{
	return CVarRelocationRealTimeCombat.GetValueOnAnyThread() != 0;
}

bool RelocationRules::CanRelocateNow(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bLeaderZoneSolo, bool bRealTimeCombat)
{
	if (Phase != ECodexGamePhase::WaveCombat || Mode == ECodexCombatMode::TacticalPause || bLeaderZoneSolo)
	{
		return true;
	}
	return bRealTimeCombat && Mode == ECodexCombatMode::RealTime;
}

bool RelocationRules::ShouldDropActiveTask(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bLeaderZoneSolo, bool bPlannedInPause, bool bRealTimeCombat)
{
	if (CanRelocateNow(Phase, Mode, bLeaderZoneSolo, bRealTimeCombat))
	{
		return false;
	}
	return !(bPlannedInPause && Phase == ECodexGamePhase::WaveCombat && Mode == ECodexCombatMode::RealTime);
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
