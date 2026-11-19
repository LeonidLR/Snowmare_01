// CodexTactics.Combat.FireGate.* - the one fire gate of every operative fire path (bugs 058c / 061, game designer 2026-11-15: turn -> raise the
// weapon -> only then the round; FireGateRules).

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Combat/FireGateRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FireGateTestLocal
{
	/** A healthy, still open-stance shooter on target, raised and settled: the round may leave. */
	FFireGateInput Ready()
	{
		FFireGateInput In;
		In.ResidualDeg = 4.f;
		In.ConeDeg = 30.f;
		In.bBodyReady = true;
		In.SecondsOnTarget = 1.f;
		In.SecondsRaised = 1.f;
		In.bAnimLive = true;
		return In;
	}

	/** TestEqual for a verdict (named in the failure line). */
	bool Verdict(FAutomationTestBase& Test, const TCHAR* What, EFireGateBlock Actual, EFireGateBlock Expected)
	{
		return Test.TestEqual(FString::Printf(TEXT("%s [%s / want %s]"), What, FireGateRules::BlockName(Actual), FireGateRules::BlockName(Expected)),
			static_cast<int32>(Actual), static_cast<int32>(Expected));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateOpenTest, "CodexTactics.Combat.FireGate.OpenTurnRaiseFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateOpenTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	using FireGateTestLocal::Ready;
	const FFireGateConfig Config;
	FireGateTestLocal::Verdict(*this, TEXT("on target, raised, settled: the round leaves"), Evaluate(Ready(), Config), EFireGateBlock::None);

	FFireGateInput Turning = Ready();
	Turning.ResidualDeg = 31.f;
	FireGateTestLocal::Verdict(*this, TEXT("the barrel past the cone: turning first"), Evaluate(Turning, Config), EFireGateBlock::NotOnTarget);

	FFireGateInput BodyOff = Ready();
	BodyOff.bBodyReady = false; // the twisted rifle is on target, the body faces the wall (058c)
	FireGateTestLocal::Verdict(*this, TEXT("the rifle twisted on target but the body faces away: no round"), Evaluate(BodyOff, Config), EFireGateBlock::NotOnTarget);

	FFireGateInput Down = Ready();
	Down.SecondsRaised = -1.f;
	FireGateTestLocal::Verdict(*this, TEXT("on target with the weapon down: raise first"), Evaluate(Down, Config), EFireGateBlock::Raising);
	Down.SecondsRaised = Config.RaiseSeconds - 0.01f;
	FireGateTestLocal::Verdict(*this, TEXT("... still raising just under raise_seconds"), Evaluate(Down, Config), EFireGateBlock::Raising);
	Down.SecondsRaised = Config.RaiseSeconds;
	FireGateTestLocal::Verdict(*this, TEXT("... raised at raise_seconds"), Evaluate(Down, Config), EFireGateBlock::None);

	FFireGateInput JustTurned = Ready();
	JustTurned.SecondsOnTarget = 0.f;
	FireGateTestLocal::Verdict(*this, TEXT("the barrel just came onto the target: it settles first"), Evaluate(JustTurned, Config), EFireGateBlock::Settling);
	JustTurned.SecondsOnTarget = Config.SettleSeconds;
	FireGateTestLocal::Verdict(*this, TEXT("... settled after settle_seconds"), Evaluate(JustTurned, Config), EFireGateBlock::None);

	FFireGateInput Clip = Ready();
	Clip.bStanceClip = true;
	FireGateTestLocal::Verdict(*this, TEXT("a stance clip plays: no round"), Evaluate(Clip, Config), EFireGateBlock::StanceClip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateOrderTest, "CodexTactics.Combat.FireGate.VerdictOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateOrderTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	const FFireGateConfig Config;
	// Everything wrong at once: the turn is named first, then the wound, the stance clip, the raise, the settle.
	FFireGateInput In;
	In.ResidualDeg = 90.f;
	In.bBodyReady = false;
	In.SecondsOnTarget = -1.f;
	In.SecondsRaised = -1.f;
	In.bStanceClip = true;
	In.bWoundedRaisePending = true;
	FireGateTestLocal::Verdict(*this, TEXT("1: not on target"), Evaluate(In, Config), EFireGateBlock::NotOnTarget);
	In.ResidualDeg = 0.f;
	In.bBodyReady = true;
	FireGateTestLocal::Verdict(*this, TEXT("2: the wounded raise"), Evaluate(In, Config), EFireGateBlock::WoundedRaise);
	In.bWoundedRaisePending = false;
	FireGateTestLocal::Verdict(*this, TEXT("3: the stance clip"), Evaluate(In, Config), EFireGateBlock::StanceClip);
	In.bStanceClip = false;
	FireGateTestLocal::Verdict(*this, TEXT("4: the raise"), Evaluate(In, Config), EFireGateBlock::Raising);
	In.SecondsRaised = 5.f;
	FireGateTestLocal::Verdict(*this, TEXT("5: the settle"), Evaluate(In, Config), EFireGateBlock::Settling);
	In.SecondsOnTarget = 5.f;
	FireGateTestLocal::Verdict(*this, TEXT("6: open"), Evaluate(In, Config), EFireGateBlock::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateWoundedTest, "CodexTactics.Combat.FireGate.WoundedRaiseAnyStance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateWoundedTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	using FireGateTestLocal::Ready;
	const FFireGateConfig Config;
	// The designer's case: badly wounded, crouched next to a cube. The raise state itself holds the round (not the standing layer's share).
	FFireGateInput Crouched = Ready();
	Crouched.bWoundedRaisePending = true;
	FireGateTestLocal::Verdict(*this, TEXT("wounded, crouched, on target: the torso rises first"), Evaluate(Crouched, Config), EFireGateBlock::WoundedRaise);
	FFireGateInput Cover = Crouched;
	Cover.bInCover = true;
	FireGateTestLocal::Verdict(*this, TEXT("wounded in cover, the cover pose on target: the torso rises first"), Evaluate(Cover, Config), EFireGateBlock::WoundedRaise);
	Cover.bWoundedRaisePending = false;
	FireGateTestLocal::Verdict(*this, TEXT("... raised: the round leaves"), Evaluate(Cover, Config), EFireGateBlock::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateCoverTest, "CodexTactics.Combat.FireGate.CoverPoseAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateCoverTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	using FireGateTestLocal::Ready;
	const FFireGateConfig Config;
	FFireGateInput In = Ready();
	In.bInCover = true;
	In.SecondsRaised = Config.RaiseSeconds; // enough for the open pose, not for the cover stance
	FireGateTestLocal::Verdict(*this, TEXT("cover: the fire stance needs cover_raise_seconds"), Evaluate(In, Config),
		Config.CoverRaiseSeconds > Config.RaiseSeconds ? EFireGateBlock::Raising : EFireGateBlock::None);
	In.SecondsRaised = Config.CoverRaiseSeconds;
	FireGateTestLocal::Verdict(*this, TEXT("cover: the stance up, the pose's barrel on target - the round leaves"), Evaluate(In, Config), EFireGateBlock::None);

	// The wall slot's facing is no aim: the lean / pop-up barrel misses the point.
	In.ResidualDeg = 70.f;
	In.SecondsRaised = 0.1f;
	FireGateTestLocal::Verdict(*this, TEXT("cover: the stance rising, its barrel off the point - not on target yet"), Evaluate(In, Config), EFireGateBlock::NotOnTarget);
	In.SecondsRaised = Config.CoverReachSeconds;
	FireGateTestLocal::Verdict(*this, TEXT("cover: the stance up cover_reach_seconds and still off - out of the pose's reach (step out)"), Evaluate(In, Config),
		EFireGateBlock::CoverPoseOutOfReach);
	In.bBodyReady = false;
	FireGateTestLocal::Verdict(*this, TEXT("cover: his back not to the wall yet - only turning"), Evaluate(In, Config), EFireGateBlock::NotOnTarget);

	FFireGateInput Open = Ready();
	Open.ResidualDeg = 70.f;
	Open.SecondsRaised = 10.f;
	FireGateTestLocal::Verdict(*this, TEXT("open: no reach rule, he turns"), Evaluate(Open, Config), EFireGateBlock::NotOnTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateStaleTest, "CodexTactics.Combat.FireGate.StaleAnimHoldsOnlyTheAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateStaleTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	using FireGateTestLocal::Ready;
	const FFireGateConfig Config;
	FFireGateInput In = Ready();
	In.bAnimLive = false;
	In.SecondsRaised = -1.f;
	In.bStanceClip = true;
	In.bWoundedRaisePending = true;
	FireGateTestLocal::Verdict(*this, TEXT("no anim update: no pose hold (nobody sees the pose)"), Evaluate(In, Config), EFireGateBlock::None);
	In.ResidualDeg = 45.f;
	FireGateTestLocal::Verdict(*this, TEXT("... but the aim still holds"), Evaluate(In, Config), EFireGateBlock::NotOnTarget);
	In.ResidualDeg = 0.f;
	In.SecondsOnTarget = 0.f;
	FireGateTestLocal::Verdict(*this, TEXT("... and the settle after a turn"), Evaluate(In, Config), EFireGateBlock::Settling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateHelpersTest, "CodexTactics.Combat.FireGate.BodyArcAndStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateHelpersTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	TestTrue(TEXT("body 25 deg off, arc 30: in"), IsBodyInArc(25.f, 30.f));
	TestTrue(TEXT("signed error counts by its size"), IsBodyInArc(-29.f, 30.f));
	TestFalse(TEXT("body 55 deg off, arc 30: out (the old max(residual, error - arc) let 60 deg through)"), IsBodyInArc(55.f, 30.f));
	TestTrue(TEXT("arc 180: walking fire keeps the twist"), IsBodyInArc(170.f, 180.f));
	TestFalse(TEXT("arc 0: only dead ahead"), IsBodyInArc(1.f, 0.f));

	TestEqual(TEXT("stamp: set when the condition turns true"), StepSince(-1.0, true, 5.0), 5.0);
	TestEqual(TEXT("stamp: kept while it holds"), StepSince(5.0, true, 7.0), 5.0);
	TestEqual(TEXT("stamp: cleared when false"), StepSince(5.0, false, 7.0), -1.0);
	TestEqual(TEXT("seconds since: unset = -1"), SecondsSince(-1.0, 3.0), -1.f);
	TestEqual(TEXT("seconds since: elapsed"), SecondsSince(2.0, 3.5), 1.5f);
	TestEqual(TEXT("seconds since: never negative"), SecondsSince(4.0, 3.5), 0.f);

	FFireGateConfig Off;
	Off.Enabled = 0.f;
	TestFalse(TEXT("enabled 0: off"), IsEnabled(Off));
	TestTrue(TEXT("defaults: on"), IsEnabled(FFireGateConfig()));
	for (uint8 Block = 0; Block < static_cast<uint8>(EFireGateBlock::Count); ++Block)
	{
		TestFalse(FString::Printf(TEXT("verdict %d has a name"), Block), FString(BlockName(static_cast<EFireGateBlock>(Block))).Contains(TEXT("?")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireGateTuningTest, "CodexTactics.Combat.FireGate.TuningFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireGateTuningTest::RunTest(const FString&)
{
	using namespace FireGateRules;
	FString Text;
	if (!TestTrue(TEXT("fire_gate.json readable"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())))
	{
		return false;
	}
	FFireGateConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("the file parses"), ApplyJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys in the file"), Unknown.Num(), 0);
	const FFireGateConfig Defaults;
	for (const FString& Key : KnownKeys())
	{
		float File = 0.f;
		float Code = 0.f;
		GetValue(FromFile, Key, File);
		GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code);
	}
	TestTrue(TEXT("the raise is short (<= 0.5 s): the designer asked for a visible but quick raise"), Defaults.RaiseSeconds > 0.f && Defaults.RaiseSeconds <= 0.5f
		&& Defaults.CoverRaiseSeconds > 0.f && Defaults.CoverRaiseSeconds <= 0.5f);
	FFireGateConfig Patched;
	TArray<FString> PatchUnknown;
	TestTrue(TEXT("a partial file applies"), ApplyJson(TEXT("{\"params\": {\"raise_seconds\": 0.35, \"nope\": 1}}"), Patched, &PatchUnknown));
	TestEqual(TEXT("... its key"), Patched.RaiseSeconds, 0.35f);
	TestEqual(TEXT("... the unknown key reported"), PatchUnknown.Num(), 1);
	TestFalse(TEXT("no params object: refused"), ApplyJson(TEXT("{}"), Patched));
	// G007: the dead zone / ground probe / melee reach sections come through the same loader (no second file reader).
	for (const TCHAR* Key : { TEXT("dead_zone.edge_drop_cm"), TEXT("ground_probe.spacing_cm"), TEXT("melee_reach.step_max_cm"), TEXT("melee_reach.height_cap_cm"), TEXT("roof_lip.lip_cm"), TEXT("roof_lip.past_cm") })
	{
		TestTrue(FString::Printf(TEXT("%s is a known key"), Key), KnownKeys().Contains(Key));
	}
	FFireGateConfig Sections;
	TArray<FString> SectionUnknown;
	TestTrue(TEXT("sections apply"), ApplyJson(TEXT("{\"params\": {}, \"dead_zone\": {\"comment\": \"x\", \"edge_drop_cm\": 70, \"nope\": 1}, \"melee_reach\": {\"step_max_cm\": 40}}"),
		Sections, &SectionUnknown));
	TestEqual(TEXT("... dead_zone.edge_drop_cm"), Sections.DeadZone.EdgeDropCm, 70.f);
	TestEqual(TEXT("... melee_reach.step_max_cm"), Sections.MeleeReach.StepMaxCm, 40.f);
	TestTrue(TEXT("... a section's unknown key reported with its section, its comment not"), SectionUnknown.Num() == 1 && SectionUnknown[0] == TEXT("dead_zone.nope"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
