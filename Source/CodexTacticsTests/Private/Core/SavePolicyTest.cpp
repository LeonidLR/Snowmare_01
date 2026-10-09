#include "Misc/AutomationTest.h"
#include "Core/SaveGameRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Save policy (user decision 2026-10-08, no Godot reference): saving only outside combat, autosave right before a fight
// starts and right after it ends, the flow a load resumes in, save format versioning.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavePolicyBlockReasonTest, "CodexTactics.Save.Policy.BlockReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavePolicyBlockReasonTest::RunTest(const FString&)
{
	TestEqual(TEXT("exploration: allowed"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::Exploration)), static_cast<int32>(ESaveBlockReason::None));
	TestEqual(TEXT("preparation: allowed"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::Preparation)), static_cast<int32>(ESaveBlockReason::None));
	TestEqual(TEXT("wave cleared: allowed"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::WaveCleared)), static_cast<int32>(ESaveBlockReason::None));
	TestEqual(TEXT("post combat: allowed"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::PostCombat)), static_cast<int32>(ESaveBlockReason::None));
	// Real time, tactical pause and turn-based are all WaveCombat.
	TestEqual(TEXT("fight: blocked"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::WaveCombat)), static_cast<int32>(ESaveBlockReason::Combat));
	TestEqual(TEXT("cutscene: blocked"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::Cutscene)), static_cast<int32>(ESaveBlockReason::Cutscene));
	TestEqual(TEXT("game over: blocked"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::GameOver)), static_cast<int32>(ESaveBlockReason::GameOver));
	TestEqual(TEXT("combat hint"), SaveGameRules::GetSaveBlockText(ESaveBlockReason::Combat).ToString(), FString(TEXT("Saving is disabled during combat")));
	TestTrue(TEXT("no hint when allowed"), SaveGameRules::GetSaveBlockText(ESaveBlockReason::None).IsEmpty());
	TestFalse(TEXT("cutscene hint"), SaveGameRules::GetSaveBlockText(ESaveBlockReason::Cutscene).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavePolicyAutosaveTest, "CodexTactics.Save.Policy.AutosaveMoments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavePolicyAutosaveTest::RunTest(const FString&)
{
	using P = ECodexGamePhase;
	TestEqual(TEXT("wave start"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::Preparation, P::WaveCombat)), static_cast<int32>(EAutosaveMoment::BeforeCombat));
	TestEqual(TEXT("ambush start"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::Exploration, P::WaveCombat)), static_cast<int32>(EAutosaveMoment::BeforeCombat));
	TestEqual(TEXT("wave cleared"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::WaveCombat, P::WaveCleared)), static_cast<int32>(EAutosaveMoment::AfterCombat));
	TestEqual(TEXT("pause / turn-based inside the fight"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::WaveCombat, P::WaveCombat)), static_cast<int32>(EAutosaveMoment::None));
	TestEqual(TEXT("next wave's preparation"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::WaveCleared, P::Preparation)), static_cast<int32>(EAutosaveMoment::None));
	TestEqual(TEXT("victory return"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::WaveCleared, P::PostCombat)), static_cast<int32>(EAutosaveMoment::None));
	TestEqual(TEXT("defeat"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::WaveCombat, P::GameOver)), static_cast<int32>(EAutosaveMoment::None));
	TestEqual(TEXT("cutscene"), static_cast<int32>(SaveGameRules::GetAutosaveMoment(P::Exploration, P::Cutscene)), static_cast<int32>(EAutosaveMoment::None));
	TestEqual(TEXT("autosave slot"), SaveGameRules::GetSaveType(SaveGameRules::GetAutosaveSlotName()), FString(TEXT("autosave")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavePolicyLoadFlowTest, "CodexTactics.Save.Policy.LoadFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavePolicyLoadFlowTest::RunTest(const FString&)
{
	using P = ECodexGamePhase;
	auto Check = [this](const TCHAR* What, const FSaveLoadFlow& Flow, P Phase, int32 Wave, bool bUnlocked)
	{
		TestEqual(FString(What) + TEXT(": phase"), Flow.Phase, Phase);
		TestEqual(FString(What) + TEXT(": wave"), Flow.WaveIndex, Wave);
		TestEqual(FString(What) + TEXT(": combat unlocked"), Flow.bCombatUnlocked, bUnlocked);
	};
	// Version 1 (Godot keys only).
	Check(TEXT("v1 exploration"), SaveGameRules::ResolveLoadFlow(1, P::Exploration, false, false, false, 1, 3, false, true), P::Exploration, 1, false);
	Check(TEXT("v1 wave"), SaveGameRules::ResolveLoadFlow(1, P::Exploration, true, false, true, 2, 3, false, true), P::Preparation, 2, true);
	// Version 2.
	Check(TEXT("exploration"), SaveGameRules::ResolveLoadFlow(2, P::Exploration, false, false, false, 0, 3, false, true), P::Exploration, 0, false);
	Check(TEXT("preparation"), SaveGameRules::ResolveLoadFlow(2, P::Preparation, true, true, false, 2, 3, false, true), P::Preparation, 2, true);
	Check(TEXT("cutscene"), SaveGameRules::ResolveLoadFlow(2, P::Cutscene, true, false, false, 0, 3, false, true), P::Preparation, 1, true);
	Check(TEXT("forced in-fight save"), SaveGameRules::ResolveLoadFlow(2, P::WaveCombat, true, false, true, 2, 3, false, true), P::Preparation, 2, true);
	Check(TEXT("ambush fight save"), SaveGameRules::ResolveLoadFlow(2, P::WaveCombat, true, false, true, 1, 3, true, true), P::Exploration, 0, false);
	Check(TEXT("wave 1 of 3 cleared"), SaveGameRules::ResolveLoadFlow(2, P::WaveCleared, true, false, false, 1, 3, false, true), P::Preparation, 2, true);
	Check(TEXT("last wave cleared"), SaveGameRules::ResolveLoadFlow(2, P::WaveCleared, true, false, false, 3, 3, false, true), P::Exploration, 3, false);
	Check(TEXT("ambush won"), SaveGameRules::ResolveLoadFlow(2, P::WaveCleared, true, false, false, 1, 3, true, true), P::Exploration, 1, false);
	Check(TEXT("ambush, more waves allowed"), SaveGameRules::ResolveLoadFlow(2, P::WaveCleared, true, false, false, 1, 3, true, false), P::Preparation, 2, true);
	Check(TEXT("post combat"), SaveGameRules::ResolveLoadFlow(2, P::PostCombat, true, false, false, 3, 3, false, true), P::Exploration, 3, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavePolicyVersionTest, "CodexTactics.Save.Policy.Version",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavePolicyVersionTest::RunTest(const FString&)
{
	TestEqual(TEXT("current format"), SaveGameRules::CurrentSaveVersion, 2);
	for (const ECodexGamePhase Phase : { ECodexGamePhase::Exploration, ECodexGamePhase::Cutscene, ECodexGamePhase::Preparation,
		ECodexGamePhase::WaveCombat, ECodexGamePhase::WaveCleared, ECodexGamePhase::PostCombat, ECodexGamePhase::GameOver })
	{
		ECodexGamePhase Parsed = ECodexGamePhase::GameOver;
		const FString Text = SaveGameRules::PhaseToString(Phase);
		TestTrue(FString::Printf(TEXT("%s parses"), *Text), SaveGameRules::ParsePhase(Text, Parsed) && Parsed == Phase);
	}
	ECodexGamePhase Unchanged = ECodexGamePhase::Preparation;
	TestFalse(TEXT("unknown phase"), SaveGameRules::ParsePhase(TEXT("Nonsense"), Unchanged));
	TestFalse(TEXT("empty phase (version 1)"), SaveGameRules::ParsePhase(FString(), Unchanged));
	TestEqual(TEXT("left alone"), Unchanged, ECodexGamePhase::Preparation);
	return true;
}

#endif
