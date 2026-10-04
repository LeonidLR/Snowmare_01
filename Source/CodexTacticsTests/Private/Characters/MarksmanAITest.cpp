#include "Misc/AutomationTest.h"
#include "Characters/MarksmanAIRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// UE-only Marksman enemy (Gemini's spec, docs/port/TANDEM.md request 3): pure decision rules.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanRetreatTest, "CodexTactics.Marksman.RetreatAndRangeBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanRetreatTest::RunTest(const FString&)
{
	const FMarksmanConfig Config; // retreat < 12 m, band 20-35 m
	TestTrue(TEXT("11.9 m: retreat"), MarksmanAIRules::ShouldRetreat(1190.f, Config.RetreatDistance));
	TestFalse(TEXT("12 m: no retreat"), MarksmanAIRules::ShouldRetreat(1200.f, Config.RetreatDistance));
	TestTrue(TEXT("8 m -> Retreat"), MarksmanAIRules::ChooseMove(Config, 800.f, true) == EMarksmanMove::Retreat);
	TestTrue(TEXT("15 m -> BackOff"), MarksmanAIRules::ChooseMove(Config, 1500.f, true) == EMarksmanMove::BackOff);
	TestTrue(TEXT("25 m clear -> Hold"), MarksmanAIRules::ChooseMove(Config, 2500.f, true) == EMarksmanMove::Hold);
	TestTrue(TEXT("25 m blocked -> Approach"), MarksmanAIRules::ChooseMove(Config, 2500.f, false) == EMarksmanMove::Approach);
	TestTrue(TEXT("40 m -> Approach"), MarksmanAIRules::ChooseMove(Config, 4000.f, true) == EMarksmanMove::Approach);
	TestTrue(TEXT("8 m, kiting on cooldown -> Hold"), MarksmanAIRules::ChooseMove(Config, 800.f, true, false) == EMarksmanMove::Hold);
	TestTrue(TEXT("15 m, kiting on cooldown -> Hold"), MarksmanAIRules::ChooseMove(Config, 1500.f, true, false) == EMarksmanMove::Hold);
	TestTrue(TEXT("40 m, kiting on cooldown -> Approach"), MarksmanAIRules::ChooseMove(Config, 4000.f, true, false) == EMarksmanMove::Approach);
	TestTrue(TEXT("8 m behind a wall: seeks a firing position"), MarksmanAIRules::ChooseMove(Config, 800.f, false) == EMarksmanMove::Approach);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanFlankDecisionTest, "CodexTactics.Marksman.FlankWhenTargetCamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanFlankDecisionTest::RunTest(const FString&)
{
	TestFalse(TEXT("In the open"), MarksmanAIRules::ShouldFlank(false, 10.f, 4.f));
	TestFalse(TEXT("Covered 3.9 s"), MarksmanAIRules::ShouldFlank(true, 3.9f, 4.f));
	TestTrue(TEXT("Covered 4 s"), MarksmanAIRules::ShouldFlank(true, 4.f, 4.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanStanceTest, "CodexTactics.Marksman.StanceSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanStanceTest::RunTest(const FString&)
{
	TestTrue(TEXT("Moving -> stand"), MarksmanAIRules::EvaluateBestStance(true, true, true) == EOperativeStance::Standing);
	TestTrue(TEXT("Low cover -> crouch"), MarksmanAIRules::EvaluateBestStance(true, false, false) == EOperativeStance::Crouching);
	TestTrue(TEXT("Open ground -> prone"), MarksmanAIRules::EvaluateBestStance(false, false, false) == EOperativeStance::Prone);
	TestTrue(TEXT("High ground -> prone"), MarksmanAIRules::EvaluateBestStance(true, true, false) == EOperativeStance::Prone);
	const FMarksmanConfig Config;
	TestEqual(TEXT("Prone capsule = 1/3"), MarksmanAIRules::GetHalfHeight(Config, EOperativeStance::Prone),
		MarksmanAIRules::GetHalfHeight(Config, EOperativeStance::Standing) / 3.f, 0.01f);
	TestTrue(TEXT("Crouch between"), MarksmanAIRules::GetHalfHeight(Config, EOperativeStance::Crouching) < Config.StandHalfHeight
		&& MarksmanAIRules::GetHalfHeight(Config, EOperativeStance::Crouching) > Config.ProneHalfHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanFlankPointTest, "CodexTactics.Marksman.FlankDestination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanFlankPointTest::RunTest(const FString&)
{
	const FVector Target(0.f, 0.f, 0.f);
	const FVector Facing(1.f, 0.f, 0.f);
	// Marksman ahead-left of the target (+Y): the left flank point, 70 deg off the facing, 22 m out.
	const FVector Point = MarksmanAIRules::ComputeFlankDestination(FVector(2500.f, 800.f, 0.f), Target, Facing, 70.f, 2200.f);
	TestEqual(TEXT("Distance"), static_cast<float>(FVector::Dist2D(Point, Target)), 2200.f, 0.5f);
	const float Angle = (float)FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Point.GetSafeNormal2D(), Facing)));
	TestEqual(TEXT("70 deg off the facing"), Angle, 70.f, 0.1f);
	TestTrue(TEXT("Nearer side"), Point.Y > 0.f);
	// Ahead-right -> the right flank; the angle is clamped to 45-90.
	const FVector Right = MarksmanAIRules::ComputeFlankDestination(FVector(2500.f, -800.f, 0.f), Target, Facing, 20.f, 1000.f);
	TestTrue(TEXT("Right side"), Right.Y < 0.f);
	TestEqual(TEXT("Clamped to 45"), (float)FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Right.GetSafeNormal2D(), Facing))), 45.f, 0.1f);
	const FVector Wide = MarksmanAIRules::ComputeFlankDestination(FVector(0.f, 900.f, 0.f), Target, Facing, 120.f, 1000.f);
	TestEqual(TEXT("Clamped to 90"), static_cast<float>(Wide.X), 0.f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanHitChanceTest, "CodexTactics.Marksman.HitChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanHitChanceTest::RunTest(const FString&)
{
	const FMarksmanConfig Config; // base 0.6, prone x1.35, crouch x1.15
	using S = EOperativeStance;
	TestEqual(TEXT("Stand vs stand, 25 m"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Standing, 1.f, 2500.f), 0.6f, 0.001f);
	TestEqual(TEXT("Prone shooter +35 %"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Prone, S::Standing, 1.f, 2500.f), 0.81f, 0.001f);
	TestEqual(TEXT("Crouched target"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Crouching, 1.f, 2500.f), 0.48f, 0.001f);
	TestEqual(TEXT("Prone target"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Prone, 1.f, 2500.f), 0.36f, 0.001f);
	TestEqual(TEXT("Cover halves"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Standing, 0.5f, 2500.f), 0.3f, 0.001f);
	TestEqual(TEXT("Twice the band -> half"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Standing, 1.f, 7000.f), 0.3f, 0.001f);
	TestEqual(TEXT("Floor 5 %"), MarksmanAIRules::ComputeSniperHitChance(Config, S::Standing, S::Prone, 0.f, 2500.f), 0.05f, 0.001f);
	FMarksmanConfig Sharp = Config;
	Sharp.BaseAccuracy = 1.f;
	TestEqual(TEXT("Cap 95 %"), MarksmanAIRules::ComputeSniperHitChance(Sharp, S::Prone, S::Standing, 1.f, 1000.f), 0.95f, 0.001f);
	return true;
}

#endif
