// CodexTactics.AI.Scent.* — frost hound scent (game designer 2026-10-18, user decisions 1 / 2 / 4; UE-only, no Godot
// reference): walls cut the scent, a doorway lets it through, the 6 m radius; the sniff -> follow -> detect state machine.

#include "AI/PerceptionRules.h"
#include "AI/ScentRules.h"
#include "Data/EnemyPerception.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SCENT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ScentTest
{
	/** The built-in frost hound (smell 6 m, x0.3 per wall, detects within 2 m, sniffs 2.5 s, lost after 1 s). */
	FEnemyPerceptionParams Hound()
	{
		return PerceptionRules::Sanitize(EEnemyArchetype::FrostHound, PerceptionRules::GetArchetypeDefaults(EEnemyArchetype::FrostHound));
	}

	/** Steps Seconds in 0.2 s perception steps with the same sense; returns the first action that is not None (or None). */
	EHoundScentAction StepFor(const FEnemyPerceptionParams& Params, FHoundScentState& State, const FHoundScentSense& Sense, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / 0.2f);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			const EHoundScentAction Action = ScentRules::Step(Params, State, Sense, 0.2f);
			if (Action != EHoundScentAction::None)
			{
				return Action;
			}
		}
		return EHoundScentAction::None;
	}

	FHoundScentSense Smelling(const FVector& Point)
	{
		FHoundScentSense Sense;
		Sense.bSmells = true;
		Sense.Point = Point;
		return Sense;
	}
}

SCENT_TEST(FScentWallsTest, "AI.Scent.WallsAndDoorway")
bool FScentWallsTest::RunTest(const FString& Parameters)
{
	using namespace ScentRules;
	const FEnemyPerceptionParams P = ScentTest::Hound();
	TestEqual(TEXT("hound smell radius 6 m (was 12)"), P.SmellRadiusCm, 600.f, 0.01f);

	// 0 walls: the open-air radius.
	TestTrue(TEXT("open air: smells at 5.9 m"), Smells(P, 590.f, 0));
	TestFalse(TEXT("open air: nothing at 6.1 m"), Smells(P, 610.f, 0));
	TestFalse(TEXT("the old 12 m reach is gone (10 m)"), Smells(P, 1000.f, 0));
	// 1 wall: x0.3 = 1.8 m.
	TestEqual(TEXT("one wall: reach 1.8 m"), ScentReachCm(P, 1), 180.f, 0.5f);
	TestFalse(TEXT("one wall: an operative behind it at 3 m is not smelled"), Smells(P, 300.f, 1));
	TestTrue(TEXT("one wall: pressed against it at 1.5 m he is"), Smells(P, 150.f, 1));
	// 2 walls: ~0.5 m - practically nothing.
	TestEqual(TEXT("two walls: reach ~0.54 m"), ScentReachCm(P, 2), 54.f, 0.5f);
	TestFalse(TEXT("two walls: nothing at 1 m"), Smells(P, 100.f, 2));
	TestTrue(TEXT("many walls count like the hearing cap"), ScentReachCm(P, 9) <= ScentReachCm(P, 2));

	// Doorway / gap: the clearest of the offset traces counts.
	TestEqual(TEXT("doorway: one trace through the gap = open"), CombineTraceWalls({ 1, 0, 1, 1 }), 0);
	TestTrue(TEXT("doorway: smelled at 5 m like open air"), Smells(P, 500.f, CombineTraceWalls({ 1, 0, 1, 1 })));
	TestEqual(TEXT("solid wall: every trace blocked = 1"), CombineTraceWalls({ 1, 1, 1, 1 }), 1);
	TestEqual(TEXT("a wall and a crate: the clearest trace (1)"), CombineTraceWalls({ 2, 1, 2 }), 1);
	TestEqual(TEXT("no trace = open"), CombineTraceWalls({}), 0);

	// Close detection: open air within 2 m only.
	TestTrue(TEXT("close in open air (1.9 m): detected"), IsCloseDetection(P, 190.f, 0));
	TestFalse(TEXT("2.1 m: not yet"), IsCloseDetection(P, 210.f, 0));
	TestFalse(TEXT("1 m through a wall: never by the nose"), IsCloseDetection(P, 100.f, 1));

	// Data knobs: a stronger occlusion and no smell for the others.
	FEnemyPerceptionParams Leaky = P;
	Leaky.SmellOcclusionPerWall = 1.f;
	TestTrue(TEXT("occlusion 1: walls change nothing"), Smells(Leaky, 590.f, 2));
	FEnemyPerceptionParams Spitter = PerceptionRules::GetArchetypeDefaults(EEnemyArchetype::Spitter);
	Spitter.SmellRadiusCm = 900.f;
	Spitter = PerceptionRules::Sanitize(EEnemyArchetype::Spitter, Spitter);
	TestFalse(TEXT("a spitter never smells"), Smells(Spitter, 100.f, 0) || IsCloseDetection(Spitter, 50.f, 0));
	FEnemyPerceptionParams Searching = PerceptionRules::Scaled(P, 1.25f);
	TestTrue(TEXT("searching x1.25: 7 m in open air"), Smells(Searching, 700.f, 0));
	TestEqual(TEXT("searching: the close-detection range stays 2 m"), Searching.SmellDetectCm, P.SmellDetectCm, 0.01f);
	return true;
}

