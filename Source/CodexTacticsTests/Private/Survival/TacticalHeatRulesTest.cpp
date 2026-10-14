#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Data/TacticalHeatTuning.h"
#include "Interactables/DeployableRules.h"
#include "Survival/ColdRules.h"
#include "Survival/TacticalHeatRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sprint 15 «Mobile & Tactical Heat» (TANDEM request #14, UE-only; Gemini's spec values = FTacticalHeatConfig defaults).

#define HEAT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Survival.TacticalHeat." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HEAT_TEST(FHeatPackResetTest, "HeatPackColdResetAndFrostbite")
bool FHeatPackResetTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	// Frostbite (100 %): -35 points, frostbite cleared, 45 s buffer.
	const FHeatPackResult Frozen = TacticalHeatRules::ApplyHeatPack(Config, 100.f, true);
	TestEqual(TEXT("100 -> 65"), Frozen.NewCold, 65.f);
	TestTrue(TEXT("frostbite removed"), Frozen.bClearsFrostbite);
	TestEqual(TEXT("buffer 45 s"), Frozen.BufferSeconds, 45.f);
	TestTrue(TEXT("no longer the frostbite tier"), ColdRules::GetTier(Frozen.NewCold) != EColdTier::Frostbite);
	// Mild cold never goes below 0; a warm operative does not need one.
	TestEqual(TEXT("20 -> 0"), TacticalHeatRules::ApplyHeatPack(Config, 20.f, false).NewCold, 0.f);
	TestFalse(TEXT("not frostbitten: nothing to clear"), TacticalHeatRules::ApplyHeatPack(Config, 50.f, false).bClearsFrostbite);
	TestFalse(TEXT("warm: refused"), TacticalHeatRules::CanUseHeatPack(0.f, false));
	TestTrue(TEXT("cold: usable"), TacticalHeatRules::CanUseHeatPack(12.f, false));
	// Rev 5d: a heat pack dropped on a mate is cracked on a cold one, handed over to a warm one - and always handed over with Shift.
	TestTrue(TEXT("drop on a cold mate: cracked on him"), TacticalHeatRules::ShouldCrackHeatPackOnDrop(12.f, false, false));
	TestTrue(TEXT("drop on a frostbitten mate: cracked on him"), TacticalHeatRules::ShouldCrackHeatPackOnDrop(0.f, true, false));
	TestFalse(TEXT("drop on a warm mate: handed over"), TacticalHeatRules::ShouldCrackHeatPackOnDrop(0.f, false, false));
	TestFalse(TEXT("Shift + drop on a cold mate: handed over (kept for later)"), TacticalHeatRules::ShouldCrackHeatPackOnDrop(55.f, false, true));
	TestFalse(TEXT("Shift + drop on a frostbitten mate: handed over"), TacticalHeatRules::ShouldCrackHeatPackOnDrop(90.f, true, true));
	// Rev 5d: a campfire indoors (Closed / Shelter zone) gives itself away within 40 % of the open-air noise radius.
	TestEqual(TEXT("campfire in the open: 15 m"), TacticalHeatRules::GetCampfireNoiseRadius(Config, false), 1500.f, 0.01f);
	TestEqual(TEXT("campfire indoors: 6 m"), TacticalHeatRules::GetCampfireNoiseRadius(Config, true), 600.f, 0.01f);
	FTacticalHeatConfig Loud = Config;
	Loud.CampfireIndoorNoiseMultiplier = 3.f;
	TestEqual(TEXT("indoor multiplier clamped to 1 (never louder inside)"), TacticalHeatRules::GetCampfireNoiseRadius(Loud, true), 1500.f, 0.01f);
	TestEqual(TEXT("1 AP, stack 3"), Config.HeatPackActionPoints * 10 + Config.HeatPackMaxStack, 13);
	return true;
}

