#include "Misc/AutomationTest.h"
#include "Combat/QuickFireRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Quick fire rules (game designer request 2026-10-19, Ctrl + click on a point; UE-only, no Godot reference): the enemy a
// click means, burst size, the round's reach and what it crosses.

#define QUICK_FIRE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.QuickFire." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

QUICK_FIRE_TEST(FQuickFireTargetPickTest, "TargetPick")
bool FQuickFireTargetPickTest::RunTest(const FString&)
{
	using namespace QuickFireRules;
	const float Radius = FQuickFireConfig().EnemyPickRadiusCm;
	TestEqual(TEXT("pick radius ~1.5 m"), Radius, 150.f);
	const FVector Point(1000.f, 0.f, 0.f);
	auto Make = [](const FVector& Location, bool bVisible = true, bool bAlive = true)
	{
		FQuickFireCandidate Candidate;
		Candidate.Location = Location;
		Candidate.bVisible = bVisible;
		Candidate.bAlive = bAlive;
		return Candidate;
	};
	TestEqual(TEXT("nobody: fire at the point"), PickTarget(Point, {}, Radius), INDEX_NONE);
	TestEqual(TEXT("a visible enemy 1.2 m off the point is the target"), PickTarget(Point, { Make(FVector(1120.f, 0.f, 90.f)) }, Radius), 0);
	TestEqual(TEXT("1.6 m off: the point (area fire)"), PickTarget(Point, { Make(FVector(1160.f, 0.f, 90.f)) }, Radius), INDEX_NONE);
	TestEqual(TEXT("planar: the height of its centre does not count"), PickTarget(Point, { Make(FVector(1000.f, 100.f, 400.f)) }, Radius), 0);
	TestEqual(TEXT("the nearest visible one wins"),
		PickTarget(Point, { Make(FVector(1130.f, 0.f, 0.f)), Make(FVector(1000.f, 40.f, 0.f)), Make(FVector(900.f, 0.f, 0.f)) }, Radius), 1);
	TestEqual(TEXT("an unseen enemy is never auto-targeted, even the closest"),
		PickTarget(Point, { Make(FVector(1000.f, 0.f, 0.f), false), Make(FVector(1100.f, 0.f, 0.f)) }, Radius), 1);
	TestEqual(TEXT("only unseen ones: the point"), PickTarget(Point, { Make(FVector(1000.f, 10.f, 0.f), false) }, Radius), INDEX_NONE);
	TestEqual(TEXT("a dead one is no target"), PickTarget(Point, { Make(FVector(1000.f, 0.f, 0.f), true, false) }, Radius), INDEX_NONE);
	return true;
}

QUICK_FIRE_TEST(FQuickFireBurstTest, "BurstRounds")
bool FQuickFireBurstTest::RunTest(const FString&)
{
	using namespace QuickFireRules;
	const FQuickFireConfig Config;
	TestEqual(TEXT("knife (no ammo): no fire at a point"), GetBurstRounds(false, false, 0, Config), 0);
	TestEqual(TEXT("M16 (30 rounds): a 3-round burst"), GetBurstRounds(true, false, 30, Config), 3);
	TestEqual(TEXT("sniper rifle: one round"), GetBurstRounds(true, true, 5, Config), 1);
	TestEqual(TEXT("pistol (15): one round"), GetBurstRounds(true, false, 15, Config), 1);
	TestEqual(TEXT("clip of 20: a burst"), GetBurstRounds(true, false, 20, Config), 3);
	return true;
}

