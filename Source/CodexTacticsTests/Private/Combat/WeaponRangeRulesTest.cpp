#include "Misc/AutomationTest.h"
#include "Combat/WeaponRangeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Bug #39 (user 2026-11-04, UE-only): no shot beyond the weapon's max reach, the hit chance falls steeply from its effective range.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponRangeFalloffTest, "CodexTactics.Combat.WeaponRange.Falloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRangeFalloffTest::RunTest(const FString&)
{
	const FWeaponRangeFalloff Falloff; // floor 0.1, quadratic
	// M16: effective 28 m, max 60 m.
	TestEqual(TEXT("M16 20 m: inside the effective range x1"), WeaponRangeRules::HitMultiplier(2000.f, 2800.f, 6000.f, Falloff), 1.f);
	TestEqual(TEXT("M16 28 m: x1 at the effective range"), WeaponRangeRules::HitMultiplier(2800.f, 2800.f, 6000.f, Falloff), 1.f);
	// t = (50 - 28) / 32 = 0.6875 -> 1 - 0.9 x 0.4727 = 0.5746
	TestTrue(TEXT("M16 50 m: x0.575"), FMath::IsNearlyEqual(WeaponRangeRules::HitMultiplier(5000.f, 2800.f, 6000.f, Falloff), 0.5746f, 0.001f));
	TestTrue(TEXT("M16 60 m: the floor x0.1"), FMath::IsNearlyEqual(WeaponRangeRules::HitMultiplier(6000.f, 2800.f, 6000.f, Falloff), 0.1f, 0.001f));
	TestEqual(TEXT("M16 80 m: beyond the max reach x0"), WeaponRangeRules::HitMultiplier(8000.f, 2800.f, 6000.f, Falloff), 0.f);
	TestFalse(TEXT("M16 80 m: no shot"), WeaponRangeRules::IsWithinReach(8000.f, 6000.f));
	TestTrue(TEXT("M16 60 m: still a shot"), WeaponRangeRules::IsWithinReach(6000.f, 6000.f));
	// Monotonic and steep: the first half of the band keeps more than the second (quadratic).
	float Last = 1.f;
	bool bMonotonic = true;
	for (float Distance = 2800.f; Distance <= 6000.f; Distance += 100.f)
	{
		const float Now = WeaponRangeRules::HitMultiplier(Distance, 2800.f, 6000.f, Falloff);
		bMonotonic &= Now <= Last + 1.e-5f;
		Last = Now;
	}
	TestTrue(TEXT("falls monotonically from the effective range to the max"), bMonotonic);
	FWeaponRangeFalloff Linear;
	Linear.Exponent = 1.f;
	TestTrue(TEXT("linear: halfway x0.55"), FMath::IsNearlyEqual(WeaponRangeRules::HitMultiplier(4400.f, 2800.f, 6000.f, Linear), 0.55f, 0.001f));

	// Sniper rifle: effective 90 m, max 120 m.
	TestEqual(TEXT("sniper 80 m: x1"), WeaponRangeRules::HitMultiplier(8000.f, 9000.f, 12000.f, Falloff), 1.f);
	TestTrue(TEXT("sniper 105 m: x0.775"), FMath::IsNearlyEqual(WeaponRangeRules::HitMultiplier(10500.f, 9000.f, 12000.f, Falloff), 0.775f, 0.001f));
	// Pistol 15 / 25 m, shotgun 8 / 15 m.
	TestTrue(TEXT("pistol 20 m: x0.775"), FMath::IsNearlyEqual(WeaponRangeRules::HitMultiplier(2000.f, 1500.f, 2500.f, Falloff), 0.775f, 0.001f));
	TestEqual(TEXT("shotgun 16 m: no shot"), WeaponRangeRules::HitMultiplier(1600.f, 800.f, 1500.f, Falloff), 0.f);

	// No effective range (melee, the grenade) = the max: no falloff inside the reach.
	TestEqual(TEXT("no effective range: the max"), WeaponRangeRules::ResolveEffectiveCm(0.f, 1200.f), 1200.f);
	TestEqual(TEXT("an effective range past the max: the max"), WeaponRangeRules::ResolveEffectiveCm(2000.f, 1200.f), 1200.f);
	TestEqual(TEXT("no effective range: x1 up to the reach"), WeaponRangeRules::HitMultiplier(1150.f, 0.f, 1200.f, Falloff), 1.f);
	TestEqual(TEXT("no max reach: unlimited"), WeaponRangeRules::HitMultiplier(1.e6f, 2800.f, 0.f, Falloff), 1.f);

	// The stance / elevation bonus stretches the effective range, never the max reach.
	TestEqual(TEXT("prone x1.35: 28 -> 37.8 m"), WeaponRangeRules::ScaledEffectiveCm(2800.f, 6000.f, 1.35f), 3780.f);
	TestEqual(TEXT("prone + elevated x1.69 on a sniper rifle: capped at 120 m"), WeaponRangeRules::ScaledEffectiveCm(9000.f, 12000.f, 1.6875f), 12000.f);
	return true;
}

#endif
