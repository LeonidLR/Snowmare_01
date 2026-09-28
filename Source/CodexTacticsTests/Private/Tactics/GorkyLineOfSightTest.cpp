#include "Misc/AutomationTest.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"

#if WITH_DEV_AUTOMATION_TESTS

// Parity with Godot Scripts/tactics/gorky17_los.gd (and tests/test_diagonal_movement.gd 5.1).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGorkyLineOfSightTest, "CodexTactics.Tactics.LineOfSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGorkyLineOfSightTest::RunTest(const FString&)
{
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);
	AActor* Dummy = NewObject<AActor>();

	Grid->SetOccupant(FIntPoint(3, 3), Dummy, EGorkyOccupantType::Enemy);
	TestTrue(TEXT("Diagonal neighbour (2,2)->(3,3): the target itself never blocks"), GorkyLineOfSight::HasLineOfSight(FIntPoint(2, 2), FIntPoint(3, 3), *Grid));
	TestTrue(TEXT("Same cell"), GorkyLineOfSight::HasLineOfSight(FIntPoint(5, 5), FIntPoint(5, 5), *Grid));
	TestFalse(TEXT("Outside the grid"), GorkyLineOfSight::HasLineOfSight(FIntPoint(0, 0), FIntPoint(20, 0), *Grid));

	Grid->SetOccupant(FIntPoint(5, 0), Dummy, EGorkyOccupantType::Barricade);
	TestFalse(TEXT("Barricade on the row blocks"), GorkyLineOfSight::HasLineOfSight(FIntPoint(2, 0), FIntPoint(8, 0), *Grid));
	Grid->SetOccupant(FIntPoint(5, 0), Dummy, EGorkyOccupantType::Mine);
	TestTrue(TEXT("A mine does not block"), GorkyLineOfSight::HasLineOfSight(FIntPoint(2, 0), FIntPoint(8, 0), *Grid));

	// Steep line (0,0)->(2,6): Godot stepping visits (0,1), (1,2), (1,3) ...
	Grid->SetOccupant(FIntPoint(1, 3), Dummy, EGorkyOccupantType::Obstacle);
	TestFalse(TEXT("Obstacle on the steep line blocks"), GorkyLineOfSight::HasLineOfSight(FIntPoint(0, 0), FIntPoint(2, 6), *Grid));
	TestTrue(TEXT("Off-line obstacle does not"), GorkyLineOfSight::HasLineOfSight(FIntPoint(0, 0), FIntPoint(0, 6), *Grid));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
