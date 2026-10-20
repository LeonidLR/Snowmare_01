// CodexTactics.Tactics.CoverWrap.* - wall-following round a cover object (game designer 2026-10-20, Tactics/CoverWrapRules): the footprint
// outline, the perimeter path offset by the slot distance (convex corners on a quarter arc, concave ones at the mitre point), the shorter way
// round, the goal's corner snap and the click verdicts. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Tactics/CoverWrapRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COVER_WRAP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.CoverWrap." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CoverWrapTestUtil
{
	/** A 3 x 3 m box round the origin. Edges: 0 = -Y face, 1 = +X face, 2 = +Y face, 3 = -X face (counter-clockwise). */
	FCoverFootprint MakeWrapBox()
	{
		return CoverWrapRules::MakeBoxFootprint(FVector2D::ZeroVector, FVector2D(1.f, 0.f), 150.f, 150.f);
	}

	/** Every 5 cm along the path: the distance from the outline stays within [MinCm, MaxCm] (outside the polygon). */
	bool StaysAtOffset(const FCoverFootprint& Footprint, const FCoverWrapPath& Path, float MinCm, float MaxCm, float& OutMin, float& OutMax)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = -1.f;
		for (float At = 0.f; At <= Path.LengthCm; At += 5.f)
		{
			const FVector2D Point = CoverWrapRules::PointAtDistance(Path.Points, At);
			const float Distance = CoverWrapRules::NearestOutlinePoint(Footprint, Point).SignedDistanceCm;
			OutMin = FMath::Min(OutMin, Distance);
			OutMax = FMath::Max(OutMax, Distance);
		}
		return OutMin >= MinCm && OutMax <= MaxCm;
	}

	float WrapStandOff(ECoverFacing Facing)
	{
		return Facing == ECoverFacing::Right ? 50.f : 30.f;
	}
}

COVER_WRAP_TEST(FCoverWrapOutlineTest, "Outline")
bool FCoverWrapOutlineTest::RunTest(const FString&)
{
	using namespace CoverWrapTestUtil;
	const FCoverFootprint Box = MakeWrapBox();
	TestTrue(TEXT("Counter-clockwise (positive area)"), CoverWrapRules::SignedArea(Box) > 0.f);
	TestTrue(TEXT("Edge 0 faces -Y"), CoverWrapRules::EdgeNormal(Box, 0).Equals(FVector2D(0.f, -1.f), 0.001f));
	TestTrue(TEXT("Edge 1 faces +X"), CoverWrapRules::EdgeNormal(Box, 1).Equals(FVector2D(1.f, 0.f), 0.001f));
	TestTrue(TEXT("All box corners are convex"), CoverWrapRules::IsConvexVertex(Box, 0) && CoverWrapRules::IsConvexVertex(Box, 2));
	// A clockwise outline is turned round.
	FCoverFootprint Clockwise;
	Clockwise.Vertices = { FVector2D(0.f, 0.f), FVector2D(0.f, 100.f), FVector2D(100.f, 100.f), FVector2D(100.f, 0.f) };
	CoverWrapRules::MakeCounterClockwise(Clockwise);
	TestTrue(TEXT("Clockwise -> counter-clockwise"), CoverWrapRules::SignedArea(Clockwise) > 0.f);
	// Signed distance: outside +, inside -.
	const FCoverOutlinePoint Out = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(0.f, -230.f));
	TestTrue(TEXT("80 cm out in front of edge 0"), Out.Edge == 0 && FMath::IsNearlyEqual(Out.SignedDistanceCm, 80.f, 0.01f));
	const FCoverOutlinePoint In = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(100.f, 0.f));
	TestTrue(TEXT("Inside (the roof): 50 cm in from edge 1"), In.Edge == 1 && FMath::IsNearlyEqual(In.SignedDistanceCm, -50.f, 0.01f));
	TestEqual(TEXT("The +X edge's along position (T)"), static_cast<float>(CoverWrapRules::NearestOutlinePoint(Box, FVector2D(200.f, 75.f)).T), 0.75f, 0.001f);
	// The edge a slot stands at: its wall point and normal.
	TestEqual(TEXT("A slot at the +X face"), CoverWrapRules::FindEdgeForWall(Box, FVector2D(150.f, 20.f), FVector2D(1.f, 0.f)), 1);
	TestEqual(TEXT("A wall point 50 cm off every face: none"), CoverWrapRules::FindEdgeForWall(Box, FVector2D(200.f, 20.f), FVector2D(1.f, 0.f)), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("A normal that agrees with no face: none"), CoverWrapRules::FindEdgeForWall(Box, FVector2D(150.f, 20.f), FVector2D(0.7f, 0.7f)), static_cast<int32>(INDEX_NONE));
	return true;
}

