#include "Misc/AutomationTest.h"
#include "Combat/SuppressionRules.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

// Suppressive fire rules (game designer decisions 2026-10-26; UE-only, no Godot reference; docs/design/SUPPRESSION.md): impacts and
// their pressure by weapon, decay, tiers with hysteresis, the fire effects on both sides, the SUPPRESS order's cadence / stop.

#define SUPPRESSION_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Suppression." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SUPPRESSION_TEST(FSuppressionImpactTest, "ImpactClassification")
bool FSuppressionImpactTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	const FVector Start(0.f, 0.f, 100.f);
	const FVector End(2000.f, 0.f, 100.f);
	float Distance = 0.f;
	TestEqual(TEXT("a hit is a hit wherever he stands"),
		ClassifyImpact(Config, ESuppressionWeapon::SemiAuto, Start, End, false, true, FVector(5000.f, 5000.f, 0.f), Distance), ESuppressionImpact::Hit);
	TestEqual(TEXT("a round 1.5 m past him: near miss"),
		ClassifyImpact(Config, ESuppressionWeapon::SemiAuto, Start, End, false, false, FVector(1000.f, 150.f, 100.f), Distance), ESuppressionImpact::NearMiss);
	TestTrue(TEXT("... rated by its pass distance"), FMath::IsNearlyEqual(Distance, 150.f, 0.5f));
	TestEqual(TEXT("2.5 m past him: nothing"),
		ClassifyImpact(Config, ESuppressionWeapon::SemiAuto, Start, End, false, false, FVector(1000.f, 250.f, 100.f), Distance), ESuppressionImpact::None);
	TestEqual(TEXT("a cannon shell 4 m past him: near miss (explosive radius)"),
		ClassifyImpact(Config, ESuppressionWeapon::Cannon, Start, End, false, false, FVector(1000.f, 400.f, 100.f), Distance), ESuppressionImpact::NearMiss);
	// He hides 2.2 m behind the wall the round struck (the segment ends at the wall, far from his line).
	TestEqual(TEXT("a round into his cover 2.2 m off him: cover strike"),
		ClassifyImpact(Config, ESuppressionWeapon::FullAuto, Start, End, true, false, FVector(2000.f, 220.f, 100.f), Distance), ESuppressionImpact::CoverStrike);
	TestEqual(TEXT("the same round with no cover struck: nothing (2.2 m is outside the near-miss radius)"),
		ClassifyImpact(Config, ESuppressionWeapon::FullAuto, Start, End, false, false, FVector(2000.f, 220.f, 100.f), Distance), ESuppressionImpact::None);
	TestEqual(TEXT("a round into cover 3 m from him: nothing"),
		ClassifyImpact(Config, ESuppressionWeapon::FullAuto, Start, End, true, false, FVector(2000.f, 300.f, 100.f), Distance), ESuppressionImpact::None);
	TestEqual(TEXT("the round passes 30 cm from him and strikes cover by him: the stronger near miss"),
		ClassifyImpact(Config, ESuppressionWeapon::FullAuto, Start, End, true, false, FVector(1990.f, 30.f, 100.f), Distance), ESuppressionImpact::NearMiss);
	return true;
}

