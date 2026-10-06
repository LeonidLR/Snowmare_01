// CodexTactics.AI.Perception.* / CodexTactics.AI.Patrol.* / CodexTactics.Combat.AmbushStartsCombat — enemy perception,
// trap search, dialogue pause and the ambush combat start (user request + amendment 2026-10-06; UE-only, no Godot
// reference).

#include "AI/PatrolRouteRules.h"
#include "AI/PerceptionRules.h"
#include "Data/EnemyPerception.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "GameFlow/GameFlowStateMachine.h"
#include "GameFlow/LevelEncounterRules.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PERCEPTION_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PerceptionTest
{
	/** Plain 25 m / 45 deg observer at the origin looking down +X. */
	FEnemyPerceptionParams MakeObserver()
	{
		FEnemyPerceptionParams Params;
		Params.SightRangeCm = 2500.f;
		Params.SightHalfAngleDeg = 45.f;
		Params.StandingVisibility = 1.f;
		Params.CrouchingVisibility = 0.7f;
		Params.ProneVisibility = 0.4f;
		Params.ProximityCm = 300.f;
		return Params;
	}
}

PERCEPTION_TEST(FPerceptionSightFovRangeStanceTest, "AI.Perception.SightFovRangeStance")
bool FPerceptionSightFovRangeStanceTest::RunTest(const FString& Parameters)
{
	using namespace PerceptionRules;
	const FEnemyPerceptionParams P = PerceptionTest::MakeObserver();
	const FVector Eye = FVector::ZeroVector;
	const FVector Forward(1.f, 0.f, 0.f);

	// Range by stance: standing 25 m, crouching 17.5 m, prone 10 m.
	TestTrue(TEXT("standing at 24 m ahead: seen"), CanSee(P, Eye, Forward, FVector(2400.f, 0.f, 0.f), EOperativeStance::Standing, true));
	TestFalse(TEXT("standing at 26 m: too far"), CanSee(P, Eye, Forward, FVector(2600.f, 0.f, 0.f), EOperativeStance::Standing, true));
	TestTrue(TEXT("crouching at 17 m: seen"), CanSee(P, Eye, Forward, FVector(1700.f, 0.f, 0.f), EOperativeStance::Crouching, true));
	TestFalse(TEXT("crouching at 18 m: unseen"), CanSee(P, Eye, Forward, FVector(1800.f, 0.f, 0.f), EOperativeStance::Crouching, true));
	TestFalse(TEXT("prone at 12 m: unseen"), CanSee(P, Eye, Forward, FVector(1200.f, 0.f, 0.f), EOperativeStance::Prone, true));
	TestTrue(TEXT("prone at 9 m: seen"), CanSee(P, Eye, Forward, FVector(900.f, 0.f, 0.f), EOperativeStance::Prone, true));
	TestEqual(TEXT("effective prone range"), EffectiveSightRange(P, EOperativeStance::Prone), 1000.f, 0.01f);

	// Field of view: 45 deg half-angle.
	TestTrue(TEXT("40 deg off the facing: in view"), CanSee(P, Eye, Forward, FVector(1000.f, 1000.f * FMath::Tan(FMath::DegreesToRadians(40.f)), 0.f),
		EOperativeStance::Standing, true));
	TestFalse(TEXT("60 deg off the facing: out of view"), CanSee(P, Eye, Forward, FVector(500.f, 866.f, 0.f), EOperativeStance::Standing, true));
	TestFalse(TEXT("behind it at 10 m: unseen"), CanSee(P, Eye, Forward, FVector(-1000.f, 0.f, 0.f), EOperativeStance::Standing, true));
	TestTrue(TEXT("behind it at 2 m: proximity notices"), CanSee(P, Eye, Forward, FVector(-200.f, 0.f, 0.f), EOperativeStance::Standing, true));
	FEnemyPerceptionParams AllRound = P;
	AllRound.SightHalfAngleDeg = 180.f;
	TestTrue(TEXT("180 deg: sees behind"), CanSee(AllRound, Eye, Forward, FVector(-1000.f, 0.f, 0.f), EOperativeStance::Standing, true));

	// A blocked line hides even the closest target.
	TestFalse(TEXT("line blocked: unseen"), CanSee(P, Eye, Forward, FVector(500.f, 0.f, 0.f), EOperativeStance::Standing, false));

	// Suspicion: 1 s at the edge of the range, 3x faster point-blank, decays without sight; 0 s = instant.
	FEnemyPerceptionParams Slow = P;
	Slow.TimeToDetectSeconds = 1.f;
	Slow.SuspicionDecayPerSecond = 0.5f;
	float Meter = StepSuspicion(Slow, 0.f, 0.5f, true, 2500.f, 2500.f);
	TestEqual(TEXT("half a second at the edge: 0.5"), Meter, 0.5f, 0.001f);
	TestEqual(TEXT("point-blank: 3x faster"), StepSuspicion(Slow, 0.f, 0.2f, true, 0.f, 2500.f), 0.6f, 0.001f);
	TestEqual(TEXT("decays without sight"), StepSuspicion(Slow, Meter, 0.4f, false, 0.f, 0.f), 0.3f, 0.001f);
	TestEqual(TEXT("caps at 1"), StepSuspicion(Slow, 0.9f, 1.f, true, 2500.f, 2500.f), 1.f);
	Slow.TimeToDetectSeconds = 0.f;
	TestEqual(TEXT("time to detect 0: at once"), StepSuspicion(Slow, 0.f, 0.01f, true, 2400.f, 2500.f), 1.f);
	return true;
}

