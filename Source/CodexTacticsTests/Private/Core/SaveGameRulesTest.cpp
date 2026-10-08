#include "Misc/AutomationTest.h"
#include "Core/SaveGameRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scripts/managers/save_manager.gd parity: sanitize_slot_name, save type, suggest_next_slot_name,
// compute_current_stage_name, compute_squad_summary.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveGameRulesTest, "CodexTactics.Core.SaveGame.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSaveGameRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("empty -> Leonid_01"), SaveGameRules::SanitizeSlotName(TEXT("   ")), FString(TEXT("Leonid_01")));
	TestEqual(TEXT("invalid characters"), SaveGameRules::SanitizeSlotName(TEXT(" a/b:c*d? ")), FString(TEXT("a_b_c_d_")));
	TestEqual(TEXT("dots trimmed"), SaveGameRules::SanitizeSlotName(TEXT(".slot.")), FString(TEXT("slot")));

	TestEqual(TEXT("autosave"), SaveGameRules::GetSaveType(TEXT("autosave")), FString(TEXT("autosave")));
	TestEqual(TEXT("quicksave"), SaveGameRules::GetSaveType(TEXT("QuickSave")), FString(TEXT("quicksave")));
	TestEqual(TEXT("Быстрое"), SaveGameRules::GetSaveType(TEXT("Быстрое")), FString(TEXT("quicksave")));
	TestEqual(TEXT("manual"), SaveGameRules::GetSaveType(TEXT("Leonid_02")), FString(TEXT("manual")));

	TestEqual(TEXT("first slot"), SaveGameRules::SuggestNextSlotName({}), FString(TEXT("Leonid_01")));
	TestEqual(TEXT("next after 07"), SaveGameRules::SuggestNextSlotName({ TEXT("Leonid_03"), TEXT("Leonid_07"), TEXT("quicksave"), TEXT("Leonid_x") }),
		FString(TEXT("Leonid_08")));

	FQuestChainState Quests;
	TestEqual(TEXT("start"), SaveGameRules::GetStageName(Quests, false, false, false, 1, false), FString(TEXT("Checkpoint perimeter (Find canister)")));
	Quests.bHasEmptyCanister = true;
	TestEqual(TEXT("empty canister"), SaveGameRules::GetStageName(Quests, false, false, false, 1, true), FString(TEXT("Checkpoint perimeter (Find diesel) [Solo]")));
	Quests.bIsGeneratorRunning = true;
	TestEqual(TEXT("generator"), SaveGameRules::GetStageName(Quests, false, false, false, 1, false), FString(TEXT("Checkpoint (Generator running)")));
	TestEqual(TEXT("gate open"), SaveGameRules::GetStageName(Quests, true, false, false, 1, false), FString(TEXT("Inner yard (Gate open)")));
	TestEqual(TEXT("preparation"), SaveGameRules::GetStageName(Quests, true, false, true, 1, false), FString(TEXT("Defence preparation: Wave 1")));
	TestEqual(TEXT("wave"), SaveGameRules::GetStageName(Quests, true, true, false, 2, false), FString(TEXT("Defence: Wave 2")));

	TestEqual(TEXT("squad summary"), SaveGameRules::GetSquadSummary({ { 140.f, 140.f }, { 75.f, 150.f }, { 0.f, 120.f } }),
		FString(TEXT("Operatives: 2/3 | HP: 50%")));
	TestEqual(TEXT("no squad"), SaveGameRules::GetSquadSummary({}), FString(TEXT("Squad: 0 operatives")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