SUPPRESSION_TEST(FSuppressionPressureTest, "PressureByWeapon")
bool FSuppressionPressureTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	const float SemiNear = ImpactPressure(Config, ESuppressionWeapon::SemiAuto, ESuppressionImpact::NearMiss, 0.f);
	const float AutoNear = ImpactPressure(Config, ESuppressionWeapon::FullAuto, ESuppressionImpact::NearMiss, 0.f);
	const float MgNear = ImpactPressure(Config, ESuppressionWeapon::MachineGun, ESuppressionImpact::NearMiss, 0.f);
	TestTrue(TEXT("full-auto weighs more than a single shot, the MG more still"), SemiNear < AutoNear && AutoNear < MgNear);
	TestEqual(TEXT("point-blank near miss = the weight"), SemiNear, Config.WeightSemiAuto);
	TestTrue(TEXT("a near miss at the radius edge counts the edge factor"),
		FMath::IsNearlyEqual(ImpactPressure(Config, ESuppressionWeapon::SemiAuto, ESuppressionImpact::NearMiss, Config.NearMissRadiusCm),
			Config.WeightSemiAuto * Config.NearMissEdgeFactor));
	TestEqual(TEXT("outside the radius: 0"), ImpactPressure(Config, ESuppressionWeapon::SemiAuto, ESuppressionImpact::NearMiss, Config.NearMissRadiusCm + 1.f), 0.f);
	TestTrue(TEXT("a hit counts more than a near miss"),
		ImpactPressure(Config, ESuppressionWeapon::Sniper, ESuppressionImpact::Hit, 0.f) > ImpactPressure(Config, ESuppressionWeapon::Sniper, ESuppressionImpact::NearMiss, 0.f));
	TestTrue(TEXT("a strike on his cover counts"), ImpactPressure(Config, ESuppressionWeapon::FullAuto, ESuppressionImpact::CoverStrike, 100.f) > 0.f);
	TestTrue(TEXT("the weapon's own scale and a deliberate intent multiply it"),
		FMath::IsNearlyEqual(ImpactPressure(Config, ESuppressionWeapon::Acid, ESuppressionImpact::Hit, 0.f, 2.f),
			2.f * ImpactPressure(Config, ESuppressionWeapon::Acid, ESuppressionImpact::Hit, 0.f)));
	TestEqual(TEXT("None: 0"), ImpactPressure(Config, ESuppressionWeapon::Cannon, ESuppressionImpact::None, 0.f), 0.f);
	return true;
}

SUPPRESSION_TEST(FSuppressionTierTest, "TiersAndHysteresis")
bool FSuppressionTierTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	TestEqual(TEXT("defaults: Suppressed at 0.5"), Config.SuppressedEnter, 0.5f);
	TestEqual(TEXT("defaults: Pinned at 0.85"), Config.PinnedEnter, 0.85f);
	TestEqual(TEXT("0.49 from free: free"), ResolveTier(Config, 0.49f, ESuppressionTier::None), ESuppressionTier::None);
	TestEqual(TEXT("0.5: suppressed"), ResolveTier(Config, 0.5f, ESuppressionTier::None), ESuppressionTier::Suppressed);
	TestEqual(TEXT("0.45 while suppressed: stays (exit 0.4)"), ResolveTier(Config, 0.45f, ESuppressionTier::Suppressed), ESuppressionTier::Suppressed);
	TestEqual(TEXT("0.39 while suppressed: free"), ResolveTier(Config, 0.39f, ESuppressionTier::Suppressed), ESuppressionTier::None);
	TestEqual(TEXT("0.85: pinned"), ResolveTier(Config, 0.85f, ESuppressionTier::Suppressed), ESuppressionTier::Pinned);
	TestEqual(TEXT("0.75 while pinned: stays (exit 0.7)"), ResolveTier(Config, 0.75f, ESuppressionTier::Pinned), ESuppressionTier::Pinned);
	TestEqual(TEXT("0.6 while pinned: back to suppressed"), ResolveTier(Config, 0.6f, ESuppressionTier::Pinned), ESuppressionTier::Suppressed);
	TestEqual(TEXT("0.75 from free: suppressed, not pinned"), ResolveTier(Config, 0.75f, ESuppressionTier::None), ESuppressionTier::Suppressed);
	return true;
}

