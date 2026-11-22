#include "Misc/AutomationTest.h"
#include "Characters/VaultRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot locomotion_controller.gd check_vault_obstacle / start_vault / the vault arc parity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaultRulesTest, "CodexTactics.Characters.Vault.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaultRulesTest::RunTest(const FString&)
{
	TestTrue(TEXT("A 1 m barricade with ground behind"), VaultRules::CanVault(100.f, true, 0.f));
	TestFalse(TEXT("Too high (over 1.05 m)"), VaultRules::CanVault(120.f, true, 0.f));
	TestFalse(TEXT("Too low (under 25 cm: just walk)"), VaultRules::CanVault(20.f, true, 0.f));
	TestFalse(TEXT("Nothing to land on"), VaultRules::CanVault(80.f, false, 0.f));
	TestFalse(TEXT("A drop of more than 1 m behind"), VaultRules::CanVault(80.f, true, -150.f));
	TestTrue(TEXT("Walking vault 1.25 s / 1.15"), FMath::IsNearlyEqual(VaultRules::Duration(false), 1.25f / 1.15f, 0.001f));
	TestTrue(TEXT("Running vault 0.95 s / 1.15"), FMath::IsNearlyEqual(VaultRules::Duration(true), 0.95f / 1.15f, 0.001f));

	const FVector Start(0.f, 0.f, 90.f);
	const FVector Landing(250.f, 0.f, 90.f);
	TestTrue(TEXT("Starts at the start"), VaultRules::Position(Start, Landing, 100.f, 0.f).Equals(Start, 0.1f));
	TestTrue(TEXT("Ends on the landing spot"), VaultRules::Position(Start, Landing, 100.f, 1.f).Equals(Landing, 0.1f));
	const FVector Middle = VaultRules::Position(Start, Landing, 100.f, 0.5f);
	TestTrue(TEXT("Apex over the obstacle (height + 15 cm) halfway"), FMath::IsNearlyEqual(Middle.Z, 90.f + 115.f, 0.5f)
		&& FMath::IsNearlyEqual(Middle.X, 125.f, 0.5f));
	return true;
}

// Bug 2026-11-22 (doors R1, L_Gen_StoryShowcase S03 / S09): the walk after a vault goes on whatever the angle to the goal; only a crossing back
// over the same obstacle with the goal behind it again ends it (the ping-pong guard that replaced the Godot ~84 deg gate).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaultResumeRulesTest, "CodexTactics.Characters.Vault.Resume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaultResumeRulesTest::RunTest(const FString&)
{
	using namespace VaultRules;
	FVaultResumeFacts Facts;
	Facts.bHasOrder = true;
	Facts.Start = FVector(0.f, 0.f, 90.f);
	Facts.Landing = FVector(250.f, 0.f, 90.f); // vaulted along +X

	Facts.Goal = FVector(1250.f, 0.f, 90.f);
	TestTrue(TEXT("Goal straight ahead: resume"), DecideResume(Facts) == EVaultResume::Resume);
	Facts.Goal = FVector(300.f, 800.f, 90.f); // ~86 deg off: the window into a room, the goal to the side (the bug)
	TestTrue(TEXT("Goal ~86 deg to the side (window into a room): resume"), DecideResume(Facts) == EVaultResume::Resume);
	Facts.Goal = FVector(250.f, -900.f, 390.f); // 90 deg, a storey up: the stairs inside
	TestTrue(TEXT("Goal 90 deg to the side, a storey up: resume"), DecideResume(Facts) == EVaultResume::Resume);
	Facts.Goal = FVector(-600.f, 200.f, 90.f); // behind: overshot by a blocked follower - the new path walks back
	TestTrue(TEXT("Goal behind the obstacle, first crossing: resume (new navmesh path)"), DecideResume(Facts) == EVaultResume::Resume);
	Facts.Goal = FVector(290.f, 30.f, 120.f);
	TestTrue(TEXT("Goal within 60 cm of the landing: arrived"), DecideResume(Facts) == EVaultResume::AtGoal);
	Facts.Goal = FVector(1250.f, 0.f, 90.f);
	Facts.bHasOrder = false;
	TestTrue(TEXT("No order when the arc began: nothing to resume"), DecideResume(Facts) == EVaultResume::NoOrder);

	// Crossing back over the same obstacle.
	Facts.bHasOrder = true;
	Facts.bRecrossing = true;
	Facts.Goal = FVector(-600.f, 200.f, 90.f);
	TestTrue(TEXT("Back over the same obstacle, goal behind it again: ping-pong stop"), DecideResume(Facts) == EVaultResume::PingPong);
	Facts.Goal = FVector(320.f, 700.f, 90.f); // dot 0.0995: the old gate's line still holds for a second crossing
	TestTrue(TEXT("Back over the same obstacle, goal ~84+ deg to the side: ping-pong stop"), DecideResume(Facts) == EVaultResume::PingPong);
	Facts.Goal = FVector(600.f, 500.f, 90.f);
	TestTrue(TEXT("Back over the same obstacle, goal ahead on this side (overshoot corrected): resume"), DecideResume(Facts) == EVaultResume::Resume);
	Facts.Start = Facts.Landing; // degenerate arc: no direction to judge by
	Facts.Goal = FVector(-600.f, 0.f, 90.f);
	TestTrue(TEXT("No arc direction: resume"), DecideResume(Facts) == EVaultResume::Resume);

	const FVector East(1.f, 0.f, 0.f);
	const FVector West(-1.f, 0.f, 0.f);
	const FVector North(0.f, 1.f, 0.f);
	const FVector Goal(500.f, 500.f, 0.f);
	TestTrue(TEXT("Recrossing: same obstacle, reversed, same goal, soon"), IsRecrossing(true, true, East, West, Goal, Goal + FVector(50.f, 0.f, 0.f), 3.f));
	TestFalse(TEXT("Another obstacle"), IsRecrossing(false, true, East, West, Goal, Goal, 3.f));
	TestFalse(TEXT("The previous vault did not resume (a new order)"), IsRecrossing(true, false, East, West, Goal, Goal, 3.f));
	TestFalse(TEXT("Same direction (a second barricade of a row, or the same one again forward)"), IsRecrossing(true, true, East, East, Goal, Goal, 3.f));
	TestFalse(TEXT("Perpendicular is not back"), IsRecrossing(true, true, East, North, Goal, Goal, 3.f));
	TestFalse(TEXT("Another goal (over and back by two orders: BarricadeVaultBackSmoke)"), IsRecrossing(true, true, East, West, Goal, Goal + FVector(400.f, 0.f, 0.f), 3.f));
	TestFalse(TEXT("Too long ago"), IsRecrossing(true, true, East, West, Goal, Goal, RecrossWindowSeconds + 1.f));
	return true;
}

#endif
