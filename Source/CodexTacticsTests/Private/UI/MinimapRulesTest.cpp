// CodexTactics.UI.Minimap.* - minimap rules (user decisions 2026-10-18, docs/design/MINIMAP.md; UE-only, no Godot reference):
// the isometric capture projection (world -> map UV and the inverse onto a height plane, fitted to the level), the screen view
// (square pixels, zoom round the cursor, clamping, the whole image fitted), the camera footprint, clipping, floor bands (auto
// detection, data clean-up, the selected operative's floor with hysteresis, what each floor's capture hides), the fog of war
// grid (reveal, per floor, base64 round trip) and the fading enemy icon; the level JSON "minimap" block.

#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Misc/AutomationTest.h"
#include "UI/MinimapRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define MINIMAP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.UI.Minimap." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MinimapTest
{
	FMinimapIsoProjection Iso()
	{
		FMinimapIsoProjection P;
		P.Center = FVector(1000.f, 2000.f, 50.f);
		P.YawDeg = 45.f;
		P.PitchDeg = -35.26f;
		P.WidthCm = 8000.f;
		P.HeightCm = 6000.f;
		return P;
	}
}

MINIMAP_TEST(FMinimapProjectionRoundTripTest, "Projection.RoundTrip")
bool FMinimapProjectionRoundTripTest::RunTest(const FString&)
{
	const FMinimapIsoProjection P = MinimapTest::Iso();
	TestTrue(TEXT("the centre maps to the image centre"), P.WorldToMap(P.Center).Equals(FVector2D(0.5f, 0.5f), 1e-5f));
	for (const FVector& World : { FVector(0.f, 0.f, 0.f), FVector(2500.f, -800.f, 400.f), FVector(-3000.f, 4100.f, -120.f), FVector(1000.f, 2000.f, 800.f) })
	{
		const FVector2D UV = P.WorldToMap(World);
		const FVector Back = P.MapToWorldOnPlane(UV, World.Z);
		TestTrue(FString::Printf(TEXT("inverse at its height: %s -> %s -> %s"), *World.ToString(), *UV.ToString(), *Back.ToString()), Back.Equals(World, 0.05f));
	}
	// Orthographic: every point on one view ray lands on the same map point; the image axes are the view's right / up.
	const FVector Ray = P.GetForward() * 1234.f;
	TestTrue(TEXT("a point further along the view ray: same map point"), P.WorldToMap(P.Center + Ray).Equals(FVector2D(0.5f, 0.5f), 1e-5f));
	TestTrue(TEXT("half the width along the view's right = the right edge"), P.WorldToMap(P.Center + P.GetRight() * 4000.f).Equals(FVector2D(1.f, 0.5f), 1e-5f));
	TestTrue(TEXT("half the height along the view's up = the top edge"), P.WorldToMap(P.Center + P.GetUp() * 3000.f).Equals(FVector2D(0.5f, 0.f), 1e-5f));
	// Isometric at yaw 45: world +X and +Y go diagonally (opposite sides, the same height on the image); higher = up the image.
	const FVector2D Origin = P.WorldToMap(P.Center);
	const FVector2D AlongX = P.WorldToMap(P.Center + FVector(1000.f, 0.f, 0.f)) - Origin;
	const FVector2D AlongY = P.WorldToMap(P.Center + FVector(0.f, 1000.f, 0.f)) - Origin;
	TestTrue(TEXT("+X and +Y: mirrored across the image's vertical"), FMath::IsNearlyEqual(AlongX.X, -AlongY.X, 1e-5f) && FMath::IsNearlyEqual(AlongX.Y, AlongY.Y, 1e-5f));
	TestTrue(TEXT("looking along +X+Y: both go up the image (further away)"), AlongX.Y < 0.f && AlongY.Y < 0.f);
	TestTrue(TEXT("a higher point is drawn higher"), P.WorldToMap(P.Center + FVector(0.f, 0.f, 300.f)).Y < 0.5f);
	// Never rotates with the player's camera: nothing but yaw / pitch at capture time enter the projection.
	FMinimapIsoProjection Other = P;
	Other.YawDeg = 135.f;
	const FVector Probe = P.Center + FVector(1000.f, 0.f, 0.f);
	TestFalse(TEXT("another yaw = another image (the start yaw is the one kept)"), Other.WorldToMap(Probe).Equals(P.WorldToMap(Probe), 1e-4f));
	return true;
}

