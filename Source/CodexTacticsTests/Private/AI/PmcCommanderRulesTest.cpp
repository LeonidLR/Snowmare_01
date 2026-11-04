#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcOperatorRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// The PMC squad commander's utility scoring (user request 2026-11-03, docs/systems/pmc-squad-ai.md): pure rules.

namespace PmcCommanderTestLocal
{
	FPmcFighterView Fighter(float ThreatCm, bool bSeen)
	{
		FPmcFighterView View;
		View.Location = FVector::ZeroVector;
		View.bHasThreat = true;
		View.Threat.Location = FVector(ThreatCm, 0.f, 0.f);
		View.Threat.DistanceCm = ThreatCm;
		View.Threat.bSeenNow = bSeen;
		View.Threat.SecondsSince = bSeen ? 0.f : 3.f;
		return View;
	}

	FPmcSquadView Squad(int32 Alive, int32 Enemies, bool bContact)
	{
		FPmcSquadView View;
		View.Alive = Alive;
		View.EnemiesKnown = Enemies;
		View.EnemiesSeenNow = bContact ? Enemies : 0;
		View.bContact = bContact;
		View.bEverContact = bContact;
		return View;
	}

	TSharedPtr<FJsonObject> PmcParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderFlankVsFrontalTest, "CodexTactics.AI.PmcCommander.FlankVsFrontal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderFlankVsFrontalTest::RunTest(const FString&)
{
	using namespace PmcCommanderTestLocal;
	const FPmcWeights W;
	// A healthy fighter in contact at 12 m (in range) with a mate pinning the threat: the flank beats a frontal advance / hold.
	FPmcFighterView F = Fighter(1200.f, true);
	FPmcSquadView S = Squad(3, 3, true);
	S.MatesSuppressing = 1;
	const TArray<FPmcOrderScore> Scored = PmcCommanderRules::ScoreAll(F, S, W);
	TestEqual(TEXT("Flank wins with a mate suppressing"), PmcCommanderRules::OrderName(Scored[0].Order), TEXT("Flank"));
	TestTrue(TEXT("Flank scores above Advance"), PmcCommanderRules::ScoreOrder(EPmcOrder::Flank, F, S, W) > PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, F, S, W));
	TestTrue(TEXT("Flank scores above Hold"), PmcCommanderRules::ScoreOrder(EPmcOrder::Flank, F, S, W) > PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, F, S, W));
	// A mate already flanking: this one does not pile onto the same flank - Suppress (frontal) wins.
	S.MatesSuppressing = 0;
	S.MatesFlanking = 1;
	const TArray<FPmcOrderScore> Second = PmcCommanderRules::ScoreAll(F, S, W);
	TestEqual(TEXT("With a flanker out, the next man suppresses (frontal)"), PmcCommanderRules::OrderName(Second[0].Order), TEXT("Suppress"));
	// A threat far out of range and unseen: nobody flanks a memory; Advance (bounding) takes over.
	FPmcFighterView Far = Fighter(4000.f, false);
	FPmcSquadView Quiet = Squad(3, 2, true);
	TestEqual(TEXT("Flank needs a seen threat within the band"), PmcCommanderRules::ScoreOrder(EPmcOrder::Flank, Far, Quiet, W), 0.f);
	TestTrue(TEXT("Advance scores on a remembered far threat"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Far, Quiet, W) > 0.f);
	// Bounding overwatch: with a mate already advancing the second man's advance is nearly off.
	FPmcSquadView Bounding = Quiet;
	Bounding.MatesAdvancing = 1;
	TestTrue(TEXT("One man moves at a time"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Far, Bounding, W) < PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Far, Quiet, W) * 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderSurvivalTest, "CodexTactics.AI.PmcCommander.Survival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderSurvivalTest::RunTest(const FString&)
{
	using namespace PmcCommanderTestLocal;
	const FPmcWeights W;
	// Badly wounded in contact: fall back beats everything.
	FPmcFighterView Hurt = Fighter(1200.f, true);
	Hurt.HealthFrac = 0.2f;
	TestEqual(TEXT("A wounded man falls back"), PmcCommanderRules::OrderName(PmcCommanderRules::ScoreAll(Hurt, Squad(2, 3, true), W)[0].Order), TEXT("FallBack"));
	// A wounded mate, nobody in sight, a medkit: heal.
	FPmcFighterView Medic = Fighter(2000.f, false);
	Medic.bHasMedkit = true;
	Medic.WoundedMateDistanceCm = 400.f;
	Medic.WoundedMateHealthFrac = 0.3f;
	FPmcSquadView Lull = Squad(3, 2, true);
	Lull.EnemiesSeenNow = 0;
	TestEqual(TEXT("A medkit goes to the wounded mate when nobody watches"), PmcCommanderRules::OrderName(PmcCommanderRules::ScoreAll(Medic, Lull, W)[0].Order), TEXT("Heal"));
	FPmcSquadView Watched = Lull;
	Watched.EnemiesSeenNow = 2;
	TestTrue(TEXT("... but not under the enemy's eyes"), PmcCommanderRules::ScoreOrder(EPmcOrder::Heal, Medic, Watched, W) < PmcCommanderRules::ScoreOrder(EPmcOrder::Heal, Medic, Lull, W) * 0.5f);
	// No contact yet in the fight: the ambush hold; once the squad met the enemy it is over.
	FPmcFighterView Waiting;
	Waiting.bInCover = true;
	TestEqual(TEXT("No contact: hold in ambush"), PmcCommanderRules::OrderName(PmcCommanderRules::ScoreAll(Waiting, Squad(3, 0, false), W)[0].Order), TEXT("Ambush"));
	TestEqual(TEXT("After the first contact the ambush is over"), PmcCommanderRules::ScoreOrder(EPmcOrder::Ambush, Waiting, Squad(3, 2, true), W), 0.f);
	// Exposed at one spot for too long: relocate.
	FPmcFighterView Spotted = Fighter(1500.f, true);
	Spotted.bInCover = true;
	Spotted.CoverQuality = 0.9f;
	Spotted.bExposed = true;
	Spotted.SecondsExposedHere = 8.f;
	Spotted.bCoverAvailable = true;
	TestTrue(TEXT("Relocate scores after RelocateAfterSeconds exposed"), PmcCommanderRules::ScoreOrder(EPmcOrder::Relocate, Spotted, Squad(3, 3, true), W) > 0.f);
	Spotted.SecondsExposedHere = 2.f;
	TestEqual(TEXT("... not before"), PmcCommanderRules::ScoreOrder(EPmcOrder::Relocate, Spotted, Squad(3, 3, true), W), 0.f);
	// Cover: an exposed man in the open with a slot nearby takes it; one already well covered does not.
	FPmcFighterView Open = Fighter(1500.f, true);
	Open.bExposed = true;
	Open.bCoverAvailable = true;
	TestEqual(TEXT("In the open: take cover"), PmcCommanderRules::OrderName(PmcCommanderRules::ScoreAll(Open, Squad(3, 3, true), W)[0].Order), TEXT("TakeCover"));
	FPmcFighterView Covered = Open;
	Covered.bInCover = true;
	Covered.CoverQuality = 0.9f;
	TestEqual(TEXT("Well covered: no cover order"), PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, Covered, Squad(3, 3, true), W), 0.f);
	// An empty rifle at point-blank range: the sidearm.
	FPmcFighterView Empty = Fighter(250.f, true);
	Empty.ClipFrac = 0.f;
	Empty.bHasSidearm = true;
	TestEqual(TEXT("Empty clip at 2.5 m: the pistol"), PmcCommanderRules::OrderName(PmcCommanderRules::ScoreAll(Empty, Squad(3, 3, true), W)[0].Order), TEXT("Sidearm"));
	// Knocked down: nothing scores.
	FPmcFighterView Down = Fighter(1000.f, true);
	Down.bKnockedDown = true;
	TestEqual(TEXT("Knocked down: Hold"), PmcCommanderRules::OrderName(PmcCommanderRules::Pick(PmcCommanderRules::ScoreAll(Down, Squad(3, 3, true), W), EPmcOrder::Flank, true, W)), TEXT("Hold"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderGeometryTest, "CodexTactics.AI.PmcCommander.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderGeometryTest::RunTest(const FString&)
{
	const FPmcWeights W;
	const FVector Threat(0.f, 0.f, 0.f);
	const FVector Centre(2000.f, 0.f, 0.f);
	const FVector Fighter(2000.f, 300.f, 0.f);
	const FVector Flank = PmcCommanderRules::FlankPoint(Threat, Centre, Fighter, W);
	TestEqual(TEXT("Flank point FlankDistanceCm from the threat"), static_cast<float>(FVector::Dist2D(Flank, Threat)), W.FlankDistanceCm, 1.f);
	TestEqual(TEXT("Flank point 90 deg off the threat -> squad line"), PmcCommanderRules::ApproachAngleDeg(Threat, Flank, Centre), 90.f, 0.5f);
	TestTrue(TEXT("... on the side away from the fighter"), FVector::DotProduct(Flank - Threat, Fighter - Threat) <= 0.f + 1.f);
	TestEqual(TEXT("Advance stops at the range fraction"), static_cast<float>(FVector::Dist2D(PmcCommanderRules::AdvancePoint(Centre, Threat, 1400.f, W), Threat)), 1400.f * W.AdvanceStopRangeFrac, 1.f);
	TestEqual(TEXT("Advance from inside the stop range stays put"), PmcCommanderRules::AdvancePoint(FVector(500.f, 0.f, 0.f), Threat, 1400.f, W), FVector(500.f, 0.f, 0.f));
	TestEqual(TEXT("Fall back straight away"), PmcCommanderRules::FallBackPoint(Centre, Threat, 800.f), FVector(2800.f, 0.f, 0.f));
	// Cover quality: the wall behind the man (he faces along the normal) shields threats on the -normal side.
	TestEqual(TEXT("Threat behind the wall: full cover"), PmcCommanderRules::CoverQualityAgainst(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), FVector(-1000.f, 0.f, 0.f)), 1.f, 0.001f);
	TestEqual(TEXT("Threat in front of the man: no cover"), PmcCommanderRules::CoverQualityAgainst(FVector::ZeroVector, FVector(1.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f)), 0.f, 0.001f);
	TestEqual(TEXT("Numbers: even = 0.5"), PmcCommanderRules::NumbersFactor(3, 3), 0.5f, 0.001f);
	TestEqual(TEXT("Numbers: two more = 1"), PmcCommanderRules::NumbersFactor(4, 2), 1.f, 0.001f);
	TestEqual(TEXT("Numbers: two fewer = 0"), PmcCommanderRules::NumbersFactor(1, 3), 0.f, 0.001f);
	// Hysteresis: the running order within the margin keeps running.
	TArray<FPmcOrderScore> Scored = { { EPmcOrder::Suppress, 0.80f }, { EPmcOrder::Flank, 0.75f }, { EPmcOrder::Hold, 0.1f } };
	TestEqual(TEXT("Running Flank within the margin stays"), PmcCommanderRules::OrderName(PmcCommanderRules::Pick(Scored, EPmcOrder::Flank, true, W)), TEXT("Flank"));
	TestEqual(TEXT("A finished Flank gives way to the best"), PmcCommanderRules::OrderName(PmcCommanderRules::Pick(Scored, EPmcOrder::Flank, false, W)), TEXT("Suppress"));
	Scored[0].Score = 0.95f;
	TestEqual(TEXT("Beyond the margin the best wins"), PmcCommanderRules::OrderName(PmcCommanderRules::Pick(Scored, EPmcOrder::Flank, true, W)), TEXT("Suppress"));
	TestEqual(TEXT("A committed walk keeps going under the move margin"), PmcCommanderRules::OrderName(PmcCommanderRules::PickWithMargin(Scored, EPmcOrder::Flank, true, W.MoveSwitchMargin)), TEXT("Flank"));
	TestTrue(TEXT("Flank / Advance / FallBack / Relocate / TakeCover are walks"), PmcCommanderRules::IsMoveOrder(EPmcOrder::Flank) && PmcCommanderRules::IsMoveOrder(EPmcOrder::TakeCover)
		&& !PmcCommanderRules::IsMoveOrder(EPmcOrder::Suppress) && !PmcCommanderRules::IsMoveOrder(EPmcOrder::Hold));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderWeightsJsonTest, "CodexTactics.AI.PmcCommander.WeightsJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderWeightsJsonTest::RunTest(const FString&)
{
	using namespace PmcCommanderTestLocal;
	const TSharedPtr<FJsonObject> Json = PmcParseJson(TEXT("{\"name\":\"aggressive\",\"flank\":2.5,\"heal_below\":0.6,\"decision_interval\":0.01,\"unknown_key\":7}"));
	TestNotNull(TEXT("parsed"), Json.Get());
	if (!Json.IsValid())
	{
		return false;
	}
	const FPmcWeights W = PmcCommanderRules::FromJson(*Json);
	TestEqual(TEXT("name"), W.Name, FString(TEXT("aggressive")));
	TestEqual(TEXT("flank"), W.Flank, 2.5f, 0.001f);
	TestEqual(TEXT("heal_below"), W.HealBelow, 0.6f, 0.001f);
	TestEqual(TEXT("decision interval floored at 0.1"), W.DecisionInterval, 0.1f, 0.001f);
	TestEqual(TEXT("missing keys keep the defaults"), W.Suppress, FPmcWeights().Suppress, 0.001f);
	// The shipped file loads.
	FPmcWeights Shipped;
	TestTrue(TEXT("Content/Data/AI/pmc_commander.json loads"), PmcCommanderRules::LoadWeightsFile(FString(), Shipped));
	TestTrue(TEXT("... with a positive flank weight"), Shipped.Flank > 0.f);
	FPmcWeights Missing;
	TestFalse(TEXT("a missing file gives the defaults"), PmcCommanderRules::LoadWeightsFile(TEXT("no_such_weights_file.json"), Missing));
	TestEqual(TEXT("... name default"), Missing.Name, FString(TEXT("default")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcOperatorsDataTest, "CodexTactics.AI.PmcOperators.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcOperatorsDataTest::RunTest(const FString&)
{
	using namespace PmcCommanderTestLocal;
	const TSharedPtr<FJsonObject> Json = PmcParseJson(TEXT("{\"display_names\":[\"A\",\"B\"],\"looks\":[{\"id\":\"x\",\"tint_srgb\":[255,0,0],\"snow\":0.5},{\"id\":\"y\"}],")
		TEXT("\"weapon_pool\":[{\"id\":\"m16\",\"weight\":1},{\"id\":\"shotgun\",\"weight\":0}],\"stats\":{\"accuracy\":70,\"medkits\":2},\"factions\":{\"monsters_attack_pmc\":false,\"pmc_self_defence_m\":5}}"));
	TestNotNull(TEXT("parsed"), Json.Get());
	if (!Json.IsValid())
	{
		return false;
	}
	const FPmcOperatorConfig C = PmcOperatorRules::FromJson(*Json);
	TestEqual(TEXT("names"), C.Names.Num(), 2);
	TestEqual(TEXT("looks"), C.Looks.Num(), 2);
	TestEqual(TEXT("look x snow"), C.Looks[0].SnowShare, 0.5f, 0.001f);
	TestTrue(TEXT("look x tint red"), C.Looks[0].Tint.R > 0.9f && C.Looks[0].Tint.G < 0.01f);
	TestEqual(TEXT("accuracy"), C.Accuracy, 70.f, 0.001f);
	TestEqual(TEXT("medkits"), C.Medkits, 2);
	TestFalse(TEXT("monsters flag"), C.bMonstersAttackPmc);
	TestEqual(TEXT("self-defence metres -> cm"), C.SelfDefenceCm, 500.f, 0.001f);
	// Picks: seeded, a squad of two wears two different looks, a zero-weight weapon is never drawn.
	TestNotEqual(TEXT("two fighters, two looks"), PmcOperatorRules::PickLook(C, 3, 0).Id, PmcOperatorRules::PickLook(C, 3, 1).Id);
	TestEqual(TEXT("the same seed + index = the same name"), PmcOperatorRules::PickName(C, 3, 1), PmcOperatorRules::PickName(C, 3, 1));
	for (int32 Index = 0; Index < 20; ++Index)
	{
		TestEqual(TEXT("zero weight never drawn"), PmcOperatorRules::PickWeaponId(C, Index, Index), FString(TEXT("m16")));
	}
	TestEqual(TEXT("weapon asset path"), PmcOperatorRules::WeaponAssetPath(TEXT("shotgun")), FString(TEXT("/Game/Data/Weapons/DA_Weapon_shotgun.DA_Weapon_shotgun")));
	TestTrue(TEXT("Content/Data/AI/pmc_operators.json loads"), PmcOperatorRules::Reload());
	TestTrue(TEXT("... with looks and a weapon pool"), PmcOperatorRules::Get().Looks.Num() >= 1 && PmcOperatorRules::Get().WeaponPool.Num() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderHighGroundTest, "CodexTactics.AI.PmcCommander.HighGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderHighGroundTest::RunTest(const FString&)
{
	// The bonus grows per 3 m level up to the cap, never below 1; 0 switches it off; the JSON keys load.
	FPmcWeights W;
	W.HighGround = 0.2f;
	W.HighGroundMaxLevels = 3;
	TestEqual(TEXT("ground = 1"), PmcCommanderRules::HighGroundFactor(0, W), 1.f, 0.001f);
	TestEqual(TEXT("below ground = 1"), PmcCommanderRules::HighGroundFactor(-2, W), 1.f, 0.001f);
	TestEqual(TEXT("one roof up"), PmcCommanderRules::HighGroundFactor(1, W), 1.2f, 0.001f);
	TestEqual(TEXT("capped at 3 levels"), PmcCommanderRules::HighGroundFactor(7, W), 1.6f, 0.001f);
	TestTrue(TEXT("higher is better"), PmcCommanderRules::HighGroundFactor(2, W) > PmcCommanderRules::HighGroundFactor(1, W));
	W.HighGround = 0.f;
	TestEqual(TEXT("weight 0 = height ignored"), PmcCommanderRules::HighGroundFactor(3, W), 1.f, 0.001f);
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TEXT("{\"high_ground\": 0.3, \"high_ground_max_levels\": 2}"));
	if (!TestTrue(TEXT("json parses"), FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid()))
	{
		return false;
	}
	const FPmcWeights Loaded = PmcCommanderRules::FromJson(*Json);
	TestEqual(TEXT("high_ground read"), Loaded.HighGround, 0.3f, 0.001f);
	TestEqual(TEXT("high_ground_max_levels read"), Loaded.HighGroundMaxLevels, 2);
	TestEqual(TEXT("loaded cap"), PmcCommanderRules::HighGroundFactor(5, Loaded), 1.6f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaSpawnPairTest, "CodexTactics.AI.PmcCommander.SpawnPair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaSpawnPairTest::RunTest(const FString&)
{
	// A line of spots 10 m apart on alternating levels: every pick is 35..80 m apart, reachable, the same seed -> the same pair.
	TArray<FPmcSpawnCandidate> Spots;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		Spots.Add({ FVector(Index * 1000.f, 0.f, (Index % 3) * 300.f), Index % 3 });
	}
	auto All = [](int32, int32) { return true; };
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	TSet<FIntPoint> Distinct;
	int32 MixedLevels = 0;
	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		if (!TestTrue(TEXT("a pair found"), PmcCommanderRules::PickSpawnPair(Spots, 3500.f, 8000.f, Seed, All, A, B)))
		{
			return false;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Spots[A].Location, Spots[B].Location));
		TestTrue(TEXT("35..80 m apart"), Distance >= 3500.f && Distance <= 8000.f);
		int32 A2 = INDEX_NONE;
		int32 B2 = INDEX_NONE;
		PmcCommanderRules::PickSpawnPair(Spots, 3500.f, 8000.f, Seed, All, A2, B2);
		TestTrue(TEXT("deterministic"), A == A2 && B == B2);
		Distinct.Add(FIntPoint(A, B));
		MixedLevels += Spots[A].Level != Spots[B].Level ? 1 : 0;
	}
	TestTrue(TEXT("seeds vary the pair"), Distinct.Num() >= 5);
	TestTrue(TEXT("variety bias: mostly different levels"), MixedLevels >= 10);
	// Reachability: only pairs touching spot 0 are walkable -> the pick includes spot 0.
	auto OnlyZero = [](int32 I, int32 J) { return I == 0 || J == 0; };
	TestTrue(TEXT("a reachable pair"), PmcCommanderRules::PickSpawnPair(Spots, 3500.f, 8000.f, 3, OnlyZero, A, B, 200));
	TestTrue(TEXT("... it is the reachable one"), A == 0 || B == 0);
	// Nothing in range / nothing reachable -> false.
	TestFalse(TEXT("no pair in range"), PmcCommanderRules::PickSpawnPair(Spots, 20000.f, 30000.f, 1, All, A, B));
	TestFalse(TEXT("no pair reachable"), PmcCommanderRules::PickSpawnPair(Spots, 3500.f, 8000.f, 1, [](int32, int32) { return false; }, A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderEngagementBandTest, "CodexTactics.AI.PmcCommander.EngagementBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderEngagementBandTest::RunTest(const FString&)
{
	// User request 2026-11-04: riflemen keep their distance (fire from corners / prone, never point-blank), a pistol comes closer, the shotgun
	// is the close-range weapon. Bands per weapon id, capped at the weapon's reach.
	using namespace PmcCommanderTestLocal;
	const FPmcWeights W;
	// Resolve: m16 25-60 m but its reach is 25 m -> max 23.75 (0.95 x reach), min 19 (0.8 x max); shotgun 4-14 m, reach 7 -> 4-6.65, close quarters.
	const FPmcEngagementBand Rifle = PmcCommanderRules::ResolveBand(TEXT("m16"), 2500.f, W);
	TestEqual(TEXT("rifle max = 0.95 x reach"), Rifle.MaxCm, 2375.f, 0.5f);
	TestEqual(TEXT("rifle min = 0.8 x max"), Rifle.MinCm, 1900.f, 0.5f);
	TestFalse(TEXT("rifle is not close quarters"), Rifle.bCloseQuarters);
	const FPmcEngagementBand Long = PmcCommanderRules::ResolveBand(TEXT("m16"), 10000.f, W);
	TestEqual(TEXT("a long-reach rifle keeps the data band 25-60 m (min)"), Long.MinCm, 2500.f, 0.5f);
	TestEqual(TEXT("... (max)"), Long.MaxCm, 6000.f, 0.5f);
	const FPmcEngagementBand Shotgun = PmcCommanderRules::ResolveBand(TEXT("shotgun"), 700.f, W);
	TestEqual(TEXT("shotgun min 4 m"), Shotgun.MinCm, 400.f, 0.5f);
	TestEqual(TEXT("shotgun max 6.65 m"), Shotgun.MaxCm, 665.f, 0.5f);
	TestTrue(TEXT("shotgun is close quarters"), Shotgun.bCloseQuarters);
	const FPmcEngagementBand Unknown = PmcCommanderRules::ResolveBand(TEXT("crossbow"), 2000.f, W);
	TestEqual(TEXT("an unknown weapon: 0.5 x reach"), Unknown.MinCm, 1000.f, 0.5f);
	TestEqual(TEXT("... to 0.95 x reach"), Unknown.MaxCm, 1900.f, 0.5f);
	FPmcWeights Off;
	Off.bUseBands = false;
	TestFalse(TEXT("use_engagement_bands false: no band"), PmcCommanderRules::ResolveBand(TEXT("m16"), 2500.f, Off).IsValid());

	// Band factor: 1 inside, a strong (quadratic) penalty close in, a mild one out of reach.
	TestEqual(TEXT("inside the band = 1"), PmcCommanderRules::BandFactor(2100.f, Rifle, W), 1.f, 0.0001f);
	TestEqual(TEXT("point-blank = the floor 0.1"), PmcCommanderRules::BandFactor(0.f, Rifle, W), 0.1f, 0.0001f);
	const float At8m = PmcCommanderRules::BandFactor(800.f, Rifle, W);
	TestTrue(FString::Printf(TEXT("8 m from a rifleman's threat scores low (%.2f < 0.3)"), At8m), At8m < 0.3f);
	TestTrue(TEXT("closer is worse"), PmcCommanderRules::BandFactor(400.f, Rifle, W) < At8m && At8m < PmcCommanderRules::BandFactor(1500.f, Rifle, W));
	TestEqual(TEXT("2 x max = 0.5"), PmcCommanderRules::BandFactor(4750.f, Rifle, W), 0.5f, 0.0001f);
	TestEqual(TEXT("no band = 1"), PmcCommanderRules::BandFactor(100.f, FPmcEngagementBand(), W), 1.f, 0.0001f);

	// Too close: a rifleman under 0.8 x 19 m = 15.2 m; never the shotgun.
	TestTrue(TEXT("rifleman at 8 m is too close"), PmcCommanderRules::IsTooClose(800.f, Rifle, W));
	TestFalse(TEXT("rifleman at 16 m is not"), PmcCommanderRules::IsTooClose(1600.f, Rifle, W));
	TestFalse(TEXT("a shotgunner at 2 m is never too close"), PmcCommanderRules::IsTooClose(200.f, Shotgun, W));

	// Scoring: a rifleman with a seen threat 8 m off backs off (FallBack beats Hold / Suppress / Crouch / Advance).
	FPmcFighterView Close = Fighter(800.f, true);
	Close.WeaponRangeCm = 2500.f;
	Close.Band = Rifle;
	const FPmcSquadView Even = Squad(3, 3, true);
	const TArray<FPmcOrderScore> CloseScores = PmcCommanderRules::ScoreAll(Close, Even, W);
	TestEqual(TEXT("rifleman at 8 m: FALL BACK to his band"), FString(PmcCommanderRules::OrderName(CloseScores[0].Order)), FString(TEXT("FallBack")));
	TestEqual(TEXT("... no Advance (inside his stop)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Close, Even, W), 0.f, 0.0001f);
	FPmcFighterView CloseNoBand = Close;
	CloseNoBand.Band = FPmcEngagementBand();
	TestEqual(TEXT("the old rules (no band) never retreat a healthy man at 8 m"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, CloseNoBand, Even, W), 0.f, 0.0001f);
	// Hysteresis: at 17 m (past the 15.2 m trigger) a fresh decision does not retreat, a RUNNING retreat goes on to the band's middle.
	FPmcFighterView Mid = Close;
	Mid.Threat.DistanceCm = 1700.f;
	TestEqual(TEXT("17 m, not retreating: no new retreat"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, Mid, Even, W), 0.f, 0.0001f);
	Mid.RunningOrder = EPmcOrder::FallBack;
	TestTrue(TEXT("17 m, retreating: the retreat keeps scoring above Hold"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, Mid, Even, W)
		> PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Mid, Even, W) + W.MoveSwitchMargin);
	FPmcFighterView Flanking = Close;
	Flanking.RunningOrder = EPmcOrder::Flank;
	TestEqual(TEXT("8 m while running a flank: no standoff (the flank walk passes the threat on purpose)"),
		PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, Flanking, Even, W), 0.f, 0.0001f);
	Mid.Threat.DistanceCm = 2200.f;
	TestEqual(TEXT("22 m (past the band's middle 21.4 m), retreating: done"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, Mid, Even, W), 0.f, 0.0001f);
	// At his band (21 m) he stays: no retreat, Hold worth its full value.
	FPmcFighterView AtBand = Close;
	AtBand.Threat.DistanceCm = 2100.f;
	AtBand.Threat.Location = FVector(2100.f, 0.f, 0.f);
	TestEqual(TEXT("rifleman at 21 m: no retreat"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, AtBand, Even, W), 0.f, 0.0001f);
	TestEqual(TEXT("... and no advance inside his band"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, AtBand, Even, W), 0.f, 0.0001f);
	TestTrue(TEXT("... Hold worth more at the band than at 8 m"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, AtBand, Even, W)
		> PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Close, Even, W) * 2.f);
	// A cover slot at band distance is preferred over one at 8 m.
	FPmcFighterView CoverFar = AtBand;
	CoverFar.bCoverAvailable = true;
	CoverFar.bExposed = true;
	CoverFar.CoverBandFactor = 1.f;
	FPmcFighterView CoverNear = CoverFar;
	CoverNear.CoverBandFactor = PmcCommanderRules::BandFactor(800.f, Rifle, W);
	TestTrue(TEXT("TakeCover: a slot in the band beats one at 8 m"), PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, CoverFar, Even, W)
		> PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, CoverNear, Even, W) * 2.f);

	// Advance / flank stop at the band: a rifleman 40 m off advances to 19 m (clamp(0.6 x 25 = 15, 19, 23.75)), not to 15.
	TestEqual(TEXT("rifle advance stops at the band's min"), PmcCommanderRules::AdvanceStopCm(2500.f, Rifle, W), 1900.f, 0.5f);
	const FVector Goal = PmcCommanderRules::AdvancePoint(FVector::ZeroVector, FVector(4000.f, 0.f, 0.f), 2500.f, W, Rifle);
	TestEqual(TEXT("... the advance goal 19 m short of the threat"), static_cast<float>(4000.0 - Goal.X), 1900.f, 1.f);
	TestEqual(TEXT("the old advance without a band: 15 m short"), static_cast<float>(4000.0 - PmcCommanderRules::AdvancePoint(FVector::ZeroVector,
		FVector(4000.f, 0.f, 0.f), 2500.f, W).X), 1500.f, 1.f);
	TestEqual(TEXT("flank distance clamped into the rifle band (10 m -> 19 m)"), PmcCommanderRules::FlankDistanceFor(Rifle, W), 1900.f, 0.5f);
	TestEqual(TEXT("flank distance without a band: 10 m"), PmcCommanderRules::FlankDistanceFor(FPmcEngagementBand(), W), 1000.f, 0.5f);
	const FVector Flank = PmcCommanderRules::FlankPoint(FVector::ZeroVector, FVector(-3000.f, 0.f, 0.f), FVector(-2000.f, 500.f, 0.f), W, 1900.f);
	TestEqual(TEXT("the flank point lies at the band distance"), static_cast<float>(FVector::Dist2D(Flank, FVector::ZeroVector)), 1900.f, 1.f);
	// The standoff goal: straight away to the band's middle (21.4 m).
	const FVector Back = PmcCommanderRules::StandoffPoint(FVector(800.f, 0.f, 0.f), FVector::ZeroVector, Rifle);
	TestEqual(TEXT("standoff point at the band's middle"), static_cast<float>(Back.X), 0.5f * (Rifle.MinCm + Rifle.MaxCm), 1.f);
	TestEqual(TEXT("... straight away (planar)"), static_cast<float>(Back.Y), 0.f, 0.01f);

	// The shotgunner closes in: 15 m off, Advance scores (x close_in) and outranks Hold; at 5 m he stops.
	FPmcFighterView Shot = Fighter(1500.f, true);
	Shot.WeaponRangeCm = 700.f;
	Shot.Band = Shotgun;
	const float ShotAdvance = PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Shot, Even, W);
	const float ShotHold = PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Shot, Even, W);
	TestTrue(FString::Printf(TEXT("shotgunner 15 m off advances (%.2f > Hold %.2f)"), ShotAdvance, ShotHold), ShotAdvance > ShotHold);
	TestEqual(TEXT("... no retreat for the shotgun"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, Shot, Even, W), 0.f, 0.0001f);
	FPmcFighterView FarRifle = Fighter(4000.f, true);
	FarRifle.WeaponRangeCm = 2500.f;
	FarRifle.Band = Rifle;
	TestTrue(TEXT("... its advance outscores a rifleman's at 40 m (close_in)"), ShotAdvance > PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, FarRifle, Even, W));
	FPmcFighterView ShotClose = Shot;
	ShotClose.Threat.DistanceCm = 600.f;
	TestEqual(TEXT("shotgunner at 6 m: no 1.8 m micro-advance (a step must be >= 2.5 m past the 4.2 m stop)"),
		PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, ShotClose, Even, W), 0.f, 0.0001f);
	ShotClose.Threat.DistanceCm = 400.f;
	TestEqual(TEXT("shotgunner at 4 m stops advancing (stop clamp(0.6 x 7, 4, 6.65) = 4.2 m)"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, ShotClose, Even, W), 0.f, 0.0001f);

	// Data: the shipped file's bands and weights.
	FPmcWeights Shipped;
	TestTrue(TEXT("pmc_commander.json loads"), PmcCommanderRules::LoadWeightsFile(FString(), Shipped));
	TestTrue(TEXT("... bands on"), Shipped.bUseBands);
	TestEqual(TEXT("... m16 band 25 m"), Shipped.Bands.FindRef(TEXT("m16")).MinCm, 2500.f, 0.5f);
	TestEqual(TEXT("... sniper band 110 m"), Shipped.Bands.FindRef(TEXT("sniper_rifle")).MaxCm, 11000.f, 0.5f);
	TestTrue(TEXT("... shotgun close quarters"), Shipped.Bands.FindRef(TEXT("shotgun")).bCloseQuarters);
	TestEqual(TEXT("... pistol band 10 m"), Shipped.Bands.FindRef(TEXT("pistol")).MinCm, 1000.f, 0.5f);
	TestEqual(TEXT("... standoff weight"), Shipped.Standoff, 1.3f, 0.001f);
	const TSharedPtr<FJsonObject> Json = PmcParseJson(TEXT("{\"use_engagement_bands\":false,\"standoff\":2,\"engagement_bands\":{\"_comment\":\"x\",\"m16\":{\"min_m\":30},\"bow\":{\"min_m\":5,\"max_m\":12,\"close_quarters\":true}}}"));
	TestNotNull(TEXT("parsed"), Json.Get());
	if (Json.IsValid())
	{
		const FPmcWeights Parsed = PmcCommanderRules::FromJson(*Json);
		TestFalse(TEXT("use_engagement_bands read"), Parsed.bUseBands);
		TestEqual(TEXT("standoff read"), Parsed.Standoff, 2.f, 0.001f);
		TestEqual(TEXT("a partial row keeps the rest (m16 min 30, max 60)"), Parsed.Bands.FindRef(TEXT("m16")).MaxCm, 6000.f, 0.5f);
		TestEqual(TEXT("... min overridden"), Parsed.Bands.FindRef(TEXT("m16")).MinCm, 3000.f, 0.5f);
		TestTrue(TEXT("a new weapon row added"), Parsed.Bands.Contains(TEXT("bow")) && Parsed.Bands.FindRef(TEXT("bow")).bCloseQuarters);
		TestFalse(TEXT("the comment is no band"), Parsed.Bands.Contains(TEXT("_comment")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
