#include "Misc/AutomationTest.h"
#include "Characters/SquadFormation.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot main.gd _get_group_target_positions parity (box-selected group move).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroupTargetsTest, "CodexTactics.Formation.GroupTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroupTargetsTest::RunTest(const FString&)
{
	// The group stands at the origin and is sent 10 m along +X: right = +Y.
	const FVector Center(1000.f, 0.f, 50.f);
	const TArray<FVector> Targets = SquadFormation::ComputeGroupTargets(Center, FVector::ZeroVector, FVector::ForwardVector, 6);
	TestEqual(TEXT("Six targets"), Targets.Num(), 6);
	TestTrue(TEXT("Leader on the click"), Targets[0].Equals(Center, 0.1f));
	TestTrue(TEXT("Second: 1.8 m right, 0.8 m back"), Targets[1].Equals(FVector(920.f, 180.f, 50.f), 0.1f));
	TestTrue(TEXT("Third: 1.8 m left, 0.8 m back"), Targets[2].Equals(FVector(920.f, -180.f, 50.f), 0.1f));
	TestTrue(TEXT("Fourth: 2.2 m behind"), Targets[3].Equals(FVector(780.f, 0.f, 50.f), 0.1f));
	TestTrue(TEXT("Fifth: row 2, right"), Targets[5].Equals(FVector(1000.f - 300.f, 180.f, 50.f), 0.1f));
	TestTrue(TEXT("Index 4: row 2, left"), Targets[4].Equals(FVector(1000.f - 300.f, -180.f, 50.f), 0.1f));

	// Clicking on the group itself: the camera's forward is the heading.
	const TArray<FVector> Here = SquadFormation::ComputeGroupTargets(FVector::ZeroVector, FVector(2.f, 0.f, 0.f), FVector(0.f, 1.f, -0.5f), 2);
	TestTrue(TEXT("Camera heading (+Y): the second goes to its right (-X)"), Here[1].Equals(FVector(-180.f, -80.f, 0.f), 0.1f));
	return true;
}

#endif