SUPPRESSION_TEST(FSuppressionDecayTest, "AccumulationAndDecay")
bool FSuppressionDecayTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	const float Dt = 1.f / 60.f;
	// One rifleman's normal fire (a semi-auto round every 0.65 s, every one a point-blank near miss) never suppresses alone.
	{
		FSuppressionState State;
		float Clock = 0.f;
		float NextShot = 0.f;
		float Peak = 0.f;
		while (Clock < 30.f)
		{
			if (Clock >= NextShot)
			{
				AddPressure(Config, State, ImpactPressure(Config, ESuppressionWeapon::SemiAuto, ESuppressionImpact::NearMiss, 0.f));
				NextShot += 0.65f;
			}
			Step(Config, State, Dt);
			Clock += Dt;
			Peak = FMath::Max(Peak, State.Level);
		}
		TestTrue(FString::Printf(TEXT("one rifleman's normal fire peaks at %.2f, below Suppressed"), Peak), Peak < Config.SuppressedEnter);
	}
	// The SUPPRESS order: full-auto bursts of 6 at 0.1 s with a 0.6 s pause, every round a strike on his cover 1 m from him.
	{
		FSuppressionState State;
		float Clock = 0.f;
		float NextShot = 0.f;
		int32 Burst = 0;
		float SuppressedAt = -1.f;
		float PinnedAt = -1.f;
		while (Clock < 10.f)
		{
			if (Clock >= NextShot)
			{
				AddPressure(Config, State, ImpactPressure(Config, ESuppressionWeapon::FullAuto, ESuppressionImpact::CoverStrike, 100.f));
				NextShot += NextOrderRoundDelay(Config, 0.65f, 0.1f, Burst);
			}
			Step(Config, State, Dt);
			Clock += Dt;
			if (SuppressedAt < 0.f && State.Tier != ESuppressionTier::None)
			{
				SuppressedAt = Clock;
			}
			if (PinnedAt < 0.f && State.Tier == ESuppressionTier::Pinned)
			{
				PinnedAt = Clock;
			}
		}
		TestTrue(FString::Printf(TEXT("one operative's SUPPRESS order suppresses within 4 s (%.1f s)"), SuppressedAt), SuppressedAt > 0.f && SuppressedAt < 4.f);
		TestTrue(FString::Printf(TEXT("... and pins within 7 s (%.1f s)"), PinnedAt), PinnedAt > SuppressedAt && PinnedAt < 7.f);
		// Fire stops (a last round now): always the base decay, faster once quiet.
		AddPressure(Config, State, 0.01f);
		const float Before = State.Level;
		Step(Config, State, 1.f);
		const float AfterOne = State.Level;
		TestTrue(TEXT("first second of quiet: base decay only"), FMath::IsNearlyEqual(Before - AfterOne, Config.DecayPerSecond, 0.01f));
		Step(Config, State, 1.f);
		TestTrue(TEXT("once quiet: base + quiet decay"), FMath::IsNearlyEqual(AfterOne - State.Level, Config.DecayPerSecond + Config.QuietDecayPerSecond, 0.01f));
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Step(Config, State, 0.5f);
		}
		TestEqual(TEXT("it decays to 0 and frees him"), State.Level, 0.f);
		TestEqual(TEXT("... tier None"), State.Tier, ESuppressionTier::None);
	}
	{
		FSuppressionState State;
		AddPressure(Config, State, 5.f);
		TestEqual(TEXT("the level is clamped to 1"), State.Level, 1.f);
		AddPressure(Config, State, -1.f);
		TestEqual(TEXT("negative pressure is ignored"), State.Level, 1.f);
	}
	return true;
}

SUPPRESSION_TEST(FSuppressionEffectsTest, "FireEffects")
bool FSuppressionEffectsTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	TestTrue(TEXT("a free enemy fires"), EnemyMayFire(ESuppressionTier::None));
	TestFalse(TEXT("a suppressed enemy does not fire at all (decision 2)"), EnemyMayFire(ESuppressionTier::Suppressed));
	TestFalse(TEXT("a pinned enemy does not fire"), EnemyMayFire(ESuppressionTier::Pinned));
	const FSuppressionFireEffect Free = OperativeFire(Config, ESuppressionTier::None);
	TestTrue(TEXT("a free operative fires aimed"), Free.bMayFire && !Free.bBlind && Free.HitMultiplier == 1.f && Free.SpreadMultiplier == 1.f);
	const FSuppressionFireEffect Suppressed = OperativeFire(Config, ESuppressionTier::Suppressed);
	TestTrue(TEXT("a suppressed operative fires blind with a large spread (decision 3)"),
		Suppressed.bMayFire && Suppressed.bBlind && Suppressed.HitMultiplier < 1.f && Suppressed.SpreadMultiplier > 1.f);
	TestFalse(TEXT("a pinned operative does not fire (decision 3)"), OperativeFire(Config, ESuppressionTier::Pinned).bMayFire);
	TestTrue(TEXT("free: may peek"), MayExpose(ESuppressionTier::None));
	TestFalse(TEXT("suppressed: stays in cover, no peek"), MayExpose(ESuppressionTier::Suppressed));
	TestFalse(TEXT("pinned: no peek"), MayExpose(ESuppressionTier::Pinned));
	return true;
}

