// CodexTactics.Weapons.Mortar.* - the mortar nests' pure rules (2026-11-09; docs/design/SQUAD_SIX_FIRE_TEAMS.md 7.3; docs/systems/weapons.md
// "Mortar nests"): the fire checks in order (crew of three, ammo, the 30 / 250 m band, the spotter), the bracketing walk, the crew places, the PMC's
// target cluster and its utility score, the data ("mortar_nest": the round is a high-angle projectile that reaches the band in a few seconds) and
// the level JSON placements (the combat lab's two test nests).

#include "AI/PmcCommanderRules.h"
#include "Combat/MortarRules.h"
#include "Combat/ProjectileRules.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MortarRulesTest
{
	TSharedPtr<FJsonObject> MortarJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	TSharedPtr<FJsonObject> MortarFile(const FString& Relative)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / Relative)) ? MortarJson(Text) : nullptr;
	}

	FMortarNestConfig ProjectMortar(FAutomationTestBase& Test)
	{
		const TSharedPtr<FJsonObject> Root = MortarFile(TEXT("Data/Weapons/weapons_tuning.json"));
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Test.TestTrue(TEXT("weapons_tuning.json has a mortar_nest block"), Root.IsValid() && Root->TryGetObjectField(TEXT("mortar_nest"), Block) && Block))
		{
			return MortarRules::Defaults();
		}
		TArray<FString> Errors;
		const FMortarNestConfig Config = MortarRules::Parse(**Block, &Errors);
		Test.TestEqual(FString::Printf(TEXT("the shipped block reads cleanly (%s)"), *FString::Join(Errors, TEXT("; "))), Errors.Num(), 0);
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMortarCheckTest, "CodexTactics.Weapons.Mortar.Checks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMortarCheckTest::RunTest(const FString&)
{
	const FMortarNestConfig Config = MortarRulesTest::ProjectMortar(*this);
	TestEqual(TEXT("a crew of three (the designer)"), Config.CrewNeeded, 3);
	TestEqual(TEXT("min 30 m"), Config.MinRangeCm, 3000.f);
	TestEqual(TEXT("max 250 m (a guess)"), Config.MaxRangeCm, 25000.f);
	using EMR = EMortarRefusal;
	TestTrue(TEXT("a crew of three, ammo, 80 m, spotted: fires"), MortarRules::Check(3, 10, 8000.f, true, Config) == EMR::None);
	TestTrue(TEXT("a crew of two: no fire"), MortarRules::Check(2, 10, 8000.f, true, Config) == EMR::Crew);
	TestTrue(TEXT("nobody at the nest: no fire"), MortarRules::Check(0, 10, 8000.f, true, Config) == EMR::Crew);
	TestTrue(TEXT("a crew of four: fires"), MortarRules::Check(4, 10, 8000.f, true, Config) == EMR::None);
	TestTrue(TEXT("no rounds: no fire"), MortarRules::Check(3, 0, 8000.f, true, Config) == EMR::NoAmmo);
	TestTrue(TEXT("20 m: too close"), MortarRules::Check(3, 10, 2000.f, true, Config) == EMR::TooClose);
	TestTrue(TEXT("30 m: fires"), MortarRules::Check(3, 10, 3000.f, true, Config) == EMR::None);
	TestTrue(TEXT("250 m: fires"), MortarRules::Check(3, 10, 25000.f, true, Config) == EMR::None);
	TestTrue(TEXT("260 m: too far"), MortarRules::Check(3, 10, 26000.f, true, Config) == EMR::TooFar);
	TestTrue(TEXT("unspotted: no fire (the spotter rule)"), MortarRules::Check(3, 10, 8000.f, false, Config) == EMR::NotSpotted);
	TestTrue(TEXT("the crew is checked first"), MortarRules::Check(1, 0, 100.f, false, Config) == EMR::Crew);
	TestFalse(TEXT("a refusal has a text"), MortarRules::RefusalText(EMR::NotSpotted).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMortarBracketingTest, "CodexTactics.Weapons.Mortar.Bracketing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMortarBracketingTest::RunTest(const FString&)
{
	const FMortarNestConfig Config = MortarRulesTest::ProjectMortar(*this);
	const FMortarBracketing& B = Config.Bracketing;
	TestEqual(TEXT("the first round 15 m off"), MortarRules::BracketErrorCm(0, B), 1500.f);
	float Previous = TNumericLimits<float>::Max();
	for (int32 Round = 0; Round < 10; ++Round)
	{
		const float Error = MortarRules::BracketErrorCm(Round, B);
		TestTrue(FString::Printf(TEXT("round %d (%.0f cm) not farther than the one before"), Round + 1, Error), Error <= Previous);
		TestTrue(FString::Printf(TEXT("round %d never under the floor"), Round + 1), Error >= B.MinErrorCm - 0.01f);
		Previous = Error;
	}
	TestTrue(TEXT("round 2 closer than round 1"), MortarRules::BracketErrorCm(1, B) < MortarRules::BracketErrorCm(0, B));
	TestEqual(TEXT("it settles at the floor"), MortarRules::BracketErrorCm(20, B), B.MinErrorCm, 0.01f);
	const FVector Target(5000.f, 2000.f, 30.f);
	for (const float Roll : { 0.f, 0.25f, 0.6f, 0.99f })
	{
		const FVector Point = MortarRules::BracketPoint(Target, 800.f, Roll);
		TestEqual(FString::Printf(TEXT("roll %.2f: 8 m off"), Roll), static_cast<float>(FVector::Dist2D(Point, Target)), 800.f, 0.5f);
		TestEqual(TEXT("... on the ground plane"), static_cast<float>(Point.Z), 30.f);
	}
	TestTrue(TEXT("5 m away = the same target (keeps walking in)"), MortarRules::IsSameTarget(Target, Target + FVector(500.f, 0.f, 0.f), B));
	TestFalse(TEXT("20 m away = a new target (starts again)"), MortarRules::IsSameTarget(Target, Target + FVector(2000.f, 0.f, 0.f), B));

	// The crew's three places: distinct, at the radius, behind / beside the tube (not in front of it).
	const FVector Nest(100.f, 200.f, 0.f);
	TArray<FVector> Places;
	for (int32 Index = 0; Index < Config.CrewNeeded; ++Index)
	{
		const FVector Place = MortarRules::CrewPlace(Nest, 0.f, Index, Config);
		TestEqual(FString::Printf(TEXT("place %d at the crew radius"), Index), static_cast<float>(FVector::Dist2D(Place, Nest)), Config.CrewRadiusCm, 0.5f);
		TestTrue(FString::Printf(TEXT("place %d not in front of the tube"), Index), Place.X - Nest.X <= 1.f);
		for (const FVector& Other : Places)
		{
			TestTrue(TEXT("places apart"), FVector::Dist2D(Place, Other) > 100.f);
		}
		Places.Add(Place);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMortarRoundTest, "CodexTactics.Weapons.Mortar.Round",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMortarRoundTest::RunTest(const FString&)
{
	const FMortarNestConfig Config = MortarRulesTest::ProjectMortar(*this);
	const FWeaponStats& Round = Config.Round;
	TestTrue(TEXT("the round: kind projectile, high-angle"), Round.Kind == EWeaponKind::Projectile && Round.Projectile.IsHighAngle());
	TestTrue(TEXT("explosive, class vehicle"), Round.Damage.DamageType == EDamageType::Explosive && Round.Damage.PenetrationClass == EArmorClass::Vehicle);
	TestTrue(TEXT("a blast radius"), Round.Explosion.RadiusCm > 0.f);
	const float Gravity = ProjectileRules::GravityFor(Round.Projectile, -980.f);
	const FVector Tube(0.f, 0.f, 100.f);
	for (const float Metres : { 30.f, 80.f, 150.f, 250.f })
	{
		FVector Velocity;
		const FVector Target(Metres * 100.f, 0.f, 0.f);
		const bool bSolved = ProjectileRules::SolveLaunch(Tube, Target, Round.Projectile.SpeedCmS, Gravity, EProjectileTrajectory::HighAngle, Velocity);
		const float Elevation = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Z, FVector(Velocity.X, Velocity.Y, 0.f).Size()));
		const float Flight = ProjectileRules::FlightTimeTo(Velocity, Metres * 100.f);
		TestTrue(FString::Printf(TEXT("%.0f m: a high-angle solution (%.1f deg, %.1f s)"), Metres, Elevation, Flight), bSolved && Elevation > 45.f);
		TestTrue(FString::Printf(TEXT("%.0f m: a long but playable flight (%.1f s in 2 .. 10 s)"), Metres, Flight), Flight > 2.f && Flight < 10.f);
	}
	FVector Velocity;
	TestFalse(TEXT("400 m: out of the round's reach"), ProjectileRules::SolveLaunch(Tube, FVector(40000.f, 0.f, 0.f), Round.Projectile.SpeedCmS, Gravity,
		EProjectileTrajectory::HighAngle, Velocity));

	// Bad data keeps the defaults with a line each.
	TArray<FString> Errors;
	const FMortarNestConfig Bad = MortarRules::Parse(*MortarRulesTest::MortarJson(TEXT("{\"crew_needed\": 0, \"min_range_m\": 300, \"max_range_m\": 100,")
		TEXT(" \"round\": {\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 60, \"trajectory\": \"flat\"}}}")), &Errors);
	TestTrue(FString::Printf(TEXT("bad data: errors (%d)"), Errors.Num()), Errors.Num() >= 3);
	TestEqual(TEXT("bad data: the crew of three kept"), Bad.CrewNeeded, 3);
	TestTrue(TEXT("bad data: the band kept"), Bad.MinRangeCm == 3000.f && Bad.MaxRangeCm == 25000.f);
	TestTrue(TEXT("bad data: a flat round refused (the default high-angle one kept)"), Bad.Round.Projectile.IsHighAngle());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMortarPmcTest, "CodexTactics.Weapons.Mortar.PmcCluster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMortarPmcTest::RunTest(const FString&)
{
	// The biggest cluster of the seen enemies.
	const TArray<FVector> Seen = { FVector(8000.f, 0.f, 0.f), FVector(8300.f, 200.f, 0.f), FVector(8100.f, -300.f, 0.f), FVector(2000.f, 6000.f, 0.f) };
	FVector Centre;
	int32 Count = 0;
	TestTrue(TEXT("a cluster found"), MortarRules::BestCluster(Seen, 800.f, FVector::ZeroVector, Centre, Count));
	TestEqual(TEXT("the three together"), Count, 3);
	TestTrue(TEXT("its centre among them"), FVector::Dist2D(Centre, FVector(8133.f, -33.f, 0.f)) < 5.f);
	TestFalse(TEXT("nobody seen: no cluster"), MortarRules::BestCluster({}, 800.f, FVector::ZeroVector, Centre, Count));
	const TArray<FVector> Two = { FVector(9000.f, 0.f, 0.f), FVector(5000.f, 0.f, 0.f) };
	MortarRules::BestCluster(Two, 800.f, FVector::ZeroVector, Centre, Count);
	TestTrue(TEXT("a tie: the one nearer the nest"), Count == 1 && FVector::Dist2D(Centre, FVector(5000.f, 0.f, 0.f)) < 1.f);

	// The commander's score (weights in pmc_commander.json).
	FPmcWeights Weights;
	TestTrue(TEXT("pmc_commander.json loads"), PmcCommanderRules::LoadWeightsFile(PmcCommanderRules::DefaultWeightsPath(), Weights));
	TestTrue(TEXT("one man seen: a third of the weight"), FMath::IsNearlyEqual(PmcCommanderRules::ScoreMortarFire(1, true, true, Weights), Weights.MortarFire / Weights.MortarClusterFull));
	TestEqual(TEXT("a full cluster: the whole weight"), PmcCommanderRules::ScoreMortarFire(5, true, true, Weights), Weights.MortarFire);
	TestEqual(TEXT("an unmanned nest: 0"), PmcCommanderRules::ScoreMortarFire(5, false, true, Weights), 0.f);
	TestEqual(TEXT("out of the band: 0"), PmcCommanderRules::ScoreMortarFire(5, true, false, Weights), 0.f);
	TestTrue(TEXT("the shipped weights: one seen man is not worth a round (< threshold)"), PmcCommanderRules::ScoreMortarFire(1, true, true, Weights) < Weights.MortarThreshold);
	TestTrue(TEXT("the shipped weights: two men together are (>= threshold)"), PmcCommanderRules::ScoreMortarFire(2, true, true, Weights) >= Weights.MortarThreshold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMortarPlacementTest, "CodexTactics.Weapons.Mortar.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMortarPlacementTest::RunTest(const FString&)
{
	TArray<FString> Errors;
	const TArray<FMortarNestPlacement> Read = MortarRules::ParsePlacements(*MortarRulesTest::MortarJson(TEXT("{\"mortar_nests\": [")
		TEXT("{\"id\": \"A\", \"side\": \"squad\", \"pos\": [100, 200], \"yaw\": 45},")
		TEXT("{\"id\": \"B\", \"side\": \"pmc\", \"pos\": [1, 2, 3], \"crew\": 3, \"ammo\": 8},")
		TEXT("{\"id\": \"C\", \"team\": 4, \"pos\": [5, 6]},")
		TEXT("{\"id\": \"D\"}]}")), &Errors);
	TestEqual(TEXT("three nests read"), Read.Num(), 3);
	TestEqual(TEXT("one without a pos refused"), Errors.Num(), 1);
	if (Read.Num() == 3)
	{
		TestTrue(TEXT("A: the squad's, dropped on the ground"), Read[0].TeamId == 0 && !Read[0].bHasZ && Read[0].YawDeg == 45.f && Read[0].Crew == 0);
		TestTrue(TEXT("B: a PMC nest (a free team), its crew of 3, 8 rounds, a z"), Read[1].TeamId == -1 && Read[1].Crew == 3 && Read[1].Ammo == 8 && Read[1].bHasZ);
		TestTrue(TEXT("C: team 4"), Read[2].TeamId == 4);
	}
	TestEqual(TEXT("no mortar_nests: none"), MortarRules::ParsePlacements(*MortarRulesTest::MortarJson(TEXT("{}"))).Num(), 0);

	// The combat lab's two test nests (no Depot nests yet).
	Errors.Reset();
	const TSharedPtr<FJsonObject> Lab = MortarRulesTest::MortarFile(TEXT("Data/LevelJson/L_FormationLabCombat.json"));
	const TArray<FMortarNestPlacement> LabNests = Lab.IsValid() ? MortarRules::ParsePlacements(*Lab, &Errors) : TArray<FMortarNestPlacement>();
	TestEqual(TEXT("the combat lab: two nests"), LabNests.Num(), 2);
	TestEqual(TEXT("the combat lab: no errors"), Errors.Num(), 0);
	TestTrue(TEXT("the combat lab: one squad nest, one unmanned PMC nest"), LabNests.Num() == 2 && LabNests[0].TeamId == 0 && LabNests[1].TeamId < 0
		&& LabNests[1].Crew == 0);
	const TSharedPtr<FJsonObject> Depot = MortarRulesTest::MortarFile(TEXT("Data/LevelJson/L_Gen_Depot_1.json"));
	TestTrue(TEXT("no nests in the Depot yet"), !Depot.IsValid() || MortarRules::ParsePlacements(*Depot).Num() == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
