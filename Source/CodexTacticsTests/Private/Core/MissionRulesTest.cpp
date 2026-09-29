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
		FString(TEXT("ПОДГОТОВКА К ОБОРОНЕ: расставьте турели, баррикады и мины ([Space] — приказы)")));
	TestTrue(TEXT("Rest preparation"), GetPhaseObjective(ECodexGamePhase::Preparation, 2, 29.7f, false, Objective));
	TestEqual(TEXT("Rest text truncates seconds"), Objective.ToString(), FString(TEXT("ПОДГОТОВКА: 29 сек до волны 2. Укрепите оборону!")));
	TestTrue(TEXT("Victory"), GetPhaseObjective(ECodexGamePhase::PostCombat, 3, 0.f, false, Objective));
	TestEqual(TEXT("Victory text"), Objective.ToString(), FString(TEXT("РУБЕЖ ЗАЧИЩЕН: Отряд выжил! Перегруппировка...")));
	TestTrue(TEXT("Exploration after combat"), GetPhaseObjective(ECodexGamePhase::Exploration, 3, 0.f, true, Objective));
	TestEqual(TEXT("Explore text"), Objective.ToString(), FString(TEXT("РУБЕЖ ЗАЧИЩЕН: Исследуйте территорию КПП")));
	TestFalse(TEXT("Exploration before combat keeps the quest objective"), GetPhaseObjective(ECodexGamePhase::Exploration, 0, 0.f, false, Objective));
	TestFalse(TEXT("Wave objective comes from the wave start"), GetPhaseObjective(ECodexGamePhase::WaveCombat, 1, 0.f, false, Objective));
	TestEqual(TEXT("Wave text"), GetWaveObjective(1, 12).ToString(), FString(TEXT("ОБОРОНА: Отразить волну 1! Врагов: 12")));
	TestEqual(TEXT("Start"), GetStartObjective().ToString(), FString(TEXT("Исследовать КПП и найти способ открыть гермоворота")));
	return true;
}

MISSION_TEST(FMissionFailureTest, "FailureReasonByCold")
bool FMissionFailureTest::RunTest(const FString&)
{
	using namespace MissionRules;
	const FText Name = FText::FromString(TEXT("Инженер"));
	TestEqual(TEXT("Frozen at 99"), GetFailureReason(Name, 99.f).ToString(),
		FString(TEXT("Оперативник Инженер погиб от критического переохлаждения!")));
	TestEqual(TEXT("Wounds below 99"), GetFailureReason(Name, 98.9f).ToString(),
		FString(TEXT("Оперативник Инженер погиб в бою от полученных ранений!")));
	TestEqual(TEXT("Radio"), GetFailureRadio(Name).ToString(), FString(TEXT("Внимание! Связь с Инженер потеряна. Миссия провалена.")));
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
		FString(TEXT("Исследовать КПП и найти способ открыть гермоворота")));
	TestTrue(TEXT("Combat sets no objective itself"), GetModeObjective(EMissionStartMode::Combat).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
