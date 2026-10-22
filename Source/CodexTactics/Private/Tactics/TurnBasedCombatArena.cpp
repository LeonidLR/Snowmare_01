// UTurnBasedCombatSubsystem: where the grid of a turn-based fight is laid out (TurnArenaRules; UE-only, game designer rule
// 2026-09-30, repeated 2026-10-22 after the Demo 01 lake play-test laid the grid inside the base building). Godot reference: none
// (Godot built its grid round the leader wherever he stood: Scenes/movements/main.gd _enter_turn_based_combat).

#include "Tactics/TurnBasedCombatSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"

#define LOCTEXT_NAMESPACE "TurnBasedArena"

namespace TurnArenaWorld
{
	TAutoConsoleVariable<int32> CVarTurnArenaDebug(TEXT("Codex.TurnArena.Debug"), 0,
		TEXT("1: every turn-based arena search logs the grid round the leader cell by cell (. ok, R roof, # obstacle, ~ not flat, N off the navmesh, _ no ground) and the faults of all candidates."));

	/** Overhead probe: a roof / ceiling / overhang this close above the ground marks the cell as indoors, cm. */
	constexpr float RoofProbeCm = 450.f;

	/** Samples one world cell centre (XY) round the reference height RefZ. */
	FTurnArenaSample SampleCell(UWorld& World, UNavigationSystemV1* Nav, const FVector2D& XY, float RefZ, float CellSize,
		const FCollisionQueryParams& Params)
	{
		FTurnArenaSample Sample;
		FHitResult Ground;
		const FVector Top(XY.X, XY.Y, RefZ + 250.f);
		if (!World.LineTraceSingleByChannel(Ground, Top, FVector(XY.X, XY.Y, RefZ - 400.f), ECC_Visibility, Params))
		{
			return Sample;
		}
		Sample.bGround = true;
		Sample.GroundZ = Ground.ImpactPoint.Z;
		FHitResult Roof;
		Sample.bRoofed = World.LineTraceSingleByChannel(Roof, FVector(XY.X, XY.Y, Sample.GroundZ + 60.f),
			FVector(XY.X, XY.Y, Sample.GroundZ + RoofProbeCm), ECC_Visibility, Params);
		// Anything that blocks a walking operative between knee and head height inside the cell (walls, vehicles, fences, crates).
		const FCollisionShape Box = FCollisionShape::MakeBox(FVector(CellSize * 0.35f, CellSize * 0.35f, 60.f));
		Sample.bBlocked = World.OverlapBlockingTestByChannel(FVector(XY.X, XY.Y, Sample.GroundZ + 90.f), FQuat::Identity, ECC_Pawn, Box, Params);
		if (Nav)
		{
			FNavLocation OnNav;
			Sample.bOnNav = Nav->ProjectPointToNavigation(FVector(XY.X, XY.Y, Sample.GroundZ), OnNav, FVector(50.f, 50.f, 120.f))
				&& FMath::Abs(OnNav.Location.Z - Sample.GroundZ) < 60.f;
		}
		else
		{
			Sample.bOnNav = true;
		}
		return Sample;
	}
}

FText UTurnBasedCombatSubsystem::DescribeArenaFault(ETurnArenaFault Fault)
{
	switch (Fault)
	{
	case ETurnArenaFault::Roof:
		return LOCTEXT("ArenaRoof", "âš ï¸ Turn-based combat only in the open - not inside or under a building! Move out onto open, level ground.");
	case ETurnArenaFault::Obstacle:
		return LOCTEXT("ArenaObstacle", "âš ï¸ Turn-based combat needs open ground - walls, vehicles or buildings in the way! Only barrels, barricades and turrets may stand on the battlefield.");
	case ETurnArenaFault::SquadOutside:
	case ETurnArenaFault::SquadOnBadCell:
		return LOCTEXT("ArenaSquad", "âš ï¸ Turn-based combat: gather the squad on open, level ground first!");
	default:
		return LOCTEXT("ArenaNotFlat", "âš ï¸ Turn-based combat can only start on level ground - not on high ground or in a pit!");
	}
}

