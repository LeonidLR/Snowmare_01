// CodexTactics.Tactics.TurnArena.* - where a turn-based fight may be laid out and which cells of its grid are open (TurnArenaRules;
// game designer decision 2026-10-26: the grid round the leader as before 2026-10-22, buildings / doors / ladders subtracted, a connected
// open area of 8 x 8 or 8 x 4 cells required).

#include "Misc/AutomationTest.h"
#include "Tactics/GorkyGridManager.h"
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

	FTurnArenaUnit Unit(int32 X, int32 Y, float FeetZ = 0.f)
	{
		FTurnArenaUnit Result;
		Result.Cell = FIntPoint(X, Y);
		Result.FeetZ = FeetZ;
		return Result;
	}

	/** Only the cells inside [Origin, Origin + Size) are open; the rest is solid (walls). */
	FTurnArenaVerdict OpenPatch(const FIntPoint& Origin, const FIntPoint& Size, const FTurnArenaConfig& Config)
	{
		return TurnArenaRules::Evaluate([Origin, Size](const FIntPoint& C)
		{
			FTurnArenaSample S = Open();
			S.bBlocked = C.X < Origin.X || C.Y < Origin.Y || C.X >= Origin.X + Size.X || C.Y >= Origin.Y + Size.Y;
			return S;
		}, { Unit(Origin.X, Origin.Y) }, Config);
	}
}

TURN_ARENA_TEST(FTurnArenaOpenYardTest, "OpenYardIsValidRoundTheLeader")
bool FTurnArenaOpenYardTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	// A yard with barrels (no samples: barrels never count), a crate and a closed shed (solid cells): obstacles, the fight still starts.
	const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate([](const FIntPoint& C)
	{
		FTurnArenaSample S = Open(5.f);
		S.bBlocked = (C.X >= 2 && C.X <= 3 && C.Y >= 2 && C.Y <= 3) || C == FIntPoint(10, 4); // a closed shed, a crate
		return S;
	}, { Unit(7, 7, 5.f), Unit(6, 8, 5.f), Unit(8, 6, 5.f) }, Config);
	TestTrue(TEXT("a yard with a closed shed and a crate: valid"), Verdict.IsValid());
	TestEqual(TEXT("its ground height"), Verdict.GroundZ, 5.f);
	TestEqual(TEXT("the shed is an obstacle (cover), not subtracted"), Verdict.ClassOf(FIntPoint(2, 2)), ETurnArenaCell::Obstacle);
	TestEqual(TEXT("the arena = every other cell"), Verdict.ArenaCells, 14 * 14 - 5);
	TestTrue(TEXT("the operatives keep their cells"), Verdict.UnitCells.Num() == 3 && Verdict.UnitCells[0] == FIntPoint(7, 7) && Verdict.UnitCells[2] == FIntPoint(8, 6));
	TestEqual(TEXT("the leader's own grid is the best candidate"), TurnArenaRules::CandidateOffsets(Config)[0], FIntPoint(0, 0));
	TestTrue(TEXT("an engaged enemy inside a shifted grid is worth a cell of distance"),
		TurnArenaRules::CandidateScore(FIntPoint(1, 0), 1, Config) <= TurnArenaRules::CandidateScore(FIntPoint(0, 0), 0, Config));
	return true;
}

