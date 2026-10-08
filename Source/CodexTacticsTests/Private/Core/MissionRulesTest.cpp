#include "Misc/AutomationTest.h"
#include "Core/MissionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Objective banner / mission-failed texts, parity with Godot Scenes/movements/main.gd update_objective, _trigger_game_over.

#define MISSION_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Mission." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

MISSION_TEST(FMissionObjectivesTest, "ObjectivePerPhase")
bool FMissionObjectivesTest::RunTest(const FString&)
{
	using namespace MissionRules;
	FText Objective;
	TestTrue(TEXT("First preparation"), GetPhaseObjective(ECodexGamePhase::Preparation, 1, 45.f, false, Objective));
	TestEqual(TEXT("First preparation text"), Objective.ToString(),
		FString(TEXT("DEFENSE PREPARATION: place turrets, barricades and mines ([Space] for orders)")));
	TestTrue(TEXT("Rest preparation"), GetPhaseObjective(ECodexGamePhase::Preparation, 2, 29.7f, false, Objective));
	TestEqual(TEXT("Rest text truncates seconds"), Objective.ToString(), FString(TEXT("PREPARATION: 29 sec until wave 2. Fortify the defenses!")));
	TestTrue(TEXT("Victory"), GetPhaseObjective(ECodexGamePhase::PostCombat, 3, 0.f, false, Objective));
	TestEqual(TEXT("Victory text"), Objective.ToString(), FString(TEXT("LINE SECURED: The squad survived! Regrouping...")));
	TestTrue(TEXT("Exploration after combat"), GetPhaseObjective(ECodexGamePhase::Exploration, 3, 0.f, true, Objective));
	TestEqual(TEXT("Explore text"), Objective.ToString(), FString(TEXT("LINE SECURED: Explore the checkpoint grounds")));
	TestFalse(TEXT("Exploration before combat keeps the quest objective"), GetPhaseObjective(ECodexGamePhase::Exploration, 0, 0.f, false, Objective));
	TestFalse(TEXT("Wave objective comes from the wave start"), GetPhaseObjective(ECodexGamePhase::WaveCombat, 1, 0.f, false, Objective));
	TestEqual(TEXT("Wave text"), GetWaveObjective(1, 12).ToString(), FString(TEXT("DEFENSE: Repel wave 1! Enemies: 12")));
	TestEqual(TEXT("Start"), GetStartObjective().ToString(), FString(TEXT("Explore the checkpoint and find a way to open the blast gates")));
	return true;
}

MISSION_TEST(FMissionFailureTest, "FailureReasonByCold")
bool FMissionFailureTest::RunTest(const FString&)
{
	using namespace MissionRules;
	const FText Name = FText::FromString(TEXT("Engineer"));
	TestEqual(TEXT("Frozen at 99"), GetFailureReason(Name, 99.f).ToString(),
		FString(TEXT("Operative Engineer died of critical hypothermia!")));
	TestEqual(TEXT("Wounds below 99"), GetFailureReason(Name, 98.9f).ToString(),
		FString(TEXT("Operative Engineer died of battle wounds!")));
	TestEqual(TEXT("Radio"), GetFailureRadio(Name).ToString(), FString(TEXT("Warning! Contact with Engineer lost. Mission failed.")));
	return true;
}

MISSION_TEST(FMissionStartModeTest, "StartModeAndMenu")
bool FMissionStartModeTest::RunTest(const FString&)
{
	using namespace MissionRules;
	TestTrue(TEXT("Fresh start opens the menu"), GetAutoStartMode(false, EMissionStartMode::Combat, false) == EMissionStartMode::None);
	TestTrue(TEXT("Ctrl + X repeats the last mode"), GetAutoStartMode(true, EMissionStartMode::Combat, false) == EMissionStartMode::Combat);
	TestTrue(TEXT("Quick restart without a mode shows the menu"), GetAutoStartMode(true, EMissionStartMode::None, false) == EMissionStartMode::None);
	TestTrue(TEXT("Headless checks start the game"), GetAutoStartMode(false, EMissionStartMode::None, true) == EMissionStartMode::Game);
	TestEqual(TEXT("Game objective"), GetModeObjective(EMissionStartMode::Game).ToString(),
		FString(TEXT("Explore the checkpoint and find a way to open the blast gates")));
	TestTrue(TEXT("Combat sets no objective itself"), GetModeObjective(EMissionStartMode::Combat).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
