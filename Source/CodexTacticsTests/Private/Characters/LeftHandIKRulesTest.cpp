// CodexTactics.Anim.LeftHandIK.* — the left hand onto the rifle's handguard (user-approved plan 2026-10-07; UE-only, no
// Godot reference): the grip socket in hand_r space and when the IK is on.

#include "Characters/LeftHandIKRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLeftHandIKOffsetFromSocketTest, "CodexTactics.Anim.LeftHandIK.OffsetFromSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLeftHandIKOffsetFromSocketTest::RunTest(const FString&)
{
	using namespace LeftHandIKRules;
	// BP_Operative's WeaponMesh on hand_r (loc (24.6, 4.0, 17.2), pitch 76.556 / yaw 176.436 / roll 177.565) and the m16
	// LeftHandGrip socket (-5.8, 4.4, 54.5): the grip lies where AS_Rifle_Aim's left hand is in hand_r space (measured
	// (-26.9, 9.1, -1.1)) — the hand the weapon offset was tuned for.
	const FTransform Weapon(FRotator(76.556f, 176.436f, 177.565f), FVector(24.6f, 4.0f, 17.2f));
	const FTransform Socket(FRotator(58.7f, -85.2f, -170.8f), FVector(-5.8f, 4.4f, 54.5f));
	const FTransform Grip = GripInHandSpace(Weapon, Socket);
	TestTrue(FString::Printf(TEXT("grip in hand_r space %s ~ (-26.9, 9.1, -1.1)"), *Grip.GetLocation().ToString()),
		Grip.GetLocation().Equals(FVector(-26.9f, 9.1f, -1.1f), 1.f));
	// Identity weapon offset: the socket itself; a pure translation adds.
	TestTrue(TEXT("identity weapon: the socket"), GripInHandSpace(FTransform::Identity, Socket).GetLocation().Equals(Socket.GetLocation(), 0.01f));
	TestTrue(TEXT("translated weapon"), GripInHandSpace(FTransform(FVector(10.f, 0.f, 0.f)), FTransform(FVector(0.f, 0.f, 50.f))).GetLocation()
		.Equals(FVector(10.f, 0.f, 50.f), 0.01f));
	// A weapon turned 90 deg about Z: the socket's +X goes to +Y.
	TestTrue(TEXT("rotated weapon"), GripInHandSpace(FTransform(FRotator(0.f, 90.f, 0.f)), FTransform(FVector(30.f, 0.f, 0.f))).GetLocation()
		.Equals(FVector(0.f, 30.f, 0.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLeftHandIKAlphaRulesTest, "CodexTactics.Anim.LeftHandIK.AlphaRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLeftHandIKAlphaRulesTest::RunTest(const FString&)
{
	using namespace LeftHandIKRules;
	FLeftHandIKState State;
	State.bHasGrip = true;
	TestTrue(TEXT("rifle with the grip, nothing else going on: IK"), WantsIK(State));
	auto Without = [&State](auto Mutate) { FLeftHandIKState Copy = State; Mutate(Copy); return WantsIK(Copy); };
	TestFalse(TEXT("no grip socket"), Without([](FLeftHandIKState& S) { S.bHasGrip = false; }));
	TestFalse(TEXT("disabled"), Without([](FLeftHandIKState& S) { S.bEnabled = false; }));
	TestFalse(TEXT("reloading (the hand goes to the magazine)"), Without([](FLeftHandIKState& S) { S.bReloading = true; }));
	TestFalse(TEXT("grenade throw / hit reaction"), Without([](FLeftHandIKState& S) { S.bUpperBodyAction = true; }));
	TestFalse(TEXT("vaulting"), Without([](FLeftHandIKState& S) { S.bVaulting = true; }));
	TestFalse(TEXT("dead"), Without([](FLeftHandIKState& S) { S.bDead = true; }));
	TestFalse(TEXT("weapon hidden"), Without([](FLeftHandIKState& S) { S.bWeaponVisible = false; }));
	TestFalse(TEXT("prone (crawl hand on the ground; prone aim already holds the grip)"), Without([](FLeftHandIKState& S) { S.bProne = true; }));
	// 0.15 s blend: half way after 0.075 s, full after 0.15 s, and back.
	float Alpha = StepAlpha(0.f, true, 0.075f, 0.15f);
	TestEqual(TEXT("half way in"), Alpha, 0.5f, 0.001f);
	Alpha = StepAlpha(Alpha, true, 0.1f, 0.15f);
	TestEqual(TEXT("full"), Alpha, 1.f, 0.001f);
	TestEqual(TEXT("out in 0.15 s"), StepAlpha(StepAlpha(1.f, false, 0.1f, 0.15f), false, 0.05f, 0.15f), 0.f, 0.001f);
	TestEqual(TEXT("no blend: at once"), StepAlpha(0.f, true, 0.01f, 0.f), 1.f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
