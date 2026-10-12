#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Core/MissionRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/NarrativeManifest.h"
#include "Dom/JsonObject.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01 tutorial framework: steps JSON contract, command registry, the per-step state machine, lesson rule, save.

#define TUTORIAL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialTestData
{
	const TCHAR* Sample = TEXT(R"JSON({
		"version": 1, "id": "test", "start_beat": "A0",
		"beats": [ { "id": "A0", "lesson": true }, { "id": "A1", "checkpoint": false }, { "id": "FIGHT", "lesson": false } ],
		"steps": [
			{ "id": "card1", "beat": "A0", "kind": "hint_card", "text_id": "TUT_1", "keys": ["Squad.Solo", "Alt+V"],
			  "trigger": { "type": "beat_start" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "card2", "beat": "A0", "kind": "hint_card", "text_id": "TUT_2",
			  "trigger": { "type": "after_step", "step": "card1" }, "complete": { "commands": ["Stance.*"], "count": 2, "timeout_s": 10 } },
			{ "id": "obj", "beat": "A0", "kind": "objective", "text_id": "OBJ_1", "complete": { "volume": "VOL_END" } },
			{ "id": "talk", "beat": "A0", "kind": "walk_talk", "trigger": { "type": "volume", "volume": "VOL_TALK" },
			  "lines": [ { "speaker": "Engineer", "text_id": "WT_1" } ] },
			{ "id": "tip", "beat": "A1", "kind": "hint_card", "trigger": { "type": "first_use", "command": "Grenade.Aim" },
			  "complete": { "timeout_s": 5 } },
			{ "id": "evt", "beat": "A1", "kind": "hint_card", "trigger": { "type": "event", "event": "ColdTier.*", "count": 2 },
			  "complete": { "event": "BarrelLit" } },
			{ "id": "fix", "beat": "A1", "kind": "hint_card", "once": false, "trigger": { "type": "failure_retry", "event": "RelocationRefused" },
			  "complete": { "commands": ["Select.Member"] } },
			{ "id": "delayed", "beat": "A1", "kind": "radio", "trigger": { "type": "manual", "delay_s": 3 }, "lines": [ { "text_id": "R_1" } ] }
		] })JSON");

	bool Load(FAutomationTestBase& Test, FTutorialStateMachine& Machine)
	{
		FTutorialStepsData Data;
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Sample, Data, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("sample parses (%s)"), *Error), bOk);
		Machine.Load(Data);
		Machine.StartBeat(Data.StartBeat);
		return bOk;
	}
}

