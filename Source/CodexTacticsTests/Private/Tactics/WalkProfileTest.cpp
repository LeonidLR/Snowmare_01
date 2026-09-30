#include "Misc/AutomationTest.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// Turn-based walk over a path: one continuous speed profile in Godot's total time (sum of tactical_step_duration),
// starting and ending at rest, so the walk animation follows the body (no running on the spot).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWalkProfileTest, "CodexTactics.Tactics.TurnBased.WalkProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWalkProfileTest::RunTest(const FString&)
{
	// Four cells of 150 cm, 0.52 s each (Godot balance.tres): 600 cm in 2.08 s.
	const float Total = 600.f;
	const float TotalTime = 2.08f;
	float Distance = 0.f;
	float Speed = 0.f;
	UTurnBasedCombatSubsystem::SampleWalkProfile(Total, 150.f, 150.f, TotalTime, 0.f, Distance, Speed);
	TestTrue(TEXT("Starts at rest"), Distance == 0.f && Speed == 0.f);
	UTurnBasedCombatSubsystem::SampleWalkProfile(Total, 150.f, 150.f, TotalTime, TotalTime, Distance, Speed);
	TestTrue(TEXT("Arrives in Godot's total time, at rest"), FMath::IsNearlyEqual(Distance, Total, 0.5f) && Speed < 1.f);

	float PreviousDistance = 0.f;
	float PreviousSpeed = 0.f;
	float MaxJump = 0.f;
	for (float Time = 0.01f; Time <= TotalTime; Time += 0.01f)
	{
		UTurnBasedCombatSubsystem::SampleWalkProfile(Total, 150.f, 150.f, TotalTime, Time, Distance, Speed);
		if (Distance + 0.001f < PreviousDistance)
		{
			AddError(FString::Printf(TEXT("Moves backwards at %.2f s"), Time));
		}
		MaxJump = FMath::Max(MaxJump, FMath::Abs(Speed - PreviousSpeed));
		PreviousDistance = Distance;
		PreviousSpeed = Speed;
	}
	// Cruise = (600 + 300) / 2.08 = 433 cm/s; the ramp adds 433 / 0.69 s * 0.01 s = ~6.3 cm/s per 10 ms at most.
	TestTrue(*FString::Printf(TEXT("No speed jumps (max %.1f cm/s per 10 ms)"), MaxJump), MaxJump < 10.f);

	// Two cells: accelerate over the first, decelerate over the second, no cruise.
	UTurnBasedCombatSubsystem::SampleWalkProfile(300.f, 150.f, 150.f, 1.04f, 0.52f, Distance, Speed);
	TestTrue(TEXT("Two cells: halfway at half time"), FMath::IsNearlyEqual(Distance, 150.f, 1.f));
	return true;
}

#endif
