#include "Misc/AutomationTest.h"
#include "Falls/FallRules.h"
#include "Falls/DropLinkNavigation.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

// Falls from heights, running drops and knockback falls (game designer items 6 / 10 / 11a, 2026-11-03; UE-only, no Godot reference;
// docs/systems/stances-and-movement.md "Falls and drops"): the fall damage (the ladder curve, one level free), the run-up off an edge,
// the drop link profile, the knockback throw, the marker text and the tuning file.

#define FALL_RULES_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Falls.Rules." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FallRulesTestLocal
{
	FallRules::FProfileSample FallSample(float Dist, bool bHit, float Drop)
	{
		FallRules::FProfileSample Out;
		Out.DistCm = Dist;
		Out.bHit = bHit;
		Out.DropCm = Drop;
		return Out;
	}

	/** Samples every 15 cm up to MaxDist: the floor function of the distance (bHit false = nothing below). */
	TArray<FallRules::FProfileSample> Profile(float MaxDist, TFunctionRef<FallRules::FProfileSample(float)> At)
	{
		TArray<FallRules::FProfileSample> Samples;
		for (float D = 15.f; D <= MaxDist; D += 15.f)
		{
			Samples.Add(At(D));
		}
		return Samples;
	}
}

FALL_RULES_TEST(FFallRulesDamageTest, "Damage")
bool FFallRulesDamageTest::RunTest(const FString&)
{
	using namespace FallRules;
	const FFallConfig Config;
	TestEqual(TEXT("one level (3 m) is free"), FallDamage(300.f, Config), 0.f);
	TestEqual(TEXT("3.2 m is still free"), FallDamage(320.f, Config), 0.f);
	TestTrue(TEXT("two levels (6 m): (600 - 320) x 0.1 = 28"), FMath::IsNearlyEqual(FallDamage(600.f, Config), 28.f, 0.01f));
	TestEqual(TEXT("just under 9 m: the ladder curve's cap 40"), FallDamage(890.f, Config), 40.f);
	FFallConfig NoBands = Config;
	NoBands.HeavyHeightCm = 0.f;
	NoBands.LethalHeightCm = 0.f;
	TestEqual(TEXT("with both bands off: 20 m = the curve's cap"), FallDamage(2000.f, NoBands), 40.f);
	FFallConfig Lethal = NoBands;
	Lethal.LethalHeightCm = 1000.f;
	TestEqual(TEXT("a lethal height (when set) kills"), FallDamage(1000.f, Lethal), LethalDamage);
	TestEqual(TEXT("... below it the curve"), FallDamage(900.f, Lethal), 40.f);
	TestFalse(TEXT("a 3 m drop does not hurt (UNavArea_Drop)"), IsHurtingDrop(300.f, Config));
	TestTrue(TEXT("a 6 m drop hurts (UNavArea_DropHurt)"), IsHurtingDrop(600.f, Config));
	return true;
}