TUTORIAL_TEST(FTutorialSchemaParseTest, "Schema.Parse")
bool FTutorialSchemaParseTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(TEXT("valid"), TutorialRules::ParseSteps(TutorialTestData::Sample, Data, Warnings, Error));
	TestEqual(TEXT("steps"), Data.Steps.Num(), 8);
	TestEqual(TEXT("beats"), Data.Beats.Num(), 3);
	TestTrue(TEXT("A0 lesson"), Data.Beats[0].bLesson);
	TestFalse(TEXT("A1 no checkpoint"), Data.Beats[1].bCheckpoint);
	const FTutorialStep* Card = Data.FindStep(TEXT("card2"));
	TestNotNull(TEXT("card2"), Card);
	if (Card)
	{
		TestTrue(TEXT("after_step"), Card->Trigger.Type == ETutorialTriggerType::AfterStep && Card->Trigger.Step == FName(TEXT("card1")));
		TestEqual(TEXT("count"), Card->Complete.Count, 2);
		TestEqual(TEXT("timeout"), Card->Complete.TimeoutSeconds, 10.f);
	}
	const FTutorialStep* Objective = Data.FindStep(TEXT("obj"));
	TestTrue(TEXT("no trigger + beat = beat_start"), Objective && Objective->Trigger.Type == ETutorialTriggerType::BeatStart);
	TestTrue(TEXT("tip without condition keeps its timeout"), Data.FindStep(TEXT("tip"))->Complete.TimeoutSeconds == 5.f);
	TestFalse(TEXT("fix may repeat"), Data.FindStep(TEXT("fix"))->bOnce);

	// Errors.
	TestFalse(TEXT("invalid JSON"), TutorialRules::ParseSteps(TEXT("{"), Data, Warnings, Error));
	TestFalse(TEXT("no steps"), TutorialRules::ParseSteps(TEXT("{\"beats\": []}"), Data, Warnings, Error));
	TestFalse(TEXT("duplicate id"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\"},{\"id\":\"a\"}]}"), Data, Warnings, Error));
	TestFalse(TEXT("unknown kind"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\",\"kind\":\"popup\"}]}"), Data, Warnings, Error));
	TestFalse(TEXT("unknown trigger"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\",\"trigger\":{\"type\":\"telepathy\"}}]}"), Data, Warnings, Error));
	TestFalse(TEXT("after_step unknown"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\",\"trigger\":{\"type\":\"after_step\",\"step\":\"zz\"}}]}"), Data, Warnings, Error));
	TestFalse(TEXT("event without name"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\",\"trigger\":{\"type\":\"event\"}}]}"), Data, Warnings, Error));

	// Soft problems: blocking forced off, undeclared beat added, card without completion gets a timeout.
	Warnings.Reset();
	TestTrue(TEXT("soft"), TutorialRules::ParseSteps(TEXT("{\"steps\": [{\"id\":\"a\",\"beat\":\"B9\",\"blocking\":true,\"text_id\":\"T\"}]}"), Data, Warnings, Error));
	TestFalse(TEXT("never blocking"), Data.Steps[0].bBlocking);
	TestEqual(TEXT("beat added"), Data.FindBeatIndex(TEXT("B9")), 0);
	TestEqual(TEXT("default card timeout"), Data.Steps[0].Complete.TimeoutSeconds, 12.f);
	TestTrue(TEXT("warnings"), Warnings.Num() >= 3);
	return true;
}

