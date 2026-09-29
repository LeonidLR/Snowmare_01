#include "Tactics/ExposedZones.h"

int32 FExposedZones::GetQuadrant(const FIntPoint& Cell, const FIntPoint& GridSize)
{
	const bool bEast = Cell.X >= GridSize.X / 2;
	const bool bSouth = Cell.Y >= GridSize.Y / 2;
	return (bSouth ? 2 : 0) + (bEast ? 1 : 0);
}

FIntRect FExposedZones::GetQuadrantRect(int32 Quadrant, const FIntPoint& GridSize)
{
	const int32 MidX = GridSize.X / 2;
	const int32 MidY = GridSize.Y / 2;
	switch (Quadrant)
	{
	case 0: return FIntRect(0, 0, MidX, MidY);
	case 1: return FIntRect(MidX, 0, GridSize.X, MidY);
	case 2: return FIntRect(0, MidY, MidX, GridSize.Y);
	case 3: return FIntRect(MidX, MidY, GridSize.X, GridSize.Y);
	default: return FIntRect(0, 0, GridSize.X, GridSize.Y);
	}
}

FString FExposedZones::GetQuadrantName(int32 Quadrant)
{
	switch (Quadrant)
	{
	case 0: return TEXT("Северо-Запад");
	case 1: return TEXT("Северо-Восток");
	case 2: return TEXT("Юго-Запад");
	case 3: return TEXT("Юго-Восток");
	default: return TEXT("Неизвестный сектор");
	}
}

void FExposedZones::Reset()
{
	for (int32& Count : Turns)
	{
		Count = 0;
	}
	ReinforcementsSpawned = 0;
}

void FExposedZones::EvaluateRoster(const TArray<FRosterEntry>& Enemies)
{
	if (Enemies.IsEmpty())
	{
		return;
	}
	struct FTypeScore
	{
		EEnemyArchetype Type;
		int32 Count = 0;
		float TotalScore = 0.f;
		float Average() const { return TotalScore / FMath::Max(1, Count); }
	};
	TArray<FTypeScore> Types;
	for (const FRosterEntry& Entry : Enemies)
	{
		FTypeScore* Score = Types.FindByPredicate([&Entry](const FTypeScore& S) { return S.Type == Entry.Type; });
		if (!Score)
		{
			Score = &Types.Add_GetRef(FTypeScore{ Entry.Type });
		}
		++Score->Count;
		Score->TotalScore += Entry.MaxHealth * Entry.Damage * FMath::Max(1.f, Entry.MaxAP / 2.f);
	}
	// Weakest first (Godot sort_custom by avg_score; stable for equal scores).
	Types.StableSort([](const FTypeScore& A, const FTypeScore& B) { return A.Average() < B.Average(); });
	if (Types.Num() > 1)
	{
		Types.Pop(); // the wave's elite
	}
	Pool.Reset();
	for (const FTypeScore& Score : Types)
	{
		Pool.Add(Score.Type);
	}
}

FExposedZones::FTurnResult FExposedZones::EndSquadTurn(const bool Covered[NumQuadrants], int32 AliveEnemies,
	TFunctionRef<int32()> RollCount, TFunctionRef<int32(int32 Quadrant, int32 Count)> TrySpawn)
{
	FTurnResult Result;
	for (int32 Quadrant = 0; Quadrant < NumQuadrants; ++Quadrant)
	{
		if (Covered[Quadrant])
		{
			Turns[Quadrant] = 0;
		}
		else
		{
			++Turns[Quadrant];
			if (Turns[Quadrant] > TurnsBeforeBreach && ReinforcementsSpawned < MaxReinforcementsPerCombat && Result.BreachQuadrant == INDEX_NONE)
			{
				const int32 Allowed = FMath::Max(0, MaxCombatEnemies - AliveEnemies);
				const int32 Count = FMath::Min(RollCount(), Allowed);
				if (Count > 0)
				{
					const int32 Spawned = TrySpawn(Quadrant, Count);
					if (Spawned > 0)
					{
						++ReinforcementsSpawned;
						Turns[Quadrant] = 0;
						Result.BreachQuadrant = Quadrant;
						Result.Spawned = Spawned;
					}
				}
			}
		}
		Result.Turns[Quadrant] = Turns[Quadrant];
	}
	return Result;
}
