#include "Misc/AutomationTest.h"
#include "Core/SaveGameRules.h"
#include "Core/SaveGameSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Save-system audit 2026-10-19 (no Godot reference): the restore order is one explicit list, slot metadata parses from any JSON
// (missing / mistyped keys -> defaults, a non-save -> skipped), the modal dialogue blocks a save.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveRestoreOrderTest, "CodexTactics.Save.Policy.RestoreOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSaveRestoreOrderTest::RunTest(const FString&)
{
	// The documented order (docs/systems/save-load.md "Restore order"): a change here is a change there.
	const TArray<FName> Expected = { TEXT("Reset"), TEXT("SquadTransfer"), TEXT("Tutorial"), TEXT("Squad"), TEXT("GameState"), TEXT("Quests"),
		TEXT("World"), TEXT("Vehicles"), TEXT("Enemies"), TEXT("Stealth"), TEXT("UI") };
	const TArray<FName> Order = USaveGameSubsystem::GetRestoreOrder();
	TestEqual(TEXT("11 restore steps"), Order.Num(), Expected.Num());
	for (int32 Index = 0; Index < Expected.Num() && Index < Order.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("step %d"), Index + 1), Order[Index].ToString(), Expected[Index].ToString());
	}
	// The invariants behind the order.
	auto IndexOf = [&Order](const TCHAR* Name) { return Order.IndexOfByKey(FName(Name)); };
	TestTrue(TEXT("the hand-over counts before the tutorial (its conditions read them)"), IndexOf(TEXT("SquadTransfer")) < IndexOf(TEXT("Tutorial")));
	TestTrue(TEXT("the tutorial before the squad is placed (a teleport into a volume meets its saved fired flag)"), IndexOf(TEXT("Tutorial")) < IndexOf(TEXT("Squad")));
	TestTrue(TEXT("the squad before its state (leader, Solo, postures act on restored operatives)"), IndexOf(TEXT("Squad")) < IndexOf(TEXT("GameState")));
	TestTrue(TEXT("the quest chain before the world (a powered gate starts opening, the world snaps it)"), IndexOf(TEXT("Quests")) < IndexOf(TEXT("World")));
	TestTrue(TEXT("vehicles after the squad (crews) and the world"), IndexOf(TEXT("Vehicles")) > IndexOf(TEXT("Squad")) && IndexOf(TEXT("Vehicles")) > IndexOf(TEXT("World")));
	TestTrue(TEXT("enemies after the world (routes, cover objects)"), IndexOf(TEXT("Enemies")) > IndexOf(TEXT("World")));
	TestTrue(TEXT("stealth cooldowns after the squad"), IndexOf(TEXT("Stealth")) > IndexOf(TEXT("Squad")));
	TestEqual(TEXT("UI last"), Order.Last().ToString(), FString(TEXT("UI")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveSlotInfoParseTest, "CodexTactics.Save.Policy.SlotInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSaveSlotInfoParseTest::RunTest(const FString&)
{
	auto Parse = [](const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Data;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Json)), Data);
		return Data;
	};
	FSaveSlotInfo Info;
	// A full header.
	TSharedPtr<FJsonObject> Full = Parse(TEXT(R"({"version":3,"timestamp":1700000000,"datetime_str":"2026-10-19 12:00:00","save_type":"quicksave",
		"slot_name":"quicksave","title":"Quicksave: Checkpoint","author":"Commander","stage_name":"Checkpoint perimeter","squad_summary":"Operatives: 3/3 | HP: 90%",
		"map":"/Game/Maps/L_MovementTest","squad":[{},{},{}],"game_state":{"current_wave_index":2}})"));
	TestTrue(TEXT("full header parses"), Full.IsValid() && SaveGameRules::ParseSlotInfo(*Full, TEXT("quicksave"), Info));
	TestEqual(TEXT("version"), Info.Version, 3);
	TestEqual(TEXT("wave"), Info.Wave, 2);
	TestEqual(TEXT("squad count"), Info.SquadCount, 3);
	TestEqual(TEXT("map"), Info.MapName, FString(TEXT("/Game/Maps/L_MovementTest")));
	TestEqual(TEXT("timestamp"), Info.Timestamp, static_cast<int64>(1700000000));
	TestEqual(TEXT("type"), Info.SaveType, FString(TEXT("quicksave")));

	// Missing keys: the defaults, never a crash (a version 1 / hand-edited file).
	TSharedPtr<FJsonObject> Sparse = Parse(TEXT(R"({"squad":[]})"));
	TestTrue(TEXT("a file with only a squad list is a save"), Sparse.IsValid() && SaveGameRules::ParseSlotInfo(*Sparse, TEXT("Leonid_02"), Info));
	TestEqual(TEXT("version defaults to 1"), Info.Version, 1);
	TestEqual(TEXT("slot name from the file name"), Info.SlotName, FString(TEXT("Leonid_02")));
	TestEqual(TEXT("title from the slot"), Info.Title, FString(TEXT("Leonid_02")));
	TestEqual(TEXT("type from the slot name"), Info.SaveType, FString(TEXT("manual")));
	TestEqual(TEXT("author"), Info.Author, FString(TEXT("Commander")));
	TestEqual(TEXT("stage"), Info.StageName, FString(TEXT("Unknown stage")));
	TestEqual(TEXT("wave 1"), Info.Wave, 1);
	TestEqual(TEXT("no squad"), Info.SquadCount, 0);
	TestTrue(TEXT("no map"), Info.MapName.IsEmpty());

	// Mistyped keys: a non-number counts as missing; a number in a text field becomes its text (UE's TryGetStringField), which for the
	// map means "a map that does not exist" - refused by IsSaveMapAvailable, never a travel to nowhere.
	TSharedPtr<FJsonObject> Wrong = Parse(TEXT(R"({"version":"three","timestamp":"yesterday","squad":{"not":"a list"},"game_state":[1,2],"map":12,"title":5})"));
	TestTrue(TEXT("mistyped keys still parse"), Wrong.IsValid() && SaveGameRules::ParseSlotInfo(*Wrong, TEXT("odd"), Info));
	TestEqual(TEXT("mistyped version -> 1"), Info.Version, 1);
	TestEqual(TEXT("mistyped timestamp -> 0"), Info.Timestamp, static_cast<int64>(0));
	TestEqual(TEXT("mistyped squad -> 0"), Info.SquadCount, 0);
	TestEqual(TEXT("mistyped game_state -> wave 1"), Info.Wave, 1);
	TestFalse(TEXT("a numeric map is no map of this build"), USaveGameSubsystem::IsSaveMapAvailable(Info));
	TestEqual(TEXT("a numeric title becomes its text"), Info.Title, FString(TEXT("5")));

	// Not a save at all: skipped (the list hides it, a load refuses it).
	TSharedPtr<FJsonObject> Foreign = Parse(TEXT(R"({"settings":{"volume":0.5},"title":"options"})"));
	TestTrue(TEXT("foreign JSON object parses"), Foreign.IsValid());
	TestFalse(TEXT("... but is no save"), Foreign.IsValid() && SaveGameRules::ParseSlotInfo(*Foreign, TEXT("options"), Info));
	TestFalse(TEXT("an empty object is no save"), SaveGameRules::ParseSlotInfo(FJsonObject(), TEXT("empty"), Info));

	// A future format is still described (the load applies what it knows).
	TSharedPtr<FJsonObject> Future = Parse(TEXT(R"({"version":99,"game_state":{}})"));
	TestTrue(TEXT("future version parses"), Future.IsValid() && SaveGameRules::ParseSlotInfo(*Future, TEXT("new"), Info) && Info.Version == 99);
	TestTrue(TEXT("a save without a map counts as loadable on the current level"), Info.MapName.IsEmpty() && USaveGameSubsystem::IsSaveMapAvailable(Info));
	Info.MapName = TEXT("/Game/Maps/L_DoesNotExist_Audit");
	TestFalse(TEXT("a missing map is detected"), USaveGameSubsystem::IsSaveMapAvailable(Info));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDialogueBlockTest, "CodexTactics.Save.Policy.DialogueBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSaveDialogueBlockTest::RunTest(const FString&)
{
	TestEqual(TEXT("exploration, no dialogue: allowed"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::Exploration, false)),
		static_cast<int32>(ESaveBlockReason::None));
	TestEqual(TEXT("exploration, dialogue open: blocked"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::Exploration, true)),
		static_cast<int32>(ESaveBlockReason::Dialogue));
	TestEqual(TEXT("the fight wins over the dialogue reason"), static_cast<int32>(SaveGameRules::GetSaveBlockReason(ECodexGamePhase::WaveCombat, true)),
		static_cast<int32>(ESaveBlockReason::Combat));
	TestFalse(TEXT("dialogue hint"), SaveGameRules::GetSaveBlockText(ESaveBlockReason::Dialogue).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