COVER_WRAP_TEST(FCoverWrapConvexCornerTest, "ConvexCornerPath")
bool FCoverWrapConvexCornerTest::RunTest(const FString&)
{
	using namespace CoverWrapTestUtil;
	const FCoverFootprint Box = MakeWrapBox();
	// In cover on the -Y face near its +X end, to the middle of the +X face: round one outer corner.
	const FCoverOutlinePoint From = CoverWrapRules::ProjectOntoEdge(Box, 0, FVector2D(100.f, -195.f));
	const FCoverOutlinePoint To = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(230.f, 0.f));
	const FCoverWrapPath Path = CoverWrapRules::BuildShorterPath(Box, From, To, 45.f, 2);
	TestTrue(TEXT("A valid path"), Path.IsValid());
	TestEqual(TEXT("Along +RightTangent (his right on both faces)"), Path.Direction, 1);
	TestEqual(TEXT("One outer corner, no inner one"), Path.ConvexCorners * 10 + Path.ConcaveCorners, 10);
	TestEqual(TEXT("Two faces walked along"), Path.Legs.Num(), 2);
	TestTrue(TEXT("Starts 45 cm off his face"), Path.Points[0].Equals(FVector2D(100.f, -195.f), 0.01f));
	TestTrue(TEXT("Ends 45 cm off the +X face at the click"), Path.Points.Last().Equals(FVector2D(195.f, 0.f), 0.01f));
	TestTrue(TEXT("The arc starts level with the corner on his face"), Path.Points[1].Equals(FVector2D(150.f, -195.f), 0.01f));
	TestEqual(TEXT("Start + arc start + 2 arc points + arc end + goal"), Path.Points.Num(), 6);
	TestEqual(TEXT("He holds his face up to the arc"), Path.PointEdge[0], 0);
	TestEqual(TEXT("... and the next face from the arc's start (the body turns with it)"), Path.PointEdge[1], 1);
	const float Expected = 50.f + 2.f * 45.f * FMath::Sin(PI / 12.f) * 3.f + 150.f; // the arc as its 3 chords
	TestEqual(TEXT("Length: 50 + the quarter arc (~70) + 150"), Path.LengthCm, Expected, 0.5f);
	float Min = 0.f;
	float Max = 0.f;
	TestTrue(TEXT("Hugs the object: 43..46 cm off the outline all the way (never across open ground)"), StaysAtOffset(Box, Path, 43.f, 46.f, Min, Max));
	AddInfo(FString::Printf(TEXT("off the outline: %.1f .. %.1f cm"), Min, Max));
	return true;
}