PERCEPTION_TEST(FPerceptionCoverHidesProneTest, "AI.Perception.CoverHidesProne")
bool FPerceptionCoverHidesProneTest::RunTest(const FString& Parameters)
{
	using namespace PerceptionRules;
	const FEnemyPerceptionParams P = PerceptionTest::MakeObserver();
	const FVector Forward(1.f, 0.f, 0.f);
	// Sprint 08 rule: a 60 cm cover 1 m in front of an operative 10 m away from a standing enemy.
	const bool bProneClear = IsLineClearOverCover(EOperativeStance::Prone, 900.f, 1000.f);
	const bool bStandingClear = IsLineClearOverCover(EOperativeStance::Standing, 900.f, 1000.f);
	TestFalse(TEXT("prone behind 60 cm cover: line blocked"), bProneClear);
	TestTrue(TEXT("standing behind 60 cm cover: line clear"), bStandingClear);
	TestFalse(TEXT("prone behind cover in range and view: unseen"),
		CanSee(P, FVector::ZeroVector, Forward, FVector(800.f, 0.f, 0.f), EOperativeStance::Prone, bProneClear));
	TestTrue(TEXT("standing behind cover: seen"),
		CanSee(P, FVector::ZeroVector, Forward, FVector(1000.f, 0.f, 0.f), EOperativeStance::Standing, bStandingClear));
	return true;
}

