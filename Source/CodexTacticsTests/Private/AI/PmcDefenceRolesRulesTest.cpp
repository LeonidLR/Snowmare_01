#include "Misc/AutomationTest.h"
#include "AI/PmcDefenceRolesRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Smart enemies T-08 first slice: the pure rules of the holder + fall-back layer (PmcDefenceRolesRules.h). The world half (zone post scan, nav routes,
// the commander's walk) is the paired seed runs' and the gate smokes'.

namespace PmcDefenceRolesTest
{
	FFiringCandidate MakeZonePost(const TCHAR* Source, const FVector& Stand, float Score, const TCHAR* Key)
	{
		FFiringCandidate Out;
		Out.Position.SourceId = Source;
		Out.Position.StandPoint = Stand;
		Out.Position.CoverKey = Key;
		Out.Position.bValid = true;
		Out.Score = Score;
		return Out;
	}

	/** Objective radius 6 m, kill zone 15 m out, the defence has fired, nobody down, the zone is held, a hammer 25 m of route from his post at sprint 725 / walk 220 cm/s. */
	FPmcFallBackSituation BaseSituation()
	{
		FPmcFallBackSituation Out;
		Out.bZoneHeld = true;
		Out.NearestAttackerFromObjectiveCm = -1.f;
		Out.ObjectiveRadiusCm = 600.f;
		Out.KillZoneFromObjectiveCm = 1500.f;
		Out.AttackersDown = 0;
		Out.bDefenceHasFired = true;
		Out.AttackerSpeedCm = 220.f;
		Out.HammerSpeedCm = 725.f;
		Out.HammerPathCm = -1.f;
		return Out;
	}