FALL_RULES_TEST(FFallRulesBandsTest, "HeightBands")
bool FFallRulesBandsTest::RunTest(const FString&)
{
	// User decision 2026-11-03: below 9 m the curve, 9-12 m a heavy wound (big damage, knocked down, survivable at full health), 12 m+ lethal.
	using namespace FallRules;
	const FFallConfig Config;
	TestEqual(TEXT("default heavy band from 9 m"), Config.HeavyHeightCm, 900.f);
	TestEqual(TEXT("default lethal from 12 m"), Config.LethalHeightCm, 1200.f);
	const FFallVerdict Free = JudgeFall(300.f, Config);
	TestTrue(TEXT("3 m: unhurt, no knockdown"), Free.Outcome == EFallOutcome::Unhurt && Free.Damage == 0.f && !Free.bKnockdown);
	const FFallVerdict Six = JudgeFall(600.f, Config);
	TestTrue(TEXT("6 m: hurt by the curve (28), no knockdown"), Six.Outcome == EFallOutcome::Hurt && FMath::IsNearlyEqual(Six.Damage, 28.f, 0.01f) && !Six.bKnockdown);
	const FFallVerdict Below = JudgeFall(899.f, Config);
	TestTrue(TEXT("8.99 m: still the curve (40), no knockdown"), Below.Outcome == EFallOutcome::Hurt && Below.Damage == 40.f && !Below.bKnockdown);
	const FFallVerdict Nine = JudgeFall(900.f, Config);
	TestTrue(TEXT("9 m: a heavy wound - heavy_min 60 and the knockdown"), Nine.Outcome == EFallOutcome::HeavyWound && Nine.Damage == 60.f && Nine.bKnockdown);
	const FFallVerdict TenHalf = JudgeFall(1050.f, Config);
	TestTrue(TEXT("10.5 m: halfway, 70"), TenHalf.Outcome == EFallOutcome::HeavyWound && FMath::IsNearlyEqual(TenHalf.Damage, 70.f, 0.01f) && TenHalf.bKnockdown);
	const FFallVerdict Under12 = JudgeFall(1199.f, Config);
	TestTrue(TEXT("11.99 m: just under heavy_max 80, survivable at 100 HP"), Under12.Outcome == EFallOutcome::HeavyWound && Under12.Damage < 80.f
		&& Under12.Damage > 79.9f && Under12.Damage < 100.f);
	const FFallVerdict Twelve = JudgeFall(1200.f, Config);
	TestTrue(TEXT("12 m: lethal"), Twelve.Outcome == EFallOutcome::Lethal && Twelve.Damage == LethalDamage);
	TestTrue(TEXT("30 m: lethal"), JudgeFall(3000.f, Config).Outcome == EFallOutcome::Lethal);
	// Anyone at full health survives a heavy wound (damage.heavy_max_health_fraction 0.85).
	TestEqual(TEXT("an operative (100 HP) at 10.5 m: 70 (under the cap 85)"), JudgeFallForBody(1050.f, 100.f, Config).Damage, 70.f);
	TestTrue(TEXT("a Frostbitten (60 HP) at 10.5 m: capped at 51"), FMath::IsNearlyEqual(JudgeFallForBody(1050.f, 60.f, Config).Damage, 51.f, 0.01f)
		&& JudgeFallForBody(1050.f, 60.f, Config).bKnockdown);
	TestTrue(TEXT("a hound (45 HP) at 11.9 m: capped at 38.25"), FMath::IsNearlyEqual(JudgeFallForBody(1190.f, 45.f, Config).Damage, 38.25f, 0.01f));
	TestEqual(TEXT("the cap is only for heavy wounds: 6 m on a 20 HP body stays 28"), JudgeFallForBody(600.f, 20.f, Config).Damage, 28.f);
	TestEqual(TEXT("... and never for a lethal fall"), JudgeFallForBody(1300.f, 60.f, Config).Damage, LethalDamage);
	FFallConfig NoHeavy = Config;
	NoHeavy.HeavyHeightCm = 0.f;
	TestTrue(TEXT("heavy band off: 10 m is the curve's cap, no knockdown"), JudgeFall(1000.f, NoHeavy).Outcome == EFallOutcome::Hurt && !JudgeFall(1000.f, NoHeavy).bKnockdown);
	FFallConfig NoLethal = Config;
	NoLethal.LethalHeightCm = 0.f;
	const FFallVerdict Open = JudgeFall(2000.f, NoLethal);
	TestTrue(TEXT("lethal off: the heavy band runs on at heavy_min"), Open.Outcome == EFallOutcome::HeavyWound && Open.Damage == 60.f && Open.bKnockdown);
	return true;
}

