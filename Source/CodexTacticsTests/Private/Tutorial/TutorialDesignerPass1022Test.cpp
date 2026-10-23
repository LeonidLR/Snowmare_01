#include "Misc/AutomationTest.h"
#include "Characters/FirePostureRules.h"
#include "Core/PlayerCommands.h"
#include "Interactables/TrapApproachRules.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01, the game designer's play-test of 2026-10-22 (UE-only, no Godot reference): (1) the tactical pause lesson (two beat-less cards
// on the first TacticalPauseStarted), (2) a failed defuse - a click right at the trap he works on is the trap again
// (TrapApproachRules::IsRetryClick), (3) the posture lesson asks for DEFENSIVE (a DEFENSIVE squad never shoots first: the lure still
// works), (4) the tripwire card is beat-less and never precredited (the fence crawl's Stance.Prone used to precredit it - never shown),
// (5) "cancel_on": { "condition" } + the stat "all_dead.<binding>": the A4 steps bound to a dead zombie close, the horde prep names the traps.

#define TUTORIAL_PASS1022_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialPass1022
{
	bool LoadDemoSteps(FAutomationTestBase& Test, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("demo steps load (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			Test.AddError(FString::Printf(TEXT("the shipped steps file warns: %s"), *Warning));
		}
		return bOk;
	}

	void Run(FTutorialStateMachine& Machine, int32 Ticks = 8)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Machine.Tick(0.5f);
		}
	}

	/** Z1 = the zombie on the perimeter loop (A4_PERIMETER), Z2 = the far one (A4_LURE); A4_ANY = either. */
	void InstallZombies(FTutorialStateMachine& Machine)
	{
		Machine.EnemyFilterProvider = [](const FTutorialEnemyFilter& Filter, FName SubjectId)
		{
			const bool bBound = (SubjectId == FName(TEXT("Z1")) && Filter.Enemy == FName(TEXT("A4_PERIMETER")))
				|| (SubjectId == FName(TEXT("Z2")) && Filter.Enemy == FName(TEXT("A4_LURE")))
				|| ((SubjectId == FName(TEXT("Z1")) || SubjectId == FName(TEXT("Z2"))) && Filter.Enemy == FName(TEXT("A4_ANY")));
			return TutorialRules::PassesEnemyFilter(Filter, bBound, 3000.f, 0.f);
		};
	}

	/** A0 -> A3 with the cover field done, then the overlook (A4): the cone card and the posture card answered. */
	void ToTheOverlook(FTutorialStateMachine& Machine)
	{
		Machine.StartBeat(TEXT("A0"));
		Machine.StartBeat(TEXT("A3"));
		Machine.TriggerStep(TEXT("A3_cover"));
		Machine.SkipStep(TEXT("A3_cover"));
		Run(Machine);
		Machine.SkipStep(TEXT("A3_peek"));
		Run(Machine);
		Machine.StartBeat(TEXT("A4"));
		Run(Machine);
		Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
		Machine.HandleEvent(TEXT("EnemyWoke.A4_PERIMETER")); // 2026-10-23: zombie #1 wakes (the squad in cover at the overlook)
		Run(Machine);
		Machine.HandleCommand(PlayerCommands::PostureDefensive);
		Run(Machine);
	}

	/** Every A4 step bound to a zombie. */
	const TCHAR* const BoundSteps[] = { TEXT("A4_tripwire"), TEXT("A4_trap_wait"), TEXT("A4_hug_wall"), TEXT("A4_round_corner"), TEXT("A4_lure"),
		TEXT("A4_close_shot"), TEXT("A4_spotted") };
}

// --- (5) "cancel_on": { "condition" } - the engine ----------------------------------------------------------------------------------------

