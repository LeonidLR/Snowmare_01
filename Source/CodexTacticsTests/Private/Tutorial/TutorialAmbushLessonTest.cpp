#include "Misc/AutomationTest.h"
#include "Characters/FirePostureRules.h"
#include "Core/PlayerCommands.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01, game designer play-test 2026-10-19 (beat sheet decision block "2026-10-19 b"): the A3 -> A4 lessons on the real
// demo_01_steps.json - the posture lesson right after the trap disarm (with its A4 twin when the wire was never spotted), the
// squad entering the complex Defensive ("set_posture" action), the ambush in two parts (a patrol walks into the wire, the other
// one is lured: whistle / stone). 2026-10-21: each part bound to ITS zombie ("enemies" bindings): part 1 the zombie on the loop round
// the near container walks into the wire, part 2 the zombie by the far container is lured into the open and shot at close range.

#define TUTORIAL_AMBUSH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.AmbushLesson." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialAmbushTestData
{
	bool LoadDemo(FAutomationTestBase& Test, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("demo steps load (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			Test.AddWarning(Warning);
		}
		return bOk;
	}

	/** Ticks the machine long enough for delays, breathers and short speech lines (lines run in the runtime, not here). */
	void Settle(FTutorialStateMachine& Machine, int32 Ticks = 8)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Machine.Tick(0.5f);
		}
	}

	/** A0 -> A3 with the cover field lesson done (the posture lesson "requires" A3_cover). */
	void IntoTheField(FTutorialStateMachine& Machine)
	{
		Machine.StartBeat(TEXT("A0"));
		Machine.StartBeat(TEXT("A3"));
		Machine.TriggerStep(TEXT("A3_cover"));
		Machine.SkipStep(TEXT("A3_cover"));
		Settle(Machine);
		Machine.SkipStep(TEXT("A3_peek")); // the corner peek card follows the cover card
		Settle(Machine);
	}

	bool PostureFree(const FTutorialStateMachine& Machine)
	{
		return !Machine.IsCommandLocked(PlayerCommands::PosturePassive) && !Machine.IsCommandLocked(PlayerCommands::PostureDefensive)
			&& !Machine.IsCommandLocked(PlayerCommands::PostureAggressive);
	}
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureSchemaTest, "PostureSchema")
bool FTutorialAmbushPostureSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialAmbushTestData::LoadDemo(*this, Data))
	{
		return false;
	}
	// The squad enters the complex Defensive (the A3 approach lines carry the action).
	const FTutorialStep* Approach = Data.FindStep(TEXT("A3_wt_approach"));
	TestTrue(TEXT("A3_wt_approach sets the squad DEFENSIVE"), Approach && Approach->Actions.ContainsByPredicate([](const FTutorialAction& Action)
	{
		ESquadFirePosture Posture = ESquadFirePosture::Aggressive;
		return Action.Type == FName(TEXT("set_posture")) && FirePostureRules::ParseName(Action.Posture, Posture) && Posture == ESquadFirePosture::Defensive;
	}));
	for (const TCHAR* Id : { TEXT("A3_posture"), TEXT("A4_posture") })
	{
		const FTutorialStep* Card = Data.FindStep(Id);
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), Id), Card))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: a card with the three posture keys"), Id), Card->Kind == ETutorialStepKind::HintCard
			&& Card->Keys.Contains(PlayerCommands::PosturePassive.ToString()) && Card->Keys.Contains(PlayerCommands::PostureDefensive.ToString())
			&& Card->Keys.Contains(PlayerCommands::PostureAggressive.ToString()));
		TestTrue(FString::Printf(TEXT("%s: unlocks Posture.*, done on Posture.Passive, never precredited"), Id), Card->Unlock.Contains(TEXT("Posture.*"))
			&& Card->Complete.Commands.Contains(PlayerCommands::PosturePassive.ToString()) && !Card->bPrecredit);
	}
	const FTutorialStep* A3Card = Data.FindStep(TEXT("A3_posture"));
	TestTrue(TEXT("A3_posture: beat-less (never dropped by the overlook), after the wire lesson, requires the cover field"), A3Card && A3Card->Beat.IsNone()
		&& A3Card->Trigger.Type == ETutorialTriggerType::AfterStep && A3Card->Trigger.Step == FName(TEXT("A3_wire"))
		&& A3Card->Trigger.Requires.Contains(TEXT("A3_cover")) && A3Card->CancelOn.Steps.Contains(TEXT("A4_posture")));
	const FTutorialStep* A4Card = Data.FindStep(TEXT("A4_posture"));
	TestTrue(TEXT("A4_posture: the overlook twin, cancelled by the A3 card"), A4Card && A4Card->Beat == FName(TEXT("A4"))
		&& A4Card->Trigger.Type == ETutorialTriggerType::BeatStart && A4Card->CancelOn.Steps.Contains(TEXT("A3_posture")));
	const FTutorialStep* Radio = Data.FindStep(TEXT("A3_posture_radio"));
	TestTrue(TEXT("the Commander's 'Hold your fire' radio"), Radio && Radio->Kind == ETutorialStepKind::Radio && Radio->Lines.Num() == 1
		&& Radio->Lines[0].Speaker == TEXT("Commander") && Radio->Lines[0].TextId == FName(TEXT("WT_A3_POSTURE")));

	// The action is validated.
	FTutorialStepsData Bad;
	TArray<FString> Warnings;
	FString Error;
	TestFalse(TEXT("set_posture with an unknown posture fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "kind": "walk_talk",
		"trigger": { "type": "manual" }, "action": { "type": "set_posture", "posture": "berserk" }, "lines": [ { "text_id": "X" } ] } ] })JSON"), Bad, Warnings, Error));
	TestTrue(TEXT("set_posture is a known action"), TutorialRules::IsKnownAction(TEXT("set_posture")));
	return true;
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureAfterDisarmTest, "PostureAfterDisarm")
bool FTutorialAmbushPostureAfterDisarmTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	IntoTheField(Machine);
	TestFalse(TEXT("the posture keys are still locked in the field"), PostureFree(Machine));
	// The wire at the field exit: spotted -> the disarm card -> disarmed.
	Machine.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op3"));
	Settle(Machine);
	TestEqual(TEXT("the wire card is up"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Active);
	TestEqual(TEXT("no posture card before the disarm lesson ends"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Idle);
	Machine.HandleCommand(PlayerCommands::DisarmTripwire);
	TestEqual(TEXT("disarm: the wire lesson is done"), Machine.GetState(TEXT("A3_wire")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("right after it: the posture card"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("the posture radio runs with it"), TutorialRules::IsRunning(Machine.GetState(TEXT("A3_posture_radio"))));
	TestTrue(TEXT("the card unlocks , . /"), PostureFree(Machine));
	// The overlook comes before the player pressed it: the beat-less card stays up, its twin waits behind it.
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	TestEqual(TEXT("A4 start: the A3 posture card is NOT dropped"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestNotEqual(TEXT("... its A4 twin is not shown"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PostureDefensive);
	TestEqual(TEXT("Defensive is not what it asks for"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	TestEqual(TEXT("Posture.Passive completes it"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("the A4 twin is cancelled (never shown)"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Skipped);
	// The radio lines are played by the runtime (here: finished by hand) - the twin radio is dropped when they are done.
	TestTrue(TEXT("the A3 radio finishes"), Machine.CompleteStep(TEXT("A3_posture_radio")));
	Settle(Machine);
	TestEqual(TEXT("the A4 twin radio is dropped (never played)"), Machine.GetState(TEXT("A4_posture_radio")), ETutorialStepState::Skipped);
	return true;
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushPostureFallbackTest, "PostureAtTheOverlookWithoutTheWire")
bool FTutorialAmbushPostureFallbackTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	// The wire never spotted (gone round it / tripped it): the lesson comes at the overlook.
	FTutorialStateMachine Machine;
	Machine.Load(Data);
	IntoTheField(Machine);
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	TestEqual(TEXT("the cone card first"), Machine.GetState(TEXT("A4_cones")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Machine);
	TestEqual(TEXT("then the posture card at the overlook"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("it unlocks , . /"), PostureFree(Machine));
	TestNotEqual(TEXT("the tripwire card waits behind it"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	TestEqual(TEXT("done on Posture.Passive"), Machine.GetState(TEXT("A4_posture")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("the tripwire card follows"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
	// The wire spotted later after all: no second posture lesson.
	Machine.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op3"));
	Machine.SkipStep(TEXT("A3_wire"));
	Settle(Machine);
	TestNotEqual(TEXT("no second posture card"), Machine.GetState(TEXT("A3_posture")), ETutorialStepState::Active);
	TestTrue(TEXT("the A3 card is cancelled or never armed"), Machine.GetState(TEXT("A3_posture")) == ETutorialStepState::Skipped
		|| Machine.GetState(TEXT("A3_posture")) == ETutorialStepState::Idle);

	// A wire spotted before the cover field (the player wandered off the route) does not arm it.
	FTutorialStateMachine Early;
	Early.Load(Data);
	Early.StartBeat(TEXT("A0"));
	Early.StartBeat(TEXT("A2"));
	Early.HandleEvent(TEXT("TripwireSpotted"), TEXT("Op1"));
	Settle(Early);
	Early.SkipStep(TEXT("A3_wire"));
	Settle(Early);
	TestEqual(TEXT("an early wire: no posture lesson (requires A3_cover)"), Early.GetState(TEXT("A3_posture")), ETutorialStepState::Idle);
	TestFalse(TEXT("... and the keys stay locked"), PostureFree(Early));
	return true;
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushLureSchemaTest, "LureSchema")
bool FTutorialAmbushLureSchemaTest::RunTest(const FString&)
{
	FTutorialStepsData Data;
	if (!TutorialAmbushTestData::LoadDemo(*this, Data))
	{
		return false;
	}
	// The events the steps wait for are the ones the game raises.
	TestEqual(TEXT("an enemy pulled the pin"), GameplayEvents::TripwireTriggeredByEnemy, FName(TEXT("TripwireTriggered.Enemy")));
	TestEqual(TEXT("a lure was heard"), GameplayEvents::LureHeard, FName(TEXT("StealthLure.Heard")));
	TestEqual(TEXT("an enemy died"), GameplayEvents::EnemyKilled, FName(TEXT("EnemyKilled")));
	// 2026-10-21: two parts, each bound to ITS zombie by its patrol route (the user's layout: no tag / map change).
	const FTutorialEnemyBinding* Perimeter = Data.FindEnemy(TEXT("A4_PERIMETER"));
	const FTutorialEnemyBinding* LureZombie = Data.FindEnemy(TEXT("A4_LURE"));
	TestTrue(TEXT("part 1's zombie: the loop round the near container (route PatrolRoute_A4_Hounds)"), Perimeter
		&& Perimeter->Routes.Contains(TEXT("PatrolRoute_A4_Hounds")) && Perimeter->Actors.IsEmpty());
	TestTrue(TEXT("part 2's zombie: by the far container (route PatrolRoute_A4_Frostbitten)"), LureZombie
		&& LureZombie->Routes.Contains(TEXT("PatrolRoute_A4_Frostbitten")) && LureZombie->Actors.IsEmpty());
	const FTutorialStep* Wire = Data.FindStep(TEXT("A4_tripwire"));
	TestTrue(TEXT("part 1: the wire spot is on the PERIMETER zombie's loop"), Wire && Wire->Guide == FName(TEXT("@patrol_route.A4_PERIMETER")));
	const FTutorialStep* Wait = Data.FindStep(TEXT("A4_trap_wait"));
	TestTrue(TEXT("part 1: the objective after the wire card, done when THE PERIMETER zombie walks into a wire"), Wait && Wait->Kind == ETutorialStepKind::Objective
		&& Wait->Beat == FName(TEXT("A4")) && Wait->Trigger.Type == ETutorialTriggerType::AfterStep && Wait->Trigger.Step == FName(TEXT("A4_tripwire"))
		&& Wait->Complete.Event == GameplayEvents::TripwireTriggeredByEnemy.ToString() && Wait->Complete.EnemyFilter.Enemy == FName(TEXT("A4_PERIMETER"))
		&& Wait->Complete.TimeoutSeconds > 0.f && Wait->CancelOn.Event == GameplayEvents::EnemyKilled.ToString()
		&& Wait->CancelOn.EnemyFilter.Enemy == FName(TEXT("A4_PERIMETER")));
	const FTutorialStep* Line = Data.FindStep(TEXT("A4_wt_trap"));
	TestTrue(TEXT("'that's one' only for the perimeter zombie"), Line && Line->Trigger.EnemyFilter.Enemy == FName(TEXT("A4_PERIMETER")));
	const FTutorialStep* Lure = Data.FindStep(TEXT("A4_lure"));
	TestTrue(TEXT("part 2: the lure card after part 1, keys 5 / 6, unlocks Stealth.*, done when THE FAR zombie heard it"), Lure && Lure->Kind == ETutorialStepKind::HintCard
		&& Lure->Trigger.Type == ETutorialTriggerType::AfterStep && Lure->Trigger.Step == FName(TEXT("A4_round_corner"))
		&& Lure->Keys.Contains(PlayerCommands::StealthWhistle.ToString()) && Lure->Keys.Contains(PlayerCommands::StealthThrow.ToString())
		&& Lure->Unlock.Contains(TEXT("Stealth.*")) && Lure->Complete.Event == GameplayEvents::LureHeard.ToString()
		&& Lure->Complete.EnemyFilter.Enemy == FName(TEXT("A4_LURE")) && !Lure->bPrecredit && Lure->Guide == FName(TEXT("@lure_spot.A4_LURE")));
	// 2026-10-21 b: part 2's approach - cover at the near container (her CoverEntered), then round it in cover (wall-follow / corner moves).
	const FTutorialStep* Hug = Data.FindStep(TEXT("A4_hug_wall"));
	const FTutorialStep* Corner = Data.FindStep(TEXT("A4_round_corner"));
	TestTrue(TEXT("the wall card after part 1: her own cover, never precredited, skipped in a fight, dropped when a zombie engages"), Hug
		&& Hug->Trigger.Type == ETutorialTriggerType::AfterStep && Hug->Trigger.Step == FName(TEXT("A4_trap_wait"))
		&& Hug->Complete.Event == GameplayEvents::CoverEntered.ToString() && Hug->Complete.bFocusSubject && !Hug->bPrecredit
		&& Hug->Conditions.Num() == 1 && Hug->Conditions[0].Stat == FName(TEXT("in_fight")) && Hug->CancelOn.EnemyFilter.Enemy == FName(TEXT("A4_ANY")));
	TestTrue(TEXT("the corner card: the wall-following / corner commands or CornerReached"), Corner && Corner->Trigger.Step == FName(TEXT("A4_hug_wall"))
		&& Corner->Complete.Commands.Contains(PlayerCommands::CoverShimmy.ToString()) && Corner->Complete.Commands.Contains(PlayerCommands::CoverCorner.ToString())
		&& Corner->Complete.Event == GameplayEvents::CornerReached.ToString() && !Corner->bPrecredit);
	const FTutorialStep* Shot = Data.FindStep(TEXT("A4_close_shot"));
	TestTrue(TEXT("part 2: the close shot - THE FAR zombie killed by the squad, an operative within 10 m, 4 m off his route"), Shot
		&& Shot->Kind == ETutorialStepKind::HintCard && Shot->Trigger.Type == ETutorialTriggerType::Event && Shot->Trigger.Event == GameplayEvents::LureHeard.ToString()
		&& Shot->Trigger.EnemyFilter.Enemy == FName(TEXT("A4_LURE")) && Shot->Trigger.Requires.Contains(TEXT("A4_lure"))
		&& Shot->Complete.Event == TEXT("EnemyKilled.Squad") && Shot->Complete.EnemyFilter.Enemy == FName(TEXT("A4_LURE"))
		&& FMath::IsNearlyEqual(Shot->Complete.EnemyFilter.WithinMetres, 10.f) && FMath::IsNearlyEqual(Shot->Complete.EnemyFilter.OffRouteMetres, 4.f)
		&& Shot->Guide == FName(TEXT("@enemy.A4_LURE")) && Shot->CancelOn.Steps.Contains(TEXT("A4_close_shot_far")));
	const FTutorialStep* Far = Data.FindStep(TEXT("A4_close_shot_far"));
	TestTrue(TEXT("the far / trap kill line: any kill of the far zombie, twin of the card"), Far && Far->Kind == ETutorialStepKind::Feed
		&& Far->Trigger.Event == GameplayEvents::EnemyKilled.ToString() && Far->Trigger.EnemyFilter.Enemy == FName(TEXT("A4_LURE"))
		&& Far->CancelOn.Steps.Contains(TEXT("A4_close_shot")));
	TestNull(TEXT("the old AGGRESSIVE reminder is gone"), Data.FindStep(TEXT("A4_lure_ambush")));
	// A0 locks the lure until its lesson (STEALTH_LURE.md §5).
	TestTrue(TEXT("A0 locks Stealth.*"), Data.Beats.Num() > 0 && Data.Beats[0].Lock.Contains(TEXT("Stealth.*")));
	return true;
}

namespace TutorialAmbushTestData
{
	/** A stand-in for the runtime's enemy filter: Z1 walks the perimeter loop, Z2 is the far zombie; their facts are set by the test. */
	struct FZombies
	{
		float NearestCm = 3000.f;
		float OffRouteCm = 0.f;

		void Install(FTutorialStateMachine& Machine)
		{
			Machine.EnemyFilterProvider = [this](const FTutorialEnemyFilter& Filter, FName SubjectId)
			{
				const bool bBound = (SubjectId == FName(TEXT("Z1")) && Filter.Enemy == FName(TEXT("A4_PERIMETER")))
					|| (SubjectId == FName(TEXT("Z2")) && Filter.Enemy == FName(TEXT("A4_LURE")))
					|| ((SubjectId == FName(TEXT("Z1")) || SubjectId == FName(TEXT("Z2"))) && Filter.Enemy == FName(TEXT("A4_ANY")));
				return TutorialRules::PassesEnemyFilter(Filter, bBound, NearestCm, OffRouteCm);
			};
		}
	};

	/** The runtime posts a shown feed line and completes it at once (UTutorialSubsystem); the bare machine leaves it Active. */
	void PlayFeeds(FTutorialStateMachine& Machine)
	{
		for (const FTutorialStep& Step : Machine.GetData().Steps)
		{
			if (Step.Kind == ETutorialStepKind::Feed && Machine.GetState(Step.Id) == ETutorialStepState::Active)
			{
				Machine.CompleteStep(Step.Id);
			}
		}
	}

	/** 2026-10-21: part 2's approach - she takes cover at the near container, then rounds its corner in cover. */
	void PassTheWall(FTutorialStateMachine& Machine)
	{
		Machine.HandleEvent(GameplayEvents::CoverEntered, TEXT("Medic"));
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::CoverShimmy);
		Settle(Machine);
	}

	/** IntoTheField -> A4 -> cones -> posture -> the wire rigged: part 1 waits for the perimeter zombie. */
	void ToTheWait(FTutorialStateMachine& Machine)
	{
		IntoTheField(Machine);
		Machine.StartBeat(TEXT("A4"));
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::PosturePassive); // the overlook posture card (no wire in this run)
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::DeployTripwire);
		Settle(Machine);
	}
}

TUTORIAL_AMBUSH_TEST(FTutorialAmbushLureFlowTest, "TrapThenLure")
bool FTutorialAmbushLureFlowTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	FZombies Zombies;
	FTutorialStateMachine Machine;
	Zombies.Install(Machine);
	Machine.Load(Data);
	IntoTheField(Machine);
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::PosturePassive); // the overlook posture card (no wire in this run)
	Settle(Machine);
	TestEqual(TEXT("the tripwire card"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
	TestTrue(TEXT("the lure is locked before its lesson"), Machine.IsCommandLocked(PlayerCommands::StealthWhistle) && Machine.IsCommandLocked(PlayerCommands::StealthThrow));
	Machine.HandleCommand(PlayerCommands::DeployTripwire);
	Settle(Machine);
	TestEqual(TEXT("part 1: wait for the perimeter zombie"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	Machine.HandleEvent(TEXT("TripwireTriggered"), TEXT("Engineer")); // an operative tripping a wire is no ambush
	TestEqual(TEXT("a wire tripped by the squad does not count"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
	TestEqual(TEXT("the perimeter zombie walked into the wire: part 1 done"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Completed);
	TestTrue(TEXT("the 'that's one' line"), Machine.GetState(TEXT("A4_wt_trap")) != ETutorialStepState::Idle);
	Settle(Machine);
	TestEqual(TEXT("part 2 starts with the wall card"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
	PassTheWall(Machine);
	TestEqual(TEXT("part 2: the lure card"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
	TestFalse(TEXT("... unlocks whistle and throw"), Machine.IsCommandLocked(PlayerCommands::StealthWhistle) || Machine.IsCommandLocked(PlayerCommands::StealthThrow));
	Machine.HandleCommand(PlayerCommands::StealthWhistle);
	Machine.HandleEvent(TEXT("StealthLure.Whistle"), TEXT("Commander"));
	TestEqual(TEXT("a whistle nobody heard does not count"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
	Machine.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
	TestEqual(TEXT("the far zombie heard it and investigates: done"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("the CLOSE SHOT card"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
	Machine.HandleEvent(GameplayEvents::CombatStarted, NAME_None);
	TestEqual(TEXT("the fight starting is not the lesson"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
	Zombies.NearestCm = 700.f;
	Zombies.OffRouteCm = 650.f;
	Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
	Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
	TestEqual(TEXT("shot 7 m from her, 6.5 m off his route: the close shot is done"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Completed);
	Settle(Machine);
	PlayFeeds(Machine);
	TestEqual(TEXT("... and the 'not up close' line never shows"), Machine.GetState(TEXT("A4_close_shot_far")), ETutorialStepState::Skipped);

	// The wire never caught anyone: the lure lesson still follows (the objective times out).
	FTutorialStateMachine Slow;
	Zombies.Install(Slow);
	Slow.Load(Data);
	ToTheWait(Slow);
	for (int32 Tick = 0; Tick < 260; ++Tick)
	{
		Slow.Tick(1.f);
	}
	TestEqual(TEXT("no catch: the wait times out"), Slow.GetState(TEXT("A4_trap_wait")), ETutorialStepState::TimedOut);
	TestTrue(TEXT("... and part 2 comes anyway (the wall card)"), TutorialRules::IsRunning(Slow.GetState(TEXT("A4_hug_wall")))
		|| TutorialRules::IsFinished(Slow.GetState(TEXT("A4_hug_wall"))));
	return true;
}

// 2026-10-21 (game designer: "part 1 - a zombie on a perimeter round ONE building, the tripwire; part 2 - lure a zombie into the open
// and shoot it at close range"; two zombies, two buildings): each part reacts only to ITS zombie.
TUTORIAL_AMBUSH_TEST(FTutorialAmbushPartsBoundTest, "PartsBoundToTheirZombie")
bool FTutorialAmbushPartsBoundTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	FZombies Zombies;
	FTutorialStateMachine Machine;
	Zombies.Install(Machine);
	Machine.Load(Data);
	ToTheWait(Machine);
	TestEqual(TEXT("part 1 waits"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	// The FAR zombie walks into a wire: not part 1's zombie.
	Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z2"));
	Settle(Machine);
	TestEqual(TEXT("the far zombie tripping a wire does not complete part 1"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	TestEqual(TEXT("... no 'that's one' line"), Machine.GetState(TEXT("A4_wt_trap")), ETutorialStepState::Idle);
	TestEqual(TEXT("... no lure card yet"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Idle);
	Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
	TestEqual(TEXT("the far zombie killed does not end part 1 either"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	// An unknown enemy (a hound passing by): nothing.
	Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("BP_Enemy_Hound_C_7"));
	TestEqual(TEXT("an unbound enemy does not count"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
	TestEqual(TEXT("the perimeter zombie: part 1 done"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Completed);
	Settle(Machine);
	TestEqual(TEXT("part 2 starts with the wall card"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
	PassTheWall(Machine);
	TestEqual(TEXT("part 2: the lure card"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
	// The NEAR zombie hears the lure (it survived the wire): not part 2's zombie.
	Machine.HandleEvent(GameplayEvents::LureHeard, TEXT("Z1"));
	Settle(Machine);
	TestEqual(TEXT("the perimeter zombie hearing the lure does not complete part 2"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
	TestEqual(TEXT("... and no close-shot card"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Idle);
	Machine.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
	Settle(Machine);
	TestEqual(TEXT("the far zombie: the close-shot card"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
	// The NEAR zombie shot at close range in the open: not part 2's zombie.
	Zombies.NearestCm = 500.f;
	Zombies.OffRouteCm = 800.f;
	Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z1"));
	Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z1"));
	Settle(Machine);
	PlayFeeds(Machine);
	TestEqual(TEXT("the perimeter zombie shot close does not complete part 2"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
	TestEqual(TEXT("... nor the 'not up close' line"), Machine.GetState(TEXT("A4_close_shot_far")), ETutorialStepState::Idle);
	// The FAR zombie, killed by a trap: no close shot - the line, the card is dropped.
	{
		FTutorialStateMachine Trap;
		Zombies.Install(Trap);
		Trap.Load(Data);
		ToTheWait(Trap);
		Trap.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		Settle(Trap);
		PassTheWall(Trap);
		Trap.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
		Settle(Trap);
		Trap.HandleEvent(TEXT("EnemyKilled.Trap"), TEXT("Z2"));
		Trap.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Settle(Trap);
		PlayFeeds(Trap);
		TestEqual(TEXT("a trap kill: the 'not up close' line"), Trap.GetState(TEXT("A4_close_shot_far")), ETutorialStepState::Completed);
		TestEqual(TEXT("... and the close-shot card is dropped"), Trap.GetState(TEXT("A4_close_shot")), ETutorialStepState::Skipped);
	}
	// The FAR zombie shot from 25 m (the squad never let him come close), and shot close but still on his beat.
	for (const TPair<float, float>& Facts : { TPair<float, float>(2500.f, 800.f), TPair<float, float>(500.f, 150.f) })
	{
		FTutorialStateMachine Shot;
		Zombies.Install(Shot);
		Shot.Load(Data);
		ToTheWait(Shot);
		Shot.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		Settle(Shot);
		PassTheWall(Shot);
		Shot.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
		Settle(Shot);
		Zombies.NearestCm = Facts.Key;
		Zombies.OffRouteCm = Facts.Value;
		Shot.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
		Shot.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Settle(Shot);
		PlayFeeds(Shot);
		TestTrue(FString::Printf(TEXT("killed %.0f m away, %.1f m off his route: no close shot, the line, the card dropped"), Facts.Key / 100.f, Facts.Value / 100.f),
			Shot.GetState(TEXT("A4_close_shot")) == ETutorialStepState::Skipped && Shot.GetState(TEXT("A4_close_shot_far")) == ETutorialStepState::Completed);
	}
	// The far zombie killed BEFORE he was lured: the lure card is dropped (nothing to lure), the line says why.
	{
		FTutorialStateMachine Early;
		Zombies.Install(Early);
		Early.Load(Data);
		ToTheWait(Early);
		Early.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		Settle(Early);
		PassTheWall(Early);
		TestEqual(TEXT("the lure card is up"), Early.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
		Zombies.NearestCm = 2500.f;
		Early.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
		Early.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Settle(Early);
		PlayFeeds(Early);
		TestEqual(TEXT("... the 'not up close' line says why"), Early.GetState(TEXT("A4_close_shot_far")), ETutorialStepState::Completed);
		TestEqual(TEXT("killed before the lure: the lure card is dropped"), Early.GetState(TEXT("A4_lure")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... no close-shot card ever"), Early.GetState(TEXT("A4_close_shot")), ETutorialStepState::Idle);
	}
	// The perimeter zombie shot dead before the wire caught him: part 1 is over, part 2 follows.
	{
		FTutorialStateMachine Shot1;
		Zombies.Install(Shot1);
		Shot1.Load(Data);
		ToTheWait(Shot1);
		Shot1.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z1"));
		Shot1.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z1"));
		Settle(Shot1);
		TestEqual(TEXT("the perimeter zombie killed otherwise: the wait is dropped"), Shot1.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... part 2 follows (the wall card)"), Shot1.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
	}
	// No runtime filter (a level without the bindings' enemies / a unit test without the provider): a filtered event never counts.
	{
		FTutorialStateMachine Bare;
		Bare.Load(Data);
		ToTheWait(Bare);
		Bare.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		TestEqual(TEXT("no provider: the bound wait does not complete"), Bare.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
	}
	return true;
}

// 2026-10-21: the data side of the bindings ("enemies" + "enemy" / "within_m" / "off_route_m") and the pure filter / spot rules.
TUTORIAL_AMBUSH_TEST(FTutorialAmbushEnemyBindingSchemaTest, "EnemyBindingSchema")
bool FTutorialAmbushEnemyBindingSchemaTest::RunTest(const FString&)
{
	auto Parse = [](const FString& Json, FTutorialStepsData& Out, TArray<FString>& Warnings, FString& Error)
	{
		Warnings.Reset();
		Error.Reset();
		return TutorialRules::ParseSteps(Json, Out, Warnings, Error);
	};
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	const FString Good = TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": ["Route_A", "PatrolRouteActor_1"], "actor": "BP_Zombie", "_why": "comment" } ],
		"steps": [ { "id": "s", "kind": "objective", "text_id": "X", "trigger": { "type": "event", "event": "StealthLure.Heard", "enemy": "Z" },
		"cancel_on": { "event": "EnemyKilled", "enemy": "Z" },
		"complete": { "event": "EnemyKilled.Squad", "enemy": "Z", "within_m": 10, "off_route_m": 4 } } ] })JSON");
	TestTrue(TEXT("a binding with route + actor selectors parses"), Parse(Good, Data, Warnings, Error));
	TestEqual(TEXT("... without warnings"), Warnings.Num(), 0);
	const FTutorialEnemyBinding* Binding = Data.FindEnemy(TEXT("Z"));
	TestTrue(TEXT("route / actor selectors"), Binding && Binding->Routes.Num() == 2 && Binding->Actors.Num() == 1 && Binding->Actors[0] == TEXT("BP_Zombie"));
	const FTutorialStep* Step = Data.FindStep(TEXT("s"));
	TestTrue(TEXT("trigger / cancel_on / complete filters"), Step && Step->Trigger.EnemyFilter.Enemy == FName(TEXT("Z")) && Step->CancelOn.EnemyFilter.Enemy == FName(TEXT("Z"))
		&& Step->Complete.EnemyFilter.Enemy == FName(TEXT("Z")) && FMath::IsNearlyEqual(Step->Complete.EnemyFilter.WithinMetres, 10.f)
		&& FMath::IsNearlyEqual(Step->Complete.EnemyFilter.OffRouteMetres, 4.f) && Step->Trigger.EnemyFilter.WithinMetres == 0.f);

	TestFalse(TEXT("an unknown binding in complete: error"), Parse(TEXT(R"JSON({ "steps": [ { "id": "s", "complete": { "event": "E", "enemy": "Nope" } } ] })JSON"),
		Data, Warnings, Error));
	TestTrue(TEXT("... naming it"), Error.Contains(TEXT("Nope")));
	TestFalse(TEXT("an unknown binding in a trigger: error"), Parse(TEXT(R"JSON({ "steps": [ { "id": "s", "trigger": { "type": "event", "event": "E", "enemy": "Nope" },
		"complete": { "timeout_s": 1 } } ] })JSON"), Data, Warnings, Error));
	TestFalse(TEXT("an unknown binding in cancel_on: error"), Parse(TEXT(R"JSON({ "steps": [ { "id": "s", "cancel_on": { "event": "E", "enemy": "Nope" },
		"complete": { "timeout_s": 1 } } ] })JSON"), Data, Warnings, Error));
	TestFalse(TEXT("a binding without id: error"), Parse(TEXT(R"JSON({ "enemies": [ { "route": "R" } ], "steps": [ { "id": "s" } ] })JSON"), Data, Warnings, Error));
	TestFalse(TEXT("a binding without selectors: error"), Parse(TEXT(R"JSON({ "enemies": [ { "id": "Z" } ], "steps": [ { "id": "s" } ] })JSON"), Data, Warnings, Error));
	TestFalse(TEXT("a duplicate binding: error"), Parse(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R" }, { "id": "Z", "actor": "A" } ],
		"steps": [ { "id": "s" } ] })JSON"), Data, Warnings, Error));
	TestTrue(TEXT("an unknown binding key: warning"), Parse(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R", "tag": "T" } ],
		"steps": [ { "id": "s", "complete": { "timeout_s": 1 } } ] })JSON"), Data, Warnings, Error) && Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("tag")); }));
	TestTrue(TEXT("within_m without enemy: warning, ignored"), Parse(TEXT(R"JSON({ "steps": [ { "id": "s", "complete": { "event": "E", "within_m": 5 } } ] })JSON"),
		Data, Warnings, Error) && Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("within_m")); })
		&& Data.Steps[0].Complete.EnemyFilter.WithinMetres == 0.f);
	TestTrue(TEXT("enemy without event: warning"), Parse(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R" } ], "steps": [ { "id": "s",
		"complete": { "enemy": "Z", "timeout_s": 3 } } ] })JSON"), Data, Warnings, Error) && Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("only filters events")); }));

	// Guide spots bound to an enemy.
	FString Kind;
	FName Enemy;
	TestTrue(TEXT("@lure_spot.A4_LURE parses"), TutorialRules::ParseGuideSpot(TEXT("@lure_spot.A4_LURE"), Kind, Enemy) && Kind == TEXT("lure_spot") && Enemy == FName(TEXT("A4_LURE")));
	TestTrue(TEXT("@patrol_route parses without an enemy"), TutorialRules::ParseGuideSpot(TEXT("@patrol_route"), Kind, Enemy) && Kind == TEXT("patrol_route") && Enemy.IsNone());
	TestFalse(TEXT("a volume id is no spot"), TutorialRules::ParseGuideSpot(TEXT("VOL_A4_OVERLOOK"), Kind, Enemy));
	TestTrue(TEXT("known spots"), TutorialRules::IsKnownGuideSpot(TEXT("@patrol_route")) && TutorialRules::IsKnownGuideSpot(TEXT("@patrol_route.Z"))
		&& TutorialRules::IsKnownGuideSpot(TEXT("@lure_spot.Z")) && TutorialRules::IsKnownGuideSpot(TEXT("@enemy.Z")));
	TestFalse(TEXT("unknown spots"), TutorialRules::IsKnownGuideSpot(TEXT("@lure_spot")) || TutorialRules::IsKnownGuideSpot(TEXT("@enemy"))
		|| TutorialRules::IsKnownGuideSpot(TEXT("@campfire.Z")));
	FTutorialStepsData Spots;
	TestTrue(TEXT("a spot naming an unknown binding parses"), Parse(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R" } ], "steps": [
		{ "id": "a", "guide": "@lure_spot.Nope", "complete": { "timeout_s": 1 } }, { "id": "b", "guide": "@enemy.Z", "complete": { "timeout_s": 1 } } ] })JSON"), Spots, Warnings, Error));
	TArray<FString> Validation;
	TutorialRules::ValidateSteps(Spots, Validation);
	TestTrue(TEXT("... and the validator warns about it"), Validation.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("@lure_spot.Nope")); }));
	TestFalse(TEXT("... not about the known one"), Validation.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("@enemy.Z")); }));

	// The pure rules.
	FTutorialEnemyFilter Filter;
	TestTrue(TEXT("no filter: everything passes"), TutorialRules::PassesEnemyFilter(Filter, false, -1.f, -1.f));
	Filter.Enemy = TEXT("Z");
	TestFalse(TEXT("another enemy never passes"), TutorialRules::PassesEnemyFilter(Filter, false, 100.f, 900.f));
	TestTrue(TEXT("the bound enemy passes"), TutorialRules::PassesEnemyFilter(Filter, true, -1.f, -1.f));
	Filter.WithinMetres = 10.f;
	Filter.OffRouteMetres = 4.f;
	TestTrue(TEXT("close and in the open"), TutorialRules::PassesEnemyFilter(Filter, true, 999.f, 400.f));
	TestFalse(TEXT("10.01 m away"), TutorialRules::PassesEnemyFilter(Filter, true, 1001.f, 900.f));
	TestFalse(TEXT("no operative alive"), TutorialRules::PassesEnemyFilter(Filter, true, -1.f, 900.f));
	TestFalse(TEXT("3.9 m off his route = still on his beat"), TutorialRules::PassesEnemyFilter(Filter, true, 500.f, 390.f));
	TestTrue(TEXT("no route: the off-route rule passes"), TutorialRules::PassesEnemyFilter(Filter, true, 500.f, -1.f));
	const TArray<FName> Tags = { TEXT("A4Zombie") };
	TestTrue(TEXT("selector by object name"), TutorialRules::MatchesActorSelector(TEXT("PatrolRouteActor_1"), TEXT("PatrolRouteActor_1"), FString(), Tags));
	TestTrue(TEXT("selector by label, any case"), TutorialRules::MatchesActorSelector(TEXT("patrolroute_a4_hounds"), TEXT("PatrolRouteActor_1"), TEXT("PatrolRoute_A4_Hounds"), Tags));
	TestTrue(TEXT("selector by tag"), TutorialRules::MatchesActorSelector(TEXT("A4Zombie"), TEXT("X"), FString(), Tags));
	TestFalse(TEXT("whole words only"), TutorialRules::MatchesActorSelector(TEXT("PatrolRouteActor"), TEXT("PatrolRouteActor_1"), TEXT("PatrolRoute_A4_Hounds"), Tags)
		|| TutorialRules::MatchesActorSelector(TEXT(""), NAME_None, FString(), TArray<FName>()));
	// The user's perimeter loop round the near container (cm) and the far zombie's route with a waypoint dragged 12.6 m under the ground.
	const TArray<FVector> Loop = { FVector(25642.f, -2473.f, 4.f), FVector(24640.f, -2490.f, 4.f), FVector(25047.f, -3374.f, 4.f), FVector(25625.f, -3422.f, 4.f) };
	TestTrue(TEXT("on the loop: 0 m off it"), TutorialRules::DistanceToRoute2D(Loop, true, FVector(25100.f, -2482.f, 90.f)) < 10.f);
	TestTrue(TEXT("6 m west of the loop's west corner"), FMath::IsNearlyEqual(TutorialRules::DistanceToRoute2D(Loop, true, FVector(24040.f, -2490.f, 90.f)), 600.f, 15.f));
	TestTrue(TEXT("the closing leg counts (loop)"), TutorialRules::DistanceToRoute2D(Loop, true, FVector(25700.f, -2950.f, 0.f)) < 100.f
		&& TutorialRules::DistanceToRoute2D(Loop, false, FVector(25700.f, -2950.f, 0.f)) > 300.f);
	TestEqual(TEXT("no route: MAX"), TutorialRules::DistanceToRoute2D(TArray<FVector>(), true, FVector::ZeroVector), MAX_flt);
	const TArray<FVector> Far = { FVector(27177.f, -2775.f, 4.5f), FVector(27977.f, -2775.f, 4.5f), FVector(29163.f, -1775.f, -1256.f), FVector(27477.f, -1399.f, 4.5f) };
	const FVector Nearest = TutorialRules::NearestPointOnRoute(Far, true, FVector(28400.f, -2300.f, 90.f));
	TestTrue(TEXT("a waypoint below the ground never pulls the nearest point under it"), Nearest.Z > 0.f);
	const FVector Spot = TutorialRules::LureSpotTowards(FVector(27177.f, -2775.f, 4.5f), FVector(24000.f, -2775.f, 90.f), TutorialRules::LureSpotDistanceCm);
	TestTrue(TEXT("the lure spot: 10 m short of his path towards her"), FMath::IsNearlyEqual(Spot.X, 26177.f, 1.f) && FMath::IsNearlyEqual(Spot.Y, -2775.f, 1.f));
	TestEqual(TEXT("already closer than 10 m: she stays where she is"), TutorialRules::LureSpotTowards(FVector(27177.f, -2775.f, 0.f), FVector(26500.f, -2775.f, 0.f),
		TutorialRules::LureSpotDistanceCm), FVector(26500.f, -2775.f, 0.f));
	return true;
}

// 2026-10-20 (game designer: "two stay in place, one sets up the ambush"): the Medic-Sapper alone; the other two held at the overlook
// until the lure is done; her targets are the wire spot on the patrol route, then the overlook.
TUTORIAL_AMBUSH_TEST(FTutorialAmbushOneOperativeTest, "OneOperative")
bool FTutorialAmbushOneOperativeTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Wire = Data.FindStep(TEXT("A4_tripwire"));
	TestTrue(TEXT("the wire card: the Medic-Sapper, Solo with her selected, held at the overlook, the wire spot as the target"), Wire
		&& TutorialRules::ParseRoleFocus(Wire->Focus) == FName(TEXT("MedicSapper")) && Wire->Confine.bFreeFocus && Wire->Confine.Volume == FName(TEXT("VOL_A4_OVERLOOK"))
		&& Wire->Confine.Until == FName(TEXT("A4_lure")) && Wire->Actions.ContainsByPredicate([](const FTutorialAction& Action) { return Action.Type == FName(TEXT("solo_focus")); })
		&& Wire->Guide == FName(TEXT("@patrol_route.A4_PERIMETER")));
	TestTrue(TEXT("role focus parse"), TutorialRules::ParseRoleFocus(TEXT("role:Engineer")) == FName(TEXT("Engineer")) && TutorialRules::ParseRoleFocus(TEXT("role:Nobody")).IsNone()
		&& !TutorialRules::IsKnownFocus(TEXT("role:Nobody")) && TutorialRules::IsKnownFocus(TEXT("role:MedicSapper")));

	// The pure judge: she is free, the others held whatever their stance (outside); inside the area they may move.
	FTutorialConfineView View;
	View.Spec = Wire ? Wire->Confine : FTutorialConfine();
	View.FreeOperative = TEXT("Medic");
	TestEqual(TEXT("the ambusher goes anywhere"), TutorialRules::JudgeConfinedMove(View, TEXT("Medic"), TEXT("stand"), false), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("a mate is held back"), TutorialRules::JudgeConfinedMove(View, TEXT("Engineer"), TEXT("prone"), false), ETutorialConfineVerdict::HeldBack);
	TestEqual(TEXT("a mate may move inside the overlook"), TutorialRules::JudgeConfinedMove(View, TEXT("Engineer"), TEXT("stand"), true), ETutorialConfineVerdict::Allowed);
	TestEqual(TEXT("verdict name"), FString(TutorialRules::ConfineVerdictName(ETutorialConfineVerdict::HeldBack)), FString(TEXT("held_back")));

	FZombies Zombies;
	FTutorialStateMachine Machine;
	Zombies.Install(Machine);
	Machine.FocusSubjectProvider = [](FName) { return FName(TEXT("Medic")); };
	bool bRouteInLevel = true;
	Machine.SpotProvider = [&bRouteInLevel](FName Spot, FName)
	{
		return bRouteInLevel && (Spot == FName(TEXT("@patrol_route.A4_PERIMETER")) || Spot == FName(TEXT("@lure_spot.A4_LURE")));
	};
	Machine.VolumeProvider = [](FName VolumeId) { return VolumeId == FName(TEXT("VOL_A4_OVERLOOK")); };
	Machine.Load(Data);
	Machine.RegisterVolumeBeat(TEXT("VOL_A4_OVERLOOK"), TEXT("A4"));
	IntoTheField(Machine);
	Machine.HandleVolume(TEXT("VOL_A4_OVERLOOK"), NAME_None, TEXT("A4"), TEXT("Medic"));
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	Settle(Machine);
	FTutorialConfineView Held;
	TestTrue(TEXT("the wire card holds the squad at the overlook, the Medic-Sapper free"), Machine.GetState(TEXT("A4_tripwire")) == ETutorialStepState::Active
		&& Machine.GetActiveConfine(Held) && Held.Owner == FName(TEXT("A4_tripwire")) && Held.FreeOperative == FName(TEXT("Medic")));
	FTutorialGuide Guide = Machine.GetGuide();
	TestTrue(TEXT("her target: the wire spot on the PERIMETER zombie's loop"), Guide.Kind == ETutorialGuideKind::Spot && Guide.Id == FName(TEXT("@patrol_route.A4_PERIMETER")));
	bRouteInLevel = false;
	TestTrue(TEXT("no patrol route in the level: the overlook (the place reached last)"), Machine.GetGuide().Id == FName(TEXT("VOL_A4_OVERLOOK")));
	bRouteInLevel = true;
	Machine.HandleCommand(PlayerCommands::DeployTripwire);
	Settle(Machine);
	TestTrue(TEXT("part 1 waits - still held"), Machine.GetState(TEXT("A4_trap_wait")) == ETutorialStepState::Active && Machine.IsConfined());
	TestEqual(TEXT("the wait: back to the overlook"), Machine.GetGuide().Id, FName(TEXT("VOL_A4_OVERLOOK")));
	Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
	Settle(Machine);
	TestTrue(TEXT("part 2: the wall card - her target at the near container (the perimeter loop)"), Machine.GetState(TEXT("A4_hug_wall")) == ETutorialStepState::Active
		&& Machine.GetGuide().Id == FName(TEXT("@patrol_route.A4_PERIMETER")));
	PassTheWall(Machine);
	TestTrue(TEXT("part 2: the lure - still held"), Machine.GetState(TEXT("A4_lure")) == ETutorialStepState::Active && Machine.IsConfined());
	TestTrue(TEXT("part 2: her target is the lure spot short of the FAR zombie's path"), Machine.GetGuide().Kind == ETutorialGuideKind::Spot
		&& Machine.GetGuide().Id == FName(TEXT("@lure_spot.A4_LURE")));
	Machine.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
	TestTrue(TEXT("the lure done: the hold is released"), Machine.GetState(TEXT("A4_lure")) == ETutorialStepState::Completed && !Machine.IsConfined());

	// A fight releases the hold at once (the runtime's UnlockAll / ReleaseConfines).
	FTutorialStateMachine Fight;
	Fight.FocusSubjectProvider = Machine.FocusSubjectProvider;
	Fight.Load(Data);
	IntoTheField(Fight);
	Fight.StartBeat(TEXT("A4"));
	Settle(Fight);
	Fight.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Fight);
	TestTrue(TEXT("held"), Fight.IsConfined());
	TestTrue(TEXT("a fight: released"), Fight.ReleaseConfines() && !Fight.IsConfined());
	return true;
}

// Symmetric sight (2026-10-19 b): "Prone behind cover they can't see you - but you can't see them either" - the first time someone
// lies prone while an enemy is out of the squad's sight, from the overlook on.
TUTORIAL_AMBUSH_TEST(FTutorialAmbushHiddenCardTest, "OutOfSightCard")
bool FTutorialAmbushHiddenCardTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	TestTrue(TEXT("known stats"), TutorialRules::IsKnownStat(TEXT("squad_prone")) && TutorialRules::IsKnownStat(TEXT("enemies_unseen")));
	const FTutorialStep* Card = Data.FindStep(TEXT("A4_hidden"));
	if (!TestNotNull(TEXT("the out-of-sight card"), Card))
	{
		return false;
	}
	TestTrue(TEXT("A4, condition-triggered from the overlook on (requires A4_cones)"), Card->Beat == FName(TEXT("A4"))
		&& Card->Trigger.Type == ETutorialTriggerType::Condition && Card->Trigger.Requires.Contains(TEXT("A4_cones")) && Card->Conditions.Num() == 2);
	float Prone = 0.f;
	float Unseen = 0.f;
	FTutorialStateMachine Machine;
	Machine.StatProvider = [&Prone, &Unseen](FName Stat)
	{
		return Stat == FName(TEXT("squad_prone")) ? Prone : (Stat == FName(TEXT("enemies_unseen")) ? Unseen : 0.f);
	};
	Machine.Load(Data);
	IntoTheField(Machine);
	Prone = 3.f;
	Unseen = 1.f;
	Settle(Machine);
	TestEqual(TEXT("A3 (the fence, prone): nothing - the card belongs to the overlook"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Idle);
	TestEqual(TEXT("... and it does not start A4"), Machine.GetCurrentBeat(), FName(TEXT("A3")));
	Prone = 0.f;
	Machine.StartBeat(TEXT("A4"));
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
	Settle(Machine);
	Machine.HandleCommand(PlayerCommands::PosturePassive);
	Settle(Machine);
	TestEqual(TEXT("standing: no card"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Idle);
	Unseen = 0.f;
	Prone = 1.f;
	Settle(Machine);
	TestEqual(TEXT("prone, every enemy in sight: no card"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Idle);
	Unseen = 1.f;
	Machine.Tick(0.1f);
	TestNotEqual(TEXT("prone with an enemy out of sight: the card is due"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Idle);
	// One card at a time: finish the tripwire card that came after the cones.
	Machine.HandleCommand(PlayerCommands::DeployTripwire);
	Settle(Machine);
	TestEqual(TEXT("the out-of-sight card shows"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Active);
	Machine.HandleCommand(PlayerCommands::StanceStand);
	TestEqual(TEXT("rising to look completes it"), Machine.GetState(TEXT("A4_hidden")), ETutorialStepState::Completed);
	return true;
}

// 2026-10-21 (user play-test): a zombie that ENGAGES the PASSIVE squad mid-lesson -> the free tactical pause + the posture card (once per
// level, either part); the stealth cards of the moment are dropped and the lesson goes on after the skirmish. Part 2's approach along the
// near container's wall.
TUTORIAL_AMBUSH_TEST(FTutorialAmbushSpottedTest, "SpottedPausesTheFight")
bool FTutorialAmbushSpottedTest::RunTest(const FString&)
{
	using namespace TutorialAmbushTestData;
	FTutorialStepsData Data;
	if (!LoadDemo(*this, Data))
	{
		return false;
	}
	const FTutorialStep* Card = Data.FindStep(TEXT("A4_spotted"));
	TestTrue(TEXT("the spotted card: either zombie engages, the tactical pause, DEFENSIVE / AGGRESSIVE, only a PASSIVE squad"), Card
		&& Card->Trigger.Event == GameplayEvents::EnemyAlerted.ToString() && Card->Trigger.EnemyFilter.Enemy == FName(TEXT("A4_ANY"))
		&& Card->Actions.ContainsByPredicate([](const FTutorialAction& Action) { return Action.Type == FName(TEXT("tactical_pause")); })
		&& Card->Complete.Commands.Contains(PlayerCommands::PostureDefensive.ToString()) && Card->Complete.Commands.Contains(PlayerCommands::PostureAggressive.ToString())
		&& Card->Conditions.Num() == 1 && Card->Conditions[0].Stat == FName(TEXT("squad_passive")) && Card->bOnce);
	const FTutorialStep* Lure = Data.FindStep(TEXT("A4_lure"));
	TestTrue(TEXT("the lure card sets the squad PASSIVE again"), Lure && Lure->Actions.ContainsByPredicate([](const FTutorialAction& Action)
	{
		return Action.Type == FName(TEXT("set_posture")) && Action.Posture == TEXT("passive");
	}));
	TestTrue(TEXT("known: action tactical_pause, stats squad_passive / in_fight"), TutorialRules::IsKnownAction(TEXT("tactical_pause"))
		&& TutorialRules::IsKnownStat(TEXT("squad_passive")) && TutorialRules::IsKnownStat(TEXT("in_fight")));

	TMap<FName, float> Stats;
	Stats.Add(TEXT("squad_passive"), 3.f);
	auto Install = [&Stats](FTutorialStateMachine& Machine)
	{
		Machine.StatProvider = [&Stats](FName Stat) { const float* Value = Stats.Find(Stat); return Value ? *Value : 0.f; };
	};
	FZombies Zombies;

	// PART 1 fails: the perimeter zombie spots her while she rigs the wire.
	{
		FTutorialStateMachine Machine;
		Zombies.Install(Machine);
		Install(Machine);
		Machine.Load(Data);
		IntoTheField(Machine);
		Machine.StartBeat(TEXT("A4"));
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
		Settle(Machine);
		Machine.HandleCommand(PlayerCommands::PosturePassive);
		Settle(Machine);
		TestEqual(TEXT("the wire card"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Engineer"));
		TestEqual(TEXT("an operative's 'alert' is nothing"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Idle);
		Stats.Add(TEXT("in_fight"), 1.f);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Z1"));
		Settle(Machine);
		TestEqual(TEXT("spotted: the wire card is dropped"), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... the SPOTTED card (pause + posture)"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Active);
		Machine.HandleCommand(PlayerCommands::PostureDefensive);
		TestEqual(TEXT("DEFENSIVE completes it"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Completed);
		// The skirmish: the perimeter zombie is shot dead -> the wait is over, the fight ends, part 2 follows.
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z1"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z1"));
		Stats.Add(TEXT("in_fight"), 0.f);
		Settle(Machine);
		TestEqual(TEXT("part 1 is over"), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Skipped);
		TestEqual(TEXT("part 2 goes on: the wall card"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
		PassTheWall(Machine);
		TestEqual(TEXT("... round the corner, then the lure"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Active);
		// The far zombie spots her too: once per level - no second card, no second pause; the lure card is dropped.
		Stats.Add(TEXT("in_fight"), 1.f);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Z2"));
		Settle(Machine);
		TestEqual(TEXT("spotted again: the lure card is dropped"), Machine.GetState(TEXT("A4_lure")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... the spotted card never comes twice"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Completed);
		Stats.Add(TEXT("in_fight"), 0.f);
	}
	// PART 2 fails during the approach: the far zombie spots her at the wall -> the card, the stealth chain ends quietly.
	{
		FTutorialStateMachine Machine;
		Zombies.Install(Machine);
		Install(Machine);
		Machine.Load(Data);
		ToTheWait(Machine);
		Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		Settle(Machine);
		TestEqual(TEXT("the wall card"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
		Stats.Add(TEXT("in_fight"), 1.f);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Z2"));
		Settle(Machine);
		TestEqual(TEXT("spotted at the wall: the wall card is dropped"), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Skipped);
		TestEqual(TEXT("... the SPOTTED card"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Active);
		TestTrue(TEXT("... the corner and lure cards skip themselves in the fight"), Machine.GetState(TEXT("A4_round_corner")) == ETutorialStepState::Skipped
			&& Machine.GetState(TEXT("A4_lure")) == ETutorialStepState::Skipped);
		Stats.Add(TEXT("squad_passive"), 0.f);
		Machine.Tick(0.5f);
		TestEqual(TEXT("nobody PASSIVE any more (Alt + /): the card completes"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Completed);
		Stats.Add(TEXT("squad_passive"), 3.f);
		Stats.Add(TEXT("in_fight"), 0.f);
	}
	// PART 2: the lured zombie spots her while the close-shot card is up - the card keeps its slot (it already says AGGRESSIVE).
	{
		FTutorialStateMachine Machine;
		Zombies.Install(Machine);
		Install(Machine);
		Machine.Load(Data);
		ToTheWait(Machine);
		Machine.HandleEvent(GameplayEvents::TripwireTriggeredByEnemy, TEXT("Z1"));
		Settle(Machine);
		PassTheWall(Machine);
		Machine.HandleEvent(GameplayEvents::LureHeard, TEXT("Z2"));
		Settle(Machine);
		TestEqual(TEXT("the close-shot card"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
		Stats.Add(TEXT("in_fight"), 1.f);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Z2"));
		Settle(Machine);
		TestEqual(TEXT("he spots her up close: the close-shot card stays"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Active);
		TestNotEqual(TEXT("... the spotted card waits behind it"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Active);
		Zombies.NearestCm = 500.f;
		Zombies.OffRouteCm = 700.f;
		Machine.HandleEvent(TEXT("EnemyKilled.Squad"), TEXT("Z2"));
		Machine.HandleEvent(GameplayEvents::EnemyKilled, TEXT("Z2"));
		Settle(Machine);
		TestEqual(TEXT("shot up close in the open: done"), Machine.GetState(TEXT("A4_close_shot")), ETutorialStepState::Completed);
		TestEqual(TEXT("... the waiting spotted card is dropped (no late pause)"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Skipped);
		Zombies.NearestCm = 3000.f;
		Zombies.OffRouteCm = 0.f;
		Stats.Add(TEXT("in_fight"), 0.f);
	}
	// A squad already firing back (not PASSIVE): no card, no pause.
	{
		FTutorialStateMachine Machine;
		Zombies.Install(Machine);
		Stats.Add(TEXT("squad_passive"), 0.f);
		Install(Machine);
		Machine.Load(Data);
		ToTheWait(Machine);
		Machine.HandleEvent(GameplayEvents::EnemyAlerted, TEXT("Z1"));
		Settle(Machine);
		TestEqual(TEXT("an AGGRESSIVE squad: no spotted card"), Machine.GetState(TEXT("A4_spotted")), ETutorialStepState::Skipped);
	}
	return true;
}

#undef TUTORIAL_AMBUSH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
