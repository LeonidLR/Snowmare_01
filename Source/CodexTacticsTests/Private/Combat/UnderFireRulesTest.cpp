// CodexTactics.Combat.UnderFire.* - a column fired at from a corner turns to the muzzle flash and returns fire at it (bug #43,
// UnderFireRules).

#include "Misc/AutomationTest.h"
#include "Combat/UnderFireRules.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireThreatTest, "CodexTactics.Combat.UnderFire.ThreatMemoryAndSharing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireThreatTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	const FUnderFireConfig& Config = GetConfig();
	TestTrue(TEXT("the config keeps a threat for a few seconds"), Config.MemorySeconds >= 1.f && Config.MemorySeconds <= 10.f);
	TestTrue(TEXT("a round 1 s ago is still the threat"), IsThreatActive(11.0, 10.0, Config.MemorySeconds));
	TestFalse(TEXT("forgotten after the memory"), IsThreatActive(10.0 + Config.MemorySeconds + 0.1, 10.0, Config.MemorySeconds));
	TestFalse(TEXT("never shot at (time -1e9): no threat"), IsThreatActive(5.0, -1.0e9, Config.MemorySeconds));
	TestTrue(TEXT("the target himself always shares it"), SharesThreat(true, 100000.f, Config.ShareRadiusCm));
	TestTrue(TEXT("the rear man of the column 8 m back shares it"), SharesThreat(false, 800.f, Config.ShareRadiusCm));
	TestFalse(TEXT("a mate far off does not"), SharesThreat(false, Config.ShareRadiusCm + 1.f, Config.ShareRadiusCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireFaceTest, "CodexTactics.Combat.UnderFire.TurnsToTheFlash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireFaceTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	FFaceFacts Facts;
	Facts.bRealTimeFight = true;
	Facts.bAlive = true;
	Facts.bThreatActive = true;
	TestTrue(TEXT("walking in the column, no target in his sights: he turns to the flash"), ShouldFaceThreat(Facts));
	auto Without = [&Facts](TFunctionRef<void(FFaceFacts&)> Change)
	{
		FFaceFacts Copy = Facts;
		Change(Copy);
		return ShouldFaceThreat(Copy);
	};
	TestFalse(TEXT("a combat target turns him already"), Without([](FFaceFacts& F) { F.bHasCombatTarget = true; }));
	TestFalse(TEXT("in cover the back stays to the wall"), Without([](FFaceFacts& F) { F.bInCover = true; }));
	TestFalse(TEXT("sprinting he faces his run"), Without([](FFaceFacts& F) { F.bSprinting = true; }));
	TestFalse(TEXT("carrying a body"), Without([](FFaceFacts& F) { F.bCarrying = true; }));
	TestFalse(TEXT("panicking"), Without([](FFaceFacts& F) { F.bPanicking = true; }));
	TestFalse(TEXT("a quick fire / SUPPRESS order turns him itself"), Without([](FFaceFacts& F) { F.bFireOrder = true; }));
	TestFalse(TEXT("the player's facing order wins"), Without([](FFaceFacts& F) { F.bExplicitFacing = true; }));
	TestFalse(TEXT("turn-based: the grid owns the facing"), Without([](FFaceFacts& F) { F.bGridOwnsFacing = true; }));
	TestFalse(TEXT("exploration: no fight"), Without([](FFaceFacts& F) { F.bRealTimeFight = false; }));
	TestFalse(TEXT("forgotten threat"), Without([](FFaceFacts& F) { F.bThreatActive = false; }));
	TestFalse(TEXT("dead"), Without([](FFaceFacts& F) { F.bAlive = false; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireReturnTest, "CodexTactics.Combat.UnderFire.ReturnsFireAtTheFlash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireReturnTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	TestTrue(TEXT("no body line, a line to the flash, in reach, AGGRESSIVE: he fires back at the flash"), ShouldReturnFire(true, false, true, true, true));
	TestFalse(TEXT("a body line: the normal aimed shot instead"), ShouldReturnFire(true, true, true, true, true));
	TestFalse(TEXT("the rear man without a line to the flash only turns"), ShouldReturnFire(true, false, true, false, true));
	TestFalse(TEXT("the posture holds his own fire (PASSIVE)"), ShouldReturnFire(true, false, false, true, true));
	TestFalse(TEXT("beyond his weapon's reach"), ShouldReturnFire(true, false, true, true, false));
	TestFalse(TEXT("no threat"), ShouldReturnFire(false, false, true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireManoeuvreGateTest, "CodexTactics.Combat.UnderFire.ManoeuvreGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireManoeuvreGateTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	const FUnderFireConfig Config;
	FManoeuvreFacts Facts;
	Facts.bRealTimeFight = true;
	Facts.bAlive = true;
	Facts.bThreatActive = true;
	Facts.SecondsUnderFire = 1.f;
	TestTrue(TEXT("Solo / a PMC fighter in the open under fire, no order: he manoeuvres"), MayManoeuvre(Config, Facts));
	auto With = [&Facts, &Config](TFunctionRef<void(FManoeuvreFacts&)> Change)
	{
		FManoeuvreFacts Copy = Facts;
		Change(Copy);
		return MayManoeuvre(Config, Copy);
	};
	TestFalse(TEXT("never over the player's explicit move order (any own order)"), With([](FManoeuvreFacts& F) { F.bOwnOrder = true; }));
	TestFalse(TEXT("nor over a fire / facing order"), With([](FManoeuvreFacts& F) { F.bFireOrFacingOrder = true; }));
	TestFalse(TEXT("in cover already"), With([](FManoeuvreFacts& F) { F.bInCover = true; }));
	TestFalse(TEXT("on his way into cover"), With([](FManoeuvreFacts& F) { F.bCoverPending = true; }));
	TestFalse(TEXT("carrying a body"), With([](FManoeuvreFacts& F) { F.bCarrying = true; }));
	TestFalse(TEXT("panicking"), With([](FManoeuvreFacts& F) { F.bPanicking = true; }));
	TestFalse(TEXT("turn-based"), With([](FManoeuvreFacts& F) { F.bGridOwnsFacing = true; }));
	TestFalse(TEXT("exploration"), With([](FManoeuvreFacts& F) { F.bRealTimeFight = false; }));
	TestFalse(TEXT("forgotten threat"), With([](FManoeuvreFacts& F) { F.bThreatActive = false; }));
	TestFalse(TEXT("Commander Mode owns the cover decisions"), With([](FManoeuvreFacts& F) { F.bAutonomyOwnsCover = true; }));
	// Game designer 2026-11-09: Solo with the autonomy off - he fires from where he was sent, no manoeuvre of his own.
	TestFalse(TEXT("Solo, autonomy off: he holds where he was sent"), With([](FManoeuvreFacts& F) { F.bSoloHold = true; }));
	TestFalse(TEXT("formation: nothing while it walks (the leader's slot logic first)"),
		With([](FManoeuvreFacts& F) { F.bFormationFollower = true; F.bFormationWalking = true; }));
	TestFalse(TEXT("formation halted: a moment's grace after the first round"),
		With([&Config](FManoeuvreFacts& F) { F.bFormationFollower = true; F.SecondsUnderFire = Config.ReactionSeconds * 0.5f; }));
	TestTrue(TEXT("formation halted, the grace over: he manoeuvres"),
		With([&Config](FManoeuvreFacts& F) { F.bFormationFollower = true; F.SecondsUnderFire = Config.ReactionSeconds + 0.1f; }));
	FUnderFireConfig Off;
	Off.ManoeuvreEnabled = 0.f;
	TestFalse(TEXT("manoeuvre.enabled 0: only the turn / the return fire"), MayManoeuvre(Off, Facts));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireManoeuvreChoiceTest, "CodexTactics.Combat.UnderFire.ManoeuvreChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireManoeuvreChoiceTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	const FUnderFireConfig Config;
	TestTrue(TEXT("data: cover within 8 m"), FMath::IsNearlyEqual(Config.CoverSeekRadiusCm, 800.f));
	TestTrue(TEXT("data: a step of at most 2 m"), FMath::IsNearlyEqual(Config.SidestepMaxCm, 200.f));
	FManoeuvreOptions Options;
	Options.bCoverFound = true;
	Options.bWantsLine = true;
	Options.bSidestepFound = true;
	TestTrue(TEXT("cover nearby: he takes it first"), ChooseManoeuvre(Config, Options) == EUnderFireManoeuvre::TakeCover);
	Options.bCoverFound = false;
	TestTrue(TEXT("no cover, no line, a spot with a line: a step off the line"), ChooseManoeuvre(Config, Options) == EUnderFireManoeuvre::Sidestep);
	Options.bSidestepCoolingDown = true;
	TestTrue(TEXT("... just stepped: prone instead"), ChooseManoeuvre(Config, Options) == EUnderFireManoeuvre::GoProne);
	Options.bSidestepCoolingDown = false;
	Options.bWantsLine = false;
	TestTrue(TEXT("a clear line, no cover: prone"), ChooseManoeuvre(Config, Options) == EUnderFireManoeuvre::GoProne);
	Options.bProne = true;
	TestTrue(TEXT("prone already: nothing more"), ChooseManoeuvre(Config, Options) == EUnderFireManoeuvre::None);
	Options.bProne = false;
	FUnderFireConfig NoProne;
	NoProne.ProneWhenNoCover = 0.f;
	TestTrue(TEXT("prone.when_no_cover 0: he stays up"), ChooseManoeuvre(NoProne, Options) == EUnderFireManoeuvre::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireSidestepTest, "CodexTactics.Combat.UnderFire.SidestepAndMateInLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireSidestepTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	const FUnderFireConfig Config;
	const TArray<float> Offsets = SidestepOffsets(Config);
	TestEqual(TEXT("50 cm steps both sides up to 2 m: 8 candidates"), Offsets.Num(), 8);
	if (Offsets.Num() == 8)
	{
		TestTrue(TEXT("nearest first, alternating sides"), Offsets[0] == 50.f && Offsets[1] == -50.f && Offsets[2] == 100.f && Offsets[7] == -200.f);
	}
	float MaxStep = 0.f;
	for (const float Offset : Offsets)
	{
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Offset));
	}
	TestTrue(TEXT("never more than sidestep.max_cm"), MaxStep <= Config.SidestepMaxCm);
	// The column walks along +X, the shooter 20 m ahead: a step is sideways (Y), never towards / away from him.
	const FVector Feet(0.f, 0.f, 0.f);
	const FVector Flash(2000.f, 0.f, 140.f);
	const FVector Right = SidestepPoint(Feet, Flash, 100.f);
	TestTrue(TEXT("a +1 m step is 1 m to the right of the line, no closer to the flash"), Right.Equals(FVector(0.f, 100.f, 0.f), 0.01f));
	TestTrue(TEXT("a -1 m step to the left"), SidestepPoint(Feet, Flash, -100.f).Equals(FVector(0.f, -100.f, 0.f), 0.01f));
	// A mate in the column between him and the flash blocks the line; one beside it or behind him does not.
	TestTrue(TEXT("the mate 3 m ahead on the line blocks it"), IsMateInLine(Feet, Flash, FVector(300.f, 20.f, 0.f), Config.MateBlockRadiusCm));
	TestFalse(TEXT("a mate 1 m to the side does not"), IsMateInLine(Feet, Flash, FVector(300.f, 100.f, 0.f), Config.MateBlockRadiusCm));
	TestFalse(TEXT("a mate behind him does not"), IsMateInLine(Feet, Flash, FVector(-300.f, 0.f, 0.f), Config.MateBlockRadiusCm));
	TestFalse(TEXT("a mate past the flash does not"), IsMateInLine(Feet, Flash, FVector(2300.f, 0.f, 0.f), Config.MateBlockRadiusCm));
	TestFalse(TEXT("his own body at the muzzle does not"), IsMateInLine(Feet, Flash, FVector(10.f, 0.f, 0.f), Config.MateBlockRadiusCm));
	TestFalse(TEXT("after a 1 m step the mate ahead is out of the line"), IsMateInLine(Right, Flash, FVector(300.f, 0.f, 0.f), Config.MateBlockRadiusCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireHoldTest, "CodexTactics.Combat.UnderFire.HoldSpotAndRise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireHoldTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	const FUnderFireConfig Config;
	const double Last = 100.0;
	TestTrue(TEXT("manoeuvred, a round 2 s ago: he holds the spot"), IsHoldingManoeuvreSpot(Config, true, Last + 2.0, Last, false));
	TestTrue(TEXT("... still within memory + hold"), IsHoldingManoeuvreSpot(Config, true, Last + Config.MemorySeconds + Config.HoldSeconds - 0.1, Last, false));
	TestFalse(TEXT("the threat long over: the formation takes him again"),
		IsHoldingManoeuvreSpot(Config, true, Last + Config.MemorySeconds + Config.HoldSeconds + 0.1, Last, false));
	TestFalse(TEXT("the leader got a new order: released at once"), IsHoldingManoeuvreSpot(Config, true, Last + 1.0, Last, true));
	TestFalse(TEXT("no manoeuvre: nothing to hold"), IsHoldingManoeuvreSpot(Config, false, Last + 1.0, Last, false));
	TestTrue(TEXT("prone under fire, the threat over: he crouches again"), ShouldRiseAfterThreat(true, true, false));
	TestFalse(TEXT("still under fire: he stays down"), ShouldRiseAfterThreat(true, true, true));
	TestFalse(TEXT("prone by the player's own choice: left alone"), ShouldRiseAfterThreat(false, true, false));
	TestFalse(TEXT("already up"), ShouldRiseAfterThreat(true, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnderFireTuningTest, "CodexTactics.Combat.UnderFire.TuningFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnderFireTuningTest::RunTest(const FString&)
{
	using namespace UnderFireRules;
	FString Text;
	if (!TestTrue(TEXT("under_fire.json readable"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())))
	{
		return false;
	}
	FUnderFireConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("the file parses"), ApplyJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys in the file"), Unknown.Num(), 0);
	const FUnderFireConfig Defaults;
	for (const FString& Key : KnownKeys())
	{
		float File = 0.f;
		float Code = 0.f;
		GetValue(FromFile, Key, File);
		GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code);
	}
	FUnderFireConfig Patched;
	TArray<FString> PatchUnknown;
	TestTrue(TEXT("a partial file applies"), ApplyJson(TEXT("{\"params\": {\"cover.radius_cm\": 1200, \"nope\": 1}}"), Patched, &PatchUnknown));
	TestEqual(TEXT("... its key"), Patched.CoverSeekRadiusCm, 1200.f);
	TestEqual(TEXT("... the unknown key reported"), PatchUnknown.Num(), 1);
	TestFalse(TEXT("no params object: refused"), ApplyJson(TEXT("{}"), Patched));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
