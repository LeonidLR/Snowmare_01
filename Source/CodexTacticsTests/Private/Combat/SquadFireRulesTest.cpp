#include "Misc/AutomationTest.h"
#include "Combat/SquadFireRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/player.gd parity: _find_shoot_target (posture range, barricade rules, flank switch),
// get_elevation_advantage, is_target_in_dead_zone, _shoot_at_target damage.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadFireRulesTest, "CodexTactics.Combat.SquadFire.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadFireRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Range standing x1"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Standing), 1.f);
	TestEqual(TEXT("Range crouching x1.15"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Crouching), 1.15f);
	TestEqual(TEXT("Range prone x1.35"), SquadFireRules::GetPostureRangeMultiplier(EOperativeStance::Prone), 1.35f);
	TestEqual(TEXT("Damage crouching x1.25"), SquadFireRules::GetStanceDamageMultiplier(EOperativeStance::Crouching), 1.25f);
	TestEqual(TEXT("Damage prone x1.6"), SquadFireRules::GetStanceDamageMultiplier(EOperativeStance::Prone), 1.6f);

	// Godot: centre (feet + 1 m) at least 1.5 m above the enemy's feet.
	TestFalse(TEXT("Same floor: not elevated"), SquadFireRules::GetElevationAdvantage(0.f, 0.f).bElevated);
	TestFalse(TEXT("0.4 m up: not elevated"), SquadFireRules::GetElevationAdvantage(40.f, 0.f).bElevated);
	const FElevationAdvantage High = SquadFireRules::GetElevationAdvantage(50.f, 0.f);
	TestTrue(TEXT("0.5 m up: elevated"), High.bElevated && High.RangeMultiplier == 1.25f && High.DamageMultiplier == 1.15f && High.bBypassLowCover);

	TestFalse(TEXT("Dead zone needs 1.8 m"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 70.f), FVector(100.f, 0.f, 0.f)));
	TestTrue(TEXT("Right under the platform"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 200.f), FVector(150.f, 0.f, 0.f)));
	TestFalse(TEXT("Too far out"), SquadFireRules::IsInDeadZone(FVector(0.f, 0.f, 200.f), FVector(300.f, 0.f, 0.f)));

	const FElevationAdvantage Flat;
	TestTrue(TEXT("Clear line"), SquadFireRules::JudgeLine(EShotLineHit::Clear, EOperativeStance::Prone, Flat).bCanHit);
	TestFalse(TEXT("Wall blocks"), SquadFireRules::JudgeLine(EShotLineHit::Blocked, EOperativeStance::Standing, Flat).bCanHit);
	const FShotLineVerdict Prone = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Prone, Flat);
	TestTrue(TEXT("Prone behind a barricade: blocked"), !Prone.bCanHit && Prone.bBarricadeBlocked);
	const FShotLineVerdict Crouched = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Crouching, Flat);
	TestTrue(TEXT("Crouched behind a barricade: cover 0.8"), Crouched.bCanHit && Crouched.Cover == 0.8f);
	const FShotLineVerdict Standing = SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Standing, Flat);
	TestTrue(TEXT("Standing over a barricade: cover 1"), Standing.bCanHit && Standing.Cover == 1.f);
	TestTrue(TEXT("Elevated prone over a barricade"), SquadFireRules::JudgeLine(EShotLineHit::Barricade, EOperativeStance::Prone, High).bCanHit);

	TestTrue(TEXT("Closer than 0.9 x current"), SquadFireRules::IsSignificantlyCloser(800.f, 1000.f, 0.9f));
	TestFalse(TEXT("Not closer enough"), SquadFireRules::IsSignificantlyCloser(950.f, 1000.f, 0.9f));
	TestTrue(TEXT("Within 3.5 m always"), SquadFireRules::IsSignificantlyCloser(340.f, 360.f, 0.65f));

	TestTrue(TEXT("Crit: roll 0.24 < luck 25"), SquadFireRules::IsCrit(25.f, 0.24f));
	TestFalse(TEXT("No crit: roll 0.25"), SquadFireRules::IsCrit(25.f, 0.25f));

	TestEqual(TEXT("Crouched crit elevated: 18 * 1.25 * 0.8 * 2 * 1.15 * 0.9"),
		SquadFireRules::ComputeShotDamage(18.f, EOperativeStance::Crouching, 0.8f, true, High, 0.9f), 18.f * 1.25f * 0.8f * 2.f * 1.15f * 0.9f, 0.001f);
	TestEqual(TEXT("Plain standing shot"), SquadFireRules::ComputeShotDamage(18.f, EOperativeStance::Standing, 1.f, false, Flat, 1.f), 18.f);

	TestEqual(TEXT("1 m: 1 cell"), SquadFireRules::GetDistanceCells(100.f), 1);
	TestEqual(TEXT("7.5 m: 5 cells"), SquadFireRules::GetDistanceCells(750.f), 5);
	TestEqual(TEXT("2.2 m: 1 cell"), SquadFireRules::GetDistanceCells(220.f), 1);

	const FSquadFireConfig Defaults = SquadFireRules::ConfigFromBalance(nullptr);
	TestTrue(TEXT("Default switching"), Defaults.SwitchDelay[2] == 1.2f && Defaults.SwitchRatio[1] == 0.8f);
	return true;
}

// fix/corner-peek-standoff (game designer play-test 2026-11-09, L_Gen_Depot_1): our man and a PMC fighter peeking round two corners saw each
// other (the honest sight looks at the leaned-out head) but the shot line ran to the body behind the corner - nobody fired. UE-only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadFirePeekAimPointTest, "CodexTactics.Combat.SquadFire.PeekAimPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadFirePeekAimPointTest::RunTest(const FString&)
{
	using SquadFireRules::ChooseShotAimPoint;
	// The body stays the aim point while its line is clear (an exposed head changes nothing then).
	TestTrue(TEXT("a clear body line: the body"), ChooseShotAimPoint(EShotLineHit::Clear, true, EShotLineHit::Clear) == EShotAimPoint::Body);
	TestTrue(TEXT("a clear body line, nobody peeking: the body"), ChooseShotAimPoint(EShotLineHit::Clear, false, EShotLineHit::Blocked) == EShotAimPoint::Body);
	// The bug: the body behind his corner, the head and shoulder out with a clear line - aim there.
	TestTrue(TEXT("body behind his corner, his head out with a clear line: the exposed part"),
		ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Clear) == EShotAimPoint::Exposed);
	// Corners still protect: not peeking (nothing shows), or the line to the head is blocked too -> the blocked body line stands (no shot).
	TestTrue(TEXT("behind his corner, not peeking: the body (blocked, no shot)"), ChooseShotAimPoint(EShotLineHit::Blocked, false, EShotLineHit::Clear) == EShotAimPoint::Body);
	TestTrue(TEXT("peeking, but the wall blocks the head too: the body (blocked)"), ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Blocked) == EShotAimPoint::Body);
	TestTrue(TEXT("peeking, a barricade in front of the head: the body (blocked)"), ChooseShotAimPoint(EShotLineHit::Blocked, true, EShotLineHit::Barricade) == EShotAimPoint::Body);
	// A barricade keeps its own stance rules (JudgeLine) on the body.
	TestTrue(TEXT("a barricade before the body: the body (JudgeLine decides)"), ChooseShotAimPoint(EShotLineHit::Barricade, true, EShotLineHit::Clear) == EShotAimPoint::Body);
	return true;
}

#endif