COVER_WRAP_TEST(FCoverWrapShorterWayTest, "ShorterWayRound")
bool FCoverWrapShorterWayTest::RunTest(const FString&)
{
	using namespace CoverWrapTestUtil;
	const FCoverFootprint Box = MakeWrapBox();
	const FCoverOutlinePoint From = CoverWrapRules::ProjectOntoEdge(Box, 0, FVector2D(100.f, -195.f));
	// The far side (+Y face) near its +X end: round two corners, the +X side being the shorter way.
	const FCoverOutlinePoint Far = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(120.f, 230.f));
	const FCoverWrapPath Right = CoverWrapRules::BuildPath(Box, From, Far, 1, 45.f, 2);
	const FCoverWrapPath Left = CoverWrapRules::BuildPath(Box, From, Far, -1, 45.f, 2);
	const FCoverWrapPath Shorter = CoverWrapRules::BuildShorterPath(Box, From, Far, 45.f, 2);
	TestTrue(TEXT("Both ways round two outer corners"), Right.ConvexCorners == 2 && Left.ConvexCorners == 2);
	TestTrue(TEXT("Via the +X face is shorter"), Right.LengthCm < Left.LengthCm);
	TestEqual(TEXT("The shorter way is taken"), Shorter.Direction, 1);
	TestEqual(TEXT("... its length"), Shorter.LengthCm, Right.LengthCm, 0.01f);
	float Min = 0.f;
	float Max = 0.f;
	TestTrue(TEXT("... hugging the object all the way"), StaysAtOffset(Box, Shorter, 43.f, 46.f, Min, Max));
	// The far side near its -X end: the other way round now.
	const FCoverOutlinePoint FarLeft = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(-140.f, 230.f));
	TestEqual(TEXT("Far side near the -X end: round the -X face (against the vertex order)"), CoverWrapRules::BuildShorterPath(Box, From, FarLeft, 45.f, 2).Direction, -1);
	// The same face further on: no corner, one leg.
	const FCoverOutlinePoint Along = CoverWrapRules::NearestOutlinePoint(Box, FVector2D(-100.f, -230.f));
	const FCoverWrapPath Straight = CoverWrapRules::BuildShorterPath(Box, From, Along, 45.f, 2);
	TestTrue(TEXT("The same face further on: no corner, straight along it"), Straight.CornerCount() == 0 && Straight.Legs.Num() == 1
		&& FMath::IsNearlyEqual(Straight.LengthCm, 200.f, 0.01f) && Straight.Direction == -1);
	// Behind him on the same face, the long way would go round all four corners: never chosen.
	TestTrue(TEXT("All the way round is never the shorter way"), CoverWrapRules::BuildPath(Box, From, Along, 1, 45.f, 2).ConvexCorners == 4);
	return true;
}

COVER_WRAP_TEST(FCoverWrapConcaveCornerTest, "ConcaveCornerMitre")
bool FCoverWrapConcaveCornerTest::RunTest(const FString&)
{
	using namespace CoverWrapTestUtil;
	// An L-shaped building: the inner corner at (200, 200).
	FCoverFootprint L;
	L.Vertices = { FVector2D(0.f, 0.f), FVector2D(400.f, 0.f), FVector2D(400.f, 200.f), FVector2D(200.f, 200.f), FVector2D(200.f, 400.f), FVector2D(0.f, 400.f) };
	CoverWrapRules::MakeCounterClockwise(L);
	TestFalse(TEXT("(200, 200) is an inner corner"), CoverWrapRules::IsConvexVertex(L, 3));
	TestTrue(TEXT("(400, 200) is an outer one"), CoverWrapRules::IsConvexVertex(L, 2));
	// From the face y = 200 (edge 2, facing +Y) into the inner corner and up the face x = 200 (edge 3, facing +X).
	const FCoverOutlinePoint From = CoverWrapRules::ProjectOntoEdge(L, 2, FVector2D(300.f, 245.f));
	const FCoverOutlinePoint To = CoverWrapRules::NearestOutlinePoint(L, FVector2D(260.f, 320.f));
	const FCoverWrapPath Path = CoverWrapRules::BuildShorterPath(L, From, To, 45.f, 2);
	TestEqual(TEXT("One inner corner"), Path.ConcaveCorners * 10 + Path.ConvexCorners, 10);
	TestEqual(TEXT("Start, the mitre point, the goal"), Path.Points.Num(), 3);
	TestTrue(TEXT("The mitre point lies 45 cm off both faces"), Path.Points.Num() == 3 && Path.Points[1].Equals(FVector2D(245.f, 245.f), 0.01f));
	TestEqual(TEXT("Length 55 + 75"), Path.LengthCm, 130.f, 0.01f);
	TestEqual(TEXT("The next face from the mitre point on"), Path.PointEdge.Num() == 3 ? Path.PointEdge[1] : -1, 3);
	float Min = 0.f;
	float Max = 0.f;
	TestTrue(TEXT("Never closer than 45 cm to the walls, never out across the yard"), StaysAtOffset(L, Path, 44.9f, 46.f, Min, Max));
	// Round the whole L from the inner yard to its far outer side: inner and outer corners on one path.
	const FCoverOutlinePoint Outer = CoverWrapRules::NearestOutlinePoint(L, FVector2D(-60.f, 200.f));
	const FCoverWrapPath Mixed = CoverWrapRules::BuildShorterPath(L, From, Outer, 45.f, 2);
	TestTrue(TEXT("The yard to the far outer side: corners of both kinds or outer ones only, always at the offset"), Mixed.IsValid()
		&& StaysAtOffset(L, Mixed, 43.f, 46.f, Min, Max));
	AddInfo(FString::Printf(TEXT("mixed path: %d outer, %d inner corners, %.0f cm"), Mixed.ConvexCorners, Mixed.ConcaveCorners, Mixed.LengthCm));
	return true;
}

