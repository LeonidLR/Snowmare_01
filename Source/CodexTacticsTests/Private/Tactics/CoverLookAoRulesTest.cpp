#include "Misc/AutomationTest.h"
#include "Combat/PeekSightRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverLookAoRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverLookAoRulesTest, "CodexTactics.Tactics.Cover.LookAo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverLookAoRulesTest::RunTest(const FString&)
{
	using namespace CoverLookAoRules;
	const FCoverFacingConfig Config;
	const FVector Normal(1.f, 0.f, 0.f);
	TestTrue(TEXT("straight out along the normal: 0"), FMath::IsNearlyZero(SignedYawFromNormal(Normal, FVector(1.f, 0.f, 0.f))));
	TestTrue(TEXT("to the right of the normal: +90"), FMath::IsNearlyEqual(SignedYawFromNormal(Normal, FVector(0.f, 1.f, 0.f)), 90.f, 0.01f));
	TestTrue(TEXT("to the left of the normal: -90"), FMath::IsNearlyEqual(SignedYawFromNormal(Normal, FVector(0.f, -1.f, 0.f)), -90.f, 0.01f));
	TestEqual(TEXT("a look on the normal keeps the right hand"), SideFromLook(0.f, ECoverFacing::Right, Config.CornerLookSideHysteresisDeg), ECoverFacing::Right);
	TestEqual(TEXT("a look on the normal keeps the left hand"), SideFromLook(0.f, ECoverFacing::Left, Config.CornerLookSideHysteresisDeg), ECoverFacing::Left);
	TestEqual(TEXT("inside the band, still right"), SideFromLook(-Config.CornerLookSideHysteresisDeg, ECoverFacing::Right, Config.CornerLookSideHysteresisDeg), ECoverFacing::Right);
	TestEqual(TEXT("past the band to the left: the left hand"), SideFromLook(-(Config.CornerLookSideHysteresisDeg + 1.f), ECoverFacing::Right, Config.CornerLookSideHysteresisDeg), ECoverFacing::Left);
	TestEqual(TEXT("past the band to the right: the right hand"), SideFromLook(Config.CornerLookSideHysteresisDeg + 1.f, ECoverFacing::Left, Config.CornerLookSideHysteresisDeg), ECoverFacing::Right);

	float Yaw = 120.f;
	float Pitch = -40.f;
	ClampAxes(Yaw, Pitch, 90.f, 30.f);
	TestEqual(TEXT("yaw clamped to the aim-offset range"), Yaw, 90.f, 0.01f);
	TestEqual(TEXT("pitch clamped to the aim-offset range"), Pitch, -30.f, 0.01f);
	TestEqual(TEXT("the wider axis end is the range"), AxisAbs(-180.f, 90.f, 60.f), 180.f, 0.01f);
	TestEqual(TEXT("an unset axis keeps the rifle fallback"), AxisAbs(0.f, 0.f, 60.f), 60.f, 0.01f);

	TestFalse(TEXT("a plain peek, no look: the current pose"), WantsPose(true, false));
	TestFalse(TEXT("a look away from the corner: no cover pose"), WantsPose(false, true));
	TestTrue(TEXT("a look at the corner peek: the CoverIndoor pose"), WantsPose(true, true));

	// The sight cone follows the look. Clamping the aim offset must not pull that yaw in.
	const float LookYaw = PeekSightRules::LookYawDeg(FVector(0.f, 0.f, 160.f), FVector(1.f, 0.f, 0.f), FVector(-100.f, 800.f, 160.f), 180.f);
	const float Cone = PeekSightRules::PeekConeYawDeg(true, LookYaw, 30.f);
	float ClampedYaw = Cone;
	float UnusedPitch = 0.f;
	ClampAxes(ClampedYaw, UnusedPitch, 60.f, 90.f);
	TestTrue(TEXT("the cone keeps the look past the aim-offset clamp"), FMath::Abs(Cone) > 60.f && FMath::IsNearlyEqual(Cone, LookYaw, 0.01f));
	TestTrue(TEXT("the aim offset would have stopped at its range"), FMath::IsNearlyEqual(ClampedYaw, 60.f, 0.01f));
	return true;
}

#endif
