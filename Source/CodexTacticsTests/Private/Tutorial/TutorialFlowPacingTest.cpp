#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Data/NarrativeManifest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-20 (game designer: "there's no flow - transitions feel forced, I keep waiting for the next instruction"): a scripted player runs
// the whole Demo 01 tutorial (A0 -> the end of the A4 ambush) on the real steps file, reacting to every card after 1 s and walking to every
// target in 6 s, and the run is measured:
//  - the HANDOVER of each card he completed: seconds until the next card shows (breather + delays; target <= ~1 s);
//  - the seconds with NO actionable instruction on screen: no card, no objective and no current navigation target.
// The table goes to the log (AddInfo). Optional comparison runs: Saved/Tutorial/flow_baseline_steps.json / flow_before_steps.json
// (older copies of the steps file, e.g. `git show <rev>:Content/Data/Tutorial/demo_01_steps.json`), measured with the old 1.5 s breather.

#define TUTORIAL_FLOW_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.Flow." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialFlowTestData
{
	constexpr float FlowTick = 0.25f;
	/** The scripted player reads a card for this long before he does what it says. */
	constexpr float FlowReaction = 1.f;
	/** ... and needs this long to walk to a target / do an objective's task. */
	constexpr float FlowTravel = 6.f;

	const TCHAR* const FlowMarkerSteps[] = { TEXT("A0_move"), TEXT("A1_bridge"), TEXT("A1_obj_hut"), TEXT("A1_hut_stack_l"), TEXT("A1_hut_stack_r"),
		TEXT("A1_hut_enter"), TEXT("A1_loot_open"), TEXT("A2_push"), TEXT("A3_peek"), TEXT("A3_cover") };
	const TCHAR* const FlowVolumes[] = { TEXT("VOL_A0_BEACON1"), TEXT("VOL_A1_BEACON2"), TEXT("VOL_A1_HUT_APPROACH"), TEXT("VOL_A1_DOOR_L"), TEXT("VOL_A1_DOOR_R"),
		TEXT("VOL_A1_HUT"), TEXT("VOL_A1_BEACON3"), TEXT("VOL_A2_CAMP"), TEXT("VOL_A2_RUIN"), TEXT("VOL_A3_APPROACH"), TEXT("VOL_A3_WIND"), TEXT("VOL_A3_RIDGE"),
		TEXT("VOL_A3_RIDGE_FAR"), TEXT("VOL_A3_FENCE"), TEXT("VOL_A3_FIELD"), TEXT("VOL_A4_OVERLOOK") };

	struct FFlowHandover
	{
		FName From;
		FName To;
		float Seconds = 0.f;
	};

	struct FFlowResult
	{
		bool bFinished = false;
		float TotalSeconds = 0.f;
		/** No card, no objective (and, when counted, no current target) on screen. */
		float NoInstructionSeconds = 0.f;
		float NoCardNoObjectiveSeconds = 0.f;
		TArray<FFlowHandover> Handovers;
		int32 CardsShown = 0;
		FString StuckAt;
	};

	/** A concrete command id for a completion pattern ("Stance.*" -> the first registered stance command). */
	FName FlowCommandFor(const FString& Pattern)
	{
		if (!Pattern.Contains(TEXT("*")))
		{
			return FName(*Pattern);
		}
		for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
		{
			if (PlayerCommands::Matches(Pattern, Info.Id))
			{
				return Info.Id;
			}
		}
		return NAME_None;
	}

	float FlowSpeechSeconds(const FTutorialStep& Step)
	{
		const FNarrativeManifest& Manifest = FNarrativeManifest::Get();
		float Seconds = 0.f;
		auto Line = [&Manifest, &Seconds](FName TextId, float Override)
		{
			const FTutorialTextView Text = Manifest.GetTutorialText(TextId.ToString());
			Seconds += TutorialRules::LineDurationSeconds(!Text.Radio.IsEmpty() ? Text.Radio : Text.Text, Override);
		};
		for (const FTutorialLine& Spoken : Step.Lines)
		{
			Line(Spoken.TextId, Spoken.DurationSeconds);
		}
		if (Step.Lines.IsEmpty() && !Step.TextId.IsNone())
		{
			Line(Step.TextId, 0.f);
		}
		return Seconds;
	}

	/** Does what the step asks (its completion), as the scripted player. */
	void FlowPerform(FTutorialStateMachine& Machine, const FTutorialStep& Step, TMap<FName, float>& Stats)
	{
		const FTutorialCompletion& Complete = Step.Complete;
		for (const FTutorialCondition& Clause : Complete.Conditions)
		{
			Stats.Add(Clause.Stat, Clause.bAtLeast ? Clause.Value : FMath::Max(0.f, Clause.Value - 1.f));
		}
		for (const FTutorialCondition& Clause : Step.Beyond)
		{
			Stats.Add(Clause.Stat, Clause.bAtLeast ? Clause.Value : FMath::Max(0.f, Clause.Value - 1.f));
		}
		if (!Complete.Commands.IsEmpty())
		{
			const FName Command = FlowCommandFor(Complete.Commands[0]);
			for (int32 Use = 0; Use < Complete.Count; ++Use)
			{
				Machine.HandleCommand(Command);
			}
			if (Complete.HoldSeconds > 0.f)
			{
				Machine.HandleCommandHold(Command, Complete.HoldSeconds + 0.1f);
			}
		}
		if (!Complete.Event.IsEmpty())
		{
			const FName Event = FName(*Complete.Event.Replace(TEXT("*"), TEXT("")));
			const TArray<FName> Where = Complete.InVolume.IsNone() ? TArray<FName>() : TArray<FName>{ Complete.InVolume };
			// Distinct operatives, and never one who did another part ("distinct_from"): fresh subjects per step.
			for (int32 Use = 0; Use < Complete.Count; ++Use)
			{
				Machine.HandleEvent(Event, FName(*FString::Printf(TEXT("%s_Op%d"), *Step.Id.ToString(), Use)), Where);
			}
		}
		if (!Complete.Volume.IsNone())
		{
			Machine.HandleVolume(Complete.Volume, NAME_None, NAME_None, FName(*FString::Printf(TEXT("%s_Op"), *Step.Id.ToString())));
		}
	}

	FFlowResult RunFlow(const FTutorialStepsData& Data, float Breather, bool bCountGuide)
	{
		FFlowResult Result;
		FTutorialStateMachine Machine;
		Machine.CardBreatherSeconds = Breather;
		TMap<FName, float> Stats;
		Stats.Add(TEXT("squad_not_across.VOL_A1_BEACON2"), 3.f);
		Stats.Add(TEXT("squad_cold_max"), 10.f);
		Stats.Add(TEXT("squad_heat_packs"), 3.f);
		Machine.StatProvider = [&Stats](FName Stat) { const float* Value = Stats.Find(Stat); return Value ? *Value : 0.f; };
		Machine.StepMarkerProvider = [](FName StepId)
		{
			for (const TCHAR* Marker : FlowMarkerSteps)
			{
				if (StepId == FName(Marker))
				{
					return true;
				}
			}
			return false;
		};
		Machine.VolumeProvider = [](FName VolumeId)
		{
			for (const TCHAR* Volume : FlowVolumes)
			{
				if (VolumeId == FName(Volume))
				{
					return true;
				}
			}
			return false;
		};
		Machine.SpotProvider = [](FName, FName) { return true; };
		// 2026-10-21: the scripted player's enemy events are those of the zombie the lesson is bound to.
		Machine.EnemyFilterProvider = [](const FTutorialEnemyFilter&, FName) { return true; };
		Machine.Load(Data);
		Machine.RegisterVolumeBeat(TEXT("VOL_A0_BEACON1"), TEXT("A1"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A2_CAMP"), TEXT("A2"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A3_APPROACH"), TEXT("A3"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A4_OVERLOOK"), TEXT("A4"));
		Machine.StartBeat(Data.StartBeat);

		TMap<FName, float> ShownAt;
		float LastCardDoneAt = -1.f;
		FName LastCardDone;
		float WalkStartedAt = -1.f;
		FName WalkingTo;
		float Now = 0.f;
		for (; Now < 1500.f; Now += FlowTick)
		{
			Machine.Tick(FlowTick);
			for (const FTutorialTransition& Transition : Machine.ConsumeTransitions())
			{
				const FTutorialStep* Step = Data.FindStep(Transition.StepId);
				if (!Step)
				{
					continue;
				}
				if (Transition.To == ETutorialStepState::Active)
				{
					ShownAt.Add(Step->Id, Now);
					if (Step->Kind == ETutorialStepKind::HintCard)
					{
						++Result.CardsShown;
						if (LastCardDoneAt >= 0.f)
						{
							Result.Handovers.Add({ LastCardDone, Step->Id, Now - LastCardDoneAt });
							LastCardDoneAt = -1.f;
						}
					}
					// The runtime's actions the flow depends on: the gust's cold shock starts the heat pack lesson.
					for (const FTutorialAction& Action : Step->Actions)
					{
						if (Action.Type == FName(TEXT("cold_bump")))
						{
							Stats.Add(TEXT("squad_cold_max"), FMath::Max(Action.MinLevel, 45.f));
						}
					}
				}
			}
			if (TutorialRules::IsFinished(Machine.GetState(TEXT("A4_close_shot"))))
			{
				Result.bFinished = true;
				break;
			}
			// Speech plays out; feed lines complete at once.
			for (const FTutorialStep& Step : Data.Steps)
			{
				if (Machine.GetState(Step.Id) != ETutorialStepState::Active || Step.Complete.HasCondition())
				{
					continue;
				}
				const float* Shown = ShownAt.Find(Step.Id);
				if (Step.Kind == ETutorialStepKind::Feed || ((Step.Kind == ETutorialStepKind::Radio || Step.Kind == ETutorialStepKind::WalkTalk)
					&& Shown && Now - *Shown >= FlowSpeechSeconds(Step)))
				{
					Machine.CompleteStep(Step.Id);
				}
			}
			// What is on screen.
			const FName Card = Machine.GetActiveCard();
			const TArray<FName> Objectives = Machine.GetActiveObjectives();
			const bool bGuide = bCountGuide && Machine.GetGuide().IsSet();
			if (Card.IsNone() && Objectives.IsEmpty())
			{
				Result.NoCardNoObjectiveSeconds += FlowTick;
				Result.NoInstructionSeconds += bGuide ? 0.f : FlowTick;
			}
			// The scripted player: the card in hand first, then an objective's task, then the current target.
			if (!Card.IsNone())
			{
				WalkingTo = NAME_None; // he stops to read (and do) the card
				const float* Shown = ShownAt.Find(Card);
				if (Shown && Now - *Shown >= FlowReaction)
				{
					const FTutorialStep* Step = Data.FindStep(Card);
					FlowPerform(Machine, *Step, Stats);
					if (Machine.GetActiveCard() == Card && Now - *Shown >= 30.f)
					{
						Machine.SkipStep(Card); // nothing the scripted player can do: skipped (counted below)
						Result.StuckAt += Card.ToString() + TEXT(" ");
					}
					if (Machine.GetActiveCard() != Card)
					{
						LastCardDone = Card;
						LastCardDoneAt = Now;
						if (Card == FName(TEXT("A1_heatpack_mate")) || Card == FName(TEXT("A2_heatpack")))
						{
							Stats.Add(TEXT("squad_cold_max"), 10.f); // warmed up
						}
					}
				}
				continue;
			}
			bool bDidObjective = false;
			for (const FName Objective : Objectives)
			{
				const FTutorialStep* Step = Data.FindStep(Objective);
				const float* Shown = ShownAt.Find(Objective);
				// "Go to X" objectives are walked to through the target; the others are tasks done in the travel time.
				if (Step && Step->Complete.Volume.IsNone() && Shown && Now - *Shown >= FlowTravel)
				{
					FlowPerform(Machine, *Step, Stats);
					bDidObjective = true;
					break;
				}
			}
			if (bDidObjective)
			{
				continue;
			}
			const FTutorialGuide Guide = Machine.GetGuide();
			FName Target = Guide.Kind == ETutorialGuideKind::Volume ? Guide.Id : NAME_None;
			if (Guide.Kind == ETutorialGuideKind::StepMarkers || Guide.Kind == ETutorialGuideKind::Spot)
			{
				const FTutorialStep* Step = Data.FindStep(Guide.SourceStep);
				Target = Step ? (!Step->Complete.Volume.IsNone() ? Step->Complete.Volume : Step->Complete.InVolume) : NAME_None;
			}
			if (Target.IsNone())
			{
				continue;
			}
			if (WalkingTo != Target)
			{
				WalkingTo = Target;
				WalkStartedAt = Now;
			}
			else if (Now - WalkStartedAt >= FlowTravel)
			{
				Machine.HandleVolume(Target, NAME_None, NAME_None, TEXT("Leader"));
				if (Target == FName(TEXT("VOL_A1_HUT_APPROACH")))
				{
					Stats.Add(TEXT("squad_not_across.VOL_A1_BEACON2"), 0.f); // across the crevasse by then
				}
				WalkingTo = NAME_None;
			}
		}
		Result.TotalSeconds = Now;
		return Result;
	}

	FString FlowDescribe(const TCHAR* Label, const FFlowResult& Result)
	{
		float Max = 0.f;
		float Sum = 0.f;
		int32 Over = 0;
		FString Slow;
		for (const FFlowHandover& Handover : Result.Handovers)
		{
			Max = FMath::Max(Max, Handover.Seconds);
			Sum += Handover.Seconds;
			if (Handover.Seconds > 1.05f)
			{
				++Over;
				Slow += FString::Printf(TEXT(" %s->%s %.2f s;"), *Handover.From.ToString(), *Handover.To.ToString(), Handover.Seconds);
			}
		}
		return FString::Printf(TEXT("| %s | %s | %.0f s | %d | %.2f s | %.2f s | %d | %.1f s | %.1f s |%s"), Label, Result.bFinished ? TEXT("yes") : TEXT("NO"),
			Result.TotalSeconds, Result.CardsShown, Result.Handovers.IsEmpty() ? 0.f : Sum / Result.Handovers.Num(), Max, Over, Result.NoCardNoObjectiveSeconds,
			Result.NoInstructionSeconds, Slow.IsEmpty() ? TEXT("") : *(TEXT(" slow:") + Slow));
	}
}

TUTORIAL_FLOW_TEST(FTutorialFlowPacingTest, "Pacing")
bool FTutorialFlowPacingTest::RunTest(const FString&)
{
	using namespace TutorialFlowTestData;
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
	{
		return false;
	}
	AddInfo(TEXT("| run | finished | game time | cards | handover avg | handover max | handovers > 1 s | no card / objective | no instruction at all |"));
	// Optional comparison runs (older copies of the steps file, the old 1.5 s breather, no current target in the old build).
	for (const TCHAR* Old : { TEXT("flow_baseline_steps.json"), TEXT("flow_before_steps.json") })
	{
		FString Json;
		FTutorialStepsData OldData;
		TArray<FString> OldWarnings;
		FString OldError;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Tutorial") / Old;
		if (FFileHelper::LoadFileToString(Json, *Path) && TutorialRules::ParseSteps(Json, OldData, OldWarnings, OldError))
		{
			const bool bBaseline = FString(Old).Contains(TEXT("baseline"));
			AddInfo(FlowDescribe(Old, RunFlow(OldData, 1.5f, !bBaseline)));
		}
	}
	FTutorialStateMachine Defaults;
	const FFlowResult Now = RunFlow(Data, Defaults.CardBreatherSeconds, true);
	AddInfo(FlowDescribe(TEXT("demo_01_steps.json (now)"), Now));
	TestTrue(FString::Printf(TEXT("the scripted player finishes the tutorial A0 -> A4 (stuck: %s)"), *Now.StuckAt), Now.bFinished && Now.StuckAt.IsEmpty());
	TestTrue(FString::Printf(TEXT("the breather between two cards is at most 1 s (%.2f)"), Defaults.CardBreatherSeconds), Defaults.CardBreatherSeconds <= 1.f);
	for (const FFlowHandover& Handover : Now.Handovers)
	{
		// A card that follows the one just done shows within ~1 s (a later card triggered by a place / an event is no handover).
		const FTutorialStep* Next = Data.FindStep(Handover.To);
		const bool bChained = Next && Next->Trigger.Type == ETutorialTriggerType::AfterStep && Next->Trigger.AfterSteps.Contains(Handover.From);
		if (bChained)
		{
			TestTrue(FString::Printf(TEXT("handover %s -> %s within ~1 s (%.2f s)"), *Handover.From.ToString(), *Handover.To.ToString(), Handover.Seconds),
				Handover.Seconds <= 1.05f);
		}
	}
	TestTrue(FString::Printf(TEXT("never without an instruction (no card, no objective, no target: %.1f s)"), Now.NoInstructionSeconds), Now.NoInstructionSeconds <= 1.f);
	return true;
}

#undef TUTORIAL_FLOW_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
