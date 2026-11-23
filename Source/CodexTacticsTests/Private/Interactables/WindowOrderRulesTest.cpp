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
	const FWindowOrderTuning Flipped = WindowOrderRules::Parse(TEXT("{\"formation_follow_broken_only\": 0}"));
	TestFalse(TEXT("a 0 in the file switches it off"), Flipped.bFormationFollowBrokenOnly);
	TestTrue(TEXT("missing keys keep the defaults"), Flipped.bSoloGroupCombatWindows);
	return true;
}

#endif
