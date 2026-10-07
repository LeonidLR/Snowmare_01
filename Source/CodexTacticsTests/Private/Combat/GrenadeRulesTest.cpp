#include "Misc/AutomationTest.h"
#include "Combat/GrenadeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/weapons/grenade.gd parity: calculate_effective_range, falloff, throw_to clamp / flight time, arc.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGrenadeRulesTest, "CodexTactics.Combat.Grenade.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGrenadeRulesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Standing 12 m"), GrenadeRules::EffectiveRange(1200.f, EOperativeStance::Standing), 1200.f);
	TestEqual(TEXT("Crouching 9 m"), GrenadeRules::EffectiveRange(1200.f, EOperativeStance::Crouching), 900.f);
	TestEqual(TEXT("Prone 6 m"), GrenadeRules::EffectiveRange(1200.f, EOperativeStance::Prone), 600.f);

	TestEqual(TEXT("Release: no measured time = 70 % of the clip"), GrenadeRules::ReleaseDelay(2.f, -1.f), 1.4f, 0.001f);
	TestEqual(TEXT("Release: measured time wins"), GrenadeRules::ReleaseDelay(2.042f, 1.28f), 1.28f, 0.001f);
	TestEqual(TEXT("Release: clamped to the clip length"), GrenadeRules::ReleaseDelay(1.f, 3.f), 1.f, 0.001f);
	TestEqual(TEXT("Release: no clip, measured time kept"), GrenadeRules::ReleaseDelay(0.f, 0.9f), 0.9f, 0.001f);
	TestEqual(TEXT("Release: nothing at all = 0"), GrenadeRules::ReleaseDelay(0.f, -1.f), 0.f, 0.001f);

	TestEqual(TEXT("Centre: full damage"), GrenadeRules::DamageFalloff(0.f, 400.f), 1.f);
	TestEqual(TEXT("Half radius: 75 %"), GrenadeRules::DamageFalloff(200.f, 400.f), 0.75f);
	TestEqual(TEXT("Edge: 50 %"), GrenadeRules::DamageFalloff(400.f, 400.f), 0.5f);
	TestEqual(TEXT("Beyond: still 50 %"), GrenadeRules::DamageFalloff(600.f, 400.f), 0.5f);

	const FVector Clamped = GrenadeRules::ClampTarget(FVector(0.f, 0.f, 135.f), FVector(2000.f, 0.f, 10.f), 1200.f);
	TestTrue(TEXT("Far target clamped to the range, target height kept"), Clamped.Equals(FVector(1200.f, 0.f, 10.f), 0.1f));
	TestTrue(TEXT("Near target unchanged"), GrenadeRules::ClampTarget(FVector::ZeroVector, FVector(300.f, 400.f, 0.f), 1200.f).Equals(FVector(300.f, 400.f, 0.f)));

	TestEqual(TEXT("Flight 7 m: 0.5 s at 14 m/s"), GrenadeRules::FlightDuration(700.f), 0.5f, 0.001f);
	TestEqual(TEXT("Minimum flight 0.12 s"), GrenadeRules::FlightDuration(0.f), 0.12f, 0.001f);

	const FVector Top = GrenadeRules::ArcPoint(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), 0.5f);
	TestTrue(TEXT("Arc apex 2.6 m at the middle"), Top.Equals(FVector(500.f, 0.f, 260.f), 0.1f));
	TestTrue(TEXT("Arc ends on the target"), GrenadeRules::ArcPoint(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), 1.f).Equals(FVector(1000.f, 0.f, 0.f), 0.1f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
