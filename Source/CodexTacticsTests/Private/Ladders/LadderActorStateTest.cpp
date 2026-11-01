#include "Misc/AutomationTest.h"
#include "Ladders/LadderActor.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Game designer bug 07 (2026-11-01, L_FormationLabCombat in PIE): "[Ladder] ... climb refused (6)" = NotClimbable on the towers' 11-rung
// ladders. The rung count was a non-UPROPERTY cache filled by the construction pass; a PIE world is a property copy of the editor world
// (StaticDuplicateObject), the cache came over as 0 and the copied rung instances kept BeginPlay from rebuilding it: every ladder was a
// "decoration" in PIE (the -game smokes load the map and rebuild, so they passed). The climbable state must come from the saved
// parameters alone - an actor whose construction never ran, and a property copy of it, read the same.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLadderActorDerivedStateTest, "CodexTactics.Ladder.Actor.DerivedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLadderActorDerivedStateTest::RunTest(const FString&)
{
	ALadderActor* Tower = NewObject<ALadderActor>(GetTransientPackage(), NAME_None, RF_Transient);
	Tower->Height = 11 * 36.4f; // build_formation_lab.py: rungs x pitch
	TestEqual(TEXT("no construction pass: 11 rungs from Height / Pitch"), Tower->GetRungCount(), 11);
	TestTrue(TEXT("no construction pass: climbable"), Tower->IsClimbable());
	TestTrue(TEXT("its top at 400 cm over its floor"), FMath::IsNearlyEqual(Tower->GetTopWorldZ() - Tower->GetFloorZ(), 400.4f, 0.1f));

	// The PIE copy: UPROPERTYs only.
	ALadderActor* PieCopy = DuplicateObject<ALadderActor>(Tower, GetTransientPackage());
	TestEqual(TEXT("the property copy (PIE) keeps 11 rungs"), PieCopy ? PieCopy->GetRungCount() : -1, 11);
	TestTrue(TEXT("the property copy (PIE) stays climbable (the bug: refused as a decoration)"), PieCopy && PieCopy->IsClimbable());

	ALadderActor* Short = NewObject<ALadderActor>(GetTransientPackage(), NAME_None, RF_Transient);
	Short->Height = 6 * 36.4f;
	TestFalse(TEXT("6 rungs: still a decoration"), Short->IsClimbable());
	Tower->bTopExit = false;
	TestFalse(TEXT("no top exit: a decoration"), Tower->IsClimbable());

	for (UObject* Each : TArray<UObject*>{ Tower, PieCopy, Short })
	{
		if (Each)
		{
			Each->MarkAsGarbage();
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
