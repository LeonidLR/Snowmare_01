#include "Misc/AutomationTest.h"
#include "Core/LoadoutRules.h"
#include "Core/PlayerCommands.h"
#include "Data/LevelJsonRules.h"
#include "Data/NarrativeManifest.h"
#include "Dom/JsonObject.h"
#include "Survival/ColdRules.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01 A0-A1 redesign (user decision 2026-10-12): tutorial command locks (sprint locked until warmed up), condition
// triggers / "requires", the gust safety action, the "feed" kind, the heat-pack loot lesson order and the demo loadout.

#define TUTORIAL_LOCK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialLockTestData
{
	const TCHAR* Locks = TEXT(R"JSON({
		"id": "locks", "start_beat": "A0",
		"beats": [ { "id": "A0", "lock": ["Order.Sprint"] }, { "id": "A1" }, { "id": "A2", "unlock": ["Order.Sprint"] } ],
		"steps": [
			{ "id": "warmed", "beat": "A1", "kind": "walk_talk", "trigger": { "type": "manual" }, "unlock": ["Order.Sprint"],
			  "lines": [ { "speaker": "Commander", "text_id": "WT_X" } ] },
			{ "id": "freeze_all", "beat": "A1", "kind": "hint_card", "trigger": { "type": "manual" }, "lock": ["Order.*"],
			  "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "nosprint", "beat": "A0", "kind": "feed", "text_id": "TUT_X",
			  "trigger": { "type": "event", "event": "Tutorial.Locked.Order.Sprint" } },
			{ "id": "card", "beat": "A0", "kind": "hint_card", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "talk", "beat": "A0", "kind": "walk_talk", "trigger": { "type": "manual" }, "lines": [ { "text_id": "WT_Y" } ] }
		] })JSON");

	const TCHAR* Gust = TEXT(R"JSON({
		"id": "gust", "start_beat": "A1", "beats": [ { "id": "A1" } ],
		"steps": [
			{ "id": "solo", "beat": "A1", "kind": "hint_card", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "gust", "beat": "A1", "kind": "walk_talk", "trigger": { "type": "after_step", "step": "solo", "delay_s": 20 },
			  "condition": { "stat": "squad_cold_max", "below": 40 }, "action": { "type": "cold_bump", "amount": 20, "min_level": 42 },
			  "lines": [ { "speaker": "Medic-Sapper", "text_id": "WT_GUST" } ] },
			{ "id": "after_gust", "beat": "A1", "kind": "objective", "trigger": { "type": "after_step", "step": "gust" }, "complete": { "event": "Never" } }
		] })JSON");

	const TCHAR* Conditions = TEXT(R"JSON({
		"id": "cond", "start_beat": "A1", "beats": [ { "id": "A1" } ],
		"steps": [
			{ "id": "gate", "beat": "A1", "kind": "hint_card", "trigger": { "type": "manual" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "heat", "beat": "A1", "kind": "hint_card", "focus": "coldest_operative",
			  "trigger": { "type": "condition", "requires": ["gate"] }, "condition": { "stat": "squad_cold_max", "at_least": 40 },
			  "complete": { "event": "ItemUsed.HEAT_PACK" } },
			{ "id": "nopack", "beat": "A1", "kind": "radio", "trigger": { "type": "condition", "requires": ["heat"] },
			  "condition": [ { "stat": "squad_heat_packs", "below": 1 } ], "lines": [ { "text_id": "WT_NOPACK" } ] },
			{ "id": "looted", "beat": "A1", "kind": "objective", "trigger": { "type": "event", "event": "ItemLooted.HeatPack", "requires": ["gate"] },
			  "complete": { "event": "Never" } }
		] })JSON");

	bool Parse(FAutomationTestBase& Test, const TCHAR* Json, FTutorialStepsData& OutData)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Json, OutData, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("parses (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			if (!Warning.Contains(TEXT("no text_id"))) // the samples have bare cards on purpose
			{
				Test.AddError(TEXT("unexpected warning: ") + Warning);
			}
		}
		return bOk;
	}

	/** Ticks in 0.25 s steps; plays out every speech step at once and completes feed steps (what UTutorialSubsystem does). */
	void Pump(FTutorialStateMachine& Machine, float Seconds)
	{
		for (float Time = 0.f; Time < Seconds - KINDA_SMALL_NUMBER; Time += 0.25f)
		{
			Machine.Tick(0.25f);
			for (int32 Guard = 0; Guard < 8; ++Guard)
			{
				bool bAny = false;
				for (const FTutorialStep& Step : Machine.GetData().Steps)
				{
					const bool bPlayable = Step.Kind == ETutorialStepKind::Feed
						|| ((Step.Kind == ETutorialStepKind::Radio || Step.Kind == ETutorialStepKind::WalkTalk) && !Step.Complete.HasCondition());
					if (bPlayable && Machine.GetState(Step.Id) == ETutorialStepState::Active)
					{
						Machine.CompleteStep(Step.Id);
						bAny = true;
					}
				}
				if (!bAny)
				{
					break;
				}
			}
			Machine.ConsumeTransitions();
		}
	}
}

