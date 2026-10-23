#include "Misc/AutomationTest.h"
#include "Environment/SnowOverlayRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment phase 4d, the snow GROUND overlay (docs/systems/environment-weather.md §12.d, user decision 2026-10-23 «dust the current
// floors»): which meshes are ground slabs (M_SnowGroundOverlay instead of M_SnowOverlay), the ground cover sampling, the nearest-edge
// distance and the interior-zone rectangles of the ground material (the C++ mirror of its "Interior zones" HLSL).

namespace
{
	/** A slab with the given full size, cm (X / Y / Z). */
	SnowOverlayRules::FSnowTargetTraits SnowGroundSlab(float X, float Y, float Z)
	{
		SnowOverlayRules::FSnowTargetTraits Traits;
		Traits.bStaticMesh = true;
		Traits.FootprintCm = FMath::Max(X, Y);
		Traits.MinWidthCm = FMath::Min(X, Y);
		Traits.HeightCm = Z;
		Traits.BoundsRadiusCm = 0.5f * FVector(X, Y, Z).Size();
		return Traits;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowGroundClassifyTest, "CodexTactics.Environment.SnowOverlay.GroundClassify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowGroundClassifyTest::RunTest(const FString&)
{
	using SnowOverlayRules::ESnowTargetVerdict;
	const SnowOverlayRules::FSnowTargetConfig Config;

	// The shipped grounds (layout dumps 2026-10-22): the lake's SM_Template_Map_Floor slabs, the MovementTest / Frozen Factory cube floors.
	struct FGround
	{
		const TCHAR* Name;
		SnowOverlayRules::FSnowTargetTraits Traits;
	};
	SnowOverlayRules::FSnowTargetTraits LakeFloor = SnowGroundSlab(40000.f, 16000.f, 400.f);
	LakeFloor.bFloorTemplate = true;
	const FGround Grounds[] = {
		{ TEXT("lake Floor (400 m template slab)"), LakeFloor },
		{ TEXT("lake Floor2 as a plain mesh (468 m, 4 m thick)"), SnowGroundSlab(46822.f, 40000.f, 400.f) },
		{ TEXT("MovementTest floor (60 x 60 m cube, 20 cm)"), SnowGroundSlab(6000.f, 6000.f, 20.f) },
		{ TEXT("Frozen Factory compound floor (44 x 42 m)"), SnowGroundSlab(4400.f, 4200.f, 20.f) },
		{ TEXT("a 30 m flat slab"), SnowGroundSlab(3000.f, 2500.f, 20.f) },
	};
	for (const FGround& Ground : Grounds)
	{
		TestTrue(*FString::Printf(TEXT("%s is a ground shape"), Ground.Name), SnowOverlayRules::IsGroundShape(Ground.Traits, Config));
		TestEqual(*FString::Printf(TEXT("%s: object overlay says Ground"), Ground.Name), static_cast<int32>(SnowOverlayRules::Classify(Ground.Traits, Config)),
			static_cast<int32>(ESnowTargetVerdict::Ground));
		TestEqual(*FString::Printf(TEXT("%s: ground overlay accepts"), Ground.Name), static_cast<int32>(SnowOverlayRules::ClassifyGround(Ground.Traits, Config)),
			static_cast<int32>(ESnowTargetVerdict::Accept));
	}

	// Not ground: objects keep the object overlay, long fences / sky domes stay skipped.
	struct FNotGround
	{
		const TCHAR* Name;
		SnowOverlayRules::FSnowTargetTraits Traits;
		ESnowTargetVerdict ObjectVerdict;
	};
	SnowOverlayRules::FSnowTargetTraits TallTemplate = SnowGroundSlab(800.f, 800.f, 400.f);
	TallTemplate.bFloorTemplate = true;
	const FNotGround NotGrounds[] = {
		{ TEXT("a 2 m crate"), SnowGroundSlab(200.f, 200.f, 200.f), ESnowTargetVerdict::Accept },
		{ TEXT("the lake's 134 m low fence (20 cm wide)"), SnowGroundSlab(13440.f, 20.f, 120.f), ESnowTargetVerdict::TooLarge },
		{ TEXT("a 40 m compound wall"), SnowGroundSlab(4000.f, 40.f, 250.f), ESnowTargetVerdict::Accept },
		{ TEXT("an 18 m flat roof (below the ground width)"), SnowGroundSlab(1840.f, 1040.f, 20.f), ESnowTargetVerdict::Accept },
		{ TEXT("a sky dome"), SnowGroundSlab(3276800.f, 3276800.f, 3276800.f), ESnowTargetVerdict::TooLarge },
		{ TEXT("a 30 m building"), SnowGroundSlab(3000.f, 2000.f, 900.f), ESnowTargetVerdict::Accept },
		{ TEXT("a floor template stood up as a block"), TallTemplate, ESnowTargetVerdict::Accept },
	};
	for (const FNotGround& Case : NotGrounds)
	{
		TestFalse(*FString::Printf(TEXT("%s is no ground shape"), Case.Name), SnowOverlayRules::IsGroundShape(Case.Traits, Config));
		TestEqual(*FString::Printf(TEXT("%s: ground overlay says NotGround"), Case.Name), static_cast<int32>(SnowOverlayRules::ClassifyGround(Case.Traits, Config)),
			static_cast<int32>(ESnowTargetVerdict::NotGround));
		TestEqual(*FString::Printf(TEXT("%s: object verdict %s"), Case.Name, SnowOverlayRules::VerdictToString(Case.ObjectVerdict)),
			static_cast<int32>(SnowOverlayRules::Classify(Case.Traits, Config)), static_cast<int32>(Case.ObjectVerdict));
	}

	// A small floor template is ground whatever its size (it is a floor); unknown width (0) = the footprint.
	SnowOverlayRules::FSnowTargetTraits SmallTemplate = SnowGroundSlab(800.f, 800.f, 20.f);
	SmallTemplate.bFloorTemplate = true;
	TestTrue(TEXT("an 8 m floor template is ground"), SnowOverlayRules::IsGroundShape(SmallTemplate, Config));
	SnowOverlayRules::FSnowTargetTraits UnknownWidth = SnowGroundSlab(3000.f, 3000.f, 20.f);
	UnknownWidth.MinWidthCm = 0.f;
	TestTrue(TEXT("width 0 = the footprint"), SnowOverlayRules::IsGroundShape(UnknownWidth, Config));

	// The same rules as the object overlay otherwise.
	struct FRule
	{
		const TCHAR* Name;
		TFunction<void(SnowOverlayRules::FSnowTargetTraits&)> Change;
		ESnowTargetVerdict Expected;
	};
	const FRule Rules[] = {
		{ TEXT("instanced"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bInstanced = true; }, ESnowTargetVerdict::Instanced },
		{ TEXT("a pawn's"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bPawn = true; }, ESnowTargetVerdict::Pawn },
		{ TEXT("landscape"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bLandscape = true; }, ESnowTargetVerdict::Landscape },
		{ TEXT("hidden"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bVisible = false; }, ESnowTargetVerdict::Hidden },
		{ TEXT("has its own overlay"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bHasOverlay = true; }, ESnowTargetVerdict::HasOverlay },
		{ TEXT("tagged NoSnow"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bOptOut = true; }, ESnowTargetVerdict::OptOut },
		{ TEXT("translucent (ice sheet)"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bTranslucentOnly = true; }, ESnowTargetVerdict::TranslucentOnly },
		{ TEXT("Nanite"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bNanite = true; }, ESnowTargetVerdict::Nanite },
		{ TEXT("inside an interior zone"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bInterior = true; }, ESnowTargetVerdict::Interior },
		{ TEXT("under a roof"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bCovered = true; }, ESnowTargetVerdict::Covered },
		{ TEXT("not a static mesh"), [](SnowOverlayRules::FSnowTargetTraits& T) { T.bStaticMesh = false; }, ESnowTargetVerdict::NotStaticMesh },
	};
	for (const FRule& Rule : Rules)
	{
		SnowOverlayRules::FSnowTargetTraits Traits = SnowGroundSlab(6000.f, 6000.f, 20.f);
		Rule.Change(Traits);
		TestEqual(*FString::Printf(TEXT("ground %s -> %s"), Rule.Name, SnowOverlayRules::VerdictToString(Rule.Expected)),
			static_cast<int32>(SnowOverlayRules::ClassifyGround(Traits, Config)), static_cast<int32>(Rule.Expected));
	}
	SnowOverlayRules::FSnowTargetConfig NaniteFallback;
	NaniteFallback.bAllowNaniteFallback = true;
	SnowOverlayRules::FSnowTargetTraits NaniteFloor = SnowGroundSlab(6000.f, 6000.f, 20.f);
	NaniteFloor.bNanite = true;
	TestEqual(TEXT("a Nanite floor with the fallback allowed"), static_cast<int32>(SnowOverlayRules::ClassifyGround(NaniteFloor, NaniteFallback)),
		static_cast<int32>(ESnowTargetVerdict::Accept));

	// Thresholds are the config's.
	SnowOverlayRules::FSnowTargetConfig Narrow;
	Narrow.GroundMinWidthCm = 10.f;
	TestTrue(TEXT("with a 10 cm ground width a 20 cm wide 134 m strip is ground"), SnowOverlayRules::IsGroundShape(SnowGroundSlab(13440.f, 20.f, 120.f), Narrow));
	TestEqual(TEXT("verdict name"), FString(SnowOverlayRules::VerdictToString(ESnowTargetVerdict::NotGround)), FString(TEXT("NotGround")));

	// Floor templates by mesh name.
	TestTrue(TEXT("SM_Template_Map_Floor"), SnowOverlayRules::IsFloorTemplateMeshName(TEXT("SM_Template_Map_Floor")));
	TestTrue(TEXT("a copy, any case"), SnowOverlayRules::IsFloorTemplateMeshName(TEXT("sm_template_map_floor_Snow")));
	TestFalse(TEXT("the engine cube"), SnowOverlayRules::IsFloorTemplateMeshName(TEXT("Cube")));
	TestFalse(TEXT("another floor tile"), SnowOverlayRules::IsFloorTemplateMeshName(TEXT("SM_Floor_Tile")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowGroundCoverTest, "CodexTactics.Environment.SnowOverlay.GroundCover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowGroundCoverTest::RunTest(const FString&)
{
	// Five sample points: the centre and the quarter points.
	const TArray<FVector2D> Points = SnowOverlayRules::GroundCoverSamplePoints(FVector2D(-1000.0, 0.0), FVector2D(3000.0, 2000.0));
	TestEqual(TEXT("5 points"), Points.Num(), 5);
	if (Points.Num() == 5)
	{
		TestTrue(TEXT("the centre first"), Points[0].Equals(FVector2D(1000.0, 1000.0)));
		TestTrue(TEXT("a quarter point"), Points[1].Equals(FVector2D(0.0, 500.0)));
		TestTrue(TEXT("the opposite quarter point"), Points[4].Equals(FVector2D(2000.0, 1500.0)));
	}
	// Majority: an indoor floor is covered, a field with one hut is not.
	TestTrue(TEXT("5 of 5 covered"), SnowOverlayRules::IsGroundCovered(5, 5));
	TestTrue(TEXT("3 of 5 covered"), SnowOverlayRules::IsGroundCovered(3, 5));
	TestFalse(TEXT("2 of 5 (a hut on a field)"), SnowOverlayRules::IsGroundCovered(2, 5));
	TestFalse(TEXT("no samples"), SnowOverlayRules::IsGroundCovered(0, 0));

	// The nearest edge of a slab: 0 on it, the 2D gap outside (Z ignored).
	const FVector Min(0.0, 0.0, -400.0);
	const FVector Max(40000.0, 16000.0, 0.0);
	TestEqual(TEXT("on the slab"), SnowOverlayRules::DistanceSqToBox2D(FVector(20000.0, 8000.0, 500.0), Min, Max), 0.0);
	TestEqual(TEXT("3 m off its side"), SnowOverlayRules::DistanceSqToBox2D(FVector(-300.0, 8000.0, 9000.0), Min, Max), 90000.0);
	TestEqual(TEXT("off its corner (3, 4 m)"), SnowOverlayRules::DistanceSqToBox2D(FVector(40300.0, 16400.0, 0.0), Min, Max), 250000.0);

	// The nearest edge orders a 400 m slab under the view (its centre 195 m away, out of the 60 m range) before a 30 m slab 20 m away.
	const FVector Focus(0.0, 0.0, 0.0);
	TArray<SnowOverlayRules::FSnowCandidate> Candidates;
	SnowOverlayRules::FSnowCandidate Small;
	Small.Index = 0;
	Small.DistanceSq = SnowOverlayRules::DistanceSqToBox2D(Focus, FVector(2000.0, -1500.0, 0.0), FVector(5000.0, 1500.0, 0.0));
	Candidates.Add(Small);
	SnowOverlayRules::FSnowCandidate Big;
	Big.Index = 1;
	Big.DistanceSq = SnowOverlayRules::DistanceSqToBox2D(Focus, FVector(-500.0, -8000.0, 0.0), FVector(39500.0, 8000.0, 0.0));
	Candidates.Add(Big);
	const TArray<int32> Order = SnowOverlayRules::OrderCandidates(Candidates, 6000.f, 32);
	TestEqual(TEXT("both within 60 m"), Order.Num(), 2);
	if (Order.Num() == 2)
	{
		TestEqual(TEXT("the slab under the view first"), Order[0], 1);
		TestEqual(TEXT("then the one 20 m away"), Order[1], 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvironmentSnowGroundZonesTest, "CodexTactics.Environment.SnowOverlay.GroundZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnvironmentSnowGroundZonesTest::RunTest(const FString&)
{
	// A shelter 10 x 4 m (half 500 x 200), 4 m tall, centre (1000, 2000, 200), turned 90 deg: in world it spans X 800..1200, Y 1500..2500.
	const FTransform Zone(FRotator(0.f, 90.f, 0.f), FVector(1000.0, 2000.0, 200.0));
	const FVector Half(500.0, 200.0, 200.0);
	FLinearColor A, B;
	SnowOverlayRules::MakeGroundZoneParams(Zone, Half, A, B);
	TestTrue(TEXT("A = centre + half extent"), A.Equals(FLinearColor(1000.f, 2000.f, 500.f, 200.f), 0.01f));
	TestTrue(TEXT("B = cos / sin yaw, top, bottom"), B.Equals(FLinearColor(0.f, 1.f, 400.f, 0.f), 0.001f));

	const float Soft = 40.f;
	TestEqual(TEXT("the floor inside is bare"), SnowOverlayRules::GroundZoneMask(FVector(1000.0, 2400.0, 0.0), A, B, Soft), 1.f);
	TestEqual(TEXT("outside along the turned long side"), SnowOverlayRules::GroundZoneMask(FVector(1300.0, 2000.0, 0.0), A, B, Soft), 0.f);
	TestEqual(TEXT("inside along the turned long side"), SnowOverlayRules::GroundZoneMask(FVector(1000.0, 1550.0, 0.0), A, B, Soft), 1.f);
	TestEqual(TEXT("half on the edge"), SnowOverlayRules::GroundZoneMask(FVector(1200.0, 2000.0, 0.0), A, B, Soft), 0.5f, 0.001f);
	TestEqual(TEXT("a floor 1 m below the zone bottom still counts"), SnowOverlayRules::GroundZoneMask(FVector(1000.0, 2000.0, -90.0), A, B, Soft), 1.f);
	TestEqual(TEXT("far below: no"), SnowOverlayRules::GroundZoneMask(FVector(1000.0, 2000.0, -150.0), A, B, Soft), 0.f);
	TestEqual(TEXT("above the zone top (its roof): no"), SnowOverlayRules::GroundZoneMask(FVector(1000.0, 2000.0, 450.0), A, B, Soft), 0.f);
	TestEqual(TEXT("an unused slot"), SnowOverlayRules::GroundZoneMask(FVector(0.0, 0.0, 0.0), FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(1.f, 0.f, 0.f, 0.f), Soft), 0.f);
	TestEqual(TEXT("6 zones in the material"), SnowOverlayRules::GroundZoneCount, 6);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