FALL_RULES_TEST(FFallRulesEnemyDropsTest, "EnemyDropUse")
bool FFallRulesEnemyDropsTest::RunTest(const FString&)
{
	// enemy_drops.<archetype> in EEnemyArchetype order: Base, FrostHound, Spitter, Brute, Frostbitten, Cutter, CryoDrone, Marksman.
	using namespace FallRules;
	const FFallConfig Config;
	TestTrue(TEXT("Base zombie: free drops"), EnemyDropUse(0, Config) == EEnemyDropUse::Free);
	TestTrue(TEXT("Frost Hound: jumper"), EnemyDropUse(1, Config) == EEnemyDropUse::Jumper);
	TestTrue(TEXT("Spitter: never"), EnemyDropUse(2, Config) == EEnemyDropUse::None);
	TestTrue(TEXT("Brute: never (heavy, no jump)"), EnemyDropUse(3, Config) == EEnemyDropUse::None);
	TestTrue(TEXT("Frostbitten (zombies, PMC fighters): free drops"), EnemyDropUse(4, Config) == EEnemyDropUse::Free);
	TestTrue(TEXT("Cutter: jumper"), EnemyDropUse(5, Config) == EEnemyDropUse::Jumper);
	TestTrue(TEXT("Cryo Drone: never"), EnemyDropUse(6, Config) == EEnemyDropUse::None);
	TestTrue(TEXT("Marksman: free drops"), EnemyDropUse(7, Config) == EEnemyDropUse::Free);
	TestTrue(TEXT("an unknown archetype: never"), EnemyDropUse(8, Config) == EEnemyDropUse::None && EnemyDropUse(-1, Config) == EEnemyDropUse::None);
	FFallConfig Tuned = Config;
	Tuned.EnemyDropsBrute = 2.f;
	TestTrue(TEXT("data-driven: the Brute set to 2 jumps"), EnemyDropUse(3, Tuned) == EEnemyDropUse::Jumper);
	FFallConfig FromJson;
	TestTrue(TEXT("the JSON key sets it"), ApplyJson(TEXT("{\"params\": {\"enemy_drops.cutter\": 0}}"), FromJson) && EnemyDropUse(5, FromJson) == EEnemyDropUse::None);
	return true;
}

FALL_RULES_TEST(FFallRulesEnemyFiltersTest, "EnemyFilters")
bool FFallRulesEnemyFiltersTest::RunTest(const FString&)
{
	// The enemy's nav filter per drop use and ladder climbing (DropLinkNavigation::EnemyFilterClass, 2026-11-03).
	using namespace FallRules;
	TestTrue(TEXT("never + no ladders: NoVault"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::None, false) == UNavFilter_NoVault::StaticClass());
	TestTrue(TEXT("never + ladders: NoVaultLadder"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::None, true) == UNavFilter_NoVaultLadder::StaticClass());
	TestTrue(TEXT("free drops"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::Free, false) == UNavFilter_EnemyDropFree::StaticClass());
	TestTrue(TEXT("free drops + ladders"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::Free, true) == UNavFilter_EnemyDropFreeLadder::StaticClass());
	TestTrue(TEXT("jumper"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::Jumper, false) == UNavFilter_EnemyDropJumper::StaticClass());
	TestTrue(TEXT("jumper + ladders"), DropLinkNavigation::EnemyFilterClass(EEnemyDropUse::Jumper, true) == UNavFilter_EnemyDropJumperLadder::StaticClass());
	auto Excludes = [](const UClass* Filter, const UClass* Area)
	{
		const UNavigationQueryFilter* Defaults = Filter ? Filter->GetDefaultObject<UNavigationQueryFilter>() : nullptr;
		return Defaults && Defaults->Areas.ContainsByPredicate([Area](const FNavigationFilterArea& Each) { return Each.AreaClass == Area && Each.bIsExcluded; });
	};
	TestTrue(TEXT("the base filter closes the drops"), Excludes(UNavFilter_NoVault::StaticClass(), UNavArea_Drop::StaticClass())
		&& Excludes(UNavFilter_NoVault::StaticClass(), UNavArea_DropHurt::StaticClass()));
	for (const UClass* Open : { UNavFilter_EnemyDropFree::StaticClass(), UNavFilter_EnemyDropFreeLadder::StaticClass(), UNavFilter_EnemyDropJumper::StaticClass(),
		UNavFilter_EnemyDropJumperLadder::StaticClass() })
	{
		TestFalse(FString::Printf(TEXT("%s opens the drops"), *Open->GetName()), Excludes(Open, UNavArea_Drop::StaticClass()) || Excludes(Open, UNavArea_DropHurt::StaticClass()));
		TestTrue(FString::Printf(TEXT("%s keeps the vault areas closed"), *Open->GetName()), Excludes(Open, UNavArea_Vault::StaticClass()));
	}
	TestTrue(TEXT("the ladder variants keep the ladder links open"), !Excludes(UNavFilter_EnemyDropFreeLadder::StaticClass(), UNavArea_Ladder::StaticClass())
		&& Excludes(UNavFilter_EnemyDropFree::StaticClass(), UNavArea_Ladder::StaticClass()));
	return true;
}