MINIMAP_TEST(FMinimapProjectionFitTest, "Projection.Fit")
bool FMinimapProjectionFitTest::RunTest(const FString&)
{
	const FBox Bounds(FVector(-5000.f, -3000.f, -100.f), FVector(7000.f, 4000.f, 900.f));
	const FMinimapIsoProjection P = FMinimapIsoProjection::Fit(Bounds, 45.f, -35.26f, 500.f);
	float MinU = 1.f;
	float MaxU = 0.f;
	float MinV = 1.f;
	float MaxV = 0.f;
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Point((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
		const FVector2D UV = P.WorldToMap(Point);
		MinU = FMath::Min(MinU, static_cast<float>(UV.X));
		MaxU = FMath::Max(MaxU, static_cast<float>(UV.X));
		MinV = FMath::Min(MinV, static_cast<float>(UV.Y));
		MaxV = FMath::Max(MaxV, static_cast<float>(UV.Y));
	}
	const float MarginU = 500.f / P.WidthCm;
	const float MarginV = 500.f / P.HeightCm;
	TestTrue(FString::Printf(TEXT("every corner on the image with the margin (u %.3f..%.3f, v %.3f..%.3f)"), MinU, MaxU, MinV, MaxV),
		FMath::IsNearlyEqual(MinU, MarginU, 1e-3f) && FMath::IsNearlyEqual(MaxU, 1.f - MarginU, 1e-3f)
		&& FMath::IsNearlyEqual(MinV, MarginV, 1e-3f) && FMath::IsNearlyEqual(MaxV, 1.f - MarginV, 1e-3f));

	const FMinimapIsoProjection Square = P.WithPixelAspect(2048, 1024);
	TestTrue(TEXT("pixel aspect 2:1"), FMath::IsNearlyEqual(Square.GetAspect(), 2.f, 1e-4f));
	TestTrue(TEXT("grown, never cropped"), Square.WidthCm >= P.WidthCm - 0.01f && Square.HeightCm >= P.HeightCm - 0.01f);
	FMinimapIsoProjection Wide = P;
	Wide.WidthCm = 4000.f;
	Wide.HeightCm = 1000.f;
	TestEqual(TEXT("texture 4:1 -> 2048 x 512"), Wide.ChooseTextureSize(2048), FIntPoint(2048, 512));
	Wide.WidthCm = 1000.f;
	Wide.HeightCm = 2000.f;
	TestEqual(TEXT("texture 1:2 -> 1024 x 2048"), Wide.ChooseTextureSize(2048), FIntPoint(1024, 2048));
	return true;
}

MINIMAP_TEST(FMinimapViewTest, "View.ScreenMapping")
bool FMinimapViewTest::RunTest(const FString&)
{
	const FMinimapIsoProjection P = MinimapTest::Iso(); // aspect 4:3
	const FMinimapView View = FMinimapView::Make(FVector2D(900.f, 40.f), FVector2D(400.f, 300.f), FVector2D(0.3f, 0.6f), 0.25f, P.GetAspect());
	TestTrue(TEXT("the centre UV at the rectangle centre"), View.MapToScreen(FVector2D(0.3f, 0.6f)).Equals(FVector2D(1100.f, 190.f), 1e-3f));
	for (const FVector2D& Screen : { FVector2D(900.f, 40.f), FVector2D(1234.f, 77.f), FVector2D(1300.f, 340.f) })
	{
		TestTrue(TEXT("screen -> map -> screen"), View.MapToScreen(View.ScreenToMap(Screen)).Equals(Screen, 1e-3f));
	}
	TestTrue(TEXT("inside"), View.Contains(FVector2D(1000.f, 100.f)));
	TestFalse(TEXT("outside"), View.Contains(FVector2D(899.f, 100.f)));
	// Square pixels: 10 m along the view's right and 10 m along its up are the same number of pixels.
	const FVector2D Right = View.MapToScreen(P.WorldToMap(P.Center + P.GetRight() * 1000.f)) - View.MapToScreen(P.WorldToMap(P.Center));
	const FVector2D Up = View.MapToScreen(P.WorldToMap(P.Center + P.GetUp() * 1000.f)) - View.MapToScreen(P.WorldToMap(P.Center));
	TestTrue(FString::Printf(TEXT("square pixels (%.2f vs %.2f px per 10 m)"), Right.Size(), Up.Size()), FMath::IsNearlyEqual(Right.Size(), Up.Size(), 1e-2f));

	// The whole image fits: its corners inside the rectangle, one pair on the border.
	const float Fit = FMinimapView::FitSpanU(FVector2D(800.f, 300.f), P.GetAspect());
	const FMinimapView Full = FMinimapView::Make(FVector2D(0.f, 0.f), FVector2D(800.f, 300.f), FVector2D(0.5f, 0.5f), Fit, P.GetAspect());
	TestTrue(TEXT("full map: the image's corners inside"), Full.Contains(Full.MapToScreen(FVector2D(0.f, 0.f)) + FVector2D(0.01f)) && Full.Contains(Full.MapToScreen(FVector2D(1.f, 1.f)) - FVector2D(0.01f)));
	TestTrue(TEXT("full map: fitted (the image spans the height)"), FMath::IsNearlyEqual(Full.SpanV, 1.f, 1e-4f) || FMath::IsNearlyEqual(Full.SpanU, 1.f, 1e-4f));

	// Zoom keeps the point under the cursor; a zoomed-in window stays on the image.
	const FVector2D Cursor(1000.f, 120.f);
	const FVector2D Under = View.ScreenToMap(Cursor);
	const float NewSpan = MinimapRules::StepZoomSpan(View.SpanU, -1.f, 0.05f, 1.f);
	TestTrue(TEXT("wheel up zooms in by 1.25"), FMath::IsNearlyEqual(NewSpan, 0.2f, 1e-5f));
	const FMinimapView Zoomed = MinimapRules::ZoomAround(View, Cursor, NewSpan, P.GetAspect());
	TestTrue(TEXT("zoom round the cursor: the same map point under it"), Zoomed.ScreenToMap(Cursor).Equals(Under, 1e-5f));
	TestEqual(TEXT("zoom clamped"), MinimapRules::StepZoomSpan(0.06f, -5.f, 0.05f, 1.f), 0.05f);
	TestTrue(TEXT("window clamped onto the image"), MinimapRules::ClampCenter(FVector2D(0.02f, 0.99f), 0.2f, 0.3f).Equals(FVector2D(0.1f, 0.85f), 1e-5f));
	TestTrue(TEXT("a window wider than the image is centred"), MinimapRules::ClampCenter(FVector2D(0.2f, 0.3f), 1.5f, 0.5f).Equals(FVector2D(0.5f, 0.3f), 1e-5f));
	return true;
}

MINIMAP_TEST(FMinimapFootprintClipTest, "View.FootprintAndClipping")
bool FMinimapFootprintClipTest::RunTest(const FString&)
{
	// Straight down from 10 m, 90 deg, square: a 20 x 20 m footprint.
	const TArray<FVector> Down = MinimapRules::FrustumFootprint(FVector(0.f, 0.f, 1000.f), FRotator(-90.f, 0.f, 0.f), 90.f, 1.f, 0.f, 15000.f);
	TestEqual(TEXT("four corners"), Down.Num(), 4);
	bool bSquare = Down.Num() == 4;
	for (const FVector& Corner : Down)
	{
		bSquare &= FMath::IsNearlyEqual(FMath::Abs(Corner.X), 1000.f, 0.5f) && FMath::IsNearlyEqual(FMath::Abs(Corner.Y), 1000.f, 0.5f) && FMath::IsNearlyZero(Corner.Z, 0.01f);
	}
	TestTrue(TEXT("footprint +-10 m on the ground"), bSquare);
	const FVector Sky = MinimapRules::RayToPlane(FVector(0.f, 0.f, 1000.f), FVector(1.f, 0.f, 0.2f), 0.f, 5000.f);
	TestTrue(TEXT("a ray above the horizon stops at the max distance, on the plane"), Sky.Equals(FVector(5000.f, 0.f, 0.f), 0.01f));

	// A square half outside the rectangle: cut, UVs interpolated.
	const TArray<FVector2D> Points = { FVector2D(-50.f, 0.f), FVector2D(50.f, 0.f), FVector2D(50.f, 100.f), FVector2D(-50.f, 100.f) };
	const TArray<FVector2D> UVs = { FVector2D(0.f, 0.f), FVector2D(1.f, 0.f), FVector2D(1.f, 1.f), FVector2D(0.f, 1.f) };
	TArray<FVector2D> OutPoints;
	TArray<FVector2D> OutUVs;
	MinimapRules::ClipPolygonToRect(Points, UVs, FBox2D(FVector2D(0.f, 0.f), FVector2D(200.f, 200.f)), OutPoints, OutUVs);
	bool bInside = OutPoints.Num() >= 3;
	bool bHalfUV = false;
	for (int32 Index = 0; Index < OutPoints.Num(); ++Index)
	{
		bInside &= OutPoints[Index].X >= -1e-3f;
		bHalfUV |= FMath::IsNearlyZero(OutPoints[Index].X, 1e-3f) && FMath::IsNearlyEqual(OutUVs[Index].X, 0.5f, 1e-4f);
	}
	TestTrue(TEXT("clipped to the rectangle"), bInside);
	TestTrue(TEXT("the cut edge carries u = 0.5"), bHalfUV);
	MinimapRules::ClipPolygonToRect(Points, UVs, FBox2D(FVector2D(500.f, 500.f), FVector2D(600.f, 600.f)), OutPoints, OutUVs);
	TestEqual(TEXT("entirely outside: nothing"), OutPoints.Num(), 0);

	FVector2D A(-100.f, 50.f);
	FVector2D B(300.f, 50.f);
	TestTrue(TEXT("segment crosses"), MinimapRules::ClipSegmentToRect(A, B, FBox2D(FVector2D(0.f, 0.f), FVector2D(200.f, 100.f))));
	TestTrue(TEXT("segment cut to the rectangle"), A.Equals(FVector2D(0.f, 50.f), 1e-3f) && B.Equals(FVector2D(200.f, 50.f), 1e-3f));
	FVector2D C(-100.f, 150.f);
	FVector2D D(300.f, 150.f);
	TestFalse(TEXT("segment above: none"), MinimapRules::ClipSegmentToRect(C, D, FBox2D(FVector2D(0.f, 0.f), FVector2D(200.f, 100.f))));
	return true;
}

MINIMAP_TEST(FMinimapFloorDetectTest, "Floors.Detect")
bool FMinimapFloorDetectTest::RunTest(const FString&)
{
	const MinimapRules::FFloorParams Params;
	TArray<float> Stacked;
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Stacked.Add(400.f + (Index % 5) - 2.f); // a slab at 4 m (+-2 cm)
	}
	for (float Z = 230.f; Z < 390.f; Z += 23.f)
	{
		Stacked.Add(Z); // a ramp: one sample per height
	}
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Stacked.Add(805.f); // a 3rd floor
	}
	Stacked.Add(1300.f);
	Stacked.Add(1301.f); // a loft of two samples: too small
	const TArray<float> Bases = MinimapRules::DetectFloorBases(Stacked, Params);
	TestEqual(TEXT("two upper floors (the ramp and the loft ignored)"), Bases.Num(), 2);
	if (Bases.Num() == 2)
	{
		TestTrue(FString::Printf(TEXT("2nd floor at 4 m (%.1f)"), Bases[0]), FMath::IsNearlyEqual(Bases[0], 400.f, 3.f));
		TestTrue(FString::Printf(TEXT("3rd floor at 8.05 m (%.1f)"), Bases[1]), FMath::IsNearlyEqual(Bases[1], 805.f, 1.f));
	}
	TestEqual(TEXT("nothing stacked: one floor"), MinimapRules::DetectFloorBases({}, Params).Num(), 0);
	MinimapRules::FFloorParams OneUpper = Params;
	OneUpper.MaxUpperFloors = 1;
	const TArray<float> Capped = MinimapRules::DetectFloorBases(Stacked, OneUpper);
	TestTrue(TEXT("at most MaxUpperFloors (the lowest kept)"), Capped.Num() == 1 && FMath::IsNearlyEqual(Capped[0], 400.f, 3.f));

	const TArray<float> Data = MinimapRules::NormalizeFloorBases({ 820.f, 400.f, 450.f, 1250.f, 1700.f, 2100.f }, Params);
	TestTrue(TEXT("data list: sorted, 450 (too close to 400) dropped, 3 kept"), Data == TArray<float>({ 400.f, 820.f, 1250.f }));
	return true;
}

