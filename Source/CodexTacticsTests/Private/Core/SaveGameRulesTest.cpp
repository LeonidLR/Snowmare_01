#include "Misc/AutomationTest.h"
#include "Core/SaveGameRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scripts/managers/save_manager.gd parity: sanitize_slot_name, save type, suggest_next_slot_name,
// compute_current_stage_name, compute_squad_summary.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveGameRulesTest, "CodexTactics.Core.SaveGame.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSaveGameRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("empty -> Леонид_01"), SaveGameRules::SanitizeSlotName(TEXT("   ")), FString(TEXT("Леонид_01")));
	TestEqual(TEXT("invalid characters"), SaveGameRules::SanitizeSlotName(TEXT(" a/b:c*d? ")), FString(TEXT("a_b_c_d_")));
	TestEqual(TEXT("dots trimmed"), SaveGameRules::SanitizeSlotName(TEXT(".slot.")), FString(TEXT("slot")));

	TestEqual(TEXT("autosave"), SaveGameRules::GetSaveType(TEXT("autosave")), FString(TEXT("autosave")));
	TestEqual(TEXT("quicksave"), SaveGameRules::GetSaveType(TEXT("QuickSave")), FString(TEXT("quicksave")));
	TestEqual(TEXT("Быстрое"), SaveGameRules::GetSaveType(TEXT("Быстрое")), FString(TEXT("quicksave")));
	TestEqual(TEXT("manual"), SaveGameRules::GetSaveType(TEXT("Леонид_02")), FString(TEXT("manual")));

	TestEqual(TEXT("first slot"), SaveGameRules::SuggestNextSlotName({}), FString(TEXT("Леонид_01")));
	TestEqual(TEXT("next after 07"), SaveGameRules::SuggestNextSlotName({ TEXT("Леонид_03"), TEXT("Леонид_07"), TEXT("quicksave"), TEXT("Леонид_x") }),
		FString(TEXT("Леонид_08")));

	FQuestChainState Quests;
	TestEqual(TEXT("start"), SaveGameRules::GetStageName(Quests, false, false, false, 1, false), FString(TEXT("Периметр КПП (Поиск канистры)")));
	Quests.bHasEmptyCanister = true;
	TestEqual(TEXT("empty canister"), SaveGameRules::GetStageName(Quests, false, false, false, 1, true), FString(TEXT("Периметр КПП (Поиск дизеля) [Соло]")));
	Quests.bIsGeneratorRunning = true;
	TestEqual(TEXT("generator"), SaveGameRules::GetStageName(Quests, false, false, false, 1, false), FString(TEXT("КПП (Генератор запущен)")));
	TestEqual(TEXT("gate open"), SaveGameRules::GetStageName(Quests, true, false, false, 1, false), FString(TEXT("Внутренний двор (Ворота открыты)")));
	TestEqual(TEXT("preparation"), SaveGameRules::GetStageName(Quests, true, false, true, 1, false), FString(TEXT("Подготовка к обороне: Волна 1")));
	TestEqual(TEXT("wave"), SaveGameRules::GetStageName(Quests, true, true, false, 2, false), FString(TEXT("Оборона: Волна 2")));

	TestEqual(TEXT("squad summary"), SaveGameRules::GetSquadSummary({ { 140.f, 140.f }, { 75.f, 150.f }, { 0.f, 120.f } }),
		FString(TEXT("Бойцов: 2/3 | HP: 50%")));
	TestEqual(TEXT("no squad"), SaveGameRules::GetSquadSummary({}), FString(TEXT("Отряд: 0 бойцов")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
