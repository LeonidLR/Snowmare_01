#include "Misc/AutomationTest.h"
#include "Combat/HordeIntroRules.h"
#include "Core/LevelFlowRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "GameFlow/PauseOrderRules.h"
#include "Interactables/RelocationRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sudden horde start (user decisions / feedback 2026-10-11; UE-only): the wave spawns at once into the running fight;
// once the horde has become a running mass the FREE, untimed tactical pause opens (no charge spent) and a slow-motion
// camera flyover shows the charging mass, then a running front-runner of every type not yet shown in this fight.

#define HORDEINTRO_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.HordeIntro." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HordeIntroTest
{
	FHordeIntroShot Shot(EHordeIntroShotKind Kind, EEnemyArchetype Type, float Seconds)
	{
		FHordeIntroShot Result;
		Result.Kind = Kind;
		Result.Archetype = Type;
		Result.Seconds = Seconds;
		return Result;
	}

	FString Labels(const TArray<FHordeIntroShot>& Shots)
	{
		FString Text;
		for (const FHordeIntroShot& Each : Shots)
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT(" ")) + HordeIntroRules::ShotLabel(Each);
		}
		return Text;
	}
}

using namespace HordeIntroTest;

HORDEINTRO_TEST(FHordeIntroFirstWaveShotsTest, "ShotList.WideThenEachNewTypeThenReturn")
bool FHordeIntroFirstWaveShotsTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	const TArray<EEnemyArchetype> Spawned = { EEnemyArchetype::FrostHound, EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten,
		EEnemyArchetype::FrostHound, EEnemyArchetype::Brute };
	const TArray<EEnemyArchetype> Types = HordeIntroRules::UniqueTypesInOrder(Spawned);
	TestEqual(TEXT("unique types in spawn order"), Types.Num(), 3);
	const TArray<FHordeIntroShot> Shots = HordeIntroRules::BuildShotList(Types, TSet<EEnemyArchetype>(), Config);
	TestEqual(TEXT("wide, hound, frostbitten, brute, return"), Labels(Shots),
		FString(TEXT("wide type:FrostHound type:Frostbitten type:Brute return")));
	TestEqual(TEXT("wide shot length"), Shots[0].Seconds, Config.WideShotSeconds);
	TestEqual(TEXT("type shot length"), Shots[1].Seconds, Config.TypeShotSeconds);
	TestEqual(TEXT("return glide"), Shots.Last().Seconds, Config.ReturnBlendSeconds);
	TestEqual(TEXT("total 3 + 3 x 2.5 + 1 s"), HordeIntroRules::GetShotsSeconds(Shots), 11.5f, 0.001f);
	// User feedback 2026-10-11: dramatic slow motion (not the 0.02 pause), close tracking shots, a wait for the mass.
	TestTrue(TEXT("flyover slow motion between the pause and real time"), Config.FlyoverTimeDilation > 0.1f && Config.FlyoverTimeDilation < 0.5f);
	TestTrue(TEXT("type shots close (6-8 m)"), Config.TypeShotDistance >= 600.f && Config.TypeShotDistance <= 800.f);
	TestTrue(TEXT("the mass gets 2.5..6 s"), FMath::IsNearlyEqual(Config.MinDelaySeconds, 2.5f) && FMath::IsNearlyEqual(Config.MaxWaitSeconds, 6.f));
	return true;
}

