// Tests of WindowBreakage (designer 2026-11-22: a window climbed through shows broken - its glass hidden). UE-only.
#include "Misc/AutomationTest.h"
#include "Interactables/WindowBreakage.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowBreakageOverSillTest, "CodexTactics.Interactables.WindowBreakage.OverSill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowBreakageOverSillTest::RunTest(const FString& Parameters)
{
	// A 120 cm wide, 20 cm deep, 90 cm high ground-floor sill at the origin.
	const FBox Sill(FVector(-60.f, -10.f, 0.f), FVector(60.f, 10.f, 90.f));
	TestTrue(TEXT("the pane over the sill"), WindowBreakage::IsOverSill(Sill, FVector(0.f, 0.f, 160.f)));
	TestTrue(TEXT("a kit window frame a little proud of the wall"), WindowBreakage::IsOverSill(Sill, FVector(0.f, 30.f, 160.f)));
	TestFalse(TEXT("the next window along the wall"), WindowBreakage::IsOverSill(Sill, FVector(200.f, 0.f, 160.f)));
	TestFalse(TEXT("the window of the storey above"), WindowBreakage::IsOverSill(Sill, FVector(0.f, 0.f, 420.f)));
	TestFalse(TEXT("an invalid sill box"), WindowBreakage::IsOverSill(FBox(ForceInit), FVector::ZeroVector));
	return true;
}

#endif
