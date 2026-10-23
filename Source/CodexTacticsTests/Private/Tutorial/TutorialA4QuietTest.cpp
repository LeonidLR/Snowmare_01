#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Data/NarrativeManifest.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-23 (game designer redesign of the Demo 01 A4 part 1, "zombie #1 spots the squad while they run to the cover"): zombie #1 is
// DORMANT until the whole squad is in cover at the overlook ("enemies" binding "dormant"), then wakes at the point of its loop hidden
// from every operative and farthest along the loop (TutorialRules::ChooseHiddenRoutePoint); the card "TAKE HIM QUIETLY" lets ANY one
// operative (confine "free": "first") rig a tripwire or plant a mine; part 1 is done when he is killed by a SQUAD trap (EnemyKilled.SquadTrap).

#define TUTORIAL_A4_QUIET_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tutorial.A4Quiet." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TutorialA4QuietTestData
{
	bool LoadQuietDemo(FAutomationTestBase& Test, FTutorialStepsData& Out)
	{
		TArray<FString> Warnings;
		FString Error;
		const bool bOk = TutorialRules::LoadStepsFile(TEXT("demo_01_steps.json"), Out, Warnings, Error);
		Test.TestTrue(FString::Printf(TEXT("demo steps load (%s)"), *Error), bOk);
		Test.TestEqual(TEXT("... without warnings"), Warnings.Num(), 0);
		return bOk;
	}

	void SettleQuiet(FTutorialStateMachine& Machine)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Machine.Tick(0.5f);
		}
	}
}

TUTORIAL_A4_QUIET_TEST(FTutorialA4QuietSpawnChoiceTest, "HiddenSpawnChoice")
bool FTutorialA4QuietSpawnChoiceTest::RunTest(const FString&)
{
	// A 1000 cm loop sampled every 100 cm; the squad near 0 cm.
	TArray<float> Arcs;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Arcs.Add(Index * 100.f);
	}
	const TArray<float> Squad = { 0.f, 50.f };
	TArray<bool> Hidden;
	Hidden.Init(false, 10);
	Hidden[3] = Hidden[4] = Hidden[8] = true;
	// Along the loop: 400 cm is 350 from the nearest (50), 800 cm is 200 (the short way round to 0).
	TestEqual(TEXT("the hidden point farthest along the loop from the squad"), TutorialRules::ChooseHiddenRoutePoint(Arcs, Hidden, Squad, 1000.f), 4);
	TestEqual(TEXT("an open route counts straight along (800 is farthest)"), TutorialRules::ChooseHiddenRoutePoint(Arcs, Hidden, Squad, 0.f), 8);
	Hidden.Init(false, 10);
	TestEqual(TEXT("nothing hidden: the farthest point (500 on the loop)"), TutorialRules::ChooseHiddenRoutePoint(Arcs, Hidden, Squad, 1000.f), 5);
	Hidden[1] = true;
	TestEqual(TEXT("a hidden point always beats a seen one, even close"), TutorialRules::ChooseHiddenRoutePoint(Arcs, Hidden, Squad, 1000.f), 1);
	TestEqual(TEXT("no samples: none"), TutorialRules::ChooseHiddenRoutePoint(TArray<float>(), TArray<bool>(), Squad, 1000.f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("loop distance the short way round"), TutorialRules::RouteArcDistance(50.f, 950.f, 1000.f), 100.f);
	TestEqual(TEXT("open route distance straight"), TutorialRules::RouteArcDistance(50.f, 950.f, 0.f), 900.f);
	return true;
}

