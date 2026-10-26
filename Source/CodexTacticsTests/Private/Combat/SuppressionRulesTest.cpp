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

SUPPRESSION_TEST(FSuppressionExplosionTest, "ExplosionsAreShort")
bool FSuppressionExplosionTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	TestEqual(TEXT("reach = blast radius x factor"), ExplosionReachCm(Config, 400.f), 400.f * Config.ExplosionRadiusFactor);
	TestEqual(TEXT("an unknown radius uses the minimum reach"), ExplosionReachCm(Config, 0.f), Config.ExplosionMinRadiusCm);
	TestEqual(TEXT("the centre gets the full burst"), ExplosionPressure(Config, 400.f, 0.f), Config.ExplosionPressure);
	TestTrue(TEXT("it falls off to the edge"), ExplosionPressure(Config, 400.f, 500.f) < Config.ExplosionPressure);
	TestEqual(TEXT("beyond the reach: nothing"), ExplosionPressure(Config, 400.f, 601.f), 0.f);
	// The same 0.6 from a blast and from gunfire: the blast's suppression is gone sooner.
	FSuppressionState Blast;
	AddBurst(Config, Blast, 0.6f);
	FSuppressionState Gunfire;
	AddPressure(Config, Gunfire, 0.6f);
	TestEqual(TEXT("a blast suppresses at once"), Blast.Tier, ESuppressionTier::Suppressed);
	Step(Config, Blast, 0.6f);
	Step(Config, Gunfire, 0.6f);
	TestEqual(TEXT("0.6 s later the blast's burst no longer suppresses"), Blast.Tier, ESuppressionTier::None);
	TestEqual(TEXT("... the gunfire's still does"), Gunfire.Tier, ESuppressionTier::Suppressed);
	FSuppressionState Capped;
	AddBurst(Config, Capped, 5.f);
	AddBurst(Config, Capped, 0.3f);
	TestEqual(TEXT("the burst is capped and blasts do not stack"), Capped.Burst, Config.ExplosionMaxBurst);
	TestEqual(TEXT("effective level = gunfire + burst, at most 1"), EffectiveLevel(Capped), Config.ExplosionMaxBurst);
	return true;
}

SUPPRESSION_TEST(FSuppressionFearZoneTest, "DenseFireOpensFearZones")
bool FSuppressionFearZoneTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	const float Dt = 1.f / 60.f;
	auto Fire = [&](float RoundsPerSecond, float Seconds, bool bSquadClose, float& OutOpenedAt)
	{
		TArray<FSuppressionFireCell> Cells;
		float Clock = 0.f;
		float Next = 0.f;
		OutOpenedAt = -1.f;
		int32 Round = 0;
		while (Clock < Seconds)
		{
			if (Clock >= Next)
			{
				// Rounds walked over a 1.2 m disc round one spot.
				const FVector End(1000.f + 120.f * FMath::Sin(Round * 1.7f), 120.f * FMath::Cos(Round * 2.3f), 0.f);
				const int32 Index = FindFireCell(Config, Cells, End);
				AddFireToCell(Index != INDEX_NONE ? Cells[Index] : Cells.AddDefaulted_GetRef(), End);
				Next += 1.f / RoundsPerSecond;
				++Round;
			}
			for (int32 Index = Cells.Num() - 1; Index >= 0; --Index)
			{
				if (!StepFireCell(Config, Cells[Index], Dt, bSquadClose))
				{
					Cells.RemoveAt(Index);
				}
			}
			Clock += Dt;
			if (OutOpenedAt < 0.f && Cells.ContainsByPredicate([](const FSuppressionFireCell& Cell) { return IsZoneOpen(Cell); }))
			{
				OutOpenedAt = Clock;
			}
		}
		return Cells;
	};
	float Opened = 0.f;
	TArray<FSuppressionFireCell> Cells = Fire(5.f, 6.f, false, Opened);
	TestTrue(FString::Printf(TEXT("a SUPPRESS order's full-auto fire (5 rounds/s) opens a fear zone within 4 s (%.1f s)"), Opened), Opened > 0.f && Opened < 4.f);
	TestEqual(TEXT("... one cluster for the walked disc"), Cells.Num(), 1);
	Fire(1.5f, 20.f, false, Opened);
	TestTrue(TEXT("one rifleman's normal fire never does"), Opened < 0.f);
	Fire(5.f, 6.f, true, Opened);
	TestTrue(TEXT("no zone with an operative right there"), Opened < 0.f);
	// Fire stops: the zone stays the duration, then closes.
	Cells = Fire(5.f, 6.f, false, Opened);
	FSuppressionFireCell Cell = Cells[0];
	float Open = 0.f;
	while (IsZoneOpen(Cell) && Open < 30.f)
	{
		StepFireCell(Config, Cell, Dt, false);
		Open += Dt;
	}
	TestTrue(FString::Printf(TEXT("the zone closes a while after the fire stops (%.1f s)"), Open), Open >= Config.ZoneDurationSeconds && Open < Config.ZoneDurationSeconds + 4.f);
	TArray<FSuppressionFireCell> Two;
	AddFireToCell(Two.AddDefaulted_GetRef(), FVector(0.f, 0.f, 0.f));
	TestEqual(TEXT("a round 3 m off counts to the same cell"), FindFireCell(Config, Two, FVector(300.f, 0.f, 0.f)), 0);
	TestEqual(TEXT("a round 6 m off starts another"), FindFireCell(Config, Two, FVector(600.f, 0.f, 0.f)), INDEX_NONE);
	return true;
}

SUPPRESSION_TEST(FSuppressionClickTest, "ShiftClickTarget")
bool FSuppressionClickTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	auto Make = [](const FVector& Location, bool bKnown)
	{
		FSuppressionClickCandidate Candidate;
		Candidate.Location = Location;
		Candidate.bKnown = bKnown;
		return Candidate;
	};
	const FVector Point(0.f, 0.f, 0.f);
	TestEqual(TEXT("nobody: no suppress order (the green Shift + click)"), PickClickTarget(Config, Point, {}), INDEX_NONE);
	TestEqual(TEXT("a known enemy 3 m off the point is meant"), PickClickTarget(Config, Point, { Make(FVector(300.f, 0.f, 0.f), true) }), 0);
	TestEqual(TEXT("5 m off: not"), PickClickTarget(Config, Point, { Make(FVector(500.f, 0.f, 0.f), true) }), INDEX_NONE);
	TestEqual(TEXT("an enemy the squad does not know of is never meant"), PickClickTarget(Config, Point, { Make(FVector(50.f, 0.f, 0.f), false) }), INDEX_NONE);
	TestEqual(TEXT("the nearest known one"),
		PickClickTarget(Config, Point, { Make(FVector(350.f, 0.f, 0.f), true), Make(FVector(100.f, 0.f, 900.f), true), Make(FVector(20.f, 0.f, 0.f), false) }), 1);
	return true;
}

SUPPRESSION_TEST(FSuppressionRageTest, "RageIgnoresSuppression")
bool FSuppressionRageTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	TestTrue(TEXT("an operative in rage ignores suppression (user decision 2026-10-26)"), IgnoresSuppression(true));
	TestFalse(TEXT("out of rage it acts on him"), IgnoresSuppression(false));
	return true;
}

#undef SUPPRESSION_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