COVER_WRAP_TEST(FCoverWrapClickRulesTest, "ClickRules")
bool FCoverWrapClickRulesTest::RunTest(const FString&)
{
	using namespace CoverWrapTestUtil;
	const FCoverFootprint Box = MakeWrapBox();
	const FCoverWrapConfig Config;
	TestEqual(TEXT("Clicks count within 1 m of the object (data-driven: AOperativeCharacter::CoverWrapMaxSurfaceDistanceCm)"), Config.MaxSurfaceDistanceCm, 100.f);
	const FVector2D Him(100.f, -195.f);
	FCoverWrapPath Path;
	ECoverFacing Facing = ECoverFacing::Left;
	bool bCorner = false;
	auto Plan = [&](const FVector2D& Click, const FCoverWrapConfig& WithConfig)
	{
		return CoverWrapRules::PlanPath(Box, Him, 0, Click, WithConfig, &WrapStandOff, Path, Facing, &bCorner);
	};
	TestEqual(TEXT("The ground 60 cm off the next face: the wrap"), static_cast<int32>(Plan(FVector2D(210.f, 0.f), Config)), static_cast<int32>(ECoverWrapVerdict::Wrap));
	TestTrue(TEXT("... round one corner, ending on the face's middle facing the way he walked (Right)"), Path.CornerCount() == 1 && !bCorner
		&& Facing == ECoverFacing::Right);
	TestEqual(TEXT("The face itself (a click on the wall): the wrap"), static_cast<int32>(Plan(FVector2D(150.f, -20.f), Config)), static_cast<int32>(ECoverWrapVerdict::Wrap));
	TestEqual(TEXT("1.5 m out in the open: no wrap (the ordinary move leaves the cover)"), static_cast<int32>(Plan(FVector2D(300.f, 0.f), Config)),
		static_cast<int32>(ECoverWrapVerdict::TooFar));
	TestEqual(TEXT("Right next to him: no wrap"), static_cast<int32>(Plan(FVector2D(130.f, -230.f), Config)), static_cast<int32>(ECoverWrapVerdict::TooShort));
	FCoverWrapConfig Off = Config;
	Off.bEnabled = false;
	TestEqual(TEXT("Switched off"), static_cast<int32>(Plan(FVector2D(210.f, 0.f), Off)), static_cast<int32>(ECoverWrapVerdict::Disabled));
	FCoverWrapConfig Short = Config;
	Short.MaxTravelCm = 150.f;
	TestEqual(TEXT("Longer than the travel limit"), static_cast<int32>(Plan(FVector2D(210.f, 0.f), Short)), static_cast<int32>(ECoverWrapVerdict::TooLong));
	TestEqual(TEXT("No outline"), static_cast<int32>(CoverWrapRules::PlanPath(FCoverFootprint(), Him, 0, FVector2D(210.f, 0.f), Config, &WrapStandOff, Path, Facing)),
		static_cast<int32>(ECoverWrapVerdict::NoOutline));
	// Near the far end of the next face: the corner slot there, facing that corner (his right on the +X face = +Y), the Right stand-off.
	TestEqual(TEXT("Near the next face's far end"), static_cast<int32>(Plan(FVector2D(220.f, 120.f), Config)), static_cast<int32>(ECoverWrapVerdict::Wrap));
	TestTrue(TEXT("... snapped to that corner, facing it"), bCorner && Facing == ECoverFacing::Right);
	TestTrue(TEXT("... the Right stand-off (50 cm) short of the corner"), Path.Points.Last().Equals(FVector2D(195.f, 100.f), 0.01f));
	// Near the near end of the next face (just round the corner): that corner, facing back towards it (Left).
	TestEqual(TEXT("Just round the corner"), static_cast<int32>(Plan(FVector2D(220.f, -110.f), Config)), static_cast<int32>(ECoverWrapVerdict::Wrap));
	TestTrue(TEXT("... the corner he came round, facing it (Left), the Left stand-off"), bCorner && Facing == ECoverFacing::Left
		&& Path.Points.Last().Equals(FVector2D(195.f, -120.f), 0.01f));
	return true;
}

#undef COVER_WRAP_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
