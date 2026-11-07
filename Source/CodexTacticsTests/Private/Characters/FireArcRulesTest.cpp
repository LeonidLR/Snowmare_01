// CodexTactics.Squad.FireArc.* - a shooter turns first and fires only within his body's / rifle's arc; prone he turns at the prone
// turn rate (bug #33, FireArcRules).

#include "Misc/AutomationTest.h"
#include "Characters/FireArcRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireArcTurnTest, "CodexTactics.Squad.FireArc.ProneTurnsSlowly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireArcTurnTest::RunTest(const FString&)
{
	using namespace FireArcRules;
	constexpr float Aim = 360.f;
	constexpr float Prone = 143.f;
	constexpr float Dt = 1.f / 60.f;
	// Prone: exactly the prone rate per frame, never more.
	TestTrue(TEXT("prone: one frame turns 143 deg/s"), FMath::IsNearlyEqual(StepAimYaw(0.f, 180.f - 0.1f, Dt, true, Aim, Prone), Prone * Dt, 0.01f));
	// A 180 deg turn prone takes over a second; standing well under half a second.
	auto SecondsTo = [](bool bProne, float Target)
	{
		float Yaw = 0.f;
		float Time = 0.f;
		while (FMath::Abs(FRotator::NormalizeAxis(Target - Yaw)) > 30.f && Time < 10.f)
		{
			Yaw = StepAimYaw(Yaw, Target, Dt, bProne, Aim, Prone);
			Time += Dt;
		}
		return Time;
	};
	const float ProneSeconds = SecondsTo(true, 179.f);
	const float StandSeconds = SecondsTo(false, 179.f);
	TestTrue(FString::Printf(TEXT("prone, into the 30 deg cone of a target behind him: %.2f s (>= 1 s)"), ProneSeconds), ProneSeconds >= 1.0f && ProneSeconds <= 1.1f);
	TestTrue(FString::Printf(TEXT("standing: %.2f s (eased, never slower than 360 deg/s)"), StandSeconds), StandSeconds < 0.45f);
	// Standing keeps the old rule: the faster of the eased and the constant step.
	const float Eased = FMath::RInterpTo(FRotator::ZeroRotator, FRotator(0.f, 90.f, 0.f), Dt, AimEaseSpeed).Yaw;
	TestTrue(TEXT("standing: never slower than the constant aim rate"), StepAimYaw(0.f, 90.f, Dt, false, Aim, Prone) >= FMath::Min(90.f, Aim * Dt) - 0.01f);
	TestTrue(TEXT("standing: eased when that is faster"), StepAimYaw(0.f, 90.f, Dt, false, Aim, Prone) >= Eased - 0.01f);
	TestTrue(TEXT("the turn takes the short way round"), StepAimYaw(170.f, -170.f, Dt, true, Aim, Prone) > 170.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireArcResidualTest, "CodexTactics.Squad.FireArc.ProneBodyArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireArcResidualTest::RunTest(const FString&)
{
	using namespace FireArcRules;
	TestEqual(TEXT("standing: the rifle / stance residual (the twist may turn the rifle off the body)"), ShotResidualDeg(10.f, 50.f, false), 10.f);
	TestEqual(TEXT("prone: the body 50 deg off - no round whatever the rifle pose says"), ShotResidualDeg(10.f, 50.f, true), 50.f);
	TestEqual(TEXT("prone: a signed body error counts by its size"), ShotResidualDeg(5.f, -40.f, true), 40.f);
	TestEqual(TEXT("prone: on target, the rifle's residual"), ShotResidualDeg(12.f, 3.f, true), 12.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
