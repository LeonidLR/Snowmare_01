#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Level/LevelObstacleRules.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-20 (game designer play-test of the lake): the current navigation target ("guide") - exactly one, derived from the steps / the
// route; places behind the squad never call it back ("beyond", objectives tied to a place left behind); the target survives a save.

#define TUTORIAL_GUIDE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.Guide." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialGuideTestData
{
	/** A small route: three beacons in B1 (an objective with a marker at the second), the camp starts B2 (a volume actor's Beat Id). */
	const TCHAR* GuideRoute = TEXT(R"JSON({
		"id": "guide", "start_beat": "B1",
		"beats": [ { "id": "B1", "checkpoint": false }, { "id": "B2", "checkpoint": false } ],
		"steps": [
			{ "id": "go_camp", "beat": "B1", "kind": "objective", "text_id": "OBJ_X", "trigger": { "type": "beat_start" }, "complete": { "volume": "VOL_CAMP" } },
			{ "id": "talk1", "beat": "B1", "kind": "walk_talk", "trigger": { "type": "volume", "volume": "VOL_1" }, "lines": [ { "text_id": "WT_1" } ] },
			{ "id": "cross", "beat": "B1", "kind": "objective", "text_id": "OBJ_X", "trigger": { "type": "volume", "volume": "VOL_2" },
			  "complete": { "event": "Crossed", "count": 3, "distinct_operatives": true } },
			{ "id": "card3", "beat": "B1", "kind": "hint_card", "text_id": "TUT_X", "trigger": { "type": "volume", "volume": "VOL_3" },
			  "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "guided", "beat": "B1", "kind": "hint_card", "text_id": "TUT_X", "guide": "VOL_FAR", "trigger": { "type": "manual" },
			  "complete": { "commands": ["Squad.Regroup"] } },
			{ "id": "quiet", "beat": "B1", "kind": "hint_card", "text_id": "TUT_X", "guide": "none", "trigger": { "type": "manual" },
			  "complete": { "commands": ["Stance.Crouch"] } },
			{ "id": "push", "beat": "B2", "kind": "hint_card", "text_id": "TUT_X", "trigger": { "type": "beat_start" },
			  "complete": { "event": "Pushed", "in_volume": "VOL_RUIN" } }
		] })JSON");

	bool GuideTestParse(FAutomationTestBase& Test, const TCHAR* Json, FTutorialStepsData& OutData)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::ParseSteps(Json, OutData, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("parses (%s)"), *Error), bOk);
		for (const FString& Warning : Warnings)
		{
			Test.AddError(TEXT("unexpected warning: ") + Warning);
		}
		return bOk;
	}

	/** Ticks in 0.25 s steps, plays out shown speeches / feed lines (as UTutorialSubsystem does) and records every transition. */
	void GuideTestPump(FTutorialStateMachine& Machine, float Seconds, TArray<FTutorialTransition>& InOutLog)
	{
		InOutLog.Append(Machine.ConsumeTransitions());
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
			InOutLog.Append(Machine.ConsumeTransitions());
		}
	}

	bool GuideTestWasShown(const TArray<FTutorialTransition>& Log, FName StepId)
	{
		return Log.ContainsByPredicate([StepId](const FTutorialTransition& Transition) { return Transition.StepId == StepId && Transition.To == ETutorialStepState::Active; });
	}

	bool GuideTestWasPassed(const TArray<FTutorialTransition>& Log, FName StepId)
	{
		return Log.ContainsByPredicate([StepId](const FTutorialTransition& Transition) { return Transition.StepId == StepId && Transition.bPassed; });
	}

	FString GuideTestDescribe(const FTutorialGuide& Guide)
	{
		if (!Guide.IsSet())
		{
			return TEXT("none");
		}
		return FString::Printf(TEXT("%s:%s (by %s)"), Guide.Kind == ETutorialGuideKind::StepMarkers ? TEXT("markers") : TEXT("volume"), *Guide.Id.ToString(),
			*Guide.SourceStep.ToString());
	}

	/** The lake map's tutorial markers (Step Ids, docs/demo/DEMO01_AUTOLAYOUT.md) and volumes. */
	const TCHAR* const LakeMarkerSteps[] = { TEXT("A0_move"), TEXT("A1_bridge"), TEXT("A1_obj_hut"), TEXT("A1_hut_stack_l"), TEXT("A1_hut_stack_r"),
		TEXT("A1_hut_enter"), TEXT("A1_loot_open"), TEXT("A2_push"), TEXT("A3_peek"), TEXT("A3_cover") };
	const TCHAR* const LakeVolumes[] = { TEXT("VOL_A0_BEACON1"), TEXT("VOL_A1_BEACON2"), TEXT("VOL_A1_HUT_APPROACH"), TEXT("VOL_A1_DOOR_L"), TEXT("VOL_A1_DOOR_R"),
		TEXT("VOL_A1_HUT"), TEXT("VOL_A1_BEACON3"), TEXT("VOL_A2_CAMP"), TEXT("VOL_A2_RUIN"), TEXT("VOL_A3_APPROACH"), TEXT("VOL_A3_WIND"), TEXT("VOL_A3_RIDGE"),
		TEXT("VOL_A3_RIDGE_FAR"), TEXT("VOL_A3_FENCE"), TEXT("VOL_A3_FIELD"), TEXT("VOL_A4_OVERLOOK") };

	/** The real demo file with the lake's markers / volumes / beat volumes and a stat table the test sets. */
	bool GuideTestLoadLake(FAutomationTestBase& Test, FTutorialStateMachine& Machine, TSharedRef<TMap<FName, float>> Stats)
	{
		FTutorialStepsData Data;
		TArray<FString> Warnings;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("demo file loads (%s)"), *Error), TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Data, Warnings, Error)))
		{
			return false;
		}
		Machine.StatProvider = [Stats](FName Stat) { const float* Value = Stats->Find(Stat); return Value ? *Value : 0.f; };
		Machine.StepMarkerProvider = [](FName StepId)
		{
			for (const TCHAR* Marker : LakeMarkerSteps)
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
			for (const TCHAR* Volume : LakeVolumes)
			{
				if (VolumeId == FName(Volume))
				{
					return true;
				}
			}
			return false;
		};
		Machine.Load(Data);
		Machine.RegisterVolumeBeat(TEXT("VOL_A0_BEACON1"), TEXT("A1"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A2_CAMP"), TEXT("A2"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A3_APPROACH"), TEXT("A3"));
		Machine.RegisterVolumeBeat(TEXT("VOL_A4_OVERLOOK"), TEXT("A4"));
		return true;
	}

	const FName NotAcrossStat(TEXT("squad_not_across.VOL_A1_BEACON2"));
}