PERCEPTION_TEST(FPerceptionHearingTest, "AI.Perception.HearingByMovementAndGunshot")
bool FPerceptionHearingTest::RunTest(const FString& Parameters)
{
	using namespace PerceptionRules;
	// Gait classification.
	TestEqual(TEXT("still"), static_cast<int32>(ClassifyMovement(EOperativeStance::Standing, 5.f, false)), static_cast<int32>(ESquadMovementNoise::Still));
	TestEqual(TEXT("walk"), static_cast<int32>(ClassifyMovement(EOperativeStance::Standing, 250.f, false)), static_cast<int32>(ESquadMovementNoise::Walk));
	TestEqual(TEXT("run"), static_cast<int32>(ClassifyMovement(EOperativeStance::Standing, 500.f, true)), static_cast<int32>(ESquadMovementNoise::Run));
	TestEqual(TEXT("crouch walk"), static_cast<int32>(ClassifyMovement(EOperativeStance::Crouching, 150.f, false)),
		static_cast<int32>(ESquadMovementNoise::CrouchWalk));
	TestEqual(TEXT("crawl"), static_cast<int32>(ClassifyMovement(EOperativeStance::Prone, 60.f, false)), static_cast<int32>(ESquadMovementNoise::Crawl));

	// Radii of the spitter defaults (lowered 2026-10-06): walk 5.5 m, run 11 m, crouch 2.5 m, crawl 1 m, gunshot 40 m, grenade 60 m.
	const FEnemyPerceptionParams P = GetArchetypeDefaults(EEnemyArchetype::Spitter);
	TestTrue(TEXT("walking at 5 m: heard"), HearsMovement(P, ESquadMovementNoise::Walk, 500.f));
	TestFalse(TEXT("walking at 6 m: not heard"), HearsMovement(P, ESquadMovementNoise::Walk, 600.f));
	TestTrue(TEXT("running at 10.5 m: heard"), HearsMovement(P, ESquadMovementNoise::Run, 1050.f));
	TestFalse(TEXT("running at 12 m: not heard"), HearsMovement(P, ESquadMovementNoise::Run, 1200.f));
	TestFalse(TEXT("crouch-walking at 3 m: not heard"), HearsMovement(P, ESquadMovementNoise::CrouchWalk, 300.f));
	TestTrue(TEXT("crawling at 0.9 m: heard"), HearsMovement(P, ESquadMovementNoise::Crawl, 90.f));
	TestFalse(TEXT("crawling at 1.5 m: not heard"), HearsMovement(P, ESquadMovementNoise::Crawl, 150.f));
	TestFalse(TEXT("standing still next to it: no footsteps"), HearsMovement(P, ESquadMovementNoise::Still, 50.f));
	TestTrue(TEXT("gunshot at 39 m: heard"), HearsGunshot(P, 3900.f));
	TestFalse(TEXT("gunshot at 41 m: not heard"), HearsGunshot(P, 4100.f));
	TestTrue(TEXT("grenade at 55 m: heard"), HearsExplosion(P, 5500.f));
	TestFalse(TEXT("grenade at 65 m: not heard"), HearsExplosion(P, 6500.f));
	// Loudness order holds for every archetype.
	for (const EEnemyArchetype Type : { EEnemyArchetype::FrostHound, EEnemyArchetype::Marksman, EEnemyArchetype::Spitter, EEnemyArchetype::Brute,
		EEnemyArchetype::Cutter, EEnemyArchetype::Frostbitten })
	{
		const FEnemyPerceptionParams D = GetArchetypeDefaults(Type);
		TestTrue(FString::Printf(TEXT("%d: crawl < crouch < walk < run < gunshot"), static_cast<int32>(Type)),
			D.HearCrawlCm < D.HearCrouchWalkCm && D.HearCrouchWalkCm < D.HearWalkCm && D.HearWalkCm < D.HearRunCm && D.HearRunCm < D.HearGunshotCm);
	}
	// The search boost scales the radii.
	TestTrue(TEXT("searching (x1.25): walking at 6.5 m heard"), HearsMovement(Scaled(P, 1.25f), ESquadMovementNoise::Walk, 650.f));
	return true;
}

