// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyFacingAndVectorConversionTest,
	"CodexTactics.Tactics.FacingAndVectorConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyFacingAndVectorConversionTest::RunTest(const FString& Parameters)
{
	// Test Facing to Vector
	TestEqual(TEXT("North vector"), FGorky17Utils::FacingToVector(EGorkyFacing::North), FIntPoint(0, -1));
	TestEqual(TEXT("East vector"), FGorky17Utils::FacingToVector(EGorkyFacing::East), FIntPoint(1, 0));
	TestEqual(TEXT("South vector"), FGorky17Utils::FacingToVector(EGorkyFacing::South), FIntPoint(0, 1));
	TestEqual(TEXT("West vector"), FGorky17Utils::FacingToVector(EGorkyFacing::West), FIntPoint(-1, 0));
	TestEqual(TEXT("NorthEast vector"), FGorky17Utils::FacingToVector(EGorkyFacing::NorthEast), FIntPoint(1, -1));
	TestEqual(TEXT("SouthEast vector"), FGorky17Utils::FacingToVector(EGorkyFacing::SouthEast), FIntPoint(1, 1));
	TestEqual(TEXT("SouthWest vector"), FGorky17Utils::FacingToVector(EGorkyFacing::SouthWest), FIntPoint(-1, 1));
	TestEqual(TEXT("NorthWest vector"), FGorky17Utils::FacingToVector(EGorkyFacing::NorthWest), FIntPoint(-1, -1));

	// Test Vector to Facing
	TestEqual(TEXT("Vector (0, -1) to North"), FGorky17Utils::VectorToFacing(FIntPoint(0, -1)), EGorkyFacing::North);
	TestEqual(TEXT("Vector (1, 0) to East"), FGorky17Utils::VectorToFacing(FIntPoint(1, 0)), EGorkyFacing::East);
	TestEqual(TEXT("Vector (0, 1) to South"), FGorky17Utils::VectorToFacing(FIntPoint(0, 1)), EGorkyFacing::South);
	TestEqual(TEXT("Vector (-1, 0) to West"), FGorky17Utils::VectorToFacing(FIntPoint(-1, 0)), EGorkyFacing::West);
	TestEqual(TEXT("Vector (1, 1) to SouthEast"), FGorky17Utils::VectorToFacing(FIntPoint(1, 1)), EGorkyFacing::SouthEast);
	TestEqual(TEXT("Vector (-1, -1) to NorthWest"), FGorky17Utils::VectorToFacing(FIntPoint(-1, -1)), EGorkyFacing::NorthWest);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyArcZoneCalculationTest,
	"CodexTactics.Tactics.ArcZoneCalculation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyArcZoneCalculationTest::RunTest(const FString& Parameters)
{
	// Defender at (5, 5), Attacker at (5, 3) (directly to North of defender)
	const FIntPoint Defender(5, 5);
	const FIntPoint Attacker(5, 3);

	// Case 1: Defender faces North (towards attacker) -> Front Arc (1.0x damage, 1.0x armor)
	FGorkyArcResult FrontRes = FGorky17Utils::CalculateAttackArc(Attacker, Defender, EGorkyFacing::North);
	TestEqual(TEXT("Facing North is Front Arc"), FrontRes.Arc, EGorkyArcZone::Front);
	TestEqual(TEXT("Front damage multiplier is 1.0"), FrontRes.DamageMultiplier, 1.0f);
	TestEqual(TEXT("Front armor multiplier is 1.0"), FrontRes.EffectiveArmorMultiplier, 1.0f);

	// Case 2: Defender faces South (away from attacker) -> Rear Arc (1.75x damage, 0.0x armor)
	FGorkyArcResult RearRes = FGorky17Utils::CalculateAttackArc(Attacker, Defender, EGorkyFacing::South);
	TestEqual(TEXT("Facing South is Rear Arc"), RearRes.Arc, EGorkyArcZone::Rear);
	TestEqual(TEXT("Rear damage multiplier is 1.75"), RearRes.DamageMultiplier, 1.75f);
	TestEqual(TEXT("Rear ignores armor (0.0)"), RearRes.EffectiveArmorMultiplier, 0.0f);

	// Case 3: Defender faces East (perpendicular to attacker) -> Flank Arc (1.25x damage, 0.5x armor)
	FGorkyArcResult FlankRes = FGorky17Utils::CalculateAttackArc(Attacker, Defender, EGorkyFacing::East);
	TestEqual(TEXT("Facing East is Flank Arc"), FlankRes.Arc, EGorkyArcZone::Flank);
	TestEqual(TEXT("Flank damage multiplier is 1.25"), FlankRes.DamageMultiplier, 1.25f);
	TestEqual(TEXT("Flank shreds 50% armor (0.5)"), FlankRes.EffectiveArmorMultiplier, 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyGridWorldConversionTest,
	"CodexTactics.Tactics.GridWorldConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyGridWorldConversionTest::RunTest(const FString& Parameters)
{
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 50.f);

	// Grid spans 14 * 150 = 2100 cm (-1050 to +1050 in X and Y)
	TestTrue(TEXT("Cell (0, 0) is valid"), Grid->IsValidCell(FIntPoint(0, 0)));
	TestTrue(TEXT("Cell (13, 13) is valid"), Grid->IsValidCell(FIntPoint(13, 13)));
	TestFalse(TEXT("Cell (-1, 0) is invalid"), Grid->IsValidCell(FIntPoint(-1, 0)));
	TestFalse(TEXT("Cell (14, 14) is invalid"), Grid->IsValidCell(FIntPoint(14, 14)));

	// World pos (0, 0, 50) is center of grid -> cell (7, 7)
	FIntPoint CenterCell = Grid->WorldToGrid(FVector(0.f, 0.f, 50.f));
	TestEqual(TEXT("Center world converts to grid (7, 7)"), CenterCell, FIntPoint(7, 7));

	// Grid pos (7, 7) converts to world center
	FVector CenterWorld = Grid->GridToWorld(FIntPoint(7, 7));
	TestNearlyEqual(TEXT("Grid (7, 7) X is 75 cm"), static_cast<float>(CenterWorld.X), 75.f, 1.0f);
	TestNearlyEqual(TEXT("Grid (7, 7) Y is 75 cm"), static_cast<float>(CenterWorld.Y), 75.f, 1.0f);
	TestEqual(TEXT("Grid Z matches GroundZ"), static_cast<float>(CenterWorld.Z), 50.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyGridOccupancyTest,
	"CodexTactics.Tactics.GridOccupancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyGridOccupancyTest::RunTest(const FString& Parameters)
{
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);

	const FIntPoint TestPos(5, 5);
	TestTrue(TEXT("Initial cell is walkable"), Grid->IsCellWalkable(TestPos));
	TestFalse(TEXT("Initial cell is not occupied"), Grid->IsCellOccupied(TestPos));

	// Set Obstacle
	Grid->SetOccupant(TestPos, nullptr, EGorkyOccupantType::Obstacle);
	TestFalse(TEXT("Cell with Obstacle is not walkable"), Grid->IsCellWalkable(TestPos));
	TestEqual(TEXT("Occupant type is Obstacle"), Grid->GetOccupantType(TestPos), EGorkyOccupantType::Obstacle);

	// Set Mine (Mines do not block movement)
	Grid->SetOccupant(TestPos, nullptr, EGorkyOccupantType::Mine);
	TestTrue(TEXT("Cell with Mine remains walkable"), Grid->IsCellWalkable(TestPos));
	TestEqual(TEXT("Occupant type is Mine"), Grid->GetOccupantType(TestPos), EGorkyOccupantType::Mine);

	// Clear occupant
	Grid->ClearOccupant(TestPos);
	TestTrue(TEXT("Cleared cell is walkable"), Grid->IsCellWalkable(TestPos));
	TestEqual(TEXT("Cleared occupant type is None"), Grid->GetOccupantType(TestPos), EGorkyOccupantType::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyGridReachableCellsAndDiagonalCostTest,
	"CodexTactics.Tactics.ReachableCellsAndDiagonalCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyGridReachableCellsAndDiagonalCostTest::RunTest(const FString& Parameters)
{
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);
	Grid->DiagonalAPCost = 2;

	const FIntPoint Start(5, 5);

	// Budget 1 AP: Only start + 4 cardinal neighbors should be reachable
	TMap<FIntPoint, int32> Budget1Map = Grid->GetReachableCells(Start, 1);
	TestEqual(TEXT("Budget 1 AP includes 5 cells (origin + 4 cardinals)"), Budget1Map.Num(), 5);
	TestTrue(TEXT("North is reachable at 1 AP"), Budget1Map.Contains(FIntPoint(5, 4)));
	TestTrue(TEXT("East is reachable at 1 AP"), Budget1Map.Contains(FIntPoint(6, 5)));
	TestTrue(TEXT("South is reachable at 1 AP"), Budget1Map.Contains(FIntPoint(5, 6)));
	TestTrue(TEXT("West is reachable at 1 AP"), Budget1Map.Contains(FIntPoint(4, 5)));
	TestFalse(TEXT("Diagonal (6, 6) is NOT reachable at 1 AP"), Budget1Map.Contains(FIntPoint(6, 6)));

	// Budget 2 AP: Diagonals become reachable (cost 2)
	TMap<FIntPoint, int32> Budget2Map = Grid->GetReachableCells(Start, 2);
	TestTrue(TEXT("Diagonal (6, 6) is reachable at 2 AP"), Budget2Map.Contains(FIntPoint(6, 6)));
	TestEqual(TEXT("Diagonal step cost is 2"), Budget2Map[FIntPoint(6, 6)], 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGorkyGridAStarPathfindingTest,
	"CodexTactics.Tactics.AStarPathfinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGorkyGridAStarPathfindingTest::RunTest(const FString& Parameters)
{
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);
	Grid->DiagonalAPCost = 2;

	const FIntPoint Start(5, 5);
	const FIntPoint Target(5, 8);

	// Straight path without obstacles
	TArray<FIntPoint> StraightPath = Grid->FindPath(Start, Target, 5);
	// Godot find_path: the steps without the start cell.
	TestEqual(TEXT("Straight path has 3 steps"), StraightPath.Num(), 3);
	if (StraightPath.Num() == 3)
	{
		TestEqual(TEXT("First step"), StraightPath[0], FIntPoint(5, 6));
		TestEqual(TEXT("Path end"), StraightPath[2], FIntPoint(5, 8));
	}
	TestTrue(TEXT("No path to the own cell"), Grid->FindPath(Start, Start, 5).IsEmpty());

	// Place obstacle at (5, 6)
	Grid->SetOccupant(FIntPoint(5, 6), nullptr, EGorkyOccupantType::Obstacle);

	// Path should navigate around the obstacle
	TArray<FIntPoint> DetourPath = Grid->FindPath(Start, Target, 6);
	TestTrue(TEXT("Detour path exists"), DetourPath.Num() > 0);
	TestFalse(TEXT("Detour path does not pass through obstacle (5, 6)"), DetourPath.Contains(FIntPoint(5, 6)));
	TestEqual(TEXT("Detour path reaches target"), DetourPath.Last(), Target);

	return true;
}
