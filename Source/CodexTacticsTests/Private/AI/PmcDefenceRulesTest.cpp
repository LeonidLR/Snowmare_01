#include "Misc/AutomationTest.h"
#include "AI/PmcDefenceRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// G008 / PMC_DEFENCE.md D1. Pure rules: layout access, roles, sectors, spacing, spread, guard share n/a.

namespace PmcDefenceRulesTest
{
	const TCHAR* LayoutJson = TEXT(R"JSON({
		"ground_z": 0,
		"buildings": [
			{
				"id": "B_hall",
				"footprint": [[0, 0], [2000, 2000]],
				"floors": 2,
				"roof_walkable": true,
				"stairs": [
					{"id": "B_hall_stair_low", "top": [100, 100], "from": 0, "to": 1},
					{"id": "B_hall_stair_roof", "top": [1800, 200], "from": 1, "to": 2},
					{"id": "B_hall_stair_far", "top": [100, 1800], "from": 1, "to": 2}
				],
				"hatches": [
					{"id": "B_hall_roof_hatch", "pos": [1500, 150], "floor": 2}
				]
			},
			{
				"id": "B_flat",
				"footprint": [[0, 0], [4000, 1000]],
				"rotation": 90,
				"floors": 1,
				"roof_walkable": true,
				"stairs": [],
				"hatches": [{"id": "B_flat_hatch", "pos": [2000, 2000], "floor": 1}]
			},
			{
				"id": "B_ladder_only",
				"footprint": [[3000, 3000], [4000, 4000]],
				"floors": 1,
				"roof_walkable": true,
				"stairs": [{"id": "B_ladder_only_mid", "top": [3100, 3100], "from": 0, "to": 0}]
			}
		],
		"ladders": [
			{"id": "LAD_B_ladder_only", "top": [3900, 3900], "to": "roof:B_ladder_only", "level_to": 1},
			{"id": "LAD_other", "top": [0, 0], "to": "roof:B_other", "level_to": 1}
		]
	})JSON");

	bool LoadLayout(FPmcLayoutAccess& Out, FAutomationTestBase& Test)
	{
		TArray<FString> Errors;
		const bool bParsed = PmcDefenceRules::ParseLayout(LayoutJson, Out, Errors);
		Test.TestTrue(TEXT("layout parses"), bParsed);
		Test.TestEqual(TEXT("no skipped entries"), Errors.Num(), 0);
		return bParsed && Errors.Num() == 0;
	}

	FPmcDefenceMan Man(int32 Index, float X, float Y, EPmcDefenceRole Previous = EPmcDefenceRole::None)
	{
		FPmcDefenceMan Out;
		Out.Index = Index;
		Out.Feet = FVector(X, Y, 0.f);
		Out.PreviousRole = Previous;
		return Out;
	}

	/** L_Gen_Depot_X4.tactical.json overwatch roof B_gallery (access_kind ladder) and its ladder top. */
	void Gallery(FPmcRoofRef& Roof, FPmcLadderTop& Top)
	{
		Roof.Building = TEXT("B_gallery");
		Roof.Z = 300.f;
		Roof.AccessKind = EPmcAccessKind::Ladder;
		Roof.AccessIds = { TEXT("ladder:LAD_B_gallery") };
		Roof.Spots = {
			{ FVector(4100.f, -8350.f, 300.f), 858.f },
			{ FVector(4100.f, -7350.f, 300.f), 854.f }
		};
		Top.Building = TEXT("B_gallery");
		Top.LadderId = TEXT("LAD_B_gallery");
		Top.Location = FVector(4100.f, -7810.f, 300.f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceLayoutParseTest, "CodexTactics.AI.PmcDefence.LayoutParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceLayoutParseTest::RunTest(const FString&)
{
	using namespace PmcDefenceRulesTest;
	FPmcLayoutAccess Layout;
	if (!LoadLayout(Layout, *this))
	{
		return false;
	}
	TestEqual(TEXT("buildings"), Layout.Buildings.Num(), 3);
	TestEqual(TEXT("roof stair top x"), Layout.Buildings[0].Stairs[1].Top.X, 1800.f);
	TestEqual(TEXT("hatch pos x"), Layout.Buildings[0].Hatches[0].Pos.X, 1500.f);
	TestEqual(TEXT("ladder top x"), Layout.Ladders[0].Top.X, 3900.f);

	const FVector OnRoof(1900.f, 100.f, 600.f);
	const int32 Hall = PmcDefenceRules::BuildingAt(Layout, OnRoof);
	TestEqual(TEXT("point in B_hall"), Hall, 0);
	TestEqual(TEXT("outside"), PmcDefenceRules::BuildingAt(Layout, FVector(5000.f, 5000.f, 0.f)), INDEX_NONE);

	// A 90 deg turn of the wide footprint [0,0]-[4000,1000] about its centre contains (2000, 2500)
	// (outside B_hall) and not (3500, 500), which sits on the unrotated arm.
	TestEqual(TEXT("rotated contains"), PmcDefenceRules::BuildingAt(Layout, FVector(2000.f, 2500.f, 300.f)), 1);
	TestFalse(TEXT("rotated excludes the unrotated arm"), PmcDefenceRules::BuildingAt(Layout, FVector(3500.f, 500.f, 300.f)) == 1);

	TestTrue(TEXT("slab is a roof"), PmcDefenceRules::IsWalkableRoofSite(Layout, Hall, 600.f, 150.f));
	TestFalse(TEXT("ground in the footprint is not a roof"), PmcDefenceRules::IsWalkableRoofSite(Layout, Hall, 0.f, 150.f));

	const FPmcDefenceAccess Stair = PmcDefenceRules::ResolveLayoutAccess(Layout, Hall, OnRoof);
	TestEqual(TEXT("kind"), FString(PmcDefenceRules::AccessKindName(Stair.Kind)), TEXT("stairs"));
	TestEqual(TEXT("source"), FString(PmcDefenceRules::AccessSourceName(Stair.Source)), TEXT("layout"));
	TestEqual(TEXT("nearest roof stair, not the low one or the hatch"), Stair.Id, TEXT("B_hall_stair_roof"));
	TestTrue(TEXT("stair xyz"), Stair.Location.Equals(FVector(1800.f, 200.f, 600.f), 0.1f));
	TestEqual(TEXT("log"), PmcDefenceRules::AccessLogLine(1, Stair),
		TEXT("[PmcDefence] team 1 access stairs at (1800, 200, 600) source=layout"));

	// Drop the roof stairs: the hatch wins over the ladder that this building does not have.
	Layout.Buildings[0].Stairs.Empty();
	const FPmcDefenceAccess Hatch = PmcDefenceRules::ResolveLayoutAccess(Layout, 0, OnRoof);
	TestEqual(TEXT("hatch id"), Hatch.Id, TEXT("B_hall_roof_hatch"));
	TestTrue(TEXT("hatch kind"), Hatch.Kind == EPmcAccessKind::Hatch);
	TestTrue(TEXT("hatch z is the slab"), Hatch.Location.Equals(FVector(1500.f, 150.f, 600.f), 0.1f));

	Layout.Buildings[0].Hatches.Empty();
	TestFalse(TEXT("hall has no ladder"), PmcDefenceRules::ResolveLayoutAccess(Layout, 0, OnRoof).IsSet());

	const int32 LadderBuilding = PmcDefenceRules::BuildingAt(Layout, FVector(3500.f, 3500.f, 300.f));
	const FPmcDefenceAccess Ladder = PmcDefenceRules::ResolveLayoutAccess(Layout, LadderBuilding, FVector(3500.f, 3500.f, 300.f));
	TestEqual(TEXT("layout ladder"), Ladder.Id, TEXT("LAD_B_ladder_only"));
	TestTrue(TEXT("ladder source"), Ladder.Source == EPmcAccessSource::Layout);
	TestTrue(TEXT("ladder z"), Ladder.Location.Equals(FVector(3900.f, 3900.f, 300.f), 0.1f));

	FPmcLayoutAccess Broken;
	TArray<FString> Errors;
	TestFalse(TEXT("not json"), PmcDefenceRules::ParseLayout(TEXT("nope"), Broken, Errors));
	TestEqual(TEXT("none log"), PmcDefenceRules::AccessLogLine(0, FPmcDefenceAccess()), TEXT("[PmcDefence] team 0 access none"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceGalleryLadderTest, "CodexTactics.AI.PmcDefence.GalleryLadder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceGalleryLadderTest::RunTest(const FString&)
{
	using namespace PmcDefenceRulesTest;
	FPmcRoofRef Roof;
	FPmcLadderTop Top;
	Gallery(Roof, Top);
	const FPmcDefenceAccess Access = PmcDefenceRules::ResolveTacticalAccess(Roof, MakeArrayView(&Top, 1), nullptr);
	TestTrue(TEXT("kind"), Access.Kind == EPmcAccessKind::Ladder);
	TestTrue(TEXT("source"), Access.Source == EPmcAccessSource::Tactical);
	TestEqual(TEXT("id"), Access.Id, TEXT("LAD_B_gallery"));
	TestTrue(TEXT("ladder top xyz"), Access.Location.Equals(FVector(4100.f, -7810.f, 300.f), 0.1f));
	TestEqual(TEXT("log"), PmcDefenceRules::AccessLogLine(0, Access),
		TEXT("[PmcDefence] team 0 access ladder at (4100, -7810, 300) source=tactical"));

	// Spot centroid (4100, -7850). Inset 1.5 m toward it from the top (4100, -7810) lands at (4100, -7960).
	const FVector Centroid = PmcDefenceRules::SpotCentroid(Roof.Spots);
	TestTrue(TEXT("centroid"), Centroid.Equals(FVector(4100.f, -7850.f, 300.f), 0.1f));
	const FVector Post = PmcDefenceRules::GuardPostLocation(Access, Centroid, 150.f);
	TestTrue(TEXT("inset post"), Post.Equals(FVector(4100.f, -7960.f, 300.f), 0.1f));
	TestTrue(TEXT("looks out through the top"), FMath::IsNearlyEqual(PmcDefenceRules::YawDegrees(Post, Access.Location), 90.f, 0.1f));

	// A stair roof takes the layout top and the tactical roof's Z, not the ladder top.
	FPmcLayoutAccess Layout;
	if (!LoadLayout(Layout, *this))
	{
		return false;
	}
	FPmcRoofRef StairRoof;
	StairRoof.Building = TEXT("B_hall");
	StairRoof.Z = 600.f;
	StairRoof.AccessKind = EPmcAccessKind::Stairs;
	StairRoof.AccessIds = { TEXT("stair:B_hall_stair_roof"), TEXT("hatch:B_hall_roof_hatch") };
	const FPmcDefenceAccess Stair = PmcDefenceRules::ResolveTacticalAccess(StairRoof, MakeArrayView(&Top, 1), &Layout);
	TestTrue(TEXT("tactical stair source"), Stair.Source == EPmcAccessSource::Layout);
	TestTrue(TEXT("tactical stair xyz"), Stair.Location.Equals(FVector(1800.f, 200.f, 600.f), 0.1f));
	TestFalse(TEXT("stairs without a layout"), PmcDefenceRules::ResolveTacticalAccess(StairRoof, {}, nullptr).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceRolesTest, "CodexTactics.AI.PmcDefence.Roles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceRolesTest::RunTest(const FString&)
{
	using namespace PmcDefenceRulesTest;
	FPmcDefenceConfig Config;
	FPmcDefenceAccess Access;
	Access.Kind = EPmcAccessKind::Stairs;
	Access.Source = EPmcAccessSource::Layout;
	Access.Location = FVector::ZeroVector;

	// Two defenders: the nearer man guards, the other owns the whole 180 deg arc.
	TArray<FPmcDefenceMan> Two = { Man(0, 1000.f, 0.f), Man(1, 200.f, 0.f) };
	const TArray<FPmcDefencePost> Pair = PmcDefenceRules::AssignRoles(Two, Access, FVector::ZeroVector, 0.f, {}, {}, nullptr, Config);
	TestTrue(TEXT("guard"), Pair[1].Role == EPmcDefenceRole::AccessGuard);
	TestEqual(TEXT("guard has no sector"), Pair[1].Sector, INDEX_NONE);
	TestTrue(TEXT("shooter"), Pair[0].Role == EPmcDefenceRole::SectorShooter);
	TestEqual(TEXT("one wedge"), Pair[0].Sector, 0);
	const FVector Ahead(500.f, 0.f, 0.f);
	const FVector Behind(-500.f, 0.f, 0.f);
	TestEqual(TEXT("approach is inside the arc"), PmcDefenceRules::SectorIndex(FVector::ZeroVector, Ahead, 0.f, 1, 180.f), 0);
	TestEqual(TEXT("the rear is outside the arc"), PmcDefenceRules::SectorIndex(FVector::ZeroVector, Behind, 0.f, 1, 180.f), INDEX_NONE);
	TestEqual(TEXT("arc edge in"), PmcDefenceRules::SectorIndex(FVector::ZeroVector, FVector(10.f, 500.f, 0.f), 0.f, 1, 180.f), 0);
	TestEqual(TEXT("past the arc"), PmcDefenceRules::SectorIndex(FVector::ZeroVector, FVector(-10.f, 500.f, 0.f), 0.f, 1, 180.f), INDEX_NONE);

	// A tie on distance keeps the lower squad index.
	TArray<FPmcDefenceMan> Tied = { Man(2, 300.f, 0.f), Man(0, -300.f, 0.f) };
	const TArray<FPmcDefencePost> Tie = PmcDefenceRules::AssignRoles(Tied, Access, FVector::ZeroVector, 0.f, {}, {}, nullptr, Config);
	TestTrue(TEXT("tie guard"), Tie[1].Role == EPmcDefenceRole::AccessGuard);

	// The living guard stays the guard even when a teammate is closer. Sectors follow the new shooter count.
	TArray<FPmcDefenceMan> Kept = { Man(0, 800.f, 0.f, EPmcDefenceRole::AccessGuard), Man(1, 50.f, 0.f, EPmcDefenceRole::SectorShooter) };
	const TArray<FPmcDefencePost> Keep = PmcDefenceRules::AssignRoles(Kept, Access, FVector::ZeroVector, 0.f, {}, {}, nullptr, Config);
	TestTrue(TEXT("kept guard"), Keep[0].Role == EPmcDefenceRole::AccessGuard);
	TestTrue(TEXT("kept shooter"), Keep[1].Role == EPmcDefenceRole::SectorShooter);

	// One defender left: he guards the access and has no sector.
	TArray<FPmcDefenceMan> One = { Man(3, 10.f, 0.f, EPmcDefenceRole::SectorShooter) };
	const TArray<FPmcDefencePost> Solo = PmcDefenceRules::AssignRoles(One, Access, FVector::ZeroVector, 0.f, {}, {}, nullptr, Config);
	TestTrue(TEXT("solo role"), Solo[0].Role == EPmcDefenceRole::AccessGuard);
	TestEqual(TEXT("solo sector"), Solo[0].Sector, INDEX_NONE);
	TestTrue(TEXT("solo post"), Solo[0].bHasPost);

	// No access: every defender is a sector shooter. The guard share is n/a, not 0.
	FPmcDefenceAccess None;
	TArray<FPmcDefenceMan> Three = { Man(0, 0.f, 0.f), Man(1, 100.f, 0.f), Man(2, 0.f, 100.f) };
	const TArray<FPmcDefencePost> Open = PmcDefenceRules::AssignRoles(Three, None, FVector::ZeroVector, 0.f, {}, {}, nullptr, Config);
	TestTrue(TEXT("open 0"), Open[0].Role == EPmcDefenceRole::SectorShooter);
	TestTrue(TEXT("open 1"), Open[1].Role == EPmcDefenceRole::SectorShooter);
	TestTrue(TEXT("open 2"), Open[2].Role == EPmcDefenceRole::SectorShooter);
	TestEqual(TEXT("wedges"), Open[0].Sector + Open[1].Sector + Open[2].Sector, 0 + 1 + 2);
	const FPmcGuardSample Missing = PmcDefenceRules::SampleGuard({ FVector::ZeroVector }, None, Config.GuardDistCm, Config.LevelToleranceCm);
	TestFalse(TEXT("no access is not applicable"), Missing.bApplicable);
	float Share = 123.f;
	TestFalse(TEXT("n/a does not become 0"), PmcDefenceRules::TryGuardShare(0, 0, Share));
	TestEqual(TEXT("untouched"), Share, 123.f);
	TestTrue(TEXT("nobody near is a real zero"), PmcDefenceRules::TryGuardShare(0, 4, Share));
	TestEqual(TEXT("share 0"), Share, 0.f);

	const FPmcGuardSample Near = PmcDefenceRules::SampleGuard({ FVector(100.f, 0.f, 0.f) }, Access, Config.GuardDistCm, Config.LevelToleranceCm);
	TestTrue(TEXT("near"), Near.bApplicable && Near.bNear);
	const FPmcGuardSample OtherLevel = PmcDefenceRules::SampleGuard({ FVector(100.f, 0.f, 1000.f) }, Access, Config.GuardDistCm, Config.LevelToleranceCm);
	TestTrue(TEXT("other level is applicable but not near"), OtherLevel.bApplicable && !OtherLevel.bNear);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceSectorsTest, "CodexTactics.AI.PmcDefence.Sectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceSectorsTest::RunTest(const FString&)
{
	using namespace PmcDefenceRulesTest;
	FPmcDefenceConfig Config;
	FPmcDefenceAccess Access;
	Access.Kind = EPmcAccessKind::Hatch;
	Access.Source = EPmcAccessSource::Layout;
	Access.Location = FVector(0.f, -400.f, 900.f);
	// Two shooters (the guard is a third man). Approach yaw 0: wedge 0 is -90..0, wedge 1 is 0..90.
	TArray<FPmcDefenceSpot> Spots = {
		{ FVector(400.f, -400.f, 900.f), 10.f },
		{ FVector(400.f, 400.f, 900.f), 30.f },
		{ FVector(200.f, -200.f, 900.f), 4.f }
	};
	TArray<FPmcDefenceMan> Men = { Man(0, 0.f, 0.f), Man(1, 0.f, 0.f), Man(2, 0.f, -400.f) };
	const TArray<FPmcDefencePost> Posts = PmcDefenceRules::AssignRoles(Men, Access, FVector(0.f, 0.f, 900.f), 0.f, Spots, {}, nullptr, Config);
	TestTrue(TEXT("nearest the hatch guards"), Posts[2].Role == EPmcDefenceRole::AccessGuard);
	TestEqual(TEXT("shooter 0 wedge"), Posts[0].Sector, 0);
	TestEqual(TEXT("shooter 0 best covered in the wedge"), Posts[0].SpotIndex, 0);
	TestEqual(TEXT("shooter 1 wedge"), Posts[1].Sector, 1);
	TestEqual(TEXT("shooter 1 spot"), Posts[1].SpotIndex, 1);
	TestTrue(TEXT("shooters face the approach"), FMath::IsNearlyEqual(Posts[0].Yaw, 0.f, 0.01f));

	// The hatch post is inset toward the spot centroid, looking back at the hatch.
	TestTrue(TEXT("guard inset"), Posts[2].Location.Z == 900.f && FVector::Dist2D(Posts[2].Location, Access.Location) > 100.f
		&& FVector::Dist2D(Posts[2].Location, Access.Location) < Config.GuardDistCm);

	TestEqual(TEXT("next spot in the wedge"), PmcDefenceRules::NextEdgeSpot(Spots, FVector(0.f, 0.f, 900.f), 0.f, 0, 2, 180.f, 2), 0);
	TestEqual(TEXT("wraps to the first"), PmcDefenceRules::NextEdgeSpot(Spots, FVector(0.f, 0.f, 900.f), 0.f, 0, 2, 180.f, 0), 2);
	TestEqual(TEXT("a lone spot stays"), PmcDefenceRules::NextEdgeSpot(Spots, FVector(0.f, 0.f, 900.f), 0.f, 1, 2, 180.f, 1), 1);
	TestTrue(TEXT("shift timer"), PmcDefenceRules::ShouldSectorShift(8.f, false, 8.f));
	TestFalse(TEXT("before the timer"), PmcDefenceRules::ShouldSectorShift(7.9f, false, 8.f));
	TestTrue(TEXT("exposed in contact"), PmcDefenceRules::ShouldSectorShift(0.f, true, 8.f));

	// Roof area: centroid of the three spots, slack 200. Under the roof is outside.
	const FPmcDefenceConfig Area = Config;
	TestTrue(TEXT("on the roof"), PmcDefenceRules::IsInsideDefendedArea(FVector(300.f, 0.f, 900.f), FVector(0.f, 0.f, 900.f), 900.f, 600.f, Spots, Area));
	TestFalse(TEXT("under the roof"), PmcDefenceRules::IsInsideDefendedArea(FVector(300.f, 0.f, 0.f), FVector(0.f, 0.f, 900.f), 900.f, 600.f, Spots, Area));
	TestFalse(TEXT("past the spots"), PmcDefenceRules::IsInsideDefendedArea(FVector(5000.f, 0.f, 900.f), FVector(0.f, 0.f, 900.f), 900.f, 600.f, Spots, Area));

	// Ground: radius 600 * area 2 = 1200. 900 inside, 1500 outside, off the level outside.
	TestTrue(TEXT("ground 900"), PmcDefenceRules::IsInsideDefendedArea(FVector(900.f, 0.f, 0.f), FVector::ZeroVector, 0.f, 600.f, {}, Area));
	TestFalse(TEXT("ground 1500"), PmcDefenceRules::IsInsideDefendedArea(FVector(1500.f, 0.f, 0.f), FVector::ZeroVector, 0.f, 600.f, {}, Area));
	TestFalse(TEXT("off level"), PmcDefenceRules::IsInsideDefendedArea(FVector(900.f, 0.f, 1000.f), FVector::ZeroVector, 0.f, 600.f, {}, Area));
	TestTrue(TEXT("fallback on level"), PmcDefenceRules::IsInsideAreaFallback(true, 900.f, 600.f, 2.f));
	TestFalse(TEXT("fallback off level"), PmcDefenceRules::IsInsideAreaFallback(false, 1000.f, 600.f, 2.f));
	TestFalse(TEXT("fallback 1500"), PmcDefenceRules::IsInsideAreaFallback(true, 1500.f, 600.f, 2.f));
	TestEqual(TEXT("inside scores 0"), PmcDefenceRules::DefenderObjectiveScore(true, 1.f, 1.f), 0.f);
	TestEqual(TEXT("outside scores 0.7"), PmcDefenceRules::DefenderObjectiveScore(false, 1.f, 1.f), 0.7f);
	TestEqual(TEXT("hurt floor"), PmcDefenceRules::DefenderObjectiveScore(false, 1.f, 0.2f), 0.35f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcDefenceSpacingTest, "CodexTactics.AI.PmcDefence.Spacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcDefenceSpacingTest::RunTest(const FString&)
{
	const FPmcDefenceConfig Config;
	const FString Wall = PmcDefenceRules::CoverObjectKey(TEXT("Wall_A"), FVector(0.f, 0.f, 0.f));
	const FString Same = PmcDefenceRules::CoverObjectKey(TEXT("Wall_A"), FVector(9000.f, 0.f, 0.f));
	TestEqual(TEXT("same actor"), Wall, Same);
	const TArray<FVector> Claimed = { FVector(0.f, 0.f, 0.f), FVector(100.f, 0.f, 0.f) };
	const TArray<FString> Keys = { Wall, PmcDefenceRules::CoverObjectKey(TEXT("Wall_B"), FVector(100.f, 0.f, 0.f)) };
	TestEqual(TEXT("same wall is 0"), PmcDefenceRules::SpacingFactor(FVector(8000.f, 0.f, 0.f), Wall, Claimed, Keys, Config), 0.f);
	TestEqual(TEXT("close once"), PmcDefenceRules::SpacingFactor(FVector(200.f, 0.f, 0.f), TEXT("Wall_C"), Claimed, Keys, Config), Config.SpacingPenalty);
	TestEqual(TEXT("far"), PmcDefenceRules::SpacingFactor(FVector(2000.f, 0.f, 0.f), TEXT("Wall_C"), Claimed, Keys, Config), 1.f);

	const FString EmptyA = PmcDefenceRules::CoverObjectKey(TEXT(""), FVector(10.f, 10.f, 0.f));
	const FString EmptyB = PmcDefenceRules::CoverObjectKey(TEXT("None"), FVector(40.f, 40.f, 0.f));
	const FString EmptyFar = PmcDefenceRules::CoverObjectKey(TEXT(""), FVector(1000.f, 0.f, 0.f));
	TestEqual(TEXT("empty actors share only the quantized point"), EmptyA, EmptyB);
	TestNotEqual(TEXT("a different object"), EmptyA, EmptyFar);
	TestEqual(TEXT("shared pairs"), PmcDefenceRules::SharedCoverPairs({ Wall, Wall, TEXT(""), EmptyA }), 1);
	TestEqual(TEXT("empty keys are not a pair"), PmcDefenceRules::SharedCoverPairs({ TEXT(""), TEXT("") }), 0);

	TestEqual(TEXT("one man"), PmcDefenceRules::MeanSpreadM({ FVector::ZeroVector }), 0.f);
	TestEqual(TEXT("10 m"), PmcDefenceRules::MeanSpreadM({ FVector::ZeroVector, FVector(1000.f, 0.f, 0.f) }), 10.f);
	TestEqual(TEXT("objective share"), PmcDefenceRules::ObjectiveOrderShare(2, 10), 0.2f);
	TestEqual(TEXT("no orders"), PmcDefenceRules::ObjectiveOrderShare(0, 0), 0.f);

	FPmcRoofRef Near;
	Near.Z = 300.f;
	Near.Spots = { { FVector(4100.f, -7810.f, 300.f), 1.f } };
	FPmcRoofRef Far;
	Far.Z = 900.f;
	Far.Spots = { { FVector(0.f, 0.f, 900.f), 9.f } };
	const TArray<FPmcRoofRef> Roofs = { Far, Near };
	TestEqual(TEXT("roof on the objective level"), PmcDefenceRules::PickRoof(Roofs, FVector(4000.f, -7800.f, 300.f), 150.f), 1);
	return true;
}

#endif
