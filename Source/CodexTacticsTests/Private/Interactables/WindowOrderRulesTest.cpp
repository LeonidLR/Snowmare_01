// Tests of WindowOrderRules (designer decision 2026-11-23: who may take a WHOLE window, per mode and fight). UE-only.
#include "Misc/AutomationTest.h"
#include "Interactables/WindowOrderRules.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowOrderSoloGroupCombatTest, "CodexTactics.Interactables.WindowOrder.Rule1SoloGroupInFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowOrderSoloGroupCombatTest::RunTest(const FString& Parameters)
{
	const FWindowOrderTuning T;
	TestTrue(TEXT("Solo, the whole squad selected, in a fight: a walk opens the windows"), WindowOrderRules::MoveOpensWindows(true, 3, 3, true, false, T));
	TestFalse(TEXT("... out of a fight it does not (rule 3)"), WindowOrderRules::MoveOpensWindows(true, 3, 3, false, false, T));
	TestFalse(TEXT("two of three selected: not the whole squad"), WindowOrderRules::MoveOpensWindows(true, 2, 3, true, false, T));
	FWindowOrderTuning Any = T;
	Any.bSoloGroupWholeSquadOnly = false;
	TestTrue(TEXT("solo_group_whole_squad_only 0: any box selection of 2+"), WindowOrderRules::MoveOpensWindows(true, 2, 3, true, false, Any));
	TestFalse(TEXT("one man in Solo walking in a fight: no"), WindowOrderRules::MoveOpensWindows(true, 1, 3, true, false, T));
	TestTrue(TEXT("one man in Solo RUNNING in a fight: the 2026-11-22 run rule"), WindowOrderRules::MoveOpensWindows(true, 1, 3, true, true, T));
	FWindowOrderTuning Off = T;
	Off.bSoloGroupCombatWindows = false;
	TestFalse(TEXT("solo_group_combat_windows 0: the group walk opens none"), WindowOrderRules::MoveOpensWindows(true, 3, 3, true, false, Off));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowOrderFormationTest, "CodexTactics.Interactables.WindowOrder.Rule2FormationBrokenOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowOrderFormationTest::RunTest(const FString& Parameters)
{
	const FWindowOrderTuning T;
	TestFalse(TEXT("formation: a run in a fight opens no window"), WindowOrderRules::MoveOpensWindows(false, 3, 3, true, true, T));
	TestFalse(TEXT("formation: a walk in a fight opens none either"), WindowOrderRules::MoveOpensWindows(false, 3, 3, true, false, T));
	FWindowOrderTuning Old = T;
	Old.bFormationRunInFightWindows = true;
	TestTrue(TEXT("formation_run_in_fight_windows 1: the old run rule"), WindowOrderRules::MoveOpensWindows(false, 3, 3, true, true, Old));
	TestTrue(TEXT("the leader on a window order takes the whole window"), WindowOrderRules::MayTakeWholeWindow(true, false, T));
	TestFalse(TEXT("a follower on it does not - he follows once it is broken"), WindowOrderRules::MayTakeWholeWindow(true, true, T));
	TestFalse(TEXT("nobody without a window order"), WindowOrderRules::MayTakeWholeWindow(false, false, T));
	FWindowOrderTuning Free = T;
	Free.bFormationFollowBrokenOnly = false;
	TestTrue(TEXT("formation_follow_broken_only 0: the follower may take it too"), WindowOrderRules::MayTakeWholeWindow(true, true, Free));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowOrderSoloQuietTest, "CodexTactics.Interactables.WindowOrder.Rule3SoloOutOfFight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowOrderSoloQuietTest::RunTest(const FString& Parameters)
{
	const FWindowOrderTuning T;
	TestFalse(TEXT("Solo out of a fight: a walk opens no window"), WindowOrderRules::MoveOpensWindows(true, 1, 3, false, false, T));
	TestFalse(TEXT("... nor a run"), WindowOrderRules::MoveOpensWindows(true, 1, 3, false, true, T));
	TestFalse(TEXT("... nor the whole squad selected"), WindowOrderRules::MoveOpensWindows(true, 3, 3, false, true, T));
	TestTrue(TEXT("the explicit window order still opens it for him"), WindowOrderRules::MayTakeWholeWindow(true, false, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowOrderFileTest, "CodexTactics.Interactables.WindowOrder.FileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowOrderFileTest::RunTest(const FString& Parameters)
{
	FString Text;
	if (!TestTrue(TEXT("windows.json is there"), FFileHelper::LoadFileToString(Text, *WindowOrderRules::FilePath())))
	{
		return false;
	}
	const FWindowOrderTuning File = WindowOrderRules::Parse(Text);
	const FWindowOrderTuning Code;
	TestEqual(TEXT("solo_group_combat_windows"), File.bSoloGroupCombatWindows, Code.bSoloGroupCombatWindows);
	TestEqual(TEXT("solo_group_whole_squad_only"), File.bSoloGroupWholeSquadOnly, Code.bSoloGroupWholeSquadOnly);
	TestEqual(TEXT("solo_run_in_fight_windows"), File.bSoloRunInFightWindows, Code.bSoloRunInFightWindows);
	TestEqual(TEXT("formation_run_in_fight_windows"), File.bFormationRunInFightWindows, Code.bFormationRunInFightWindows);
	TestEqual(TEXT("formation_follow_broken_only"), File.bFormationFollowBrokenOnly, Code.bFormationFollowBrokenOnly);
	TestTrue(TEXT("approach_past_cm"), FMath::IsNearlyEqual(File.ApproachPastCm, Code.ApproachPastCm));
	TestTrue(TEXT("landing_past_cm"), FMath::IsNearlyEqual(File.LandingPastCm, Code.LandingPastCm));
	TestTrue(TEXT("approach_match_cm"), FMath::IsNearlyEqual(File.ApproachMatchCm, Code.ApproachMatchCm));
	TestTrue(TEXT("approach_arrive_cm"), FMath::IsNearlyEqual(File.ApproachArriveCm, Code.ApproachArriveCm));
	const FWindowOrderTuning Flipped = WindowOrderRules::Parse(TEXT("{\"formation_follow_broken_only\": 0}"));
	TestFalse(TEXT("a 0 in the file switches it off"), Flipped.bFormationFollowBrokenOnly);
	TestTrue(TEXT("missing keys keep the defaults"), Flipped.bSoloGroupCombatWindows);
	TestTrue(TEXT("missing distances keep the defaults"), FMath::IsNearlyEqual(Flipped.ApproachPastCm, Code.ApproachPastCm));
	const FWindowOrderTuning Near = WindowOrderRules::Parse(TEXT("{\"approach_past_cm\": 40}"));
	TestTrue(TEXT("approach_past_cm is read"), FMath::IsNearlyEqual(Near.ApproachPastCm, 40.f));
	TestTrue(TEXT("the other distance stays"), FMath::IsNearlyEqual(Near.LandingPastCm, Code.LandingPastCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowOrderSideTest, "CodexTactics.Interactables.WindowOrder.SideIsTheOperatives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWindowOrderSideTest::RunTest(const FString& Parameters)
{
	const FVector Forward(1.f, 0.f, 0.f);
	const FVector Right(0.f, 1.f, 0.f);
	const FVector Center(0.f, 0.f, 10.f);
	{
		const FWindowSideChoice Choice = WindowOrderRules::ChooseSide(FVector(0.f, 200.f, 90.f), Center, Forward, Right, 150.f, 30.f, 80.f, 120.f);
		TestTrue(TEXT("long forward: the thin axis is +Right, not the mesh forward"), FVector::DotProduct(Choice.Through, Right) > 0.99f);
		TestTrue(TEXT("approach stays on the wall centre along the wall"), FMath::IsNearlyEqual(Choice.Approach.X, Center.X, 0.1f));
		TestTrue(TEXT("approach is on his +Right side"), Choice.Approach.Y > Center.Y + 80.f);
		TestTrue(TEXT("landing is on the other side"), Choice.Landing.Y < Center.Y - 100.f);
		const FVector Facing = FRotator(0.f, Choice.Yaw, 0.f).Vector();
		TestTrue(TEXT("yaw faces the landing"), FVector::DotProduct(Facing, (Choice.Landing - Choice.Approach).GetSafeNormal2D()) > 0.99f);
	}
	{
		const FWindowSideChoice Choice = WindowOrderRules::ChooseSide(FVector(0.f, -200.f, 90.f), Center, Forward, Right, 150.f, 30.f, 80.f, 120.f);
		TestTrue(TEXT("the other side flips the approach onto -Right"), Choice.Approach.Y < Center.Y - 80.f);
		TestTrue(TEXT("and the landing onto +Right"), Choice.Landing.Y > Center.Y + 100.f);
	}
	{
		const FWindowSideChoice Choice = WindowOrderRules::ChooseSide(FVector(-200.f, 50.f, 90.f), Center, Forward, Right, 30.f, 150.f, 80.f, 120.f);
		TestTrue(TEXT("thin forward: through is -or +forward, not the long right"), FVector::DotProduct(Choice.Through, Forward) > 0.99f);
		TestTrue(TEXT("approach is on his -forward side"), Choice.Approach.X < Center.X - 80.f);
		TestTrue(TEXT("landing is on +forward"), Choice.Landing.X > Center.X + 100.f);
		TestTrue(TEXT("his along-wall offset is not copied"), FMath::IsNearlyEqual(Choice.Approach.Y, Center.Y, 0.1f));
		TestTrue(TEXT("Z is his feet"), FMath::IsNearlyEqual(Choice.Approach.Z, 90.f, 0.1f));
	}
	return true;
}

#endif