MINIMAP_TEST(FMinimapFloorSelectTest, "Floors.Select")
bool FMinimapFloorSelectTest::RunTest(const FString&)
{
	using MinimapRules::SelectFloor;
	const MinimapRules::FFloorParams Params;
	const TArray<float> Bases = { 400.f, 800.f };
	TestEqual(TEXT("ground"), SelectFloor(0.f, 0.f, Bases, 0, Params), 0);
	TestEqual(TEXT("a hill top as high as the 2nd floor: nothing walkable below -> ground"), SelectFloor(420.f, 20.f, Bases, 0, Params), 0);
	TestEqual(TEXT("on the 2nd floor"), SelectFloor(400.f, 400.f, Bases, 0, Params), 1);
	TestEqual(TEXT("on the 3rd floor"), SelectFloor(805.f, 805.f, Bases, 1, Params), 2);
	TestEqual(TEXT("half-way up the stairs: still the ground"), SelectFloor(200.f, 200.f, Bases, 0, Params), 0);
	// Band edge 400 - 120 = 280, hysteresis 30 cm.
	TestEqual(TEXT("coming up: 300 cm not yet (edge 310)"), SelectFloor(300.f, 300.f, Bases, 0, Params), 0);
	TestEqual(TEXT("coming up: 315 cm = the 2nd floor"), SelectFloor(315.f, 315.f, Bases, 0, Params), 1);
	TestEqual(TEXT("going down: 260 cm still the 2nd floor (edge 250)"), SelectFloor(260.f, 260.f, Bases, 1, Params), 1);
	TestEqual(TEXT("going down: 240 cm = the ground"), SelectFloor(240.f, 240.f, Bases, 1, Params), 0);
	TestEqual(TEXT("one floor level: always 0"), SelectFloor(900.f, 900.f, {}, 0, Params), 0);

	using MinimapRules::HideOnFloor;
	TestTrue(TEXT("ground map: a roof 2.8 m above the ground is cut"), HideOnFloor(0, 280.f, 280.f, Bases, Params));
	TestTrue(TEXT("ground map: the 2nd floor's walls (4 m up) are cut"), HideOnFloor(0, 400.f, 400.f, Bases, Params));
	TestFalse(TEXT("ground map: a wall on the floor stays"), HideOnFloor(0, 0.f, 0.f, Bases, Params));
	TestFalse(TEXT("ground map: a lamp hanging at 1.5 m stays"), HideOnFloor(0, 150.f, 150.f, Bases, Params));
	TestFalse(TEXT("ground map on a hill: a hut 6 m up the slope stays (its bottom sits on the ground)"), HideOnFloor(0, 600.f, 0.f, Bases, Params));
	TestFalse(TEXT("2nd floor map: its slab stays"), HideOnFloor(1, 380.f, 380.f, Bases, Params));
	TestFalse(TEXT("2nd floor map: its walls stay"), HideOnFloor(1, 400.f, 400.f, Bases, Params));
	TestTrue(TEXT("2nd floor map: the roof 2.8 m above it is cut"), HideOnFloor(1, 680.f, 680.f, Bases, Params));
	TestTrue(TEXT("2nd floor map: the 3rd floor is cut"), HideOnFloor(1, 800.f, 800.f, Bases, Params));
	TestFalse(TEXT("3rd floor map: the 3rd floor's walls stay"), HideOnFloor(2, 805.f, 805.f, Bases, Params));
	TestEqual(TEXT("label ground"), MinimapRules::FloorLabel(0), FString(TEXT("GROUND")));
	TestEqual(TEXT("label 2nd floor"), MinimapRules::FloorLabel(1), FString(TEXT("FLOOR 2")));
	return true;
}