SCENT_TEST(FScentSniffFollowDetectTest, "AI.Scent.SniffFollowDetect")
bool FScentSniffFollowDetectTest::RunTest(const FString& Parameters)
{
	using namespace ScentRules;
	const FEnemyPerceptionParams P = ScentTest::Hound();
	TestTrue(TEXT("sniff 2-3 s"), P.SniffSeconds >= 2.f && P.SniffSeconds <= 3.f);
	FHoundScentState State;
	const FVector Op(500.f, 0.f, 0.f);

	// Caught a scent at 5 m: it sniffs, it does not detect.
	TestEqual(TEXT("no scent: nothing"), Step(P, State, FHoundScentSense(), 0.2f), EHoundScentAction::None);
	TestEqual(TEXT("a scent: start sniffing"), Step(P, State, ScentTest::Smelling(Op), 0.2f), EHoundScentAction::StartSniff);
	TestEqual(TEXT("phase sniffing"), State.Phase, EHoundScentPhase::Sniffing);
	TestEqual(TEXT("still sniffing after 2.2 s"), ScentTest::StepFor(P, State, ScentTest::Smelling(Op), 2.2f), EHoundScentAction::None);
	TestEqual(TEXT("then it follows the scent"), ScentTest::StepFor(P, State, ScentTest::Smelling(Op), 0.4f), EHoundScentAction::StartFollow);
	TestEqual(TEXT("phase following"), State.Phase, EHoundScentPhase::Following);

	// Following: the scent point moves with him; a short gap (0.8 s) keeps the trail.
	const FVector Moved(450.f, 80.f, 0.f);
	TestEqual(TEXT("following: no event"), Step(P, State, ScentTest::Smelling(Moved), 0.2f), EHoundScentAction::None);
	TestTrue(TEXT("follow goal = where it smells him now"), State.ScentPoint.Equals(Moved));
	TestEqual(TEXT("a 0.8 s gap keeps the trail"), ScentTest::StepFor(P, State, FHoundScentSense(), 0.8f), EHoundScentAction::None);
	TestEqual(TEXT("still following"), State.Phase, EHoundScentPhase::Following);

	// Close in open air: detected.
	FHoundScentSense Close = ScentTest::Smelling(FVector(150.f, 0.f, 0.f));
	Close.bClose = true;
	TestEqual(TEXT("within 2 m in open air: detect"), Step(P, State, Close, 0.2f), EHoundScentAction::Detect);
	TestEqual(TEXT("back to no scent"), State.Phase, EHoundScentPhase::None);

	// Close from the start (he walked into its face, unseen): detected at once.
	FHoundScentState Fresh;
	TestEqual(TEXT("close in open air with no scent yet: detect at once"), Step(P, Fresh, Close, 0.2f), EHoundScentAction::Detect);

	// No sniff time: follows at once.
	FEnemyPerceptionParams NoSniff = P;
	NoSniff.SniffSeconds = 0.f;
	FHoundScentState Quick;
	TestEqual(TEXT("sniff 0: follow at once"), Step(NoSniff, Quick, ScentTest::Smelling(Op), 0.2f), EHoundScentAction::StartFollow);

	// Already on the trail (investigating a lost scent): no second sniff.
	FHoundScentState Trail;
	FHoundScentSense Again = ScentTest::Smelling(Op);
	Again.bSkipSniff = true;
	TestEqual(TEXT("on the trail: follow without a sniff"), Step(P, Trail, Again, 0.2f), EHoundScentAction::StartFollow);
	return true;
}

SCENT_TEST(FScentLostTest, "AI.Scent.LostScent")
bool FScentLostTest::RunTest(const FString& Parameters)
{
	using namespace ScentRules;
	const FEnemyPerceptionParams P = ScentTest::Hound();
	const FVector Op(500.f, 0.f, 0.f);

	// He backs off during the sniff: at its end the hound investigates the last scent point.
	FHoundScentState State;
	Step(P, State, ScentTest::Smelling(Op), 0.2f);
	ScentTest::StepFor(P, State, ScentTest::Smelling(FVector(560.f, 0.f, 0.f)), 0.4f);
	TestEqual(TEXT("he is gone: no event during the sniff"), ScentTest::StepFor(P, State, FHoundScentSense(), 1.8f), EHoundScentAction::None);
	TestEqual(TEXT("the sniff ends: lost scent"), ScentTest::StepFor(P, State, FHoundScentSense(), 0.6f), EHoundScentAction::LoseScent);
	TestTrue(TEXT("investigates where it last smelled him"), State.ScentPoint.Equals(FVector(560.f, 0.f, 0.f)));
	TestEqual(TEXT("no scent any more"), State.Phase, EHoundScentPhase::None);

	// He slips away while it follows: lost after 1 s without the scent.
	FHoundScentState Follow;
	Step(P, Follow, ScentTest::Smelling(Op), 0.2f);
	TestEqual(TEXT("follows"), ScentTest::StepFor(P, Follow, ScentTest::Smelling(Op), 3.f), EHoundScentAction::StartFollow);
	TestEqual(TEXT("1.2 s without it: lost"), ScentTest::StepFor(P, Follow, FHoundScentSense(), 1.2f), EHoundScentAction::LoseScent);

	// A trail it never closes on: given up after the follow limit.
	FHoundScentState Long;
	Step(P, Long, ScentTest::Smelling(Op), 0.2f);
	ScentTest::StepFor(P, Long, ScentTest::Smelling(Op), 3.f);
	TestEqual(TEXT("follows for 19 s"), ScentTest::StepFor(P, Long, ScentTest::Smelling(Op), 19.f), EHoundScentAction::None);
	TestEqual(TEXT("gives the trail up after 20 s"), ScentTest::StepFor(P, Long, ScentTest::Smelling(Op), 2.f), EHoundScentAction::LoseScent);
	return true;
}

