// CodexTactics.Animation.Injured.* - the injured locomotion (the game designer 2026-11-10, InjuredLocomotionRules): a wounded operative walks /
// stands / crawls injured; a freezing one (cold >= cold_from 0.7) walks the injured walk, a frostbitten one (cold >= cold_slow_from 0.9) the slow
// injured walk forward, both keep the shiver idle standing; wounded first.
// Tiers.* (feat/injured-tiers-fullbody): the light / heavy / critical gaits, the run and the idle sets, the idle variants, the full-body raise.

#include "Misc/AutomationTest.h"
#include "Characters/InjuredLocomotionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredGaitTest, "CodexTactics.Animation.Injured.GaitFromTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredGaitTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	FInjuredTriggers Triggers;
	TestEqual(TEXT("defaults: the wounded trigger on, the cold off (the schema's default)"), Triggers.bWounded && Triggers.ColdFrom <= 0.f, true);
	TestTrue(TEXT("wounded -> the injured walk"), ChooseGait(Triggers, true, 0.f) == EInjuredGait::Wounded);
	TestTrue(TEXT("healthy, warm -> off"), ChooseGait(Triggers, false, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("healthy and frozen, the cold trigger off -> off"), ChooseGait(Triggers, false, 0.95f) == EInjuredGait::None);
	Triggers.ColdFrom = 0.7f; // the designer's decisions 2026-11-10: Freezing = the injured walk, Frostbite = the slow walk
	Triggers.ColdSlowFrom = 0.9f;
	TestTrue(TEXT("cold 75 % (Freezing) -> the injured walk"), ChooseGait(Triggers, false, 0.75f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold exactly 70 % -> the injured walk"), ChooseGait(Triggers, false, 0.7f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold 89 % -> still the injured walk"), ChooseGait(Triggers, false, 0.89f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold exactly 90 % (Frostbite) -> the slow injured walk"), ChooseGait(Triggers, false, 0.9f) == EInjuredGait::ColdSlow);
	TestTrue(TEXT("cold 100 % -> the slow walk"), ChooseGait(Triggers, false, 1.f) == EInjuredGait::ColdSlow);
	TestTrue(TEXT("cold 69 % -> off"), ChooseGait(Triggers, false, 0.69f) == EInjuredGait::None);
	TestTrue(TEXT("wounded AND frostbitten -> wounded takes priority"), ChooseGait(Triggers, true, 0.95f) == EInjuredGait::Wounded);
	FInjuredTriggers SlowOnly;
	SlowOnly.ColdSlowFrom = 0.9f;
	TestTrue(TEXT("only cold_slow_from set: freezing -> off"), ChooseGait(SlowOnly, false, 0.75f) == EInjuredGait::None);
	TestTrue(TEXT("only cold_slow_from set: frostbite -> the slow walk"), ChooseGait(SlowOnly, false, 0.95f) == EInjuredGait::ColdSlow);
	Triggers.bWounded = false;
	TestTrue(TEXT("the wounded trigger off: a wounded, warm body -> off"), ChooseGait(Triggers, true, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("the wounded trigger off: wounded and freezing -> the cold walk"), ChooseGait(Triggers, true, 0.8f) == EInjuredGait::Cold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredColdFromTest, "CodexTactics.Animation.Injured.SanitizeColdFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredColdFromTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("a share stays"), SanitizeColdFrom(0.7), 0.7f);
	TestEqual(TEXT("a percent is taken as a percent"), SanitizeColdFrom(70.0), 0.7f);
	TestEqual(TEXT("1 = 100 %"), SanitizeColdFrom(1.0), 1.f);
	TestEqual(TEXT("0 = off"), SanitizeColdFrom(0.0), 0.f);
	TestEqual(TEXT("negative = off"), SanitizeColdFrom(-0.5), 0.f);
	TestEqual(TEXT("over 100 = off"), SanitizeColdFrom(150.0), 0.f);
	TestEqual(TEXT("NaN = off"), SanitizeColdFrom(FMath::Sqrt(-1.0)), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredLayerTargetTest, "CodexTactics.Animation.Injured.LayerTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredLayerTargetTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("wounded, standing still: the injured idle (the layer's)"), LayerTarget(EInjuredGait::Wounded, true, 0.f), 1.f);
	TestEqual(TEXT("wounded, walking: the directional injured walk"), LayerTarget(EInjuredGait::Wounded, true, 1.f), 1.f);
	TestEqual(TEXT("freezing, standing still: the ABP's shiver idle (layer off)"), LayerTarget(EInjuredGait::Cold, true, 0.f), 0.f);
	TestEqual(TEXT("freezing, walking: the injured walk"), LayerTarget(EInjuredGait::Cold, true, 1.f), 1.f);
	TestEqual(TEXT("frostbite, standing still: the shiver idle"), LayerTarget(EInjuredGait::ColdSlow, true, 0.f), 0.f);
	TestEqual(TEXT("frostbite, half way into the walk"), LayerTarget(EInjuredGait::ColdSlow, true, 0.5f), 0.5f);
	TestEqual(TEXT("healthy: off"), LayerTarget(EInjuredGait::None, true, 1.f), 0.f);
	TestEqual(TEXT("not eligible (crouched / prone / in cover / a full-body clip): off"), LayerTarget(EInjuredGait::Wounded, false, 1.f), 0.f);
	TestEqual(TEXT("frostbite walking forward: the slow walk"), SlowWalkShare(EInjuredGait::ColdSlow, 1.f), 1.f);
	TestEqual(TEXT("frostbite strafing (outside the cone): the injured walk"), SlowWalkShare(EInjuredGait::ColdSlow, 0.f), 0.f);
	TestEqual(TEXT("freezing: never the slow walk"), SlowWalkShare(EInjuredGait::Cold, 1.f), 0.f);
	TestEqual(TEXT("wounded: never the slow walk"), SlowWalkShare(EInjuredGait::Wounded, 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredConeTest, "CodexTactics.Animation.Injured.ForwardCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredConeTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("straight ahead: the full slow walk"), ForwardConeShare(0.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("30 deg left: inside the full cone"), ForwardConeShare(-30.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("half way between the edges (57.5 deg)"), ForwardConeShare(57.5f, 35.f, 80.f), 0.5f, 1.e-4f);
	TestEqual(TEXT("a strafe (90 deg): none"), ForwardConeShare(90.f, 35.f, 80.f), 0.f);
	TestEqual(TEXT("backwards: none"), ForwardConeShare(180.f, 35.f, 80.f), 0.f);
	TestEqual(TEXT("an unnormalised angle (360 = ahead)"), ForwardConeShare(360.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("zero edge below the full edge: a hard cone"), ForwardConeShare(40.f, 35.f, 10.f), 0.f);
	TestEqual(TEXT("clip band 10 deg: 3 deg off a clip (alpha 3/45) plays it pure"), BandedAlpha(3.f / 45.f, 10.f), 0.f);
	TestEqual(TEXT("... 3 deg off the next clip: the next clip pure"), BandedAlpha(42.f / 45.f, 10.f), 1.f);
	TestEqual(TEXT("... half way: still half"), BandedAlpha(0.5f, 10.f), 0.5f, 1.e-4f);
	TestEqual(TEXT("... 20 deg past a clip: (20 - 10) / 25"), BandedAlpha(20.f / 45.f, 10.f), 0.4f, 1.e-4f);
	TestEqual(TEXT("no band: unchanged"), BandedAlpha(0.3f, 0.f), 0.3f);
	TestEqual(TEXT("... inside it"), ForwardConeShare(20.f, 35.f, 10.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredCrawlAndTorsoTest, "CodexTactics.Animation.Injured.CrawlAndTorso",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredCrawlAndTorsoTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestTrue(TEXT("wounded and prone with the clips: the injured crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, true, true));
	TestFalse(TEXT("wounded standing: no crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, false, true));
	TestFalse(TEXT("freezing and prone: the mocap crawl (the cold is not a wound)"), UsesInjuredCrawl(EInjuredGait::Cold, true, true));
	TestFalse(TEXT("frostbitten and prone: the mocap crawl"), UsesInjuredCrawl(EInjuredGait::ColdSlow, true, true));
	TestFalse(TEXT("no injured crawl clips for this body (the PMC's UE5 skeleton): the mocap crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, true, false));
	TestEqual(TEXT("aiming: the torso keeps the ABP's rotation exactly"), TorsoPelvisShare(0.3f, true), 0.f);
	TestEqual(TEXT("not aiming: the data's share of the limp"), TorsoPelvisShare(0.3f, false), 0.3f);
	TestEqual(TEXT("clamped to 1"), TorsoPelvisShare(4.f, false), 1.f);
	TestEqual(TEXT("clamped to 0"), TorsoPelvisShare(-1.f, false), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredLimpCueTest, "CodexTactics.Animation.Injured.LimpCue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredLimpCueTest::RunTest(const FString&)
{
	// Bug 2026-11-10 (fix/injured-layer-visible): from the game camera the lower-body limp did not read - the torso cue bends the torso.
	using namespace InjuredLocomotionRules;
	FInjuredLimpCue Cue;
	Cue.bEnabled = true;
	Cue.HunchDeg = 14.f;
	Cue.SwayDeg = 7.f;
	Cue.SwayPhase = 0.f;
	Cue.AimingScale = 0.35f;
	const FVector2D Full = LimpCueAngles(Cue, 1.f, 1.f, 0.25f, false);
	TestEqual(TEXT("full weight: the whole hunch"), static_cast<float>(Full.X), 14.f, 1e-3f);
	TestEqual(TEXT("the sway peaks a quarter cycle after the phase"), static_cast<float>(Full.Y), 7.f, 1e-3f);
	TestEqual(TEXT("... and swings the other way half a cycle on (once per cycle)"), static_cast<float>(LimpCueAngles(Cue, 1.f, 1.f, 0.75f, false).Y), -7.f, 1e-3f);
	TestEqual(TEXT("standing still (walk share 0): the hunch stays, no sway"), static_cast<float>(LimpCueAngles(Cue, 1.f, 0.f, 0.25f, false).Y), 0.f, 1e-4f);
	TestEqual(TEXT("standing still: the hunch"), static_cast<float>(LimpCueAngles(Cue, 1.f, 0.f, 0.25f, false).X), 14.f, 1e-3f);
	TestEqual(TEXT("half the layer: half the hunch"), static_cast<float>(LimpCueAngles(Cue, 0.5f, 1.f, 0.f, false).X), 7.f, 1e-3f);
	const FVector2D Aim = LimpCueAngles(Cue, 1.f, 1.f, 0.25f, true);
	TestEqual(TEXT("aiming: the hunch x aiming_scale"), static_cast<float>(Aim.X), 14.f * 0.35f, 1e-3f);
	TestEqual(TEXT("aiming: the sway x aiming_scale"), static_cast<float>(Aim.Y), 7.f * 0.35f, 1e-3f);
	Cue.SwayPhase = 0.25f;
	TestEqual(TEXT("sway_phase shifts the peak"), static_cast<float>(LimpCueAngles(Cue, 1.f, 1.f, 0.f, false).Y), 7.f, 1e-3f);
	Cue.bEnabled = false;
	TestTrue(TEXT("disabled: no cue"), LimpCueAngles(Cue, 1.f, 1.f, 0.25f, false).IsNearlyZero());
	FInjuredLimpCue Wild;
	Wild.HunchDeg = 90.f;
	Wild.SwayDeg = -5.f;
	Wild.SwayPhase = 1.25f;
	Wild.AimingScale = 3.f;
	const FInjuredLimpCue Clean = SanitizeLimpCue(Wild);
	TestEqual(TEXT("sanitize: hunch <= 30"), Clean.HunchDeg, 30.f);
	TestEqual(TEXT("sanitize: sway >= 0"), Clean.SwayDeg, 0.f);
	TestEqual(TEXT("sanitize: the phase wraps"), Clean.SwayPhase, 0.25f, 1e-4f);
	TestEqual(TEXT("sanitize: aiming scale <= 1"), Clean.AimingScale, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredTierGaitTest, "CodexTactics.Animation.Injured.Tiers.GaitFromTier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredTierGaitTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	using CriticalWoundRules::EWoundTier;
	FInjuredTriggers Triggers;
	Triggers.ColdFrom = 0.7f;
	Triggers.ColdSlowFrom = 0.9f;
	TestTrue(TEXT("no wound, warm -> off"), ChooseTierGait(Triggers, EWoundTier::None, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("light wound -> the injured walk (Wounded)"), ChooseTierGait(Triggers, EWoundTier::Light, 0.f) == EInjuredGait::Wounded);
	TestTrue(TEXT("heavy wound -> the slow walk (WoundedHeavy)"), ChooseTierGait(Triggers, EWoundTier::Heavy, 0.f) == EInjuredGait::WoundedHeavy);
	TestTrue(TEXT("critical (standing a moment before he goes down) -> WoundedHeavy"), ChooseTierGait(Triggers, EWoundTier::Critical, 0.f) == EInjuredGait::WoundedHeavy);
	TestTrue(TEXT("no wound, freezing -> the cold walk (its mapping stays)"), ChooseTierGait(Triggers, EWoundTier::None, 0.75f) == EInjuredGait::Cold);
	TestTrue(TEXT("no wound, frostbite -> the cold slow walk"), ChooseTierGait(Triggers, EWoundTier::None, 0.95f) == EInjuredGait::ColdSlow);
	TestTrue(TEXT("light wound + freezing -> the wound (the same walk, the wounded idle)"), ChooseTierGait(Triggers, EWoundTier::Light, 0.75f) == EInjuredGait::Wounded);
	TestTrue(TEXT("light wound + frostbite -> the worse: the heavy clips"), ChooseTierGait(Triggers, EWoundTier::Light, 0.95f) == EInjuredGait::WoundedHeavy);
	TestTrue(TEXT("heavy wound + freezing -> heavy"), ChooseTierGait(Triggers, EWoundTier::Heavy, 0.75f) == EInjuredGait::WoundedHeavy);
	Triggers.bWounded = false;
	TestTrue(TEXT("the wounded trigger off: a heavy wound, warm -> off"), ChooseTierGait(Triggers, EWoundTier::Heavy, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("the wounded trigger off: heavy + freezing -> the cold walk"), ChooseTierGait(Triggers, EWoundTier::Heavy, 0.8f) == EInjuredGait::Cold);
	TestTrue(TEXT("wound gaits"), IsWoundGait(EInjuredGait::Wounded) && IsWoundGait(EInjuredGait::WoundedHeavy) && !IsWoundGait(EInjuredGait::Cold)
		&& !IsWoundGait(EInjuredGait::ColdSlow) && !IsWoundGait(EInjuredGait::None));
	TestEqual(TEXT("heavy standing still: the slow idle is the layer's"), LayerTarget(EInjuredGait::WoundedHeavy, true, 0.f), 1.f);
	TestEqual(TEXT("heavy walking forward: the slow walk"), SlowWalkShare(EInjuredGait::WoundedHeavy, 1.f), 1.f);
	TestEqual(TEXT("heavy strafing: the 8-direction walk"), SlowWalkShare(EInjuredGait::WoundedHeavy, 0.f), 0.f);
	TestTrue(TEXT("heavy / critical prone: the injured crawl"), UsesInjuredCrawl(EInjuredGait::WoundedHeavy, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredRunAndIdleTest, "CodexTactics.Animation.Injured.Tiers.RunAndIdles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredRunAndIdleTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	// The run (light wound, Run_Injured over run_blend_speeds [170, 250] cm/s within the forward cone).
	TestEqual(TEXT("light, walking 143 cm/s: no run"), RunShare(EInjuredGait::Wounded, 143.f, 170.f, 250.f, 1.f), 0.f);
	TestEqual(TEXT("light, the wounded regroup run 312 cm/s forward: the run"), RunShare(EInjuredGait::Wounded, 312.f, 170.f, 250.f, 1.f), 1.f);
	TestEqual(TEXT("light, 210 cm/s: half way"), RunShare(EInjuredGait::Wounded, 210.f, 170.f, 250.f, 1.f), 0.5f, 0.001f);
	TestEqual(TEXT("light, running sideways (outside the cone): the walk"), RunShare(EInjuredGait::Wounded, 312.f, 170.f, 250.f, 0.f), 0.f);
	TestEqual(TEXT("heavy: never runs"), RunShare(EInjuredGait::WoundedHeavy, 400.f, 170.f, 250.f, 1.f), 0.f);
	TestEqual(TEXT("cold: no run clip"), RunShare(EInjuredGait::Cold, 400.f, 170.f, 250.f, 1.f), 0.f);
	TestEqual(TEXT("no band (full 0): off"), RunShare(EInjuredGait::Wounded, 400.f, 0.f, 0.f, 1.f), 0.f);
	// The idle sets per state.
	TestTrue(TEXT("light, never ran: the walk idles"), ChooseIdleSet(EInjuredGait::Wounded, -1.f, 6.f) == EInjuredIdleSet::Walk);
	TestTrue(TEXT("light, 2 s after a run: the run idles"), ChooseIdleSet(EInjuredGait::Wounded, 2.f, 6.f) == EInjuredIdleSet::Run);
	TestTrue(TEXT("light, 6 s after a run: back to the walk idles"), ChooseIdleSet(EInjuredGait::Wounded, 6.f, 6.f) == EInjuredIdleSet::Walk);
	TestTrue(TEXT("heavy: the slow idles"), ChooseIdleSet(EInjuredGait::WoundedHeavy, 1.f, 6.f) == EInjuredIdleSet::Slow);
	TestTrue(TEXT("cold: no injured idle (the shiver)"), ChooseIdleSet(EInjuredGait::Cold, -1.f, 6.f) == EInjuredIdleSet::None);
	// The variants: alternating 1 / 2 per loop, random = always another one.
	TestEqual(TEXT("the first loop: variant 0"), NextIdleVariant(-1, 2, false, 0.f), 0);
	TestEqual(TEXT("alternate: 0 -> 1"), NextIdleVariant(0, 2, false, 0.f), 1);
	TestEqual(TEXT("alternate: 1 -> 0"), NextIdleVariant(1, 2, false, 0.f), 0);
	TestEqual(TEXT("alternate of three: 2 -> 0"), NextIdleVariant(2, 3, false, 0.f), 0);
	TestEqual(TEXT("one variant: stays"), NextIdleVariant(0, 1, true, 0.7f), 0);
	TestEqual(TEXT("random of two: the other one"), NextIdleVariant(0, 2, true, 0.1f), 1);
	TestEqual(TEXT("random of two from 1: 0"), NextIdleVariant(1, 2, true, 0.99f), 0);
	bool bNeverSame = true;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		bNeverSame &= NextIdleVariant(1, 3, true, Step / 20.f) != 1;
	}
	TestTrue(TEXT("random of three never repeats the previous"), bNeverSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredRaiseTest, "CodexTactics.Animation.Injured.Tiers.RaiseToFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredRaiseTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	FInjuredRaiseConfig Config;
	Config.bFullBody = true; // the designer's defaults: raise 0.25 s, lower after 1.0 s, over 0.4 s
	TestEqual(TEXT("defaults"), Config.RaiseSeconds == 0.25f && Config.LowerAfterSeconds == 1.f && Config.LowerSeconds == 0.4f, true);
	// Raising from the lowered pose at 60 fps: the shot waits until the torso is >= 98 % up.
	float Raise = 0.f;
	int32 Frames = 0;
	while (HoldsShotForRaise(UpperBodyShare(Config, 1.f, Raise, 0.f)) && Frames < 200)
	{
		Raise = StepRaiseShare(Raise, 0.f, 1.f / 60.f, Config);
		++Frames;
	}
	TestEqual(TEXT("the first shot from the lowered pose waits 15 frames at 60 fps (0.25 s)"), Frames, 15);
	TestEqual(TEXT("FirstShotRaiseDelay: lowered = 0.245 s"), FirstShotRaiseDelay(Config, 0.f), 0.245f, 0.0001f);
	TestEqual(TEXT("FirstShotRaiseDelay: already up = 0"), FirstShotRaiseDelay(Config, 1.f), 0.f);
	TestEqual(TEXT("FirstShotRaiseDelay: half up = 0.12 s"), FirstShotRaiseDelay(Config, 0.5f), 0.12f, 0.0001f);
	// Held up while the demands keep coming (a burst, the cooldown between rounds), then lowered after lower_after_s.
	Raise = 1.f;
	Raise = StepRaiseShare(Raise, 0.9f, 0.1f, Config);
	TestEqual(TEXT("0.9 s after the last attempt: still up"), Raise, 1.f);
	Raise = StepRaiseShare(Raise, 1.05f, 0.1f, Config);
	TestEqual(TEXT("1.05 s after it: lowering at 1 / 0.4 s"), Raise, 0.75f, 0.0001f);
	Raise = StepRaiseShare(Raise, 2.f, 1.f, Config);
	TestEqual(TEXT("lowered"), Raise, 0.f);
	TestEqual(TEXT("no demand yet (-1): stays lowered"), StepRaiseShare(0.f, -1.f, 0.1f, Config), 0.f);
	FInjuredRaiseConfig Instant = Config;
	Instant.RaiseSeconds = 0.f;
	TestEqual(TEXT("raise_to_fire_s 0: up at once"), StepRaiseShare(0.f, 0.f, 0.016f, Instant), 1.f);
	// The torso's share in the clip.
	TestEqual(TEXT("lowered, full layer: the whole torso is the clip's"), UpperBodyShare(Config, 1.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("raised: the weapon pose"), UpperBodyShare(Config, 1.f, 1.f, 0.f), 0.f);
	TestEqual(TEXT("an upper-body clip (a reload / a grenade) takes the torso by its weight"), UpperBodyShare(Config, 1.f, 0.f, 0.75f), 0.25f, 0.0001f);
	TestEqual(TEXT("the layer half in"), UpperBodyShare(Config, 0.5f, 0.f, 0.f), 0.5f);
	FInjuredRaiseConfig Off;
	TestEqual(TEXT("full_body off (the morning's lower-body layer): never the clip's torso"), UpperBodyShare(Off, 1.f, 0.f, 0.f), 0.f);
	TestEqual(TEXT("full_body off: no shot delay"), FirstShotRaiseDelay(Off, 0.f), 0.f);
	TestFalse(TEXT("the healthy body (share 0) never holds a shot"), HoldsShotForRaise(0.f));
	FInjuredRaiseConfig Wild;
	Wild.RaiseSeconds = -3.f;
	Wild.LowerAfterSeconds = 99.f;
	Wild.LowerSeconds = FMath::Sqrt(-1.f);
	const FInjuredRaiseConfig Clean = SanitizeRaise(Wild);
	TestEqual(TEXT("sanitize: raise >= 0"), Clean.RaiseSeconds, 0.f);
	TestEqual(TEXT("sanitize: lower_after <= 5"), Clean.LowerAfterSeconds, 5.f);
	TestEqual(TEXT("sanitize: NaN lower -> 0.4"), Clean.LowerSeconds, 0.4f);
	return true;
}

#endif