MINIMAP_TEST(FMinimapFogTest, "Fog.RevealAndPersist")
bool FMinimapFogTest::RunTest(const FString&)
{
	FMinimapFogGrid Fog;
	Fog.Init(FBox2D(FVector2D(-5000.f, -3000.f), FVector2D(5000.f, 3000.f)), 200.f, 2);
	TestEqual(TEXT("100 x 60 m in 2 m cells"), Fog.GetSize(), FIntPoint(50, 30));
	TestFalse(TEXT("unexplored = dark"), Fog.IsRevealed(0, FVector2D(0.f, 0.f)));
	const int32 First = Fog.RevealDisc(0, FVector2D(0.f, 0.f), 1500.f);
	TestTrue(FString::Printf(TEXT("a 15 m disc reveals ~pi 7.5^2 cells (%d)"), First), First >= 160 && First <= 190);
	TestTrue(TEXT("the centre is revealed"), Fog.IsRevealed(0, FVector2D(100.f, -100.f)));
	TestTrue(TEXT("14 m away revealed"), Fog.IsRevealed(0, FVector2D(1390.f, 0.f)));
	TestFalse(TEXT("17 m away not"), Fog.IsRevealed(0, FVector2D(1700.f, 0.f)));
	TestFalse(TEXT("the other floor stays dark"), Fog.IsRevealed(1, FVector2D(0.f, 0.f)));
	TestEqual(TEXT("the same disc again: nothing new"), Fog.RevealDisc(0, FVector2D(0.f, 0.f), 1500.f), 0);
	const uint32 Revision = Fog.GetRevision();
	const int32 Walked = Fog.RevealDisc(0, FVector2D(600.f, 0.f), 1500.f);
	TestTrue(TEXT("walking on reveals more"), Walked > 0 && Fog.GetRevision() != Revision);
	TestEqual(TEXT("count"), Fog.GetRevealedCount(0), First + Walked);
	TestEqual(TEXT("outside the grid: nothing"), Fog.RevealDisc(0, FVector2D(90000.f, 0.f), 1500.f), 0);
	Fog.RevealDisc(1, FVector2D(-4000.f, 2000.f), 600.f);

	// Save / load: base64 per floor into a fresh grid of the same level.
	FMinimapFogGrid Loaded;
	Loaded.Init(FBox2D(FVector2D(-5000.f, -3000.f), FVector2D(5000.f, 3000.f)), 200.f, 2);
	TestTrue(TEXT("floor 0 decoded"), Loaded.Decode(0, Fog.Encode(0)));
	TestTrue(TEXT("floor 1 decoded"), Loaded.Decode(1, Fog.Encode(1)));
	TestEqual(TEXT("floor 0 count restored"), Loaded.GetRevealedCount(0), Fog.GetRevealedCount(0));
	TestEqual(TEXT("floor 1 count restored"), Loaded.GetRevealedCount(1), Fog.GetRevealedCount(1));
	bool bSame = true;
	for (int32 Y = 0; Y < 30; ++Y)
	{
		for (int32 X = 0; X < 50; ++X)
		{
			bSame &= Loaded.IsCellRevealed(0, X, Y) == Fog.IsCellRevealed(0, X, Y) && Loaded.IsCellRevealed(1, X, Y) == Fog.IsCellRevealed(1, X, Y);
		}
	}
	TestTrue(TEXT("every cell restored"), bSame);
	FMinimapFogGrid Other;
	Other.Init(FBox2D(FVector2D(0.f, 0.f), FVector2D(3000.f, 3000.f)), 200.f, 1);
	TestFalse(TEXT("another level's fog does not fit"), Other.Decode(0, Fog.Encode(0)));
	TestEqual(TEXT("... and changed nothing"), Other.GetRevealedCount(0), 0);
	TestFalse(TEXT("garbage refused"), Loaded.Decode(0, TEXT("not base64 !!")));

	FMinimapFogGrid Huge;
	Huge.Init(FBox2D(FVector2D(0.f, 0.f), FVector2D(200000.f, 50000.f)), 200.f, 1, 512);
	TestTrue(FString::Printf(TEXT("a 2 km level: the cells grow to stay within 512 (%dx%d of %.0f cm)"), Huge.GetSize().X, Huge.GetSize().Y, Huge.GetCellCm()),
		Huge.GetSize().X <= 512 && Huge.GetCellCm() >= 390.f);
	return true;
}

