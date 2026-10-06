// CodexTactics.Combat.TimeMode.* — RTS combat time modes (user request 2026-10-06, FCombatTimeModeRules):
// full real time by default, Space tap = tactical pause on / off, Space hold 1.5 s = turn-based fight and back.

#include "Misc/AutomationTest.h"
#include "Combat/CombatTimeModeRules.h"
#include "Combat/SpaceInput.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TIMEMODE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.TimeMode." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TimeModeTest
{
	/** A fresh machine in WaveCombat / RealTime of wave 1 (the mode every fight starts in). */
	void StartFight(FGameFlowStateMachine& Machine)
	{
		Machine.TriggerCombatZone();
		Machine.FinishCutscene();
		Machine.FinishPreparation();
	}

	/** Presses Space, advances HeldSeconds of real time in 0.1 s steps, releases; returns the resolved action. */
	ESpaceInputAction PressFor(FSpaceInputTracker& Space, float HeldSeconds, float HoldSeconds)
	{
		Space.Press();
		ESpaceInputAction Result = ESpaceInputAction::None;
		for (float Time = 0.f; Time < HeldSeconds - KINDA_SMALL_NUMBER; Time += 0.1f)
		{
			if (Space.Tick(FMath::Min(0.1f, HeldSeconds - Time), HoldSeconds) == ESpaceInputAction::Hold)
			{
				Result = ESpaceInputAction::Hold;
			}
		}
		const ESpaceInputAction Released = Space.Release();
		return Result == ESpaceInputAction::Hold ? Result : Released;
	}

	/** Applies a request to the machine the way the player controller does; returns the flow result. */
	EGameFlowResult Apply(FGameFlowStateMachine& Machine, ECombatTimeModeRequest Request)
	{
		switch (Request)
		{
		case ECombatTimeModeRequest::EnterTacticalPause:
		case ECombatTimeModeRequest::ResumeRealTime:
			return Machine.ToggleTacticalPause();
		case ECombatTimeModeRequest::EnterTurnBased:
			return Machine.RequestEnterTurnBased(true);
		case ECombatTimeModeRequest::ExitTurnBasedToRealTime:
			return Machine.ExitTurnBasedToRealTime();
		case ECombatTimeModeRequest::ExitTurnBasedToPause:
			return Machine.ExitTurnBased();
		default:
			return EGameFlowResult::WrongPhase;
		}
	}

	/** One Space press of HeldSeconds resolved and applied in the machine's current mode. */
	ECombatTimeModeRequest Press(FGameFlowStateMachine& Machine, FSpaceInputTracker& Space, float HeldSeconds)
	{
		const FGameFlowConfig& Config = Machine.GetConfig();
		const ESpaceInputAction Action = PressFor(Space, HeldSeconds, FCombatTimeModeRules::GetHoldSeconds(Machine.GetCombatMode(), Config));
		const ECombatTimeModeRequest Request = FCombatTimeModeRules::ResolveSpace(Machine.GetPhase(), Machine.GetCombatMode(), Action, Config);
		Apply(Machine, Request);
		return Request;
	}
}

using namespace TimeModeTest;

