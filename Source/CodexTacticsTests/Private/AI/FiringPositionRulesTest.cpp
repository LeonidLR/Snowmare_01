#include "Misc/AutomationTest.h"
#include "AI/FiringPositionRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Smart enemies T-05: the pure rules of the firing positions (GDD smart-enemies.md section 5 + F2). The line traces are FiringPositionSmoke's.

namespace FiringPositionRulesTest
{
	FFiringCandidate MakeFiringCandidate(const TCHAR* Source, const FVector& Stand, float Score, const TCHAR* Key = TEXT(""), bool bValid = true)
	{
		FFiringCandidate Out;
		Out.Position.SourceId = Source;
		Out.Position.StandPoint = Stand;
		Out.Position.CoverKey = Key;
		Out.Position.bValid = bValid;
		Out.Position.CoveredShare = 0.5f;
		Out.Score = Score;
		return Out;
	}

	TSharedPtr<FJsonObject> ParseBlock(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionScoreF2Test, "CodexTactics.AI.FiringPosition.ScoreF2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionScoreF2Test::RunTest(const FString&)
{
	FFiringScoreInputs Inputs;
	Inputs.Fire = 0.8f;
	Inputs.Protect = 0.5f;
	Inputs.Escape = 0.6f;
	Inputs.Danger = 0.5f;
	Inputs.Spacing = 0.25f;
	Inputs.RoleFit = 0.9f;
	// F2: fire * protect * escape * (1 - w_danger * danger) * spacing * role_fit.
	const float Expected = 0.8f * 0.5f * 0.6f * (1.f - 0.6f * 0.5f) * 0.25f * 0.9f;
	TestEqual(TEXT("F2 is the plain product"), FiringPositionRules::ScoreF2(Inputs, 0.6f), Expected, 0.0001f);

	FFiringScoreInputs Perfect;
	Perfect.Fire = 1.f;
	TestEqual(TEXT("a perfect position scores 1"), FiringPositionRules::ScoreF2(Perfect, 0.6f), 1.f, 0.0001f);

	Perfect.Danger = 1.f;
	TestEqual(TEXT("full danger costs w_danger"), FiringPositionRules::ScoreF2(Perfect, 0.6f), 0.4f, 0.0001f);
	TestEqual(TEXT("w_danger 0 ignores the danger"), FiringPositionRules::ScoreF2(Perfect, 0.f), 1.f, 0.0001f);

	FFiringScoreInputs Blind = Perfect;
	Blind.Fire = 0.f;
	TestEqual(TEXT("cannot fire = not a position, whatever the rest"), FiringPositionRules::ScoreF2(Blind, 0.6f), 0.f, 0.0001f);

	FFiringScoreInputs Wild;
	Wild.Fire = 3.f;
	Wild.Protect = 2.f;
	Wild.Danger = -1.f;
	TestEqual(TEXT("terms are clamped to 0..1"), FiringPositionRules::ScoreF2(Wild, 0.6f), 1.f, 0.0001f);

	TestEqual(TEXT("spacing 0 (a mate's cover object) zeroes it"), FiringPositionRules::ScoreF2([]() { FFiringScoreInputs I; I.Fire = 1.f; I.Spacing = 0.f; return I; }(), 0.6f), 0.f, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionCoveredShareTest, "CodexTactics.AI.FiringPosition.CoveredShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionCoveredShareTest::RunTest(const FString&)
{
	TestEqual(TEXT("4 of 9"), FiringPositionRules::CoveredShare(4, 9), 4.f / 9.f, 0.0001f);
	TestEqual(TEXT("none"), FiringPositionRules::CoveredShare(0, 9), 0.f, 0.0001f);
	TestEqual(TEXT("all"), FiringPositionRules::CoveredShare(9, 9), 1.f, 0.0001f);
	TestEqual(TEXT("no samples is 0, not a division"), FiringPositionRules::CoveredShare(3, 0), 0.f, 0.0001f);
	TestEqual(TEXT("more hits than samples clamps to 1"), FiringPositionRules::CoveredShare(12, 9), 1.f, 0.0001f);
	FFiringPositionConfig Config;
	TestTrue(TEXT("3 of 9 is under min_covered_share 0.34"), FiringPositionRules::CoveredShare(3, 9) < Config.MinCoveredShare);
	TestTrue(TEXT("4 of 9 reaches it"), FiringPositionRules::CoveredShare(4, 9) >= Config.MinCoveredShare);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionCrossfireTest, "CodexTactics.AI.FiringPosition.Crossfire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionCrossfireTest::RunTest(const FString&)
{
	const FVector Zone(0.0, 0.0, 0.0);
	const FVector North(0.0, 3000.0, 0.0);
	const FVector East(3000.0, 0.0, 0.0);
	const FVector South(0.0, -3000.0, 0.0);
	const FVector NorthNear(300.0, 3000.0, 0.0);
	TestEqual(TEXT("north and east are 90 deg apart"), FiringPositionRules::CrossfireAngleDeg(North, East, Zone), 90.f, 0.01f);
	TestEqual(TEXT("north and south are 180 deg apart"), FiringPositionRules::CrossfireAngleDeg(North, South, Zone), 180.f, 0.01f);
	TestEqual(TEXT("a man on the zone has no bearing"), FiringPositionRules::CrossfireAngleDeg(Zone, East, Zone), 0.f, 0.01f);
	TestTrue(TEXT("90 deg is a crossfire"), FiringPositionRules::CrossfireOk(North, East, Zone, 45.f, 100.f));
	TestFalse(TEXT("two men side by side on one bearing are not (~5.7 deg)"), FiringPositionRules::CrossfireOk(North, NorthNear, Zone, 45.f, 100.f));
	TestFalse(TEXT("a higher minimum refuses 90 deg"), FiringPositionRules::CrossfireOk(North, East, Zone, 100.f, 100.f));
	// 180 deg apart: the zone lies between them, but is any man in the other's line of fire? The line shooter -> zone ends at the zone,
	// the mate stands beyond it: not inside the segment.
	TestTrue(TEXT("opposite sides of the zone are a crossfire (nobody in the other's line)"), FiringPositionRules::CrossfireOk(North, South, Zone, 45.f, 100.f));
	// A mate standing on the shooter's line to the zone is in his line of fire.
	const FVector Mate(0.0, 1500.0, 0.0);
	TestTrue(TEXT("a mate halfway down the line is in the line of fire"), FiringPositionRules::InLineOfFire(North, Mate, Zone, 100.f));
	TestFalse(TEXT("a mate 3 m off the line is not (half width 1 m)"), FiringPositionRules::InLineOfFire(North, FVector(300.0, 1500.0, 0.0), Zone, 100.f));
	TestFalse(TEXT("a man behind the shooter is not in front of him"), FiringPositionRules::InLineOfFire(North, FVector(0.0, 3500.0, 0.0), Zone, 100.f));
	TestFalse(TEXT("a man beyond the zone is not in the line"), FiringPositionRules::InLineOfFire(North, FVector(0.0, -500.0, 0.0), Zone, 100.f));
	// 30 deg apart with one in front of the other's line: both tests must hold.
	const FVector Shooter(0.0, 3000.0, 0.0);
	const FVector Front(0.0, 1500.0, 0.0);
	TestFalse(TEXT("a pair with one hammer in the other's line is no crossfire whatever the angle"), FiringPositionRules::CrossfireOk(Shooter, Front, Zone, 0.f, 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionPickTest, "CodexTactics.AI.FiringPosition.Pick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionPickTest::RunTest(const FString&)
{
	using namespace FiringPositionRulesTest;
	const FFiringPositionConfig Config; // min spacing 500 cm, penalty 0.25
	TArray<FFiringCandidate> Candidates;
	Candidates.Add(MakeFiringCandidate(TEXT("a"), FVector(0.0, 0.0, 0.0), 0.9f, TEXT("wallA")));
	Candidates.Add(Candidates[0]);
	Candidates[1].Position.SourceId = TEXT("a2");
	Candidates[1].Position.StandPoint = FVector(5000.0, 0.0, 0.0);
	Candidates[1].Score = 0.85f; // the same wall object as "a": scores 0 once "a" is taken
	Candidates.Add(MakeFiringCandidate(TEXT("b"), FVector(200.0, 0.0, 0.0), 0.8f, TEXT("wallB"))); // 2 m from "a": x0.25 = 0.2
	Candidates.Add(MakeFiringCandidate(TEXT("c"), FVector(0.0, 4000.0, 0.0), 0.5f, TEXT("wallC")));
	Candidates.Add(MakeFiringCandidate(TEXT("dead"), FVector(0.0, -4000.0, 0.0), 1.0f, TEXT("wallD"), false)); // invalid: never picked

	const TArray<int32> Picked = FiringPositionRules::PickN(Candidates, 3, TConstArrayView<FString>(), Config);
	TestEqual(TEXT("three picked"), Picked.Num(), 3);
	if (Picked.Num() == 3)
	{
		TestEqual(TEXT("the best first"), Candidates[Picked[0]].Position.SourceId, FString(TEXT("a")));
		TestEqual(TEXT("then c (0.5), not b (0.8 x 0.25 = 0.2) nor a2 (same object)"), Candidates[Picked[1]].Position.SourceId, FString(TEXT("c")));
		TestEqual(TEXT("then b, the near one, at its penalised score"), Candidates[Picked[2]].Position.SourceId, FString(TEXT("b")));
	}
	TestFalse(TEXT("an invalid candidate is never picked"), FiringPositionRules::PickN(Candidates, 10, TConstArrayView<FString>(), Config).ContainsByPredicate(
		[&Candidates](int32 Index) { return Candidates[Index].Position.SourceId == TEXT("dead"); }));
	TestEqual(TEXT("a2 shares a's wall: 4 valid minus a2 (score 0) leaves 3"), FiringPositionRules::PickN(Candidates, 10, TConstArrayView<FString>(), Config).Num(), 3);

	const TArray<FString> Exclude = { TEXT("wallA") };
	const TArray<int32> WithoutA = FiringPositionRules::PickN(Candidates, 1, Exclude, Config);
	TestEqual(TEXT("an excluded cover object (a mate's) is skipped: a and a2 both out, b is best"), WithoutA.Num() == 1 ? Candidates[WithoutA[0]].Position.SourceId : FString(), FString(TEXT("b")));
	TestEqual(TEXT("N 0 picks none"), FiringPositionRules::PickN(Candidates, 0, TConstArrayView<FString>(), Config).Num(), 0);

	// Ties go to the lower index (deterministic).
	TArray<FFiringCandidate> Ties;
	Ties.Add(MakeFiringCandidate(TEXT("first"), FVector(0.0, 0.0, 0.0), 0.5f));
	Ties.Add(MakeFiringCandidate(TEXT("second"), FVector(9000.0, 0.0, 0.0), 0.5f));
	const TArray<int32> TiePick = FiringPositionRules::PickN(Ties, 1, TConstArrayView<FString>(), Config);
	TestTrue(TEXT("a tie goes to the lower index"), TiePick.Num() == 1 && TiePick[0] == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionPairTest, "CodexTactics.AI.FiringPosition.CrossfirePair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionPairTest::RunTest(const FString&)
{
	using namespace FiringPositionRulesTest;
	const FFiringPositionConfig Config;
	const FVector Zone(0.0, 0.0, 0.0);
	TArray<FFiringCandidate> Candidates;
	Candidates.Add(MakeFiringCandidate(TEXT("north"), FVector(0.0, 4000.0, 0.0), 0.9f, TEXT("n")));
	Candidates.Add(MakeFiringCandidate(TEXT("north2"), FVector(900.0, 4000.0, 0.0), 0.85f, TEXT("n2"))); // ~12.7 deg from north: no crossfire
	Candidates.Add(MakeFiringCandidate(TEXT("east"), FVector(4000.0, 0.0, 0.0), 0.6f, TEXT("e")));
	Candidates.Add(MakeFiringCandidate(TEXT("west"), FVector(-4000.0, 0.0, 0.0), 0.7f, TEXT("w")));
	Candidates.Add(MakeFiringCandidate(TEXT("dead"), FVector(0.0, -4000.0, 0.0), 1.f, TEXT("d"), false));
	int32 First = INDEX_NONE;
	int32 Second = INDEX_NONE;
	TestTrue(TEXT("a pair is found"), FiringPositionRules::PickCrossfirePair(Candidates, Zone, TConstArrayView<FString>(), Config, First, Second));
	TestEqual(TEXT("the best hammer first"), First != INDEX_NONE ? Candidates[First].Position.SourceId : FString(), FString(TEXT("north")));
	TestEqual(TEXT("its best partner that crosses (west 0.7, not the adjacent north2 0.85)"), Second != INDEX_NONE ? Candidates[Second].Position.SourceId : FString(), FString(TEXT("west")));
	if (First != INDEX_NONE && Second != INDEX_NONE)
	{
		TestTrue(TEXT("the pair satisfies the crossfire test"), FiringPositionRules::CrossfireOk(Candidates[First].Position.StandPoint, Candidates[Second].Position.StandPoint, Zone,
			Config.CrossfireMinDeg, Config.LineClearCm));
	}

	// Nothing crosses: every valid candidate on one bearing.
	TArray<FFiringCandidate> OneSide;
	OneSide.Add(MakeFiringCandidate(TEXT("p"), FVector(0.0, 4000.0, 0.0), 0.9f, TEXT("p")));
	OneSide.Add(MakeFiringCandidate(TEXT("q"), FVector(600.0, 4200.0, 0.0), 0.8f, TEXT("q")));
	int32 A = 7;
	int32 B = 8;
	TestFalse(TEXT("no pair on one side"), FiringPositionRules::PickCrossfirePair(OneSide, Zone, TConstArrayView<FString>(), Config, A, B));
	TestTrue(TEXT("outputs untouched on failure"), A == 7 && B == 8);

	// The best candidate's partner is the best one that crosses, not the best one: x (0.5) is 10 deg off, y (0.4) is 90 deg.
	TArray<FFiringCandidate> Lonely;
	Lonely.Add(MakeFiringCandidate(TEXT("lonely"), FVector(0.0, 4000.0, 0.0), 0.95f, TEXT("l"), true));
	Lonely.Add(MakeFiringCandidate(TEXT("x"), FVector(700.0, 4000.0, 0.0), 0.5f, TEXT("x")));
	Lonely.Add(MakeFiringCandidate(TEXT("y"), FVector(4000.0, 0.0, 0.0), 0.4f, TEXT("y")));
	TestTrue(TEXT("a pair exists"), FiringPositionRules::PickCrossfirePair(Lonely, Zone, TConstArrayView<FString>(), Config, A, B));
	TestEqual(TEXT("first = lonely"), Lonely[A].Position.SourceId, FString(TEXT("lonely")));
	TestEqual(TEXT("partner = y (x is 10 deg off)"), Lonely[B].Position.SourceId, FString(TEXT("y")));

	// An excluded cover object removes its candidate from both seats.
	const TArray<FString> Exclude = { TEXT("n") };
	TestTrue(TEXT("a pair without the excluded north"), FiringPositionRules::PickCrossfirePair(Candidates, Zone, Exclude, Config, First, Second));
	TestFalse(TEXT("north is not in it"), Candidates[First].Position.SourceId == TEXT("north") || Candidates[Second].Position.SourceId == TEXT("north"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionAreaTest, "CodexTactics.AI.FiringPosition.AreaSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionAreaTest::RunTest(const FString&)
{
	const FVector Centre(1000.0, 2000.0, 300.0);
	const TArray<FVector> Grid = FiringPositionRules::AreaSamplePoints(Centre, 900.f, 9);
	TestEqual(TEXT("a 3x3 grid"), Grid.Num(), 9);
	if (Grid.Num() == 9)
	{
		TestTrue(TEXT("the middle one is the centre"), Grid[4].Equals(Centre, 0.01));
		TestEqual(TEXT("the grid steps 2/3 of the radius"), static_cast<float>(Grid[5].X - Grid[4].X), 600.f, 0.01f);
		for (const FVector& Point : Grid)
		{
			TestEqual(TEXT("on the floor of the area"), static_cast<float>(Point.Z), 300.f, 0.01f);
			TestTrue(TEXT("inside the circle"), FVector::Dist2D(Point, Centre) <= 900.0 + 0.01);
		}
	}
	TestEqual(TEXT("AreaSamples 1 is the centre only"), FiringPositionRules::AreaSamplePoints(Centre, 900.f, 1).Num(), 1);
	TestEqual(TEXT("a zero radius is the centre only"), FiringPositionRules::AreaSamplePoints(Centre, 0.f, 9).Num(), 1);

	TestTrue(TEXT("inside the band"), FiringPositionRules::SampleInBand(3000.f, 2500.f, 6000.f, 0.f));
	TestFalse(TEXT("too close"), FiringPositionRules::SampleInBand(2000.f, 2500.f, 6000.f, 0.f));
	TestFalse(TEXT("too far"), FiringPositionRules::SampleInBand(7000.f, 2500.f, 6000.f, 0.f));
	TestTrue(TEXT("no upper band limit"), FiringPositionRules::SampleInBand(90000.f, 0.f, 0.f, 0.f));
	TestFalse(TEXT("beyond the weapon's reach"), FiringPositionRules::SampleInBand(3000.f, 0.f, 0.f, 2500.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionRoofTermsTest, "CodexTactics.AI.FiringPosition.RoofTerms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionRoofTermsTest::RunTest(const FString&)
{
	const FFiringPositionConfig Config;
	TestEqual(TEXT("a ladder-only roof escapes at 0.3"), FiringPositionRules::RoofEscape(1, true, Config), 0.3f, 0.0001f);
	TestEqual(TEXT("a roof with one way out 0.6"), FiringPositionRules::RoofEscape(1, false, Config), 0.6f, 0.0001f);
	TestEqual(TEXT("a roof with two ways out 1"), FiringPositionRules::RoofEscape(3, false, Config), 1.f, 0.0001f);
	TestEqual(TEXT("a one-storey roof: 0.7 + 0.2"), FiringPositionRules::RoofProtectOf(1, Config), 0.9f, 0.0001f);
	TestEqual(TEXT("a three-storey roof caps at 1"), FiringPositionRules::RoofProtectOf(3, Config), 1.f, 0.0001f);

	const TArray<float> Steps = FiringPositionRules::RimSteps(Config);
	TestEqual(TEXT("rim steps 0, 10 .. 80 = 9 values"), Steps.Num(), 9);
	if (Steps.Num() == 9)
	{
		TestEqual(TEXT("starts at 0"), Steps[0], 0.f, 0.001f);
		TestEqual(TEXT("ends at rim_step_max_cm"), Steps[8], 80.f, 0.001f);
	}
	TestEqual(TEXT("rim_search_max_cm default"), Config.RimSearchMaxCm, 1500.f, 0.001f);
	TestTrue(TEXT("default queries stand and crouch"), FiringStances::Allows(FiringStances::Default, EOperativeStance::Standing) && FiringStances::Allows(FiringStances::Default, EOperativeStance::Crouching));
	TestFalse(TEXT("prone is not a position stance"), FiringStances::Allows(FiringStances::Default, EOperativeStance::Prone));
	TestFalse(TEXT("a sniper's query (crouching only) does not allow standing"), FiringStances::Allows(FiringStances::Crouching, EOperativeStance::Standing));
	TestTrue(TEXT("...and allows crouching"), FiringStances::Allows(FiringStances::Crouching, EOperativeStance::Crouching));
	FFiringPositionConfig NoStep = Config;
	NoStep.RimStepMaxCm = 0.f;
	TestEqual(TEXT("no rim step: just the spot"), FiringPositionRules::RimSteps(NoStep).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiringPositionConfigTest, "CodexTactics.AI.FiringPosition.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFiringPositionConfigTest::RunTest(const FString&)
{
	using namespace FiringPositionRulesTest;
	const FFiringPositionConfig Defaults;
	TestEqual(TEXT("muzzle standing"), Defaults.MuzzleHeight(EOperativeStance::Standing), 140.f, 0.001f);
	TestEqual(TEXT("muzzle crouching"), Defaults.MuzzleHeight(EOperativeStance::Crouching), 85.f, 0.001f);
	TestEqual(TEXT("muzzle prone"), Defaults.MuzzleHeight(EOperativeStance::Prone), 25.f, 0.001f);

	const TSharedPtr<FJsonObject> Block = ParseBlock(TEXT(R"JSON({
		"crossfire_min_deg": 60, "area_samples": 1, "min_covered_share": 2.0, "rim_step_max_cm": 500, "min_spacing_m": 7,
		"muzzle_height_cm": { "crouching": 90 }, "defence_use": false, "scan_rays": 99, "max_cover_slots": 0, "ignored_key": 5
	})JSON"));
	TestTrue(TEXT("the block parses"), Block.IsValid());
	if (Block.IsValid())
	{
		const FFiringPositionConfig Read = FiringPositionRules::ConfigFromJson(*Block);
		TestEqual(TEXT("crossfire_min_deg read"), Read.CrossfireMinDeg, 60.f, 0.001f);
		TestEqual(TEXT("area_samples read"), Read.AreaSamples, 1);
		TestEqual(TEXT("min_covered_share clamps to 1"), Read.MinCoveredShare, 1.f, 0.001f);
		TestEqual(TEXT("rim_step_max_cm clamps to 300"), Read.RimStepMaxCm, 300.f, 0.001f);
		TestEqual(TEXT("min_spacing_m is metres in the file, cm here"), Read.MinSpacingCm, 700.f, 0.001f);
		TestEqual(TEXT("a muzzle key read"), Read.MuzzleCrouchingCm, 90.f, 0.001f);
		TestEqual(TEXT("a missing muzzle key keeps the default"), Read.MuzzleStandingCm, 140.f, 0.001f);
		TestFalse(TEXT("defence_use false read"), Read.bDefenceUse);
		TestEqual(TEXT("scan_rays clamps to 16"), Read.ScanRays, 16);
		TestEqual(TEXT("max_cover_slots is at least 1"), Read.MaxCoverSlots, 1);
		TestEqual(TEXT("a missing key keeps the default"), Read.LineClearCm, Defaults.LineClearCm, 0.001f);
	}

	// The shipped file mirrors the code defaults (the numbers live in pmc_commander.json; the defaults are only the fallback).
	const FFiringPositionConfig Shipped = FiringPositionRules::LoadConfig();
	TestEqual(TEXT("shipped crossfire_min_deg"), Shipped.CrossfireMinDeg, Defaults.CrossfireMinDeg, 0.001f);
	TestEqual(TEXT("shipped line_clear_cm"), Shipped.LineClearCm, Defaults.LineClearCm, 0.001f);
	TestEqual(TEXT("shipped area_samples"), Shipped.AreaSamples, Defaults.AreaSamples);
	TestEqual(TEXT("shipped sample_height_cm"), Shipped.SampleHeightCm, Defaults.SampleHeightCm, 0.001f);
	TestEqual(TEXT("shipped min_covered_share"), Shipped.MinCoveredShare, Defaults.MinCoveredShare, 0.001f);
	TestEqual(TEXT("shipped rim_step_cm"), Shipped.RimStepCm, Defaults.RimStepCm, 0.001f);
	TestEqual(TEXT("shipped rim_step_max_cm"), Shipped.RimStepMaxCm, Defaults.RimStepMaxCm, 0.001f);
	TestEqual(TEXT("shipped rim_search_max_cm"), Shipped.RimSearchMaxCm, Defaults.RimSearchMaxCm, 0.001f);
	TestEqual(TEXT("shipped muzzle standing"), Shipped.MuzzleStandingCm, Defaults.MuzzleStandingCm, 0.001f);
	TestEqual(TEXT("shipped muzzle crouching"), Shipped.MuzzleCrouchingCm, Defaults.MuzzleCrouchingCm, 0.001f);
	TestEqual(TEXT("shipped muzzle prone"), Shipped.MuzzleProneCm, Defaults.MuzzleProneCm, 0.001f);
	TestEqual(TEXT("shipped w_danger"), Shipped.DangerWeight, Defaults.DangerWeight, 0.001f);
	TestEqual(TEXT("shipped roof_protect"), Shipped.RoofProtect, Defaults.RoofProtect, 0.001f);
	TestEqual(TEXT("shipped high_ground"), Shipped.HighGroundPerLevel, Defaults.HighGroundPerLevel, 0.001f);
	TestEqual(TEXT("shipped window_protect"), Shipped.WindowProtect, Defaults.WindowProtect, 0.001f);
	TestEqual(TEXT("shipped escape_second_way"), Shipped.EscapeSecondWay, Defaults.EscapeSecondWay, 0.001f);
	TestEqual(TEXT("shipped escape_one_way"), Shipped.EscapeOneWay, Defaults.EscapeOneWay, 0.001f);
	TestEqual(TEXT("shipped escape_ladder_only"), Shipped.EscapeLadderOnly, Defaults.EscapeLadderOnly, 0.001f);
	TestEqual(TEXT("shipped min_spacing_m"), Shipped.MinSpacingCm, Defaults.MinSpacingCm, 0.001f);
	TestEqual(TEXT("shipped spacing_penalty"), Shipped.SpacingPenalty, Defaults.SpacingPenalty, 0.001f);
	TestEqual(TEXT("shipped scan_step_cm"), Shipped.ScanStepCm, Defaults.ScanStepCm, 0.001f);
	TestEqual(TEXT("shipped scan_rays"), Shipped.ScanRays, Defaults.ScanRays);
	TestEqual(TEXT("shipped scan_reach_cm"), Shipped.ScanReachCm, Defaults.ScanReachCm, 0.001f);
	TestEqual(TEXT("shipped max_scan_points"), Shipped.MaxScanPoints, Defaults.MaxScanPoints);
	TestEqual(TEXT("shipped max_cover_slots"), Shipped.MaxCoverSlots, Defaults.MaxCoverSlots);
	TestTrue(TEXT("shipped defence_use"), Shipped.bDefenceUse == Defaults.bDefenceUse);
	return true;
}

#endif