MINIMAP_TEST(FMinimapEnemyIconTest, "Enemies.LastKnownFade")
bool FMinimapEnemyIconTest::RunTest(const FString&)
{
	using MinimapRules::EnemyIconAlpha;
	TestEqual(TEXT("seen: solid"), EnemyIconAlpha(true, 5.0, 12.f), 1.f);
	TestEqual(TEXT("just lost: the last known spot at 0.65"), EnemyIconAlpha(false, 0.0, 12.f), 0.65f);
	TestTrue(TEXT("half-way faded"), FMath::IsNearlyEqual(EnemyIconAlpha(false, 6.0, 12.f), 0.325f, 1e-4f));
	TestEqual(TEXT("gone after the fade"), EnemyIconAlpha(false, 13.0, 12.f), 0.f);
	TestEqual(TEXT("never seen: nothing"), EnemyIconAlpha(false, -1.0, 12.f), 0.f);
	return true;
}

MINIMAP_TEST(FMinimapLevelJsonTest, "LevelJson")
bool FMinimapLevelJsonTest::RunTest(const FString&)
{
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("parses"), LevelJsonRules::ParseLevel(
		TEXT("{\"level_id\":\"m\",\"waves\":[],\"minimap\":{\"floors_cm\":[420,850],\"bounds_cm\":[-1000,-2000,3000,4000]}}"), TEXT("m"), Level, Error));
	TestTrue(TEXT("floors"), Level.MinimapFloorsCm == TArray<float>({ 420.f, 850.f }));
	TestTrue(TEXT("bounds"), Level.MinimapBoundsCm.bIsValid && Level.MinimapBoundsCm.Min.Equals(FVector2D(-1000.f, -2000.f)) && Level.MinimapBoundsCm.Max.Equals(FVector2D(3000.f, 4000.f)));
	FLevelCombatConfig Plain;
	TestTrue(TEXT("without the block"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\":\"p\",\"waves\":[]}"), TEXT("p"), Plain, Error));
	TestTrue(TEXT("... auto floors, navmesh bounds"), Plain.MinimapFloorsCm.IsEmpty() && !Plain.MinimapBoundsCm.bIsValid);
	FLevelCombatConfig Bad;
	TestTrue(TEXT("inverted bounds"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\":\"b\",\"waves\":[],\"minimap\":{\"bounds_cm\":[5,5,1,1]}}"), TEXT("b"), Bad, Error));
	TestFalse(TEXT("... ignored"), Bad.MinimapBoundsCm.bIsValid);
	return true;
}

#endif
