#include "Misc/AutomationTest.h"
#include "Core/SaveGameRules.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Save policy (user decision 2026-10-08, no Godot reference): saving only outside combat, the flow a load resumes in, save format
// versioning. Rev 5f (user decision 2026-10-14): NO automatic saves - only tutorial checkpoints, F5 and the menu's SAVE GAME.

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSavePolicyNoAutosaveTest, "CodexTactics.Save.Policy.NoAutosave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSavePolicyNoAutosaveTest::RunTest(const FString&)
{
	// Kinds and the load list: checkpoints / quicksaves / manual saves are listed and labelled, a legacy autosave file is hidden.
	TestEqual(TEXT("checkpoint slot"), SaveGameRules::GetSaveType(SaveGameRules::GetCheckpointSlotName()), FString(TEXT("checkpoint")));
	TestEqual(TEXT("quicksave slot"), SaveGameRules::GetSaveType(SaveGameRules::GetQuicksaveSlotName()), FString(TEXT("quicksave")));
	TestFalse(TEXT("legacy autosave files are not listed"), SaveGameRules::IsListedSaveType(SaveGameRules::GetSaveType(TEXT("autosave"))));
	TestTrue(TEXT("checkpoints are listed"), SaveGameRules::IsListedSaveType(TEXT("checkpoint")));
	TestTrue(TEXT("quicksaves and manual saves are listed"), SaveGameRules::IsListedSaveType(TEXT("quicksave")) && SaveGameRules::IsListedSaveType(TEXT("manual")));
	TestEqual(TEXT("checkpoint label"), SaveGameRules::GetSaveTypeLabel(TEXT("checkpoint")), FString(TEXT("CHECKPOINT")));
	TestEqual(TEXT("quicksave label"), SaveGameRules::GetSaveTypeLabel(TEXT("quicksave")), FString(TEXT("QUICKSAVE")));
	TestEqual(TEXT("manual label"), SaveGameRules::GetSaveTypeLabel(TEXT("manual")), FString(TEXT("SAVE")));

	// F9 = the newest of the quicksave and the checkpoint; manual saves and legacy autosaves never.
	auto MakeInfo = [](const TCHAR* Slot, int64 Timestamp)
	{
		FSaveSlotInfo Info;
		Info.SlotName = Slot;
		Info.SaveType = SaveGameRules::GetSaveType(Slot);
		Info.Timestamp = Timestamp;
		return Info;
	};
	TArray<FSaveSlotInfo> Slots = { MakeInfo(TEXT("autosave"), 900), MakeInfo(TEXT("Leonid_03"), 800), MakeInfo(TEXT("checkpoint"), 500), MakeInfo(TEXT("quicksave"), 400) };
	TestEqual(TEXT("F9: the checkpoint (newer than the quicksave; the newer manual save / old autosave do not count)"), SaveGameRules::PickQuickLoadIndex(Slots), 2);
	Slots[3].Timestamp = 600;
	TestEqual(TEXT("F9: the quicksave once it is newer"), SaveGameRules::PickQuickLoadIndex(Slots), 3);
	TestEqual(TEXT("F9: nothing without a quicksave / checkpoint"), SaveGameRules::PickQuickLoadIndex({ MakeInfo(TEXT("autosave"), 1), MakeInfo(TEXT("Leonid_01"), 2) }), INDEX_NONE);

	// No code path writes a save on its own: in the game module (dev checks under Private/Debug excluded) SaveGame( is called only by
	// F5 (QuickSave), the menu's SAVE GAME (SaveToSlotWithMessage) and the tutorial checkpoint (SaveCheckpoint); WriteSave only by
	// SaveGame; nothing names an autosave any more and the save system does not listen to the game flow.
	const FString SourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("CodexTactics"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*.cpp"), true, false);
	TArray<FString> Headers;
	IFileManager::Get().FindFilesRecursive(Headers, *SourceDir, TEXT("*.h"), true, false);
	Files.Append(Headers);
	if (!TestTrue(FString::Printf(TEXT("game module sources found (%s)"), *SourceDir), Files.Num() > 50))
	{
		return false;
	}
	TMap<FString, int32> SaveCalls;
	int32 WriteSaveCalls = 0;
	for (const FString& File : Files)
	{
		const FString Normalized = File.Replace(TEXT("\\"), TEXT("/"));
		if (Normalized.Contains(TEXT("/Private/Debug/")))
		{
			continue;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			continue;
		}
		const FString Name = FPaths::GetCleanFilename(File);
		FRegexMatcher Calls(FRegexPattern(TEXT("(->|\\.|\\s)SaveGame\\s*\\(")), Text);
		while (Calls.FindNext())
		{
			SaveCalls.FindOrAdd(Name) += 1;
		}
		FRegexMatcher Writes(FRegexPattern(TEXT("[^:]WriteSave\\s*\\(")), Text);
		while (Writes.FindNext())
		{
			WriteSaveCalls += (Name == TEXT("SaveGameSubsystem.cpp") || Name == TEXT("SaveGameSubsystem.h")) ? 1 : 100;
		}
		TestFalse(FString::Printf(TEXT("%s names no autosave call"), *Name), Text.Contains(TEXT("Autosave(")) || Text.Contains(TEXT("GetAutosaveMoment")));
		if (Name == TEXT("SaveGameSubsystem.cpp"))
		{
			TestFalse(TEXT("the save system does not listen to the game flow (no save on a fight start / end)"), Text.Contains(TEXT("OnBeforeGameFlowChanged"))
				|| Text.Contains(TEXT("OnGameFlowChanged")));
		}
	}
	TestEqual(TEXT("SaveGameSubsystem.cpp: SaveGame( only in QuickSave and SaveToSlotWithMessage (the declaration line aside)"), SaveCalls.FindRef(TEXT("SaveGameSubsystem.cpp")), 2);
	TestEqual(TEXT("TutorialSubsystem.cpp: SaveGame( only in SaveCheckpoint"), SaveCalls.FindRef(TEXT("TutorialSubsystem.cpp")), 1);
	TestEqual(TEXT("SaveGameSubsystem.h: only the declaration"), SaveCalls.FindRef(TEXT("SaveGameSubsystem.h")), 1);
	int32 Elsewhere = 0;
	for (const TPair<FString, int32>& Entry : SaveCalls)
	{
		if (Entry.Key != TEXT("SaveGameSubsystem.cpp") && Entry.Key != TEXT("TutorialSubsystem.cpp") && Entry.Key != TEXT("SaveGameSubsystem.h"))
		{
			AddError(FString::Printf(TEXT("%s calls SaveGame( %d time(s): a save written without F5 / SAVE GAME / a checkpoint"), *Entry.Key, Entry.Value));
			++Elsewhere;
		}
	}
	TestEqual(TEXT("no other file writes a save"), Elsewhere, 0);
	TestEqual(TEXT("WriteSave( only from SaveGame (+ its declaration)"), WriteSaveCalls, 2);
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
