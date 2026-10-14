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
	TestFalse(TEXT("A2 frees Order.Sprint out of the wildcard (exception entry, 2026-10-13)"), Machine.IsCommandLocked(PlayerCommands::Sprint));
	TestTrue(TEXT("the rest of the wildcard stays locked"), Machine.IsCommandLocked(PlayerCommands::Move));

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

TUTORIAL_LOCK_TEST(FTutorialConditionCompletionTest, "Condition.Completion")
bool FTutorialConditionCompletionTest::RunTest(const FString&)
{
	// Rev 5d (designer request 2026-10-14): "complete": { "condition": ... } - the heat pack share task after the hut loot.
	const TCHAR* Json = TEXT(R"JSON({
		"id": "share", "start_beat": "A1", "beats": [ { "id": "A1" } ],
		"steps": [
			{ "id": "loot", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "manual" }, "complete": { "event": "ItemLooted.HeatPack" } },
			{ "id": "share", "beat": "A1", "kind": "objective", "text_id": "T", "trigger": { "type": "after_step", "step": "loot" },
			  "complete": { "condition": [ { "stat": "squad_min_heat_packs", "at_least": 1 }, { "stat": "heat_packs_given", "at_least": 1 } ] } },
			{ "id": "share_card", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "after_step", "step": "loot", "delay_s": 1 },
			  "complete": { "condition": [ { "stat": "squad_min_heat_packs", "at_least": 1 }, { "stat": "heat_packs_given", "at_least": 1 } ], "timeout_s": 60 } },
			{ "id": "regroup", "beat": "A1", "kind": "hint_card", "text_id": "T", "trigger": { "type": "after_step", "step": "share_card" },
			  "complete": { "commands": ["Squad.Regroup"] } }
		] })JSON");
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, Json, Data))
	{
		return false;
	}
	TestTrue(TEXT("parsed: two completion clauses, counts as a completion"), Data.FindStep(TEXT("share"))->Complete.Conditions.Num() == 2
		&& Data.FindStep(TEXT("share"))->Complete.HasCondition());
	float MinPacks = 0.f;
	float Given = 0.f;
	auto Provider = [&MinPacks, &Given](FName Stat) { return Stat == FName(TEXT("heat_packs_given")) ? Given : MinPacks; };
	{
		FTutorialStateMachine Machine;
		Machine.StatProvider = Provider;
		Machine.Load(Data);
		Machine.StartBeat(TEXT("A1"));
		Machine.TriggerStep(TEXT("loot"));
		Machine.Tick(0.1f);
		Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
		Machine.Tick(2.f);
		TestEqual(TEXT("one mate has none: the objective is up"), Machine.GetState(TEXT("share")), ETutorialStepState::Active);
		TestEqual(TEXT("... and the card"), Machine.GetState(TEXT("share_card")), ETutorialStepState::Active);
		MinPacks = 1.f;
		Machine.Tick(0.1f);
		TestEqual(TEXT("everyone has one, but nothing was handed over: still up"), Machine.GetState(TEXT("share")), ETutorialStepState::Active);
		Given = 1.f;
		Machine.Tick(0.1f);
		TestEqual(TEXT("handed over + everyone carries one: objective done"), Machine.GetState(TEXT("share")), ETutorialStepState::Completed);
		TestEqual(TEXT("card done"), Machine.GetState(TEXT("share_card")), ETutorialStepState::Completed);
		Machine.Tick(2.f);
		TestTrue(TEXT("its follower runs"), TutorialRules::IsRunning(Machine.GetState(TEXT("regroup"))));
	}
	{
		// The looter already handed them out before the cards were due: precredited, never shown.
		MinPacks = 1.f;
		Given = 2.f;
		FTutorialStateMachine Machine;
		Machine.StatProvider = Provider;
		Machine.Load(Data);
		Machine.StartBeat(TEXT("A1"));
		Machine.TriggerStep(TEXT("loot"));
		Machine.Tick(0.1f);
		Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
		TArray<FTutorialTransition> Transitions = Machine.ConsumeTransitions();
		Machine.Tick(2.f);
		Transitions.Append(Machine.ConsumeTransitions());
		TestEqual(TEXT("precredit: objective completed"), Machine.GetState(TEXT("share")), ETutorialStepState::Completed);
		TestEqual(TEXT("precredit: card completed"), Machine.GetState(TEXT("share_card")), ETutorialStepState::Completed);
		TestTrue(TEXT("... marked precredited, never shown"), Transitions.ContainsByPredicate([](const FTutorialTransition& T)
			{ return T.StepId == FName(TEXT("share_card")) && T.To == ETutorialStepState::Completed && T.bPrecredited && T.bSilent; })
			&& !Transitions.ContainsByPredicate([](const FTutorialTransition& T) { return T.StepId == FName(TEXT("share_card")) && T.To == ETutorialStepState::Active; }));
	}
	{
		// Unknown stat in a completion condition: a parse error.
		FTutorialStepsData Bad;
		TArray<FString> Warnings;
		FString Error;
		TestFalse(TEXT("unknown completion stat is an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "id": "x", "steps": [ { "id": "s", "kind": "objective",
			"complete": { "condition": { "stat": "squad_mood", "at_least": 1 } } } ] })JSON"), Bad, Warnings, Error));
	}
	TestTrue(TEXT("new stats known"), TutorialRules::IsKnownStat(TEXT("squad_min_heat_packs")) && TutorialRules::IsKnownStat(TEXT("heat_packs_given")));
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
	// Static order (user flow 2026-10-13, rev 5b): the HQ radio is a talk-on-the-move with the camera lesson alongside; move and
	// sprint are never locked; every family not taught yet is locked at A0 and unlocked by its lesson (progressive unlock).
	TestTrue(TEXT("A0 locks the untaught families, never move / sprint / camera / selection"), Data.Beats[0].Id == FName(TEXT("A0"))
		&& Data.Beats[0].Lock.Contains(TEXT("Stance.*")) && Data.Beats[0].Lock.Contains(TEXT("Time.*")) && Data.Beats[0].Lock.Contains(TEXT("Inventory.*"))
		&& !Data.Beats[0].Lock.Contains(TEXT("Order.Sprint")) && !Data.Beats[0].Lock.Contains(TEXT("Order.*")) && !Data.Beats[0].Lock.Contains(TEXT("Camera.*")));
	TestTrue(TEXT("A1 (beacon 1) changes no locks"), Data.Beats[1].Id == FName(TEXT("A1")) && Data.Beats[1].Unlock.IsEmpty() && Data.Beats[1].Lock.IsEmpty());
	TestTrue(TEXT("A2 repeats the A1 unlocks (safety), no sprint entry any more"), Data.Beats[2].Id == FName(TEXT("A2")) && Data.Beats[2].Unlock.Contains(TEXT("Inventory.*"))
		&& Data.Beats[2].Unlock.Contains(TEXT("Squad.Solo")) && !Data.Beats[2].Unlock.Contains(TEXT("Order.Sprint")));
	for (const FTutorialBeat& Beat : Data.Beats)
	{
		TestFalse(FString::Printf(TEXT("beat %s: sprint never locked (2026-10-13)"), *Beat.Id.ToString()), Beat.Lock.Contains(TEXT("Order.Sprint")));
	}
	const FTutorialStep* Radio = Data.FindStep(TEXT("A0_radio_hq"));
	TestTrue(TEXT("A0_radio_hq: HQ radio at the beat start, locks nothing (talk on the move)"), Radio && Radio->Kind == ETutorialStepKind::Radio
		&& Radio->Trigger.Type == ETutorialTriggerType::BeatStart && Radio->Lock.IsEmpty() && Radio->Lines.Num() >= 4 && Radio->Lines[0].Speaker == TEXT("HQ"));
	const FTutorialStep* Pan = Data.FindStep(TEXT("A0_cam_pan"));
	TestTrue(TEXT("the camera lesson starts WITH the radio (beat start + 2 s), independent of it"), Pan && Pan->Trigger.Type == ETutorialTriggerType::BeatStart
		&& Pan->Trigger.DelaySeconds > Radio->Trigger.DelaySeconds && Pan->Unlock.IsEmpty());
	const FTutorialStep* Walk = Data.FindStep(TEXT("A0_move"));
	TestTrue(TEXT("rev 5c: 'click to walk' right after the camera chain (no longer waits for the radio), done on a move order"), Walk
		&& Walk->Trigger.Type == ETutorialTriggerType::AfterStep && Walk->Trigger.AfterSteps == TArray<FName>{ FName(TEXT("A0_cam_zoom")) }
		&& Walk->Complete.Commands == TArray<FString>{ PlayerCommands::Move.ToString() } && Walk->Complete.Volume.IsNone());
	const FTutorialStep* RunCard = Data.FindStep(TEXT("A0_sprint"));
	TestTrue(TEXT("rev 5c: then 'double-click to run', done on a sprint order"), RunCard && RunCard->Trigger.Step == FName(TEXT("A0_move"))
		&& RunCard->Complete.Commands == TArray<FString>{ PlayerCommands::Sprint.ToString() } && RunCard->Lock.IsEmpty());
	const FTutorialStep* Objective = Data.FindStep(TEXT("A0_obj_beacon"));
	TestTrue(TEXT("the beacon objective shows with the radio"), Objective && Objective->Trigger.Type == ETutorialTriggerType::BeatStart);
	TestTrue(TEXT("A0_noinventory: a feed line on the first locked inventory"), Data.FindStep(TEXT("A0_noinventory"))
		&& Data.FindStep(TEXT("A0_noinventory"))->Trigger.Event == TEXT("Tutorial.Locked.Inventory.Open"));
	for (const TCHAR* Gone : { TEXT("A0_walk_talk"), TEXT("A1_wt_scout"), TEXT("A1_guard"), TEXT("A1_stance"), TEXT("A1_vault"), TEXT("A1_wt_wind"), TEXT("A6_cover"),
		TEXT("A0_wait_hq"), TEXT("A0_nosprint") })
	{
		TestNull(FString::Printf(TEXT("%s is gone in rev 5"), Gone), Data.FindStep(Gone));
	}
	// Rev 5c (designer review 2026-10-13): the bridge teaches nothing (it holds the followers itself: one at a time on its own);
	// selection + Solo are taught at the hut.
	const FTutorialStep* Bridge = Data.FindStep(TEXT("A1_bridge"));
	TestTrue(TEXT("the bridge: an objective done when all three are across (BridgeCrossed x3 distinct)"), Bridge && Bridge->Kind == ETutorialStepKind::Objective
		&& Bridge->Trigger.Volume == FName(TEXT("VOL_A1_BEACON2")) && Bridge->Complete.Event == TEXT("BridgeCrossed") && Bridge->Complete.Count == 3
		&& Bridge->Complete.bDistinctSubjects && Bridge->Unlock.IsEmpty());
	const FTutorialStep* BridgeTalk = Data.FindStep(TEXT("A1_wt_bridge"));
	TestTrue(TEXT("a short line at the bridge"), BridgeTalk && BridgeTalk->Trigger.Volume == FName(TEXT("VOL_A1_BEACON2")) && BridgeTalk->Lines.Num() == 2);
	for (const FTutorialStep& Step : Data.Steps)
	{
		TestFalse(FString::Printf(TEXT("%s: nothing at the bridge teaches select / solo"), *Step.Id.ToString()),
			Step.Trigger.Volume == FName(TEXT("VOL_A1_BEACON2")) && (Step.Keys.Contains(TEXT("Squad.Solo")) || Step.Keys.Contains(TEXT("Select.Member"))));
	}
	const FTutorialStep* Select = Data.FindStep(TEXT("A1_select"));
	TestTrue(TEXT("A1_select on the approach to the hut"), Select && Select->Trigger.Type == ETutorialTriggerType::Volume
		&& Select->Trigger.Volume == FName(TEXT("VOL_A1_HUT_APPROACH")));
	// Rev 5c part 3: the tactical hut entry - left of the door, right of the door, the third goes in (three different operatives).
	const FTutorialStep* StackL = Data.FindStep(TEXT("A1_hut_stack_l"));
	const FTutorialStep* StackR = Data.FindStep(TEXT("A1_hut_stack_r"));
	const FTutorialStep* Enter = Data.FindStep(TEXT("A1_hut_enter"));
	TestTrue(TEXT("left of the door: cover entered in VOL_A1_DOOR_L, not by the other two; unlocks cover"), StackL && StackL->Trigger.Step == FName(TEXT("A1_solo"))
		&& StackL->Complete.Event == TEXT("CoverEntered") && StackL->Complete.InVolume == FName(TEXT("VOL_A1_DOOR_L")) && StackL->Unlock.Contains(TEXT("Cover.*"))
		&& StackL->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_stack_r"))) && StackL->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_enter"))));
	TestTrue(TEXT("right of the door: cover entered in VOL_A1_DOOR_R, not by the other two"), StackR && StackR->Trigger.Step == FName(TEXT("A1_solo"))
		&& StackR->Complete.Event == TEXT("CoverEntered") && StackR->Complete.InVolume == FName(TEXT("VOL_A1_DOOR_R"))
		&& StackR->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_stack_l"))) && StackR->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_enter"))));
	TestTrue(TEXT("the third goes in: VOL_A1_HUT entered by neither stacker"), Enter && Enter->Trigger.Step == FName(TEXT("A1_solo"))
		&& Enter->Complete.Volume == FName(TEXT("VOL_A1_HUT")) && Enter->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_stack_l")))
		&& Enter->Complete.DistinctFrom.Contains(FName(TEXT("A1_hut_stack_r"))));
	TestTrue(TEXT("the loot lesson follows the entry"), Data.FindStep(TEXT("A1_loot_open")) && Data.FindStep(TEXT("A1_loot_open"))->Trigger.Step == FName(TEXT("A1_hut_enter")));
	const FTutorialStep* Solo = Data.FindStep(TEXT("A1_solo"));
	TestTrue(TEXT("A1_solo after the select card: Solo ON (never precredited), unlocks solo / regroup"), Solo && Solo->Trigger.Type == ETutorialTriggerType::AfterStep
		&& Solo->Trigger.Step == FName(TEXT("A1_select")) && Solo->Complete.Commands == TArray<FString>{ PlayerCommands::SoloOn.ToString() }
		&& !TutorialRules::CanPrecredit(*Solo) && Solo->Unlock.Contains(TEXT("Squad.Solo")) && Solo->Unlock.Contains(TEXT("Squad.Regroup")));
	// Rev 5d (designer request 2026-10-14): share the hut's heat packs - everyone carries one and at least one was handed over.
	const FTutorialStep* Share = Data.FindStep(TEXT("A1_share"));
	const FTutorialStep* ShareCard = Data.FindStep(TEXT("A1_share_card"));
	auto IsShareCondition = [](const FTutorialStep* Step)
	{
		return Step && Step->Complete.Conditions.Num() == 2 && Step->Complete.Conditions.ContainsByPredicate([](const FTutorialCondition& C)
			{ return C.Stat == FName(TEXT("squad_min_heat_packs")) && C.bAtLeast && C.Value == 1.f; })
			&& Step->Complete.Conditions.ContainsByPredicate([](const FTutorialCondition& C)
			{ return C.Stat == FName(TEXT("heat_packs_given")) && C.bAtLeast && C.Value == 1.f; }) && Step->bPrecredit;
	};
	TestTrue(TEXT("rev 5d: the share objective after the loot, done when everyone carries a pack and one was handed over"), Share
		&& Share->Kind == ETutorialStepKind::Objective && Share->Trigger.Step == FName(TEXT("A1_loot_take")) && IsShareCondition(Share));
	TestTrue(TEXT("rev 5d: the share card (how to hand an item over) with the same completion"), ShareCard && ShareCard->Kind == ETutorialStepKind::HintCard
		&& ShareCard->Trigger.Step == FName(TEXT("A1_loot_take")) && IsShareCondition(ShareCard) && ShareCard->Keys.Contains(PlayerCommands::GiveItem.ToString()));
	const FTutorialStep* Regroup = Data.FindStep(TEXT("A1_regroup"));
	TestTrue(TEXT("regroup after the share card, never precredited"), Regroup && Regroup->Trigger.Step == FName(TEXT("A1_share_card")) && !Regroup->bPrecredit);
	const FTutorialStep* Box = Data.FindStep(TEXT("A1_box"));
	TestTrue(TEXT("box select after the regroup (moved from the bridge)"), Box && Box->Trigger.Type == ETutorialTriggerType::AfterStep && Box->Trigger.Step == FName(TEXT("A1_regroup")));
	const FTutorialStep* Banter = Data.FindStep(TEXT("A1_wt_banter"));
	TestTrue(TEXT("the squad banter plays after the bridge"), Banter && Banter->Kind == ETutorialStepKind::WalkTalk && Banter->Trigger.Step == FName(TEXT("A1_bridge"))
		&& Banter->Lines.Num() == 4);
	const FTutorialStep* HeatPack = Data.FindStep(TEXT("A1_heatpack"));
	TestTrue(TEXT("heat pack card focuses the coldest and UNLOCKS the inventory / items"), HeatPack && HeatPack->Focus == FName(TEXT("coldest_operative"))
		&& HeatPack->Unlock.Contains(TEXT("Inventory.*")) && HeatPack->Unlock.Contains(TEXT("Item.*")) && HeatPack->Trigger.Requires.Contains(FName(TEXT("A1_bridge"))));
	const FTutorialStep* SprintCard = Data.FindStep(TEXT("A1_sprint"));
	TestTrue(TEXT("A1_sprint: 'running keeps you warm' after the heat pack lesson, a survival tip (no command completion, unlocks nothing; rev 5c)"), SprintCard
		&& SprintCard->Trigger.Type == ETutorialTriggerType::AfterStep && SprintCard->Trigger.Step == FName(TEXT("A1_heatpack_mate")) && SprintCard->Unlock.IsEmpty()
		&& SprintCard->Complete.Commands.IsEmpty() && !SprintCard->Complete.HasCondition() && SprintCard->Complete.TimeoutSeconds > 0.f);
	const FTutorialStep* Resupply = Data.FindStep(TEXT("A1_wt_resupply"));
	TestTrue(TEXT("A1_wt_resupply after the sprint card"), Resupply && Resupply->Trigger.Type == ETutorialTriggerType::AfterStep && Resupply->Trigger.Step == FName(TEXT("A1_sprint"))
		&& Resupply->Lines.Num() == 1 && Resupply->Lines[0].TextId == FName(TEXT("WT_A1_RESUPPLY")));
	const FTutorialStep* HutObjective = Data.FindStep(TEXT("A1_obj_hut"));
	TestTrue(TEXT("A1_obj_hut keeps its id, shows after the sprint card, done on ItemLooted.HeatPack"), HutObjective && HutObjective->Kind == ETutorialStepKind::Objective
		&& HutObjective->Trigger.Step == FName(TEXT("A1_sprint")) && HutObjective->Complete.Event == TEXT("ItemLooted.HeatPack"));
	const FTutorialStep* GustStep = Data.FindStep(TEXT("A1_gust"));
	TestTrue(TEXT("gust: 20 s after the crossing, only below 40 %, cold_bump 20"), GustStep && GustStep->Trigger.Type == ETutorialTriggerType::AfterStep
		&& GustStep->Trigger.Step == FName(TEXT("A1_bridge")) && GustStep->Trigger.DelaySeconds == 20.f && GustStep->Conditions.Num() == 1
		&& !GustStep->Conditions[0].bAtLeast && GustStep->Conditions[0].Value == 40.f && GustStep->Actions.Num() == 1 && GustStep->Actions[0].Amount == 20.f);
	if (!GustStep || GustStep->Actions.IsEmpty())
	{
		return false;
	}
	TestTrue(TEXT("stances moved to the approach (A3, VOL_A3_WIND)"), Data.FindStep(TEXT("A3_stance")) && Data.FindStep(TEXT("A3_stance"))->Trigger.Volume == FName(TEXT("VOL_A3_WIND")));
	// Rev 5d (designer request 2026-10-14): the barrel goes INTO the half-ruined building (VOL_A2_RUIN) and is lit there.
	const FTutorialStep* RuinTalk = Data.FindStep(TEXT("A2_wt_ruin"));
	const FTutorialStep* Push = Data.FindStep(TEXT("A2_push"));
	const FTutorialStep* Light = Data.FindStep(TEXT("A2_light"));
	const FTutorialStep* Warm = Data.FindStep(TEXT("A2_obj_warm"));
	TestTrue(TEXT("rev 5d: the ruin walk-and-talk (Engineer / Commander) after the cold lines"), RuinTalk && RuinTalk->Kind == ETutorialStepKind::WalkTalk
		&& RuinTalk->Trigger.Step == FName(TEXT("A2_wt_cold")) && RuinTalk->Lines.Num() == 3 && RuinTalk->Lines[0].Speaker == TEXT("Engineer"));
	TestTrue(TEXT("rev 5d: push done only when the barrel is set down inside VOL_A2_RUIN"), Push && Push->Complete.Event == TEXT("RelocationDone")
		&& Push->Complete.InVolume == FName(TEXT("VOL_A2_RUIN")) && Push->Unlock.Contains(PlayerCommands::Relocate.ToString()));
	TestTrue(TEXT("rev 5d: then light it there"), Light && Light->Trigger.Step == FName(TEXT("A2_push")) && Light->Complete.Event == TEXT("BarrelLit")
		&& Light->Complete.InVolume == FName(TEXT("VOL_A2_RUIN")));
	TestTrue(TEXT("rev 5d: the warm-up objective asks for the fire in the ruin"), Warm && Warm->Complete.InVolume == FName(TEXT("VOL_A2_RUIN")));
	TestNull(TEXT("rev 5d: the 'roll it to the hut' radio is gone"), Data.FindStep(TEXT("A2_wt_roll")));
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
	float MinPacks = 0.f;
	float Given = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Cold, &Packs, &MinPacks, &Given](FName Stat)
	{
		return Stat == FName(TEXT("squad_heat_packs")) ? Packs : Stat == FName(TEXT("squad_min_heat_packs")) ? MinPacks
			: Stat == FName(TEXT("heat_packs_given")) ? Given : Cold;
	};
	Machine.Load(Data);
	Machine.StartBeat(Data.StartBeat);
	TestTrue(TEXT("A0: the inventory, stances and the pause locked from the start"), Machine.IsCommandLocked(PlayerCommands::InventoryOpen)
		&& Machine.IsCommandLocked(PlayerCommands::UseHeatPack) && Machine.IsCommandLocked(PlayerCommands::StanceCrouch) && Machine.IsCommandLocked(PlayerCommands::TacticalPause));
	TestFalse(TEXT("A0: move and sprint free from the very start (2026-10-13)"), Machine.IsCommandLocked(PlayerCommands::Move) || Machine.IsCommandLocked(PlayerCommands::Sprint));
	Machine.Tick(1.1f); // the radio's 1 s delay (no Pump: it would play the radio out at once)
	TestEqual(TEXT("A0: the HQ radio plays"), Machine.GetActiveSpeech(), FName(TEXT("A0_radio_hq")));
	TestEqual(TEXT("A0: the beacon objective is up with it"), Machine.GetState(TEXT("A0_obj_beacon")), ETutorialStepState::Active);
	Machine.Tick(1.f);
	TestEqual(TEXT("A0: the first camera card shows WHILE HQ talks"), Machine.GetActiveCard(), FName(TEXT("A0_cam_pan")));
	TestEqual(TEXT("A0: the radio still plays"), Machine.GetActiveSpeech(), FName(TEXT("A0_radio_hq")));
	Machine.HandleCommand(PlayerCommands::CameraPanKeys);
	Machine.HandleCommandHold(PlayerCommands::CameraPanKeys, 1.2f);
	TestEqual(TEXT("pan done during the radio"), Machine.GetState(TEXT("A0_cam_pan")), ETutorialStepState::Completed);
	// Q / E pressed early (before their card): precredited when it is due.
	Machine.HandleCommand(PlayerCommands::CameraRotateKeys);
	Machine.HandleCommand(PlayerCommands::CameraRotateKeys);
	Machine.Tick(1.6f);
	TestEqual(TEXT("the MMB drag card next (the radio still talking)"), Machine.GetActiveCard(), FName(TEXT("A0_cam_pan_drag")));
	Machine.HandleCommand(PlayerCommands::CameraPanDrag);
	Machine.HandleCommandHold(PlayerCommands::CameraPanDrag, 0.6f);
	TestEqual(TEXT("drag done"), Machine.GetState(TEXT("A0_cam_pan_drag")), ETutorialStepState::Completed);
	TestEqual(TEXT("Q/E precredited from the history"), Machine.GetState(TEXT("A0_cam_rotate_keys")), ETutorialStepState::Completed);
	for (const TCHAR* Camera : { TEXT("A0_cam_rotate_mouse"), TEXT("A0_cam_zoom") })
	{
		Machine.Tick(1.6f);
		TestTrue(FString(Camera) + TEXT(" skipped"), Machine.SkipStep(Camera));
	}
	Machine.Tick(1.6f);
	TestEqual(TEXT("rev 5c: camera chain done, the radio still talks: 'click to walk' shows at once"), Machine.GetActiveCard(), FName(TEXT("A0_move")));
	TestEqual(TEXT("the radio still plays"), Machine.GetActiveSpeech(), FName(TEXT("A0_radio_hq")));
	Machine.HandleCommand(PlayerCommands::Move);
	TestEqual(TEXT("a move order completes the walk card"), Machine.GetState(TEXT("A0_move")), ETutorialStepState::Completed);
	Machine.Tick(1.6f);
	TestEqual(TEXT("then 'double-click to run'"), Machine.GetActiveCard(), FName(TEXT("A0_sprint")));
	Machine.HandleCommand(PlayerCommands::Sprint);
	TestEqual(TEXT("a sprint order completes the run card"), Machine.GetState(TEXT("A0_sprint")), ETutorialStepState::Completed);
	Machine.CompleteStep(TEXT("A0_radio_hq")); // played out, or Enter-held / SKIP (both finish it as completed)
	Machine.ConsumeTransitions();
	TestEqual(TEXT("the beacon objective still waits for beacon 1"), Machine.GetState(TEXT("A0_obj_beacon")), ETutorialStepState::Active);
	Machine.HandleVolume(TEXT("VOL_A0_BEACON1"), NAME_None, TEXT("A1"));
	TestEqual(TEXT("beacon 1 starts A1"), Machine.GetCurrentBeat(), FName(TEXT("A1")));
	TestEqual(TEXT("the beacon objective completes at beacon 1"), Machine.GetState(TEXT("A0_obj_beacon")), ETutorialStepState::Completed);
	TestTrue(TEXT("A1: the inventory still locked until the heat pack lesson"), Machine.IsCommandLocked(PlayerCommands::InventoryOpen));
	TestTrue(TEXT("A1: solo locked until the hut solo card"), Machine.IsCommandLocked(PlayerCommands::SoloOn));
	Pump(Machine, 2.f);
	TestEqual(TEXT("no card before the crack"), Machine.GetActiveCard(), FName());
	Machine.HandleVolume(TEXT("VOL_A1_BEACON2"), NAME_None, NAME_None);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the crack: a line + the crossing objective, no card (rev 5c)"), Machine.GetActiveCard(), FName());
	TestEqual(TEXT("the crossing objective is up"), Machine.GetState(TEXT("A1_bridge")), ETutorialStepState::Active);
	TestEqual(TEXT("the bridge line played"), Machine.GetState(TEXT("A1_wt_bridge")), ETutorialStepState::Completed);
	TestTrue(TEXT("the bridge unlocks nothing: solo still locked"), Machine.IsCommandLocked(PlayerCommands::SoloOn));
	Machine.HandleEvent(TEXT("BridgeRefused"), TEXT("Op2"));
	TestEqual(TEXT("the bridge creak line plays once"), Machine.GetState(TEXT("A1_bridge_refused")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op1"));
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op1"));
	TestEqual(TEXT("the same operative twice counts once"), Machine.GetProgress(TEXT("A1_bridge")), 1);
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op2"));
	Machine.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op3"));
	TestEqual(TEXT("three distinct crossings complete it"), Machine.GetState(TEXT("A1_bridge")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then the squad banter"), Machine.GetState(TEXT("A1_wt_banter")), ETutorialStepState::Completed);
	TestEqual(TEXT("no regroup / box card after the bridge (they arrive together)"), Machine.GetActiveCard(), FName());
	TestEqual(TEXT("no heat-pack card while nobody is cold"), Machine.GetState(TEXT("A1_heatpack")), ETutorialStepState::Idle);
	TestEqual(TEXT("the gust has not come yet (2 s after the crossing)"), Machine.GetState(TEXT("A1_gust")), ETutorialStepState::Waiting);
	Pump(Machine, 19.f);
	TestEqual(TEXT("20 s after the crossing, nobody at Chills: the gust played"), Machine.GetState(TEXT("A1_gust")), ETutorialStepState::Completed);
	Cold = ColdRules::ApplyColdShock(Cold, GustStep->Actions[0].Amount, GustStep->Actions[0].MinLevel); // what the runtime does on show
	TestTrue(TEXT("the gust leaves everyone at Chills"), Cold >= 40.f);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then the heat pack card"), Machine.GetActiveCard(), FName(TEXT("A1_heatpack")));
	TestFalse(TEXT("the heat pack card UNLOCKS the inventory and the items"), Machine.IsCommandLocked(PlayerCommands::InventoryOpen) || Machine.IsCommandLocked(PlayerCommands::UseHeatPack));
	TestEqual(TEXT("the running tip waits for the lesson"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::Idle);
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"));
	Cold = 7.f;
	Packs = 2.f;
	TestEqual(TEXT("using a heat pack completes it"), Machine.GetState(TEXT("A1_heatpack")), ETutorialStepState::Completed);
	Pump(Machine, 1.5f);
	TestEqual(TEXT("nobody else cold: the mate card is skipped"), Machine.GetState(TEXT("A1_heatpack_mate")), ETutorialStepState::Skipped);
	Pump(Machine, 1.5f);
	TestEqual(TEXT("'running keeps you warm': the survival tip (not precredited by the A0 sprint)"), Machine.GetActiveCard(), FName(TEXT("A1_sprint")));
	TestEqual(TEXT("a double click sprints (never locked)"), TutorialRules::ResolveLockedCommand(Machine.GetLocks(), PlayerCommands::Sprint), PlayerCommands::Sprint);
	TestEqual(TEXT("the hut objective waits for the tip"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Idle);
	Machine.HandleCommand(PlayerCommands::Sprint);
	TestEqual(TEXT("a sprint does not end the tip (no control lesson any more)"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::Active);
	Pump(Machine, 10.f);
	TestEqual(TEXT("the tip ends after its 10 s"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::TimedOut);
	Pump(Machine, 2.f);
	TestEqual(TEXT("WT_A1_RESUPPLY played"), Machine.GetState(TEXT("A1_wt_resupply")), ETutorialStepState::Completed);
	TestEqual(TEXT("and the hut objective is up"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Active);
	TestEqual(TEXT("hut loot cards not before the hut"), Machine.GetState(TEXT("A1_loot_open")), ETutorialStepState::Idle);
	TestTrue(TEXT("crates locked before the hut lesson"), Machine.IsCommandLocked(PlayerCommands::LootCrate));
	Machine.HandleVolume(TEXT("VOL_A1_HUT_APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	Pump(Machine, 2.f);
	TestEqual(TEXT("approaching the hut: the walk-and-talk played"), Machine.GetState(TEXT("A1_wt_hut_approach")), ETutorialStepState::Completed);
	TestEqual(TEXT("hut: select first (rev 5c: moved from the bridge)"), Machine.GetActiveCard(), FName(TEXT("A1_select")));
	Machine.HandleCommand(PlayerCommands::SelectMember);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then Solo"), Machine.GetActiveCard(), FName(TEXT("A1_solo")));
	TestFalse(TEXT("the solo card unlocks solo / regroup"), Machine.IsCommandLocked(PlayerCommands::SoloOn) || Machine.IsCommandLocked(PlayerCommands::Regroup));
	TestTrue(TEXT("cover still locked before the door"), Machine.IsCommandLocked(PlayerCommands::CoverEnter));
	Machine.HandleCommand(PlayerCommands::SoloOn);
	TestEqual(TEXT("B: solo on completes it"), Machine.GetState(TEXT("A1_solo")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("the door: left side first"), Machine.GetActiveCard(), FName(TEXT("A1_hut_stack_l")));
	TestTrue(TEXT("right side and 'go in' queued (they count meanwhile)"), Machine.GetState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Queued
		&& Machine.GetState(TEXT("A1_hut_enter")) == ETutorialStepState::Queued);
	TestFalse(TEXT("the door card unlocks cover"), Machine.IsCommandLocked(PlayerCommands::CoverPreview) || Machine.IsCommandLocked(PlayerCommands::CoverEnter));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("VOL_A1_HUT_APPROACH") });
	TestEqual(TEXT("cover somewhere else does not count"), Machine.GetProgress(TEXT("A1_hut_stack_l")) + Machine.GetProgress(TEXT("A1_hut_stack_r")), 0);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("VOL_A1_DOOR_R") });
	TestEqual(TEXT("Op2 right of the door first: the queued right card completes silently"), Machine.GetState(TEXT("A1_hut_stack_r")), ETutorialStepState::Completed);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("VOL_A1_DOOR_L") });
	TestEqual(TEXT("the same operative on the left does not count (distinct_from)"), Machine.GetState(TEXT("A1_hut_stack_l")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op1"), { TEXT("VOL_A1_DOOR_L") });
	TestEqual(TEXT("Op1 left of the door completes the left card"), Machine.GetState(TEXT("A1_hut_stack_l")), ETutorialStepState::Completed);
	Machine.HandleVolume(TEXT("VOL_A1_HUT"), NAME_None, NAME_None, TEXT("Op1"));
	Machine.HandleVolume(TEXT("VOL_A1_HUT"), NAME_None, NAME_None, TEXT("Op2"));
	TestEqual(TEXT("a stacker walking in is not the third man"), Machine.GetState(TEXT("A1_hut_enter")), ETutorialStepState::Queued);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then 'go in'"), Machine.GetActiveCard(), FName(TEXT("A1_hut_enter")));
	Machine.HandleVolume(TEXT("VOL_A1_HUT"), NAME_None, NAME_None, TEXT("Op3"));
	TestEqual(TEXT("Op3 inside completes it"), Machine.GetState(TEXT("A1_hut_enter")), ETutorialStepState::Completed);
	Pump(Machine, 2.f);
	TestEqual(TEXT("inside: the line played"), Machine.GetState(TEXT("A1_wt_hut")), ETutorialStepState::Completed);
	TestEqual(TEXT("hut: open the crate"), Machine.GetActiveCard(), FName(TEXT("A1_loot_open")));
	TestFalse(TEXT("the hut card unlocks the crates"), Machine.IsCommandLocked(PlayerCommands::LootCrate) || Machine.IsCommandLocked(PlayerCommands::LootTake));
	Machine.HandleCommand(PlayerCommands::LootCrate);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then take the heat packs"), Machine.GetActiveCard(), FName(TEXT("A1_loot_take")));
	Packs = 5.f;
	Machine.HandleEvent(TEXT("ItemLooted"));
	Machine.HandleEvent(TEXT("ItemLooted.HeatPack"));
	TestEqual(TEXT("taking them completes the card"), Machine.GetState(TEXT("A1_loot_take")), ETutorialStepState::Completed);
	TestEqual(TEXT("and the hut objective"), Machine.GetState(TEXT("A1_obj_hut")), ETutorialStepState::Completed);
	Pump(Machine, 2.5f);
	TestEqual(TEXT("the closing line played"), Machine.GetState(TEXT("A1_wt_stocked")), ETutorialStepState::Completed);
	// Rev 5d: share the heat packs (one mate has none; then the looter hands them out).
	TestEqual(TEXT("rev 5d: the share card after the loot"), Machine.GetActiveCard(), FName(TEXT("A1_share_card")));
	TestEqual(TEXT("rev 5d: the share objective with it"), Machine.GetState(TEXT("A1_share")), ETutorialStepState::Active);
	MinPacks = 1.f;
	Given = 2.f;
	Machine.Tick(0.1f);
	TestTrue(TEXT("everyone carries one + handed over: card and objective done"), Machine.GetState(TEXT("A1_share_card")) == ETutorialStepState::Completed
		&& Machine.GetState(TEXT("A1_share")) == ETutorialStepState::Completed);
	Pump(Machine, 3.f);
	TestEqual(TEXT("regroup card after the share card"), Machine.GetActiveCard(), FName(TEXT("A1_regroup")));
	Machine.HandleCommand(PlayerCommands::Regroup);
	Pump(Machine, 2.f);
	TestEqual(TEXT("then box select"), Machine.GetActiveCard(), FName(TEXT("A1_box")));
	Machine.HandleCommand(PlayerCommands::SelectBox);
	Pump(Machine, 2.f);
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
	TestFalse(TEXT("the stance card (even precredited) unlocks the stances"), Machine.IsCommandLocked(PlayerCommands::StanceProne) || Machine.IsCommandLocked(PlayerCommands::SquadStanceProne));
	TestTrue(TEXT("A3: the pause still locked (taught in the horde beat)"), Machine.IsCommandLocked(PlayerCommands::TacticalPause));
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
	TestEqual(TEXT("Frozen Lake: less cold (cold_rate_mult 0.8, user request 2026-10-13)"), Level.ColdRateMult, 0.8f, 0.0001f);
	TestEqual(TEXT("Demo 01: every operative starts with 1 heat pack (the A1 heat pack lesson); the hut crate holds more"), Level.SquadLoadout.HeatPacksCount, 1);
	for (const int32 RoleKit : { 0, 1, 2, 3 })
	{
		TestEqual(FString::Printf(TEXT("role kit %d: the demo value wins, 1 pack each"), RoleKit), LoadoutRules::StartingHeatPacks(RoleKit, Level.SquadLoadout.HeatPacksCount, 3), 1);
	}
	return true;
}

// --- Demo 01 rev 5b (user requests 2026-10-13): progressive unlock, radio + camera together, early beacon ---

namespace TutorialLockTestData
{
	const TCHAR* Chain = TEXT(R"JSON({
		"id": "chain", "start_beat": "A0", "beats": [ { "id": "A0" }, { "id": "A1" } ],
		"steps": [
			{ "id": "radio", "beat": "A0", "kind": "radio", "trigger": { "type": "beat_start", "delay_s": 1 }, "lines": [ { "text_id": "WT_R" } ] },
			{ "id": "pan", "beat": "A0", "kind": "hint_card", "trigger": { "type": "beat_start", "delay_s": 2 }, "complete": { "commands": ["Camera.Pan.Keys"] } },
			{ "id": "yaw", "beat": "A0", "kind": "hint_card", "trigger": { "type": "after_step", "step": "pan" }, "complete": { "commands": ["Camera.Rotate.Keys"] } },
			{ "id": "zoom", "beat": "A0", "kind": "hint_card", "trigger": { "type": "after_step", "step": "yaw" }, "complete": { "commands": ["Camera.Zoom"] } },
			{ "id": "walk", "beat": "A0", "kind": "hint_card", "trigger": { "type": "after_step", "step": ["zoom", "radio"] }, "complete": { "volume": "VOL_B1" } },
			{ "id": "a1_card", "beat": "A1", "kind": "hint_card", "trigger": { "type": "beat_start" }, "complete": { "commands": ["Squad.Solo"] } }
		] })JSON");
}

TUTORIAL_LOCK_TEST(FTutorialLockExceptionTest, "Lock.Exceptions")
bool FTutorialLockExceptionTest::RunTest(const FString&)
{
	TestTrue(TEXT("Trap.* covers Trap.DisarmMine"), TutorialRules::PatternCovers(TEXT("Trap.*"), TEXT("Trap.DisarmMine")));
	TestTrue(TEXT("Trap.* covers Trap.Disarm*"), TutorialRules::PatternCovers(TEXT("Trap.*"), TEXT("Trap.Disarm*")));
	TestTrue(TEXT("* covers all"), TutorialRules::PatternCovers(TEXT("*"), TEXT("Stance.*")));
	TestFalse(TEXT("an id does not cover a wildcard"), TutorialRules::PatternCovers(TEXT("Trap.DisarmMine"), TEXT("Trap.*")));
	TestFalse(TEXT("other family"), TutorialRules::PatternCovers(TEXT("Stance.*"), TEXT("Squad.Solo")));

	TArray<FString> Locks;
	TutorialRules::ApplyLockChanges(Locks, { TEXT("Trap.*"), TEXT("Stance.*") }, {});
	TestTrue(TEXT("one command freed out of a wildcard"), TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("Trap.DisarmMine") }));
	TestFalse(TEXT("the mine can be defused"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::DisarmMine));
	TestTrue(TEXT("the rest of Trap.* stays locked"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::DisarmTripwire)
		&& TutorialRules::IsCommandLocked(Locks, PlayerCommands::SetTrap));
	TestTrue(TEXT("stored as an exception entry"), Locks.Contains(TEXT("!Trap.DisarmMine")));
	TestFalse(TEXT("freeing it again: no change"), TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("Trap.DisarmMine") }));
	TestTrue(TEXT("a new Trap.* lock takes the exception back"), TutorialRules::ApplyLockChanges(Locks, { TEXT("Trap.*") }, {})
		&& TutorialRules::IsCommandLocked(Locks, PlayerCommands::DisarmMine) && !Locks.Contains(TEXT("!Trap.DisarmMine")));
	TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("Trap.DisarmMine") });
	TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("Trap.*") });
	TestFalse(TEXT("unlocking the wildcard drops its lock and the stale exception"), Locks.Contains(TEXT("Trap.*")) || Locks.Contains(TEXT("!Trap.DisarmMine")));
	TestTrue(TEXT("other families untouched"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::StanceCrouch));
	TutorialRules::ApplyLockChanges(Locks, { TEXT("Stance.Squad.*") }, {});
	TutorialRules::ApplyLockChanges(Locks, {}, { TEXT("Stance.*") });
	TestFalse(TEXT("Stance.* also frees the narrower Stance.Squad.* lock"), TutorialRules::IsCommandLocked(Locks, PlayerCommands::SquadStanceProne) || !Locks.IsEmpty());
	TestEqual(TEXT("command family"), TutorialRules::GetCommandFamily(PlayerCommands::SquadStanceProne), FString(TEXT("Stance")));
	TestEqual(TEXT("family of an id without a dot"), TutorialRules::GetCommandFamily(TEXT("Solo")), FString(TEXT("Solo")));

	// Exceptions survive a save round trip.
	FTutorialProgress Progress;
	Progress.CurrentBeat = TEXT("A0");
	Progress.bHasLocks = true;
	Progress.Locks = { TEXT("Trap.*"), TEXT("!Trap.DisarmMine") };
	FTutorialProgress Loaded;
	TestTrue(TEXT("loads"), TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Progress, true), Loaded));
	TestTrue(TEXT("exception kept"), Loaded.Locks == Progress.Locks && !TutorialRules::IsCommandLocked(Loaded.Locks, PlayerCommands::DisarmMine));
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialProgressiveDemoTest, "Lock.ProgressiveDemo")
bool FTutorialProgressiveDemoTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(Data.StartBeat);
	// Always allowed (user request 2026-10-13): camera, move / sprint / walk-up, selection, dialogue, system.
	const TArray<FString> AlwaysFree = { TEXT("Camera."), TEXT("Order.Move"), TEXT("Order.Sprint"), TEXT("Order.Interact"), TEXT("Select."), TEXT("Dialogue."), TEXT("System.") };
	auto IsAlwaysFree = [&AlwaysFree](FName Id)
	{
		return AlwaysFree.ContainsByPredicate([Id](const FString& Prefix) { return Id.ToString().StartsWith(Prefix); });
	};
	for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
	{
		if (IsAlwaysFree(Info.Id))
		{
			TestFalse(FString::Printf(TEXT("A0: %s is never locked"), *Info.Id.ToString()), Machine.IsCommandLocked(Info.Id));
		}
		else
		{
			TestTrue(FString::Printf(TEXT("A0: %s is locked until its lesson"), *Info.Id.ToString()), Machine.IsCommandLocked(Info.Id));
		}
	}
	// Each lesson unlocks what it teaches (fired directly: the order of the beats is covered by Schema.DemoFileA0A1Flow).
	struct FLesson
	{
		const TCHAR* Step;
		TArray<FName> Freed;
		TArray<FName> StillLocked;
	};
	const TArray<FLesson> Lessons = {
		{ TEXT("A1_solo"), { PlayerCommands::SoloOn, PlayerCommands::Regroup }, { PlayerCommands::Guard, PlayerCommands::Autonomy, PlayerCommands::CoverEnter } },
		{ TEXT("A1_hut_stack_l"), { PlayerCommands::CoverPreview, PlayerCommands::CoverEnter, PlayerCommands::CoverFireMode }, { PlayerCommands::HoldLine } },
		{ TEXT("A1_heatpack"), { PlayerCommands::InventoryOpen, PlayerCommands::UseHeatPack, PlayerCommands::UseItem }, { PlayerCommands::LootCrate } },
		{ TEXT("A1_loot_open"), { PlayerCommands::LootCrate, PlayerCommands::LootTake }, { PlayerCommands::Relocate } },
		{ TEXT("A2_push"), { PlayerCommands::Relocate }, { PlayerCommands::LightBarrel, PlayerCommands::UseObject } },
		{ TEXT("A2_light"), { PlayerCommands::LightBarrel }, { PlayerCommands::UseObject } },
		{ TEXT("A3_stance"), { PlayerCommands::StanceCrouch, PlayerCommands::StanceProne, PlayerCommands::SquadStanceProne }, { PlayerCommands::TacticalPause } },
		{ TEXT("A3_cover"), { PlayerCommands::CoverPreview, PlayerCommands::CoverEnter, PlayerCommands::CoverFireMode }, { PlayerCommands::HoldLine } },
		{ TEXT("A3_mine"), { PlayerCommands::DisarmMine, PlayerCommands::TargetedShot }, { PlayerCommands::DisarmTripwire, PlayerCommands::SetTrap, PlayerCommands::PriorityTarget } },
		{ TEXT("A3_wire"), { PlayerCommands::DisarmTripwire }, { PlayerCommands::DisarmTrap } },
		{ TEXT("A4_cones"), { PlayerCommands::ToggleVisionCones }, { PlayerCommands::GrenadeAim } },
		{ TEXT("A4_tripwire"), { PlayerCommands::DeployTripwire }, { PlayerCommands::DeployMine, PlayerCommands::DeployStart } },
	};
	for (const FLesson& Lesson : Lessons)
	{
		// A follower (after_step) is already running once its predecessor was skipped; the others are fired here.
		TestTrue(FString::Printf(TEXT("%s fires"), Lesson.Step), TutorialRules::IsRunning(Machine.GetState(Lesson.Step)) || Machine.TriggerStep(Lesson.Step));
		Machine.SkipStep(Lesson.Step); // even a skipped (never shown) lesson applies its unlock
		for (const FName Id : Lesson.Freed)
		{
			TestFalse(FString::Printf(TEXT("%s unlocks %s"), Lesson.Step, *Id.ToString()), Machine.IsCommandLocked(Id));
		}
		for (const FName Id : Lesson.StillLocked)
		{
			TestTrue(FString::Printf(TEXT("after %s, %s is still locked"), Lesson.Step, *Id.ToString()), Machine.IsCommandLocked(Id));
		}
	}
	// Taught only in the later (not implemented) beats: locked through A4.
	for (const FName Id : { PlayerCommands::TacticalPause, PlayerCommands::TurnBasedEnter, PlayerCommands::EndTurn, PlayerCommands::Guard, PlayerCommands::Autonomy,
		PlayerCommands::CycleWeapon, PlayerCommands::Reload, PlayerCommands::GrenadeAim, PlayerCommands::FlareAim, PlayerCommands::VehicleBoard, PlayerCommands::WatchSector })
	{
		TestTrue(FString::Printf(TEXT("A4: %s still locked (taught in A5-A10)"), *Id.ToString()), Machine.IsCommandLocked(Id));
	}
	// The saved set keeps the exceptions; a load mid-tutorial restores exactly it.
	FTutorialProgress Loaded;
	TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Machine.ExportProgress(), true), Loaded);
	FTutorialStateMachine Restored;
	Restored.Load(Data);
	Restored.ImportProgress(Loaded);
	TestTrue(TEXT("restored: mine free, pause locked"), !Restored.IsCommandLocked(PlayerCommands::DisarmMine) && Restored.IsCommandLocked(PlayerCommands::TacticalPause));
	// Safety: a fight / tutorial OFF unlocks everything.
	TestTrue(TEXT("UnlockAll"), Machine.UnlockAll());
	for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
	{
		TestFalse(FString::Printf(TEXT("after UnlockAll: %s free"), *Info.Id.ToString()), Machine.IsCommandLocked(Info.Id));
	}
	// The later beats' safety lists: entering A4 straight from A0 still frees what A1-A3 taught.
	FTutorialStateMachine Jump;
	Jump.Load(Data);
	Jump.StartBeat(TEXT("A0"));
	Jump.StartBeat(TEXT("A4"));
	TestFalse(TEXT("A4 safety: stances / cover / inventory / loot / push free"), Jump.IsCommandLocked(PlayerCommands::StanceProne) || Jump.IsCommandLocked(PlayerCommands::CoverEnter)
		|| Jump.IsCommandLocked(PlayerCommands::InventoryOpen) || Jump.IsCommandLocked(PlayerCommands::LootCrate) || Jump.IsCommandLocked(PlayerCommands::Relocate));
	TestTrue(TEXT("A4 safety: the pause stays locked"), Jump.IsCommandLocked(PlayerCommands::TacticalPause));
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialAfterStepListTest, "Trigger.AfterStepList")
bool FTutorialAfterStepListTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Chain, Data))
	{
		return false;
	}
	const FTutorialStep* Walk = Data.FindStep(TEXT("walk"));
	TestTrue(TEXT("\"step\": [ ... ] parsed (first id kept in Step)"), Walk && Walk->Trigger.AfterSteps == TArray<FName>{ TEXT("zoom"), TEXT("radio") }
		&& Walk->Trigger.Step == FName(TEXT("zoom")));
	TArray<FString> Warnings;
	FString Error;
	FTutorialStepsData Bad;
	TestFalse(TEXT("an unknown id in the list is an error"), TutorialRules::ParseSteps(
		TEXT(R"({"steps":[{"id":"a","beat":"B","kind":"radio"},{"id":"b","beat":"B","trigger":{"type":"after_step","step":["a","zz"]}}]})"), Bad, Warnings, Error));
	TestFalse(TEXT("after_step without a step is an error"), TutorialRules::ParseSteps(
		TEXT(R"({"steps":[{"id":"b","beat":"B","trigger":{"type":"after_step"}}]})"), Bad, Warnings, Error));

	// The radio and the camera chain run together; the walk card waits for both, in either order.
	for (const bool bRadioFirst : { true, false })
	{
		FTutorialStateMachine Machine;
		Machine.Load(Data);
		Machine.StartBeat(TEXT("A0"));
		Machine.Tick(2.1f);
		TestTrue(TEXT("radio and the first card up together"), Machine.GetActiveSpeech() == FName(TEXT("radio")) && Machine.GetActiveCard() == FName(TEXT("pan")));
		if (bRadioFirst)
		{
			Machine.CompleteStep(TEXT("radio"));
			TestEqual(TEXT("radio over, camera chain running: no walk card"), Machine.GetState(TEXT("walk")), ETutorialStepState::Idle);
		}
		for (const FName Command : { PlayerCommands::CameraPanKeys, PlayerCommands::CameraRotateKeys, PlayerCommands::CameraZoom })
		{
			Machine.HandleCommand(Command);
			Machine.Tick(1.6f);
		}
		TestEqual(TEXT("camera chain done"), Machine.GetState(TEXT("zoom")), ETutorialStepState::Completed);
		if (!bRadioFirst)
		{
			TestEqual(TEXT("camera done, radio still talking: no walk card"), Machine.GetState(TEXT("walk")), ETutorialStepState::Idle);
			TestTrue(TEXT("the radio skipped by the player (finished as played out)"), Machine.CompleteStep(TEXT("radio")));
			Machine.Tick(1.6f);
		}
		TestEqual(FString::Printf(TEXT("both done -> the walk card (radio first %d)"), bRadioFirst ? 1 : 0), Machine.GetActiveCard(), FName(TEXT("walk")));
	}
	return true;
}