TUTORIAL_TEST(FTutorialDemoFileTest, "Schema.DemoFile")
bool FTutorialDemoFileTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(FString::Printf(TEXT("demo_01_steps.json loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error));
	for (const FString& Warning : Warnings)
	{
		AddError(TEXT("demo file warning: ") + Warning);
	}
	TestEqual(TEXT("A0..A2 + A6"), Data.Beats.Num(), 4);
	TestEqual(TEXT("start beat"), Data.StartBeat, FName(TEXT("A0")));
	TestNotNull(TEXT("solo card"), Data.FindStep(TEXT("A1_solo")));
	TestNotNull(TEXT("barrel push card"), Data.FindStep(TEXT("A2_push")));
	TestNotNull(TEXT("heat pack card"), Data.FindStep(TEXT("A2_heatpack")));
	// A6 sample: each operative into cover (distinct operatives) and "enter cover 4 times".
	const FTutorialStep* SquadCover = Data.FindStep(TEXT("A6_squad_cover"));
	TestTrue(TEXT("A6 each operative into cover"), SquadCover && SquadCover->Complete.bDistinctSubjects
		&& SquadCover->Complete.Event == TEXT("CoverEntered") && SquadCover->Complete.Count == 3);
	const FTutorialStep* Bound = Data.FindStep(TEXT("A6_bound"));
	TestTrue(TEXT("A6 enter cover 4 times"), Bound && !Bound->Complete.bDistinctSubjects && Bound->Complete.Count == 4);
	for (const FTutorialStep& Step : Data.Steps)
	{
		TestFalse(TEXT("never blocking"), Step.bBlocking);
	}
	return true;
}

TUTORIAL_TEST(FTutorialCommandRegistryTest, "Commands.Registry")
bool FTutorialCommandRegistryTest::RunTest(const FString&)
{
	TSet<FName> Ids;
	for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
	{
		TestFalse(TEXT("unique id ") + Info.Id.ToString(), Ids.Contains(Info.Id));
		Ids.Add(Info.Id);
		TestTrue(TEXT("key label ") + Info.Id.ToString(), FCString::Strlen(Info.KeyLabel) > 0);
	}
	for (const FName Id : { PlayerCommands::SoloOn, PlayerCommands::Regroup, PlayerCommands::StanceProne, PlayerCommands::SquadStanceProne,
		PlayerCommands::CoverEnter, PlayerCommands::Relocate, PlayerCommands::LightBarrel, PlayerCommands::UseHeatPack, PlayerCommands::DisarmMine,
		PlayerCommands::DeployTripwire, PlayerCommands::TargetedShot, PlayerCommands::GrenadeAim, PlayerCommands::TacticalPause,
		PlayerCommands::TurnBasedEnter, PlayerCommands::EndTurn, PlayerCommands::QuickSave })
	{
		TestNotNull(TEXT("registered ") + Id.ToString(), PlayerCommands::Find(Id));
	}
	TestTrue(TEXT("exact"), PlayerCommands::Matches(TEXT("Squad.Solo"), PlayerCommands::SoloOn));
	TestTrue(TEXT("case"), PlayerCommands::Matches(TEXT("squad.solo"), PlayerCommands::SoloOn));
	TestTrue(TEXT("prefix"), PlayerCommands::Matches(TEXT("Stance.*"), PlayerCommands::SquadStanceProne));
	TestTrue(TEXT("any"), PlayerCommands::Matches(TEXT("*"), PlayerCommands::Move));
	TestFalse(TEXT("other"), PlayerCommands::Matches(TEXT("Stance.*"), PlayerCommands::SoloOn));
	TestFalse(TEXT("empty"), PlayerCommands::Matches(TEXT(""), PlayerCommands::SoloOn));
	TestEqual(TEXT("key of id"), PlayerCommands::FormatKeyToken(TEXT("Squad.Solo")), FString(TEXT("[B]")));
	TestEqual(TEXT("literal key"), PlayerCommands::FormatKeyToken(TEXT("Alt+V")), FString(TEXT("[Alt+V]")));
	TestEqual(TEXT("tokens"), PlayerCommands::ResolveKeyTokens(TEXT("Press {key:Squad.Solo} then {key:Stance.Squad.Prone}.")),
		FString(TEXT("Press [B] then [Alt+V].")));
	TestEqual(TEXT("keys row"), TutorialRules::FormatKeys({ TEXT("Squad.Solo"), TEXT("Alt+V") }), FString(TEXT("[B]  [Alt+V]")));
	return true;
}

TUTORIAL_TEST(FTutorialTriggerCompleteTest, "StateMachine.TriggerAndComplete")
bool FTutorialTriggerCompleteTest::RunTest(const FString&)
{
	FTutorialStateMachine Machine;
	TutorialTestData::Load(*this, Machine);
	TestEqual(TEXT("beat A0"), Machine.GetCurrentBeat(), FName(TEXT("A0")));
	TestEqual(TEXT("card1 shown at beat start"), Machine.GetActiveCard(), FName(TEXT("card1")));
	TestEqual(TEXT("objective shown"), Machine.GetActiveObjectives().Num(), 1);

	Machine.HandleCommand(PlayerCommands::Move);
	TestEqual(TEXT("other command: still active"), Machine.GetState(TEXT("card1")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::SoloOn);
	TestEqual(TEXT("B completes card1"), Machine.GetState(TEXT("card1")), ETutorialStepState::Completed);
	TestEqual(TEXT("card2 waits for the breather"), Machine.GetState(TEXT("card2")), ETutorialStepState::Queued);
	Machine.Tick(1.f);
	TestEqual(TEXT("still breathing"), Machine.GetActiveCard(), FName());
	Machine.Tick(0.6f);
	TestEqual(TEXT("card2 shown after 1.5 s"), Machine.GetActiveCard(), FName(TEXT("card2")));
	Machine.HandleCommand(PlayerCommands::StanceCrouch);
	TestEqual(TEXT("count 1 of 2"), Machine.GetProgress(TEXT("card2")), 1);
	Machine.HandleCommand(PlayerCommands::SquadStanceProne);
	TestEqual(TEXT("count 2 completes"), Machine.GetState(TEXT("card2")), ETutorialStepState::Completed);

	// Volumes: a trigger volume fires the walk-and-talk, the objective completes on its volume.
	Machine.HandleVolume(TEXT("VOL_TALK"), NAME_None, NAME_None);
	TestEqual(TEXT("walk-and-talk active"), Machine.GetActiveSpeech(), FName(TEXT("talk")));
	TestTrue(TEXT("lines done"), Machine.CompleteStep(TEXT("talk")));
	Machine.HandleVolume(TEXT("VOL_END"), NAME_None, TEXT("A1"));
	TestEqual(TEXT("objective done"), Machine.GetState(TEXT("obj")), ETutorialStepState::Completed);
	TestEqual(TEXT("beat A1"), Machine.GetCurrentBeat(), FName(TEXT("A1")));
	TestFalse(TEXT("not backwards"), Machine.StartBeat(TEXT("A0")));

	// first_use: only the first time the command is seen.
	Machine.HandleCommand(PlayerCommands::GrenadeAim);
	Machine.Tick(2.f);
	TestEqual(TEXT("first use tip shown"), Machine.GetActiveCard(), FName(TEXT("tip")));
	Machine.Tick(5.f);
	TestEqual(TEXT("tip timed out"), Machine.GetState(TEXT("tip")), ETutorialStepState::TimedOut);

	// event with count + wildcard; completion by event.
	Machine.HandleEvent(TEXT("ColdTier.Chills"));
	TestEqual(TEXT("one of two"), Machine.GetState(TEXT("evt")), ETutorialStepState::Idle);
	Machine.HandleEvent(TEXT("ColdTier.Freezing"));
	Machine.Tick(2.f);
	TestEqual(TEXT("event card shown"), Machine.GetActiveCard(), FName(TEXT("evt")));
	Machine.HandleEvent(TEXT("BarrelLit"));
	TestEqual(TEXT("barrel lit completes"), Machine.GetState(TEXT("evt")), ETutorialStepState::Completed);

	// delayed manual step.
	TestTrue(TEXT("manual trigger"), Machine.TriggerStep(TEXT("delayed")));
	TestEqual(TEXT("waiting"), Machine.GetState(TEXT("delayed")), ETutorialStepState::Waiting);
	Machine.Tick(3.1f);
	TestEqual(TEXT("radio active after delay"), Machine.GetActiveSpeech(), FName(TEXT("delayed")));
	return true;
}

TUTORIAL_TEST(FTutorialOnceSkipTest, "StateMachine.OnceSkipReplay")
bool FTutorialOnceSkipTest::RunTest(const FString&)
{
	FTutorialStateMachine Machine;
	TutorialTestData::Load(*this, Machine);
	TestTrue(TEXT("skip card1"), Machine.SkipStep(TEXT("card1")));
	TestEqual(TEXT("skipped"), Machine.GetState(TEXT("card1")), ETutorialStepState::Skipped);
	TestEqual(TEXT("after_step fires on skip too"), Machine.GetState(TEXT("card2")), ETutorialStepState::Queued);
	TestFalse(TEXT("once: no re-trigger"), Machine.TriggerStep(TEXT("card1")));
	TestTrue(TEXT("replay"), Machine.ReplayStep(TEXT("card1")));
	TestTrue(TEXT("replayed card queued"), TutorialRules::IsRunning(Machine.GetState(TEXT("card1"))));

	// Silent completion: done while still queued, never shown.
	Machine.HandleCommand(PlayerCommands::StanceCrouch);
	Machine.HandleCommand(PlayerCommands::StanceProne);
	bool bSilent = false;
	for (const FTutorialTransition& Transition : Machine.ConsumeTransitions())
	{
		bSilent |= Transition.StepId == FName(TEXT("card2")) && Transition.To == ETutorialStepState::Completed && Transition.bSilent;
	}
	TestTrue(TEXT("card2 completed silently while queued"), bSilent);

	// failure_retry: repeats (once = false) but at most once per remedial cooldown.
	Machine.StartBeat(TEXT("A1"));
	Machine.HandleEvent(TEXT("RelocationRefused"));
	Machine.Tick(2.f);
	TestTrue(TEXT("remedial fired"), TutorialRules::IsRunning(Machine.GetState(TEXT("fix"))));
	Machine.HandleCommand(PlayerCommands::SelectMember);
	TestEqual(TEXT("remedial done"), Machine.GetState(TEXT("fix")), ETutorialStepState::Completed);
	Machine.HandleEvent(TEXT("RelocationRefused"));
	TestEqual(TEXT("cooldown holds it"), Machine.GetState(TEXT("fix")), ETutorialStepState::Completed);
	Machine.Tick(61.f);
	Machine.HandleEvent(TEXT("RelocationRefused"));
	TestTrue(TEXT("fires again after the cooldown"), TutorialRules::IsRunning(Machine.GetState(TEXT("fix"))));
	return true;
}

TUTORIAL_TEST(FTutorialPacingTest, "StateMachine.OneCardThreeObjectives")
bool FTutorialPacingTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(TEXT("parse"), TutorialRules::ParseSteps(TEXT(R"({"start_beat":"B","beats":[{"id":"B"}],"steps":[
		{"id":"c1","beat":"B","complete":{"commands":["Order.Move"]}},{"id":"c2","beat":"B","complete":{"commands":["Order.Move"]}},
		{"id":"o1","beat":"B","kind":"objective","complete":{"event":"E"}},{"id":"o2","beat":"B","kind":"objective","complete":{"event":"E"}},
		{"id":"o3","beat":"B","kind":"objective","complete":{"event":"E"}},{"id":"o4","beat":"B","kind":"objective","complete":{"event":"F"}}]})"), Data, Warnings, Error));
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("B"));
	TestEqual(TEXT("one card"), Machine.GetActiveCard(), FName(TEXT("c1")));
	TestEqual(TEXT("three objectives"), Machine.GetActiveObjectives().Num(), 3);
	TestEqual(TEXT("fourth queued"), Machine.GetState(TEXT("o4")), ETutorialStepState::Queued);
	Machine.HandleCommand(PlayerCommands::Move); // completes the active c1 and the queued c2 (silently)
	TestEqual(TEXT("c2 done while queued"), Machine.GetState(TEXT("c2")), ETutorialStepState::Completed);
	Machine.HandleEvent(TEXT("E"));
	TestEqual(TEXT("o4 moves up"), Machine.GetActiveObjectives().Num(), 1);
	int32 Index = 0;
	int32 Count = 0;
	Machine.GetCardProgress(TEXT("c2"), Index, Count);
	TestTrue(TEXT("2 / 2"), Index == 2 && Count == 2);
	return true;
}

TUTORIAL_TEST(FTutorialLessonRuleTest, "Rules.LessonDowned")
bool FTutorialLessonRuleTest::RunTest(const FString&)
{
	TestTrue(TEXT("lesson + tutorial on: downed"), TutorialRules::ShouldDownInsteadOfKill(true, true));
	TestFalse(TEXT("tutorial off: normal death"), TutorialRules::ShouldDownInsteadOfKill(false, true));
	TestFalse(TEXT("fight beat: normal death"), TutorialRules::ShouldDownInsteadOfKill(true, false));
	TestEqual(TEXT("floor 1 HP"), TutorialRules::LessonHealthFloor, 1.f);

	FTutorialStateMachine Machine;
	TutorialTestData::Load(*this, Machine);
	TestTrue(TEXT("A0 is a lesson"), Machine.IsLessonBeat());
	Machine.StartBeat(TEXT("FIGHT"));
	TestFalse(TEXT("the fight beat is not"), Machine.IsLessonBeat());

	TestEqual(TEXT("line override"), TutorialRules::LineDurationSeconds(TEXT("abc"), 4.f), 4.f);
	TestEqual(TEXT("short line min"), TutorialRules::LineDurationSeconds(TEXT("Go.")), 2.5f);
	TestEqual(TEXT("long line max"), TutorialRules::LineDurationSeconds(FString::ChrN(400, TEXT('a'))), 9.f);
	TestEqual(TEXT("placeholder"), TutorialRules::MakeMissingText(TEXT("TUT_X")), FString(TEXT("[EN missing: TUT_X]")));
	return true;
}

TUTORIAL_TEST(FTutorialSaveTest, "Save.CompletedSteps")
bool FTutorialSaveTest::RunTest(const FString&)
{
	FTutorialStateMachine Machine;
	TutorialTestData::Load(*this, Machine);
	Machine.HandleCommand(PlayerCommands::SoloOn); // card1 completed, card2 queued
	TestTrue(TEXT("skip card2"), Machine.SkipStep(TEXT("card2")));
	Machine.HandleVolume(TEXT("VOL_TALK"), NAME_None, NAME_None); // talk active (pending)
	const FTutorialProgress Saved = Machine.ExportProgress();
	const TSharedRef<FJsonObject> Json = TutorialRules::SaveProgress(Saved, true);

	FTutorialProgress Loaded;
	TestTrue(TEXT("loads"), TutorialRules::LoadProgress(*Json, Loaded));
	TestEqual(TEXT("beat"), Loaded.CurrentBeat, FName(TEXT("A0")));
	TestEqual(TEXT("card1 completed"), Loaded.Finished.FindRef(TEXT("card1")), ETutorialStepState::Completed);
	TestEqual(TEXT("card2 skipped"), Loaded.Finished.FindRef(TEXT("card2")), ETutorialStepState::Skipped);
	TestTrue(TEXT("talk pending"), Loaded.Pending.Contains(FName(TEXT("talk"))));

	FTutorialStateMachine Restored;
	TutorialTestData::Load(*this, Restored);
	Restored.ResetProgress();
	Restored.ImportProgress(Loaded);
	TestEqual(TEXT("restored beat"), Restored.GetCurrentBeat(), FName(TEXT("A0")));
	TestEqual(TEXT("restored completed"), Restored.GetState(TEXT("card1")), ETutorialStepState::Completed);
	TestFalse(TEXT("completed once-step does not come back"), Restored.TriggerStep(TEXT("card1")));
	TestEqual(TEXT("pending re-queued and shown"), Restored.GetActiveSpeech(), FName(TEXT("talk")));
	TestFalse(TEXT("not a save object"), TutorialRules::LoadProgress(FJsonObject(), Loaded));

	FTutorialTelemetryEntry Entry;
	Entry.Event = TEXT("shown");
	Entry.Step = TEXT("card1");
	Entry.ActiveSeconds = 1.234f;
	const FString Line = TutorialRules::BuildTelemetryLine(Entry);
	TestTrue(TEXT("one json line"), Line.StartsWith(TEXT("{")) && !Line.Contains(TEXT("\n")) && Line.Contains(TEXT("\"step\":\"card1\"")));
	return true;
}

TUTORIAL_TEST(FTutorialTextAndDemoFlagsTest, "Text.ManifestAndDemoFlags")
bool FTutorialTextAndDemoFlagsTest::RunTest(const FString&)
{
	FNarrativeManifest Manifest;
	FString Error;
	TestTrue(TEXT("manifest"), FNarrativeManifest::Parse(TEXT(R"({"sequences":{"WT_SEQ":{"lines":[{"speaker_en":"Engineer","text_en":"Dry."}]}},
		"tutorial":{"TUT_A":{"title_en":"Split up","text_en":"Press {key:Squad.Solo}.","speaker_en":"HQ"},"TUT_RU":{"text_en":"Привет"}}})"), Manifest, Error)); // cyrillic-ok: test data
	const FTutorialTextView Card = Manifest.GetTutorialText(TEXT("TUT_A"));
	TestEqual(TEXT("title"), Card.Title, FString(TEXT("Split up")));
	TestFalse(TEXT("found"), Card.bMissing);
	TestEqual(TEXT("key token resolved"), PlayerCommands::ResolveKeyTokens(Card.Text), FString(TEXT("Press [B].")));
	TestEqual(TEXT("sequence fallback"), Manifest.GetTutorialText(TEXT("WT_SEQ")).Text, FString(TEXT("Dry.")));
	const FTutorialTextView Missing = Manifest.GetTutorialText(TEXT("TUT_NONE"));
	TestTrue(TEXT("missing"), Missing.bMissing);
	TestEqual(TEXT("placeholder"), Missing.Text, FString(TEXT("[EN missing: TUT_NONE]")));
	TestTrue(TEXT("cyrillic counts as missing"), Manifest.GetTutorialText(TEXT("TUT_RU")).bMissing);

	// Lookup order: manifest, then the DRAFT texts, then the placeholder.
	TestTrue(TEXT("draft parses"), FNarrativeManifest::ParseDraftTexts(TEXT(R"({"texts":{"TUT_A":{"title":"Draft A","text":"draft a"},"TUT_D":{"title":"Draft","text":"Press {key:Camera.Pan}."},"WT_D":{"text":"Radio draft."},"BEAT_D":{"title":"THE DROP"}}})"),
		Manifest.DraftTutorialTexts, Error));
	TestEqual(TEXT("manifest wins over the draft"), Manifest.GetTutorialText(TEXT("TUT_A")).Title, FString(TEXT("Split up")));
	const FTutorialTextView Draft = Manifest.GetTutorialText(TEXT("TUT_D"));
	TestFalse(TEXT("draft found"), Draft.bMissing);
	TestEqual(TEXT("draft title"), Draft.Title, FString(TEXT("Draft")));
	TestFalse(TEXT("draft key token resolves"), PlayerCommands::ResolveKeyTokens(Draft.Text).Contains(TEXT("{key:")));
	TestEqual(TEXT("draft line"), Manifest.GetTutorialText(TEXT("WT_D")).Text, FString(TEXT("Radio draft.")));
	TestEqual(TEXT("title-only draft"), Manifest.GetTutorialText(TEXT("BEAT_D")).Text, FString(TEXT("THE DROP")));
	TestEqual(TEXT("still missing -> placeholder"), Manifest.GetTutorialText(TEXT("TUT_NONE")).Text, FString(TEXT("[EN missing: TUT_NONE]")));
	TestFalse(TEXT("bad draft json rejected"), FNarrativeManifest::ParseDraftTexts(TEXT("{ nope"), Manifest.DraftTutorialTexts, Error));
	Manifest.DraftTutorialTexts.Reset();

	FLevelCombatConfig Level;
	TestTrue(TEXT("level with demo"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"d","demo":{"panic":false,"vision_cones":true,"tutorial":true,"hud":"demo"}})"),
		TEXT("d"), Level, Error));
	TestTrue(TEXT("demo"), Level.Demo.bDemo);
	TestFalse(TEXT("panic off"), Level.Demo.bPanicEnabled);
	TestTrue(TEXT("cones forced on"), Level.Demo.bOverrideVisionCones && Level.Demo.bVisionCones);
	TestTrue(TEXT("tutorial"), Level.Demo.bTutorial);
	TestEqual(TEXT("default steps file"), Level.Demo.TutorialSteps, FString(TEXT("demo_01_steps.json")));
	TestTrue(TEXT("demo HUD"), Level.Demo.bDemoHud);
	TestFalse(TEXT("demo object without the key: no intro briefing"), Level.Demo.bIntroDialogue);
	TestTrue(TEXT("intro_dialogue true"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"d","demo":{"intro_dialogue":true}})"), TEXT("d"), Level, Error));
	TestTrue(TEXT("intro kept"), Level.Demo.bIntroDialogue);
	TestTrue(TEXT("intro_dialogue false"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"d","demo":{"intro_dialogue":false}})"), TEXT("d"), Level, Error));
	TestFalse(TEXT("intro off"), Level.Demo.bIntroDialogue);
	TestTrue(TEXT("level without demo"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"n"})"), TEXT("n"), Level, Error));
	TestTrue(TEXT("normal level keeps the intro"), Level.Demo.bIntroDialogue);
	TestTrue(TEXT("intro opens: normal level"), MissionRules::ShouldOpenIntroDialogue(false, false, true));
	TestTrue(TEXT("intro opens: demo with intro_dialogue true"), MissionRules::ShouldOpenIntroDialogue(false, true, true));
	TestFalse(TEXT("no intro: demo with intro_dialogue false"), MissionRules::ShouldOpenIntroDialogue(false, true, false));
	TestFalse(TEXT("no intro: headless start"), MissionRules::ShouldOpenIntroDialogue(true, false, true));
	TestFalse(TEXT("normal game"), Level.Demo.bDemo || Level.Demo.bTutorial || Level.Demo.bOverrideVisionCones || !Level.Demo.bPanicEnabled);
	return true;
}