	TSharedPtr<FJsonObject> ParseBlock(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcFallBackRulesTest, "CodexTactics.AI.PmcDefenceRoles.FallBackRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcFallBackRulesTest::RunTest(const FString&)
{
	using namespace PmcDefenceRolesTest;
	const FPmcDefenceRolesConfig Config; // zone_near 25 m, bypass margin 5 m, finish at 2 down, margin 2 s
	const uint8 R1 = static_cast<uint8>(EPmcFallBackRule::ZoneEmptying);
	const uint8 R2 = static_cast<uint8>(EPmcFallBackRule::Bypassed);
	const uint8 R4 = static_cast<uint8>(EPmcFallBackRule::NotInTime);

	// No known attacker: the squad acts on what it saw - no rule fires, even with an empty zone and a long walk.
	{
		FPmcFallBackSituation S = BaseSituation();
		S.bZoneHeld = false;
		S.HammerPathCm = 9000.f;
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("no attacker known: no rule"), D.Rules, static_cast<uint8>(0));
		TestFalse(TEXT("no attacker known: no fall-back"), D.bFallBack);
	}
	// Rule 1: the zone is empty and an attacker is within zone_near_m of its EDGE (radius 6 m): 20 m from the point = 14 m from the edge.
	{
		FPmcFallBackSituation S = BaseSituation();
		S.bZoneHeld = false;
		S.NearestAttackerFromObjectiveCm = 2000.f;
		FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("empty zone + attacker near: rule 1 only"), D.Rules, R1);
		TestTrue(TEXT("rule 1 falls back"), D.bFallBack);
		S.bZoneHeld = true;
		TestEqual(TEXT("a held zone: no rule 1"), static_cast<uint8>(PmcDefenceRoles::DecideFallBack(S, Config).Rules & R1), static_cast<uint8>(0));
		S.bZoneHeld = false;
		S.NearestAttackerFromObjectiveCm = 3100.f; // edge 25 m exactly
		TestEqual(TEXT("exactly zone_near_m from the edge still counts"), static_cast<uint8>(PmcDefenceRoles::DecideFallBack(S, Config).Rules & R1), R1);
		S.NearestAttackerFromObjectiveCm = 3200.f;
		TestEqual(TEXT("26 m from the edge: not near"), static_cast<uint8>(PmcDefenceRoles::DecideFallBack(S, Config).Rules & R1), static_cast<uint8>(0));
		S.NearestAttackerFromObjectiveCm = 200.f; // inside the zone: the edge distance is 0
		TestEqual(TEXT("an attacker inside the zone is near"), static_cast<uint8>(PmcDefenceRoles::DecideFallBack(S, Config).Rules & R1), R1);
	}
	// Rule 2: an attacker nearer the objective than the kill zone (15 m) by more than the margin (5 m): under 10 m. Only after the defence has fired.
	{
		FPmcFallBackSituation S = BaseSituation();
		S.NearestAttackerFromObjectiveCm = 800.f;
		FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("bypassed after the first shot: rule 2"), D.Rules, R2);
		TestTrue(TEXT("rule 2 falls back"), D.bFallBack);
		S.bDefenceHasFired = false;
		TestEqual(TEXT("before the first shot rule 2 may not force a fall-back"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, static_cast<uint8>(0));
		S.bDefenceHasFired = true;
		S.NearestAttackerFromObjectiveCm = 1200.f; // between 10 m and the kill zone's 15 m: in the kill zone, not past it
		TestEqual(TEXT("an attacker in the kill zone is not a bypass"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, static_cast<uint8>(0));
		S.NearestAttackerFromObjectiveCm = 800.f;
		S.KillZoneFromObjectiveCm = 0.f;
		TestEqual(TEXT("no kill zone: rule 2 is off"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, static_cast<uint8>(0));
	}
	// Rule 4: attackers' time to the zone edge < the hammer's walk time + margin. Path 25 m at 725 cm/s = 3.45 s; + 2 s margin = 5.45 s.
	{
		FPmcFallBackSituation S = BaseSituation();
		S.HammerPathCm = 2500.f;
		S.NearestAttackerFromObjectiveCm = 2000.f; // edge 14 m / 220 = 6.36 s: slack 0.91 s
		FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("enough time: no rule"), D.Rules, static_cast<uint8>(0));
		TestEqual(TEXT("the slack is attacker time - walk time - margin"), D.SlackS, 14.f * 100.f / 220.f - 2500.f / 725.f - 2.f, 0.01f);
		S.NearestAttackerFromObjectiveCm = 1700.f; // edge 11 m / 220 = 5 s: slack -0.45 s
		D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("not in time: rule 4"), D.Rules, R4);
		TestTrue(TEXT("rule 4 falls back even before the first shot"), D.bFallBack);
		S.bDefenceHasFired = false;
		TestEqual(TEXT("rule 4 forces it before the first shot"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, R4);
		S.HammerPathCm = -1.f;
		TestEqual(TEXT("an unknown route: rule 4 is off"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, static_cast<uint8>(0));
		S.HammerPathCm = 2500.f;
		S.HammerSpeedCm = 0.f;
		TestEqual(TEXT("a hammer that cannot walk: rule 4 is off"), PmcDefenceRoles::DecideFallBack(S, Config).Rules, static_cast<uint8>(0));
	}
	// Several rules at once, the finish gate.
	{
		FPmcFallBackSituation S = BaseSituation();
		S.bZoneHeld = false;
		S.NearestAttackerFromObjectiveCm = 800.f;
		S.HammerPathCm = 2500.f;
		FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestEqual(TEXT("rules 1, 2 and 4 together"), D.Rules, static_cast<uint8>(R1 | R2 | R4));
		TestEqual(TEXT("the rule list names them"), PmcDefenceRoles::RuleList(D.Rules), FString(TEXT("1+2+4")));
		TestEqual(TEXT("no rule lists a dash"), PmcDefenceRoles::RuleList(0), FString(TEXT("-")));
		S.AttackersDown = 1;
		TestTrue(TEXT("1 of 3 down: still falls back"), PmcDefenceRoles::DecideFallBack(S, Config).bFallBack);
		S.AttackersDown = 2;
		D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestFalse(TEXT(">= 2 down: nobody falls back"), D.bFallBack);
		TestTrue(TEXT(">= 2 down: held by the finish gate"), D.bHeldByFinish);
		TestEqual(TEXT("the rules are still reported"), D.Rules, static_cast<uint8>(R1 | R2 | R4));
		S.NearestAttackerFromObjectiveCm = -1.f;
		D = PmcDefenceRoles::DecideFallBack(S, Config);
		TestFalse(TEXT("nothing to hold when no rule fires"), D.bHeldByFinish);
	}
	TestEqual(TEXT("rule names"), FString(PmcDefenceRoles::RuleName(EPmcFallBackRule::Bypassed)), FString(TEXT("bypassed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcRule4AfterFirstFireTest, "CodexTactics.AI.PmcDefenceRoles.Rule4AfterFirstFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcRule4AfterFirstFireTest::RunTest(const FString&)
{
	// T-08 slice 2 (designer decision 2026-11-21): with rule4_after_first_fire rule 4 waits for the defence's first shot; rules 1 / 2 and the finish gate
	// are unchanged. Off (the shipped value) = slice 1: rule 4 fires on the clock alone.
	using namespace PmcDefenceRolesTest;
	const uint8 R1 = static_cast<uint8>(EPmcFallBackRule::ZoneEmptying);
	const uint8 R4 = static_cast<uint8>(EPmcFallBackRule::NotInTime);
	FPmcDefenceRolesConfig Off;
	FPmcDefenceRolesConfig On;
	On.bRule4AfterFirstFire = true;
	TestFalse(TEXT("the code default is off (slice 1 behaviour)"), Off.bRule4AfterFirstFire);

	// Not in time (edge 11 m / 220 = 5 s vs walk 3.45 s + 2 s), the zone held, nobody down.
	FPmcFallBackSituation S = BaseSituation();
	S.HammerPathCm = 2500.f;
	S.NearestAttackerFromObjectiveCm = 1700.f;
	S.KillZoneFromObjectiveCm = 0.f; // rule 2 out of the way
	S.bDefenceHasFired = false;
	{
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, Off);
		TestEqual(TEXT("off, before the first shot: rule 4 fires"), D.Rules, R4);
		TestTrue(TEXT("off, before the first shot: falls back"), D.bFallBack);
	}
	{
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, On);
		TestEqual(TEXT("on, before the first shot: rule 4 waits"), D.Rules, static_cast<uint8>(0));
		TestFalse(TEXT("on, before the first shot: no fall-back"), D.bFallBack);
		TestFalse(TEXT("on, before the first shot: nothing for the finish gate"), D.bHeldByFinish);
		TestEqual(TEXT("on: the slack is still computed (log lines)"), D.SlackS, 11.f * 100.f / 220.f - 2500.f / 725.f - 2.f, 0.01f);
	}
	S.bDefenceHasFired = true;
	{
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, On);
		TestEqual(TEXT("on, after the first shot: rule 4 fires"), D.Rules, R4);
		TestTrue(TEXT("on, after the first shot: falls back"), D.bFallBack);
		TestEqual(TEXT("off, after the first shot: rule 4 fires too"), PmcDefenceRoles::DecideFallBack(S, Off).Rules, R4);
	}
	// Rule 1 is not gated by the setting: an empty zone with an attacker near still falls back before the first shot.
	S.bDefenceHasFired = false;
	S.bZoneHeld = false;
	{
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, On);
		TestEqual(TEXT("on, before the first shot: rule 1 alone"), D.Rules, R1);
		TestTrue(TEXT("on: rule 1 still falls back before the first shot"), D.bFallBack);
		TestEqual(TEXT("off: rules 1 + 4"), PmcDefenceRoles::DecideFallBack(S, Off).Rules, static_cast<uint8>(R1 | R4));
	}
	// The finish gate holds whatever the setting.
	S.bDefenceHasFired = true;
	S.AttackersDown = 2;
	{
		const FPmcFallBackDecision D = PmcDefenceRoles::DecideFallBack(S, On);
		TestEqual(TEXT("on, >= 2 down: the rules are reported"), D.Rules, static_cast<uint8>(R1 | R4));
		TestFalse(TEXT("on, >= 2 down: nobody falls back"), D.bFallBack);
		TestTrue(TEXT("on, >= 2 down: held by the finish gate"), D.bHeldByFinish);
	}
	// Rule 2 keeps its own first-shot gate (unchanged).
	{
		FPmcFallBackSituation Bypass = BaseSituation();
		Bypass.NearestAttackerFromObjectiveCm = 800.f;
		Bypass.bDefenceHasFired = true;
		TestEqual(TEXT("on: rule 2 unchanged"), PmcDefenceRoles::DecideFallBack(Bypass, On).Rules, static_cast<uint8>(EPmcFallBackRule::Bypassed));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcFallBackBoundingTest, "CodexTactics.AI.PmcDefenceRoles.Bounding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcFallBackBoundingTest::RunTest(const FString&)
{
	const FPmcDefenceRolesConfig Config; // bound_max_wait_s 6
	const TArray<FPmcMoverCandidate> Two = { { 3, 2.f }, { 5, 4.f } };
	TestEqual(TEXT("nobody to move"), PmcDefenceRoles::PickMover(TConstArrayView<FPmcMoverCandidate>(), false, 99.f, Config), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("the longer walk moves first (the nearer one covers)"), PmcDefenceRoles::PickMover(Two, false, 99.f, Config), 5);
	TestEqual(TEXT("a mover is on his way: the other waits"), PmcDefenceRoles::PickMover(Two, true, 2.f, Config), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("... until bound_max_wait_s has passed"), PmcDefenceRoles::PickMover(Two, true, 6.f, Config), 5);
	const TArray<FPmcMoverCandidate> One = { { 3, 2.f } };
	TestEqual(TEXT("the second hammer moves when the first is in (no mover moving)"), PmcDefenceRoles::PickMover(One, false, 0.5f, Config), 3);
	const TArray<FPmcMoverCandidate> Tie = { { 7, 3.f }, { 2, 3.f }, { 4, 3.f } };
	TestEqual(TEXT("a tie goes to the lower squad index"), PmcDefenceRoles::PickMover(Tie, false, 99.f, Config), 2);
	FPmcDefenceRolesConfig NoWait = Config;
	NoWait.BoundMaxWaitS = 0.f;
	TestEqual(TEXT("bound_max_wait_s 0: no bounding"), PmcDefenceRoles::PickMover(Two, true, 0.f, NoWait), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcZonePostOrderTest, "CodexTactics.AI.PmcDefenceRoles.ZonePosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcZonePostOrderTest::RunTest(const FString&)
{
	using namespace PmcDefenceRolesTest;
	const FPmcDefenceRolesConfig Config; // no_line 0.6, dist_weight 0.3, spacing 2.5 m
	// The score: quality x line x distance.
	TestEqual(TEXT("a perfect post at the centre scores its quality"), PmcDefenceRoles::ZonePostScore(0.8f, true, 0.f, 600.f, Config), 0.8f, 0.0001f);
	TestEqual(TEXT("at the edge: x (1 - 0.3)"), PmcDefenceRoles::ZonePostScore(1.f, true, 600.f, 600.f, Config), 0.7f, 0.0001f);
	TestEqual(TEXT("beyond the radius counts as the edge"), PmcDefenceRoles::ZonePostScore(1.f, true, 9000.f, 600.f, Config), 0.7f, 0.0001f);
	TestEqual(TEXT("no line to the approach: x 0.6"), PmcDefenceRoles::ZonePostScore(1.f, false, 0.f, 600.f, Config), 0.6f, 0.0001f);
	TestEqual(TEXT("no cover scores 0"), PmcDefenceRoles::ZonePostScore(0.f, true, 0.f, 600.f, Config), 0.f, 0.0001f);
	TestTrue(TEXT("a post that fires from the edge beats one that cannot fire at the centre"),
		PmcDefenceRoles::ZonePostScore(1.f, true, 600.f, 600.f, Config) > PmcDefenceRoles::ZonePostScore(1.f, false, 0.f, 600.f, Config));

	// The pick (Posts are best first).
	TArray<FFiringCandidate> Posts;
	Posts.Add(MakeZonePost(TEXT("a"), FVector(0.f, 0.f, 0.f), 0.9f, TEXT("wallA")));
	Posts.Add(MakeZonePost(TEXT("b"), FVector(100.f, 0.f, 0.f), 0.8f, TEXT("wallB")));   // 1 m from a
	Posts.Add(MakeZonePost(TEXT("c"), FVector(0.f, 400.f, 0.f), 0.7f, TEXT("wallC")));   // 4 m from a
	Posts.Add(MakeZonePost(TEXT("d"), FVector(-400.f, 0.f, 0.f), 0.6f, TEXT("wallA")));  // the same cover object as a
	const TArray<int32> NoExcluded;
	const TArray<FVector> NoStands;
	const TArray<FString> NoKeys;
	TestEqual(TEXT("nobody holds anything: the best"), PmcDefenceRoles::PickZonePost(Posts, NoExcluded, NoStands, NoKeys, Config), 0);
	TestEqual(TEXT("a holder on a: the next that keeps the spacing is c (b is 1 m away, d shares the wall)"),
		PmcDefenceRoles::PickZonePost(Posts, NoExcluded, { FVector(0.f, 0.f, 0.f) }, { FString(TEXT("wallA")) }, Config), 2);
	TestEqual(TEXT("an excluded post is skipped"), PmcDefenceRoles::PickZonePost(Posts, { 0 }, NoStands, NoKeys, Config), 1);
	// Relaxation: only posts that break the spacing remain -> a free cover object, however near.
	TArray<FFiringCandidate> Tight;
	Tight.Add(MakeZonePost(TEXT("a"), FVector(0.f, 0.f, 0.f), 0.9f, TEXT("wallA")));
	Tight.Add(MakeZonePost(TEXT("b"), FVector(100.f, 0.f, 0.f), 0.8f, TEXT("wallB")));
	Tight.Add(MakeZonePost(TEXT("d"), FVector(200.f, 0.f, 0.f), 0.6f, TEXT("wallA")));
	TestEqual(TEXT("no post keeps 2.5 m: the free cover object b"), PmcDefenceRoles::PickZonePost(Tight, NoExcluded, { FVector(0.f, 0.f, 0.f) }, { FString(TEXT("wallA")) }, Config), 1);
	// Last resort: every cover object is held - any stand point nobody stands on (1 m).
	TestEqual(TEXT("every wall held: the first post 1 m from the claimed ones"),
		PmcDefenceRoles::PickZonePost(Tight, NoExcluded, { FVector(0.f, 0.f, 0.f) }, { FString(TEXT("wallA")), FString(TEXT("wallB")) }, Config), 1);
	TestEqual(TEXT("... but never the very same stand point"),
		PmcDefenceRoles::PickZonePost(Tight, NoExcluded, { FVector(0.f, 0.f, 0.f), FVector(100.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f) }, { FString(TEXT("wallA")), FString(TEXT("wallB")) }, Config),
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no posts: none"), PmcDefenceRoles::PickZonePost(TConstArrayView<FFiringCandidate>(), NoExcluded, NoStands, NoKeys, Config), static_cast<int32>(INDEX_NONE));
	TArray<FFiringCandidate> Invalid;
	Invalid.Add(MakeZonePost(TEXT("x"), FVector::ZeroVector, 0.9f, TEXT("w")));
	Invalid[0].Position.bValid = false;
	TestEqual(TEXT("an invalid post is never picked"), PmcDefenceRoles::PickZonePost(Invalid, NoExcluded, NoStands, NoKeys, Config), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcKillZoneRouteTest, "CodexTactics.AI.PmcDefenceRoles.KillZoneRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcKillZoneRouteTest::RunTest(const FString&)
{
	const FVector KillZone(0.f, 0.f, 0.f);
	const float Clear = 800.f;
	TestTrue(TEXT("a straight route over the kill zone crosses it"), PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 0.f, 0.f), FVector(3000.f, 0.f, 0.f) }, KillZone, Clear));
	TestTrue(TEXT("a route passing 3 m from it crosses it (the segment interior counts, not only its points)"),
		PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 300.f, 0.f), FVector(3000.f, 300.f, 0.f) }, KillZone, Clear));
	TestFalse(TEXT("a route passing 12 m from it does not"), PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 1200.f, 0.f), FVector(3000.f, 1200.f, 0.f) }, KillZone, Clear));
	TestTrue(TEXT("a bend through the zone crosses it"),
		PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 2000.f, 0.f), FVector(0.f, 500.f, 0.f), FVector(3000.f, 2000.f, 0.f) }, KillZone, Clear));
	// A hammer standing 4 m from the kill zone is not refused for the start of his own walk: the limit is 0.7 x 4 m = 2.8 m.
	TestFalse(TEXT("walking away from a start 4 m from the zone is fine"), PmcDefenceRoles::PathCrossesKillZone({ FVector(400.f, 0.f, 0.f), FVector(3000.f, 0.f, 0.f) }, KillZone, Clear));
	TestTrue(TEXT("walking through it from 4 m away is not"), PmcDefenceRoles::PathCrossesKillZone({ FVector(400.f, 0.f, 0.f), FVector(-3000.f, 0.f, 0.f) }, KillZone, Clear));
	TestFalse(TEXT("clear 0 = the test is off"), PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 0.f, 0.f), FVector(3000.f, 0.f, 0.f) }, KillZone, 0.f));
	TestFalse(TEXT("fewer than two points: no route"), PmcDefenceRoles::PathCrossesKillZone({ FVector(0.f, 0.f, 0.f) }, KillZone, Clear));
	TestFalse(TEXT("the height does not matter (planar)"), PmcDefenceRoles::PathCrossesKillZone({ FVector(-3000.f, 1200.f, 900.f), FVector(3000.f, 1200.f, -900.f) }, KillZone, Clear));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceRolesConfigTest, "CodexTactics.AI.PmcDefenceRoles.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceRolesConfigTest::RunTest(const FString&)
{
	using namespace PmcDefenceRolesTest;
	const FPmcDefenceRolesConfig Defaults;
	TestFalse(TEXT("the code default is OFF"), Defaults.bEnabled);

	const TSharedPtr<FJsonObject> Block = ParseBlock(TEXT(R"JSON({
		"enabled": true, "holder_cover": false, "fall_back": false, "zone_near_m": 30, "bypass_margin_m": -3, "finish_down_min": 99, "known_max_age_s": 99,
		"not_in_time_margin_s": 99, "bound_max_wait_s": -1, "move_timeout_s": 0, "kill_zone_clear_m": 10, "zone_min_quality": 2, "zone_no_line_factor": -1,
		"zone_dist_weight": 5, "zone_min_spacing_m": 4, "zone_ray_reach_cm": 5, "max_zone_posts": 0, "max_path_queries": 500, "rule4_after_first_fire": true, "ignored_key": 7
	})JSON"));
	TestTrue(TEXT("the block parses"), Block.IsValid());
	if (Block.IsValid())
	{
		const FPmcDefenceRolesConfig Read = PmcDefenceRoles::ConfigFromJson(*Block);
		TestTrue(TEXT("enabled read"), Read.bEnabled);
		TestFalse(TEXT("holder_cover false read"), Read.bHolderCover);
		TestFalse(TEXT("fall_back false read"), Read.bFallBack);
		TestEqual(TEXT("zone_near_m is metres in the file, cm here"), Read.ZoneNearCm, 3000.f, 0.001f);
		TestEqual(TEXT("bypass_margin_m is at least 0"), Read.BypassMarginCm, 0.f, 0.001f);
		TestEqual(TEXT("finish_down_min clamps to 10"), Read.FinishDownMin, 10);
		TestEqual(TEXT("known_max_age_s clamps to 30"), Read.KnownMaxAgeS, 30.f, 0.001f);
		TestEqual(TEXT("not_in_time_margin_s clamps to 10"), Read.NotInTimeMarginS, 10.f, 0.001f);
		TestEqual(TEXT("bound_max_wait_s is at least 0"), Read.BoundMaxWaitS, 0.f, 0.001f);
		TestEqual(TEXT("move_timeout_s is at least 1"), Read.MoveTimeoutS, 1.f, 0.001f);
		TestEqual(TEXT("kill_zone_clear_m read in metres"), Read.KillZoneClearCm, 1000.f, 0.001f);
		TestEqual(TEXT("zone_min_quality clamps to 1"), Read.ZoneMinQuality, 1.f, 0.001f);
		TestEqual(TEXT("zone_no_line_factor clamps to 0"), Read.ZoneNoLineFactor, 0.f, 0.001f);
		TestEqual(TEXT("zone_dist_weight clamps to 1"), Read.ZoneDistWeight, 1.f, 0.001f);
		TestEqual(TEXT("zone_min_spacing_m read in metres"), Read.ZoneMinSpacingCm, 400.f, 0.001f);
		TestEqual(TEXT("zone_ray_reach_cm is at least 100"), Read.ZoneRayReachCm, 100.f, 0.001f);
		TestEqual(TEXT("max_zone_posts is at least 1"), Read.MaxZonePosts, 1);
		TestEqual(TEXT("max_path_queries clamps to 32"), Read.MaxPathQueries, 32);
		TestTrue(TEXT("rule4_after_first_fire read"), Read.bRule4AfterFirstFire);
		const TSharedPtr<FJsonObject> Empty = ParseBlock(TEXT("{}"));
		TestTrue(TEXT("the empty block parses"), Empty.IsValid());
		if (Empty.IsValid())
		{
			const FPmcDefenceRolesConfig Kept = PmcDefenceRoles::ConfigFromJson(*Empty);
			TestEqual(TEXT("a missing key keeps the default"), Kept.ZoneNearCm, Defaults.ZoneNearCm, 0.001f);
			TestEqual(TEXT("a missing switch keeps the default"), Kept.bEnabled, Defaults.bEnabled);
		}
	}

	// The shipped file mirrors the code defaults, and ships with the layer OFF (the lead decides whether to switch it on).
	const FPmcDefenceRolesConfig Shipped = PmcDefenceRoles::LoadConfig();
	TestFalse(TEXT("shipped enabled is false"), Shipped.bEnabled);
	TestEqual(TEXT("shipped holder_cover"), Shipped.bHolderCover, Defaults.bHolderCover);
	TestEqual(TEXT("shipped fall_back"), Shipped.bFallBack, Defaults.bFallBack);
	TestEqual(TEXT("shipped zone_near_m"), Shipped.ZoneNearCm, Defaults.ZoneNearCm, 0.001f);
	TestEqual(TEXT("shipped bypass_margin_m"), Shipped.BypassMarginCm, Defaults.BypassMarginCm, 0.001f);
	TestEqual(TEXT("shipped finish_down_min"), Shipped.FinishDownMin, Defaults.FinishDownMin);
	TestEqual(TEXT("shipped known_max_age_s"), Shipped.KnownMaxAgeS, Defaults.KnownMaxAgeS, 0.001f);
	TestEqual(TEXT("shipped not_in_time_margin_s"), Shipped.NotInTimeMarginS, Defaults.NotInTimeMarginS, 0.001f);
	TestEqual(TEXT("shipped bound_max_wait_s"), Shipped.BoundMaxWaitS, Defaults.BoundMaxWaitS, 0.001f);
	TestEqual(TEXT("shipped move_timeout_s"), Shipped.MoveTimeoutS, Defaults.MoveTimeoutS, 0.001f);
	TestEqual(TEXT("shipped kill_zone_clear_m"), Shipped.KillZoneClearCm, Defaults.KillZoneClearCm, 0.001f);
	TestEqual(TEXT("shipped zone_min_quality"), Shipped.ZoneMinQuality, Defaults.ZoneMinQuality, 0.001f);
	TestEqual(TEXT("shipped zone_no_line_factor"), Shipped.ZoneNoLineFactor, Defaults.ZoneNoLineFactor, 0.001f);
	TestEqual(TEXT("shipped zone_dist_weight"), Shipped.ZoneDistWeight, Defaults.ZoneDistWeight, 0.001f);
	TestEqual(TEXT("shipped zone_min_spacing_m"), Shipped.ZoneMinSpacingCm, Defaults.ZoneMinSpacingCm, 0.001f);
	TestEqual(TEXT("shipped zone_ray_reach_cm"), Shipped.ZoneRayReachCm, Defaults.ZoneRayReachCm, 0.001f);
	TestEqual(TEXT("shipped max_zone_posts"), Shipped.MaxZonePosts, Defaults.MaxZonePosts);
	TestEqual(TEXT("shipped max_path_queries"), Shipped.MaxPathQueries, Defaults.MaxPathQueries);
	TestFalse(TEXT("shipped rule4_after_first_fire is false (slice 1 behaviour)"), Shipped.bRule4AfterFirstFire);
	return true;
}

#endif