TURN_ARENA_TEST(FTurnArenaFootprintTest, "BuildingFootprintIsSubtracted")
bool FTurnArenaFootprintTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	// A 4.5 x 6 m building rotated 30 deg: its outline (+40 cm walls / door frames) and nothing else.
	FTurnArenaFootprint Building;
	Building.Centre = FVector2D(1000.f, 500.f);
	Building.HalfExtent = FVector2D(225.f, 300.f);
	Building.YawDegrees = 30.f;
	TestTrue(TEXT("its centre is inside"), TurnArenaRules::IsInsideFootprint(Building, Building.Centre, 0.f));
	const FVector2D Corner = Building.Centre + FVector2D(220.f, 295.f).GetRotated(30.f);
	TestTrue(TEXT("a point by its rotated corner is inside"), TurnArenaRules::IsInsideFootprint(Building, Corner, 0.f));
	TestFalse(TEXT("the unrotated corner is not"), TurnArenaRules::IsInsideFootprint(Building, Building.Centre + FVector2D(220.f, 295.f) + FVector2D(60.f, -60.f), 0.f));
	const FVector2D WallLine = Building.Centre + FVector2D(255.f, 0.f).GetRotated(30.f);
	TestFalse(TEXT("30 cm past the wall: outside the bare outline"), TurnArenaRules::IsInsideFootprint(Building, WallLine, 0.f));
	TestTrue(TEXT("... inside it with the 40 cm wall margin"), TurnArenaRules::IsInsideFootprint(Building, WallLine, 40.f));
	FTurnArenaFootprint Far = Building;
	Far.Centre += FVector2D(700.f, 0.f);
	TestFalse(TEXT("outlines apart do not overlap"), TurnArenaRules::FootprintsOverlap(Building, Far));
	Far.Centre = Building.Centre + FVector2D(300.f, 0.f);
	TestTrue(TEXT("touching outlines overlap"), TurnArenaRules::FootprintsOverlap(Building, Far));

	// The grid: a building at X 9..13, Y 3..9 (cells inside its outline), its door at (8, 6) between two jambs.
	auto Yard = [](const FIntPoint& C)
	{
		FTurnArenaSample S = Open();
		S.bInFootprint = C.X >= 9 && C.Y >= 3 && C.Y <= 9;
		S.bSqueezed = C == FIntPoint(8, 6);
		return S;
	};
	const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate(Yard, { Unit(4, 6), Unit(10, 6), Unit(12, 3) }, Config);
	int32 Inside = 0;
	int32 OpenCells = 0;
	for (int32 X = 0; X < Config.Cells; ++X)
	{
		for (int32 Y = 0; Y < Config.Cells; ++Y)
		{
			const bool bInBuilding = X >= 9 && Y >= 3 && Y <= 9;
			Inside += bInBuilding && Verdict.ClassOf(FIntPoint(X, Y)) == ETurnArenaCell::Building && !Verdict.IsArena(FIntPoint(X, Y)) ? 1 : 0;
			OpenCells += !bInBuilding && Verdict.IsArena(FIntPoint(X, Y)) ? 1 : 0;
		}
	}
	TestTrue(TEXT("the yard round the building: valid"), Verdict.IsValid());
	TestEqual(TEXT("every cell of the building is subtracted"), Inside, 5 * 7);
	TestEqual(TEXT("its door is subtracted too"), Verdict.ClassOf(FIntPoint(8, 6)), ETurnArenaCell::Doorway);
	TestTrue(TEXT("the door and the building are no arena cells"), TurnArenaRules::IsSubtracted(ETurnArenaCell::Doorway) && TurnArenaRules::IsSubtracted(ETurnArenaCell::Building));
	TestEqual(TEXT("the rest is the arena"), OpenCells, 14 * 14 - 5 * 7 - 1);
	TestTrue(TEXT("an operative inside by the door walks out through it (onto the arena, 2 cells)"),
		Verdict.UnitCells.Num() == 3 && Verdict.IsArena(Verdict.UnitCells[1]) && (Verdict.UnitCells[1] - FIntPoint(10, 6)).SizeSquared() <= 8);
	TestTrue(TEXT("... one in the corner by the wall goes out the nearest way"), Verdict.UnitCells.Num() == 3 && Verdict.IsArena(Verdict.UnitCells[2]));
	// Deep inside a big building (more than 3 cells from any open cell): no fight.
	const FTurnArenaVerdict Deep = TurnArenaRules::Evaluate([](const FIntPoint& C)
	{
		FTurnArenaSample S = Open();
		S.bRoofed = C.X >= 5; // a hall over the grid's east part
		return S;
	}, { Unit(2, 7), Unit(12, 7) }, Config);
	TestEqual(TEXT("an operative 7 cells deep inside a hall: refused (inside a building)"), Deep.Fault, ETurnArenaFault::InsideBuilding);
	TestEqual(TEXT("the leader inside a hall leaving no 8 x 4 outside: refused as inside a building (not \"no room\")"),
		TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bRoofed = C.X >= 3 && C.X <= 10; return S; },
			{ Unit(7, 7) }, Config).Fault, ETurnArenaFault::InsideBuilding);
	// A roofless walled shed: 7 of 8 wall probes hit -> a building; 6 (a dead-end alley) does not.
	FTurnArenaSample Shed = Open();
	Shed.EnclosureHits = 7;
	TestEqual(TEXT("a walled interior without a roof is a building"), TurnArenaRules::ClassifyCell(Shed, 0.f, Config), ETurnArenaCell::Building);
	Shed.EnclosureHits = 6;
	TestEqual(TEXT("an alley open at both ends is not"), TurnArenaRules::ClassifyCell(Shed, 0.f, Config), ETurnArenaCell::Walkable);
	TestTrue(TEXT("a squeezed cell far from any building (between two crates) stays open"),
		TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bSqueezed = C == FIntPoint(3, 3); return S; },
			{ Unit(7, 7) }, Config).ClassOf(FIntPoint(3, 3)) == ETurnArenaCell::Walkable);
	return true;
}

