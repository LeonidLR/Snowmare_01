// CodexTactics.Tactics.TurnArena.* - where a turn-based fight may be laid out (TurnArenaRules; game designer rule 2026-09-30,
// repeated 2026-10-22 after the Demo 01 lake grid was laid inside the base building: "only on absolutely flat open ground, no
// obstacles in the middle except barrels, barricades and turrets").

#include "Misc/AutomationTest.h"
#include "Tactics/TurnArenaRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TURN_ARENA_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.TurnArena." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TurnArenaTest
{
	FTurnArenaSample Open(float Z = 0.f)
	{
		FTurnArenaSample Sample;
		Sample.bGround = true;
		Sample.GroundZ = Z;
		Sample.bOnNav = true;
		return Sample;
	}

	/** A world of cells (lattice) with a building (roofed + walls) at X >= BuildingX. */
	struct FWorld
	{
		int32 BuildingX = 1000;
		FTurnArenaSample At(const FIntPoint& L) const
		{
			FTurnArenaSample Sample = Open();
			if (L.X >= BuildingX)
			{
				Sample.bRoofed = true; // indoors: flat floor, roof overhead
				Sample.bBlocked = L.X == BuildingX; // its wall
			}
			return Sample;
		}
	};
}

TURN_ARENA_TEST(FTurnArenaOpenGroundTest, "OpenFlatGroundIsValid")
bool FTurnArenaOpenGroundTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	const TArray<FIntPoint> Squad = { FIntPoint(7, 7), FIntPoint(6, 8), FIntPoint(8, 6) };
	const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate([](const FIntPoint&) { return Open(5.f); }, Squad, Config);
	TestTrue(TEXT("flat, open, navigable: valid"), Verdict.IsValid());
	TestEqual(TEXT("its ground height"), Verdict.GroundZ, 5.f);
	TestTrue(TEXT("no border obstacles"), Verdict.BlockedBorder.IsEmpty());
	return true;
}

TURN_ARENA_TEST(FTurnArenaRejectsTest, "RejectsBuildingsSlopesAndScatteredSquads")
bool FTurnArenaRejectsTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	const TArray<FIntPoint> Squad = { FIntPoint(7, 7) };
	auto Fault = [&](TFunctionRef<FTurnArenaSample(const FIntPoint&)> SampleAt, const TArray<FIntPoint>& Units)
	{
		return TurnArenaRules::Evaluate(SampleAt, Units, Config).Fault;
	};
	TestEqual(TEXT("a roof over a core cell (inside the building): refused"),
		Fault([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bRoofed = C == FIntPoint(5, 9); return S; }, Squad), ETurnArenaFault::Roof);
	TestEqual(TEXT("a wall in the middle: refused"),
		Fault([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bBlocked = C.X == 9 && C.Y > 3; return S; }, Squad), ETurnArenaFault::Obstacle);
	TestEqual(TEXT("a 50 cm step in the core: refused (not absolutely flat)"),
		Fault([](const FIntPoint& C) { return Open(C.X >= 8 ? 50.f : 0.f); }, Squad), ETurnArenaFault::NotFlat);
	TestEqual(TEXT("20 cm undulation: still flat"),
		Fault([](const FIntPoint& C) { return Open((C.X + C.Y) % 2 ? 20.f : 0.f); }, Squad), ETurnArenaFault::None);
	TestEqual(TEXT("off the navmesh in the core: refused"),
		Fault([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bOnNav = C != FIntPoint(4, 4); return S; }, Squad), ETurnArenaFault::OffNavmesh);
	TestEqual(TEXT("an operative outside the grid: refused"),
		Fault([](const FIntPoint&) { return Open(); }, { FIntPoint(7, 7), FIntPoint(20, 7) }), ETurnArenaFault::SquadOutside);
	TestEqual(TEXT("an operative on the outermost cells: refused"),
		Fault([](const FIntPoint&) { return Open(); }, { FIntPoint(0, 7) }), ETurnArenaFault::SquadOutside);
	// A wall on the border ring is fine: it becomes an obstacle cell.
	const FTurnArenaVerdict Edge = TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bBlocked = C.X == 13; return S; },
		Squad, Config);
	TestTrue(TEXT("a wall along the outer ring: valid"), Edge.IsValid());
	TestEqual(TEXT("... its 14 cells are obstacles"), Edge.BlockedBorder.Num(), 14);
	TestTrue(TEXT("... an operative pressed against it (a free cell next to him): valid, he is placed there"),
		TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bBlocked = C.X == 12; return S; }, { FIntPoint(12, 5) }, Config).IsValid());
	TestEqual(TEXT("... an operative deep in the blocked ring (nothing free next to him): refused"),
		TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bBlocked = C.X >= 11; return S; }, { FIntPoint(12, 5) }, Config).Fault,
		ETurnArenaFault::SquadOnBadCell);
	return true;
}

TURN_ARENA_TEST(FTurnArenaSearchTest, "SearchMovesTheGridOffTheBuilding")
bool FTurnArenaSearchTest::RunTest(const FString&)
{
	// The lake case: the squad 3 m in front of the base building's wall; the grid round the leader would reach 7.5 m into it.
	using namespace TurnArenaTest;
	FTurnArenaConfig Config;
	FWorld World;
	World.BuildingX = 2; // lattice X >= 2 is the building (the leader stands in lattice cell (0, 0))
	const TArray<FIntPoint> SquadLattice = { FIntPoint(0, 0), FIntPoint(-1, 1), FIntPoint(-1, -1) };
	const int32 Half = Config.Cells / 2;
	TOptional<FIntPoint> Chosen;
	TOptional<ETurnArenaFault> AtLeader;
	for (const FIntPoint& Offset : TurnArenaRules::CandidateOffsets(Config))
	{
		const FIntPoint Origin = Offset - FIntPoint(Half, Half);
		TArray<FIntPoint> Units;
		for (const FIntPoint& Unit : SquadLattice)
		{
			Units.Add(Unit - Origin);
		}
		const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate([&](const FIntPoint& Cell) { return World.At(Cell + Origin); }, Units, Config);
		if (Offset == FIntPoint::ZeroValue)
		{
			AtLeader = Verdict.Fault;
		}
		if (Verdict.IsValid() && !Chosen.IsSet())
		{
			Chosen = Offset; // nearest first
		}
	}
	TestTrue(TEXT("round the leader: refused (the building in the core)"), AtLeader.IsSet() && AtLeader.GetValue() != ETurnArenaFault::None);
	TestTrue(TEXT("a grid further from the building is found"), Chosen.IsSet());
	if (Chosen.IsSet())
	{
		const int32 MaxCoreLatticeX = Chosen->X - Half + Config.Cells - Config.BorderCells - 1;
		TestTrue(FString::Printf(TEXT("... its core ends before the wall (core max lattice X %d < %d)"), MaxCoreLatticeX, World.BuildingX),
			MaxCoreLatticeX < World.BuildingX);
		TestTrue(TEXT("... and still holds the squad"), Chosen->X - Half + 1 <= -1 && Chosen->X + Half - 2 >= 0);
	}
	TestEqual(TEXT("candidates: nearest first"), TurnArenaRules::CandidateOffsets(Config)[0], FIntPoint(0, 0));
	TestTrue(TEXT("an engaged enemy inside is worth a cell of distance"),
		TurnArenaRules::CandidateScore(FIntPoint(1, 0), 1, Config) <= TurnArenaRules::CandidateScore(FIntPoint(0, 0), 0, Config));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
