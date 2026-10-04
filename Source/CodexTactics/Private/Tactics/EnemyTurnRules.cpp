#include "Tactics/EnemyTurnRules.h"

FEnemyTurnProfile EnemyTurnRules::ProfileFor(EEnemyArchetype Archetype)
{
	// First-guess numbers (user decision 2026-10-04: per-archetype turns); tuned like the rest of the balance.
	FEnemyTurnProfile Profile;
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound:
		Profile.BackArcWeight = 0.5f; // a pack hunter goes for the flank
		break;
	case EEnemyArchetype::Cutter:
		Profile.APScale = 7.f / 6.f;
		Profile.DamageScale = 1.2f;
		Profile.BackArcWeight = 1.f; // the ambusher: the back whenever it can
		break;
	case EEnemyArchetype::Frostbitten:
		Profile.APScale = 4.f / 6.f;
		Profile.DamageScale = 1.1f;
		Profile.bHitAndRun = false; // stays on its victim
		break;
	case EEnemyArchetype::Brute:
		Profile.APScale = 5.f / 6.f;
		Profile.DamageScale = 1.8f;
		Profile.AttackAPCost = 3;
		Profile.bHitAndRun = false;
		break;
	case EEnemyArchetype::Spitter:
	case EEnemyArchetype::CryoDrone:
		Profile.DamageScale = 0.8f;
		Profile.AttackAPCost = 3;
		Profile.bRanged = true;
		Profile.MinRange = 2;
		Profile.MaxRange = 6;
		Profile.PreferredMin = 3;
		Profile.PreferredMax = 5;
		Profile.BaseHitChance = 0.75f;
		Profile.HitFalloffPerCell = 0.06f;
		break;
	case EEnemyArchetype::Marksman:
		Profile.APScale = 5.f / 6.f;
		Profile.DamageScale = 1.6f;
		Profile.AttackAPCost = 4;
		Profile.bRanged = true;
		Profile.MinRange = 3;
		Profile.MaxRange = 10;
		Profile.PreferredMin = 5;
		Profile.PreferredMax = 9;
		Profile.BaseHitChance = 0.8f;
		Profile.HitFalloffPerCell = 0.03f;
		break;
	default:
		break;
	}
	return Profile;
}

int32 EnemyTurnRules::MaxAP(const FEnemyTurnProfile& Profile, int32 BalanceMaxAP)
{
	return FMath::Max(1, FMath::RoundToInt(BalanceMaxAP * Profile.APScale));
}

float EnemyTurnRules::BaseDamage(const FEnemyTurnProfile& Profile, float BalanceBaseDamage)
{
	return BalanceBaseDamage * Profile.DamageScale;
}

float EnemyTurnRules::RangedHitChance(const FEnemyTurnProfile& Profile, int32 DistanceCells, EOperativeStance TargetStance, bool bTargetInCover,
	float CoverMultiplier)
{
	float Chance = Profile.BaseHitChance - Profile.HitFalloffPerCell * FMath::Max(0, DistanceCells - Profile.MinRange);
	Chance *= TargetStance == EOperativeStance::Prone ? 0.6f : (TargetStance == EOperativeStance::Crouching ? 0.8f : 1.f);
	Chance *= bTargetInCover ? CoverMultiplier : 1.f;
	return FMath::Clamp(Chance, 0.1f, 0.9f);
}

int32 EnemyTurnRules::ChooseMeleeCell(const FEnemyTurnProfile& Profile, const TArray<FEnemyMeleeCell>& Cells, int32 AP)
{
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	int32 Nearest = INDEX_NONE;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FEnemyMeleeCell& Cell = Cells[Index];
		if (Cell.PathCost < 0)
		{
			continue;
		}
		if (Nearest == INDEX_NONE || Cell.PathCost < Cells[Nearest].PathCost)
		{
			Nearest = Index;
		}
		if (Cell.PathCost + Profile.AttackAPCost > AP)
		{
			continue;
		}
		// Arc bonus (multiplier above the front's 1) weighted by the profile; one AP of walking costs a little.
		const float Score = (Cell.ArcMultiplier - 1.f) * Profile.BackArcWeight - 0.02f * Cell.PathCost;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best != INDEX_NONE ? Best : Nearest;
}

int32 EnemyTurnRules::ChooseFiringCell(const FEnemyTurnProfile& Profile, const TArray<FEnemyFiringCell>& Cells, int32 AP)
{
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FEnemyFiringCell& Cell = Cells[Index];
		if (Cell.PathCost < 0 || Cell.PathCost + Profile.AttackAPCost > AP || !Cell.bLineOfFire
			|| Cell.Distance < Profile.MinRange || Cell.Distance > Profile.MaxRange)
		{
			continue;
		}
		const bool bPreferred = Cell.Distance >= Profile.PreferredMin && Cell.Distance <= Profile.PreferredMax;
		const float Score = Cell.HitChance + (bPreferred ? 0.1f : 0.f) - (Cell.bNextToOperative ? 0.5f : 0.f) - 0.01f * Cell.PathCost;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}