HORDEINTRO_TEST(FHordeIntroShownTypesTest, "ShotList.OnlyTypesNotShownInThisFight")
bool FHordeIntroShownTypesTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	const TSet<EEnemyArchetype> Shown = { EEnemyArchetype::FrostHound };
	const TArray<FHordeIntroShot> Shots = HordeIntroRules::BuildShotList({ EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten }, Shown, Config);
	TestEqual(TEXT("the hound was shown in wave 1: only the frostbitten"), Labels(Shots), FString(TEXT("wide type:Frostbitten return")));
	TestEqual(TEXT("new types keep the full wide shot"), Shots[0].Seconds, Config.WideShotSeconds);

	// Wave 2 with the same types: the wide shot of the running mass only, then back.
	const TSet<EEnemyArchetype> All = { EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten };
	const TArray<FHordeIntroShot> Repeat = HordeIntroRules::BuildShotList({ EEnemyArchetype::Frostbitten, EEnemyArchetype::FrostHound }, All, Config);
	TestEqual(TEXT("known types: short wide + return"), Labels(Repeat), FString(TEXT("wide return")));
	TestEqual(TEXT("short wide shot"), Repeat[0].Seconds, Config.RepeatWideShotSeconds);

	FHordeIntroConfig NoRepeat = Config;
	NoRepeat.RepeatWideShotSeconds = 0.f;
	TestEqual(TEXT("repeat wide 0: no flyover for known types"), HordeIntroRules::BuildShotList({ EEnemyArchetype::FrostHound }, All, NoRepeat).Num(), 0);
	TestEqual(TEXT("but new types still get theirs"), Labels(HordeIntroRules::BuildShotList({ EEnemyArchetype::Cutter }, All, NoRepeat)),
		FString(TEXT("wide type:Cutter return")));

	FHordeIntroConfig Off = Config;
	Off.bEnabled = false;
	TestEqual(TEXT("flyover off: no shots"), HordeIntroRules::BuildShotList({ EEnemyArchetype::Brute }, TSet<EEnemyArchetype>(), Off).Num(), 0);
	TestEqual(TEXT("no enemies: no shots"), HordeIntroRules::BuildShotList({}, TSet<EEnemyArchetype>(), Config).Num(), 0);

	FHordeIntroConfig Capped = Config;
	Capped.MaxTypeShots = 2;
	TestEqual(TEXT("type shots capped"), Labels(HordeIntroRules::BuildShotList({ EEnemyArchetype::FrostHound, EEnemyArchetype::Spitter,
		EEnemyArchetype::Brute }, TSet<EEnemyArchetype>(), Capped)), FString(TEXT("wide type:FrostHound type:Spitter return")));
	return true;
}

HORDEINTRO_TEST(FHordeIntroTimelineTest, "Timeline.ShotsOnRealTimeFromTheTrigger")
bool FHordeIntroTimelineTest::RunTest(const FString&)
{
	const TArray<FHordeIntroShot> Shots = { Shot(EHordeIntroShotKind::Wide, EEnemyArchetype::Base, 3.f),
		Shot(EHordeIntroShotKind::Type, EEnemyArchetype::FrostHound, 2.5f), Shot(EHordeIntroShotKind::Return, EEnemyArchetype::Base, 1.f) };
	FHordeIntroCursor Cursor = HordeIntroRules::Evaluate(Shots, 0.f);
	TestTrue(TEXT("at the trigger: the wide shot"), !Cursor.bFinished && Cursor.ShotIndex == 0);
	Cursor = HordeIntroRules::Evaluate(Shots, 0.05f);
	TestEqual(TEXT("0.05 s into it"), Cursor.ShotSeconds, 0.05f, 0.001f);
	TestEqual(TEXT("3 s: the hound"), HordeIntroRules::Evaluate(Shots, 3.f).ShotIndex, 1);
	TestEqual(TEXT("5.6 s: the return"), HordeIntroRules::Evaluate(Shots, 5.6f).ShotIndex, 2);
	TestTrue(TEXT("6.5 s: over"), HordeIntroRules::Evaluate(Shots, 6.5f).bFinished);
	TestTrue(TEXT("no shots: over at once"), HordeIntroRules::Evaluate({}, 0.f).bFinished);
	return true;
}