HEAT_TEST(FHeatPackBufferTest, "HeatBufferHalvesAccumulation")
bool FHeatPackBufferTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Heat;
	const FColdConfig Cold;
	TestEqual(TEXT("buffer: x0.5"), TacticalHeatRules::GetBufferAccumulationMultiplier(Heat, 30.f), 0.5f);
	TestEqual(TEXT("no buffer: x1"), TacticalHeatRules::GetBufferAccumulationMultiplier(Heat, 0.f), 1.f);
	FColdEnvironment Open;
	FColdEnvironment Buffered;
	Buffered.AccumulationMultiplier = TacticalHeatRules::GetBufferAccumulationMultiplier(Heat, 45.f);
	// Commander (fortitude 15) in the open: 0.88 %/s -> 0.44 %/s with the buffer.
	TestEqual(TEXT("open 10 s"), ColdRules::StepCold(Cold, 0.f, 10.f, Open, EOperativeStance::Standing, 15.f), 8.8f, 0.001f);
	TestEqual(TEXT("buffered 10 s"), ColdRules::StepCold(Cold, 0.f, 10.f, Buffered, EOperativeStance::Standing, 15.f), 4.4f, 0.001f);
	// The buffer never touches warming.
	Buffered.bWarm = true;
	TestEqual(TEXT("warming unchanged"), ColdRules::StepCold(Cold, 50.f, 1.f, Buffered, EOperativeStance::Standing, 0.f), 42.f, 0.001f);
	return true;
}

HEAT_TEST(FHeatCampfireTest, "CampfireRadiusBurnWindAndCraft")
bool FHeatCampfireTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	TestEqual(TEXT("radius 3.5 m"), Config.CampfireHeatRadius, 350.f);
	TestEqual(TEXT("burns 75 s"), TacticalHeatRules::GetCampfireBurnSeconds(Config, false), 75.f);
	TestEqual(TEXT("wind on high ground: -20 %"), TacticalHeatRules::GetCampfireBurnSeconds(Config, true), 60.f, 0.001f);
	TestTrue(TEXT("ground at 140 cm is exposed"), TacticalHeatRules::IsWindExposed(Config, 140.f));
	TestFalse(TEXT("ground at 0 is sheltered"), TacticalHeatRules::IsWindExposed(Config, 0.f));
	TestTrue(TEXT("3.5 m warm"), TacticalHeatRules::IsInHeatRadius(350.f, Config.CampfireHeatRadius, Config.HeatTolerance));
	TestFalse(TEXT("3.6 m cold"), TacticalHeatRules::IsInHeatRadius(360.f, Config.CampfireHeatRadius, Config.HeatTolerance));
	// Turn-based: 4 rounds.
	FHeatBurnState Fire;
	Fire.Ignite(TacticalHeatRules::GetCampfireBurnSeconds(Config, false));
	const float PerRound = TacticalHeatRules::SecondsPerRound(75.f, Config.CampfireTurnRounds);
	TestEqual(TEXT("18.75 s per round"), PerRound, 18.75f, 0.001f);
	for (int32 Round = 1; Round <= 3; ++Round)
	{
		TestFalse(FString::Printf(TEXT("round %d: still burning"), Round), Fire.AdvanceRound(PerRound));
	}
	TestTrue(TEXT("round 4: burnt out"), Fire.AdvanceRound(PerRound) && Fire.bBurntOut && !Fire.bBurning);
	// Craft: 1 match + 1 firewood, or 10 units of flamethrower fuel instead of the wood.
	TestEqual(TEXT("3 matches, 2 wood, 25 fuel: 3 fires"), TacticalHeatRules::GetCraftableCampfires(Config, 3, 2, 25), 3);
	TestEqual(TEXT("no match: none"), TacticalHeatRules::GetCraftableCampfires(Config, 0, 5, 100), 0);
	int32 Matches = 2, Wood = 1, Fuel = 15;
	TestTrue(TEXT("first fire spends wood"), TacticalHeatRules::ConsumeCampfireMaterials(Config, Matches, Wood, Fuel) && Matches == 1 && Wood == 0 && Fuel == 15);
	TestTrue(TEXT("second fire spends fuel"), TacticalHeatRules::ConsumeCampfireMaterials(Config, Matches, Wood, Fuel) && Matches == 0 && Fuel == 5);
	TestFalse(TEXT("nothing left"), TacticalHeatRules::ConsumeCampfireMaterials(Config, Matches, Wood, Fuel));
	return true;
}

