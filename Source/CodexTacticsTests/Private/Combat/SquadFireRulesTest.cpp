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

#endif