HORDEINTRO_TEST(FHordeIntroTriggerTest, "Trigger.RunningMassAfterMinDelayElseTimeout")
bool FHordeIntroTriggerTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	const FVector Squad(0.f, 0.f, 0.f);
	FHordeMover Runner;
	Runner.Location = FVector(3000.f, 0.f, 0.f);
	Runner.Velocity = FVector(-600.f, 100.f, 0.f);
	FHordeMover Idle;
	Idle.Location = FVector(3000.f, 500.f, 0.f);
	FHordeMover Away;
	Away.Location = FVector(3000.f, -500.f, 0.f);
	Away.Velocity = FVector(600.f, 0.f, 0.f);
	FHordeMover Slow = Runner;
	Slow.Velocity = FVector(-150.f, 0.f, 0.f);
	TestTrue(TEXT("6 m/s at the squad runs"), HordeIntroRules::IsRunningAtSquad(Runner, Squad, Config.MassMinSpeed));
	TestFalse(TEXT("standing does not"), HordeIntroRules::IsRunningAtSquad(Idle, Squad, Config.MassMinSpeed));
	TestFalse(TEXT("running away does not"), HordeIntroRules::IsRunningAtSquad(Away, Squad, Config.MassMinSpeed));
	TestFalse(TEXT("1.5 m/s is too slow"), HordeIntroRules::IsRunningAtSquad(Slow, Squad, Config.MassMinSpeed));
	TestEqual(TEXT("3 of 5 run: 60 %"), HordeIntroRules::RunningFraction({ Runner, Runner, Runner, Idle, Away }, Squad, Config.MassMinSpeed), 0.6f, 0.001f);
	TestEqual(TEXT("no movers: 0"), HordeIntroRules::RunningFraction({}, Squad, Config.MassMinSpeed), 0.f);
	TestFalse(TEXT("0.2 s of running is no mass yet"), HordeIntroRules::IsMassFormed(0.2f, Config));
	TestTrue(TEXT("0.4 s of running forms the mass"), HordeIntroRules::IsMassFormed(Config.MassHoldSeconds, Config));
	TestFalse(TEXT("the mass formed, but too early (1 s)"), HordeIntroRules::ShouldTrigger(1.f, true, Config));
	TestTrue(TEXT("2.5 s with the mass formed (latched)"), HordeIntroRules::ShouldTrigger(2.5f, true, Config));
	TestFalse(TEXT("5.9 s, no mass"), HordeIntroRules::ShouldTrigger(5.9f, false, Config));
	TestTrue(TEXT("6 s: timeout"), HordeIntroRules::ShouldTrigger(6.f, false, Config));
	return true;
}

HORDEINTRO_TEST(FHordeIntroFramingTest, "Framing.FromTheSquadSideOnTheFrontRunner")
bool FHordeIntroFramingTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	const FVector Squad(0.f, 0.f, 0.f);
	const FVector Horde(4000.f, 0.f, 0.f);
	TestEqual(TEXT("focus 80 % towards the mass"), HordeIntroRules::WideFocus(Squad, Horde, Config), FVector(3200.f, 0.f, 0.f));
	const FVector Focus(3200.f, 0.f, 0.f);
	TestEqual(TEXT("tight mass: the minimum distance"), HordeIntroRules::WideDistance(Focus, { FVector(3400.f, 0.f, 0.f) }, Config),
		Config.WideMinDistance);
	TestEqual(TEXT("spread mass: span x 0.9 + 8 m"), HordeIntroRules::WideDistance(Focus, { FVector(5200.f, 0.f, 0.f) }, Config),
		2000.f * 0.9f + 800.f, 0.1f);
	TestEqual(TEXT("huge span: clamped"), HordeIntroRules::WideDistance(Focus, { FVector(20000.f, 0.f, 0.f) }, Config), Config.WideMaxDistance);
	TestEqual(TEXT("view from the squad towards a horde in +X, 25 deg aside"), HordeIntroRules::ViewYawAt(Squad, Horde, 25.f), 25.f, 0.01f);
	TestEqual(TEXT("horde in +Y"), HordeIntroRules::ViewYawAt(Squad, FVector(0.f, 3000.f, 0.f), 0.f), 90.f, 0.01f);
	FHordeMover Near;
	Near.Location = FVector(1500.f, 0.f, 0.f); // nearest, but standing
	FHordeMover Front;
	Front.Location = FVector(2000.f, 0.f, 0.f);
	Front.Velocity = FVector(-500.f, 0.f, 0.f);
	FHordeMover Back = Front;
	Back.Location = FVector(3500.f, 0.f, 0.f);
	TestEqual(TEXT("the representative is the running front-runner"),
		HordeIntroRules::PickRunningRepresentative({ Back, Near, Front }, Squad, Config.MassMinSpeed), 2);
	FHordeMover Flanker = Back;
	Flanker.Velocity = FVector(0.f, 300.f, 0.f); // moves, but not at the squad
	TestEqual(TEXT("nobody runs at the squad: the nearest that moves"), HordeIntroRules::PickRunningRepresentative({ Near, Flanker }, Squad, Config.MassMinSpeed), 1);
	TestEqual(TEXT("nobody moves: the nearest"), HordeIntroRules::PickRunningRepresentative({ Back, Near }, Squad, 100000.f), 1);
	TestEqual(TEXT("none without candidates"), HordeIntroRules::PickRunningRepresentative({}, Squad, Config.MassMinSpeed),
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("alert fades in"), HordeIntroRules::AlertAlpha(0.25f, 3.f, Config), 0.5f, 0.01f);
	TestEqual(TEXT("alert full"), HordeIntroRules::AlertAlpha(2.f, 3.f, Config), 1.f);
	TestEqual(TEXT("alert gone"), HordeIntroRules::AlertAlpha(3.6f, 3.f, Config), 0.f);
	return true;
}