FALL_RULES_TEST(FFallRulesAirGravityTest, "AirGravity")
bool FFallRulesAirGravityTest::RunTest(const FString&)
{
	// User 2026-11-03: the falls looked like gliding - 1.5x faster (air.gravity_scale 2.25).
	using namespace FallRules;
	const FFallConfig Config;
	TestEqual(TEXT("default scale 2.25"), Config.AirGravityScale, 2.25f);
	const float G = AirGravity(-980.f, Config);
	TestTrue(TEXT("the air gravity: 980 x 2.25 = 2205 (sign-free)"), FMath::IsNearlyEqual(G, 2205.f, 0.1f));
	TestTrue(TEXT("a 3 m fall takes 1 / 1.5 of the time"), FMath::IsNearlyEqual(FallSeconds(300.f, G), FallSeconds(300.f, 980.f) / 1.5f, 0.001f));
	TestTrue(TEXT("a 10.5 m fall too"), FMath::IsNearlyEqual(FallSeconds(1050.f, G), FallSeconds(1050.f, 980.f) / 1.5f, 0.001f));
	TestTrue(TEXT("the knockback throw x 1.5"), FMath::IsNearlyEqual(KnockbackLaunchScale(Config), 1.5f, 0.001f));
	TestTrue(TEXT("... the same arc: flight at 1.5x the speeds under 2.25 g = the unscaled flight"),
		FMath::IsNearlyEqual(KnockbackFlightCm(420.f * 1.5f, 200.f * 1.5f, G), KnockbackFlightCm(420.f, 200.f, 980.f), 0.1f));
	const FTakeoffPlan Plan = PlanTakeoff(220.f, 725.f, 40.f, 290.f, 1190.f, 300.f, G, Config);
	TestTrue(TEXT("the ruin -> annex gap under the air gravity: a run-up of (290 + 40 - 40) / 0.52 x 1.1 = 612 cm/s reaches"), Plan.bReaches
		&& FMath::IsNearlyEqual(Plan.SpeedCms, 611.6f, 1.5f));
	FFallConfig Floor = Config;
	Floor.AirGravityScale = 0.f;
	TestTrue(TEXT("a broken scale is held at 0.1"), FMath::IsNearlyEqual(AirGravity(980.f, Floor), 98.f, 0.01f));
	return true;
}

FALL_RULES_TEST(FFallRulesAnimPhaseTest, "AnimPhase")
bool FFallRulesAnimPhaseTest::RunTest(const FString&)
{
	// The operative ABP's Falling / Landing states (UOperativeAnimInstance::bFallAirborne / bFallLanding / bFallGrounded).
	using namespace FallRules;
	const FFallConfig Config;
	TestTrue(TEXT("standing: grounded"), FallAnimPhase(false, 0.f, -1.f, Config) == EFallAnimPhase::Grounded);
	TestTrue(TEXT("a step down (0.1 s falling): still grounded"), FallAnimPhase(false, 0.1f, -1.f, Config) == EFallAnimPhase::Grounded);
	TestTrue(TEXT("falling past anim.fall_min_s: falling"), FallAnimPhase(false, 0.3f, -1.f, Config) == EFallAnimPhase::Falling);
	TestTrue(TEXT("a drop link's flight: falling at once"), FallAnimPhase(true, 0.f, -1.f, Config) == EFallAnimPhase::Falling);
	TestTrue(TEXT("just landed: landing"), FallAnimPhase(false, 0.f, 0.1f, Config) == EFallAnimPhase::Landing);
	TestTrue(TEXT("anim.land_s after the landing: grounded"), FallAnimPhase(false, 0.f, Config.AnimLandSeconds + 0.01f, Config) == EFallAnimPhase::Grounded);
	TestTrue(TEXT("falling again while landing: falling"), FallAnimPhase(true, 0.f, 0.1f, Config) == EFallAnimPhase::Falling);
	return true;
}