TURN_ARENA_TEST(FTurnArenaMinimumTest, "MinimumOpenArea8x8Or8x4")
bool FTurnArenaMinimumTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	TestTrue(TEXT("8 x 8 open: valid"), OpenPatch(FIntPoint(3, 3), FIntPoint(8, 8), Config).IsValid());
	const FTurnArenaVerdict Long = OpenPatch(FIntPoint(2, 5), FIntPoint(8, 4), Config);
	TestTrue(TEXT("8 x 4 open: valid"), Long.IsValid());
	TestEqual(TEXT("... the 8 x 4 rectangle is found"), Long.RectSize, FIntPoint(8, 4));
	TestTrue(TEXT("4 x 8 open (the other orientation): valid"), OpenPatch(FIntPoint(5, 2), FIntPoint(4, 8), Config).IsValid());
	TestEqual(TEXT("7 x 4 open: too small"), OpenPatch(FIntPoint(2, 5), FIntPoint(7, 4), Config).Fault, ETurnArenaFault::TooSmall);
	TestEqual(TEXT("8 x 3 open: too small"), OpenPatch(FIntPoint(2, 5), FIntPoint(8, 3), Config).Fault, ETurnArenaFault::TooSmall);
	TestEqual(TEXT("an L of 8 x 3 + 3 x 8 (enough cells, no rectangle): too small"),
		TurnArenaRules::Evaluate([](const FIntPoint& C)
		{
			FTurnArenaSample S = Open();
			S.bBlocked = !((C.X >= 2 && C.X < 10 && C.Y >= 2 && C.Y < 5) || (C.X >= 2 && C.X < 5 && C.Y >= 2 && C.Y < 10));
			return S;
		}, { Unit(3, 3) }, Config).Fault, ETurnArenaFault::TooSmall);
	// Two open areas split by a wall: the one holding the squad is the arena, the other is not.
	const FTurnArenaVerdict Split = TurnArenaRules::Evaluate([](const FIntPoint& C) { FTurnArenaSample S = Open(); S.bBlocked = C.X == 6; return S; },
		{ Unit(10, 7), Unit(11, 4) }, Config);
	TestTrue(TEXT("a yard split by a wall: valid on the squad's side"), Split.IsValid() && Split.IsArena(FIntPoint(10, 7)) && !Split.IsArena(FIntPoint(2, 7)));
	// Regions move like the grid: diagonal past one open orthogonal cell, never squeezed between two closed corners.
	TArray<bool> Diagonal;
	Diagonal.Init(false, 9);
	Diagonal[0 * 3 + 0] = true;
	Diagonal[1 * 3 + 1] = true;
	const TArray<int32> Apart = TurnArenaRules::FindRegions(Diagonal, 3);
	TestTrue(TEXT("two cells touching only at a corner (both sides closed) are apart"), Apart[0] != Apart[4]);
	Diagonal[0 * 3 + 1] = true;
	const TArray<int32> Joined = TurnArenaRules::FindRegions(Diagonal, 3);
	TestTrue(TEXT("... with one side open they are one area"), Joined[0] == Joined[4] && Joined[0] >= 0);
	// The squad's feet: the pre-2026-10-22 level rule; operatives far outside: refused.
	TestEqual(TEXT("an operative 1 m up (on a platform): refused (not level)"),
		TurnArenaRules::Evaluate([](const FIntPoint&) { return Open(); }, { Unit(7, 7), Unit(6, 6, 100.f) }, Config).Fault, ETurnArenaFault::NotFlat);
	TestTrue(TEXT("an operative 2 cells outside the grid: moved onto it"),
		TurnArenaRules::Evaluate([](const FIntPoint&) { return Open(); }, { Unit(7, 7), Unit(15, 7) }, Config).IsValid());
	TestEqual(TEXT("one 8 cells outside: refused (gather the squad)"),
		TurnArenaRules::Evaluate([](const FIntPoint&) { return Open(); }, { Unit(7, 7), Unit(21, 7) }, Config).Fault, ETurnArenaFault::SquadOutside);
	return true;
}