HORDEINTRO_TEST(FHordeIntroFreePauseTest, "FreePause.SuddenWaveRunsThenIntroOpensFreeUntimedPause")
bool FHordeIntroFreePauseTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	TArray<ECodexCombatMode> Modes;
	Machine.OnStateChanged.AddLambda([&Modes](ECodexGamePhase, ECodexCombatMode Mode) { Modes.Add(Mode); });
	TestTrue(TEXT("default: sudden"), Machine.UsesSuddenStart(1));
	TestEqual(TEXT("combat zone"), Machine.TriggerCombatZone(), EGameFlowResult::Ok);
	TestEqual(TEXT("straight into the wave (no cutscene / preparation)"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("one state change"), Modes.Num(), 1);
	TestEqual(TEXT("the fight runs: real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("real time dilation"), Machine.GetTimeDilation(), 1.f);
	TestTrue(TEXT("the horde intro is armed"), Machine.IsAwaitingHordeIntro());
	TestEqual(TEXT("wave 1"), Machine.GetWaveIndex(), 1);
	TestEqual(TEXT("free pause opens"), Machine.EnterFreeTacticalPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("the free pause"), Machine.IsFreePause() && Machine.GetCombatMode() == ECodexCombatMode::TacticalPause);
	TestFalse(TEXT("intro no longer armed"), Machine.IsAwaitingHordeIntro());
	TestEqual(TEXT("pause dilation"), Machine.GetTimeDilation(), Config.TacticalPauseTimeDilation);
	TestEqual(TEXT("no charge spent (3/3)"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("only once"), Machine.EnterFreeTacticalPause(), EGameFlowResult::NotInRealTime);
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		Machine.Tick(0.1f);
	}
	TestTrue(TEXT("still in the free pause after 120 s (no timer)"), Machine.IsFreePause());
	int32 Releases = 0;
	Machine.OnTacticalPauseReleased.AddLambda([&Releases]() { ++Releases; });
	TestEqual(TEXT("Space releases it"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("the planned orders run"), Releases, 1);
	TestEqual(TEXT("charges still 3/3"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("no cooldown"), Machine.GetPauseCooldownRemaining(), 0.f);
	TestEqual(TEXT("no free pause outside the intro"), Machine.EnterFreeTacticalPause(), EGameFlowResult::WrongPhase);
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("the next pause spends a charge"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges - 1);
	TestEqual(TEXT("and has its timer"), Machine.GetPauseTimeRemaining(), Config.TacticalPauseDuration);
	return true;
}

HORDEINTRO_TEST(FHordeIntroSpaceBeforeTriggerTest, "FreePause.SpaceBeforeTheTriggerOpensItFree")
bool FHordeIntroSpaceBeforeTriggerTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	FGameFlowStateMachine Machine(Config);
	Machine.TriggerCombatZone();
	TestEqual(TEXT("Space tap while the horde forms"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("the free pause, not a charged one"), Machine.IsFreePause());
	TestEqual(TEXT("charges 3/3"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	FGameFlowStateMachine Hold(Config);
	Hold.TriggerCombatZone();
	TestEqual(TEXT("hold -> turn-based"), Hold.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	TestFalse(TEXT("intro dropped"), Hold.IsAwaitingHordeIntro());
	Hold.ExitTurnBasedToRealTime();
	TestFalse(TEXT("still dropped back in real time"), Hold.IsAwaitingHordeIntro());
	FGameFlowStateMachine Dismissed(Config);
	Dismissed.TriggerCombatZone();
	Dismissed.CancelHordeIntro();
	TestFalse(TEXT("dismissed"), Dismissed.IsAwaitingHordeIntro());
	Dismissed.ToggleTacticalPause();
	TestTrue(TEXT("a normal pause"), !Dismissed.IsFreePause() && Dismissed.GetPauseCharges() == Config.TacticalPauseMaxCharges - 1);
	return true;
}

HORDEINTRO_TEST(FHordeIntroNextWaveTest, "FreePause.NextWaveComesSuddenlyAfterTheRest")
bool FHordeIntroNextWaveTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	Machine.TriggerCombatZone();
	Machine.EnterFreeTacticalPause();
	Machine.ToggleTacticalPause();
	Machine.ToggleTacticalPause(); // a normal pause: 2/3
	TestEqual(TEXT("wave cleared from a pause"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	// Game designer 2026-10-25: no "Start the next wave" panel - the rest runs, then the wave breaks through on its own.
	TestTrue(TEXT("the rest before wave 2 (no panel)"), Machine.IsRestBetweenWaves() && Machine.GetWaveIndex() == 2);
	TestEqual(TEXT("the world runs in the rest"), Machine.GetTimeDilation(), 1.f);
	TestEqual(TEXT("charges back to 3/3"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("the rest countdown"), Machine.GetPreparationTimeRemaining(), Config.WaveRestDuration);
	for (float Time = 0.f; Time < Config.WaveRestDuration - 0.5f; Time += 0.25f)
	{
		Machine.Tick(0.25f);
	}
	TestEqual(TEXT("a sudden wave waits for the rest"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	Machine.Tick(1.f);
	TestEqual(TEXT("wave 2 comes by itself"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestTrue(TEXT("its horde intro armed, real time"), Machine.IsAwaitingHordeIntro() && Machine.GetWaveIndex() == 2);
	Machine.EnterFreeTacticalPause();
	TestEqual(TEXT("hold -> turn-based from the free pause"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	TestEqual(TEXT("charges untouched"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("no cooldown either"), Machine.GetPauseCooldownRemaining(), 0.f);
	TestFalse(TEXT("not free in the grid"), Machine.IsFreePause());
	Machine.ExitTurnBasedToRealTime();
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("the bot / smokes may cut the rest short"), Machine.FinishPreparation(), EGameFlowResult::Ok);
	TestTrue(TEXT("wave 3 armed"), Machine.IsAwaitingHordeIntro() && Machine.GetWaveIndex() == 3);
	Machine.TriggerGameOver();
	TestFalse(TEXT("game over: nothing armed"), Machine.IsAwaitingHordeIntro() || Machine.IsFreePause());
	return true;
}

HORDEINTRO_TEST(FHordeIntroPreparedWaveTest, "FreePause.PreparedWavesAndOverrideKeepTheOldStart")
bool FHordeIntroPreparedWaveTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	Config.PreparedWaveIndices = { 2 };
	FGameFlowStateMachine Machine(Config);
	TestTrue(TEXT("wave 1 sudden"), Machine.UsesSuddenStart(1));
	TestFalse(TEXT("wave 2 prepared"), Machine.UsesSuddenStart(2));
	Machine.TriggerCombatZone();
	TestTrue(TEXT("wave 1 armed"), Machine.IsAwaitingHordeIntro());
	Machine.NotifyWaveCleared();
	TestEqual(TEXT("wave 2: the rest countdown"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("rest seconds"), Machine.GetPreparationTimeRemaining(), Config.WaveRestDuration);
	TestFalse(TEXT("no Start Battle in the rest"), Machine.OffersStartBattle());
	Machine.Tick(Config.WaveRestDuration + 0.1f);
	TestEqual(TEXT("the rest ran out: wave 2 in real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestFalse(TEXT("no horde intro"), Machine.IsAwaitingHordeIntro());
	FGameFlowStateMachine Old(Config);
	Old.SetSuddenStartOverride(0);
	Old.TriggerCombatZone();
	TestEqual(TEXT("override 0: cutscene"), Old.GetPhase(), ECodexGamePhase::Cutscene);
	FGameFlowConfig Off;
	Off.bSuddenWaveStart = false;
	FGameFlowStateMachine Forced(Off);
	Forced.SetSuddenStartOverride(1);
	TestTrue(TEXT("override 1 beats the data"), Forced.UsesSuddenStart(1));
	return true;
}

HORDEINTRO_TEST(FHordeIntroLoadTest, "FreePause.LoadedPreparationStartsTheWaveSuddenly")
bool FHordeIntroLoadTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	// A save from the fight / after it (no rest seconds): the wave comes at once.
	Machine.RestoreForLoad(true, true, 2);
	TestEqual(TEXT("restored in the preparation"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	Machine.Tick(0.016f);
	TestEqual(TEXT("wave 2 comes at once"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestTrue(TEXT("its horde intro armed"), Machine.IsAwaitingHordeIntro());
	TestEqual(TEXT("wave index kept"), Machine.GetWaveIndex(), 2);
	// 2026-10-25: a save from the rest between waves resumes that rest, then the wave comes by itself.
	FGameFlowStateMachine Rest(Config);
	Rest.RestoreForLoad(true, true, 2, 12.f);
	Rest.Tick(0.016f);
	TestTrue(TEXT("the saved rest resumes (12 s)"), Rest.IsRestBetweenWaves() && FMath::IsNearlyEqual(Rest.GetPreparationTimeRemaining(), 12.f - 0.016f, 0.01f));
	Rest.Tick(12.f);
	TestTrue(TEXT("... then wave 2 breaks through, horde intro armed"), Rest.GetPhase() == ECodexGamePhase::WaveCombat && Rest.IsAwaitingHordeIntro());
	return true;
}

HORDEINTRO_TEST(FHordeIntroLevelDataTest, "LevelData.PerWaveSuddenStartFlag")
bool FHordeIntroLevelDataTest::RunTest(const FString&)
{
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("parses"), LevelJsonRules::ParseLevel(TEXT(
		"{\"level_id\":\"h\",\"prep_phase_duration\":45,\"wave_rest_duration\":15,\"waves\":["
		"{\"wave_index\":1,\"spawns\":[{\"enemy_type\":\"HOUND\",\"count\":2}]},"
		"{\"wave_index\":2,\"sudden_start\":false,\"spawns\":[{\"enemy_type\":\"BRUTE\",\"count\":1}]},"
		"{\"wave_index\":3,\"sudden_start\":true,\"spawns\":[{\"enemy_type\":\"CUTTER\",\"count\":1}]}]}"), TEXT("h"), Level, Error));
	TestEqual(TEXT("three waves"), Level.Waves.Num(), 3);
	if (Level.Waves.Num() != 3)
	{
		return false;
	}
	TestTrue(TEXT("default sudden"), Level.Waves[0].bSuddenStart);
	TestFalse(TEXT("wave 2 prepared"), Level.Waves[1].bSuddenStart);
	TestTrue(TEXT("wave 3 sudden"), Level.Waves[2].bSuddenStart);
	TestEqual(TEXT("prep time still parsed"), Level.PrepPhaseDuration, 45.f);
	const FGameFlowConfig Flow = LevelFlowRules::ApplyLevel(FGameFlowConfig(), &Level, nullptr);
	TestEqual(TEXT("one prepared wave"), Flow.PreparedWaveIndices.Num(), 1);
	TestTrue(TEXT("it is wave 2"), Flow.PreparedWaveIndices.Contains(2));
	TestTrue(TEXT("sudden by default"), Flow.bSuddenWaveStart);
	TestEqual(TEXT("its rest countdown from the data"), Flow.WaveRestDuration, 15.f);
	return true;
}

HORDEINTRO_TEST(FHordeIntroFreePauseRadiusTest, "FreePauseHasNoRadius")
bool FHordeIntroFreePauseRadiusTest::RunTest(const FString&)
{
	// User decision 2026-10-11: the free pause of the horde start is an unrestricted preparation (no radius ring, no distance
	// limit on move orders / placements); a paid pause (Space during a fight) keeps the 12 m order radius.
	const FGameFlowConfig Config;
	TestTrue(TEXT("a paid pause limits"), PauseOrderRules::IsRadiusLimited(false));
	TestFalse(TEXT("the free pause does not"), PauseOrderRules::IsRadiusLimited(true));
	TestEqual(TEXT("paid pause: the configured radius"), PauseOrderRules::GetOrderRadius(false, Config.PauseOrderRadius), Config.PauseOrderRadius);
	TestTrue(TEXT("free pause: far beyond any map"), PauseOrderRules::GetOrderRadius(true, Config.PauseOrderRadius) > Config.PauseOrderRadius * 100.f);
	const float FreeRadius = PauseOrderRules::GetOrderRadius(true, Config.PauseOrderRadius);
	TestEqual(TEXT("placement in the free pause is unlimited"),
		RelocationRules::GetPlacementRadius(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, FreeRadius, 1500.f), FreeRadius);
	TestTrue(TEXT("a barricade 60 m away is within the free pause"), RelocationRules::IsWithinRadius(FVector::ZeroVector, FVector(6000.f, 0.f, 0.f), FreeRadius));
	TestFalse(TEXT("... and outside a paid pause"), RelocationRules::IsWithinRadius(FVector::ZeroVector, FVector(6000.f, 0.f, 0.f),
		PauseOrderRules::GetOrderRadius(false, Config.PauseOrderRadius)));
	return true;
}

HORDEINTRO_TEST(FHordeIntroFrustumFramingTest, "Framing.WaveClusterInTheFrustum")
bool FHordeIntroFrustumFramingTest::RunTest(const FString&)
{
	// Game designer play-test 2026-10-22: "the fly-in sometimes shows nothing - too far or not close enough".
	const FHordeIntroConfig Config;
	const float Pitch = -35.26f;
	const float Fov = 30.f;
	const float Aspect = 16.f / 9.f;
	// A pack of five within 8 m, 40 m from the squad, and one straggler 60 m off to the side (or a level enemy far away).
	TArray<FVector> Points = { FVector(4000.f, 0.f, 0.f), FVector(4300.f, 300.f, 0.f), FVector(4200.f, -400.f, 0.f), FVector(4600.f, 100.f, 0.f),
		FVector(3900.f, -200.f, 0.f), FVector(4000.f, 6000.f, 0.f) };
	const TArray<int32> Cluster = HordeIntroRules::MainCluster(Points, Config.WideClusterRadiusCm);
	TestEqual(TEXT("the main cluster: the five of the pack"), Cluster.Num(), 5);
	TestFalse(TEXT("... not the straggler 60 m away"), Cluster.Contains(5));
	TestTrue(TEXT("no points: no cluster"), HordeIntroRules::MainCluster(TArray<FVector>(), 1000.f).IsEmpty());
	TArray<FVector> Feet;
	for (const int32 Index : Cluster)
	{
		Feet.Add(Points[Index]);
	}
	const FVector Squad = FVector::ZeroVector;
	const float Yaw = HordeIntroRules::ViewYawAt(Squad, HordeIntroRules::CentreOf(Feet, TArray<int32>()), Config.ViewSideDegrees);
	const HordeIntroRules::FWideFrame Frame = HordeIntroRules::FrameWide(Squad, Feet, Yaw, Pitch, Fov, Aspect, Config);
	const FHordeFrameView View = HordeIntroRules::ViewAt(Frame.Focus, Yaw, Pitch, Frame.Distance, Fov, Aspect);
	bool bAllIn = true;
	float MinFraction = 1.f;
	for (const FVector& Point : Feet)
	{
		bAllIn &= HordeIntroRules::IsInFrame(View, Point, 0.f) && HordeIntroRules::IsInFrame(View, Point + FVector(0.f, 0.f, 160.f), 0.f);
		MinFraction = FMath::Min(MinFraction, HordeIntroRules::ScreenHeightFraction(View, Point, 100.f));
	}
	TestTrue(TEXT("every runner of the pack (feet and head) is in the frame"), bAllIn);
	TestTrue(TEXT("... within the wide distances"), Frame.Distance >= Config.WideMinDistance && Frame.Distance <= Config.WideMaxDistance);
	TestTrue(FString::Printf(TEXT("... a 1 m hound fills >= 2.5 %% of the frame height (%.1f %%)"), MinFraction * 100.f), MinFraction >= 0.025f);
	// A tight pack close to the squad: the biased focus holds it at a short distance.
	const TArray<FVector> Close = { FVector(1500.f, 0.f, 0.f), FVector(1600.f, 150.f, 0.f), FVector(1450.f, -120.f, 0.f) };
	const float CloseYaw = HordeIntroRules::ViewYawAt(Squad, HordeIntroRules::CentreOf(Close, TArray<int32>()), Config.ViewSideDegrees);
	const HordeIntroRules::FWideFrame Near = HordeIntroRules::FrameWide(Squad, Close, CloseYaw, Pitch, Fov, Aspect, Config);
	TestTrue(TEXT("a tight pack near the squad: the squad-side focus, the shortest distance that holds it"), Near.bBiased && Near.Distance < 2500.f);
	// FrameDistance is the shortest that holds: a little closer drops a runner (unless already at the minimum).
	const float D = HordeIntroRules::FrameDistance(Frame.Focus, Feet, Yaw, Pitch, Fov, Aspect, Config);
	TestEqual(TEXT("FrameDistance = the frame's distance"), D, Frame.Distance, 1.f);
	if (D > Config.WideMinDistance + 50.f)
	{
		const FHordeFrameView Closer = HordeIntroRules::ViewAt(Frame.Focus, Yaw, Pitch, D - 150.f, Fov, Aspect);
		bool bCloserHolds = true;
		for (const FVector& Point : Feet)
		{
			bCloserHolds &= HordeIntroRules::IsInFrame(Closer, Point, Config.WideFrameMargin)
				&& HordeIntroRules::IsInFrame(Closer, Point + FVector(0.f, 0.f, Config.FramedBodyHeightCm), Config.WideFrameMargin);
		}
		TestFalse(TEXT("... 1.5 m closer it no longer holds them all (not too far)"), bCloserHolds);
	}
	// Behind the camera is never in the frame.
	TestFalse(TEXT("a point behind the camera"), HordeIntroRules::IsInFrame(View, View.Location - View.Rotation.Vector() * 500.f, 0.f));
	return true;
}

HORDEINTRO_TEST(FHordeIntroWaitSpawnTest, "Trigger.WaitsForTheSpawn")
bool FHordeIntroWaitSpawnTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	TestFalse(TEXT("queued spawns: no flyover to nobody"), HordeIntroRules::ShouldTriggerSpawned(Config.MaxWaitSeconds, true, false, 3, Config));
	TestFalse(TEXT("nobody alive yet: wait"), HordeIntroRules::ShouldTriggerSpawned(Config.MaxWaitSeconds + 1.f, false, true, 0, Config));
	TestTrue(TEXT("all spawned + the mass: as before"), HordeIntroRules::ShouldTriggerSpawned(Config.MinDelaySeconds, true, true, 4, Config));
	TestTrue(TEXT("the grace is over: the flyover anyway"),
		HordeIntroRules::ShouldTriggerSpawned(Config.MaxWaitSeconds + Config.SpawnGraceSeconds, false, false, 0, Config));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