TUTORIAL_LOCK_TEST(FTutorialEarlierBeatDropTest, "Beat.EarlierCardsDropped")
bool FTutorialEarlierBeatDropTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialLockTestData::Parse(*this, TutorialLockTestData::Chain, Data))
	{
		return false;
	}
	// Beacon 1 reached while the radio talks and the first camera card is up (user request 2026-10-13: walking is free).
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Machine.Tick(2.1f);
	Machine.HandleCommand(PlayerCommands::CameraRotateKeys); // Q pressed early: the next stage is precredited when dropped
	Machine.HandleVolume(TEXT("VOL_B1"), NAME_None, TEXT("A1"));
	TestEqual(TEXT("A1 started"), Machine.GetCurrentBeat(), FName(TEXT("A1")));
	TestEqual(TEXT("the shown A0 card is dropped (skipped)"), Machine.GetState(TEXT("pan")), ETutorialStepState::Skipped);
	TestEqual(TEXT("its follower done by the history is precredited"), Machine.GetState(TEXT("yaw")), ETutorialStepState::Completed);
	TestEqual(TEXT("the rest of the chain is skipped without showing"), Machine.GetState(TEXT("zoom")), ETutorialStepState::Skipped);
	TestEqual(TEXT("the radio keeps playing"), Machine.GetActiveSpeech(), FName(TEXT("radio")));
	TestEqual(TEXT("the walk card waits for the radio"), Machine.GetState(TEXT("walk")), ETutorialStepState::Idle);
	bool bPrecreditFlagged = false;
	for (const FTutorialTransition& Transition : Machine.ConsumeTransitions())
	{
		bPrecreditFlagged |= Transition.StepId == FName(TEXT("yaw")) && Transition.bPrecredited;
	}
	TestTrue(TEXT("the precredit is reported (feed tick)"), bPrecreditFlagged);
	Machine.Tick(2.f);
	TestEqual(TEXT("the A1 card shows (nothing of A0 blocks the card slot)"), Machine.GetActiveCard(), FName(TEXT("a1_card")));
	Machine.CompleteStep(TEXT("radio"));
	TestEqual(TEXT("the radio ends later: the A0 walk card is dropped, not shown"), Machine.GetState(TEXT("walk")), ETutorialStepState::Skipped);
	TestEqual(TEXT("the A1 card stays"), Machine.GetActiveCard(), FName(TEXT("a1_card")));
	// A manual / event card of an earlier beat still shows (remedial hints keep working).
	FTutorialStateMachine Remedial;
	Remedial.Load(Data);
	Remedial.StartBeat(TEXT("A1"));
	Remedial.TriggerStep(TEXT("pan"));
	Remedial.Tick(2.5f);
	TestTrue(TEXT("a manually fired earlier-beat card is not dropped (only chain followers are)"), TutorialRules::IsRunning(Remedial.GetState(TEXT("pan"))));
	return true;
}

#undef TUTORIAL_LOCK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