TUTORIAL_GUIDE_TEST(FTutorialGuideParseTest, "Parse")
bool FTutorialGuideParseTest::RunTest(const FString&)
{
	using namespace TutorialGuideTestData;
	FTutorialStepsData Data;
	if (!GuideTestParse(*this, GuideRoute, Data))
	{
		return false;
	}
	TestEqual(TEXT("\"guide\": a volume id"), Data.FindStep(TEXT("guided"))->Guide, FName(TEXT("VOL_FAR")));
	TestTrue(TEXT("\"guide\": \"none\""), Data.FindStep(TEXT("quiet"))->bNoGuide && Data.FindStep(TEXT("quiet"))->Guide.IsNone());
	TestTrue(TEXT("no guide by default"), Data.FindStep(TEXT("cross"))->Guide.IsNone() && !Data.FindStep(TEXT("cross"))->bNoGuide);

	FTutorialStepsData Beyond;
	TArray<FString> Warnings;
	FString Error;
	TestTrue(TEXT("\"beyond\" parses"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "b", "kind": "objective", "text_id": "OBJ_X",
		"trigger": { "type": "volume", "volume": "VOL_B" }, "beyond": { "stat": "squad_not_across.VOL_B", "below": 1 }, "complete": { "event": "X" } } ] })JSON"),
		Beyond, Warnings, Error));
	TestTrue(TEXT("the beyond clause (squad_not_across.<volume> is a known stat)"), Beyond.Steps.Num() == 1 && Beyond.Steps[0].Beyond.Num() == 1
		&& Beyond.Steps[0].Beyond[0].Stat == FName(TEXT("squad_not_across.VOL_B")) && !Beyond.Steps[0].Beyond[0].bAtLeast);
	TestEqual(TEXT("no warnings"), Warnings.Num(), 0);
	TestEqual(TEXT("ParseNotAcrossStat"), TutorialRules::ParseNotAcrossStat(TEXT("squad_not_across.VOL_B")), FName(TEXT("VOL_B")));
	TestTrue(TEXT("ParseNotAcrossStat: other stats"), TutorialRules::ParseNotAcrossStat(TEXT("squad_not_low_in.VOL_B")).IsNone()
		&& TutorialRules::ParseNotAcrossStat(TEXT("squad_not_across.")).IsNone());

	FTutorialStepsData Bad;
	TestFalse(TEXT("an unknown stat in \"beyond\" is an error"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "b", "text_id": "TUT_X",
		"beyond": { "stat": "nope", "below": 1 }, "complete": { "commands": ["Squad.Solo"] } } ] })JSON"), Bad, Warnings, Error));

	FTutorialStepsData NoPlace;
	TArray<FString> PlaceWarnings;
	TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "b", "text_id": "TUT_X", "trigger": { "type": "manual" },
		"beyond": { "stat": "squad_solo", "at_least": 1 }, "complete": { "commands": ["Squad.Solo"] } } ] })JSON"), NoPlace, PlaceWarnings, Error);
	TutorialRules::ValidateSteps(NoPlace, PlaceWarnings);
	TestTrue(TEXT("\"beyond\" on a step without a place: a validation warning"), PlaceWarnings.ContainsByPredicate([](const FString& Warning)
	{
		return Warning.Contains(TEXT("\"beyond\" on a step without a place"));
	}));
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideRouteTest, "NextTargetFromRoute")
bool FTutorialGuideRouteTest::RunTest(const FString&)
{
	using namespace TutorialGuideTestData;
	FTutorialStepsData Data;
	if (!GuideTestParse(*this, GuideRoute, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	TSet<FName> MarkerSteps = { TEXT("cross") };
	TSet<FName> Volumes = { TEXT("VOL_1"), TEXT("VOL_2"), TEXT("VOL_3"), TEXT("VOL_CAMP"), TEXT("VOL_RUIN"), TEXT("VOL_FAR") };
	Machine.StepMarkerProvider = [&MarkerSteps](FName StepId) { return MarkerSteps.Contains(StepId); };
	Machine.VolumeProvider = [&Volumes](FName VolumeId) { return Volumes.Contains(VolumeId); };
	Machine.Load(Data);
	Machine.RegisterVolumeBeat(TEXT("VOL_CAMP"), TEXT("B2"));
	TestFalse(TEXT("before any beat: the start of the route (the first place)"), !Machine.GetGuide().IsSet());
	TArray<FTutorialTransition> Log;
	Machine.StartBeat(TEXT("B1"));
	GuideTestPump(Machine, 1.f, Log);
	FTutorialGuide Guide = Machine.GetGuide();
	TestEqual(FString::Printf(TEXT("B1 start: the first beacon (%s)"), *GuideTestDescribe(Guide)), Guide.Id, FName(TEXT("VOL_1")));
	TestTrue(TEXT("a route target names no step"), Guide.Kind == ETutorialGuideKind::Volume && Guide.SourceStep.IsNone());

	// A beacon missing from the level is not a target: the next one is.
	Volumes.Remove(TEXT("VOL_1"));
	TestEqual(TEXT("VOL_1 not in the level: VOL_2"), Machine.GetGuide().Id, FName(TEXT("VOL_2")));
	Volumes.Add(TEXT("VOL_1"));

	Machine.HandleVolume(TEXT("VOL_1"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("at beacon 1: the next place (beacon 2)"), Machine.GetGuide().Id, FName(TEXT("VOL_2")));

	// Beacon 2: an objective with a marker there - its marker is the target.
	Machine.HandleVolume(TEXT("VOL_2"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("at beacon 2: the crossing objective's marker (%s)"), *GuideTestDescribe(Guide)),
		Guide.Kind == ETutorialGuideKind::StepMarkers && Guide.Id == FName(TEXT("cross")) && Guide.SourceStep == FName(TEXT("cross")));
	Machine.HandleEvent(TEXT("Crossed"), TEXT("Op1"));
	Machine.HandleEvent(TEXT("Crossed"), TEXT("Op2"));
	Machine.HandleEvent(TEXT("Crossed"), TEXT("Op3"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("crossed: at once the next place (beacon 3)"), Machine.GetGuide().Id, FName(TEXT("VOL_3")));

	// The card in hand: its explicit "guide" wins; "guide": "none" names nothing (the route decides).
	Machine.TriggerStep(TEXT("guided"));
	GuideTestPump(Machine, 2.f, Log);
	Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("the shown card's \"guide\" (%s)"), *GuideTestDescribe(Guide)), Guide.Id == FName(TEXT("VOL_FAR")) && Guide.SourceStep == FName(TEXT("guided")));
	Machine.HandleCommand(TEXT("Squad.Regroup"));
	Machine.TriggerStep(TEXT("quiet"));
	GuideTestPump(Machine, 2.f, Log);
	TestTrue(TEXT("a card with \"guide\": \"none\" is shown"), Machine.GetActiveCard() == FName(TEXT("quiet")));
	TestEqual(TEXT("... and the route decides (beacon 3)"), Machine.GetGuide().Id, FName(TEXT("VOL_3")));
	Machine.HandleCommand(TEXT("Stance.Crouch"));

	// Beacon 3 skipped: the camp (the next beat's start volume) is the next place; reached, B2's card asks for the ruin.
	Machine.HandleVolume(TEXT("VOL_3"), NAME_None, NAME_None, TEXT("Op1"));
	Machine.HandleCommand(TEXT("Squad.Solo"));
	GuideTestPump(Machine, 2.f, Log);
	TestEqual(TEXT("after beacon 3: the camp (the next beat's start)"), Machine.GetGuide().Id, FName(TEXT("VOL_CAMP")));
	Machine.HandleVolume(TEXT("VOL_CAMP"), NAME_None, TEXT("B2"), TEXT("Op1"));
	GuideTestPump(Machine, 2.f, Log);
	Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("B2: the push card's completion volume (%s)"), *GuideTestDescribe(Guide)),
		Guide.Id == FName(TEXT("VOL_RUIN")) && Guide.SourceStep == FName(TEXT("push")));
	Machine.HandleEvent(TEXT("Pushed"), TEXT("Op1"), { TEXT("VOL_RUIN") });
	GuideTestPump(Machine, 2.f, Log);
	Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("nothing further in the data: the place reached last (%s)"), *GuideTestDescribe(Guide)),
		Guide.Id == FName(TEXT("VOL_CAMP")) && Guide.SourceStep.IsNone());
	// Walking back never moves the target backwards.
	Machine.HandleVolume(TEXT("VOL_1"), NAME_None, NAME_None, TEXT("Op1"));
	TestEqual(TEXT("back at beacon 1: the target stays"), Machine.GetGuide().Id, FName(TEXT("VOL_CAMP")));
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideBehindTest, "MarkersBehindDropped")
bool FTutorialGuideBehindTest::RunTest(const FString&)
{
	using namespace TutorialGuideTestData;
	FTutorialStepsData Data;
	if (!GuideTestParse(*this, GuideRoute, Data))
	{
		return false;
	}
	FTutorialStateMachine Machine;
	Machine.StepMarkerProvider = [](FName StepId) { return StepId == FName(TEXT("cross")); };
	Machine.Load(Data);
	Machine.RegisterVolumeBeat(TEXT("VOL_CAMP"), TEXT("B2"));
	TArray<FTutorialTransition> Log;
	Machine.StartBeat(TEXT("B1"));
	Machine.HandleVolume(TEXT("VOL_2"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("the crossing objective (with a marker) is shown"), Machine.GetState(TEXT("cross")), ETutorialStepState::Active);
	// The squad got across another way (no "Crossed" events) and walked on to beacon 3: the objective is over, its marker gone for good.
	Machine.HandleVolume(TEXT("VOL_3"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("an objective tied to a place behind the squad: completed"), Machine.GetState(TEXT("cross")), ETutorialStepState::Completed);
	TestTrue(TEXT("... as passed on the way"), GuideTestWasPassed(Log, TEXT("cross")));
	TestTrue(TEXT("the target is never the place behind"), Machine.GetGuide().Id != FName(TEXT("cross")) && Machine.GetGuide().Id != FName(TEXT("VOL_2")));
	// Back at beacon 2: a once step never comes back.
	Log.Reset();
	Machine.HandleVolume(TEXT("VOL_2"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 2.f, Log);
	TestFalse(TEXT("back at beacon 2: the objective (and its marker) is never re-shown"), GuideTestWasShown(Log, TEXT("cross")));

	// The rule in the target choice itself: an objective whose place lies behind (the route moved while it still ran) is no target.
	FTutorialStateMachine Held;
	Held.StepMarkerProvider = [](FName StepId) { return StepId == FName(TEXT("cross")); };
	Held.Load(Data);
	Held.StartBeat(TEXT("B1"));
	Held.HandleVolume(TEXT("VOL_2"), NAME_None, NAME_None, TEXT("Op1"));
	Held.Tick(0.25f);
	TestEqual(TEXT("its place is beacon 2"), Held.GetStepPlace(TEXT("cross")).Key, Held.GetVolumeRoutePoint(TEXT("VOL_2")).Key);
	TestTrue(TEXT("the route progress is beacon 2"), Held.GetRouteProgressKey() == Held.GetVolumeRoutePoint(TEXT("VOL_2")).Key
		&& Held.GetRouteProgressVolume() == FName(TEXT("VOL_2")));
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideLakeBridgeTest, "LakeBridgeSkipped")
bool FTutorialGuideLakeBridgeTest::RunTest(const FString&)
{
	using namespace TutorialGuideTestData;
	// (a) The game designer's flow (2026-10-20): the squad crosses the crevasse WITHOUT the bridge beacon, does the whole hut lesson,
	// regroups - no bridge marker ever, and the next target (beacon 3) right after the lesson.
	TSharedRef<TMap<FName, float>> Stats = MakeShared<TMap<FName, float>>();
	Stats->Add(NotAcrossStat, 3.f);
	Stats->Add(TEXT("squad_cold_max"), 10.f);
	FTutorialStateMachine Machine;
	if (!GuideTestLoadLake(*this, Machine, Stats))
	{
		return false;
	}
	const FTutorialStep* Bridge = Machine.GetData().FindStep(TEXT("A1_bridge"));
	TestTrue(TEXT("A1_bridge has its \"beyond\" (all across the crevasse)"), Bridge && Bridge->Beyond.Num() == 1 && Bridge->Beyond[0].Stat == NotAcrossStat);
	TArray<FTutorialTransition> Log;
	Machine.StartBeat(TEXT("A0"));
	GuideTestPump(Machine, 3.f, Log);
	FTutorialGuide Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("A0: beacon 1 (%s)"), *GuideTestDescribe(Guide)), Guide.Id == FName(TEXT("VOL_A0_BEACON1")) || Guide.Id == FName(TEXT("A0_move")));
	Machine.HandleVolume(TEXT("VOL_A0_BEACON1"), NAME_None, TEXT("A1"), TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("A1: the bridge approach is next"), Machine.GetGuide().Id, FName(TEXT("VOL_A1_BEACON2")));
	// Across elsewhere: the bridge beacon never fired.
	(*Stats)[NotAcrossStat] = 0.f;
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("all across: the bridge objective is passed without ever showing"), Machine.GetState(TEXT("A1_bridge")), ETutorialStepState::Skipped);
	TestTrue(TEXT("... reported as passed"), GuideTestWasPassed(Log, TEXT("A1_bridge")));
	TestEqual(TEXT("the bridge lines are passed too"), Machine.GetState(TEXT("A1_wt_bridge")), ETutorialStepState::Skipped);
	TestEqual(TEXT("the route is past the bridge"), Machine.GetRouteProgressVolume(), FName(TEXT("VOL_A1_BEACON2")));
	Guide = Machine.GetGuide();
	TestEqual(FString::Printf(TEXT("next target: the hut approach (%s)"), *GuideTestDescribe(Guide)), Guide.Id, FName(TEXT("VOL_A1_HUT_APPROACH")));
	auto NeverTheBridge = [this, &Machine, &Log](const TCHAR* Where)
	{
		const FTutorialGuide Now = Machine.GetGuide();
		TestTrue(FString::Printf(TEXT("%s: the target is not the bridge (%s)"), Where, *GuideTestDescribe(Now)),
			Now.Id != FName(TEXT("A1_bridge")) && Now.Id != FName(TEXT("VOL_A1_BEACON2")));
		TestFalse(FString::Printf(TEXT("%s: the bridge objective never showed"), Where), GuideTestWasShown(Log, TEXT("A1_bridge")));
	};
	// The hut: Solo, select, both sides of the door, the third goes in.
	Machine.HandleVolume(TEXT("VOL_A1_HUT_APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("the hut approach: the Solo card"), Machine.GetActiveCard(), FName(TEXT("A1_solo")));
	NeverTheBridge(TEXT("Solo card"));
	Machine.HandleCommand(TEXT("Squad.Solo"));
	GuideTestPump(Machine, 2.f, Log);
	Machine.HandleCommand(TEXT("Select.Member"));
	GuideTestPump(Machine, 2.f, Log);
	Guide = Machine.GetGuide();
	TestTrue(FString::Printf(TEXT("left of the door: its marker (%s)"), *GuideTestDescribe(Guide)), Guide.Kind == ETutorialGuideKind::StepMarkers
		&& Guide.Id == FName(TEXT("A1_hut_stack_l")));
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op2"), { TEXT("VOL_A1_DOOR_L") });
	GuideTestPump(Machine, 2.f, Log);
	Machine.HandleEvent(TEXT("CoverEntered"), TEXT("Op1"), { TEXT("VOL_A1_DOOR_R") });
	GuideTestPump(Machine, 2.f, Log);
	TestEqual(TEXT("'go in': its marker"), Machine.GetGuide().Id, FName(TEXT("A1_hut_enter")));
	Machine.HandleVolume(TEXT("VOL_A1_HUT"), NAME_None, NAME_None, TEXT("Op3"));
	GuideTestPump(Machine, 2.f, Log);
	TestFalse(TEXT("inside: the squad is free"), Machine.IsConfined());
	NeverTheBridge(TEXT("inside the hut"));
	// The crate, the heat packs, the share, the regroup.
	Machine.HandleCommand(TEXT("Loot.Open"));
	GuideTestPump(Machine, 2.f, Log);
	Machine.HandleEvent(TEXT("ItemLooted.HeatPack"), TEXT("Op1"));
	GuideTestPump(Machine, 3.f, Log);
	Stats->Add(TEXT("squad_min_heat_packs"), 1.f);
	Stats->Add(TEXT("heat_packs_given"), 1.f);
	GuideTestPump(Machine, 3.f, Log);
	TestEqual(TEXT("shared"), Machine.GetState(TEXT("A1_share_card")), ETutorialStepState::Completed);
	TestEqual(TEXT("the regroup card"), Machine.GetActiveCard(), FName(TEXT("A1_regroup")));
	Machine.HandleCommand(TEXT("Squad.Regroup"));
	GuideTestPump(Machine, 2.f, Log);
	Machine.HandleCommand(TEXT("Select.Box"));
	GuideTestPump(Machine, 3.f, Log);
	Guide = Machine.GetGuide();
	TestEqual(FString::Printf(TEXT("right after the hut lesson: beacon 3 (%s)"), *GuideTestDescribe(Guide)), Guide.Id, FName(TEXT("VOL_A1_BEACON3")));
	NeverTheBridge(TEXT("after the regroup"));
	// The heat pack lesson comes late (cold now): the hut objective is NOT shown - the packs were taken already.
	(*Stats)[TEXT("squad_cold_max")] = 45.f;
	GuideTestPump(Machine, 2.f, Log);
	TestEqual(TEXT("cold: the heat pack card"), Machine.GetActiveCard(), FName(TEXT("A1_heatpack")));
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"), TEXT("Op1"));
	GuideTestPump(Machine, 3.f, Log);
	Machine.HandleEvent(TEXT("ItemUsed.HEAT_PACK"), TEXT("Op2"));
	GuideTestPump(Machine, 3.f, Log);
	(*Stats)[TEXT("squad_cold_max")] = 10.f;
	Machine.HandleCommand(TEXT("Order.Sprint"));
	GuideTestPump(Machine, 3.f, Log);
	TestEqual(TEXT("the sprint card done"), Machine.GetState(TEXT("A1_sprint")), ETutorialStepState::Completed);
	TestTrue(TEXT("the hut objective is dropped (the heat packs were taken already): never shown"), !GuideTestWasShown(Log, TEXT("A1_obj_hut"))
		&& TutorialRules::IsFinished(Machine.GetState(TEXT("A1_obj_hut"))));
	TestEqual(TEXT("still beacon 3"), Machine.GetGuide().Id, FName(TEXT("VOL_A1_BEACON3")));
	NeverTheBridge(TEXT("after the late heat pack lesson"));
	Machine.HandleVolume(TEXT("VOL_A1_BEACON3"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Machine, 1.f, Log);
	TestEqual(TEXT("beacon 3 reached: the camp"), Machine.GetGuide().Id, FName(TEXT("VOL_A2_CAMP")));

	// (b) The play-test run: the bridge objective was shown (beacon 2 entered), the squad got across another way.
	TSharedRef<TMap<FName, float>> StatsB = MakeShared<TMap<FName, float>>();
	StatsB->Add(NotAcrossStat, 3.f);
	FTutorialStateMachine Shown;
	GuideTestLoadLake(*this, Shown, StatsB);
	TArray<FTutorialTransition> LogB;
	Shown.StartBeat(TEXT("A1"));
	Shown.HandleVolume(TEXT("VOL_A1_BEACON2"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Shown, 1.f, LogB);
	Guide = Shown.GetGuide();
	TestTrue(FString::Printf(TEXT("beacon 2: the bridge objective's marker (%s)"), *GuideTestDescribe(Guide)),
		Shown.GetState(TEXT("A1_bridge")) == ETutorialStepState::Active && Guide.Id == FName(TEXT("A1_bridge")));
	Shown.HandleEvent(TEXT("BridgeCrossed"), TEXT("Op1"));
	(*StatsB)[NotAcrossStat] = 0.f; // the other two got across without the bridge
	GuideTestPump(Shown, 1.f, LogB);
	TestEqual(TEXT("all across: the bridge objective completes (passed), its marker is gone"), Shown.GetState(TEXT("A1_bridge")), ETutorialStepState::Completed);
	TestEqual(TEXT("the next target at once: the hut approach"), Shown.GetGuide().Id, FName(TEXT("VOL_A1_HUT_APPROACH")));

	// (c) Not all across (no bridge in the level, the stat never drops), the squad walks on to the hut: the objective is behind - over.
	TSharedRef<TMap<FName, float>> StatsC = MakeShared<TMap<FName, float>>();
	StatsC->Add(NotAcrossStat, 3.f);
	FTutorialStateMachine Behind;
	GuideTestLoadLake(*this, Behind, StatsC);
	TArray<FTutorialTransition> LogC;
	Behind.StartBeat(TEXT("A1"));
	Behind.HandleVolume(TEXT("VOL_A1_BEACON2"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Behind, 1.f, LogC);
	Behind.HandleVolume(TEXT("VOL_A1_HUT_APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	GuideTestPump(Behind, 1.f, LogC);
	TestEqual(TEXT("the hut approach reached: the bridge objective (behind) is completed"), Behind.GetState(TEXT("A1_bridge")), ETutorialStepState::Completed);
	TestTrue(TEXT("... as passed"), GuideTestWasPassed(LogC, TEXT("A1_bridge")));
	TestTrue(TEXT("its followers run (the banter after the crossing)"), Behind.GetState(TEXT("A1_wt_banter")) != ETutorialStepState::Idle);
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideSaveTest, "SaveRoundTrip")
bool FTutorialGuideSaveTest::RunTest(const FString&)
{
	using namespace TutorialGuideTestData;
	TSharedRef<TMap<FName, float>> Stats = MakeShared<TMap<FName, float>>();
	Stats->Add(NotAcrossStat, 0.f);
	FTutorialStateMachine Machine;
	if (!GuideTestLoadLake(*this, Machine, Stats))
	{
		return false;
	}
	TArray<FTutorialTransition> Log;
	Machine.StartBeat(TEXT("A1"));
	GuideTestPump(Machine, 1.f, Log); // across: the bridge passed, the route past it
	Machine.HandleVolume(TEXT("VOL_A1_HUT_APPROACH"), NAME_None, NAME_None, TEXT("Op1"));
	Machine.HandleCommand(TEXT("Squad.Solo"));
	GuideTestPump(Machine, 2.f, Log);
	const FTutorialGuide Before = Machine.GetGuide();
	const FTutorialProgress Exported = Machine.ExportProgress();
	TestEqual(TEXT("the save names the furthest place"), Exported.RouteVolume, FName(TEXT("VOL_A1_HUT_APPROACH")));
	FTutorialProgress Loaded;
	TestTrue(TEXT("the JSON round trip"), TutorialRules::LoadProgress(*TutorialRules::SaveProgress(Exported, true), Loaded));
	TestEqual(TEXT("\"route\" survives the JSON"), Loaded.RouteVolume, FName(TEXT("VOL_A1_HUT_APPROACH")));

	FTutorialStateMachine Restored;
	GuideTestLoadLake(*this, Restored, Stats);
	Restored.ImportProgress(Loaded);
	Restored.Tick(0.25f);
	TestEqual(TEXT("the route progress after the load"), Restored.GetRouteProgressKey(), Machine.GetRouteProgressKey());
	const FTutorialGuide After = Restored.GetGuide();
	TestTrue(FString::Printf(TEXT("the current target survives the load (%s -> %s)"), *GuideTestDescribe(Before), *GuideTestDescribe(After)), After == Before);
	TestTrue(TEXT("the dropped bridge objective stays dropped"), TutorialRules::IsFinished(Restored.GetState(TEXT("A1_bridge"))));

	// An older save without "route": the progress is its beat's start - the target still skips the places that are done.
	const TSharedRef<FJsonObject> Old = TutorialRules::SaveProgress(Exported, true);
	Old->RemoveField(TEXT("route"));
	FTutorialProgress OldLoaded;
	TutorialRules::LoadProgress(*Old, OldLoaded);
	FTutorialStateMachine OldRestored;
	GuideTestLoadLake(*this, OldRestored, Stats);
	OldRestored.ImportProgress(OldLoaded);
	const FTutorialGuide OldGuide = OldRestored.GetGuide();
	TestTrue(FString::Printf(TEXT("old save: never the finished bridge (%s)"), *GuideTestDescribe(OldGuide)),
		OldGuide.IsSet() && OldGuide.Id != FName(TEXT("VOL_A1_BEACON2")) && OldGuide.Id != FName(TEXT("A1_bridge")));
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideEdgePointerTest, "EdgePointer")
bool FTutorialGuideEdgePointerTest::RunTest(const FString&)
{
	const FVector2D View(1920.f, 1080.f);
	FVector2D Tip;
	FVector2D Direction;
	TestFalse(TEXT("on screen: no arrow"), TutorialRules::ComputeEdgePointer(View, FVector2D(900.f, 500.f), true, 48.f, Tip, Direction));
	TestTrue(TEXT("far right: an arrow"), TutorialRules::ComputeEdgePointer(View, FVector2D(5000.f, 540.f), true, 48.f, Tip, Direction));
	TestTrue(FString::Printf(TEXT("... pointing right, on the right edge (%.0f, %.0f)"), Tip.X, Tip.Y),
		Direction.X > 0.99f && FMath::IsNearlyEqual(Tip.X, 1920.f - 48.f, 0.5f) && FMath::IsNearlyEqual(Tip.Y, 540.f, 0.5f));
	TestTrue(TEXT("above-left: up-left on the inset rectangle"), TutorialRules::ComputeEdgePointer(View, FVector2D(-1000.f, -2000.f), true, 48.f, Tip, Direction)
		&& Direction.X < 0.f && Direction.Y < 0.f && (FMath::IsNearlyEqual(Tip.X, 48.f, 0.5f) || FMath::IsNearlyEqual(Tip.Y, 48.f, 0.5f)));
	TestTrue(TEXT("not projected (behind): mirrored"), TutorialRules::ComputeEdgePointer(View, FVector2D(1500.f, 540.f), false, 48.f, Tip, Direction)
		&& Direction.X < -0.99f);
	TestFalse(TEXT("a too small view: no arrow"), TutorialRules::ComputeEdgePointer(FVector2D(50.f, 50.f), FVector2D(500.f, 0.f), true, 48.f, Tip, Direction));

	// The projection: a camera at the origin looking along +X (90 deg).
	FVector2D Screen;
	TestTrue(TEXT("straight ahead: in front"), TutorialRules::ProjectToView(FVector::ZeroVector, FRotator::ZeroRotator, 90.f, View, FVector(1000.f, 0.f, 0.f), Screen));
	TestTrue(TEXT("... at the centre"), Screen.Equals(FVector2D(960.f, 540.f), 0.5f));
	TutorialRules::ProjectToView(FVector::ZeroVector, FRotator::ZeroRotator, 90.f, View, FVector(1000.f, 1000.f, 0.f), Screen);
	TestTrue(FString::Printf(TEXT("45 deg right = the right edge with a 90 deg FOV (%.0f)"), Screen.X), FMath::IsNearlyEqual(Screen.X, 1920.f, 0.5f));
	TutorialRules::ProjectToView(FVector::ZeroVector, FRotator::ZeroRotator, 90.f, View, FVector(1000.f, 0.f, 500.f), Screen);
	TestTrue(TEXT("up = towards the top"), Screen.Y < 540.f);
	TestFalse(TEXT("behind: not in front"), TutorialRules::ProjectToView(FVector::ZeroVector, FRotator::ZeroRotator, 90.f, View, FVector(-1000.f, -300.f, 0.f), Screen));
	TestTrue(TEXT("behind-left: the arrow points left"), TutorialRules::ComputeEdgePointer(View, Screen, true, 48.f, Tip, Direction) && Direction.X < -0.9f);
	// A tactical camera (pitched down 60 deg): a point far to the camera's right on the ground is off-screen right.
	const FRotator Tactical(-60.f, 0.f, 0.f);
	const FVector Camera(0.f, 0.f, 2000.f);
	TutorialRules::ProjectToView(Camera, Tactical, 60.f, View, FVector(1200.f, 6000.f, 0.f), Screen);
	TestTrue(TEXT("tactical camera: far right -> an arrow to the right"), TutorialRules::ComputeEdgePointer(View, Screen, true, 48.f, Tip, Direction) && Direction.X > 0.5f);

	TestEqual(TEXT("distance label"), TutorialRules::FormatGuideDistance(4249.f), FString(TEXT("42 m")));
	TestEqual(TEXT("distance label never negative"), TutorialRules::FormatGuideDistance(-10.f), FString(TEXT("0 m")));
	return true;
}

TUTORIAL_GUIDE_TEST(FTutorialGuideAcrossTest, "BridgeAcross")
bool FTutorialGuideAcrossTest::RunTest(const FString&)
{
	// The bridge's span runs along world +Y here (yaw 90), centred at (1000, 2000), 4 m half length.
	const FTransform Bridge(FRotator(0.f, 90.f, 0.f), FVector(1000.f, 2000.f, 0.f));
	const FVector Near(1000.f, 1000.f, 0.f); // the approach volume, 10 m before the middle
	using BridgeRules::IsAcrossFrom;
	TestFalse(TEXT("on the near side: not across"), IsAcrossFrom(Bridge, 400.f, Near, FVector(3000.f, 1200.f, 0.f)));
	TestFalse(TEXT("on the span (far half): not across yet"), IsAcrossFrom(Bridge, 400.f, Near, FVector(1000.f, 2300.f, 0.f)));
	TestTrue(TEXT("off the far end: across"), IsAcrossFrom(Bridge, 400.f, Near, FVector(1000.f, 2500.f, 0.f)));
	TestTrue(TEXT("on the far side 30 m away sideways (crossed elsewhere): across"), IsAcrossFrom(Bridge, 400.f, Near, FVector(-2000.f, 3500.f, 0.f)));
	TestFalse(TEXT("a near point on the middle line: no side"), IsAcrossFrom(Bridge, 400.f, FVector(3000.f, 2000.f, 0.f), FVector(1000.f, 2500.f, 0.f)));
	return true;
}

#undef TUTORIAL_GUIDE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