FALL_RULES_TEST(FFallRulesTakeoffTest, "Takeoff")
bool FFallRulesTakeoffTest::RunTest(const FString&)
{
	using namespace FallRules;
	const FFallConfig Config;
	TestTrue(TEXT("3 m from rest: 0.78 s"), FMath::IsNearlyEqual(FallSeconds(300.f, 980.f), 0.7825f, 0.001f));
	TestEqual(TEXT("no drop: no time"), FallSeconds(0.f, 980.f), 0.f);

	// The ruin's roof (+2) to the annex roof (+1) across the 2.5 m gap: edge 40 cm out, the lower roof 290..1190 cm out, 3 m down.
	const FTakeoffPlan Walking = PlanTakeoff(220.f, 725.f, 40.f, 290.f, 1190.f, 300.f, 980.f, Config);
	TestTrue(TEXT("walking up to the edge he speeds up to what the gap needs and reaches the lower roof"), Walking.bReaches && !Walking.bTooSlow);
	TestTrue(TEXT("... the needed speed x 1.1: (290 + 40 - 40) / 0.78 x 1.1 = 408 cm/s"), FMath::IsNearlyEqual(Walking.SpeedCms, 407.7f, 1.f));
	const FTakeoffPlan Sprinting = PlanTakeoff(725.f, 725.f, 40.f, 290.f, 1190.f, 300.f, 980.f, Config);
	TestEqual(TEXT("sprinting he keeps his momentum (the lower roof is deep enough)"), Sprinting.SpeedCms, 725.f);
	TestTrue(TEXT("... and carries on 5.7 m out"), Sprinting.bReaches && FMath::IsNearlyEqual(Sprinting.LandingCm, 40.f + 725.f * 0.7825f, 1.f));
	const FTakeoffPlan Narrow = PlanTakeoff(725.f, 725.f, 40.f, 290.f, 450.f, 300.f, 980.f, Config);
	TestTrue(TEXT("a narrow lower roof: his momentum is checked so he lands on it"), Narrow.bReaches && Narrow.SpeedCms < 725.f && Narrow.LandingCm <= 430.f);
	const FTakeoffPlan Slow = PlanTakeoff(220.f, 150.f, 40.f, 290.f, 1190.f, 300.f, 980.f, Config);
	TestTrue(TEXT("too slow (his run-up capped at 150 cm/s): he falls short"), Slow.bTooSlow && !Slow.bReaches);
	TestEqual(TEXT("... at his cap"), Slow.SpeedCms, 150.f);
	const FTakeoffPlan Bridge = PlanTakeoff(220.f, 725.f, 40.f, 40.f, 0.f, 300.f, 980.f, Config);
	TestTrue(TEXT("a bridge over the cut (the floor right below, open): a step off at his walking speed"), Bridge.bReaches && Bridge.SpeedCms == 220.f);
	const FTakeoffPlan Standing = PlanTakeoff(0.f, 725.f, 40.f, 40.f, 0.f, 300.f, 980.f, Config);
	TestEqual(TEXT("from a standstill at least the step-off speed"), Standing.SpeedCms, Config.TakeoffMinSpeedCms);
	return true;
}