TUTORIAL_PASS1022_TEST(FTutorialCancelOnConditionSchemaTest, "CancelOnCondition.Schema")
bool FTutorialCancelOnConditionSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(TEXT("cancel_on with a condition parses"), TutorialRules::ParseSteps(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R" } ],
		"steps": [ { "id": "s", "text_id": "T", "complete": { "timeout_s": 5 },
		"cancel_on": { "condition": [ { "stat": "all_dead.Z", "at_least": 1 }, { "stat": "in_fight", "below": 1 } ] } } ] })JSON"), Data, Warnings, Error));
	const FTutorialStep* Step = Data.FindStep(TEXT("s"));
	TestTrue(TEXT("... two clauses, the cancel is set"), Step && Step->CancelOn.Conditions.Num() == 2 && Step->CancelOn.IsSet()
		&& Step->CancelOn.Conditions[0].Stat == FName(TEXT("all_dead.Z")) && Step->CancelOn.Conditions[0].bAtLeast);
	TestEqual(TEXT("... no warning"), Warnings.Num(), 0);

	TestTrue(TEXT("all_dead.<binding> / tactical_pause are known stats"), TutorialRules::IsKnownStat(TEXT("all_dead.A4_LURE"))
		&& TutorialRules::IsKnownStat(TEXT("tactical_pause")) && !TutorialRules::IsKnownStat(TEXT("all_dead.")));
	TestEqual(TEXT("ParseAllDeadStat"), TutorialRules::ParseAllDeadStat(TEXT("all_dead.A4_PERIMETER")), FName(TEXT("A4_PERIMETER")));
	TestEqual(TEXT("ParseAllDeadStat: another stat"), TutorialRules::ParseAllDeadStat(TEXT("squad_passive")), FName(NAME_None));
	TestTrue(TEXT("step_shown.<step> is a known stat"), TutorialRules::IsKnownStat(TEXT("step_shown.A4_lure"))
		&& TutorialRules::ParseStepShownStat(TEXT("step_shown.A4_lure")) == FName(TEXT("A4_lure")));
	{
		FTutorialStepsData NoStep;
		TestFalse(TEXT("step_shown naming no step: an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
			"complete": { "timeout_s": 5 }, "condition": { "stat": "step_shown.nope", "at_least": 1 } } ] })JSON"), NoStep, Warnings, Error));
	}
	// step_shown is the machine's own: 1 once that step was on screen, never for one skipped before it showed.
	{
		FTutorialStepsData Shown;
		TestTrue(TEXT("a step_shown file parses"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [
			{ "id": "a", "text_id": "T", "trigger": { "type": "manual" }, "complete": { "commands": ["Order.Move"] } },
			{ "id": "b", "text_id": "T", "trigger": { "type": "manual" }, "complete": { "commands": ["Order.Move"] } },
			{ "id": "line", "kind": "feed", "text_id": "T", "trigger": { "type": "event", "event": "X" },
			  "condition": { "stat": "step_shown.a", "at_least": 1 }, "once": false } ] })JSON"), Shown, Warnings, Error));
		FTutorialStateMachine Machine;
		Machine.Load(Shown);
		Machine.TriggerStep(TEXT("a"));
		Machine.Tick(0.1f);
		Machine.HandleCommand(PlayerCommands::Move);
		Machine.HandleEvent(TEXT("X"));
		Machine.Tick(0.1f);
		TestEqual(TEXT("'a' was shown: the line plays"), Machine.GetState(TEXT("line")), ETutorialStepState::Active);
		FTutorialStateMachine Silent;
		Silent.Load(Shown);
		Silent.TriggerStep(TEXT("b"));
		Silent.Tick(0.1f);
		Silent.TriggerStep(TEXT("a")); // queued behind "b" ...
		Silent.SkipStep(TEXT("a")); // ... and dropped before it showed
		Silent.HandleEvent(TEXT("X"));
		Silent.Tick(0.1f);
		TestEqual(TEXT("'a' never showed: the line is skipped"), Silent.GetState(TEXT("line")), ETutorialStepState::Skipped);
	}

	FTutorialStepsData Bad;
	TestFalse(TEXT("all_dead naming no binding: an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
		"complete": { "timeout_s": 5 }, "cancel_on": { "condition": { "stat": "all_dead.NOPE", "at_least": 1 } } } ] })JSON"), Bad, Warnings, Error));
	TestTrue(TEXT("... it says so"), Error.Contains(TEXT("all_dead.NOPE")));
	TestFalse(TEXT("an unknown stat in cancel_on: an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T",
		"complete": { "timeout_s": 5 }, "cancel_on": { "condition": { "stat": "zombies", "at_least": 1 } } } ] })JSON"), Bad, Warnings, Error));
	return true;
}

