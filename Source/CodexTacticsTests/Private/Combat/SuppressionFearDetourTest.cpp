#include "Characters/EnemyAIRules.h"
#include "Combat/SuppressionRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionFearDetourTest, "CodexTactics.Combat.Suppression.FearDetour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSuppressionFearDetourTest::RunTest(const FString&)
{
	const FVector Centre = FVector::ZeroVector;
	const FVector Target(-1200.f, 0.f, 0.f);
	const FVector First(1200.f, 100.f, 0.f);
	const FVector Second(1200.f, -100.f, 0.f);
	const FSuppressionConfig Config;
	TestEqual(TEXT("Opposite approach lanes choose opposing turns"), SuppressionRules::PickFearDetourSide(
		SuppressionRules::FearDetourLateralOffset(First, Centre, Target), Config.ZoneDetourTieBandCm), static_cast<int8>(1));
	TestEqual(TEXT("The other lane turns clockwise"), SuppressionRules::PickFearDetourSide(
		SuppressionRules::FearDetourLateralOffset(Second, Centre, Target), Config.ZoneDetourTieBandCm), static_cast<int8>(-1));
	TestEqual(TEXT("The centre line is a tie"), SuppressionRules::PickFearDetourSide(0.f, Config.ZoneDetourTieBandCm), static_cast<int8>(0));

	FSuppressionFireCell Cell;
	const int8 FirstTie = SuppressionRules::TakeFearDetourTieSide(Cell);
	TestEqual(TEXT("The next tie takes the opposite side"), SuppressionRules::TakeFearDetourTieSide(Cell), static_cast<int8>(-FirstTie));
	TestEqual(TEXT("Ties continue alternating"), SuppressionRules::TakeFearDetourTieSide(Cell), FirstTie);

	FVector Waypoint;
	const FVector ShiftedTarget(-1200.f, 300.f, 0.f);
	TestTrue(TEXT("A detour retains its assigned turn after the target moves"), EnemyAIRules::FireDetourWaypoint(
		FVector(1200.f, 0.f, 0.f), Centre, 400.f, ShiftedTarget, Waypoint, EnemyAIRules::FireDetourStepDegrees, -1));
	TestTrue(TEXT("The next waypoint stays on the assigned side"), Waypoint.Y < 0.f);
	TestFalse(TEXT("A clear lane still takes the direct route"), EnemyAIRules::FireDetourWaypoint(
		First, Centre, 400.f, FVector(1000.f, 0.f, 0.f), Waypoint, EnemyAIRules::FireDetourStepDegrees, -1));
	return true;
}