PERCEPTION_TEST(FPerceptionSmellHoundOnlyTest, "AI.Perception.SmellHoundOnly")
bool FPerceptionSmellHoundOnlyTest::RunTest(const FString& Parameters)
{
	using namespace PerceptionRules;
	const FEnemyPerceptionParams Hound = GetArchetypeDefaults(EEnemyArchetype::FrostHound);
	TestTrue(TEXT("hound can smell"), CanSmell(EEnemyArchetype::FrostHound));
	TestTrue(TEXT("hound smells at 11 m"), Smells(Hound, 1100.f));
	TestFalse(TEXT("hound does not smell at 13 m"), Smells(Hound, 1300.f));
	for (const EEnemyArchetype Type : { EEnemyArchetype::Marksman, EEnemyArchetype::Spitter, EEnemyArchetype::Brute, EEnemyArchetype::Cutter,
		EEnemyArchetype::Frostbitten })
	{
		FEnemyPerceptionParams Tuned = GetArchetypeDefaults(Type);
		Tuned.SmellRadiusCm = 5000.f; // a data file that sets it anyway
		TestFalse(FString::Printf(TEXT("%d cannot smell"), static_cast<int32>(Type)), Smells(Sanitize(Type, Tuned), 100.f));
	}
	// Archetype character: hound short sight / big nose, marksman long narrow sight.
	const FEnemyPerceptionParams Marksman = GetArchetypeDefaults(EEnemyArchetype::Marksman);
	TestTrue(TEXT("marksman sees further than the hound"), Marksman.SightRangeCm > Hound.SightRangeCm);
	TestTrue(TEXT("marksman has the narrowest view"), Marksman.SightHalfAngleDeg < Hound.SightHalfAngleDeg);
	TestTrue(TEXT("hound hears better than the marksman"), Hound.HearWalkCm > Marksman.HearWalkCm);

	// Data file: smell set for a spitter is dropped, the hound's is taken, a missing key keeps the default.
	EnemyPerception::ResetToDefaults();
	const bool bApplied = EnemyPerception::ApplyJson(TEXT("{\"search\":{\"duration_seconds\":45},\"archetypes\":{\"SPITTER\":{\"smell_radius_m\":30,\"sight_range_m\":10},")
		TEXT("\"HOUND\":{\"smell_radius_m\":20}}}"));
	TestTrue(TEXT("json applied"), bApplied);
	TestEqual(TEXT("spitter sight from the file"), EnemyPerception::Get(EEnemyArchetype::Spitter).SightRangeCm, 1000.f, 0.01f);
	TestEqual(TEXT("spitter smell dropped"), EnemyPerception::Get(EEnemyArchetype::Spitter).SmellRadiusCm, 0.f);
	TestEqual(TEXT("hound smell from the file"), EnemyPerception::Get(EEnemyArchetype::FrostHound).SmellRadiusCm, 2000.f, 0.01f);
	TestEqual(TEXT("hound sight default kept"), EnemyPerception::Get(EEnemyArchetype::FrostHound).SightRangeCm, Hound.SightRangeCm, 0.01f);
	TestEqual(TEXT("search duration from the file"), EnemyPerception::GetSearch().DurationSeconds, 45.f, 0.01f);
	EnemyPerception::ResetToDefaults();

	// The shipped file parses and keeps the hound-only smell.
	FString Text;
	if (TestTrue(TEXT("enemy_perception.json readable"), FFileHelper::LoadFileToString(Text, *EnemyPerception::GetDefaultPath())))
	{
		TestTrue(TEXT("enemy_perception.json applies"), EnemyPerception::ApplyJson(Text));
		TestTrue(TEXT("shipped hound smells"), EnemyPerception::Get(EEnemyArchetype::FrostHound).SmellRadiusCm > 0.f);
		TestEqual(TEXT("shipped search 60 s"), EnemyPerception::GetSearch().DurationSeconds, 60.f, 0.01f);
		EnemyPerception::ResetToDefaults();
	}
	return true;
}