HEAT_TEST(FHeatStoveFlareTest, "StoveAndFlareRadiiAndBurnTimes")
bool FHeatStoveFlareTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	// Stove: 2.2 m, 180 s of gas, 2 AP, packed with the rest of the gas.
	TestEqual(TEXT("stove radius 2.2 m"), Config.StoveHeatRadius, 220.f);
	TestEqual(TEXT("stove gas 180 s"), Config.StoveGasCapacitySeconds, 180.f);
	TestEqual(TEXT("stove deploy 2 AP"), Config.StoveDeployActionPoints, 2);
	TestEqual(TEXT("one stove per operative"), DeployableRules::GetMaxCarried(EDeployableType::PortableStove), 1);
	FHeatBurnState Gas;
	Gas.Ignite(Config.StoveGasCapacitySeconds);
	TestFalse(TEXT("100 s burnt"), Gas.Step(100.f));
	Gas.Stop(); // packed up
	TestTrue(TEXT("80 s kept, not burnt out"), FMath::IsNearlyEqual(Gas.TimeLeft, 80.f) && !Gas.bBurning && !Gas.bBurntOut);
	Gas.Ignite(Gas.TimeLeft);
	TestTrue(TEXT("redeployed: empty after 80 s"), Gas.Step(80.f) && Gas.bBurntOut);
	// Flare: 1.8 m heat, 40 s (2 rounds), light 10 m, thrown 20 m standing / 15 m crouched.
	TestEqual(TEXT("flare radius 1.8 m"), Config.FlareHeatRadius, 180.f);
	TestEqual(TEXT("flare 40 s"), Config.FlareBurnSeconds, 40.f);
	FHeatBurnState Flare;
	Flare.Ignite(Config.FlareBurnSeconds);
	const float FlareRound = TacticalHeatRules::SecondsPerRound(Config.FlareBurnSeconds, Config.FlareTurnRounds);
	TestFalse(TEXT("flare round 1"), Flare.AdvanceRound(FlareRound));
	TestTrue(TEXT("flare round 2: spent"), Flare.AdvanceRound(FlareRound));
	TestEqual(TEXT("throw 20 m standing"), TacticalHeatRules::GetFlareThrowRange(Config, EOperativeStance::Standing), 2000.f);
	TestEqual(TEXT("throw 15 m crouched"), TacticalHeatRules::GetFlareThrowRange(Config, EOperativeStance::Crouching), 1500.f);
	TestTrue(TEXT("enemy at 10 m lit"), TacticalHeatRules::IsRevealedByFlare(Config, true, 1000.f));
	TestFalse(TEXT("enemy at 11 m dark"), TacticalHeatRules::IsRevealedByFlare(Config, true, 1100.f));
	TestFalse(TEXT("spent flare shows nothing"), TacticalHeatRules::IsRevealedByFlare(Config, false, 100.f));
	return true;
}

