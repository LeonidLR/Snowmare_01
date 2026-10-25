#include "Misc/AutomationTest.h"
#include "Data/CombatTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Environment/SnowInteractionRules.h"
#include "Environment/SnowStampSet.h"
#include "Materials/MaterialParameterCollection.h"

#if WITH_DEV_AUTOMATION_TESTS

// Interactive snow, phase 1 (UE-only; docs/systems/environment-weather.md section 14): the pure rules of USnowInteractionSubsystem -
// the scrolling window, world -> UV, the print stride, trail segments and quads, the per-frame cap, the vertex colour code M_SnowStamp
// decodes, the fade / fresh snow, the ground gate, the archetype tracks and the C++ stamp defaults (no asset needed). Contract checks
// the shared assets the ground materials read (RT_SnowInteraction, MPC_Environment).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionWindowTest, "CodexTactics.Environment.SnowInteraction.WindowSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionWindowTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	// Resolution: explicit values win, else Low effects -> 1024, anything higher -> 2048.
	TestEqual(TEXT("explicit 4096"), ResolveResolution(4096, 0), 4096);
	TestEqual(TEXT("explicit 1024"), ResolveResolution(1024, 3), 1024);
	TestEqual(TEXT("auto Low"), ResolveResolution(0, 0), 1024);
	TestEqual(TEXT("auto High"), ResolveResolution(0, 2), 2048);
	TestEqual(TEXT("auto Epic"), ResolveResolution(0, 3), 2048);
	TestEqual(TEXT("an odd value is auto"), ResolveResolution(1500, 2), 2048);
	TestEqual(TEXT("Low window 48 m"), GetWindowSizeCm(1024), 4800.);
	TestEqual(TEXT("High window 64 m"), GetWindowSizeCm(2048), 6400.);
	TestEqual(TEXT("Epic window 64 m"), GetWindowSizeCm(4096), 6400.);
	TestEqual(TEXT("256 texels at 2048 / 64 m = 8 m"), GetSnapStepCm(2048, 6400.), 800.);
	TestEqual(TEXT("256 texels at 1024 / 48 m = 12 m"), GetSnapStepCm(1024, 4800.), 1200.);

	// Centre: the focus + the lead along the view yaw.
	const FVector2D Centre = ComputeWindowCentre(FVector(1000., 2000., 50.), 90.f, 800.f);
	TestTrue(TEXT("lead along +Y at yaw 90"), Centre.Equals(FVector2D(1000., 2800.), 0.01));

	// Snap: whole steps, the window centre within half a step of the wanted centre.
	const double Step = 800.;
	for (const FVector2D Wanted : { FVector2D(0., 0.), FVector2D(1234.5, -987.25), FVector2D(-40123., 77001.), FVector2D(399.9, 400.1) })
	{
		const FVector2D Origin = SnapWindowOrigin(Wanted, 6400., Step);
		TestTrue(FString::Printf(TEXT("origin X on the grid (%s)"), *Wanted.ToString()), FMath::IsNearlyZero(FMath::Fmod(Origin.X, Step), 1e-6));
		TestTrue(FString::Printf(TEXT("origin Y on the grid (%s)"), *Wanted.ToString()), FMath::IsNearlyZero(FMath::Fmod(Origin.Y, Step), 1e-6));
		const FVector2D Middle = Origin + FVector2D(3200., 3200.);
		TestTrue(FString::Printf(TEXT("centre within half a step (%s)"), *Wanted.ToString()),
			FMath::Abs(Middle.X - Wanted.X) <= Step * 0.5 + 1e-6 && FMath::Abs(Middle.Y - Wanted.Y) <= Step * 0.5 + 1e-6);
	}

	// Hysteresis: small moves keep the origin, a move past 0.75 step re-snaps; no current origin -> snap.
	const FVector2D Start = SnapWindowOrigin(FVector2D(0., 0.), 6400., Step);
	TestTrue(TEXT("first placement snaps"), UpdateWindowOrigin(FVector2D(5., 5.), false, FVector2D(0., 0.), 6400., Step).Equals(Start));
	TestTrue(TEXT("a 5 m drift keeps the window"), UpdateWindowOrigin(Start, true, FVector2D(500., -500.), 6400., Step).Equals(Start));
	TestTrue(TEXT("resting on a step boundary does not flip"), UpdateWindowOrigin(Start, true, FVector2D(401., 0.), 6400., Step).Equals(Start));
	const FVector2D Moved = UpdateWindowOrigin(Start, true, FVector2D(700., 0.), 6400., Step);
	TestTrue(TEXT("past 0.75 step: one step on X"), Moved.Equals(Start + FVector2D(800., 0.)));

	// Shift: whole texels; 8 m = 256 texels at 2048 / 64 m.
	const FIntPoint Shift = ComputeShiftTexels(Start, Moved, 2048, 6400.);
	TestEqual(TEXT("shift X 256 texels"), Shift.X, 256);
	TestEqual(TEXT("shift Y 0"), Shift.Y, 0);
	TestEqual(TEXT("shift back"), ComputeShiftTexels(Moved, Start, 2048, 6400.).X, -256);
	TestEqual(TEXT("1024 / 48 m: 12 m = 256 texels"), ComputeShiftTexels(FVector2D(0., 0.), FVector2D(0., -1200.), 1024, 4800.).Y, -256);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionUVTest, "CodexTactics.Environment.SnowInteraction.WorldToUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionUVTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	const FVector2D Origin(-3200., 800.);
	TestTrue(TEXT("origin -> (0, 0)"), WorldToUV(Origin, Origin, 6400.).Equals(FVector2D(0., 0.)));
	TestTrue(TEXT("far corner -> (1, 1)"), WorldToUV(Origin + FVector2D(6400., 6400.), Origin, 6400.).Equals(FVector2D(1., 1.)));
	TestTrue(TEXT("x right, y down"), WorldToUV(FVector2D(0., 4000.), Origin, 6400.).Equals(FVector2D(0.5, 0.5)));
	const FVector2D Point(1234.5, 2345.75);
	TestTrue(TEXT("round trip"), UVToWorld(WorldToUV(Point, Origin, 6400.), Origin, 6400.).Equals(Point, 1e-6));
	// The contract: (origin X, origin Y, 1 / size, enabled); the reader computes UV = (WorldXY - xy) * z.
	const FLinearColor Param = MakeWindowParam(Origin, 6400., true);
	TestEqual(TEXT("param x = origin X"), Param.R, -3200.f);
	TestEqual(TEXT("param y = origin Y"), Param.G, 800.f);
	TestEqual(TEXT("param z = 1 / size"), Param.B, 1.f / 6400.f, 1e-9f);
	TestEqual(TEXT("param w = enabled"), Param.A, 1.f);
	TestEqual(TEXT("disabled w = 0"), MakeWindowParam(Origin, 6400., false).A, 0.f);
	const FVector2D ReaderUV((Point.X - Param.R) * Param.B, (Point.Y - Param.G) * Param.B);
	TestTrue(TEXT("the reader's UV matches"), ReaderUV.Equals(WorldToUV(Point, Origin, 6400.), 1e-5));
	TestTrue(TEXT("inside"), IsInsideWindow(FVector2D(0., 4000.), Origin, 6400.));
	TestFalse(TEXT("outside"), IsInsideWindow(FVector2D(-3300., 4000.), Origin, 6400.));
	TestFalse(TEXT("inside but in the margin"), IsInsideWindow(FVector2D(-3150., 4000.), Origin, 6400., 100.));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionStrideTest, "CodexTactics.Environment.SnowInteraction.Stride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionStrideTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	FSnowStampSpec Boot = FSnowStampTable().OperativeBoot;
	Boot.RandomYawDeg = 0.f;
	Boot.RandomScale = 0.f;
	TArray<FSnowStamp> Prints;
	FSnowStrideState Stride;
	TestEqual(TEXT("first sight: no print"), AdvanceStride(Stride, FVector2D(0., 0.), Boot, 400.f, Prints), 0);
	// 5 m along +X in 10 cm polls: the first print half a stride in, then every stride.
	for (int32 Step = 1; Step <= 50; ++Step)
	{
		AdvanceStride(Stride, FVector2D(Step * 10., 0.), Boot, 400.f, Prints);
	}
	const int32 Expected = FMath::FloorToInt32((500.f - Boot.SpacingCm * 0.5f) / Boot.SpacingCm) + 1;
	TestEqual(TEXT("one print per stride"), Prints.Num(), Expected);
	for (int32 Index = 0; Index < Prints.Num(); ++Index)
	{
		const FSnowStamp& Print = Prints[Index];
		TestEqual(FString::Printf(TEXT("print %d at its stride"), Index), Print.Centre.X, Boot.SpacingCm * 0.5 + Index * Boot.SpacingCm, 0.01);
		TestEqual(FString::Printf(TEXT("print %d alternates +-%.0f cm"), Index, Boot.SideOffsetCm), Print.Centre.Y,
			static_cast<double>((Index % 2 == 0) ? Boot.SideOffsetCm : -Boot.SideOffsetCm), 0.01);
		TestEqual(FString::Printf(TEXT("print %d faces the walk"), Index), Print.YawDeg, 0.f, 0.01f);
		TestEqual(FString::Printf(TEXT("print %d is a boot"), Index), Print.Shape, ESnowStampShape::Boot);
	}
	// The same walk in one poll gives the same prints (no dependence on the poll rate).
	TArray<FSnowStamp> OneStep;
	FSnowStrideState Single;
	AdvanceStride(Single, FVector2D(0., 0.), Boot, 1000.f, OneStep);
	AdvanceStride(Single, FVector2D(500., 0.), Boot, 1000.f, OneStep);
	TestEqual(TEXT("same count in one poll"), OneStep.Num(), Prints.Num());
	if (OneStep.Num() == Prints.Num())
	{
		for (int32 Index = 0; Index < OneStep.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("same print %d"), Index), OneStep[Index].Centre.Equals(Prints[Index].Centre, 0.01));
		}
	}
	// Walking along +Y: the right foot (+side) lies on -X (UE: X forward, Y right).
	TArray<FSnowStamp> North;
	FSnowStrideState Turned;
	AdvanceStride(Turned, FVector2D(0., 0.), Boot, 400.f, North);
	AdvanceStride(Turned, FVector2D(0., 100.), Boot, 400.f, North);
	TestEqual(TEXT("one print after a stride along +Y"), North.Num(), 1);
	if (North.Num() == 1)
	{
		TestEqual(TEXT("faces +Y"), North[0].YawDeg, 90.f, 0.01f);
		TestTrue(TEXT("side offset across the walk"), North[0].Centre.Equals(FVector2D(-Boot.SideOffsetCm, Boot.SpacingCm * 0.5), 0.01));
	}
	// A teleport restarts the tracker without a print; standing still prints nothing.
	TArray<FSnowStamp> Jump;
	TestEqual(TEXT("teleport: no print"), AdvanceStride(Turned, FVector2D(5000., 0.), Boot, 400.f, Jump), 0);
	TestEqual(TEXT("standing still"), AdvanceStride(Turned, FVector2D(5000., 0.), Boot, 400.f, Jump), 0);
	TestEqual(TEXT("nothing appended"), Jump.Num(), 0);
	// Scale (a bigger body) lengthens the stride.
	TArray<FSnowStamp> Big;
	FSnowStrideState BigStride;
	AdvanceStride(BigStride, FVector2D(0., 0.), Boot, 1000.f, Big, 2.f);
	AdvanceStride(BigStride, FVector2D(500., 0.), Boot, 1000.f, Big, 2.f);
	TestTrue(TEXT("double scale: fewer prints"), Big.Num() < Prints.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionSegmentsTest, "CodexTactics.Environment.SnowInteraction.Segments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionSegmentsTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	const FSnowStampSpec Rut = FSnowStampTable().VehicleRut;
	TArray<FSnowStamp> Segments;
	FSnowTrailState Trail;
	TestEqual(TEXT("first sight: no segment"), AdvanceTrail(Trail, FVector2D(0., 0.), Rut, 400.f, Segments), 0);
	TestEqual(TEXT("shorter than the spacing: waits"), AdvanceTrail(Trail, FVector2D(Rut.SpacingCm * 0.5, 0.), Rut, 400.f, Segments), 0);
	TestEqual(TEXT("past the spacing: one segment"), AdvanceTrail(Trail, FVector2D(Rut.SpacingCm * 1.5, 0.), Rut, 400.f, Segments), 1);
	TestEqual(TEXT("segment count"), Segments.Num(), 1);
	if (Segments.Num() == 1)
	{
		const FSnowStamp& S = Segments[0];
		TestEqual(TEXT("from the last point"), S.LengthCm, Rut.SpacingCm * 1.5f, 0.01f);
		TestTrue(TEXT("centred"), S.Centre.Equals(FVector2D(Rut.SpacingCm * 0.75, 0.), 0.01));
		TestEqual(TEXT("segment shape"), S.Shape, ESnowStampShape::Segment);
		TestEqual(TEXT("width from the spec"), S.WidthCm, Rut.WidthCm);
	}
	TestEqual(TEXT("teleport restarts"), AdvanceTrail(Trail, FVector2D(9000., 0.), Rut, 400.f, Segments), 0);

	// Quads: a segment is exactly its length (adjacent ones do not overlap) and Width / PrintFill wide; u runs along it.
	const FSnowStamp Seg = MakeSegment(Rut, FVector2D(0., 0.), FVector2D(0., 200.));
	FVector2D C[4];
	BuildStampQuad(Seg, C);
	const double HalfWidth = Rut.WidthCm * 0.5 / PrintFill;
	TestTrue(TEXT("segment back-left"), C[0].Equals(FVector2D(HalfWidth, 0.), 0.01));
	TestTrue(TEXT("segment front-left"), C[1].Equals(FVector2D(HalfWidth, 200.), 0.01));
	TestTrue(TEXT("segment front-right"), C[2].Equals(FVector2D(-HalfWidth, 200.), 0.01));
	TestTrue(TEXT("segment back-right"), C[3].Equals(FVector2D(-HalfWidth, 0.), 0.01));
	// A print gets the berm margin on both axes.
	FSnowStamp Print;
	Print.Centre = FVector2D(100., 100.);
	Print.LengthCm = 36.f;
	Print.WidthCm = 18.f;
	Print.Shape = ESnowStampShape::Boot;
	BuildStampQuad(Print, C);
	TestEqual(TEXT("print quad length = length / fill"), FVector2D::Distance(C[0], C[1]), 36. / PrintFill, 0.01);
	TestEqual(TEXT("print quad width = width / fill"), FVector2D::Distance(C[1], C[2]), 18. / PrintFill, 0.01);
	TestTrue(TEXT("print quad centred"), ((C[0] + C[2]) * 0.5).Equals(Print.Centre, 0.01));

	// Yaw from a motion; a tiny one keeps the fallback.
	TestEqual(TEXT("yaw +X"), YawFromDelta(FVector2D(10., 0.), 33.f), 0.f, 0.01f);
	TestEqual(TEXT("yaw -Y"), YawFromDelta(FVector2D(0., -10.), 33.f), -90.f, 0.01f);
	TestEqual(TEXT("no motion -> fallback"), YawFromDelta(FVector2D(0.1, 0.), 33.f), 33.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionCapTest, "CodexTactics.Environment.SnowInteraction.Cap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionCapTest::RunTest(const FString&)
{
	TArray<FSnowStamp> Stamps;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FSnowStamp& Stamp = Stamps.AddDefaulted_GetRef();
		Stamp.Centre = FVector2D(Index % 2 == 0 ? Index * 100. : -Index * 1000., 0.); // odd ones far away
		Stamp.Depth = static_cast<float>(Index);
	}
	TestEqual(TEXT("under the cap: nothing dropped"), SnowInteractionRules::CapStamps(Stamps, FVector2D::ZeroVector, 10), 0);
	TestEqual(TEXT("six dropped"), SnowInteractionRules::CapStamps(Stamps, FVector2D::ZeroVector, 4), 6);
	TestEqual(TEXT("four kept"), Stamps.Num(), 4);
	if (Stamps.Num() == 4)
	{
		// The nearest four are indices 0, 2, 4, 6 (x 0..600), in their original order.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			TestEqual(FString::Printf(TEXT("kept %d"), Index), Stamps[Index].Depth, static_cast<float>(Index * 2));
		}
	}
	TestEqual(TEXT("cap 0 drops all"), SnowInteractionRules::CapStamps(Stamps, FVector2D::ZeroVector, 0), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionEncodingTest, "CodexTactics.Environment.SnowInteraction.VertexColour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionEncodingTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	// The canvas stores FCanvasUVTri colours with ToFColor(true): the GPU must see exactly the 8-bit code.
	for (const ESnowStampShape Shape : { ESnowStampShape::Boot, ESnowStampShape::Paw, ESnowStampShape::Body, ESnowStampShape::Crater,
		ESnowStampShape::Dent, ESnowStampShape::Segment })
	{
		const FColor Stored = EncodeStampColor(Shape, 1.f, 0.5f).ToFColor(true);
		TestEqual(FString::Printf(TEXT("shape %d: R = id x 16 + 8"), static_cast<int32>(Shape)), static_cast<int32>(Stored.R), static_cast<int32>(Shape) * 16 + 8);
		TestEqual(FString::Printf(TEXT("shape %d decodes"), static_cast<int32>(Shape)), DecodeStampShape(Stored), static_cast<int32>(Shape));
	}
	for (const float Value : { 0.f, 0.3f, 0.9f, 1.4f, 2.5f, 4.f })
	{
		const FColor Stored = EncodeStampColor(ESnowStampShape::Boot, Value, Value * 0.5f).ToFColor(true);
		TestEqual(FString::Printf(TEXT("depth %.2f"), Value), DecodeStampDepth(Stored), Value, MaxChannelValue / 255.f);
		TestEqual(FString::Printf(TEXT("berm %.2f"), Value * 0.5f), DecodeStampBerm(Stored), Value * 0.5f, MaxChannelValue / 255.f);
	}
	const FColor Over = EncodeStampColor(ESnowStampShape::Boot, 9.f, -1.f).ToFColor(true);
	TestEqual(TEXT("depth clamps to 4"), DecodeStampDepth(Over), MaxChannelValue);
	TestEqual(TEXT("berm clamps to 0"), DecodeStampBerm(Over), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionFadeTest, "CodexTactics.Environment.SnowInteraction.Fade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionFadeTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	const FSnowInteractionTuning Tuning;
	TestEqual(TEXT("no time, no fade"), ComputeFadeFactor(0.f, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 1.f), 1.f);
	TestEqual(TEXT("clear: e-fold in 1 / base"), ComputeFadeFactor(1.f / Tuning.FadeBaseRate, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 0.f),
		FMath::Exp(-1.f), 1e-4f);
	const float Clear = ComputeFadeFactor(10.f, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 0.f);
	const float Snowfall = ComputeFadeFactor(10.f, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 0.6f);
	const float Blizzard = ComputeFadeFactor(10.f, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 1.f);
	TestTrue(TEXT("snowfall refills faster than clear"), Snowfall < Clear);
	TestTrue(TEXT("a blizzard refills fastest"), Blizzard < Snowfall);
	TestEqual(TEXT("refill clamps at 1"), ComputeFadeFactor(10.f, Tuning.FadeBaseRate, Tuning.FadeSnowBoost, 5.f), Blizzard, 1e-6f);
	TestEqual(TEXT("passes compose"), ComputeFadeFactor(4.f, 0.1f, 2.f, 0.5f) * ComputeFadeFactor(6.f, 0.1f, 2.f, 0.5f),
		ComputeFadeFactor(10.f, 0.1f, 2.f, 0.5f), 1e-5f);

	// 16-bit RT: a pass waits until it changes the value by MinFadeStep; a blizzard runs at the full 5 Hz.
	const float ClearRate = Tuning.FadeBaseRate;
	const float BlizzardRate = Tuning.FadeBaseRate * (1.f + Tuning.FadeSnowBoost);
	TestFalse(TEXT("clear: not every 0.2 s"), IsFadeDue(0.2f, Tuning.FadeHz, ClearRate));
	TestTrue(TEXT("clear: once enough decay accumulated"), IsFadeDue(MinFadeStep / ClearRate + 0.01f, Tuning.FadeHz, ClearRate));
	TestTrue(TEXT("blizzard: every 0.2 s"), IsFadeDue(0.2f, Tuning.FadeHz, BlizzardRate * 2.f));
	TestFalse(TEXT("never faster than FadeHz"), IsFadeDue(0.1f, Tuning.FadeHz, 100.f));

	// FreshSnow: rises while it snows, fades after; 0..1.
	float Fresh = 0.f;
	Fresh = StepFreshSnow(Fresh, 1.f, Tuning.FreshSnowRiseSeconds * 0.5f, Tuning.FreshSnowRiseSeconds, Tuning.FreshSnowFadeSeconds);
	TestEqual(TEXT("half the rise time -> 0.5"), Fresh, 0.5f, 1e-4f);
	Fresh = StepFreshSnow(Fresh, 1.f, Tuning.FreshSnowRiseSeconds, Tuning.FreshSnowRiseSeconds, Tuning.FreshSnowFadeSeconds);
	TestEqual(TEXT("caps at 1"), Fresh, 1.f);
	Fresh = StepFreshSnow(Fresh, 0.f, Tuning.FreshSnowFadeSeconds * 0.5f, Tuning.FreshSnowRiseSeconds, Tuning.FreshSnowFadeSeconds);
	TestEqual(TEXT("fades over ~10 min"), Fresh, 0.5f, 1e-4f);
	Fresh = StepFreshSnow(Fresh, 0.f, Tuning.FreshSnowFadeSeconds, Tuning.FreshSnowRiseSeconds, Tuning.FreshSnowFadeSeconds);
	TestEqual(TEXT("floors at 0"), Fresh, 0.f);
	TestEqual(TEXT("light snow rises slower"), StepFreshSnow(0.f, 0.5f, 10.f, 100.f, 600.f), 0.05f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionGatingTest, "CodexTactics.Environment.SnowInteraction.Gating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionGatingTest::RunTest(const FString&)
{
	using namespace SnowInteractionRules;
	TestTrue(TEXT("landscape always"), IsSnowGround(ESnowGroundKind::Landscape, false, false));
	TestTrue(TEXT("static mesh with any ground"), IsSnowGround(ESnowGroundKind::StaticMesh, false, true));
	TestFalse(TEXT("static mesh without any ground"), IsSnowGround(ESnowGroundKind::StaticMesh, false, false));
	TestTrue(TEXT("tagged SnowGround mesh without any ground"), IsSnowGround(ESnowGroundKind::StaticMesh, true, false));
	TestFalse(TEXT("no floor never"), IsSnowGround(ESnowGroundKind::None, true, true));
	TestFalse(TEXT("a prop / pawn never"), IsSnowGround(ESnowGroundKind::Other, false, true));
	TestTrue(TEXT("a tagged other floor"), IsSnowGround(ESnowGroundKind::Other, true, false));

	const FSnowStampTable Table;
	const FSnowStampSpec* Spec = nullptr;
	TestEqual(TEXT("Base: prints"), GetEnemyTrack(EEnemyArchetype::Base, Table, Spec), EEnemyTrack::Prints);
	TestTrue(TEXT("Base: boots"), Spec == &Table.EnemyBoot);
	TestEqual(TEXT("Hound: prints"), GetEnemyTrack(EEnemyArchetype::FrostHound, Table, Spec), EEnemyTrack::Prints);
	TestTrue(TEXT("Hound: paws"), Spec == &Table.HoundPaw && Spec->Shape == ESnowStampShape::Paw);
	TestEqual(TEXT("Cutter: prints"), GetEnemyTrack(EEnemyArchetype::Cutter, Table, Spec), EEnemyTrack::Prints);
	TestTrue(TEXT("Cutter: paws"), Spec == &Table.CutterPaw && Spec->Shape == ESnowStampShape::Paw);
	TestEqual(TEXT("Brute: prints"), GetEnemyTrack(EEnemyArchetype::Brute, Table, Spec), EEnemyTrack::Prints);
	TestTrue(TEXT("Brute: big boots"), Spec == &Table.BruteBoot && Spec->LengthCm > Table.EnemyBoot.LengthCm);
	TestEqual(TEXT("Frostbitten: a trail"), GetEnemyTrack(EEnemyArchetype::Frostbitten, Table, Spec), EEnemyTrack::Trail);
	TestTrue(TEXT("Frostbitten: drag furrow"), Spec == &Table.FrostbittenDrag && Spec->Shape == ESnowStampShape::Segment);
	TestEqual(TEXT("Cryo Drone: flies"), GetEnemyTrack(EEnemyArchetype::CryoDrone, Table, Spec), EEnemyTrack::None);
	TestEqual(TEXT("Marksman: prints"), GetEnemyTrack(EEnemyArchetype::Marksman, Table, Spec), EEnemyTrack::Prints);
	TestTrue(TEXT("hound imprint = beast"), &GetEnemyImprint(EEnemyArchetype::FrostHound, Table) == &Table.BeastImprint);
	TestTrue(TEXT("brute imprint"), &GetEnemyImprint(EEnemyArchetype::Brute, Table) == &Table.BruteImprint);
	TestTrue(TEXT("humanoid imprint"), &GetEnemyImprint(EEnemyArchetype::Spitter, Table) == &Table.EnemyImprint);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionDefaultsTest, "CodexTactics.Environment.SnowInteraction.DataDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionDefaultsTest::RunTest(const FString&)
{
	const FSnowStampTable Table;
	const FSnowInteractionTuning Tuning;
	// The user spec: boot prints alternate +-12 cm at a stride; the other kinds have their shapes.
	TestEqual(TEXT("operative boot side offset 12 cm"), Table.OperativeBoot.SideOffsetCm, 12.f);
	TestTrue(TEXT("operative stride 60..80 cm"), Table.OperativeBoot.SpacingCm >= 60.f && Table.OperativeBoot.SpacingCm <= 80.f);
	TestEqual(TEXT("boot shape"), Table.OperativeBoot.Shape, ESnowStampShape::Boot);
	TestEqual(TEXT("crawl = segment"), Table.OperativeCrawl.Shape, ESnowStampShape::Segment);
	TestTrue(TEXT("crawl furrow wider than a boot"), Table.OperativeCrawl.WidthCm > Table.OperativeBoot.WidthCm * 3.f);
	TestEqual(TEXT("knockdown = body"), Table.OperativeImprint.Shape, ESnowStampShape::Body);
	TestEqual(TEXT("death = body"), Table.OperativeDeath.Shape, ESnowStampShape::Body);
	TestEqual(TEXT("ruts = segment"), Table.VehicleRut.Shape, ESnowStampShape::Segment);
	TestEqual(TEXT("drag = segment"), Table.DragTrail.Shape, ESnowStampShape::Segment);
	TestEqual(TEXT("crater"), Table.Crater.Shape, ESnowStampShape::Crater);
	TestFalse(TEXT("craters are world-aligned"), Table.Crater.bAlignToMotion);
	TestEqual(TEXT("dent"), Table.Dent.Shape, ESnowStampShape::Dent);
	for (const FSnowStampSpec* Spec : { &Table.OperativeBoot, &Table.EnemyBoot, &Table.BruteBoot, &Table.HoundPaw, &Table.CutterPaw,
		&Table.OperativeImprint, &Table.EnemyImprint, &Table.BeastImprint, &Table.BruteImprint, &Table.Crater, &Table.Dent })
	{
		TestTrue(TEXT("print sizes positive"), Spec->LengthCm > 0.f && Spec->WidthCm > 0.f);
	}
	for (const FSnowStampSpec* Spec : { &Table.OperativeBoot, &Table.OperativeCrawl, &Table.OperativeImprint, &Table.EnemyBoot, &Table.BruteBoot,
		&Table.HoundPaw, &Table.CutterPaw, &Table.FrostbittenDrag, &Table.VehicleRut, &Table.DragTrail, &Table.Crater, &Table.Dent })
	{
		TestTrue(TEXT("depth / berm within the RT range"), Spec->Depth > 0.f && Spec->Depth <= SnowInteractionRules::MaxChannelValue
			&& Spec->Berm >= 0.f && Spec->Berm <= SnowInteractionRules::MaxChannelValue);
	}
	TestEqual(TEXT("poll 20 Hz"), Tuning.PollHz, 20.f);
	TestEqual(TEXT("fade 5 Hz"), Tuning.FadeHz, 5.f);
	TestEqual(TEXT("cap 512 stamps a flush"), Tuning.MaxStampsPerFlush, 512);
	TestEqual(TEXT("window lead 8 m"), Tuning.WindowLeadCm, 800.f);
	TestTrue(TEXT("snowfall speeds the fade"), Tuning.FadeSnowBoost > 0.f);
	TestEqual(TEXT("fresh snow fades in ~10 min"), Tuning.FreshSnowFadeSeconds, 600.f);
	// The data asset class starts from the same C++ defaults (DA_SnowStamps is created from them).
	const USnowStampSet* Defaults = GetDefault<USnowStampSet>();
	TestEqual(TEXT("asset class: boot spacing"), Defaults->Stamps.OperativeBoot.SpacingCm, Table.OperativeBoot.SpacingCm);
	TestEqual(TEXT("asset class: rut width"), Defaults->Stamps.VehicleRut.WidthCm, Table.VehicleRut.WidthCm);
	TestEqual(TEXT("asset class: poll"), Defaults->Tuning.PollHz, Tuning.PollHz);
	// Deterministic variation (the game's random stream is never used).
	const FVector2D Point(123.4, -567.8);
	TestEqual(TEXT("hash is stable"), SnowInteractionRules::HashUnit(Point, 1u), SnowInteractionRules::HashUnit(Point, 1u));
	TestNotEqual(TEXT("salts differ"), SnowInteractionRules::HashUnit(Point, 1u), SnowInteractionRules::HashUnit(Point, 2u));
	const FSnowStamp Print = SnowInteractionRules::MakePrint(Table.EnemyBoot, Point, 45.f);
	TestTrue(TEXT("yaw jitter within the spec"), FMath::Abs(Print.YawDeg - 45.f) <= Table.EnemyBoot.RandomYawDeg + 1e-3f);
	TestTrue(TEXT("size jitter within the spec"), FMath::Abs(Print.LengthCm / Table.EnemyBoot.LengthCm - 1.f) <= Table.EnemyBoot.RandomScale + 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowInteractionContractTest, "CodexTactics.Environment.SnowInteraction.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnowInteractionContractTest::RunTest(const FString&)
{
	// The assets the ground materials read (docs/systems/environment-weather.md section 14, "Contract").
	const UTextureRenderTarget2D* Target = LoadObject<UTextureRenderTarget2D>(nullptr,
		TEXT("/Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction.RT_SnowInteraction"));
	if (TestNotNull(TEXT("RT_SnowInteraction exists"), Target))
	{
		TestTrue(TEXT("RG16F (or the RGBA16F fallback)"), Target->RenderTargetFormat == RTF_RG16f || Target->RenderTargetFormat == RTF_RGBA16f);
		TestEqual(TEXT("2048 wide"), Target->SizeX, 2048);
		TestEqual(TEXT("2048 high"), Target->SizeY, 2048);
		TestEqual(TEXT("clamp X"), static_cast<int32>(Target->AddressX), static_cast<int32>(TA_Clamp));
		TestEqual(TEXT("clamp Y"), static_cast<int32>(Target->AddressY), static_cast<int32>(TA_Clamp));
	}
	const UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment"));
	if (TestNotNull(TEXT("MPC_Environment exists"), Collection))
	{
		const FCollectionVectorParameter* WindowParam = Collection->GetVectorParameterByName(TEXT("SnowTrackWindow"));
		if (TestNotNull(TEXT("vector SnowTrackWindow"), WindowParam))
		{
			TestEqual(TEXT("SnowTrackWindow defaults to 0 (disabled)"), WindowParam->DefaultValue, FLinearColor(0.f, 0.f, 0.f, 0.f));
		}
		for (const TCHAR* Name : { TEXT("SnowRefill"), TEXT("FreshSnow") })
		{
			const FCollectionScalarParameter* Scalar = Collection->GetScalarParameterByName(Name);
			if (TestNotNull(FString::Printf(TEXT("scalar %s"), Name), Scalar))
			{
				TestEqual(FString::Printf(TEXT("%s defaults to 0"), Name), Scalar->DefaultValue, 0.f);
			}
		}
		// The phase 4 parameters are still there (append-only edit).
		for (const TCHAR* Name : { TEXT("SnowAmount"), TEXT("FrostAmount"), TEXT("WindStrength"), TEXT("IsSnowing") })
		{
			TestNotNull(FString::Printf(TEXT("scalar %s kept"), Name), Collection->GetScalarParameterByName(Name));
		}
		TestNotNull(TEXT("vector WindDirection kept"), Collection->GetVectorParameterByName(TEXT("WindDirection")));
		// Every parameter keeps / gets its own valid id (the MPC script appends; the uniform layout follows the ids).
		TSet<FGuid> Ids;
		for (const FCollectionScalarParameter& Param : Collection->ScalarParameters)
		{
			Ids.Add(Param.Id);
		}
		for (const FCollectionVectorParameter& Param : Collection->VectorParameters)
		{
			Ids.Add(Param.Id);
		}
		TestEqual(TEXT("distinct ids"), Ids.Num(), Collection->ScalarParameters.Num() + Collection->VectorParameters.Num());
		TestFalse(TEXT("no empty id"), Ids.Contains(FGuid()));
	}
	for (const TCHAR* Path : { TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowStamp.M_SnowStamp"),
		TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowInteraction_Fade.M_SnowInteraction_Fade"),
		TEXT("/Game/Environment/Weather/Snow/Tracks/M_SnowTracks_Shift.M_SnowTracks_Shift") })
	{
		TestNotNull(FString::Printf(TEXT("%s exists"), Path), LoadObject<UObject>(nullptr, Path));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