TUTORIAL_TEST(FTutorialDistinctOperativesTest, "StateMachine.DistinctOperatives")
bool FTutorialDistinctOperativesTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(TEXT("parse"), TutorialRules::ParseSteps(TEXT(R"({"start_beat":"B","beats":[{"id":"B"}],"steps":[
		{"id":"each","beat":"B","kind":"objective","text_id":"OBJ_EACH","complete":{"event":"CoverEntered","count":2,"distinct_operatives":true}},
		{"id":"four","beat":"B","kind":"objective","text_id":"OBJ_FOUR","complete":{"event":"CoverEntered","count":4}}]})"), Data, Warnings, Error));
	TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
	TestTrue(TEXT("distinct flag parsed"), Data.Steps.Num() == 2 && Data.Steps[0].Complete.bDistinctSubjects && !Data.Steps[1].Complete.bDistinctSubjects);
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("B"));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Commander"));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Commander"));
	Machine.HandleEvent(TEXT("CoverEntered")); // no subject: never counts for a distinct step
	TestEqual(TEXT("the same operative twice counts once"), Machine.GetProgress(TEXT("each")), 1);
	TestEqual(TEXT("the plain count counts all three"), Machine.GetProgress(TEXT("four")), 3);
	TestEqual(TEXT("not done yet"), Machine.GetState(TEXT("each")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("CoverEntered.High"), TEXT("Engineer")); // a sub-event does not match the exact pattern
	TestEqual(TEXT("sub-event ignored"), Machine.GetProgress(TEXT("each")), 1);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Engineer"));
	TestEqual(TEXT("two different operatives complete it"), Machine.GetState(TEXT("each")), ETutorialStepState::Completed);
	TestEqual(TEXT("the fourth entry completes the plain count"), Machine.GetState(TEXT("four")), ETutorialStepState::Completed);

	// Without "event" the flag is warned about (it only counts events).
	TArray<FString> FlagWarnings;
	FTutorialStepsData FlagOnly;
	TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"x","beat":"B","complete":{"commands":["Cover.Enter"],"distinct_operatives":true}}]})"), FlagOnly, FlagWarnings, Error);
	TestTrue(TEXT("warned"), FlagWarnings.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("distinct_operatives")); }));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
