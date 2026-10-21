#include "Misc/AutomationTest.h"
#include "Core/LevelFlowRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-21 (user play-test of the Demo 01 A4 ambush: "they attack, we kill them and play on - no waves at this stage; remove Susanin"):
// the level JSON "ambush_fight": "skirmish" (a fight of the placed enemies that engaged the squad, straight back to exploration) and
// "susanin_wave" (0 = off), the tutorial's free lesson pause. UE-only (no Godot reference).

#define SKIRMISH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.GameFlow.Skirmish." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SKIRMISH_TEST(FSkirmishLevelJsonTest, "LevelJson")
bool FSkirmishLevelJsonTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	FString Error;
	TestTrue(TEXT("no keys parse"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a"})"), TEXT("a"), Config, Error));
	TestTrue(TEXT("defaults: the whole level fights, Susanin random"), !Config.bAmbushSkirmish && Config.SusaninWave == -1);
	TestTrue(TEXT("skirmish + Susanin off parse"),
		LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","ambush_fight":"skirmish","susanin_wave":0})"), TEXT("a"), Config, Error));
	TestTrue(TEXT("... read"), Config.bAmbushSkirmish && Config.SusaninWave == 0);
	TestTrue(TEXT("level + Susanin on wave 2"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","ambush_fight":"level","susanin_wave":2})"), TEXT("a"), Config, Error)
		&& !Config.bAmbushSkirmish && Config.SusaninWave == 2);
	TestFalse(TEXT("an unknown ambush_fight is an error"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","ambush_fight":"brawl"})"), TEXT("a"), Config, Error));
	TestTrue(TEXT("... naming it"), Error.Contains(TEXT("brawl")));
	FLevelCombatConfig Skirmish;
	Skirmish.bAmbushSkirmish = true;
	TestTrue(TEXT("the flow config carries the skirmish flag"), LevelFlowRules::ApplyLevel(FGameFlowConfig(), &Skirmish, nullptr).bAmbushSkirmish
		&& !LevelFlowRules::ApplyLevel(FGameFlowConfig(), nullptr, nullptr).bAmbushSkirmish);

	// The Demo 01 lake: skirmishes, no Susanin.
	FString Json;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/LevelJson/L_Demo01_A_FrozenLake.json");
	TestTrue(TEXT("the lake's level JSON loads"), FFileHelper::LoadFileToString(Json, *Path));
	FLevelCombatConfig Lake;
	TestTrue(TEXT("... parses"), LevelJsonRules::ParseLevel(Json, TEXT("L_Demo01_A_FrozenLake"), Lake, Error));
	TestTrue(TEXT("... placed-enemy fights are skirmishes, Susanin is off, its 3 scripted waves stay"), Lake.bAmbushSkirmish && Lake.SusaninWave == 0
		&& Lake.Waves.Num() == 3);
	return true;
}

SKIRMISH_TEST(FSkirmishFlowTest, "Flow")
bool FSkirmishFlowTest::RunTest(const FString&)
{
	FGameFlowConfig Config;
	Config.bAmbushSkirmish = true;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine;
	Machine.Reset(Config);
	TestFalse(TEXT("exploring: no skirmish"), Machine.IsSkirmish());
	TestEqual(TEXT("exploring: FinishSkirmish refused"), Machine.FinishSkirmish(), EGameFlowResult::WrongPhase);
	TestEqual(TEXT("exploring: no lesson pause"), Machine.EnterLessonPause(), EGameFlowResult::NotInWave);
	TestEqual(TEXT("a zombie spots the squad: the ambush fight"), Machine.StartAmbushCombat(), EGameFlowResult::Ok);
	TestTrue(TEXT("... a skirmish in real time"), Machine.IsSkirmish() && Machine.GetPhase() == ECodexGamePhase::WaveCombat
		&& Machine.GetCombatMode() == ECodexCombatMode::RealTime);
	const int32 Charges = Machine.GetPauseCharges();
	TestEqual(TEXT("the tutorial's lesson pause"), Machine.EnterLessonPause(), EGameFlowResult::Ok);
	TestTrue(TEXT("... free (no charge, no timer)"), Machine.GetCombatMode() == ECodexCombatMode::TacticalPause && Machine.IsFreePause()
		&& Machine.GetPauseCharges() == Charges);
	Machine.Tick(120.f);
	TestEqual(TEXT("... it does not run out"), Machine.GetCombatMode(), ECodexCombatMode::TacticalPause);
	TestEqual(TEXT("the player resumes (Space)"), Machine.ToggleTacticalPause(), EGameFlowResult::Ok);
	TestEqual(TEXT("... real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("its zombie is down: straight back to exploring"), Machine.FinishSkirmish(), EGameFlowResult::Ok);
	TestTrue(TEXT("... Exploration, no wave, the combat zone free again"), Machine.GetPhase() == ECodexGamePhase::Exploration
		&& Machine.GetWaveIndex() == 0 && !Machine.IsCombatUnlocked() && !Machine.IsAmbushFight());
	TestEqual(TEXT("the other zombie spots the squad later: a new skirmish"), Machine.StartAmbushCombat(), EGameFlowResult::Ok);
	TestTrue(TEXT("... again a skirmish"), Machine.IsSkirmish());

	// A level without skirmishes: the 2026-10-06 ambush fight (wave cleared panel -> PostCombat), never a skirmish.
	FGameFlowConfig LevelConfig;
	LevelConfig.TotalWaves = 3;
	FGameFlowStateMachine Level;
	Level.Reset(LevelConfig);
	Level.StartAmbushCombat();
	TestFalse(TEXT("ambush_fight level: no skirmish"), Level.IsSkirmish());
	TestEqual(TEXT("... FinishSkirmish refused"), Level.FinishSkirmish(), EGameFlowResult::WrongPhase);
	TestEqual(TEXT("... the wave cleared panel"), Level.NotifyWaveCleared(), EGameFlowResult::Ok);
	TestEqual(TEXT("... single fight: PostCombat, not the next wave"), Level.AdvanceAfterWave(), EGameFlowResult::Ok);
	TestEqual(TEXT("... PostCombat"), Level.GetPhase(), ECodexGamePhase::PostCombat);
	return true;
}

#undef SKIRMISH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