TUTORIAL_PASS1022_TEST(FTutorialCancelOnConditionMachineTest, "CancelOnCondition.Machine")
bool FTutorialCancelOnConditionMachineTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	const bool bParsed = TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [
		{ "id": "card", "text_id": "T", "trigger": { "type": "manual" }, "complete": { "commands": ["Order.Move"] },
		  "cancel_on": { "condition": { "stat": "gone", "at_least": 1 } } },
		{ "id": "next", "text_id": "T", "trigger": { "type": "after_step", "step": "card" }, "complete": { "commands": ["Order.Move"] } } ] })JSON"),
		Data, Warnings, Error);
	// "gone" is no known stat: the file above must fail; the real one uses quest_progress.
	TestFalse(TEXT("(an unknown stat is refused)"), bParsed);
	TestTrue(TEXT("a small file parses"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [
		{ "id": "card", "text_id": "T", "trigger": { "type": "manual" }, "complete": { "commands": ["Order.Move"] },
		  "cancel_on": { "condition": { "stat": "quest_progress", "at_least": 1 } } },
		{ "id": "next", "text_id": "T", "trigger": { "type": "after_step", "step": "card" }, "complete": { "commands": ["Order.Move"] } } ] })JSON"),
		Data, Warnings, Error));

	float Gone = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Gone](FName Stat) { return Stat == FName(TEXT("quest_progress")) ? Gone : 0.f; };
	Machine.Load(Data);
	Machine.TriggerStep(TEXT("card"));
	TutorialPass1022::Run(Machine, 2);
	TestEqual(TEXT("the card shows"), Machine.GetState(TEXT("card")), ETutorialStepState::Active);
	Machine.ConsumeTransitions();
	Gone = 1.f;
	Machine.Tick(0.1f);
	TestEqual(TEXT("its moment is over: cancelled at once (Skipped)"), Machine.GetState(TEXT("card")), ETutorialStepState::Skipped);
	bool bCancelledFlag = false;
	for (const FTutorialTransition& Transition : Machine.ConsumeTransitions())
	{
		bCancelledFlag |= Transition.StepId == FName(TEXT("card")) && Transition.bCancelled;
	}
	TestTrue(TEXT("... flagged as cancelled"), bCancelledFlag);
	TutorialPass1022::Run(Machine, 4);
	TestEqual(TEXT("its after_step follower still fires"), Machine.GetState(TEXT("next")), ETutorialStepState::Active);

	// Already true when it becomes due: never shown.
	FTutorialStateMachine Late;
	Late.StatProvider = [](FName) { return 1.f; };
	Late.Load(Data);
	Late.TriggerStep(TEXT("card"));
	TutorialPass1022::Run(Late, 2);
	TestEqual(TEXT("over before it was due: cancelled, never shown"), Late.GetState(TEXT("card")), ETutorialStepState::Skipped);
	return true;
}

// --- (5) the A4 steps bound to a dead zombie close; the horde prep names the traps ------------------------------------------------------

