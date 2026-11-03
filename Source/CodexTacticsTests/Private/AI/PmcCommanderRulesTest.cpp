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

#endif // WITH_DEV_AUTOMATION_TESTS