bool UTurnBasedCombatSubsystem::FindTurnArena(FTurnArenaChoice& OutChoice) const
{
	OutChoice = FTurnArenaChoice();
	UWorld* World = GetWorld();
	const USquadSubsystem* SquadSystem = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = SquadSystem ? SquadSystem->GetLeader() : nullptr;
	if (!Leader)
	{
		return false;
	}
	const FTurnArenaConfig Config;
	// The allowed props and everybody on the battlefield never count as obstacles.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaSample), false);
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<AProximityMineActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ATripwireActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	const FVector Base = Leader->GetActorLocation();
	const float RefZ = Base.Z - Leader->GetSimpleCollisionHalfHeight();
	const int32 Half = Config.Cells / 2;
	// Lattice cell L (relative to the leader): world centre Base + (L + 0.5) * CellSize; a grid offset O covers L = O - Half .. O + Half - 1.
	TMap<FIntPoint, FTurnArenaSample> Cache;
	auto Lattice = [&](const FIntPoint& L) -> FTurnArenaSample
	{
		if (const FTurnArenaSample* Found = Cache.Find(L))
		{
			return *Found;
		}
		const FVector2D XY(Base.X + (L.X + 0.5f) * Config.CellSize, Base.Y + (L.Y + 0.5f) * Config.CellSize);
		return Cache.Add(L, TurnArenaWorld::SampleCell(*World, Nav, XY, RefZ, Config.CellSize, Params));
	};
	auto LatticeOf = [&](const FVector& Point)
	{
		return FIntPoint(FMath::FloorToInt((Point.X - Base.X) / Config.CellSize), FMath::FloorToInt((Point.Y - Base.Y) / Config.CellSize));
	};

	TArray<FIntPoint> UnitLattice;
	for (const AOperativeCharacter* Member : SquadSystem->GetMembers())
	{
		if (Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			UnitLattice.Add(LatticeOf(Member->GetActorLocation()));
		}
	}
	TArray<FIntPoint> EnemyLattice;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (!It->IsDying() && It->GetHealthComponent() && It->GetHealthComponent()->IsAlive()
			&& FVector::Dist2D(It->GetActorLocation(), Base) < Config.Cells * Config.CellSize)
		{
			EnemyLattice.Add(LatticeOf(It->GetActorLocation()));
		}
	}

	float BestScore = TNumericLimits<float>::Max();
	for (const FIntPoint& Offset : TurnArenaRules::CandidateOffsets(Config))
	{
		++OutChoice.Tried;
		const FIntPoint Origin = Offset - FIntPoint(Half, Half); // lattice of grid cell (0, 0)
		TArray<FIntPoint> UnitCells;
		for (const FIntPoint& Unit : UnitLattice)
		{
			UnitCells.Add(Unit - Origin);
		}
		const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate([&](const FIntPoint& Cell) { return Lattice(Cell + Origin); }, UnitCells, Config);
		if (Offset == FIntPoint::ZeroValue)
		{
			OutChoice.LeaderFault = Verdict.Fault;
			OutChoice.LeaderFaultCell = Verdict.Cell;
		}
		if (!Verdict.IsValid())
		{
			continue;
		}
		int32 EnemiesInside = 0;
		for (const FIntPoint& Enemy : EnemyLattice)
		{
			const FIntPoint Cell = Enemy - Origin;
			EnemiesInside += Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Config.Cells && Cell.Y < Config.Cells ? 1 : 0;
		}
		const float Score = TurnArenaRules::CandidateScore(Offset, EnemiesInside, Config);
		if (Score < BestScore)
		{
			BestScore = Score;
			OutChoice.bFound = true;
			OutChoice.Offset = Offset;
			OutChoice.GroundZ = Verdict.GroundZ;
			OutChoice.Centre = FVector(Base.X + Offset.X * Config.CellSize, Base.Y + Offset.Y * Config.CellSize, Verdict.GroundZ);
			OutChoice.BlockedBorder = Verdict.BlockedBorder;
		}
	}
	if (TurnArenaWorld::CVarTurnArenaDebug.GetValueOnGameThread() != 0)
	{
		const FIntPoint Origin(-Half, -Half);
		TArray<float> Heights;
		for (int32 X = 0; X < Config.Cells; ++X)
		{
			for (int32 Y = 0; Y < Config.Cells; ++Y)
			{
				const FTurnArenaSample Sample = Lattice(FIntPoint(X, Y) + Origin);
				if (Sample.bGround && !Sample.bRoofed && TurnArenaRules::IsCoreCell(FIntPoint(X, Y), Config))
				{
					Heights.Add(Sample.GroundZ);
				}
			}
		}
		Heights.Sort();
		const float Median = Heights.IsEmpty() ? RefZ : Heights[Heights.Num() / 2];
		UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena] round the leader (rows = X, columns = Y), median ground %.0f, core heights %.0f..%.0f:"), Median,
			Heights.IsEmpty() ? 0.f : Heights[0], Heights.IsEmpty() ? 0.f : Heights.Last());
		for (int32 X = 0; X < Config.Cells; ++X)
		{
			FString Row;
			for (int32 Y = 0; Y < Config.Cells; ++Y)
			{
				const FTurnArenaSample Sample = Lattice(FIntPoint(X, Y) + Origin);
				switch (TurnArenaRules::CellFault(Sample, Median, Config))
				{
				case ETurnArenaFault::None: Row += TEXT("."); break;
				case ETurnArenaFault::Roof: Row += TEXT("R"); break;
				case ETurnArenaFault::Obstacle: Row += TEXT("#"); break;
				case ETurnArenaFault::NotFlat: Row += FString::Printf(TEXT("%c"), Sample.GroundZ > Median ? TEXT('^') : TEXT('v')); break;
				case ETurnArenaFault::OffNavmesh: Row += TEXT("N"); break;
				default: Row += TEXT("_"); break;
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena]   %2d %s"), X, *Row);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena] %s: %d candidates, %d cells sampled; round the leader: %s at (%d, %d)%s"),
		OutChoice.bFound ? TEXT("arena found") : TEXT("NO arena"), OutChoice.Tried, Cache.Num(), TurnArenaRules::FaultName(OutChoice.LeaderFault),
		OutChoice.LeaderFaultCell.X, OutChoice.LeaderFaultCell.Y,
		OutChoice.bFound ? *FString::Printf(TEXT("; chosen centre offset (%d, %d) cells, ground %.0f, %d border obstacles"), OutChoice.Offset.X,
			OutChoice.Offset.Y, OutChoice.GroundZ, OutChoice.BlockedBorder.Num()) : TEXT(""));
	return OutChoice.bFound;
}

#undef LOCTEXT_NAMESPACE