FALL_RULES_TEST(FFallRulesProfileTest, "Profile")
bool FFallRulesProfileTest::RunTest(const FString&)
{
	using namespace FallRules;
	using namespace FallRulesTestLocal;
	const FFallConfig Config;
	// A bridge over the -3 m cut: the slab ends 40 cm out, the cut floor right below, 6 m wide.
	const FDropProfile Bridge = AnalyseProfile(Profile(700.f, [](float D) { return D < 40.f ? FallSample(D, true, 0.f) : D < 640.f ? FallSample(D, true, 300.f) : FallSample(D, true, 0.f); }), Config);
	TestTrue(TEXT("bridge: ok"), Bridge.IsOk());
	TestEqual(TEXT("bridge: the edge at 45 cm (the first probe off the slab)"), Bridge.EdgeCm, 45.f);
	TestEqual(TEXT("bridge: the lower surface starts at the edge"), Bridge.NearCm, 45.f);
	TestEqual(TEXT("bridge: 3 m down"), Bridge.DropCm, 300.f);
	TestEqual(TEXT("bridge: the far wall of the cut ends it"), Bridge.FarCm, 645.f);
	// Roof to a lower roof across a 2.5 m gap (the ground 6 m below in the gap).
	const FDropProfile Roofs = AnalyseProfile(Profile(900.f, [](float D)
	{
		return D < 40.f ? FallSample(D, true, 0.f) : D < 290.f ? FallSample(D, true, 600.f) : FallSample(D, true, 300.f);
	}), Config);
	TestTrue(TEXT("roof to roof: ok, the lower roof (3 m) is the landing, not the ground (6 m) in the gap"), Roofs.IsOk() && Roofs.DropCm == 300.f && Roofs.NearCm == 300.f);
	TestTrue(TEXT("... open beyond the samples"), Roofs.FarCm <= 0.f);
	// A wall right at the edge.
	TestEqual(TEXT("a wall: blocked"), AnalyseProfile(Profile(300.f, [](float D) { return D < 40.f ? FallSample(D, true, 0.f) : FallSample(D, true, -220.f); }), Config).Verdict,
		EProfileVerdict::Blocked);
	TestEqual(TEXT("a 1 m step down: no link (walkable / not a drop)"),
		AnalyseProfile(Profile(300.f, [](float D) { return D < 40.f ? FallSample(D, true, 0.f) : FallSample(D, true, 100.f); }), Config).Verdict, EProfileVerdict::LowStep);
	TestEqual(TEXT("the lower roof 3 m beyond the edge: too wide"), AnalyseProfile(Profile(900.f, [](float D)
	{
		return D < 40.f ? FallSample(D, true, 0.f) : D < 340.f ? FallSample(D, false, 0.f) : FallSample(D, true, 300.f);
	}), Config).Verdict, EProfileVerdict::GapTooWide);
	TestEqual(TEXT("nothing below within 6.5 m (a 9 m hall roof): no lower surface"),
		AnalyseProfile(Profile(300.f, [](float D) { return D < 40.f ? FallSample(D, true, 0.f) : FallSample(D, true, 900.f); }), Config).Verdict, EProfileVerdict::NoLowerSurface);
	TestEqual(TEXT("a 60 cm wide ledge below: too shallow"), AnalyseProfile(Profile(600.f, [](float D)
	{
		return D < 40.f ? FallSample(D, true, 0.f) : D < 100.f ? FallSample(D, true, 300.f) : FallSample(D, false, 0.f);
	}), Config).Verdict, EProfileVerdict::TooShallow);
	TestEqual(TEXT("the floor runs on past 120 cm: no edge"), AnalyseProfile(Profile(300.f, [](float D) { return FallSample(D, true, 5.f); }), Config).Verdict,
		EProfileVerdict::NoEdge);
	return true;
}

FALL_RULES_TEST(FFallRulesSamplingTest, "Sampling")
bool FFallRulesSamplingTest::RunTest(const FString&)
{
	using namespace FallRules;
	const TArray<float> Three = EdgeSampleFractions(450.f, 150.f);
	TestEqual(TEXT("4.5 m of edge: three links"), Three.Num(), 3);
	TestTrue(TEXT("... at 1/6, 1/2, 5/6"), Three.Num() == 3 && FMath::IsNearlyEqual(Three[0], 1.f / 6.f) && FMath::IsNearlyEqual(Three[1], 0.5f));
	TestEqual(TEXT("a short piece: one at its middle"), EdgeSampleFractions(60.f, 150.f).Num(), 1);
	TestEqual(TEXT("nothing: none"), EdgeSampleFractions(0.f, 150.f).Num(), 0);
	const TArray<FVector> Kept = { FVector(0.f, 0.f, 300.f) };
	const TArray<float> KeptZ = { 0.f };
	TestTrue(TEXT("a start 1 m from a kept one, same levels: duplicate"), IsDuplicateStart(FVector(100.f, 0.f, 300.f), 0.f, Kept, KeptZ, 150.f));
	TestFalse(TEXT("1.3 m away: a new link"), IsDuplicateStart(FVector(130.f, 0.f, 300.f), 0.f, Kept, KeptZ, 150.f));
	TestFalse(TEXT("same spot, another lower level: a new link"), IsDuplicateStart(FVector(50.f, 0.f, 300.f), -300.f, Kept, KeptZ, 150.f));
	TestTrue(TEXT("two landings 1.5 m apart conflict (one at a time)"), LandingsConflict(FVector(0.f), FVector(150.f, 0.f, 0.f), 220.f));
	TestFalse(TEXT("3 m apart they do not"), LandingsConflict(FVector(0.f), FVector(300.f, 0.f, 0.f), 220.f));
	TestFalse(TEXT("another level does not"), LandingsConflict(FVector(0.f), FVector(50.f, 0.f, 300.f), 220.f));
	return true;
}