QUICK_FIRE_TEST(FQuickFireRoundEndTest, "RoundReach")
bool FQuickFireRoundEndTest::RunTest(const FString&)
{
	using namespace QuickFireRules;
	const FQuickFireConfig Config;
	const FVector Muzzle(0.f, 0.f, 140.f);
	const FVector Near(1000.f, 0.f, 0.f);
	const FVector NearEnd = ComputeRoundEnd(Muzzle, Near, 2500.f, Config);
	const float NearLength = static_cast<float>((Near - Muzzle).Size());
	TestEqual(TEXT("in range: 30 cm past the point"), static_cast<float>(FVector::Dist(Muzzle, NearEnd)), NearLength + 30.f, 0.01f);
	TestTrue(TEXT("in range: along the line to the point"), FVector::CrossProduct((NearEnd - Muzzle).GetSafeNormal(), (Near - Muzzle).GetSafeNormal()).Size() < 1.e-4f);
	const FVector FarEnd = ComputeRoundEnd(Muzzle, FVector(4000.f, 0.f, 0.f), 2500.f, Config);
	TestEqual(TEXT("beyond range: falls short at the weapon's range"), static_cast<float>(FVector::Dist(Muzzle, FarEnd)), 2500.f, 0.01f);
	TestEqual(TEXT("no direction: the point itself"), ComputeRoundEnd(Muzzle, Muzzle, 2500.f, Config), Muzzle);
	TestEqual(TEXT("area hit chance = weapon chance x 0.5"), GetAreaHitChance(0.8f, Config), 0.4f, 1.e-4f);
	TestEqual(TEXT("area hit chance clamped"), GetAreaHitChance(3.f, Config), 1.f);
	TestTrue(TEXT("spread: prone < crouched < standing"), GetSpreadDeg(2, Config) < GetSpreadDeg(1, Config) && GetSpreadDeg(1, Config) < GetSpreadDeg(0, Config));
	return true;
}

QUICK_FIRE_TEST(FQuickFireCapsuleTest, "CrossedEnemy")
bool FQuickFireCapsuleTest::RunTest(const FString&)
{
	using namespace QuickFireRules;
	auto Capsule = [](const FVector& Center)
	{
		FQuickFireCapsule Out;
		Out.Center = Center;
		Out.Radius = 40.f;
		Out.HalfHeight = 90.f;
		return Out;
	};
	const FVector Muzzle(0.f, 0.f, 140.f);
	const FVector Ground(1000.f, 0.f, 0.f); // the clicked point on the ground
	const float Tol = FQuickFireConfig().CapsuleToleranceCm;
	float Along = 0.f;
	TestEqual(TEXT("nothing in the way"), FindFirstCapsuleOnSegment(Muzzle, Ground, {}, Tol, Along), INDEX_NONE);
	TestEqual(TEXT("tolerance 20 cm"), Tol, 20.f);
	TestEqual(TEXT("an enemy standing on the point is hit"), FindFirstCapsuleOnSegment(Muzzle, Ground, { Capsule(FVector(1000.f, 0.f, 90.f)) }, Tol, Along), 0);
	TestEqual(TEXT("an enemy half-way in the line is hit"), FindFirstCapsuleOnSegment(Muzzle, Ground, { Capsule(FVector(500.f, 0.f, 90.f)) }, Tol, Along), 0);
	TestEqual(TEXT("a round 50 cm beside a capsule (radius 40) passes"),
		FindFirstCapsuleOnSegment(FVector(0.f, 0.f, 90.f), FVector(1000.f, 0.f, 90.f), { Capsule(FVector(500.f, 50.f, 90.f)) }, 0.f, Along), INDEX_NONE);
	TestEqual(TEXT("... but counts within the tolerance"),
		FindFirstCapsuleOnSegment(FVector(0.f, 0.f, 90.f), FVector(1000.f, 0.f, 90.f), { Capsule(FVector(500.f, 50.f, 90.f)) }, Tol, Along), 0);
	TestEqual(TEXT("one 1 m to the side is not"), FindFirstCapsuleOnSegment(Muzzle, Ground, { Capsule(FVector(500.f, 100.f, 90.f)) }, Tol, Along), INDEX_NONE);
	TestEqual(TEXT("one behind the point (the round stops at the ground) is not"),
		FindFirstCapsuleOnSegment(Muzzle, Ground, { Capsule(FVector(1300.f, 0.f, 90.f)) }, Tol, Along), INDEX_NONE);
	const int32 First = FindFirstCapsuleOnSegment(Muzzle, Ground, { Capsule(FVector(800.f, 0.f, 90.f)), Capsule(FVector(300.f, 0.f, 90.f)) }, Tol, Along);
	TestEqual(TEXT("the nearer of two in the line takes the round"), First, 1);
	// The closest approach to its axis (x ~300 cm along a slightly falling round), not the far one at x ~800.
	TestTrue(FString::Printf(TEXT("its distance along the round (%.0f cm)"), Along), Along > 250.f && Along < 350.f);
	return true;
}

#endif
