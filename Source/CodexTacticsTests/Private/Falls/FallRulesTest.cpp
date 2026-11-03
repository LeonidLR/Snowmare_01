#include "Misc/AutomationTest.h"
#include "Falls/FallRules.h"
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
	TestEqual(TEXT("three levels (9 m): the ladder curve's cap 40"), FallDamage(900.f, Config), 40.f);
	TestEqual(TEXT("no lethal height by default: 20 m = the cap"), FallDamage(2000.f, Config), 40.f);
	FFallConfig Lethal = Config;
	Lethal.LethalHeightCm = 1000.f;
	TestEqual(TEXT("a lethal height (when set) kills"), FallDamage(1000.f, Lethal), LethalDamage);
	TestEqual(TEXT("... below it the curve"), FallDamage(900.f, Lethal), 40.f);
	TestFalse(TEXT("a 3 m drop does not hurt (UNavArea_Drop)"), IsHurtingDrop(300.f, Config));
	TestTrue(TEXT("a 6 m drop hurts (UNavArea_DropHurt)"), IsHurtingDrop(600.f, Config));
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