SUPPRESSION_TEST(FSuppressionOrderTest, "OrderCadenceAndStop")
bool FSuppressionOrderTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	int32 Burst = 0;
	TArray<float> Delays;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		Delays.Add(NextOrderRoundDelay(Config, 0.65f, 0.1f, Burst));
	}
	TestTrue(TEXT("full-auto: five 0.1 s gaps inside the burst"), Delays[0] == 0.1f && Delays[4] == 0.1f);
	TestTrue(TEXT("... the burst pause after its 6th round"), FMath::IsNearlyEqual(Delays[5], 0.1f + Config.OrderBurstPauseSeconds));
	TestEqual(TEXT("... then the next burst"), Delays[6], 0.1f);
	Burst = 3;
	TestEqual(TEXT("a weapon without full-auto fires at its own cadence"), NextOrderRoundDelay(Config, 0.45f, 0.f, Burst), 0.45f);
	TestEqual(TEXT("... no burst count"), Burst, 0);
	// Ammo use: 10 s of full-auto suppression vs 10 s of the normal 0.65 s fire.
	int32 OrderRounds = 0;
	float Clock = 0.f;
	Burst = 0;
	while (Clock < 10.f)
	{
		++OrderRounds;
		Clock += NextOrderRoundDelay(Config, 0.65f, 0.1f, Burst);
	}
	TestTrue(FString::Printf(TEXT("suppression spends a lot of ammo: %d rounds in 10 s vs ~16 aimed"), OrderRounds), OrderRounds >= 3 * 16);
	TestEqual(TEXT("stop: target down"), CheckOrder(Config, true, false, true, 30, 100), ESuppressOrderStop::TargetDown);
	TestEqual(TEXT("a spot order has no target to lose"), CheckOrder(Config, false, false, true, 30, 100), ESuppressOrderStop::None);
	TestEqual(TEXT("stop: ammo low (clip + reserve at the threshold)"), CheckOrder(Config, true, true, true, 4, 6), ESuppressOrderStop::AmmoLow);
	TestEqual(TEXT("one round over the threshold: on"), CheckOrder(Config, true, true, true, 5, 6), ESuppressOrderStop::None);
	TestEqual(TEXT("stop: the knife"), CheckOrder(Config, true, true, false, 0, 0), ESuppressOrderStop::NoWeapon);
	TestEqual(TEXT("budget = clip + reserve - the kept rounds"), OrderAmmoBudget(Config, 30, 100), 120);
	TestEqual(TEXT("an empty reserve (-1: none) counts as 0"), OrderAmmoBudget(Config, 12, -1), 2);
	TestTrue(TEXT("spray cone narrows with the stance"),
		GetOrderSpreadDeg(Config, 0) > GetOrderSpreadDeg(Config, 1) && GetOrderSpreadDeg(Config, 1) > GetOrderSpreadDeg(Config, 2));
	return true;
}

SUPPRESSION_TEST(FSuppressionTuningTest, "TuningFileMirrorsDefaults")
bool FSuppressionTuningTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	FString Text;
	if (!TestTrue(TEXT("suppression.json readable"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())))
	{
		return false;
	}
	FSuppressionConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("the file parses"), ApplyJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys in the file"), Unknown.Num(), 0);
	const FSuppressionConfig Defaults;
	for (const FString& Key : KnownKeys())
	{
		float File = 0.f;
		float Code = 0.f;
		GetValue(FromFile, Key, File);
		GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code);
	}
	FSuppressionConfig Patched;
	TArray<FString> PatchUnknown;
	TestTrue(TEXT("a partial file applies"), ApplyJson(TEXT("{\"params\": {\"tier.pinned_enter\": 0.9, \"nope\": 1}}"), Patched, &PatchUnknown));
	TestEqual(TEXT("... its key"), Patched.PinnedEnter, 0.9f);
	TestEqual(TEXT("... the rest keeps the defaults"), Patched.SuppressedEnter, Defaults.SuppressedEnter);
	TestEqual(TEXT("... the unknown key is reported"), PatchUnknown.Num(), 1);
	FSuppressionConfig Broken;
	TestFalse(TEXT("broken JSON: false"), ApplyJson(TEXT("{"), Broken));
	TestFalse(TEXT("no params: false"), ApplyJson(TEXT("{\"x\": 1}"), Broken));
	return true;
}

#undef SUPPRESSION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
