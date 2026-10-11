#include "Misc/AutomationTest.h"
#include "Combat/HordeIntroRules.h"
#include "Core/LevelFlowRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sudden horde start (user decision 2026-10-11; UE-only): the wave spawns at once into a FREE, untimed tactical pause
// (no charge spent), a camera flyover shows the horde wide and then every enemy type not yet shown in this fight.

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
	TestTrue(TEXT("default delay within 1.5 - 2 s"), Config.FlyoverDelaySeconds >= 1.5f && Config.FlyoverDelaySeconds <= 2.f);
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

	// Wave 2 with the same types: a short wide shot only (where the new wave comes from), then back.
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

HORDEINTRO_TEST(FHordeIntroTimelineTest, "Timeline.DelayThenShotsOnRealTime")
bool FHordeIntroTimelineTest::RunTest(const FString&)
{
	FHordeIntroConfig Config;
	Config.FlyoverDelaySeconds = 1.75f;
	const TArray<FHordeIntroShot> Shots = { Shot(EHordeIntroShotKind::Wide, EEnemyArchetype::Base, 3.f),
		Shot(EHordeIntroShotKind::Type, EEnemyArchetype::FrostHound, 2.5f), Shot(EHordeIntroShotKind::Return, EEnemyArchetype::Base, 1.f) };
	FHordeIntroCursor Cursor = HordeIntroRules::Evaluate(Shots, Config, 1.f);
	TestTrue(TEXT("1 s after the spawn: still waiting"), Cursor.bWaiting && !Cursor.bFinished && Cursor.ShotIndex == INDEX_NONE);
	Cursor = HordeIntroRules::Evaluate(Shots, Config, 1.8f);
	TestTrue(TEXT("1.8 s: the wide shot"), !Cursor.bWaiting && Cursor.ShotIndex == 0);
	TestEqual(TEXT("0.05 s into it"), Cursor.ShotSeconds, 0.05f, 0.001f);
	TestEqual(TEXT("4.75 s: the hound"), HordeIntroRules::Evaluate(Shots, Config, 4.75f).ShotIndex, 1);
	TestEqual(TEXT("7.3 s: the return"), HordeIntroRules::Evaluate(Shots, Config, 7.3f).ShotIndex, 2);
	TestTrue(TEXT("8.25 s: over"), HordeIntroRules::Evaluate(Shots, Config, 8.25f).bFinished);
	TestTrue(TEXT("no shots: over at once"), HordeIntroRules::Evaluate({}, Config, 0.f).bFinished);
	return true;
}

HORDEINTRO_TEST(FHordeIntroFramingTest, "Framing.WideShotAndRepresentative")
bool FHordeIntroFramingTest::RunTest(const FString&)
{
	const FHordeIntroConfig Config;
	const FVector Squad(0.f, 0.f, 0.f);
	const FVector Horde(4000.f, 0.f, 0.f);
	TestEqual(TEXT("focus 60 % towards the horde"), HordeIntroRules::WideFocus(Squad, Horde, Config), FVector(2400.f, 0.f, 0.f));
	const FVector Focus(2400.f, 0.f, 0.f);
	TestEqual(TEXT("close horde: the minimum distance"), HordeIntroRules::WideDistance(Focus, { FVector(2600.f, 0.f, 0.f) }, Config),
		Config.WideMinDistance);
	TestEqual(TEXT("spread horde: span x 0.9 + 10 m"), HordeIntroRules::WideDistance(Focus, { Squad, FVector(5400.f, 0.f, 0.f) }, Config),
		3000.f * 0.9f + 1000.f, 0.1f);
	TestEqual(TEXT("huge span: clamped"), HordeIntroRules::WideDistance(Focus, { FVector(20000.f, 0.f, 0.f) }, Config), Config.WideMaxDistance);
	TestEqual(TEXT("the representative is the one nearest the squad"),
		HordeIntroRules::PickRepresentative({ FVector(3000.f, 0.f, 0.f), FVector(1500.f, 500.f, 0.f), FVector(2000.f, 0.f, 0.f) }, Squad), 1);
	TestEqual(TEXT("none without candidates"), HordeIntroRules::PickRepresentative({}, Squad), static_cast<int32>(INDEX_NONE));
	return true;
}

