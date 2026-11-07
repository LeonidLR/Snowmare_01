// CodexTactics.Combat.UnderFire.* - a column fired at from a corner turns to the muzzle flash and returns fire at it (bug #43,
// UnderFireRules).

#include "Misc/AutomationTest.h"
#include "Combat/UnderFireRules.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