TUTORIAL_PASS1022_TEST(FTutorialBothZombiesDeadEarlyTest, "AmbushLesson.BothZombiesDeadEarly")
bool FTutorialBothZombiesDeadEarlyTest::RunTest(const FString&)
{
	using namespace TutorialPass1022;
	FTutorialStepsData Data;
	if (!LoadDemoSteps(*this, Data))
	{
		return false;
	}
	for (const TCHAR* Id : BoundSteps)
	{
		const FTutorialStep* Step = Data.FindStep(Id);
		const bool bCancelOnDeath = Step && Step->CancelOn.Conditions.ContainsByPredicate([](const FTutorialCondition& Clause)
		{
			return !TutorialRules::ParseAllDeadStat(Clause.Stat).IsNone() && Clause.bAtLeast && Clause.Value >= 1.f;
		});
		TestTrue(FString::Printf(TEXT("%s is dropped once its zombie is dead (cancel_on all_dead.*)"), Id), bCancelOnDeath);
	}

	// Both zombies killed before the overlook (from afar, by a mine, ...): no wire card, no target on the loop, no part 2.
	{
		TMap<FName, float> Stats;
		Stats.Add(TEXT("all_dead.A4_PERIMETER"), 1.f);
		Stats.Add(TEXT("all_dead.A4_LURE"), 1.f);
		Stats.Add(TEXT("all_dead.A4_ANY"), 1.f);
		FTutorialStateMachine Machine;
		InstallZombies(Machine);
		Machine.StatProvider = [&Stats](FName Stat) { const float* Value = Stats.Find(Stat); return Value ? *Value : 0.f; };
		Machine.SpotProvider = [](FName, FName) { return true; };
		Machine.VolumeProvider = [](FName VolumeId) { return VolumeId == FName(TEXT("VOL_A4_OVERLOOK")); };
		Machine.Load(Data);
		Machine.RegisterVolumeBeat(TEXT("VOL_A4_OVERLOOK"), TEXT("A4"));
		ToTheOverlook(Machine);
		Run(Machine, 12);
		for (const TCHAR* Id : { TEXT("A4_tripwire"), TEXT("A4_trap_wait"), TEXT("A4_hug_wall"), TEXT("A4_round_corner"), TEXT("A4_lure") })
		{
			TestEqual(FString::Printf(TEXT("both dead early: %s never shows (skipped)"), Id), Machine.GetState(Id), ETutorialStepState::Skipped);
		}
		TestNotEqual(TEXT("... no card in hand"), Machine.GetActiveCard(), FName(TEXT("A4_tripwire")));
		TestFalse(TEXT("... the squad is not held at the overlook"), Machine.IsConfined());
		const FTutorialGuide Guide = Machine.GetGuide();
		TestFalse(TEXT("... the target is no spot on a dead zombie's loop"), Guide.Kind == ETutorialGuideKind::Spot);
	}

	// Both die at the same moment while the wire card is up (a grenade): the whole chain goes at once - and the far zombie's kill does
	// NOT say "not up close" (the lure lesson was never on screen: stat step_shown.A4_lure).
	{
		TMap<FName, float> Stats;
		FTutorialStateMachine Machine;
		InstallZombies(Machine);
		Machine.StatProvider = [&Stats](FName Stat) { const float* Value = Stats.Find(Stat); return Value ? *Value : 0.f; };
		Machine.Load(Data);
		ToTheOverlook(Machine);
		TestEqual(TEXT("at once: the wire card is up"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
		Stats.Add(TEXT("all_dead.A4_PERIMETER"), 1.f);
		Stats.Add(TEXT("all_dead.A4_LURE"), 1.f);
		Stats.Add(TEXT("all_dead.A4_ANY"), 1.f);
		Machine.Tick(0.1f);
		TestEqual(TEXT("at once: the lure card was dropped before it showed"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Skipped);
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z1"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z1"));
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Run(Machine, 4);
		TestFalse(TEXT("at once: no 'not up close' line"), Machine.GetState(TEXT("A4_close_shot_far")) == ETutorialStepState::Active
			|| Machine.GetState(TEXT("A4_close_shot_far")) == ETutorialStepState::Completed);
		TestFalse(TEXT("at once: the hold is released"), Machine.IsConfined());
	}

	// Both killed WHILE the wire card is up (a skirmish the squad won without being spotted first): the card and its target go.
	{
		TMap<FName, float> Stats;
		FTutorialStateMachine Machine;
		InstallZombies(Machine);
		Machine.StatProvider = [&Stats](FName Stat) { const float* Value = Stats.Find(Stat); return Value ? *Value : 0.f; };
		Machine.SpotProvider = [](FName, FName) { return true; };
		Machine.FocusSubjectProvider = [](FName) { return FName(TEXT("Medic")); };
		Machine.Load(Data);
		ToTheOverlook(Machine);
		TestEqual(TEXT("the wire card is up"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
		TestEqual(TEXT("... its target: the perimeter zombie's loop"), Machine.GetGuide().Id, FName(TEXT("@patrol_route.A4_PERIMETER")));
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z1"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z1"));
		Stats.Add(TEXT("all_dead.A4_PERIMETER"), 1.f);
		Machine.Tick(0.1f);
		TestEqual(TEXT("the perimeter zombie dead: the wire card closes"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Skipped);
		Run(Machine);
		TestEqual(TEXT("... the wait objective never comes up"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... part 2 goes on (the far zombie lives): the wall card"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
		TestTrue(TEXT("... still held for her lesson"), Machine.IsConfined());
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Stats.Add(TEXT("all_dead.A4_LURE"), 1.f);
		Stats.Add(TEXT("all_dead.A4_ANY"), 1.f);
		Run(Machine, 12);
		for (const TCHAR* Id : { TEXT("A4_hug_wall"), TEXT("A4_round_corner"), TEXT("A4_lure") })
		{
			TestEqual(FString::Printf(TEXT("both dead: %s closed"), Id), Machine.GetState(Id), ETutorialStepState::Skipped);
		}
		TestEqual(TEXT("... no close-shot card ever"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Idle);
		TestFalse(TEXT("... the hold is released"), Machine.IsConfined());
		const FName Active = Machine.GetActiveCard();
		TestTrue(FString::Printf(TEXT("... no A4 zombie card in hand (%s)"), *Active.ToString()), Active.IsNone() || !FString(Active.ToString()).StartsWith(TEXT("A4_")));
		TestFalse(TEXT("... no target on a dead zombie"), Machine.GetGuide().Kind == ETutorialGuideKind::Spot);
	}
	return true;
}

TUTORIAL_PASS1022_TEST(FTutorialHordePrepTrapsTest, "QuestLesson.PrepareTheGround")
bool FTutorialHordePrepTrapsTest::RunTest(const FString&)
{
	using namespace TutorialPass1022;
	FTutorialStepsData Data;
	if (!LoadDemoSteps(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Card = Data.FindStep(TEXT("A7_traps"));
	TestTrue(TEXT("A7_traps: a card of A7 after the console card, only before the console, never precredited"), Card && Card->Beat == FName(TEXT("A7"))
		&& Card->Kind == ETutorialStepKind::HintCard && Card->Trigger.Type == ETutorialTriggerType::AfterStep && Card->Trigger.Step == FName(TEXT("A7_prep"))
		&& !Card->bPrecredit && Card->Complete.Commands.Contains(PlayerCommands::DeployTripwire.ToString())
		&& Card->Complete.Commands.Contains(PlayerCommands::DeployMine.ToString()));

	float Progress = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Progress](FName Stat) { return Stat == FName(TEXT("quest_progress")) ? Progress : 0.f; };
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A7"));
	Run(Machine);
	for (const TCHAR* Kind : { TEXT("CanisterTaken"), TEXT("FuelDrained"), TEXT("GeneratorStarted") })
	{
		Progress += 1.f;
		Machine.HandleEvent(FName(*(FString(TEXT("QuestStep.")) + Kind)));
		Machine.HandleEvent(TEXT("QuestStep"));
		Run(Machine);
	}
	Machine.CompleteStep(TEXT("A7_objective"));
	Run(Machine);
	TestEqual(TEXT("the generator runs: the console card"), Machine.GetState(TEXT("A7_prep")), ETutorialStepState::Active);
	TestNotEqual(TEXT("... the traps card waits behind it"), Machine.GetState(TEXT("A7_traps")), ETutorialStepState::Active);
	Machine.SkipStep(TEXT("A7_prep"));
	Run(Machine);
	TestEqual(TEXT("then: PREPARE THE GROUND"), Machine.GetState(TEXT("A7_traps")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::DeployTripwire);
	TestEqual(TEXT("a wire set: done"), Machine.GetState(TEXT("A7_traps")), ETutorialStepState::Completed);

	// The console powered before it came up: never shown.
	float Done = 4.f;
	FTutorialStateMachine Late;
	Late.StatProvider = [&Done](FName Stat) { return Stat == FName(TEXT("quest_progress")) ? Done : 0.f; };
	Late.Load(Data);
	Late.StartBeat(TEXT("A0"));
	Late.StartBeat(TEXT("A7"));
	Late.TriggerStep(TEXT("A7_traps"));
	Run(Late);
	TestEqual(TEXT("the console already powered: the card is skipped"), Late.GetState(TEXT("A7_traps")), ETutorialStepState::Skipped);
	return true;
}

// --- (3) DEFENSIVE instead of PASSIVE ---------------------------------------------------------------------------------------------------

TUTORIAL_PASS1022_TEST(FTutorialDefensiveLureTest, "AmbushLesson.DefensiveKeepsTheLure")
bool FTutorialDefensiveLureTest::RunTest(const FString&)
{
	using namespace TutorialPass1022;
	FTutorialStepsData Data;
	if (!LoadDemoSteps(*this, Data))
	{
		return false;
	}
	for (const FTutorialStep& Step : Data.Steps)
	{
		for (const FTutorialAction& Action : Step.Actions)
		{
			TestFalse(FString::Printf(TEXT("%s: no step sets the squad PASSIVE any more"), *Step.Id.ToString()),
				Action.Type == FName(TEXT("set_posture")) && Action.Posture.Equals(TEXT("passive"), ESearchCase::IgnoreCase));
		}
		TestFalse(FString::Printf(TEXT("%s: no step asks for Posture.Passive"), *Step.Id.ToString()),
			Step.Complete.Commands.Contains(PlayerCommands::PosturePassive.ToString()));
	}
	// The existing Defensive semantics keep the lure working: never the first shot at an enemy that has not spotted the squad.
	const FFirePostureConfig Config;
	TestFalse(TEXT("DEFENSIVE never opens fire while exploring"), FirePostureRules::MayAutoFireInExploration(ESquadFirePosture::Defensive, true,
		ECodexGamePhase::Exploration, false, Config));
	TestFalse(TEXT("DEFENSIVE does not shoot the lured zombie walking up unaware"), FirePostureRules::MayAutoFireAt(ESquadFirePosture::Defensive,
		false, false, /*bTargetHasDetectedSquad*/ false, Config));
	TestTrue(TEXT("... but fires back once it has spotted them"), FirePostureRules::MayAutoFireAt(ESquadFirePosture::Defensive, false, false, true, Config));
	TestTrue(TEXT("... and when attacked"), FirePostureRules::MayAutoFireAt(ESquadFirePosture::Defensive, true, false, false, Config));
	return true;
}

// --- (4) the tripwire card: beat-less, never precredited --------------------------------------------------------------------------------

TUTORIAL_PASS1022_TEST(FTutorialWireCardShownTest, "WireLesson.ShownAfterTheCrawl")
bool FTutorialWireCardShownTest::RunTest(const FString&)
{
	using namespace TutorialPass1022;
	FTutorialStepsData Data;
	if (!LoadDemoSteps(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Card = Data.FindStep(TEXT("A3_wire"));
	TestTrue(TEXT("A3_wire: beat-less, on TripwireSpotted, never precredited, done on a disarm"), Card && Card->Beat.IsNone() && !Card->bPrecredit
		&& Card->Trigger.Event == GameplayEvents::TripwireSpotted.ToString() && Card->Complete.Event == GameplayEvents::TripwireDisarmed.ToString()
		&& !Card->Complete.Commands.Contains(PlayerCommands::StanceProne.ToString()));
	const FTutorialStep* Feed = Data.FindStep(TEXT("A3_wire_spotted"));
	TestTrue(TEXT("its feed line is beat-less too"), Feed && Feed->Beat.IsNone());

	// The fence crawl (prone x3) put Stance.Prone into the history - the old card was precredited by it and never shown.
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A4"));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Machine.HandleCommand(PlayerCommands::StanceProne);
	}
	Run(Machine);
	for (int32 Guard = 0; Guard < 12 && !Machine.GetActiveCard().IsNone(); ++Guard)
	{
		Machine.SkipStep(Machine.GetActiveCard()); // the overlook's own cards
		Run(Machine, 2);
	}
	Machine.HandleEvent(GameplayEvents::TripwireSpotted, TEXT("Op3"));
	Run(Machine, 2);
	TestEqual(TEXT("a wire met between the buildings in A4: the card SHOWS (not precredited by the crawl)"), Machine.GetState(TEXT("A3_wire")),
		ETutorialStepState::Active);
	TestEqual(TEXT("... the beat stays A4 (a beat-less card never moves the beat)"), Machine.GetCurrentBeat(), FName(TEXT("A4")));
	Machine.HandleCommand(PlayerCommands::StanceProne);
	TestEqual(TEXT("... going prone does not end it"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Active);
	Machine.HandleEvent(GameplayEvents::TripwireDisarmed, TEXT("Op3"));
	TestEqual(TEXT("... disarmed: done"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Completed);

	// Met in A1 (before the approach): shown there, and it does not start A3.
	FTutorialStateMachine Early;
	Early.Load(Data);
	Early.StartBeat(TEXT("A0"));
	Early.StartBeat(TEXT("A1"));
	Early.HandleEvent(GameplayEvents::TripwireSpotted, TEXT("Op1"));
	Run(Early, 2);
	TestEqual(TEXT("a wire in A1: still A1"), Early.GetCurrentBeat(), FName(TEXT("A1")));
	TestTrue(TEXT("... the card is running"), TutorialRules::IsRunning(Early.GetState(TEXT("A3_wire"))));
	return true;
}

// --- (1) the tactical pause lesson -----------------------------------------------------------------------------------------------------

TUTORIAL_PASS1022_TEST(FTutorialPauseLessonTest, "PauseLesson.TwoCards")
bool FTutorialPauseLessonTest::RunTest(const FString&)
{
	using namespace TutorialPass1022;
	FTutorialStepsData Data;
	if (!LoadDemoSteps(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Orders = Data.FindStep(TEXT("X_pause"));
	const FTutorialStep* Resume = Data.FindStep(TEXT("X_pause_resume"));
	TestTrue(TEXT("card 1: beat-less, the first TacticalPauseStarted, dropped when the pause ends, two orders"), Orders && Orders->Beat.IsNone()
		&& Orders->Trigger.Type == ETutorialTriggerType::Event && Orders->Trigger.Event == GameplayEvents::TacticalPauseStarted.ToString()
		&& Orders->CancelOn.Event == GameplayEvents::TacticalPauseEnded.ToString() && Orders->Complete.Count == 2 && !Orders->bPrecredit && Orders->bOnce);
	TestTrue(TEXT("card 2: after card 1, only while paused, done on the resume"), Resume && Resume->Beat.IsNone()
		&& Resume->Trigger.Type == ETutorialTriggerType::AfterStep && Resume->Trigger.Step == FName(TEXT("X_pause"))
		&& Resume->Complete.Commands.Contains(PlayerCommands::PauseRelease.ToString()) && !Resume->bPrecredit);

	float Paused = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Paused](FName Stat) { return Stat == FName(TEXT("tactical_pause")) ? Paused : 0.f; };
	Machine.Load(Data);
	Machine.StartBeat(TEXT("A0"));
	Machine.StartBeat(TEXT("A8"));
	Run(Machine);
	Machine.CompleteStep(TEXT("A8_radio_horde"));
	Run(Machine);
	TestEqual(TEXT("nothing before a pause"), Machine.GetState(TEXT("X_pause")), ETutorialStepState::Idle);
	Paused = 1.f;
	Machine.HandleEvent(TEXT("TacticalPauseStarted.Free"));
	Machine.HandleEvent(GameplayEvents::TacticalPauseStarted);
	Run(Machine, 2);
	TestEqual(TEXT("the horde's free pause: card 1 (time stops, orders)"), Machine.GetState(TEXT("X_pause")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::Move);
	TestEqual(TEXT("one order: still up"), Machine.GetState(TEXT("X_pause")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::TargetedShot);
	TestEqual(TEXT("a second order: done"), Machine.GetState(TEXT("X_pause")), ETutorialStepState::Completed);
	Run(Machine, 4);
	TestEqual(TEXT("card 2 (resume, the limits)"), Machine.GetState(TEXT("X_pause_resume")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PauseRelease);
	TestEqual(TEXT("Space: done"), Machine.GetState(TEXT("X_pause_resume")), ETutorialStepState::Completed);
	Paused = 0.f;
	Machine.HandleEvent(GameplayEvents::TacticalPauseEnded);
	Paused = 1.f;
	Machine.HandleEvent(GameplayEvents::TacticalPauseStarted);
	Run(Machine, 4);
	TestEqual(TEXT("the next pause: no second lesson"), Machine.GetState(TEXT("X_pause")), ETutorialStepState::Completed);

	// Resumed before giving orders: card 1 dropped, card 2 never shows (the pause is over).
	float Paused2 = 1.f;
	FTutorialStateMachine Quick;
	Quick.StatProvider = [&Paused2](FName Stat) { return Stat == FName(TEXT("tactical_pause")) ? Paused2 : 0.f; };
	Quick.Load(Data);
	Quick.StartBeat(TEXT("A0"));
	Quick.StartBeat(TEXT("A8"));
	Quick.HandleEvent(GameplayEvents::TacticalPauseStarted);
	Run(Quick, 2);
	TestEqual(TEXT("card 1 up"), Quick.GetState(TEXT("X_pause")), ETutorialStepState::Active);
	Paused2 = 0.f;
	Quick.HandleEvent(GameplayEvents::TacticalPauseEnded);
	Run(Quick, 4);
	TestEqual(TEXT("resumed at once: card 1 dropped"), Quick.GetState(TEXT("X_pause")), ETutorialStepState::Skipped);
	TestEqual(TEXT("... card 2 skipped (no pause any more)"), Quick.GetState(TEXT("X_pause_resume")), ETutorialStepState::Skipped);

	// The card texts name the fight's real limits.
	TestEqual(TEXT("pause tokens"), TutorialRules::ResolvePauseTokens(TEXT("{pause_charges} x {pause_seconds} s, back in {pause_cooldown} s"), 3, 30.f, 20.f),
		FString(TEXT("3 x 30 s, back in 20 s")));
	TestEqual(TEXT("no token: unchanged"), TutorialRules::ResolvePauseTokens(TEXT("Space"), 3, 30.f, 20.f), FString(TEXT("Space")));
	return true;
}

// --- (2) a failed defuse: the click right at the trap is the trap again ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrapApproachRetryClickTest, "CodexTactics.Interactables.TrapApproach.RetryClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTrapApproachRetryClickTest::RunTest(const FString&)
{
	TestTrue(TEXT("at the spot, a click on the mine under his own body: the mine"), TrapApproachRules::IsRetryClick(true, 10.f, false));
	TestTrue(TEXT("at the spot, a click on the ground 1 m from the wire: the wire"), TrapApproachRules::IsRetryClick(true, 100.f, false));
	TestFalse(TEXT("1.5 m away from the trap: a normal click"), TrapApproachRules::IsRetryClick(true, 150.f, false));
	TestFalse(TEXT("he walked off the spot: a normal click (selects / moves)"), TrapApproachRules::IsRetryClick(false, 10.f, false));
	TestFalse(TEXT("the click picks ANOTHER operative: that selection wins"), TrapApproachRules::IsRetryClick(true, 10.f, true));
	TestFalse(TEXT("no distance: nothing"), TrapApproachRules::IsRetryClick(true, -1.f, false));
	TestEqual(TEXT("the radius matches the click-select radius (1.2 m)"), TrapApproachRules::RetryClickRadiusCm, 120.f);
	return true;
}

#undef TUTORIAL_PASS1022_TEST

#endif