TUTORIAL_LOCK_TEST(FTutorialLockRulesTest, "Lock.Rules")
bool FTutorialLockRulesTest::RunTest(const FString&)
{
	TArray<FString> Locks;
	TestTrue(TEXT("lock adds"), TutorialRules::ApplyLockChanges(Locks, { TEXT("Order.Sprint") }, {}));
	TestFalse(TEXT("same lock again (any case): no change"), TutorialRules::ApplyLockChanges(Locks, { TEXT("order.sprint") }, {}));
	TestTrue(TEXT("sprint locked"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::Sprint));
	TestFalse(TEXT("walking is free"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::Move));
	TestEqual(TEXT("a locked sprint degrades to a walk"), TutorialRules::ResolveLockedCommand(Locks, PlayerCommands::Sprint), PlayerCommands::Move);
	TestEqual(TEXT("a free command is itself"), TutorialRules::ResolveLockedCommand(Locks, PlayerCommands::SoloOn), PlayerCommands::SoloOn);
	TestTrue(TEXT("unlock (any case) removes"), TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("ORDER.SPRINT") }));
	TestFalse(TEXT("unlocked"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::Sprint));
	TutorialRules::ApplyLockChanges(Locks, { TEXT("Order.*") }, {});
	TestTrue(TEXT("wildcard lock covers sprint"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::Sprint));
	TestEqual(TEXT("sprint with walking locked too: refused"), TutorialRules::ResolveLockedCommand(Locks, PlayerCommands::Sprint), FName());
	TestEqual(TEXT("a locked command without a fallback: refused"), TutorialRules::ResolveLockedCommand(Locks, PlayerCommands::Move), FName());
	TutorialRules::ApplyLockChanges(Locks, { TEXT("Squad.Solo") }, {});
	TestTrue(TEXT("unlock * clears all"), TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("*") }) && Locks.IsEmpty());
	TestEqual(TEXT("locked event name"), TutorialRules::MakeLockedEventName(PlayerCommands::Sprint), FName(TEXT("Tutorial.Locked.Order.Sprint")));
	TestTrue(TEXT("Tutorial.Locked.* matches it"), TutorialRules::MatchesName(TEXT("Tutorial.Locked.*"), TutorialRules::MakeLockedEventName(PlayerCommands::Sprint)));

	// Conditions.
	const TArray<FTutorialCondition> Clauses = { { TEXT("squad_cold_max"), true, 40.f }, { TEXT("squad_heat_packs"), false, 1.f } };
	float Cold = 39.f;
	float Packs = 0.f;
	auto Stat = [&Cold, &Packs](FName Name) { return Name == FName(TEXT("squad_heat_packs")) ? Packs : Cold; };
	TestFalse(TEXT("39 % is below at_least 40"), TutorialRules::IsConditionMet(Clauses, Stat));
	Cold = 40.f;
	TestTrue(TEXT("40 % and no packs: both clauses hold"), TutorialRules::IsConditionMet(Clauses, Stat));
	Packs = 1.f;
	TestFalse(TEXT("one pack: 'below 1' fails (AND)"), TutorialRules::IsConditionMet(Clauses, Stat));
	TestTrue(TEXT("no clauses hold"), TutorialRules::IsConditionMet({}, Stat));
	TestTrue(TEXT("known stats"), TutorialRules::IsKnownStat(TEXT("squad_cold_max")) && TutorialRules::IsKnownStat(TEXT("squad_cold_min"))
		&& TutorialRules::IsKnownStat(TEXT("leader_cold")) && TutorialRules::IsKnownStat(TEXT("squad_heat_packs")));
	TestFalse(TEXT("unknown stat"), TutorialRules::IsKnownStat(TEXT("squad_mood")));

	// The gust: +20, at least the floor, clamped.
	TestEqual(TEXT("gust 10 % +20 floor 42"), ColdRules::ApplyColdShock(10.f, 20.f, 42.f), 42.f);
	TestEqual(TEXT("gust 30 % +20 floor 42"), ColdRules::ApplyColdShock(30.f, 20.f, 42.f), 50.f);
	TestEqual(TEXT("gust without floor"), ColdRules::ApplyColdShock(10.f, 20.f), 30.f);
	TestEqual(TEXT("gust clamps at 100"), ColdRules::ApplyColdShock(95.f, 20.f, 42.f), 100.f);
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialLockSchemaTest, "Lock.Schema")
bool FTutorialLockSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Conditions, Data))
	{
		return false;
	}
	const FTutorialStep* Heat = Data.FindStep(TEXT("heat"));
	TestTrue(TEXT("condition trigger + requires + focus"), Heat && Heat->Trigger.Type == ETutorialTriggerType::Condition
		&& Heat->Trigger.Requires == TArray<FName>{ TEXT("gate") } && Heat->Conditions.Num() == 1 && Heat->Conditions[0].bAtLeast
		&& Heat->Conditions[0].Value == 40.f && Heat->Focus == FName(TEXT("coldest_operative")));
	TestTrue(TEXT("condition as an array"), Data.FindStep(TEXT("nopack")) && !Data.FindStep(TEXT("nopack"))->Conditions[0].bAtLeast);
	FTutorialStepsData GustData;
	TutorialLockTestData::Parse(*this, TutorialLockTestData::Gust, GustData);
	const FTutorialStep* Gust = GustData.FindStep(TEXT("gust"));
	TestTrue(TEXT("single action object: cold_bump 20, floor 42"), Gust && Gust->Actions.Num() == 1 && Gust->Actions[0].Type == FName(TEXT("cold_bump"))
		&& Gust->Actions[0].Amount == 20.f && Gust->Actions[0].MinLevel == 42.f);
	FTutorialStepsData LockData;
	TutorialLockTestData::Parse(*this, TutorialLockTestData::Locks, LockData);
	TestTrue(TEXT("beat lock / unlock"), LockData.Beats[0].Lock == TArray<FString>{ TEXT("Order.Sprint") } && LockData.Beats[2].Unlock == TArray<FString>{ TEXT("Order.Sprint") });
	TestTrue(TEXT("step unlock"), LockData.FindStep(TEXT("warmed"))->Unlock == TArray<FString>{ TEXT("Order.Sprint") });
	TestTrue(TEXT("feed kind"), LockData.FindStep(TEXT("nosprint"))->Kind == ETutorialStepKind::Feed);

	// Errors: they would silently never work otherwise.
	TArray<FString> Warnings;
	FString Error;
	TestFalse(TEXT("unknown stat"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","condition":{"stat":"mood","below":1}}]})"), Data, Warnings, Error));
	TestFalse(TEXT("clause without at_least / below"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","condition":{"stat":"leader_cold"}}]})"), Data, Warnings, Error));
	TestFalse(TEXT("condition trigger without a condition"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","trigger":{"type":"condition"}}]})"), Data, Warnings, Error));
	TestFalse(TEXT("requires an unknown step"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","trigger":{"type":"beat_start","requires":["zz"]}}]})"), Data, Warnings, Error));
	TestFalse(TEXT("unknown action"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","action":{"type":"explode"}}]})"), Data, Warnings, Error));
	TestFalse(TEXT("unknown focus"), TutorialRules::ParseSteps(TEXT(R"({"steps":[{"id":"a","beat":"B","focus":"tallest"}]})"), Data, Warnings, Error));
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialLockMachineTest, "Lock.LockUnlock")
bool FTutorialLockMachineTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Locks, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	TestFalse(TEXT("nothing locked before the start beat"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.StartBeat(TEXT("A0"));
	TestTrue(TEXT("beat A0 locks sprint"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	TestFalse(TEXT("move stays free"), Machine.IsCommandLocked(PlayerCommands::Move));

	// The first locked try: the feed step shows at once although a card AND a speech hold their slots.
	Machine.TriggerStep(TEXT("card"));
	Machine.TriggerStep(TEXT("talk"));
	TestTrue(TEXT("card and speech up"), Machine.GetActiveCard() == FName(TEXT("card")) && Machine.GetActiveSpeech() == FName(TEXT("talk")));
	Machine.HandleEvent(TutorialRules::MakeLockedEventName(PlayerCommands::Sprint));
	TestEqual(TEXT("feed line shown at once (no slot)"), Machine.GetState(TEXT("nosprint")), ETutorialStepState::Active);
	Machine.CompleteStep(TEXT("nosprint"));
	Machine.HandleEvent(TutorialRules::MakeLockedEventName(PlayerCommands::Sprint));
	TestEqual(TEXT("only once"), Machine.GetState(TEXT("nosprint")), ETutorialStepState::Completed);
	Machine.SkipStep(TEXT("card"));
	Machine.CompleteStep(TEXT("talk"));

	Machine.StartBeat(TEXT("A1"));
	TestTrue(TEXT("still locked in A1"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.TriggerStep(TEXT("warmed"));
	TestEqual(TEXT("warmed line shown"), Machine.GetState(TEXT("warmed")), ETutorialStepState::Active);
	TestFalse(TEXT("unlocked when the line shows"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.CompleteStep(TEXT("warmed"));
	Machine.Tick(2.f);
	Machine.TriggerStep(TEXT("freeze_all"));
	Machine.Tick(2.f);
	TestTrue(TEXT("a step can lock a wildcard"), Machine.IsCommandLocked(PlayerCommands::Move) && Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.StartBeat(TEXT("A2"));
	TestTrue(TEXT("A2 unlocks only Order.Sprint, the wildcard stays"), Machine.IsCommandLocked(PlayerCommands::Sprint));

	// A step that finishes without ever showing still applies its locks.
	FTutorialStateMachine Silent;
	Silent.Load(Data);
	Silent.StartBeat(TEXT("A0"));
	Silent.TriggerStep(TEXT("talk"));
	Silent.StartBeat(TEXT("A1"));
	Silent.TriggerStep(TEXT("warmed")); // queued behind "talk"
	TestEqual(TEXT("warmed queued"), Silent.GetState(TEXT("warmed")), ETutorialStepState::Queued);
	Silent.SkipStep(TEXT("warmed"));
	TestFalse(TEXT("skipped before showing: its unlock still applied"), Silent.IsCommandLocked(PlayerCommands::Sprint));
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialLockSaveTest, "Lock.Saved")
bool FTutorialLockSaveTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Locks, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	const TSharedRef<FJsonObject> Saved = TutorialRules::SaveProgress(Machine.ExportProgress(), true);
	const TArray<TSharedPtr<FJsonValue>>* LockArray = nullptr;
	TestTrue(TEXT("save has \"locks\": [\"Order.Sprint\"]"), Saved->TryGetArrayField(TEXT("locks"), LockArray) && LockArray->Num() == 1
		&& (*LockArray)[0]->AsString() == TEXT("Order.Sprint"));
	FTutorialProgress Loaded;
	TestTrue(TEXT("loads"), TutorialRules::LoadProgress(*Saved, Loaded) && Loaded.bHasLocks);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestTrue(TEXT("restored: sprint locked"), Restored.IsCommandLocked(PlayerCommands::Sprint));

	// Unlocked in A1 -> saved -> still unlocked after a load (the save wins over the beat lock).
	Machine.StartBeat(TEXT("A1"));
	Machine.TriggerStep(TEXT("warmed"));
	Machine.CompleteStep(TEXT("warmed"));
	TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded);
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestFalse(TEXT("restored after warming: free"), Restored.IsCommandLocked(PlayerCommands::Sprint));

	// An older save without "locks": rebuilt from the beats up to the current one and the finished steps.
	Saved->RemoveField(TEXT("locks"));
	TutorialRules::LoadProgress(*Saved, Loaded);
	TestFalse(TEXT("old save: no locks key"), Loaded.bHasLocks);
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestTrue(TEXT("old save at A0: rebuilt lock"), Restored.IsCommandLocked(PlayerCommands::Sprint));
	FTutorialProgress OldA1;
	OldA1.CurrentBeat = TEXT("A1");
	OldA1.Finished.Add(TEXT("warmed"), ETutorialStepState::Completed);
	Restored.Load(Data);
	Restored.ImportProgress(OldA1);
	TestFalse(TEXT("old save at A1 with the warmed line done: free"), Restored.IsCommandLocked(PlayerCommands::Sprint));
	Restored.ResetProgress();
	TestFalse(TEXT("reset clears the locks"), Restored.IsCommandLocked(PlayerCommands::Sprint));
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialGustTimeoutTest, "Action.GustTimeout")
bool FTutorialGustTimeoutTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Gust, Data))
	{
		return false;
	}
	float Cold = 10.f;
	for (const bool bWarm : { true, false })
	{
		Cold = bWarm ? 10.f : 45.f;
		FTutorialStateMachine Machine;
		Machine.StatProvider = [&Cold](FName) { return Cold; };
		Machine.Load(Data);
		Machine.StartBeat(TEXT("A1"));
		Machine.TriggerStep(TEXT("solo"));
		Machine.HandleCommand(PlayerCommands::SoloOn);
		TestEqual(TEXT("gust waits after solo"), Machine.GetState(TEXT("gust")), ETutorialStepState::Waiting);
		Machine.Tick(10.f);
		Machine.Tick(9.5f);
		TestEqual(TEXT("not before 20 s"), Machine.GetState(TEXT("gust")), ETutorialStepState::Waiting);
		Machine.ConsumeTransitions();
		Machine.Tick(1.f);
		const TArray<FTutorialTransition> Transitions = Machine.ConsumeTransitions();
		if (bWarm)
		{
			TestEqual(TEXT("nobody at Chills after 20 s: the gust shows (the runtime runs its cold_bump)"), Machine.GetState(TEXT("gust")), ETutorialStepState::Active);
		}
		else
		{
			TestEqual(TEXT("already cold: the gust is skipped"), Machine.GetState(TEXT("gust")), ETutorialStepState::Skipped);
			TestTrue(TEXT("silently"), Transitions.ContainsByPredicate([](const FTutorialTransition& T)
				{ return T.StepId == FName(TEXT("gust")) && T.To == ETutorialStepState::Skipped && T.bSilent; }));
			TestTrue(TEXT("its follower still fires"), TutorialRules::IsRunning(Machine.GetState(TEXT("after_gust"))));
		}
	}
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialConditionTriggerTest, "Condition.Trigger")
bool FTutorialConditionTriggerTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Conditions, Data))
	{
		return false;
	}
	float Cold = 50.f;
	float Packs = 1.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Cold, &Packs](FName Stat) { return Stat == FName(TEXT("squad_heat_packs")) ? Packs : Cold; };
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A1"));
	Machine.Tick(1.f);
	TestEqual(TEXT("cold but not armed (requires gate): idle"), Machine.GetState(TEXT("heat")), ETutorialStepState::Idle);
	Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
	TestEqual(TEXT("event before its requirement is ignored"), Machine.GetState(TEXT("looted")), ETutorialStepState::Idle);
	Machine.TriggerStep(TEXT("gate"));
	Machine.Tick(0.1f);
	TestEqual(TEXT("armed + cold: fires (queued behind the gate card)"), Machine.GetState(TEXT("heat")), ETutorialStepState::Queued);
	TestEqual(TEXT("packs carried: no remedial line"), Machine.GetState(TEXT("nopack")), ETutorialStepState::Idle);
	Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
	TestTrue(TEXT("armed event trigger counts now"), TutorialRules::IsRunning(Machine.GetState(TEXT("looted"))));
	Packs = 0.f;
	Machine.Tick(0.1f);
	TestEqual(TEXT("no packs: the remedial radio line plays"), Machine.GetState(TEXT("nopack")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::SoloOn);
	Machine.Tick(2.f);
	TestEqual(TEXT("heat card shown after the gate"), Machine.GetState(TEXT("heat")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"));
	TestEqual(TEXT("heat pack used completes it"), Machine.GetState(TEXT("heat")), ETutorialStepState::Completed);
	Machine.Tick(1.f);
	TestEqual(TEXT("once: no re-fire while still cold"), Machine.GetState(TEXT("heat")), ETutorialStepState::Completed);

	// Below the threshold nothing fires.
	FTutorialStateMachine Warm;
	Cold = 39.f;
	Warm.StatProvider = [&Cold](FName) { return Cold; };
	Warm.Load(Data);
	Warm.StartBeat(TEXT("A1"));
	Warm.TriggerStep(TEXT("gate"));
	Warm.Tick(5.f);
	TestEqual(TEXT("39 %: idle"), Warm.GetState(TEXT("heat")), ETutorialStepState::Idle);
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialDemoFlowTest, "Schema.DemoFileA0A1Flow")
bool FTutorialDemoFlowTest::RunTest(const FString&)
{
	using namespace TutorialLockTestData;
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		return false;
	}
	// Static order (user flow 2026-10-13, rev 5): HQ radio only at the start; sprint + inventory locked until after the heat pack lesson.
	TestTrue(TEXT("A0 locks sprint, the inventory and the items"), Data.Beats[0].Id == FName(TEXT("A0")) && Data.Beats[0].Lock.Contains(TEXT("Order.Sprint"))
		&& Data.Beats[0].Lock.Contains(TEXT("Inventory.*")) && Data.Beats[0].Lock.Contains(TEXT("Item.*")));
	TestTrue(TEXT("A1 (beacon 1) unlocks nothing any more (rev 5: sprint waits for the heat pack lesson)"), Data.Beats[1].Id == FName(TEXT("A1"))
		&& Data.Beats[1].Unlock.IsEmpty() && Data.Beats[1].Lock.IsEmpty());
	TestTrue(TEXT("A2 unlocks sprint + inventory (safety)"), Data.Beats[2].Id == FName(TEXT("A2")) && Data.Beats[2].Unlock.Contains(TEXT("Order.Sprint"))
		&& Data.Beats[2].Unlock.Contains(TEXT("Inventory.*")));
	const FTutorialStep* Radio = Data.FindStep(TEXT("A0_radio_hq"));
	TestTrue(TEXT("A0_radio_hq: HQ radio at the beat start, locks Order.Move while it plays"), Radio && Radio->Kind == ETutorialStepKind::Radio
		&& Radio->Trigger.Type == ETutorialTriggerType::BeatStart && Radio->Lock.Contains(TEXT("Order.Move")) && Radio->Lines.Num() >= 4
		&& Radio->Lines[0].Speaker == TEXT("HQ"));
	const FTutorialStep* Pan = Data.FindStep(TEXT("A0_cam_pan"));
	TestTrue(TEXT("the camera lesson starts only after the radio and unlocks Order.Move"), Pan && Pan->Trigger.Type == ETutorialTriggerType::AfterStep
		&& Pan->Trigger.Step == FName(TEXT("A0_radio_hq")) && Pan->Unlock.Contains(TEXT("Order.Move")));
	const FTutorialStep* WaitHq = Data.FindStep(TEXT("A0_wait_hq"));
	TestTrue(TEXT("A0_wait_hq: a feed line on a click during the radio"), WaitHq && WaitHq->Kind == ETutorialStepKind::Feed && WaitHq->Trigger.Event == TEXT("Tutorial.Locked.Order.Move"));
	const FTutorialStep* NoSprint = Data.FindStep(TEXT("A0_nosprint"));
	TestTrue(TEXT("A0_nosprint: a feed line on the first locked sprint, once"), NoSprint && NoSprint->Kind == ETutorialStepKind::Feed && NoSprint->bOnce
		&& NoSprint->TextId == FName(TEXT("TUT_A0_NOSPRINT")) && NoSprint->Trigger.Event == TEXT("Tutorial.Locked.Order.Sprint"));
	TestTrue(TEXT("A0_noinventory: a feed line on the first locked inventory"), Data.FindStep(TEXT("A0_noinventory"))
		&& Data.FindStep(TEXT("A0_noinventory"))->Trigger.Event == TEXT("Tutorial.Locked.Inventory.Open"));
	for (const TCHAR* Gone : { TEXT("A0_walk_talk"), TEXT("A1_wt_scout"), TEXT("A1_guard"), TEXT("A1_stance"), TEXT("A1_vault"), TEXT("A1_wt_wind"), TEXT("A6_cover") })
	{
		TestNull(FString::Printf(TEXT("%s is gone in rev 5"), Gone), Data.FindStep(Gone));
	}
	const FTutorialStep* Select = Data.FindStep(TEXT("A1_select"));
	TestTrue(TEXT("A1_select at the crack (VOL_A1_BEACON2 kept)"), Select && Select->Trigger.Type == ETutorialTriggerType::Volume && Select->Trigger.Volume == FName(TEXT("VOL_A1_BEACON2")));
	const FTutorialStep* Solo = Data.FindStep(TEXT("A1_solo"));
	TestTrue(TEXT("A1_solo (id kept for the marker): done when all three crossed the bridge"), Solo && Solo->Trigger.Type == ETutorialTriggerType::AfterStep
		&& Solo->Trigger.Step == FName(TEXT("A1_select")) && Solo->Complete.Event == TEXT("BridgeCrossed") && Solo->Complete.Count == 3 && Solo->Complete.bDistinctSubjects
		&& !TutorialRules::CanPrecredit(*Solo));
	const FTutorialStep* Regroup = Data.FindStep(TEXT("A1_regroup"));
	TestTrue(TEXT("regroup after the bridge, never precredited"), Regroup && Regroup->Trigger.Step == FName(TEXT("A1_solo")) && !Regroup->bPrecredit);
	const FTutorialStep* Box = Data.FindStep(TEXT("A1_box"));
	TestTrue(TEXT("box select after the regroup (orchestrator default)"), Box && Box->Trigger.Type == ETutorialTriggerType::AfterStep && Box->Trigger.Step == FName(TEXT("A1_regroup")));
	const FTutorialStep* Banter = Data.FindStep(TEXT("A1_wt_banter"));
	TestTrue(TEXT("the squad banter plays after the bridge (after the box card)"), Banter && Banter->Kind == ETutorialStepKind::WalkTalk && Banter->Trigger.Step == FName(TEXT("A1_box"))
		&& Banter->Lines.Num() == 4);
	const FTutorialStep* HeatPack = Data.FindStep(TEXT("A1_heatpack"));
	TestTrue(TEXT("heat pack card focuses the coldest and UNLOCKS the inventory / items"), HeatPack && HeatPack->Focus == FName(TEXT("coldest_operative"))
		&& HeatPack->Unlock.Contains(TEXT("Inventory.*")) && HeatPack->Unlock.Contains(TEXT("Item.*")) && HeatPack->Trigger.Requires.Contains(FName(TEXT("A1_solo"))));
	const FTutorialStep* SprintCard = Data.FindStep(TEXT("A1_sprint"));
	TestTrue(TEXT("A1_sprint: 'running warms you up' after the heat pack lesson UNLOCKS sprint"), SprintCard && SprintCard->Trigger.Type == ETutorialTriggerType::AfterStep
		&& SprintCard->Trigger.Step == FName(TEXT("A1_heatpack_mate")) && SprintCard->Unlock.Contains(TEXT("Order.Sprint")) && !SprintCard->bPrecredit
		&& SprintCard->Complete.Commands.Contains(PlayerCommands::Sprint.ToString()));
	const FTutorialStep* Resupply = Data.FindStep(TEXT("A1_wt_resupply"));
	TestTrue(TEXT("A1_wt_resupply after the sprint card"), Resupply && Resupply->Trigger.Type == ETutorialTriggerType::AfterStep && Resupply->Trigger.Step == FName(TEXT("A1_sprint"))
		&& Resupply->Lines.Num() == 1 && Resupply->Lines[0].TextId == FName(TEXT("WT_A1_RESUPPLY")));
	const FTutorialStep* HutObjective = Data.FindStep(TEXT("A1_obj_hut"));
	TestTrue(TEXT("A1_obj_hut keeps its id, shows after the sprint card, done on ItemLooted.HeatPack"), HutObjective && HutObjective->Kind == ETutorialStepKind::Objective
		&& HutObjective->Trigger.Step == FName(TEXT("A1_sprint")) && HutObjective->Complete.Event == TEXT("ItemLooted.HeatPack"));
	const FTutorialStep* GustStep = Data.FindStep(TEXT("A1_gust"));
	TestTrue(TEXT("gust: 20 s after the crossing, only below 40 %, cold_bump 20"), GustStep && GustStep->Trigger.Type == ETutorialTriggerType::AfterStep
		&& GustStep->Trigger.Step == FName(TEXT("A1_solo")) && GustStep->Trigger.DelaySeconds == 20.f && GustStep->Conditions.Num() == 1
		&& !GustStep->Conditions[0].bAtLeast && GustStep->Conditions[0].Value == 40.f && GustStep->Actions.Num() == 1 && GustStep->Actions[0].Amount == 20.f);
	if (!GustStep || GustStep->Actions.IsEmpty())
	{
		return false;
	}
	TestTrue(TEXT("stances moved to the approach (A3, VOL_A3_WIND)"), Data.FindStep(TEXT("A3_stance")) && Data.FindStep(TEXT("A3_stance"))->Trigger.Volume == FName(TEXT("VOL_A3_WIND")));
	TestTrue(TEXT("A2 heat pack card is remedial only"), Data.FindStep(TEXT("A2_heatpack")) && Data.FindStep(TEXT("A2_heatpack"))->Trigger.Type == ETutorialTriggerType::Condition);
	for (const FTutorialStep& Step : Data.Steps)
	{
		TestFalse(FString::Printf(TEXT("%s: no ColdTier event trigger (it would jump beats)"), *Step.Id.ToString()),
			Step.Trigger.Type == ETutorialTriggerType::Event && Step.Trigger.Event.StartsWith(TEXT("ColdTier")));
		// Every text of the implemented beats has at least a draft (no [EN missing]).
		TArray<FName> Ids;
		if (!Step.TextId.IsNone())
		{
			Ids.Add(Step.TextId);
		}
		for (const FTutorialLine& Line : Step.Lines)
		{
			Ids.Add(Line.TextId);
		}
		for (const FName Id : Ids)
		{
			const FString Text = FNarrativeManifest::Get().GetTutorialText(Id.ToString()).Text;
			const FString Title = FNarrativeManifest::Get().GetTutorialText(Id.ToString()).Title;
			TestFalse(FString::Printf(TEXT("%s text %s exists"), *Step.Id.ToString(), *Id.ToString()),
				(Text.IsEmpty() || Text.StartsWith(TEXT("[EN missing"))) && Title.IsEmpty());
		}
	}

	// Dynamic run through A0 -> A1 with a fake squad (cold %, heat packs carried: one starter pack per operative).
	float Cold = 5.f;
	float Packs = 3.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Cold, &Packs](FName Stat) { return Stat == FName(TEXT("squad_heat_packs")) ? Packs : Cold; };
	Machine.Load(Data);
	Machine.StartBeat(Data.StartBeat);
	TestTrue(TEXT("A0: sprint and the inventory locked from the start"), Machine.IsCommandLocked(PlayerCommands::Sprint) && Machine.IsCommandLocked(PlayerCommands::InventoryOpen)
		&& Machine.IsCommandLocked(PlayerCommands::UseHeatPack));
	Machine.Tick(1.1f); // the radio's 1 s delay (no Pump: it would play the radio out at once)
	TestEqual(TEXT("A0: the HQ radio plays first, no card"), Machine.GetActiveSpeech(), FName(TEXT("A0_radio_hq")));
	TestEqual(TEXT("A0: no camera card during the radio"), Machine.GetActiveCard(), FName());
	TestTrue(TEXT("A0: moves locked while HQ talks"), Machine.IsCommandLocked(PlayerCommands::Move));
	TestEqual(TEXT("A0: a locked move is refused (no fallback)"), TutorialRules::ResolveLockedCommand(Machine.GetLocks(), PlayerCommands::Move), FName());
	Machine.HandleEvent(TutorialRules::MakeLockedEventName(PlayerCommands::Move));
	TestEqual(TEXT("A0: TUT_A0_WAIT_HQ shows at once"), Machine.GetState(TEXT("A0_wait_hq")), ETutorialStepState::Active);
	// The player pans with WASD and MMB-drags while HQ talks: those camera stages are precredited later.
	Machine.HandleCommand(PlayerCommands::CameraPanKeys);
	Machine.HandleCommandHold(PlayerCommands::CameraPanKeys, 1.5f);
	Machine.HandleCommand(PlayerCommands::CameraPanDrag);
	Machine.HandleCommandHold(PlayerCommands::CameraPanDrag, 0.8f);
	Machine.CompleteStep(TEXT("A0_radio_hq"));
	Pump(Machine, 1.5f);
	TestFalse(TEXT("after the radio: moves unlocked by the first camera card"), Machine.IsCommandLocked(PlayerCommands::Move));
	TestEqual(TEXT("pan precredited (done during the radio)"), Machine.GetState(TEXT("A0_cam_pan")), ETutorialStepState::Completed);
	TestEqual(TEXT("MMB drag precredited"), Machine.GetState(TEXT("A0_cam_pan_drag")), ETutorialStepState::Completed);
	TestEqual(TEXT("Q/E is the first card shown"), Machine.GetActiveCard(), FName(TEXT("A0_cam_rotate_keys")));
	for (const TCHAR* Camera : { TEXT("A0_cam_rotate_keys"), TEXT("A0_cam_rotate_mouse"), TEXT("A0_cam_zoom") })
	{
		Pump(Machine, 2.f);
		TestTrue(FString(Camera) + TEXT(" skipped"), Machine.SkipStep(Camera));
	}
	Machine.HandleEvent(TutorialRules::MakeLockedEventName(PlayerCommands::Sprint));
	TestEqual(TEXT("A0: TUT_A0_NOSPRINT shows at once"), Machine.GetState(TEXT("A0_nosprint")), ETutorialStepState::Active);
	TestTrue(TEXT("A0: sprint still locked on the whole stretch to beacon 1"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.HandleVolume(TEXT("VOL_A0_BEACON1"), NAME_None, TEXT("A1"));
	TestEqual(TEXT("beacon 1 starts A1"), Machine.GetCurrentBeat(), FName(TEXT("A1")));
	TestTrue(TEXT("A1: sprint STILL locked (rev 5)"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	TestEqual(TEXT("A1: a double click still walks"), TutorialRules::ResolveLockedCommand(Machine.GetLocks(), PlayerCommands::Sprint), PlayerCommands::Move);
	Pump(Machine, 2.f);
	TestEqual(TEXT("no card before the crack"), Machine.GetActiveCard(), FName());
	Machine.HandleVolume(TEXT("VOL_A1_BEACON2"), NAME_None, NAME_None);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the crack: select first"), Machine.GetActiveCard(), FName(TEXT("A1_select")));
	Machine.HandleCommand(PlayerCommands::SelectMember);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then the one-at-a-time bridge card"), Machine.GetActiveCard(), FName(TEXT("A1_solo")));
	Machine.HandleEvent(TEXT("BridgeRefused"), TEXT("Op2"));
	TestEqual(TEXT("the bridge creak line plays once"), Machine.GetState(TEXT("A1_bridge_refused")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op1"));
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op1"));
	TestEqual(TEXT("the same operative twice counts once"), Machine.GetProgress(TEXT("A1_solo")), 1);
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op2"));
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op3"));
	TestEqual(TEXT("three distinct crossings complete it"), Machine.GetState(TEXT("A1_solo")), ETutorialStepState::Completed);
	Pump(Machine, 4.f);
	TestEqual(TEXT("regroup card 2 s after the crossing"), Machine.GetActiveCard(), FName(TEXT("A1_regroup")));
	Machine.HandleCommand(PlayerCommands::Regroup);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then box select"), Machine.GetActiveCard(), FName(TEXT("A1_box")));
	Machine.HandleCommand(PlayerCommands::SelectBox);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then the squad banter"), Machine.GetState(TEXT("A1_wt_banter")), ETutorialStepState::Completed);
	TestEqual(TEXT("no heat-pack card while nobody is cold"), Machine.GetState(TEXT("A1_heatpack")), ETutorialStepState::Idle);
	TestEqual(TEXT("the gust has not come yet (10 s after the crossing)"), Machine.GetState(TEXT("A1_gust")), ETutorialStepState::Waiting);
	Pump(Machine, 13.f);
	TestEqual(TEXT("20 s after the crossing, nobody at Chills: the gust played"), Machine.GetState(TEXT("A1_gust")), ETutorialStepState::Completed);
	Cold = ColdRules::ApplyColdShock(Cold, GustStep->Actions[0].Amount, GustStep->Actions[0].MinLevel); // what the runtime does on show
	TestTrue(TEXT("the gust leaves everyone at Chills"), Cold >= 40.f);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then the heat pack card"), Machine.GetActiveCard(), FName(TEXT("A1_heatpack")));
	TestFalse(TEXT("the heat pack card UNLOCKS the inventory and the items"), Machine.IsCommandLocked(PlayerCommands::InventoryOpen) || Machine.IsCommandLocked(PlayerCommands::UseHeatPack));
	TestTrue(TEXT("sprint still locked until the lesson is done"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	TestEqual(TEXT("the sprint card waits for the lesson"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::Idle);
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"));
	Cold = 7.f;
	Packs = 2.f;
	TestEqual(TEXT("using a heat pack completes it"), Machine.GetState(TEXT("A1_heatpack")), ETutorialStepState::Completed);
	Pump(Machine, 1.5f);
	TestEqual(TEXT("nobody else cold: the mate card is skipped"), Machine.GetState(TEXT("A1_heatpack_mate")), ETutorialStepState::Skipped);
	Pump(Machine, 1.5f);
	TestEqual(TEXT("'running warms you up': the sprint card"), Machine.GetActiveCard(), FName(TEXT("A1_sprint")));
	TestFalse(TEXT("sprint UNLOCKED by the sprint card"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	TestEqual(TEXT("a double click sprints now"), TutorialRules::ResolveLockedCommand(Machine.GetLocks(), PlayerCommands::Sprint), PlayerCommands::Sprint);
	TestEqual(TEXT("the hut objective waits for the sprint card"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Idle);
	Machine.HandleCommand(PlayerCommands::Sprint);
	TestEqual(TEXT("a sprint completes the card"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("WT_A1_RESUPPLY played"), Machine.GetState(TEXT("A1_wt_resupply")), ETutorialStepState::Completed);
	TestEqual(TEXT("and the hut objective is up"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Active);
	TestEqual(TEXT("hut loot cards not before the hut"), Machine.GetState(TEXT("A1_loot_open")), ETutorialStepState::Idle);
	Machine.HandleVolume(TEXT("VOL_A1_HUT"), NAME_None, NAME_None);
	Pump(Machine, 2.f);
	TestEqual(TEXT("hut: open the crate"), Machine.GetActiveCard(), FName(TEXT("A1_loot_open")));
	Machine.HandleCommand(PlayerCommands::LootCrate);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then take the heat packs"), Machine.GetActiveCard(), FName(TEXT("A1_loot_take")));
	Packs = 5.f;
	Machine.HandleEvent(TEXT("ItemLooted"));
	Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
	TestEqual(TEXT("taking them completes the card"), Machine.GetState(TEXT("A1_loot_take")), ETutorialStepState::Completed);
	TestEqual(TEXT("and the hut objective"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the closing line played"), Machine.GetState(TEXT("A1_wt_stocked")), ETutorialStepState::Completed);
	Machine.HandleVolume(TEXT("VOL_A1_BEACON3"), NAME_None, NAME_None);
	Pump(Machine, 2.f);
	TestEqual(TEXT("beacon 3: formation lines, no guard card any more (guard moves to the horde beat)"), Machine.GetState(TEXT("A1_wt_regroup")), ETutorialStepState::Completed);
	TestEqual(TEXT("no card at beacon 3"), Machine.GetActiveCard(), FName());
	// A3 approach: the stance card is precredited when the player already used two stance keys.
	Machine.HandleCommand(PlayerCommands::StanceCrouch);
	Machine.HandleCommand(PlayerCommands::StanceStand);
	Machine.HandleVolume(TEXT("VOL_A3_APPROACH"), NAME_None, TEXT("A3"));
	Machine.HandleVolume(TEXT("VOL_A3_WIND"), NAME_None, NAME_None);
	TestEqual(TEXT("A3 stance card precredited from the history"), Machine.GetState(TEXT("A3_stance")), ETutorialStepState::Completed);
	Machine.HandleVolume(TEXT("VOL_A3_FENCE"), NAME_None, NAME_None);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the low fence card shows (event-completed: never precredited)"), Machine.GetActiveCard(), FName(TEXT("A3_fence")));
	Machine.HandleEvent(TEXT("CrawlGapRefused"), TEXT("Op1"));
	TestEqual(TEXT("the 'go prone' feed line"), Machine.GetState(TEXT("A3_fence_refused")), ETutorialStepState::Active);
	for (const TCHAR* Op : { TEXT("Op1"), TEXT("Op2"), TEXT("Op3") })
	{
		Machine.HandleEvent(TEXT("CrawlGapPassed"), Op);
	}
	TestEqual(TEXT("three crawl-throughs complete the fence card"), Machine.GetState(TEXT("A3_fence")), ETutorialStepState::Completed);
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialDemoLoadoutTest, "Demo.HeatPackLoadout")
bool FTutorialDemoLoadoutTest::RunTest(const FString&)
{
	TestEqual(TEXT("role kit kept without a level value"), LoadoutRules::StartingHeatPacks(2, -1, 3), 2);
	TestEqual(TEXT("level heat_packs_count 0 replaces the kit"), LoadoutRules::StartingHeatPacks(2, 0, 3), 0);
	TestEqual(TEXT("level heat_packs_count 1 replaces the kit: one each"), LoadoutRules::StartingHeatPacks(2, 1, 3), 1);
	TestEqual(TEXT("level value clamped to the stack"), LoadoutRules::StartingHeatPacks(1, 5, 3), 3);
	TestEqual(TEXT("kit clamped to the stack"), LoadoutRules::StartingHeatPacks(5, -1, 3), 3);
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("parse"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"x","squad_loadout":{"heat_packs_count":1}})"), TEXT("x"), Level, Error));
	TestEqual(TEXT("heat_packs_count read"), Level.SquadLoadout.HeatPacksCount, 1);
	TestTrue(TEXT("parse without"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"x"})"), TEXT("x"), Level, Error));
	TestEqual(TEXT("absent: -1 (role kit)"), Level.SquadLoadout.HeatPacksCount, -1);
	TestTrue(FString::Printf(TEXT("demo level loads (%s)"), *Error), LevelJsonRules::LoadLevel(TEXT("L_Demo01_A_FrozenLake.json"), Level, Error));
	TestEqual(TEXT("Demo 01: every operative starts with 1 heat pack (the A1 heat pack lesson); the hut crate holds more"), Level.SquadLoadout.HeatPacksCount, 1);
	for (const int32 RoleKit : { 0, 1, 2, 3 })
	{
		TestEqual(FString::Printf(TEXT("role kit %d: the demo value wins, 1 pack each"), RoleKit), LoadoutRules::StartingHeatPacks(RoleKit, Level.SquadLoadout.HeatPacksCount, 3), 1);
	}
	return true;
}

#undef TUTORIAL_LOCK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
