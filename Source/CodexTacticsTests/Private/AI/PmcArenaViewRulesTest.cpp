#include "Misc/AutomationTest.h"
#include "AI/PmcArenaViewRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// fix/arena-clip-readability 2026-11-10: the PMC arena's clip camera framing / smoothing, team colours and spawn placement check - pure rules.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaClipViewFrameTest, "CodexTactics.AI.PmcArena.ClipView.Frame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaClipViewFrameTest::RunTest(const FString&)
{
	FPmcArenaFraming Framing;
	Framing.PitchDeg = 65.f;
	Framing.YawDeg = 0.f;
	Framing.MarginCm = 500.f;
	Framing.MinDistanceCm = 2000.f;
	Framing.MaxDistanceCm = 10000.f;
	// Two squads 40 m apart across the frame (yaw 0: the camera's right = +Y), the objective between them.
	const TArray<FVector> Points = { FVector(0.f, -2000.f, 0.f), FVector(100.f, 2000.f, 0.f), FVector(50.f, 0.f, 0.f) };
	const FPmcArenaView View = PmcArenaViewRules::FrameBox(Points, Framing);
	TestEqual(TEXT("look = the box centre (right)"), static_cast<float>(View.Look.Y), 0.f, 1.f);
	TestEqual(TEXT("look = the box centre (forward)"), static_cast<float>(View.Look.X), 50.f, 1.f);
	// Half width 2000 + 500 over tan(25 deg) = 5361 cm (the width decides: the depth is only 100 cm).
	TestEqual(TEXT("the width fits the FOV (x the perspective slack)"), View.DistanceCm, 2500.f / FMath::Tan(FMath::DegreesToRadians(25.f)) * Framing.PerspectiveSlack, 5.f);
	// Every point lies inside the view frustum of the eye.
	const FVector Eye = PmcArenaViewRules::EyeOf(View, Framing);
	const FVector Forward = FRotator(-Framing.PitchDeg, Framing.YawDeg, 0.f).Vector();
	TestEqual(TEXT("the eye looks at the look point"), static_cast<float>(FVector::DotProduct((View.Look - Eye).GetSafeNormal(), Forward)), 1.f, 0.001f);
	TestTrue(TEXT("the eye is high above (steep pitch)"), Eye.Z > 0.85f * View.DistanceCm);
	for (const FVector& Point : Points)
	{
		const FVector ToPoint = (Point - Eye).GetSafeNormal();
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ToPoint, Forward)));
		TestTrue(FString::Printf(TEXT("point %s inside the 25 deg half FOV (%.1f)"), *Point.ToCompactString(), Angle), Angle < 25.f);
	}
	// A deep box (along the view): the height extent decides through sin(pitch) and the aspect.
	const TArray<FVector> Deep = { FVector(-1500.f, 0.f, 0.f), FVector(1500.f, 0.f, 0.f) };
	const FPmcArenaView DeepView = PmcArenaViewRules::FrameBox(Deep, Framing);
	const float TanV = FMath::Tan(FMath::DegreesToRadians(25.f)) / Framing.Aspect;
	TestEqual(TEXT("the depth fits the vertical FOV"), DeepView.DistanceCm, (1500.f * FMath::Sin(FMath::DegreesToRadians(65.f)) + 500.f) / TanV * Framing.PerspectiveSlack, 5.f);
	// The clamps: one man = the minimum distance, a huge spread = the maximum.
	TestEqual(TEXT("one point: only the margin (or the minimum distance)"), PmcArenaViewRules::FrameBox({ FVector(10.f, 20.f, 30.f) }, Framing).DistanceCm,
		FMath::Max(2000.f, 500.f / TanV * Framing.PerspectiveSlack), 1.f);
	Framing.MarginCm = 0.f;
	TestEqual(TEXT("one point, no margin: the minimum distance"), PmcArenaViewRules::FrameBox({ FVector(10.f, 20.f, 30.f) }, Framing).DistanceCm, 2000.f, 0.01f);
	Framing.MarginCm = 500.f;
	TestEqual(TEXT("a huge spread: the maximum distance"), PmcArenaViewRules::FrameBox({ FVector(0.f, -20000.f, 0.f), FVector(0.f, 20000.f, 0.f) }, Framing).DistanceCm, 10000.f, 0.01f);
	// Too wide for the maximum: centred on the mean (three men together, one flanker far off), not on the box centre.
	const FPmcArenaView Wide = PmcArenaViewRules::FrameBox({ FVector(0.f, 0.f, 0.f), FVector(0.f, 100.f, 0.f), FVector(0.f, 200.f, 0.f), FVector(0.f, 40000.f, 0.f) }, Framing);
	TestEqual(TEXT("too wide: the maximum distance"), Wide.DistanceCm, 10000.f, 0.01f);
	TestEqual(TEXT("too wide: centred on the points' mean"), static_cast<float>(Wide.Look.Y), 10075.f, 1.f);
	TestEqual(TEXT("no points: the minimum distance"), PmcArenaViewRules::FrameBox({}, Framing).DistanceCm, 2000.f, 0.01f);
	TestEqual(TEXT("the look height = the points' mean"), static_cast<float>(PmcArenaViewRules::FrameBox({ FVector(0.f, 0.f, 100.f), FVector(0.f, 100.f, 400.f) }, Framing).Look.Z), 250.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaClipViewFramedFightersTest, "CodexTactics.AI.PmcArena.ClipView.FramedFighters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaClipViewFramedFightersTest::RunTest(const FString&)
{
	// Two squads 60 m apart (each man near his mates): all six kept.
	const TArray<FVector> Squads = { FVector(0.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f), FVector(0.f, 300.f, 0.f),
		FVector(6000.f, 0.f, 0.f), FVector(6200.f, 0.f, 0.f), FVector(6000.f, 300.f, 0.f) };
	const TArray<int32> Sides = { 0, 0, 0, 1, 1, 1 };
	TestEqual(TEXT("two squads far apart: all kept"), PmcArenaViewRules::FramedFighters(Squads, Sides, false, FVector::ZeroVector).Num(), 6);
	// One man on an 80 m detour, nobody within 30 m: left out.
	TArray<FVector> Flank = Squads;
	Flank[5] = FVector(8000.f, -8000.f, 0.f);
	const TArray<FVector> Kept = PmcArenaViewRules::FramedFighters(Flank, Sides, false, FVector::ZeroVector);
	TestEqual(TEXT("the lone flanker left out"), Kept.Num(), 5);
	TestFalse(TEXT("the flanker is not framed"), Kept.Contains(FVector(8000.f, -8000.f, 0.f)));
	// Near the objective he counts as part of the fight.
	TestEqual(TEXT("a lone man on the objective is kept"), PmcArenaViewRules::FramedFighters(Flank, Sides, true, FVector(8500.f, -8000.f, 0.f)).Num(), 6);
	// A side whose men are ALL lone (two survivors scattered after retreats): kept, both squads stay in the frame.
	const TArray<FVector> Scattered = { FVector(0.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f), FVector(0.f, 300.f, 0.f), FVector(9000.f, 0.f, 0.f), FVector(0.f, 9000.f, 0.f) };
	TestEqual(TEXT("a scattered side is still framed"), PmcArenaViewRules::FramedFighters(Scattered, { 0, 0, 0, 1, 1 }, false, FVector::ZeroVector).Num(), 5);
	// A side with one man near his mate and one lone: the lone one goes.
	const TArray<FVector> Mixed = { FVector(0.f, 0.f, 0.f), FVector(200.f, 0.f, 0.f), FVector(6000.f, 0.f, 0.f), FVector(6100.f, 0.f, 0.f), FVector(0.f, 9000.f, 0.f) };
	TestEqual(TEXT("a lone man of a framed side goes"), PmcArenaViewRules::FramedFighters(Mixed, { 0, 0, 1, 1, 1 }, false, FVector::ZeroVector).Num(), 4);
	TestEqual(TEXT("nobody: nothing"), PmcArenaViewRules::FramedFighters({}, {}, false, FVector::ZeroVector).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaClipViewYawTest, "CodexTactics.AI.PmcArena.ClipView.YawAcross",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaClipViewYawTest::RunTest(const FString&)
{
	// A at the origin, B along +X: the camera looks along -Y, its right = +X, so B is on the right of the frame.
	const float Yaw = PmcArenaViewRules::YawAcross(FVector::ZeroVector, FVector(5000.f, 0.f, 0.f));
	const FVector Right = FRotator(0.f, Yaw, 0.f).RotateVector(FVector::RightVector);
	TestEqual(TEXT("the camera's right runs A -> B"), static_cast<float>(FVector::DotProduct(Right, FVector(1.f, 0.f, 0.f))), 1.f, 0.001f);
	TestEqual(TEXT("same spot: yaw 0"), PmcArenaViewRules::YawAcross(FVector(1.f, 2.f, 3.f), FVector(1.f, 2.f, 500.f)), 0.f, 0.001f);
	// FrameBox with that yaw: the A-B spread is the frame's width.
	FPmcArenaFraming Framing;
	Framing.YawDeg = Yaw;
	Framing.MarginCm = 0.f;
	Framing.PerspectiveSlack = 1.f;
	Framing.MaxDistanceCm = 100000.f;
	const FPmcArenaView View = PmcArenaViewRules::FrameBox({ FVector::ZeroVector, FVector(5000.f, 0.f, 0.f) }, Framing);
	TestEqual(TEXT("the spread lies across the frame"), View.DistanceCm, FMath::Max(Framing.MinDistanceCm, 2500.f / FMath::Tan(FMath::DegreesToRadians(25.f))), 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaClipViewSmoothTest, "CodexTactics.AI.PmcArena.ClipView.Smooth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaClipViewSmoothTest::RunTest(const FString&)
{
	TestEqual(TEXT("one half-life = half way"), PmcArenaViewRules::SmoothAlpha(0.6f, 0.6f), 0.5f, 0.0001f);
	TestEqual(TEXT("no half-life = a snap"), PmcArenaViewRules::SmoothAlpha(0.016f, 0.f), 1.f, 0.0001f);
	TestEqual(TEXT("no time = no step"), PmcArenaViewRules::SmoothAlpha(0.f, 0.6f), 0.f, 0.0001f);
	// Frame-rate independent: 30 steps of 1/30 s = one step of 1 s.
	FPmcArenaView Current;
	Current.Look = FVector::ZeroVector;
	Current.DistanceCm = 3000.f;
	FPmcArenaView Target;
	Target.Look = FVector(1000.f, 0.f, 0.f);
	Target.DistanceCm = 6000.f;
	FPmcArenaView Stepped = Current;
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Stepped = PmcArenaViewRules::Smooth(Stepped, Target, 1.f / 30.f);
	}
	const FPmcArenaView Once = PmcArenaViewRules::Smooth(Current, Target, 1.f);
	TestEqual(TEXT("look: 30 small steps = one big"), static_cast<float>(Stepped.Look.X), static_cast<float>(Once.Look.X), 0.5f);
	TestEqual(TEXT("zoom: 30 small steps = one big"), Stepped.DistanceCm, Once.DistanceCm, 0.5f);
	TestTrue(TEXT("no overshoot"), Stepped.Look.X < 1000.f && Stepped.DistanceCm < 6000.f);
	// Zooming out (a man leaving the frame) is faster than zooming in.
	const float Out = PmcArenaViewRules::Smooth(Current, Target, 0.5f).DistanceCm - Current.DistanceCm;
	FPmcArenaView Far = Current;
	Far.DistanceCm = 6000.f;
	FPmcArenaView Near = Current;
	Near.DistanceCm = 3000.f;
	const float In = Far.DistanceCm - PmcArenaViewRules::Smooth(Far, Near, 0.5f).DistanceCm;
	TestEqual(TEXT("zoom out: half way in its 0.5 s half-life"), Out, 1500.f, 1.f);
	TestTrue(TEXT("zoom in slower than zoom out"), In < Out);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaTeamColorsTest, "CodexTactics.AI.PmcArena.TeamColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaTeamColorsTest::RunTest(const FString&)
{
	const FLinearColor A = PmcArenaViewRules::TeamColor(0);
	const FLinearColor B = PmcArenaViewRules::TeamColor(1);
	TestTrue(TEXT("team A is red"), A.R > 0.9f && A.G < 0.1f && A.B < 0.1f);
	TestTrue(TEXT("team B is green"), B.G > 0.9f && B.R < 0.1f && B.B < 0.1f);
	TestTrue(TEXT("both visible (alpha)"), A.A > 0.5f && B.A > 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaSpawnPlacementTest, "CodexTactics.AI.PmcArena.SpawnPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaSpawnPlacementTest::RunTest(const FString&)
{
	TestTrue(TEXT("a slot beside the spawn, reached straight"), PmcArenaViewRules::IsSpawnSpotUsable(true, 160.f, 150.f));
	TestTrue(TEXT("a small bend round a crate"), PmcArenaViewRules::IsSpawnSpotUsable(true, 500.f, 300.f));
	TestTrue(TEXT("round a crate next to the spawn (the Depot log: 697 cm for 213 cm)"), PmcArenaViewRules::IsSpawnSpotUsable(true, 697.f, 213.f));
	TestFalse(TEXT("the Depot fence (seed 986789898: 6648 cm for 261 cm)"), PmcArenaViewRules::IsSpawnSpotUsable(true, 6648.f, 261.f));
	TestFalse(TEXT("behind a fence: 3 m by air, 40 m round the fence's end"), PmcArenaViewRules::IsSpawnSpotUsable(true, 4000.f, 300.f));
	TestFalse(TEXT("no full path from the spawn"), PmcArenaViewRules::IsSpawnSpotUsable(true, -1.f, 150.f));
	TestFalse(TEXT("the objective unreachable"), PmcArenaViewRules::IsSpawnSpotUsable(false, 150.f, 150.f));
	TestTrue(TEXT("on the spawn itself"), PmcArenaViewRules::IsSpawnSpotUsable(true, 0.f, 0.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