HEAT_TEST(FHeatPuddleTest, "FirePuddleTwoZones")
bool FHeatPuddleTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	TestTrue(TEXT("0 m: core"), TacticalHeatRules::ClassifyPuddleZone(Config, 0.f) == EFirePuddleZone::Core);
	TestTrue(TEXT("1.2 m: core (edge inclusive)"), TacticalHeatRules::ClassifyPuddleZone(Config, 120.f) == EFirePuddleZone::Core);
	TestTrue(TEXT("1.21 m: radiant ring"), TacticalHeatRules::ClassifyPuddleZone(Config, 121.f) == EFirePuddleZone::Radiant);
	TestTrue(TEXT("3.5 m: radiant ring"), TacticalHeatRules::ClassifyPuddleZone(Config, 350.f) == EFirePuddleZone::Radiant);
	TestTrue(TEXT("3.6 m: outside"), TacticalHeatRules::ClassifyPuddleZone(Config, 360.f) == EFirePuddleZone::Outside);
	TestEqual(TEXT("lifetime min 12 s"), TacticalHeatRules::GetPuddleLifetime(Config, 0.f), 12.f);
	TestEqual(TEXT("lifetime max 15 s"), TacticalHeatRules::GetPuddleLifetime(Config, 1.f), 15.f);
	// The ring warms at 12 %/s, faster than a plain heat source (8 %/s x fortitude boost).
	const FColdConfig Cold;
	FColdEnvironment Ring;
	Ring.bWarm = true;
	Ring.WarmthRateOverride = Config.PuddleWarmthRate;
	TestEqual(TEXT("ring: 12 %/s"), ColdRules::StepCold(Cold, 50.f, 1.f, Ring, EOperativeStance::Standing, 15.f), 38.f, 0.001f);
	Ring.WarmthRateOverride = 0.f;
	TestEqual(TEXT("plain source: 9.2 %/s"), ColdRules::StepCold(Cold, 50.f, 1.f, Ring, EOperativeStance::Standing, 15.f), 40.8f, 0.001f);
	// The ring thaws a frozen weapon (any heat source does).
	TestFalse(TEXT("thawed in the ring"), ColdRules::UpdateWeaponFrozen(Cold, true, 95.f, true));
	// One turn-based round burns it.
	FHeatBurnState Puddle;
	Puddle.Ignite(13.f);
	TestTrue(TEXT("1 round"), Puddle.AdvanceRound(TacticalHeatRules::SecondsPerRound(13.f, Config.PuddleTurnRounds)));
	return true;
}

HEAT_TEST(FHeatPauseTest, "TimersFrozenInPauseTurnBasedAndDialogue")
bool FHeatPauseTest::RunTest(const FString&)
{
	TestTrue(TEXT("real time: runs"), TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::RealTime, false));
	TestTrue(TEXT("exploration: runs"), TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::None, false));
	TestFalse(TEXT("tactical pause (incl. the free horde pause / flyover): frozen"), TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::TacticalPause, false));
	TestFalse(TEXT("turn-based: rounds only"), TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::TurnBased, false));
	TestFalse(TEXT("dialogue / cutscene: frozen"), TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::None, true));
	// A burn state that is not stepped keeps its time.
	FHeatBurnState Fire;
	Fire.Ignite(75.f);
	if (TacticalHeatRules::ShouldAdvanceRealTime(ECodexCombatMode::TacticalPause, false))
	{
		Fire.Step(30.f);
	}
	TestEqual(TEXT("75 s left after a pause"), Fire.TimeLeft, 75.f);
	return true;
}

HEAT_TEST(FHeatTuningJsonTest, "TuningJsonRoundTrip")
bool FHeatTuningJsonTest::RunTest(const FString&)
{
	FTacticalHeatConfig Changed;
	Changed.CampfireBurnSeconds = 90.f;
	Changed.StoveHeatRadius = 250.f;
	Changed.HeatPackMaxStack = 4;
	Changed.bBarrelLeavesPuddle = false;
	Changed.StartingLoadout[1].Stoves = 0;
	const TSharedRef<FJsonObject> Json = TacticalHeatTuning::ToJson(Changed);
	const FTacticalHeatConfig Back = TacticalHeatTuning::FromJson(*Json);
	TestEqual(TEXT("campfire burn"), Back.CampfireBurnSeconds, 90.f);
	TestEqual(TEXT("stove radius"), Back.StoveHeatRadius, 250.f);
	TestEqual(TEXT("stack"), Back.HeatPackMaxStack, 4);
	TestFalse(TEXT("barrel puddle off"), Back.bBarrelLeavesPuddle);
	TestEqual(TEXT("engineer without stove"), Back.StartingLoadout[1].Stoves, 0);
	// A missing key keeps the default.
	const FTacticalHeatConfig Empty = TacticalHeatTuning::FromJson(*MakeShared<FJsonObject>());
	TestEqual(TEXT("default flare radius"), Empty.FlareHeatRadius, 180.f);
	TestEqual(TEXT("default engineer stove"), TacticalHeatRules::GetLoadout(Empty, 1).Stoves, 1);
	TestEqual(TEXT("unknown role: empty"), TacticalHeatRules::GetLoadout(Empty, 9).HeatPacks, 0);
	return true;
}

#undef HEAT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