PERCEPTION_TEST(FPatrolTrapStartsSearchTest, "AI.Patrol.TrapStartsSearchNotCombat")
bool FPatrolTrapStartsSearchTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	FPatrolAlertInput Trap;
	Trap.TrapDistanceCm = 1500.f;
	TestEqual(TEXT("tripwire at 15 m: search"), static_cast<int32>(EvaluateAlert(Trap)), static_cast<int32>(EPatrolReaction::Search));
	TestFalse(TEXT("tripwire at 15 m: no engage"), ShouldBreakPatrol(Trap));

	FPatrolAlertInput Hurt;
	Hurt.bTookTrapDamage = true;
	TestEqual(TEXT("hurt by the mine: search, not engage"), static_cast<int32>(EvaluateAlert(Hurt)), static_cast<int32>(EPatrolReaction::Search));

	FPatrolAlertInput Partner;
	Partner.bPartnerSearching = true;
	TestTrue(TEXT("partner searching: search too"), ShouldStartSearch(Partner));

	FPatrolAlertInput Shot;
	Shot.bTookDamage = true;
	Shot.TrapDistanceCm = 1000.f;
	TestEqual(TEXT("shot by the squad (even near a blast): engage"), static_cast<int32>(EvaluateAlert(Shot)), static_cast<int32>(EPatrolReaction::Engage));

	// The ambush rule only starts the fight from an Engage (BreakPatrol): a search keeps the level in exploration.
	FGameFlowStateMachine Machine;
	TestEqual(TEXT("search: still exploring"), Machine.GetPhase(), ECodexGamePhase::Exploration);
	TestFalse(TEXT("search speed above the patrol pace"), GetSearchSpeed(190.f, 1.6f, 540.f) <= 190.f);
	TestEqual(TEXT("search speed 1.6 x 190"), GetSearchSpeed(190.f, 1.6f, 540.f), 304.f, 0.01f);
	TestEqual(TEXT("search speed capped by the normal speed"), GetSearchSpeed(190.f, 3.f, 250.f), 250.f, 0.01f);
	// Sweep points stay inside the radius.
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const FVector Point = PickSearchPoint(FVector(100.f, 200.f, 30.f), 1200.f, Step / 10.f, (10 - Step) / 10.f);
		TestTrue(FString::Printf(TEXT("sweep point %d inside 12 m"), Step), FVector::Dist2D(Point, FVector(100.f, 200.f, 30.f)) <= 1200.01f);
		TestEqual(TEXT("sweep point keeps the height"), static_cast<float>(Point.Z), 30.f);
	}
	return true;
}

PERCEPTION_TEST(FPatrolSearchTimeoutTest, "AI.Patrol.SearchTimeoutReturnsToPatrol")
bool FPatrolSearchTimeoutTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	TestFalse(TEXT("59 s: still searching"), IsSearchOver(59.f, 60.f));
	TestTrue(TEXT("60 s: back to the route"), IsSearchOver(60.f, 60.f));
	TestTrue(TEXT("0 s search: ends at once"), IsSearchOver(0.f, 0.f));
	// Per level: the level JSON wins when > 0.
	TestEqual(TEXT("level 30 s wins"), LevelEncounterRules::ResolveSearchSeconds(30.f, 60.f), 30.f);
	TestEqual(TEXT("level unset: global 60 s"), LevelEncounterRules::ResolveSearchSeconds(-1.f, 60.f), 60.f);
	// Back at the nearest waypoint.
	const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
	TestEqual(TEXT("nearest waypoint"), FindNearestWaypoint(Route, FVector(900.f, 800.f, 50.f)), 2);
	TestEqual(TEXT("nearest waypoint near the start"), FindNearestWaypoint(Route, FVector(-100.f, 50.f, 0.f)), 0);
	TestEqual(TEXT("empty route"), FindNearestWaypoint({}, FVector::ZeroVector), static_cast<int32>(INDEX_NONE));
	return true;
}