TIMEMODE_TEST(FTimeModeTapTogglesPauseTest, "TapTogglesPause")
bool FTimeModeTapTogglesPauseTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	FSpaceInputTracker Space;
	TestEqual(TEXT("A fight starts in full real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Tap 0.2 s asks for the pause"), Press(Machine, Space, 0.2f), ECombatTimeModeRequest::EnterTacticalPause);
	TestEqual(TEXT("Paused"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestTrue(TEXT("World time near-stopped"), Machine.GetTimeDilation() < 0.1f);
	TestEqual(TEXT("Second tap resumes"), Press(Machine, Space, 0.3f), ECombatTimeModeRequest::ResumeRealTime);
	TestEqual(TEXT("Real time again"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Full speed"), Machine.GetTimeDilation(), 1.f);
	TestEqual(TEXT("A tap in turn-based does nothing"),
		FCombatTimeModeRules::ResolveSpace(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, ESpaceInputAction::Tap, FGameFlowConfig()),
		ECombatTimeModeRequest::None);
	TestEqual(TEXT("A tap in exploration does nothing"),
		FCombatTimeModeRules::ResolveSpace(ECodexGamePhase::Exploration, ECodexCombatMode::None, ESpaceInputAction::Tap, FGameFlowConfig()),
		ECombatTimeModeRequest::None);
	return true;
}

TIMEMODE_TEST(FTimeModeHoldEntersTurnBasedTest, "HoldEntersTurnBased")
bool FTimeModeHoldEntersTurnBasedTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	FSpaceInputTracker Space;
	TestEqual(TEXT("Default hold threshold 1.5 s"), FCombatTimeModeRules::GetHoldSeconds(ECodexCombatMode::RealTime, Machine.GetConfig()), 1.5f);
	TestEqual(TEXT("Hold 1.6 s from real time"), Press(Machine, Space, 1.6f), ECombatTimeModeRequest::EnterTurnBased);
	TestEqual(TEXT("Turn-based"), Machine.GetCombatMode(), ECodexCombatMode::TurnBased);

	// From the tactical pause too (the pause ends without running its plans).
	FGameFlowStateMachine Paused;
	StartFight(Paused);
	int32 Releases = 0;
	Paused.OnTacticalPauseReleased.AddLambda([&Releases]() { ++Releases; });
	Press(Paused, Space, 0.2f);
	TestEqual(TEXT("Paused first"), Paused.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("Hold in the pause"), Press(Paused, Space, 1.6f), ECombatTimeModeRequest::EnterTurnBased);
	TestEqual(TEXT("Turn-based from the pause"), Paused.GetCombatMode(), ECodexCombatMode::TurnBased);
	TestEqual(TEXT("Pause plans not run on the grid"), Releases, 0);

	FGameFlowConfig Old;
	Old.bAllowTurnBasedFromTacticalPause = false;
	TestEqual(TEXT("Disabled: the hold in the pause does nothing"),
		FCombatTimeModeRules::ResolveSpace(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, ESpaceInputAction::Hold, Old),
		ECombatTimeModeRequest::None);

	FGameFlowConfig Tuned;
	Tuned.TurnBasedHoldDuration = 0.8f;
	TestEqual(TEXT("Tunable entry threshold"), FCombatTimeModeRules::GetHoldSeconds(ECodexCombatMode::RealTime, Tuned), 0.8f);
	TestEqual(TEXT("Hold progress halfway"), FCombatTimeModeRules::GetHoldProgress(0.75f, 1.5f), 0.5f);
	TestEqual(TEXT("Hold progress capped"), FCombatTimeModeRules::GetHoldProgress(3.f, 1.5f), 1.f);
	return true;
}

TIMEMODE_TEST(FTimeModeHoldInTurnBasedReturnsToRealTimeTest, "HoldInTurnBasedReturnsToRealTime")
bool FTimeModeHoldInTurnBasedReturnsToRealTimeTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	FSpaceInputTracker Space;
	Press(Machine, Space, 1.6f);
	const int32 Charges = Machine.GetPauseCharges();
	TestEqual(TEXT("Hold in turn-based asks for real time"), Press(Machine, Space, 1.6f), ECombatTimeModeRequest::ExitTurnBasedToRealTime);
	TestEqual(TEXT("Full real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("No pause running"), Machine.GetPauseTimeRemaining(), 0.f);
	TestEqual(TEXT("No charge spent"), Machine.GetPauseCharges(), Charges);
	TestEqual(TEXT("Full speed"), Machine.GetTimeDilation(), 1.f);

	// Tunable exit threshold and the Godot path (free pause) behind the flag.
	FGameFlowConfig Tuned;
	Tuned.TurnBasedExitHoldDuration = 2.5f;
	TestEqual(TEXT("Exit threshold tunable"), FCombatTimeModeRules::GetHoldSeconds(ECodexCombatMode::TurnBased, Tuned), 2.5f);
	Tuned.bHoldExitsTurnBasedToRealTime = false;
	TestEqual(TEXT("Flag off: back to the free pause"),
		FCombatTimeModeRules::ResolveSpace(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, ESpaceInputAction::Hold, Tuned),
		ECombatTimeModeRequest::ExitTurnBasedToPause);

	// The grid's own end (victory) keeps the free tactical pause.
	FGameFlowStateMachine Victory;
	StartFight(Victory);
	Victory.RequestEnterTurnBased(true);
	TestEqual(TEXT("Grid end"), Victory.ExitTurnBased(), EGameFlowResult::Ok);
	TestEqual(TEXT("Grid end gives the free pause"), Victory.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("Exit to real time needs turn-based"), Victory.ExitTurnBasedToRealTime(), EGameFlowResult::NotInTurnBased);
	return true;
}

TIMEMODE_TEST(FTimeModeTapDoesNotTriggerHoldTest, "TapDoesNotTriggerHold")
bool FTimeModeTapDoesNotTriggerHoldTest::RunTest(const FString&)
{
	FSpaceInputTracker Space;
	TestEqual(TEXT("1.4 s is still a tap (resolved on release)"), PressFor(Space, 1.4f, 1.5f), ESpaceInputAction::Tap);
	TestEqual(TEXT("1.55 s is a hold (resolved at the threshold)"), PressFor(Space, 1.55f, 1.5f), ESpaceInputAction::Hold);

	// A hold never also toggles the pause on release, a tap never enters turn-based.
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	Space.Press();
	ESpaceInputAction Fired = ESpaceInputAction::None;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		const ESpaceInputAction Action = Space.Tick(0.1f, 1.5f);
		if (Action != ESpaceInputAction::None)
		{
			TestEqual(TEXT("Only one action per press"), Fired, ESpaceInputAction::None);
			Fired = Action;
		}
	}
	TestEqual(TEXT("The hold fired"), Fired, ESpaceInputAction::Hold);
	TestEqual(TEXT("Release after the hold is no tap"), Space.Release(), ESpaceInputAction::None);
	TestEqual(TEXT("No tap: no pause request"),
		FCombatTimeModeRules::ResolveSpace(Machine.GetPhase(), Machine.GetCombatMode(), ESpaceInputAction::None, Machine.GetConfig()),
		ECombatTimeModeRequest::None);

	FSpaceInputTracker Quick;
	TestEqual(TEXT("Quick tap"), PressFor(Quick, 0.1f, 1.5f), ESpaceInputAction::Tap);
	TestEqual(TEXT("The tap pauses, never enters turn-based"),
		FCombatTimeModeRules::ResolveSpace(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, ESpaceInputAction::Tap, FGameFlowConfig()),
		ECombatTimeModeRequest::EnterTacticalPause);
	return true;
}

TIMEMODE_TEST(FTimeModeOrdersExecuteInRealTimeTest, "OrdersExecuteInRealTimeWithoutPause")
bool FTimeModeOrdersExecuteInRealTimeTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	// No Space at all: the fight is in real time and orders run at once (RTS control, Codex.RealTimeOrders 1).
	TestEqual(TEXT("Real time: execute now"), FCombatTimeModeRules::GetOrderDispatch(Machine.GetPhase(), Machine.GetCombatMode(), true),
		ECombatOrderDispatch::Execute);
	TestEqual(TEXT("Exploration: execute"), FCombatTimeModeRules::GetOrderDispatch(ECodexGamePhase::Exploration, ECodexCombatMode::None, true),
		ECombatOrderDispatch::Execute);
	TestEqual(TEXT("Preparation: execute"), FCombatTimeModeRules::GetOrderDispatch(ECodexGamePhase::Preparation, ECodexCombatMode::None, true),
		ECombatOrderDispatch::Execute);
	TestEqual(TEXT("Turn-based: the grid"), FCombatTimeModeRules::GetOrderDispatch(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, true),
		ECombatOrderDispatch::TurnBasedGrid);
	TestEqual(TEXT("Old 2026-10-05 lock: blocked"), FCombatTimeModeRules::GetOrderDispatch(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, false),
		ECombatOrderDispatch::Blocked);
	TestEqual(TEXT("Label"), FCombatTimeModeRules::GetModeLabel(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime), FString(TEXT("РЕАЛЬНОЕ ВРЕМЯ")));
	TestEqual(TEXT("Label pause"), FCombatTimeModeRules::GetModeLabel(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause),
		FString(TEXT("ТАКТИЧЕСКАЯ ПАУЗА")));
	TestEqual(TEXT("Label turn-based"), FCombatTimeModeRules::GetModeLabel(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased),
		FString(TEXT("ПОШАГОВЫЙ БОЙ")));
	TestTrue(TEXT("No label in exploration"), FCombatTimeModeRules::GetModeLabel(ECodexGamePhase::Exploration, ECodexCombatMode::None).IsEmpty());
	return true;
}

TIMEMODE_TEST(FTimeModeOrdersQueuedDuringPauseTest, "OrdersQueuedDuringPauseExecuteOnResume")
bool FTimeModeOrdersQueuedDuringPauseTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	StartFight(Machine);
	FSpaceInputTracker Space;
	TArray<FVector> Queue;
	TArray<FVector> Executed;
	// The squad subsystem runs its plans on OnTacticalPauseReleased; modelled here with a plain queue.
	Machine.OnTacticalPauseReleased.AddLambda([&Queue, &Executed]()
	{
		Executed.Append(Queue);
		Queue.Reset();
	});
	auto Order = [&](const FVector& Destination)
	{
		switch (FCombatTimeModeRules::GetOrderDispatch(Machine.GetPhase(), Machine.GetCombatMode(), true))
		{
		case ECombatOrderDispatch::Execute: Executed.Add(Destination); break;
		case ECombatOrderDispatch::Queue: Queue.Add(Destination); break;
		default: break;
		}
	};
	Press(Machine, Space, 0.2f);
	TestEqual(TEXT("Paused"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	Order(FVector(100.f, 0.f, 0.f));
	Order(FVector(0.f, 200.f, 0.f));
	TestEqual(TEXT("Queued, nothing ran"), Queue.Num(), 2);
	TestEqual(TEXT("Nothing executed in the pause"), Executed.Num(), 0);
	Press(Machine, Space, 0.2f);
	TestEqual(TEXT("Resumed"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Both orders ran on resume"), Executed.Num(), 2);
	TestEqual(TEXT("Queue empty"), Queue.Num(), 0);
	Order(FVector(300.f, 0.f, 0.f));
	TestEqual(TEXT("Next order runs at once"), Executed.Num(), 3);

	// The pause timing out also runs them.
	Press(Machine, Space, 0.2f);
	Order(FVector(1.f, 2.f, 0.f));
	Machine.Tick(Machine.GetConfig().TacticalPauseDuration + 0.1f);
	TestEqual(TEXT("Timeout resumes"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Timeout ran the plan"), Executed.Num(), 4);
	return true;
}

#undef TIMEMODE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