TUTORIAL_A4_QUIET_TEST(FTutorialA4QuietSchemaTest, "DormantSchema")
bool FTutorialA4QuietSchemaTest::RunTest(const FString&)
{
	using namespace TutorialA4QuietTestData;
	FTutorialStepsData Data;
	if (!LoadQuietDemo(*this, Data))
	{
		return false;
	}
	const FTutorialEnemyBinding* Perimeter = Data.FindEnemy(TEXT("A4_PERIMETER"));
	TestTrue(TEXT("zombie #1 is dormant until the squad is in cover at the overlook, waking hidden on its route"), Perimeter && Perimeter->Dormant.IsSet()
		&& Perimeter->Dormant.SpawnsHidden() && Perimeter->Dormant.Until.Num() == 1
		&& Perimeter->Dormant.Until[0].Stat == FName(TEXT("squad_not_in_cover_at.VOL_A4_OVERLOOK")) && !Perimeter->Dormant.Until[0].bAtLeast
		&& Perimeter->Dormant.Until[0].Value == 1.f);
	TestFalse(TEXT("zombie #2 is not dormant"), Data.FindEnemy(TEXT("A4_LURE"))->Dormant.IsSet());
	TestTrue(TEXT("the new stats are known"), TutorialRules::IsKnownStat(TEXT("squad_not_in_cover_at.VOL_X")) && TutorialRules::IsKnownStat(TEXT("squad_mines"))
		&& TutorialRules::ParseNotInCoverAtStat(TEXT("squad_not_in_cover_at.VOL_X")) == FName(TEXT("VOL_X")));

	FTutorialStepsData Bad;
	TArray<FString> Warnings;
	FString Error;
	TestFalse(TEXT("dormant without 'until' fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R", "dormant": { } } ],
		"steps": [ { "id": "s", "text_id": "T", "complete": { "event": "E" } } ] })JSON"), Bad, Warnings, Error));
	TestFalse(TEXT("an unknown spawn fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R",
		"dormant": { "until": [ { "stat": "in_fight", "below": 1 } ], "spawn": "sky" } } ], "steps": [ { "id": "s", "text_id": "T", "complete": { "event": "E" } } ] })JSON"),
		Bad, Warnings, Error));
	TestTrue(TEXT("in_place parses"), TutorialRules::ParseSteps(TEXT(R"JSON({ "enemies": [ { "id": "Z", "route": "R",
		"dormant": { "until": [ { "stat": "in_fight", "below": 1 } ], "spawn": "in_place" } } ], "steps": [ { "id": "s", "text_id": "T", "complete": { "event": "E" } } ] })JSON"),
		Bad, Warnings, Error) && Bad.Enemies[0].Dormant.IsSet() && !Bad.Enemies[0].Dormant.SpawnsHidden());
	TestFalse(TEXT("an unknown confine free fails"), TutorialRules::ParseSteps(TEXT(R"JSON({ "steps": [ { "id": "s", "text_id": "T", "complete": { "event": "E" },
		"confine": { "volume": "V", "free": "everyone" } } ] })JSON"), Bad, Warnings, Error));
	return true;
}

TUTORIAL_A4_QUIET_TEST(FTutorialA4QuietCardTextTest, "CardNamesTheMineOnlyWithMines")
bool FTutorialA4QuietCardTextTest::RunTest(const FString&)
{
	const FTutorialTextView Text = FNarrativeManifest::Get().GetTutorialText(TEXT("TUT_A4_TRIPWIRE"));
	TestTrue(TEXT("the card: TAKE HIM QUIETLY, Solo, ONE operative, the others stay in cover"), Text.Title.Contains(TEXT("QUIETLY"))
		&& Text.Text.Contains(TEXT("{key:Squad.Solo}")) && Text.Text.Contains(TEXT("ONE operative")) && Text.Text.Contains(TEXT("stay in cover")));
	float Mines = 0.f;
	auto Stat = [&Mines](FName Name) { return Name == FName(TEXT("squad_mines")) ? Mines : 0.f; };
	const FString NoMine = TutorialRules::ResolveIfStatTokens(Text.Text, Stat);
	TestTrue(TEXT("nobody carries a mine: the tripwire only"), NoMine.Contains(TEXT("tripwire")) && !NoMine.Contains(TEXT("mine")) && !NoMine.Contains(TEXT("{if:")));
	Mines = 1.f;
	const FString WithMine = TutorialRules::ResolveIfStatTokens(Text.Text, Stat);
	TestTrue(TEXT("a mine in the kit: tripwire OR mine"), WithMine.Contains(TEXT("or plant a mine")) && !WithMine.Contains(TEXT("{if:")));
	TestEqual(TEXT("token syntax"), TutorialRules::ResolveIfStatTokens(TEXT("a{if:x: b}c{if:y: d}"), [](FName N) { return N == FName(TEXT("x")) ? 2.f : 0.f; }),
		FString(TEXT("a bc")));
	return true;
}

