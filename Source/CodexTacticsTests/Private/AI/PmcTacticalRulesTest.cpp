#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcTacticalRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// TACTICAL_MAP.md P2 (designer 2026-11-17: the dangerous places affect gameplay): the PMC commander's terrain terms and the squad's
// placement on the analysis spots (AI/PmcTacticalRules.h). Pure rules.

namespace PmcTacticalRulesTest
{
	/** A rifleman in contact, exposed in the open with cover nearby and a seen threat 30 m off. */
	FPmcFighterView TacticalFighter()
	{
		FPmcFighterView View;
		View.Location = FVector::ZeroVector;
		View.bHasThreat = true;
		View.Threat.Location = FVector(3000.f, 0.f, 0.f);
		View.Threat.DistanceCm = 3000.f;
		View.Threat.bSeenNow = true;
		View.Threat.SecondsSince = 0.f;
		View.bCoverAvailable = true;
		View.bExposed = true;
		View.WeaponRangeCm = 4000.f;
		return View;
	}

	FPmcSquadView TacticalSquad()
	{
		FPmcSquadView View;
		View.Alive = 3;
		View.EnemiesKnown = 3;
		View.EnemiesSeenNow = 1;
		View.bContact = true;
		View.bEverContact = true;
		return View;
	}

	FPmcPostCandidate Candidate(EPmcPostKind Kind, const FVector& Location, float Score, const FString& Group, float Danger = 0.f)
	{
		FPmcPostCandidate Out;
		Out.Kind = Kind;
		Out.Location = Location;
		Out.Score = Score;
		Out.Group = Group;
		Out.Danger = Danger;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalNoMapTest, "CodexTactics.AI.PmcTactical.NoMapUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalNoMapTest::RunTest(const FString&)
{
	// No tactical file: danger 0, no post - every order scores exactly what it scored before P2.
	using namespace PmcTacticalRulesTest;
	const FPmcWeights W;
	FPmcWeights Off = W;
	Off.TerrainDanger = 0.f;
	Off.AmbushExit = Off.OverwatchRoof = Off.WindowPosition = Off.RouteCover = 0.f;
	FPmcFighterView F = TacticalFighter();
	FPmcSquadView S = TacticalSquad();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		S.bEverContact = Pass == 1;
		for (int32 Index = 0; Index < static_cast<int32>(EPmcOrder::Count); ++Index)
		{
			const EPmcOrder Order = static_cast<EPmcOrder>(Index);
			TestEqual(FString::Printf(TEXT("%s unchanged at danger 0"), PmcCommanderRules::OrderName(Order)),
				PmcCommanderRules::ScoreOrder(Order, F, S, W), PmcCommanderRules::ScoreOrder(Order, F, S, Off));
		}
	}
	TestEqual(TEXT("position factor 1 at danger 0"), PmcTacticalRules::PositionFactor(0.f, W), 1.f);
	TestEqual(TEXT("leave factor 1 at danger 0"), PmcTacticalRules::LeaveFactor(0.f, W), 1.f);
	TestEqual(TEXT("slot factor 1 without danger / route"), PmcTacticalRules::SlotTerrainFactor(0.f, 0.f, false, W), 1.f);
	const TArray<float> Angles = PmcTacticalRules::FallBackAngles();
	const TArray<float> Zero = { 0.f, 0.f, 0.f, 0.f, 0.f };
	TestEqual(TEXT("fall back straight away without danger"), PmcTacticalRules::PickFallBack(Angles, Zero, W), 0);
	const TArray<FPmcTacticalPost> Posts = PmcTacticalRules::PlanPlacement({ FPmcPlacementFighter(), FPmcPlacementFighter() }, {}, W, 7);
	TestTrue(TEXT("no candidates = no posts (the old line spawn)"), Posts.Num() == 2 && !Posts[0].IsValid() && !Posts[1].IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalDangerTermTest, "CodexTactics.AI.PmcTactical.DangerOfMyPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalDangerTermTest::RunTest(const FString&)
{
	using namespace PmcTacticalRulesTest;
	const FPmcWeights W;
	FPmcFighterView Safe = TacticalFighter();
	Safe.bInCover = true;
	Safe.CoverQuality = 0.2f;
	FPmcFighterView Choke = Safe;
	Choke.DangerHere = 0.8f;
	const FPmcSquadView S = TacticalSquad();
	TestTrue(TEXT("Hold inside a chokepoint is worth less"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Choke, S, W) < PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Safe, S, W));
	TestTrue(TEXT("Suppress inside a chokepoint is worth less"), PmcCommanderRules::ScoreOrder(EPmcOrder::Suppress, Choke, S, W) < PmcCommanderRules::ScoreOrder(EPmcOrder::Suppress, Safe, S, W));
	TestTrue(TEXT("TakeCover out of a chokepoint is worth more"), PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, Choke, S, W) > PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, Safe, S, W));
	TestNearlyEqual(TEXT("Hold x (1 - 0.6 x 0.8)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Choke, S, W),
		PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Safe, S, W) * (1.f - 0.6f * 0.8f), 1e-4f);
	// Relocate: not spotted long enough at his spot, but standing in a chokepoint (>= choke_leave_danger) he leaves it.
	FPmcFighterView Fresh = Choke;
	Fresh.SecondsExposedHere = 0.f;
	FPmcFighterView FreshSafe = Safe;
	FreshSafe.SecondsExposedHere = 0.f;
	TestEqual(TEXT("no relocation in the open before the spotted timer"), PmcCommanderRules::ScoreOrder(EPmcOrder::Relocate, FreshSafe, S, W), 0.f);
	TestTrue(TEXT("relocation out of a chokepoint before the spotted timer"), PmcCommanderRules::ScoreOrder(EPmcOrder::Relocate, Fresh, S, W) > 0.f);
	Fresh.DangerHere = 0.3f;
	TestEqual(TEXT("a low danger is no reason to relocate"), PmcCommanderRules::ScoreOrder(EPmcOrder::Relocate, Fresh, S, W), 0.f);
	// A man on his post is not penalised for its danger (the placement already weighed it).
	FPmcFighterView OnPost = Choke;
	OnPost.bAtPost = true;
	OnPost.PostKind = EPmcPostKind::Ambush;
	TestEqual(TEXT("Hold on a post ignores the danger"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, OnPost, S, W), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Safe, S, W));
	// FallBack round the chokepoint, not into it.
	const TArray<float> Angles = PmcTacticalRules::FallBackAngles();
	TestEqual(TEXT("straight away leads into a chokepoint: the side that does not"), PmcTacticalRules::PickFallBack(Angles, TArray<float>{ 0.9f, 0.9f, 0.f, 0.9f, 0.9f }, W), 2);
	TestEqual(TEXT("a small danger keeps the straight retreat"), PmcTacticalRules::PickFallBack(Angles, TArray<float>{ 0.1f, 0.f, 0.f, 0.f, 0.f }, W), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalAmbushPostTest, "CodexTactics.AI.PmcTactical.AmbushPost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalAmbushPostTest::RunTest(const FString&)
{
	using namespace PmcTacticalRulesTest;
	const FPmcWeights W;
	FPmcSquadView Waiting = TacticalSquad();
	Waiting.bContact = false;
	Waiting.bEverContact = false;
	FPmcFighterView Plain = TacticalFighter();
	Plain.bHasThreat = false;
	FPmcFighterView Posted = Plain;
	Posted.PostKind = EPmcPostKind::Ambush;
	Posted.PostScore = 0.8f;
	const float Base = PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, Plain, Waiting, W);
	TestNearlyEqual(TEXT("an ambush post at a chokepoint exit: x (1 + ambush_exit x score)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, Posted, Waiting, W),
		Base * (1.f + W.AmbushExit * 0.8f), 1e-4f);
	Posted.PostKind = EPmcPostKind::Overwatch;
	TestNearlyEqual(TEXT("an overwatch roof post: x (1 + overwatch_roof x score)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, Posted, Waiting, W),
		Base * (1.f + W.OverwatchRoof * 0.8f), 1e-4f);
	Posted.PostKind = EPmcPostKind::Window;
	TestNearlyEqual(TEXT("a window post: x (1 + window_position x score)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, Posted, Waiting, W),
		Base * (1.f + W.WindowPosition * 0.8f), 1e-4f);
	FPmcFighterView InChoke = Plain;
	InChoke.DangerHere = 1.f;
	TestTrue(TEXT("an ambush IN a chokepoint (no post) is worth less"), PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, InChoke, Waiting, W) < Base);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalPlacementTest, "CodexTactics.AI.PmcTactical.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalPlacementTest::RunTest(const FString&)
{
	using namespace PmcTacticalRulesTest;
	FPmcWeights W;
	W.WindowShare = 0.f; // riflemen to the ambush spots first
	TArray<FPmcPlacementFighter> Fighters;
	Fighters.SetNum(4);
	Fighters[0].BandMaxCm = 2375.f;
	Fighters[1].BandMaxCm = 2850.f; // the longest reach
	Fighters[2].BandMaxCm = 2375.f;
	Fighters[3].BandMaxCm = 1330.f;
	PmcTacticalRules::MarkMarksmen(Fighters);
	TestTrue(TEXT("nobody with a sniper rifle: the longest reach is the marksman"), Fighters[1].bMarksman && !Fighters[0].bMarksman && !Fighters[2].bMarksman);
	TArray<FPmcPostCandidate> Candidates;
	Candidates.Add(Candidate(EPmcPostKind::Ambush, FVector(0.f, 0.f, 0.f), 0.9f, TEXT("HS_inside"), 0.9f)); // inside the chokepoint
	Candidates.Add(Candidate(EPmcPostKind::Ambush, FVector(1000.f, 0.f, 0.f), 0.7f, TEXT("HS_exit")));
	Candidates.Add(Candidate(EPmcPostKind::Ambush, FVector(1100.f, 0.f, 0.f), 0.65f, TEXT("HS_exit_close"))); // 1 m from the exit spot
	Candidates.Add(Candidate(EPmcPostKind::Ambush, FVector(2000.f, 500.f, 0.f), 0.5f, TEXT("HS_far")));
	Candidates.Add(Candidate(EPmcPostKind::Overwatch, FVector(500.f, 3000.f, 900.f), 0.6f, TEXT("B_tower")));
	Candidates.Add(Candidate(EPmcPostKind::Overwatch, FVector(600.f, 3000.f, 900.f), 0.8f, TEXT("B_tower")));
	Candidates[4].RangeCm = 9000.f;
	Candidates[5].RangeCm = 11000.f;
	Candidates.Add(Candidate(EPmcPostKind::LadderRoof, FVector(-2000.f, 0.f, 300.f), 0.5f, TEXT("B_gallery")));
	FPmcPostCandidate Drop = Candidate(EPmcPostKind::Drop, FVector(1500.f, -400.f, 300.f), 1.f, TEXT("DA_01"));
	Drop.Jumpers = { TEXT("cutter"), TEXT("frost_hound") };
	Candidates.Add(Drop);
	const TArray<FPmcTacticalPost> Posts = PmcTacticalRules::PlanPlacement(Fighters, Candidates, W, 11);
	TestEqual(TEXT("one post per fighter"), Posts.Num(), 4);
	TestTrue(TEXT("the marksman on the best overwatch roof spot"), Posts[1].Kind == EPmcPostKind::Overwatch && Posts[1].Location.X == 600.f);
	TestEqual(TEXT("the marksman's post keeps that roof's range_m"), Posts[1].RangeCm, 11000.f);
	TestTrue(TEXT("the last rifleman on the picked ladder-only roof"), Posts[3].Kind == EPmcPostKind::LadderRoof);
	TestTrue(TEXT("the lead ambusher at the chokepoint exit, not inside it"), Posts[0].Kind == EPmcPostKind::Ambush && Posts[0].Group == TEXT("HS_exit"));
	TestTrue(TEXT("the next ambusher keeps 3 m off the first, never inside the chokepoint"), Posts[2].Kind == EPmcPostKind::Ambush && Posts[2].Group == TEXT("HS_far"));
	for (const FPmcTacticalPost& Post : Posts)
	{
		TestTrue(TEXT("a PMC operative never takes a cutter / hound drop spot"), Post.Kind != EPmcPostKind::Drop);
	}
	// A jumper archetype takes the drop edge.
	TArray<FPmcPlacementFighter> Jumpers;
	Jumpers.SetNum(1);
	Jumpers[0].Archetype = TEXT("cutter");
	Jumpers[0].bMarksman = false;
	TArray<FPmcPostCandidate> NoRoof = Candidates.FilterByPredicate([](const FPmcPostCandidate& C) { return C.Kind != EPmcPostKind::Overwatch; });
	const TArray<FPmcTacticalPost> JumperPosts = PmcTacticalRules::PlanPlacement(Jumpers, NoRoof, W, 11);
	TestTrue(TEXT("a cutter goes onto the drop edge"), JumperPosts.Num() == 1 && JumperPosts[0].Kind == EPmcPostKind::Drop);
	TestTrue(TEXT("jumpers list: any archetype listed / 'any'"), PmcTacticalRules::CanJump(TEXT("PMC"), TArray<FString>{ TEXT("any") }) && !PmcTacticalRules::CanJump(TEXT("pmc"), Drop.Jumpers));
	// Deterministic, and window_share 1 sends the riflemen to the windows.
	TestTrue(TEXT("deterministic for the same seed"), PmcTacticalRules::PlanPlacement(Fighters, Candidates, W, 11)[2].Group == Posts[2].Group);
	W.WindowShare = 1.f;
	Candidates.Add(Candidate(EPmcPostKind::Window, FVector(800.f, 1500.f, 0.f), 0.4f, TEXT("B_shed:0")));
	const TArray<FPmcTacticalPost> Windowed = PmcTacticalRules::PlanPlacement(Fighters, Candidates, W, 11);
	TestTrue(TEXT("window_share 1: the squad's ambusher stays at the chokepoint exit"), Windowed[0].Kind == EPmcPostKind::Ambush && Windowed[0].Group == TEXT("HS_exit"));
	TestTrue(TEXT("window_share 1: the next rifleman at the window"), Windowed[2].Kind == EPmcPostKind::Window);
	// Ladder roof men 0: nobody goes up the ladder-only roof.
	W.LadderRoofMen = 0;
	for (const FPmcTacticalPost& Post : PmcTacticalRules::PlanPlacement(Fighters, Candidates, W, 11))
	{
		TestTrue(TEXT("ladder_roof_men 0"), Post.Kind != EPmcPostKind::LadderRoof);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalGeometryTest, "CodexTactics.AI.PmcTactical.RouteGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalGeometryTest::RunTest(const FString&)
{
	const TArray<FVector> Route = { FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FVector(1000.f, 1000.f, 0.f) };
	TestTrue(TEXT("nearest point on the first leg"), PmcTacticalRules::NearestOnRoute(Route, FVector(400.f, 300.f, 0.f)).Equals(FVector(400.f, 0.f, 0.f), 0.1f));
	TestTrue(TEXT("nearest point on the second leg"), PmcTacticalRules::NearestOnRoute(Route, FVector(1300.f, 600.f, 0.f)).Equals(FVector(1000.f, 600.f, 0.f), 0.1f));
	TestNearlyEqual(TEXT("distance to the route"), PmcTacticalRules::DistanceToRoute(Route, FVector(400.f, 300.f, 0.f)), 300.f, 0.1f);
	// A wall whose normal points away from the route (the man's back to the wall... the wall between him and the route) faces it fully.
	TestNearlyEqual(TEXT("wall squarely between the slot and the route"), PmcTacticalRules::RouteFacing(FVector(0.f, 500.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 0.f)), 1.f, 1e-4f);
	TestEqual(TEXT("wall on the far side: no cover from the route"), PmcTacticalRules::RouteFacing(FVector(0.f, 500.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(0.f, 0.f, 0.f)), 0.f);
	const FPmcWeights W;
	TestNearlyEqual(TEXT("route cover x (1 + 0.4) by an analysis cover spot"), PmcTacticalRules::SlotTerrainFactor(0.f, 1.f, true, W), 1.4f, 1e-4f);
	TestNearlyEqual(TEXT("route cover x (1 + 0.2) away from one"), PmcTacticalRules::SlotTerrainFactor(0.f, 1.f, false, W), 1.2f, 1e-4f);
	TestTrue(TEXT("at the post: 1 m off, same floor"), PmcTacticalRules::IsAtPost(FVector(100.f, 0.f, 20.f), FVector::ZeroVector, 200.f, 150.f));
	TestFalse(TEXT("not at a roof post from the street under it"), PmcTacticalRules::IsAtPost(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 900.f), 200.f, 150.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalWeightsJsonTest, "CodexTactics.AI.PmcTactical.WeightsJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalWeightsJsonTest::RunTest(const FString&)
{
	const FString Text = TEXT("{\"terrain_danger\": 0.75, \"ambush_exit\": 1.1, \"overwatch_roof\": 0.5, \"window_position\": 0.3, \"route_cover\": 0.25, ")
		TEXT("\"chokepoint_avoid\": 2.0, \"choke_leave_danger\": 0.4, \"window_share\": 0.2, \"tactical_reach_m\": 30, \"marksman_band_min_m\": 35, \"ladder_roof_men\": 2}");
	TSharedPtr<FJsonObject> Json;
	TestTrue(TEXT("parsed"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid());
	const FPmcWeights W = PmcCommanderRules::FromJson(*Json);
	TestNearlyEqual(TEXT("terrain_danger"), W.TerrainDanger, 0.75f, 1e-4f);
	TestNearlyEqual(TEXT("ambush_exit"), W.AmbushExit, 1.1f, 1e-4f);
	TestNearlyEqual(TEXT("overwatch_roof"), W.OverwatchRoof, 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("window_position"), W.WindowPosition, 0.3f, 1e-4f);
	TestNearlyEqual(TEXT("route_cover"), W.RouteCover, 0.25f, 1e-4f);
	TestNearlyEqual(TEXT("chokepoint_avoid clamped to 1"), W.ChokepointAvoid, 1.f, 1e-4f);
	TestNearlyEqual(TEXT("choke_leave_danger"), W.ChokeLeaveDanger, 0.4f, 1e-4f);
	TestNearlyEqual(TEXT("window_share"), W.WindowShare, 0.2f, 1e-4f);
	TestNearlyEqual(TEXT("tactical_reach_m in cm"), W.TacticalReachCm, 3000.f, 1e-2f);
	TestNearlyEqual(TEXT("marksman_band_min_m in cm"), W.MarksmanBandMinCm, 3500.f, 1e-2f);
	TestEqual(TEXT("ladder_roof_men"), W.LadderRoofMen, 2);
	// The shipped file carries every key (the trainer edits copies of it).
	FPmcWeights Shipped;
	TestTrue(TEXT("pmc_commander.json loads"), PmcCommanderRules::LoadWeightsFile(PmcCommanderRules::DefaultWeightsPath(), Shipped));
	TestNearlyEqual(TEXT("shipped terrain_danger"), Shipped.TerrainDanger, 0.6f, 1e-4f);
	TestNearlyEqual(TEXT("shipped tactical_reach_m"), Shipped.TacticalReachCm, 4000.f, 1e-2f);
	return true;
}

// G008b: the posted marksman fires from the parapet the opening shot crosses, about 60 cm inside it.
// Seed 11's spot faces across the roof; the street is the south edge. Seed 33's shot leaves the east edge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalOverwatchParapetTest, "CodexTactics.AI.PmcTactical.OverwatchParapet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalOverwatchParapetTest::RunTest(const FString&)
{
	const FBox2D Hall(FVector2D(3200.f, -3250.f), FVector2D(5700.f, 550.f));
	const FVector Lind(5600.f, -750.f, 900.f);
	const FVector2D Threat = FVector2D(2505.f - 5600.f, -8713.f - (-750.f)).GetSafeNormal();
	const FVector Snapped = PmcTacticalRules::SnapOverwatchSpotToParapet(Lind, Threat, Hall, 60.f);
	TestTrue(TEXT("seed 11 stays on B_hall_n"), Snapped.X > 3200.f && Snapped.X < 5700.f && Snapped.Y > -3250.f && Snapped.Y < 550.f);
	const float ToSouth = (-3250.f - static_cast<float>(Snapped.Y)) / static_cast<float>(Threat.Y);
	TestNearlyEqual(TEXT("seed 11: 60 cm inside the south parapet, the street the shot crosses"), ToSouth, 60.f, 1.f);
	const FVector2D Along(Snapped.X - Lind.X, Snapped.Y - Lind.Y);
	TestNearlyEqual(TEXT("the snap stays on the shot"), static_cast<float>(Along.X * Threat.Y - Along.Y * Threat.X), 0.f, 2.f);
	TestTrue(TEXT("he leaves the interior spot"), FVector::Dist2D(Snapped, Lind) > 2000.f);

	// Facing yaw -145 points across the roof to the west wall, not at the street.
	const FVector2D Facing(-0.8192f, -0.5736f);
	const FVector West = PmcTacticalRules::SnapOverwatchSpotToParapet(Lind, Facing, Hall, 60.f);
	TestTrue(TEXT("facing yaw -145 is the west parapet, not Lind's street"), West.X < 3400.f);
	TestTrue(TEXT("the threat snap is not that west point"), Snapped.X > 4000.f);

	const FBox2D HaasRoof(FVector2D(-12108.f, 613.f), FVector2D(-9975.f, 2125.f));
	const FVector Haas(-10075.f, 713.f, 900.f);
	const FVector2D HaasDir = FVector2D(-1504.f - (-10075.f), 15539.f - 713.f).GetSafeNormal();
	const FVector HaasSnap = PmcTacticalRules::SnapOverwatchSpotToParapet(Haas, HaasDir, HaasRoof, 60.f);
	const float ToEast = (-9975.f - static_cast<float>(HaasSnap.X)) / static_cast<float>(HaasDir.X);
	TestNearlyEqual(TEXT("seed 33: 60 cm inside the east parapet"), ToEast, 60.f, 1.f);
	TestTrue(TEXT("seed 33 stays on the roof"), HaasSnap.Y > 613.f && HaasSnap.Y < 2125.f);

	const FVector Close(5600.f, -3210.f, 900.f);
	const FVector Stayed = PmcTacticalRules::SnapOverwatchSpotToParapet(Close, FVector2D(0.f, -1.f), Hall, 60.f);
	TestNearlyEqual(TEXT("already within 60 cm of the parapet stays"), static_cast<float>(Stayed.Y), static_cast<float>(Close.Y), 0.1f);

	const FVector Street(2500.f, -8000.f, 0.f);
	const FVector Unmoved = PmcTacticalRules::SnapOverwatchSpotToParapet(Street, Threat, Hall, 60.f);
	TestNearlyEqual(TEXT("a spot off the roof is not pulled onto it"), static_cast<float>(Unmoved.X), static_cast<float>(Street.X), 0.1f);
	const FVector NoDir = PmcTacticalRules::SnapOverwatchSpotToParapet(Lind, FVector2D::ZeroVector, Hall, 60.f);
	TestNearlyEqual(TEXT("no shot direction stays"), static_cast<float>(NoDir.X), static_cast<float>(Lind.X), 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalScenarioTest, "CodexTactics.AI.PmcTactical.ArenaScenario",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalScenarioTest::RunTest(const FString&)
{
	int32 Tactical = 0;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		TestEqual(TEXT("no tactical map: exactly the old random pick"), static_cast<int32>(PmcCommanderRules::PickScenarioOnMap(Seed, false)),
			static_cast<int32>(PmcCommanderRules::PickScenario(Seed)));
		const EPmcScenario OnMap = PmcCommanderRules::PickScenarioOnMap(Seed, true);
		TestEqual(TEXT("deterministic"), static_cast<int32>(PmcCommanderRules::PickScenarioOnMap(Seed, true)), static_cast<int32>(OnMap));
		Tactical += OnMap == EPmcScenario::Tactical ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("about half the rounds on a tactical map are tactical (%d of 200)"), Tactical), Tactical >= 70 && Tactical <= 130);
	EPmcScenario Parsed = EPmcScenario::Skirmish;
	TestTrue(TEXT("parse tactical"), PmcCommanderRules::ParseScenario(TEXT("Tactical"), Parsed) && Parsed == EPmcScenario::Tactical);
	TestEqual(TEXT("its name"), FString(PmcCommanderRules::ScenarioName(EPmcScenario::Tactical)), FString(TEXT("tactical")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcTacticalPostWalkTest, "CodexTactics.AI.PmcTactical.PostWalkEndedShort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcTacticalPostWalkTest::RunTest(const FString&)
{
	// Only a walk that got him no closer counts toward the three tries that drop the post (API review 2026-11-17).
	TestFalse(TEXT("no walk issued: nothing to count"), PmcTacticalRules::PostWalkEndedShort(-1.f, 3000.f));
	TestFalse(TEXT("a walk that made progress (30 m -> 12 m) is no failed try"), PmcTacticalRules::PostWalkEndedShort(3000.f, 1200.f));
	TestFalse(TEXT("exactly the minimum progress is progress"), PmcTacticalRules::PostWalkEndedShort(3000.f, 3000.f - PmcTacticalRules::PostWalkMinProgressCm));
	TestTrue(TEXT("a walk that ended where it began ended short"), PmcTacticalRules::PostWalkEndedShort(3000.f, 3000.f));
	TestTrue(TEXT("a few centimetres closer is still short"), PmcTacticalRules::PostWalkEndedShort(3000.f, 2950.f));
	TestTrue(TEXT("pushed farther away is short"), PmcTacticalRules::PostWalkEndedShort(3000.f, 3400.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