TURN_ARENA_TEST(FTurnArenaLadderTest, "LaddersAreExcluded")
bool FTurnArenaLadderTest::RunTest(const FString&)
{
	using namespace TurnArenaTest;
	const FTurnArenaConfig Config;
	// The foot of a ladder 10 cm from a cell corner: the four cells round the corner are the ladder's; the middle of a cell: only it.
	const TArray<FIntPoint> AtCorner = TurnArenaRules::CellsNearPoint(FVector2D(310.f, 455.f), FVector2D::ZeroVector, 150.f, 50.f);
	TestEqual(TEXT("a foot by a cell corner: 4 cells"), AtCorner.Num(), 4);
	TestTrue(TEXT("... (2, 3) among them"), AtCorner.Contains(FIntPoint(2, 3)) && AtCorner.Contains(FIntPoint(1, 2)));
	const TArray<FIntPoint> Middle = TurnArenaRules::CellsNearPoint(FVector2D(225.f, 225.f), FVector2D::ZeroVector, 150.f, 50.f);
	TestTrue(TEXT("a foot in a cell's middle: that cell only"), Middle.Num() == 1 && Middle[0] == FIntPoint(1, 1));
	const TArray<FIntPoint> Negative = TurnArenaRules::CellsNearPoint(FVector2D(-75.f, -75.f), FVector2D::ZeroVector, 150.f, 50.f);
	TestTrue(TEXT("left of / below the lattice base: cell (-1, -1)"), Negative.Num() == 1 && Negative[0] == FIntPoint(-1, -1));
	// A ladder against a wall in the yard: its foot / rung cells are subtracted, the fight still starts.
	const FTurnArenaVerdict Verdict = TurnArenaRules::Evaluate([](const FIntPoint& C)
	{
		FTurnArenaSample S = Open();
		S.bLadder = C.X == 11 && (C.Y == 5 || C.Y == 6);
		S.bBlocked = C.X == 12; // the wall it leans on
		return S;
	}, { Unit(7, 7), Unit(11, 5) }, Config);
	TestTrue(TEXT("a yard with a ladder: valid"), Verdict.IsValid());
	TestEqual(TEXT("its foot is a ladder cell"), Verdict.ClassOf(FIntPoint(11, 5)), ETurnArenaCell::Ladder);
	TestTrue(TEXT("... subtracted, no arena cell"), TurnArenaRules::IsSubtracted(ETurnArenaCell::Ladder) && !Verdict.IsArena(FIntPoint(11, 6)));
	TestTrue(TEXT("an operative at its foot is moved off it"), Verdict.UnitCells.Num() == 2 && Verdict.IsArena(Verdict.UnitCells[1]));
	FTurnArenaSample LadderInBuilding = Open();
	LadderInBuilding.bLadder = true;
	LadderInBuilding.bRoofed = true;
	TestEqual(TEXT("a ladder cell wins over the building round it"), TurnArenaRules::ClassifyCell(LadderInBuilding, 0.f, Config), ETurnArenaCell::Ladder);
	return true;
}

TURN_ARENA_TEST(FTurnArenaClosedCellTest, "ClosedCellsStayClosed")
bool FTurnArenaClosedCellTest::RunTest(const FString&)
{
	// The grid side: a building's cell closed for the fight never opens again, whoever stood on it (a climber off a roof).
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->SetupDefault(FVector::ZeroVector);
	const FIntPoint Cell(3, 4);
	Grid->SetClosed(Cell);
	TestFalse(TEXT("closed: not walkable"), Grid->IsCellWalkable(Cell));
	TestTrue(TEXT("... and closed"), Grid->IsCellClosed(Cell));
	Grid->SetOccupant(Cell, nullptr, EGorkyOccupantType::Squad);
	Grid->ClearOccupant(Cell);
	TestFalse(TEXT("a unit left it: still not walkable"), Grid->IsCellWalkable(Cell));
	Grid->SetOccupant(Cell, nullptr, EGorkyOccupantType::None);
	TestFalse(TEXT("an empty occupant does not open it"), Grid->IsCellWalkable(Cell));
	TestTrue(TEXT("no path into it"), Grid->FindPath(FIntPoint(3, 1), Cell, 20).IsEmpty());
	TestFalse(TEXT("never a free cell"), Grid->FindNearestFreeCell(Cell) == Cell);
	Grid->ClearOccupant(FIntPoint(5, 5));
	TestTrue(TEXT("an ordinary cell clears as before"), Grid->IsCellWalkable(FIntPoint(5, 5)));
	// SnapToArena: out through the door before through the wall.
	const FIntPoint Out = TurnArenaRules::SnapToArena(FIntPoint(4, 6), 10, 3,
		[](const FIntPoint& C) { return C.X <= 2; },                       // the yard west of the building
		[](const FIntPoint& C) { return C.X >= 4 || C == FIntPoint(3, 8); }); // its floor and its door (3, 8); (3, *) else is wall
	TestEqual(TEXT("a unit inside walks out through the door"), Out, FIntPoint(2, 7));
	TestEqual(TEXT("none within reach: (-1, -1)"),
		TurnArenaRules::SnapToArena(FIntPoint(9, 5), 10, 3, [](const FIntPoint& C) { return C.X <= 2; }, [](const FIntPoint&) { return true; }), FIntPoint(-1, -1));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
