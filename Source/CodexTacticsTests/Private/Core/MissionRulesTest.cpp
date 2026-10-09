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
	TestTrue(TEXT("Fresh start: Game at once (no in-level menu)"), GetAutoStartMode(false, EMissionStartMode::Combat) == EMissionStartMode::Game);
	TestTrue(TEXT("Ctrl + X repeats the last mode"), GetAutoStartMode(true, EMissionStartMode::Combat) == EMissionStartMode::Combat);
	TestTrue(TEXT("Quick restart without a mode starts Game"), GetAutoStartMode(true, EMissionStartMode::None) == EMissionStartMode::Game);
	TestTrue(TEXT("Frontend NEW GAME plays the intro"), !ShouldSkipIntro(false, true, false, false));
	TestTrue(TEXT("Frontend NEW GAME plays the intro even in a headless check"), !ShouldSkipIntro(true, true, false, false));
	TestTrue(TEXT("Headless checks skip the intro"), ShouldSkipIntro(true, false, false, false));
	TestTrue(TEXT("Ctrl + X plays the intro again"), !ShouldSkipIntro(true, false, true, false));
	TestTrue(TEXT("A pending save load skips the intro"), ShouldSkipIntro(false, true, false, true));
	TestTrue(TEXT("Editor play (not headless) plays the intro"), !ShouldSkipIntro(false, false, false, false));
	TestEqual(TEXT("Game objective"), GetModeObjective(EMissionStartMode::Game).ToString(),
		FString(TEXT("Explore the checkpoint and find a way to open the blast gates")));
	TestTrue(TEXT("Combat sets no objective itself"), GetModeObjective(EMissionStartMode::Combat).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
