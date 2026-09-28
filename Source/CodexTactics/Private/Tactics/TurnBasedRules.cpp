#include "Tactics/TurnBasedRules.h"
#include "Data/WeaponDataAsset.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"

namespace
{
	// Godot gorky17_enums.gd ALL_DIRS / CARDINAL_DIRS order.
	const FIntPoint TurnAllDirs[] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 }, { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };

	void TurnCastRay(const UGorkyGridManager& Grid, const FIntPoint& From, const FIntPoint& Dir, int32 MaxRange, const UWeaponDataAsset* Weapon,
		EOperativeStance Stance, const FTurnBasedBalance& Balance, TMap<FIntPoint, FTurnBasedAttackCell>& Out)
	{
		for (int32 Step = 1; Step <= MaxRange; ++Step)
		{
			const FIntPoint Cell = From + Dir * Step;
			if (!Grid.IsValidCell(Cell))
			{
				break;
			}
			FTurnBasedAttackCell& Info = Out.Add(Cell);
			Info.Distance = Step;
			Info.MaxRange = MaxRange;
			Info.HitChance = TurnBasedRules::CalculateHitChance(Weapon, Step, Stance, Balance);
			Info.ProjectedDamage = TurnBasedRules::GetDamageForDistance(Weapon, Step, Balance.SquadBaseDamage);
			const EGorkyOccupantType Type = Grid.GetOccupantType(Cell);
			if (Type != EGorkyOccupantType::None && Type != EGorkyOccupantType::Mine)
			{
				break; // an obstacle or another unit stops the ray
			}
		}
	}
}

int32 TurnBasedRules::CellDistance(const FIntPoint& From, const FIntPoint& To)
{
	return FMath::Max(FMath::Abs(To.X - From.X), FMath::Abs(To.Y - From.Y));
}

float TurnBasedRules::FallbackHitChance(int32 DistanceCells)
{
	static const float Chances[] = { 0.95f, 0.85f, 0.75f, 0.65f, 0.50f };
	return Chances[FMath::Clamp(DistanceCells - 1, 0, 4)];
}

float TurnBasedRules::CalculateHitChance(const UWeaponDataAsset* Weapon, int32 DistanceCells, EOperativeStance Stance, const FTurnBasedBalance& Balance)
{
	if (DistanceCells <= 0)
	{
		return 1.f;
	}
	const float Base = Weapon ? Weapon->GetHitChanceForDistance(DistanceCells) : FallbackHitChance(DistanceCells);
	const float StanceBonus = Stance == EOperativeStance::Crouching ? Balance.CrouchAccuracyBonus
		: (Stance == EOperativeStance::Prone ? Balance.ProneAccuracyBonus : 0.f);
	return FMath::Clamp(Base + StanceBonus, 0.05f, 0.99f);
}

float TurnBasedRules::CalculateTurretHitChance(int32 DistanceCells)
{
	return FMath::Clamp(FallbackHitChance(DistanceCells), 0.05f, 0.99f);
}

float TurnBasedRules::GetDamageForDistance(const UWeaponDataAsset* Weapon, int32 DistanceCells, float FallbackDamage)
{
	if (!Weapon)
	{
		return FallbackDamage;
	}
	const TArray<float>& Multipliers = Weapon->DistanceDamageMultipliers;
	const float Multiplier = Multipliers.IsEmpty() ? 1.f : Multipliers[FMath::Clamp(DistanceCells - 1, 0, Multipliers.Num() - 1)];
	return Weapon->BaseDamage * Multiplier;
}

TMap<FIntPoint, FTurnBasedAttackCell> TurnBasedRules::GetWeaponAttackCells(const UGorkyGridManager& Grid, const FIntPoint& From,
	const UWeaponDataAsset* Weapon, EOperativeStance Stance, const FTurnBasedBalance& Balance)
{
	TMap<FIntPoint, FTurnBasedAttackCell> Result;
	const EAttackShape Shape = Weapon ? Weapon->AttackShape : EAttackShape::Rays8;
	const int32 MaxRange = Weapon ? Weapon->MaxRangeCells : 5;
	switch (Shape)
	{
	case EAttackShape::Rays8:
		for (const FIntPoint& Dir : TurnAllDirs)
		{
			TurnCastRay(Grid, From, Dir, MaxRange, Weapon, Stance, Balance, Result);
		}
		break;
	case EAttackShape::Rays4:
		for (int32 Index = 0; Index < 4; ++Index)
		{
			TurnCastRay(Grid, From, TurnAllDirs[Index], MaxRange, Weapon, Stance, Balance, Result);
		}
		break;
	case EAttackShape::MeleeAdj:
		for (const FIntPoint& Dir : TurnAllDirs)
		{
			const FIntPoint Cell = From + Dir;
			if (Grid.IsValidCell(Cell))
			{
				FTurnBasedAttackCell& Info = Result.Add(Cell);
				Info.Distance = 1;
				Info.MaxRange = 1;
				Info.HitChance = CalculateHitChance(Weapon, 1, Stance, Balance);
				Info.ProjectedDamage = GetDamageForDistance(Weapon, 1, Balance.SquadBaseDamage);
			}
		}
		break;
	case EAttackShape::FreeTarget:
		for (int32 DX = -MaxRange; DX <= MaxRange; ++DX)
		{
			for (int32 DY = -MaxRange; DY <= MaxRange; ++DY)
			{
				const FIntPoint Cell = From + FIntPoint(DX, DY);
				if ((DX == 0 && DY == 0) || !Grid.IsValidCell(Cell))
				{
					continue;
				}
				const int32 Distance = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
				FTurnBasedAttackCell& Info = Result.Add(Cell);
				Info.Distance = Distance;
				Info.MaxRange = MaxRange;
				Info.HitChance = CalculateHitChance(Weapon, Distance, Stance, Balance);
				Info.ProjectedDamage = GetDamageForDistance(Weapon, Distance, Balance.SquadBaseDamage);
			}
		}
		break;
	}
	return Result;
}