SCENT_TEST(FScentFollowSpeedAndDataTest, "AI.Scent.FollowSpeedAndData")
bool FScentFollowSpeedAndDataTest::RunTest(const FString& Parameters)
{
	using namespace ScentRules;
	TestEqual(TEXT("cautious: 0.8 x the 190 cm/s patrol pace"), GetFollowSpeed(190.f, 540.f, 0.8f), 152.f, 0.01f);
	TestEqual(TEXT("never above its normal speed"), GetFollowSpeed(400.f, 300.f, 1.f), 300.f, 0.01f);
	TestEqual(TEXT("never a crawl below 50 cm/s"), GetFollowSpeed(190.f, 540.f, 0.f), 50.f, 0.01f);
	TestFalse(TEXT("feed line"), GetCaughtScentLine().IsEmpty());

	// The file keys.
	EnemyPerception::ResetToDefaults();
	TestTrue(TEXT("json applied"), EnemyPerception::ApplyJson(TEXT("{\"archetypes\":{\"FROST_HOUND\":{\"smell_radius_m\":5,\"smell_occlusion_per_wall\":0.2,")
		TEXT("\"smell_detect_m\":1.5,\"sniff_seconds\":3,\"scent_lost_seconds\":0.5,\"scent_follow_max_seconds\":12,\"scent_follow_speed_fraction\":0.6,")
		TEXT("\"scent_search_seconds\":15,\"scent_sweep_radius_m\":2}}}")));
	const FEnemyPerceptionParams H = EnemyPerception::Get(EEnemyArchetype::FrostHound);
	TestEqual(TEXT("smell_radius_m"), H.SmellRadiusCm, 500.f, 0.01f);
	TestEqual(TEXT("smell_occlusion_per_wall"), H.SmellOcclusionPerWall, 0.2f, 0.001f);
	TestEqual(TEXT("smell_detect_m"), H.SmellDetectCm, 150.f, 0.01f);
	TestEqual(TEXT("sniff_seconds"), H.SniffSeconds, 3.f, 0.001f);
	TestEqual(TEXT("scent_lost_seconds"), H.ScentLostSeconds, 0.5f, 0.001f);
	TestEqual(TEXT("scent_follow_max_seconds"), H.ScentFollowMaxSeconds, 12.f, 0.001f);
	TestEqual(TEXT("scent_follow_speed_fraction"), H.ScentFollowSpeedFraction, 0.6f, 0.001f);
	TestEqual(TEXT("scent_search_seconds"), H.ScentSearchSeconds, 15.f, 0.001f);
	TestEqual(TEXT("scent_sweep_radius_m"), H.ScentSweepRadiusCm, 200.f, 0.01f);
	EnemyPerception::ResetToDefaults();

	// The shipped file: 6 m, x0.3 per wall, detects within 2 m, sniffs 2-3 s.
	FString Text;
	if (TestTrue(TEXT("enemy_perception.json readable"), FFileHelper::LoadFileToString(Text, *EnemyPerception::GetDefaultPath())))
	{
		TestTrue(TEXT("enemy_perception.json applies"), EnemyPerception::ApplyJson(Text));
		const FEnemyPerceptionParams S = EnemyPerception::Get(EEnemyArchetype::FrostHound);
		TestEqual(TEXT("shipped smell 6 m"), S.SmellRadiusCm, 600.f, 0.01f);
		TestEqual(TEXT("shipped x0.3 per wall"), S.SmellOcclusionPerWall, 0.3f, 0.001f);
		TestEqual(TEXT("shipped detection 2 m"), S.SmellDetectCm, 200.f, 0.01f);
		TestTrue(TEXT("shipped sniff 2-3 s"), S.SniffSeconds >= 2.f && S.SniffSeconds <= 3.f);
		TestTrue(TEXT("the whistle (12 m) now reaches beyond the nose"), S.SmellRadiusCm < EnemyPerception::GetLure().WhistleRadiusCm);
		EnemyPerception::ResetToDefaults();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