PERCEPTION_TEST(FPatrolSearchDetectionTest, "AI.Patrol.SearchDetectionStartsCombat")
bool FPatrolSearchDetectionTest::RunTest(const FString& Parameters)
{
	using namespace PatrolRouteRules;
	// While searching the perception is heightened: a hound hears a walk at 8 m (7 m x 1.25).
	const FEnemyPerceptionParams Hound = PerceptionRules::GetArchetypeDefaults(EEnemyArchetype::FrostHound);
	const FEnemyPerceptionParams Boosted = PerceptionRules::Scaled(Hound, 1.25f);
	TestFalse(TEXT("patrolling hound: walk at 8 m unheard"), PerceptionRules::HearsMovement(Hound, ESquadMovementNoise::Walk, 800.f));
	TestTrue(TEXT("searching hound: walk at 8 m heard"), PerceptionRules::HearsMovement(Boosted, ESquadMovementNoise::Walk, 800.f));

	// Detection while searching: Engage (wins over the ongoing search) ...
	FPatrolAlertInput Found;
	Found.bPartnerSearching = true;
	Found.bHearsOperative = true;
	TestEqual(TEXT("heard while searching: engage"), static_cast<int32>(EvaluateAlert(Found)), static_cast<int32>(EPatrolReaction::Engage));
	FPatrolAlertInput Smelled;
	Smelled.bSmellsOperative = true;
	TestTrue(TEXT("smelled: engage"), ShouldBreakPatrol(Smelled));

	// ... which starts the fight on an ambush level, once.
	TestTrue(TEXT("ambush level in exploration: fight starts"), LevelEncounterRules::ShouldStartAmbush(true, ECodexGamePhase::Exploration, false));
	FGameFlowStateMachine Machine;
	TestEqual(TEXT("ambush start ok"), Machine.StartAmbushCombat(), EGameFlowResult::Ok);
	TestEqual(TEXT("real-time fight"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	return true;
}

PERCEPTION_TEST(FPatrolDialoguePausesPerceptionTest, "AI.Patrol.DialoguePausesPerception")
bool FPatrolDialoguePausesPerceptionTest::RunTest(const FString& Parameters)
{
	using namespace LevelEncounterRules;
	TestTrue(TEXT("intro dialogue open in exploration: AI held"), IsWorldAIPaused(true, ECodexGamePhase::Exploration, 0));
	TestTrue(TEXT("dialogue during a wave: AI held"), IsWorldAIPaused(true, ECodexGamePhase::WaveCombat, 0));
	TestTrue(TEXT("pre-combat cutscene: AI held"), IsWorldAIPaused(false, ECodexGamePhase::Cutscene, 0));
	TestTrue(TEXT("a registered blocker (sequencer cutscene): AI held"), IsWorldAIPaused(false, ECodexGamePhase::Exploration, 1));
	TestFalse(TEXT("dialogue closed: live play"), IsWorldAIPaused(false, ECodexGamePhase::Exploration, 0));
	TestFalse(TEXT("wave without dialogue: live"), IsWorldAIPaused(false, ECodexGamePhase::WaveCombat, 0));
	TestFalse(TEXT("preparation: live"), IsWorldAIPaused(false, ECodexGamePhase::Preparation, 0));
	return true;
}

PERCEPTION_TEST(FAmbushStartsCombatTest, "Combat.AmbushStartsCombat")
bool FAmbushStartsCombatTest::RunTest(const FString& Parameters)
{
	using namespace LevelEncounterRules;
	// Per-level setting.
	ECombatStartMode Mode = ECombatStartMode::Button;
	TestTrue(TEXT("parse ambush"), ParseCombatStart(TEXT("Ambush"), Mode) && Mode == ECombatStartMode::Ambush);
	TestTrue(TEXT("parse empty = auto"), ParseCombatStart(TEXT(""), Mode) && Mode == ECombatStartMode::Auto);
	TestTrue(TEXT("parse button"), ParseCombatStart(TEXT("button"), Mode) && Mode == ECombatStartMode::Button);
	TestFalse(TEXT("unknown rejected"), ParseCombatStart(TEXT("sometimes"), Mode));
	TestTrue(TEXT("auto + patrols: ambush"), IsAmbushStart(ECombatStartMode::Auto, true));
	TestFalse(TEXT("auto, wave map without patrols: button"), IsAmbushStart(ECombatStartMode::Auto, false));
	TestTrue(TEXT("ambush forced"), IsAmbushStart(ECombatStartMode::Ambush, false));
	TestFalse(TEXT("button forced on a patrol map"), IsAmbushStart(ECombatStartMode::Button, true));

	// Level JSON field.
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("level json with combat_start parses"),
		LevelJsonRules::ParseLevel(TEXT("{\"level_id\":\"t\",\"combat_start\":\"ambush\",\"patrol_search_seconds\":30,\"waves\":[]}"), TEXT("t"), Level, Error));
	TestEqual(TEXT("combat_start read"), Level.CombatStart, ECombatStartMode::Ambush);
	TestEqual(TEXT("patrol_search_seconds read"), Level.PatrolSearchSeconds, 30.f);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("level json without the field parses"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\":\"w\",\"waves\":[]}"), TEXT("w"), Plain, Error));
	TestEqual(TEXT("default auto"), Plain.CombatStart, ECombatStartMode::Auto);
	TestFalse(TEXT("bad combat_start rejected"),
		LevelJsonRules::ParseLevel(TEXT("{\"level_id\":\"b\",\"combat_start\":\"later\",\"waves\":[]}"), TEXT("b"), Plain, Error));

	// The flow: exploration -> real-time fight of wave 1 at once (no cutscene / preparation), once.
	TestTrue(TEXT("attack in exploration starts it"), ShouldStartAmbush(true, ECodexGamePhase::Exploration, false));
	TestFalse(TEXT("not on a button level"), ShouldStartAmbush(false, ECodexGamePhase::Exploration, false));
	TestFalse(TEXT("not twice"), ShouldStartAmbush(true, ECodexGamePhase::WaveCombat, true));
	FGameFlowConfig Config;
	Config.TotalWaves = 3;
	FGameFlowStateMachine Machine(Config);
	int32 Changes = 0;
	Machine.OnStateChanged.AddLambda([&Changes](ECodexGamePhase, ECodexCombatMode) { ++Changes; });
	TestEqual(TEXT("start ok"), Machine.StartAmbushCombat(), EGameFlowResult::Ok);
	TestEqual(TEXT("wave combat"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("real time"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("wave 1"), Machine.GetWaveIndex(), 1);
	TestTrue(TEXT("ambush fight"), Machine.IsAmbushFight());
	TestEqual(TEXT("one state change (no cutscene / preparation)"), Changes, 1);
	TestEqual(TEXT("pause charges ready"), Machine.GetPauseCharges(), Config.TacticalPauseMaxCharges);
	TestTrue(TEXT("second start refused"), Machine.StartAmbushCombat() != EGameFlowResult::Ok);
	TestEqual(TEXT("combat zone refused afterwards"), Machine.TriggerCombatZone(), EGameFlowResult::WrongPhase);
	// Single fight: once its enemies are down the battle is over (PostCombat, not the level's wave 2).
	Machine.NotifyWaveCleared();
	Machine.AdvanceAfterWave();
	TestEqual(TEXT("ambush fight is the whole battle"), Machine.GetPhase(), ECodexGamePhase::PostCombat);
	// The combat zone path is unchanged and still refuses an ambush once the cutscene runs.
	FGameFlowStateMachine Classic(Config);
	Classic.TriggerCombatZone();
	TestEqual(TEXT("no ambush during the cutscene"), Classic.StartAmbushCombat(), EGameFlowResult::WrongPhase);
	return true;
}

PERCEPTION_TEST(FPerceptionHearingRadiiWallsTest, "AI.Perception.HearingRadiiAndWallOcclusion")
bool FPerceptionHearingRadiiWallsTest::RunTest(const FString& Parameters)
{
	using namespace PerceptionRules;
	// User report 2026-10-06 (heard running from very far): the shipped radii in force — run 10-12 m, walk 5-6 m for the
	// typical enemy, the hound a bit more, crouch / crawl small; the knob Codex.Perception.HearingScale stays 1.
	EnemyPerception::ResetToDefaults();
	FString Text;
	if (TestTrue(TEXT("enemy_perception.json readable"), FFileHelper::LoadFileToString(Text, *EnemyPerception::GetDefaultPath())))
	{
		TestTrue(TEXT("enemy_perception.json applies"), EnemyPerception::ApplyJson(Text));
	}
	TestEqual(TEXT("hearing knob neutral"), EnemyPerception::GetTuning().HearingScale, 1.f);
	for (const EEnemyArchetype Type : { EEnemyArchetype::Marksman, EEnemyArchetype::Spitter, EEnemyArchetype::Brute, EEnemyArchetype::Cutter,
		EEnemyArchetype::Frostbitten })
	{
		const FEnemyPerceptionParams D = EnemyPerception::Get(Type);
		TestTrue(FString::Printf(TEXT("%d: run 10-12 m (%.1f)"), static_cast<int32>(Type), D.HearRunCm / 100.f), D.HearRunCm >= 1000.f && D.HearRunCm <= 1200.f);
		TestTrue(FString::Printf(TEXT("%d: walk 5-6 m (%.1f)"), static_cast<int32>(Type), D.HearWalkCm / 100.f), D.HearWalkCm >= 500.f && D.HearWalkCm <= 600.f);
		TestTrue(FString::Printf(TEXT("%d: crouch <= 3 m, crawl <= 1.5 m"), static_cast<int32>(Type)), D.HearCrouchWalkCm <= 300.f && D.HearCrawlCm <= 150.f);
		TestEqual(FString::Printf(TEXT("%d: walls x0.5"), static_cast<int32>(Type)), D.HearingOcclusionPerWall, 0.5f);
		// The file and the built-in defaults agree.
		TestEqual(FString::Printf(TEXT("%d: file run = default"), static_cast<int32>(Type)), D.HearRunCm, GetArchetypeDefaults(Type).HearRunCm, 0.01f);
	}
	const FEnemyPerceptionParams Hound = EnemyPerception::Get(EEnemyArchetype::FrostHound);
	TestTrue(TEXT("hound run a bit more (12-15 m)"), Hound.HearRunCm > 1200.f && Hound.HearRunCm <= 1500.f);
	TestTrue(TEXT("hound walk 6-8 m"), Hound.HearWalkCm > 600.f && Hound.HearWalkCm <= 800.f);

	// Walls: each one halves the footstep radius (at most 3 count).
	const FEnemyPerceptionParams Spitter = EnemyPerception::Get(EEnemyArchetype::Spitter); // run 11 m
	TestTrue(TEXT("running 8 m away, open air: heard"), HearsMovementThroughWalls(Spitter, ESquadMovementNoise::Run, 800.f, 0));
	TestFalse(TEXT("running 8 m away behind a wall (5.5 m): unheard"), HearsMovementThroughWalls(Spitter, ESquadMovementNoise::Run, 800.f, 1));
	TestTrue(TEXT("running 5 m away behind a wall: heard"), HearsMovementThroughWalls(Spitter, ESquadMovementNoise::Run, 500.f, 1));
	TestFalse(TEXT("running 3 m away behind two walls (2.75 m): unheard"), HearsMovementThroughWalls(Spitter, ESquadMovementNoise::Run, 300.f, 2));
	TestEqual(TEXT("occluders capped at 3"), OccludedRadius(800.f, 7, 0.5f), 100.f, 0.01f);
	TestEqual(TEXT("factor clamped to 1"), OccludedRadius(800.f, 2, 3.f), 800.f, 0.01f);
	TestEqual(TEXT("negative wall count = open air"), OccludedRadius(800.f, -1, 0.5f), 800.f, 0.01f);

	// Data / knob: the per-archetype key and Codex.Perception.HearingOcclusion (>= 0 wins over the data).
	FEnemyPerceptionParams Base = GetArchetypeDefaults(EEnemyArchetype::Brute);
	TSharedPtr<FJsonObject> Json;
	if (TestTrue(TEXT("json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"hearing_occlusion_per_wall\":0.25}")), Json) && Json.IsValid()))
	{
		TestEqual(TEXT("per-archetype occlusion from the file"), EnemyPerception::ParamsFromJson(*Json, Base).HearingOcclusionPerWall, 0.25f);
	}
	FPerceptionTuning Tuning;
	Tuning.HearingOcclusionPerWall = 0.8f;
	TestEqual(TEXT("knob overrides the occlusion"), EnemyPerception::ApplyTuning(Base, Tuning).HearingOcclusionPerWall, 0.8f);
	Tuning.HearingOcclusionPerWall = -1.f;
	TestEqual(TEXT("knob -1 keeps the data"), EnemyPerception::ApplyTuning(Base, Tuning).HearingOcclusionPerWall, 0.5f);
	TestEqual(TEXT("the search boost leaves the occlusion"), Scaled(Base, 1.25f).HearingOcclusionPerWall, 0.5f);
	EnemyPerception::ResetToDefaults();
	return true;
}

#undef PERCEPTION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
