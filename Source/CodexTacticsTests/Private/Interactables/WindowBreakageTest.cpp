// Tests of WindowBreakage (designer 2026-11-22: a window climbed through shows broken - its glass hidden). UE-only.
#include "Misc/AutomationTest.h"
#include "Interactables/WindowBreakage.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Interactables/InteractableActor.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowBreakageOwnClickTargetTest, "CodexTactics.Interactables.WindowBreakage.OwnClickTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowBreakageOwnClickTargetTest::RunTest(const FString& Parameters)
{
	// Fable review 2026-11-23: a click on a pawn / an interactable by a window or doorway is a click on IT, never a window / door order.
	TestTrue(TEXT("a pawn (an enemy by the window, his own man under it)"), WindowBreakage::IsOwnClickTarget(GetDefault<APawn>()));
	TestTrue(TEXT("an interactable (a crate)"), WindowBreakage::IsOwnClickTarget(GetDefault<AInteractableActor>()));
	TestFalse(TEXT("a plain actor (a wall, the sill)"), WindowBreakage::IsOwnClickTarget(GetDefault<AActor>()));
	TestFalse(TEXT("no actor (a floor hit with no actor)"), WindowBreakage::IsOwnClickTarget(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowRoofLanding155Test, "CodexTactics.Interactables.WindowBreakage.RoofLanding155",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWindowRoofLanding155Test::RunTest(const FString& Parameters)
{
 const FBox Sill(FVector(-10.f,-60.f,0.f), FVector(10.f,60.f,95.f));
	TestFalse(TEXT("roof edge above upper-storey sill is not its window face"), WindowBreakage::IsOverSill(
		FBox(FVector(-10.f,-60.f,300.f), FVector(10.f,60.f,395.f)), FVector(0.f,0.f,600.f), WindowBreakage::FaceMarginCm, WindowBreakage::FaceHeightCm));
 TestTrue(TEXT("near landing through opening"), WindowBreakage::IsThroughLanding(Sill, FVector(-300.f,0.f,400.f), FVector(100.f,0.f,0.f)));
 // Both rays cross the pane and land within 150 cm in XY. The old XY-only test steals the roof / lower floor click.
 TestFalse(TEXT("roof beside upper window stays a roof order"), WindowBreakage::IsThroughLanding(Sill, FVector(-100.f,0.f,-200.f), FVector(100.f,0.f,600.f)));
 TestFalse(TEXT("another floor below stays its own order"), WindowBreakage::IsThroughLanding(Sill, FVector(-100.f,0.f,600.f), FVector(100.f,0.f,-300.f)));
 TestFalse(TEXT("far floor beyond opening"), WindowBreakage::IsThroughLanding(Sill, FVector(-300.f,0.f,400.f), FVector(600.f,0.f,0.f)));
 return true;
}

#endif