TUTORIAL_A4_QUIET_TEST(FTutorialA4QuietFlowTest, "AnyOneOperativeTrapKill")
bool FTutorialA4QuietFlowTest::RunTest(const FString&)
{
	using namespace TutorialA4QuietTestData;
	FTutorialStepsData Data;
	if (!LoadQuietDemo(*this, Data))
	{
		return false;
	}
	for (const bool bByMine : { false, true })
	{
		const TCHAR* Way = bByMine ? TEXT("mine") : TEXT("tripwire");
		FTutorialStateMachine Machine;
		Machine.EnemyFilterProvider = [](const FTutorialEnemyFilter& Filter, FName Subject)
		{
			return Subject == FName(TEXT("Z1")) ? Filter.Enemy == FName(TEXT("A4_PERIMETER")) || Filter.Enemy == FName(TEXT("A4_ANY"))
				: Subject == FName(TEXT("Z2")) && (Filter.Enemy == FName(TEXT("A4_LURE")) || Filter.Enemy == FName(TEXT("A4_ANY")));
		};
		Machine.Load(Data);
		Machine.StartBeat(TEXT("A0"));
		Machine.StartBeat(TEXT("A4"));
		SettleQuiet(Machine);
		Machine.HandleCommand(PlayerCommands::ToggleVisionCones);
		Machine.HandleCommand(PlayerCommands::PostureDefensive);
		SettleQuiet(Machine);
		TestNotEqual(FString::Printf(TEXT("%s: no part 1 card before zombie #1 wakes"), Way), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
		Machine.HandleEvent(TEXT("EnemyWoke.A4_PERIMETER"));
		SettleQuiet(Machine);
		TestEqual(FString::Printf(TEXT("%s: he woke - TAKE HIM QUIETLY"), Way), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Active);
		TestTrue(FString::Printf(TEXT("%s: both traps unlocked"), Way), !Machine.IsCommandLocked(PlayerCommands::DeployTripwire)
			&& !Machine.IsCommandLocked(PlayerCommands::DeployMine) && !Machine.IsCommandLocked(PlayerCommands::SoloOn));
		// Any ONE operative: the Engineer (not the Medic-Sapper, not the Commander) is sent out first and takes it; the others are held.
		FTutorialConfineView Hold;
		TestTrue(FString::Printf(TEXT("%s: held, nobody chosen"), Way), Machine.GetActiveConfine(Hold) && Hold.FreeOperative.IsNone()
			&& TutorialRules::JudgeConfinedMove(Hold, TEXT("Engineer"), TEXT("crouch"), false) == ETutorialConfineVerdict::Allowed);
		Machine.ClaimConfineFree(TEXT("Engineer"));
		Machine.GetActiveConfine(Hold);
		TestTrue(FString::Printf(TEXT("%s: the Engineer goes, the Commander and the Medic-Sapper stay"), Way),
			TutorialRules::JudgeConfinedMove(Hold, TEXT("Engineer"), TEXT("stand"), false) == ETutorialConfineVerdict::Allowed
			&& TutorialRules::JudgeConfinedMove(Hold, TEXT("Commander"), TEXT("stand"), false) == ETutorialConfineVerdict::HeldBack
			&& TutorialRules::JudgeConfinedMove(Hold, TEXT("Medic"), TEXT("prone"), false) == ETutorialConfineVerdict::HeldBack);
		Machine.HandleCommand(bByMine ? PlayerCommands::DeployMine : PlayerCommands::DeployTripwire);
		SettleQuiet(Machine);
		TestEqual(FString::Printf(TEXT("%s: placed - the card is done"), Way), Machine.GetState(TEXT("A4_tripwire")), ETutorialStepState::Completed);
		TestEqual(FString::Printf(TEXT("%s: the wait"), Way), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
		Machine.HandleEvent(TEXT("EnemyKilled.Trap"), TEXT("Z1")); // a PMC trap - not the squad's
		TestEqual(FString::Printf(TEXT("%s: a PMC trap kill is not the lesson"), Way), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
		Machine.HandleEvent(TEXT("EnemyKilled.SquadTrap"), TEXT("Z2"));
		TestEqual(FString::Printf(TEXT("%s: zombie #2 killed by the trap does not count"), Way), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Active);
		Machine.HandleEvent(TEXT("EnemyKilled.SquadTrap"), TEXT("Z1"));
		TestEqual(FString::Printf(TEXT("%s: zombie #1 killed by the squad's trap - part 1 done"), Way), Machine.GetState(TEXT("A4_trap_wait")), ETutorialStepState::Completed);
		TestTrue(FString::Printf(TEXT("%s: 'that's one'"), Way), TutorialRules::IsRunning(Machine.GetState(TEXT("A4_wt_trap")))
			|| TutorialRules::IsFinished(Machine.GetState(TEXT("A4_wt_trap"))));
		SettleQuiet(Machine);
		TestEqual(FString::Printf(TEXT("%s: part 2 (the wall card)"), Way), Machine.GetState(TEXT("A4_hug_wall")), ETutorialStepState::Active);
	}
	return true;
}

#endif