HORDEINTRO_TEST(FHordeIntroFreePauseTest, "FreePause.SuddenStartOpensFreeUntimedPause")
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
	TestEqual(TEXT("tactical pause"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestTrue(TEXT("the free pause"), Machine.IsFreePause());
	TestEqual(TEXT("wave 1"), Machine.GetWaveIndex(), 1);
	TestEqual(TEXT("pause dilation"), Machine.GetTimeDilation(), Config.TacticalPauseTimeDilation);
	TestEqual(TEXT("no charge spent (3/3)"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestTrue(TEXT("a wave is active"), Machine.IsWaveActive());
	// No timer: the normal pause would have ended after 30 s.
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		Machine.Tick(0.1f);
	}
	TestTrue(TEXT("still in the free pause after 120 s"), Machine.IsFreePause() && Machine.GetCombatMode() == ECodexCombatMode::TacticalPause);
	int32 Releases = 0;
	Machine.OnTacticalPauseReleased.AddLambda([&Releases]() { ++Releases; });
	TestEqual(TEXT("Space releases it"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("the planned orders run"), Releases, 1);
	TestEqual(TEXT("charges still 3/3"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("no cooldown"), Machine.GetPauseCooldownRemaining(), 0.f);
	TestFalse(TEXT("no longer free"), Machine.IsFreePause());
	// The next pause is a normal one: a charge and the timer.
	Machine.ToggleTacticalPause();
	TestEqual(TEXT("normal pause spends a charge"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges - 1);
	TestFalse(TEXT("normal pause is not free"), Machine.IsFreePause());
	TestEqual(TEXT("normal pause has its timer"), Machine.GetPauseTimeRemaining(), Config.TacticalPauseDuration);
	return true;
}

HORDEINTRO_TEST(FHordeIntroNextWaveTest, "FreePause.NextWaveComesSuddenlyWithoutRest")
bool FHordeIntroNextWaveTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	Machine.TriggerCombatZone();
	Machine.ToggleTacticalPause();
	Machine.ToggleTacticalPause(); // a normal pause: 2/3
	TestEqual(TEXT("wave cleared from a pause"), Machine.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("next wave"), Machine.AdvanceAfterWave(), EGameFlowResult::Ok);
	TestEqual(TEXT("wave 2 at once"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("wave index 2"), Machine.GetWaveIndex(), 2);
	TestTrue(TEXT("free pause again"), Machine.IsFreePause());
	TestEqual(TEXT("charges back to 3/3"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("no rest countdown"), Machine.GetPreparationTimeRemaining(), 0.f);
	// Turn-based is reachable from the free pause (hold), the charges stay.
	TestEqual(TEXT("hold -> turn-based"), Machine.RequestEnterTurnBased(true), EGameFlowResult::Ok);
	TestEqual(TEXT("turn-based"), Machine.GetCombatMode(), ECodexCombatMode::TurnBased);
	TestEqual(TEXT("charges untouched"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestEqual(TEXT("no cooldown either"), Machine.GetPauseCooldownRemaining(), 0.f);
	TestFalse(TEXT("not free in the grid"), Machine.IsFreePause());
	Machine.ExitTurnBasedToRealTime();
	TestEqual(TEXT("back in real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	// The game over / wave end clear the free flag.
	Machine.NotifyWaveCleared();
	Machine.AdvanceAfterWave();
	TestTrue(TEXT("wave 3 free"), Machine.IsFreePause() && Machine.GetWaveIndex() == 3);
	Machine.TriggerGameOver();
	TestFalse(TEXT("game over: no free pause"), Machine.IsFreePause());
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
	TestTrue(TEXT("wave 1 in the free pause"), Machine.IsFreePause());
	Machine.NotifyWaveCleared();
	Machine.AdvanceAfterWave();
	TestEqual(TEXT("wave 2: the rest countdown"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("rest seconds"), Machine.GetPreparationTimeRemaining(), Config.WaveRestDuration);
	TestEqual(TEXT("rest ends"), Machine.FinishPreparation(), EGameFlowResult::Ok);
	TestEqual(TEXT("wave 2 in real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestFalse(TEXT("not free"), Machine.IsFreePause());

	// Console override 0: the old start everywhere (cutscene + preparation).
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
	// The pre-combat autosave stores the wave's preparation (SaveGameSubsystem); the load restores it ...
	Machine.RestoreForLoad(true, true, 2, 15.f);
	TestEqual(TEXT("restored in the preparation"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	// ... and the next flow tick brings the horde, in the free pause.
	Machine.Tick(0.016f);
	TestEqual(TEXT("wave 2 comes at once"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestTrue(TEXT("free pause"), Machine.IsFreePause());
	TestEqual(TEXT("wave index kept"), Machine.GetWaveIndex(), 2);
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

#endif // WITH_DEV_AUTOMATION_TESTS