FALL_RULES_TEST(FFallRulesKnockbackTest, "Knockback")
bool FFallRulesKnockbackTest::RunTest(const FString&)
{
	using namespace FallRules;
	const FFallConfig Config;
	const FVector Forward(1.f, 0.f, 0.f);
	TestTrue(TEXT("away from the blow's source"),
		KnockbackDirection(EKnockdownDirection::Back, Forward, true, FVector(0.f), FVector(0.f, -500.f, 0.f)).Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	TestTrue(TEXT("no source, on the back: backwards"), KnockbackDirection(EKnockdownDirection::Back, Forward, false, FVector(0.f), FVector(0.f)).Equals(-Forward, 0.001f));
	TestTrue(TEXT("no source, on the face: forwards"), KnockbackDirection(EKnockdownDirection::Front, Forward, false, FVector(0.f), FVector(0.f)).Equals(Forward, 0.001f));
	TestTrue(TEXT("the throw carries 1.7 m (420 cm/s, 200 up)"), FMath::IsNearlyEqual(KnockbackFlightCm(420.f, 200.f, 980.f), 171.4f, 0.1f));
	TestTrue(TEXT("an open roof edge 1 m behind him, 6 m down: over it"), KnockbackGoesOver(100.f, false, 600.f, 35.f, 980.f, Config));
	TestFalse(TEXT("a waist-high parapet: not over"), KnockbackGoesOver(100.f, true, 600.f, 35.f, 980.f, Config));
	TestFalse(TEXT("the edge 1.3 m away: out of reach"), KnockbackGoesOver(130.f, false, 600.f, 35.f, 980.f, Config));
	TestFalse(TEXT("a 1 m step: no fall"), KnockbackGoesOver(60.f, false, 100.f, 35.f, 980.f, Config));
	return true;
}

FALL_RULES_TEST(FFallRulesMarkerTest, "MarkerLabel")
bool FFallRulesMarkerTest::RunTest(const FString&)
{
	using namespace FallRules;
	TestEqual(TEXT("a free drop"), DropMarkerLabel(300.f, 0.f, false), FString(TEXT("DROP 3.0 m")));
	TestEqual(TEXT("a drop that hurts"), DropMarkerLabel(600.f, 28.f, false), FString(TEXT("DROP 6.0 m  -28 HP")));
	TestEqual(TEXT("a lethal drop"), DropMarkerLabel(1200.f, LethalDamage, false), FString(TEXT("DROP 12.0 m  LETHAL")));
	TestEqual(TEXT("a run-up that falls short"), DropMarkerLabel(300.f, 0.f, true), FString(TEXT("DROP 3.0 m  TOO FAR")));
	return true;
}

FALL_RULES_TEST(FFallRulesTuningTest, "TuningFileMirrorsDefaults")
bool FFallRulesTuningTest::RunTest(const FString&)
{
	using namespace FallRules;
	FString Text;
	if (!TestTrue(TEXT("falls.json readable"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())))
	{
		return false;
	}
	FFallConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("the file parses"), ApplyJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys in the file"), Unknown.Num(), 0);
	const FFallConfig Defaults;
	for (const FString& Key : KnownKeys())
	{
		float File = -1.f;
		float Code = -2.f;
		TestTrue(FString::Printf(TEXT("%s: in the file"), *Key), Text.Contains(FString::Printf(TEXT("\"%s\""), *Key)));
		GetValue(FromFile, Key, File);
		GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code);
	}
	FFallConfig Patched;
	TArray<FString> PatchUnknown;
	TestTrue(TEXT("a partial file applies"), ApplyJson(TEXT("{\"params\": {\"damage.safe_height_cm\": 250, \"nope\": 1}}"), Patched, &PatchUnknown));
	TestEqual(TEXT("... its key"), Patched.SafeHeightCm, 250.f);
	TestEqual(TEXT("... the rest keeps the defaults"), Patched.MaxDamage, Defaults.MaxDamage);
	TestEqual(TEXT("... the unknown key is reported"), PatchUnknown.Num(), 1);
	FFallConfig Broken;
	TestFalse(TEXT("broken JSON: false"), ApplyJson(TEXT("{"), Broken));
	TestFalse(TEXT("no params: false"), ApplyJson(TEXT("{\"x\": 1}"), Broken));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
