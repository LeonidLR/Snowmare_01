#include "Misc/AutomationTest.h"
#include "AI/PmcZoneCoverRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Smart enemies T-08 slice 2: the pure rules of the arena's zone cover (PmcZoneCoverRules.h) - the approach direction from the attackers' nav path, the
// block layout inside the objective zone, the footprint tests, the JSON. The world half (ground snap, overlap / spawn skips, the navmesh wait) is the
// arena's sanity seeds'.

namespace PmcZoneCoverTest
{
	TSharedPtr<FJsonObject> ParseZoneCoverJson(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Json);
		return Json;
	}

	FPmcZoneCoverBlock MakeBlock(const FVector& Centre, float YawDeg, float HalfDepth, float HalfLength)
	{
		FPmcZoneCoverBlock Block;
		Block.Centre = Centre;
		Block.YawDeg = YawDeg;
		Block.HalfExtent = FVector(HalfDepth, HalfLength, 55.f);
		return Block;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcZoneCoverApproachTest, "CodexTactics.AI.PmcZoneCover.Approach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcZoneCoverApproachTest::RunTest(const FString&)
{
	const FVector Centre(0.f, 0.f, 0.f);
	bool bFromPath = false;
	// A straight path from the west: it enters the 6 m zone at (-600, 0).
	{
		const TArray<FVector> Path = { FVector(-2000.f, 0.f, 0.f), FVector(-1000.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f) };
		const FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Path, FVector(5000.f, 0.f, 0.f), &bFromPath);
		TestTrue(TEXT("straight path: from the path"), bFromPath);
		TestEqual(TEXT("straight path: dir x"), Dir.X, -1.0, 0.0001);
		TestEqual(TEXT("straight path: dir y"), Dir.Y, 0.0, 0.0001);
	}
	// A path that passes through the zone with no point inside it: the segment's own crossing at (-331.7, 500).
	{
		const TArray<FVector> Path = { FVector(-2000.f, 500.f, 0.f), FVector(2000.f, 500.f, 0.f) };
		const FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Path, FVector(0.f, -5000.f, 0.f), &bFromPath);
		const FVector2D Want = FVector2D(-FMath::Sqrt(600.0 * 600.0 - 500.0 * 500.0), 500.0).GetSafeNormal();
		TestTrue(TEXT("crossing segment: from the path"), bFromPath);
		TestEqual(TEXT("crossing segment: dir x"), Dir.X, Want.X, 0.0001);
		TestEqual(TEXT("crossing segment: dir y"), Dir.Y, Want.Y, 0.0001);
	}
	// The FIRST entry wins (a path that enters from the south, leaves and comes back from the east).
	{
		const TArray<FVector> Path = { FVector(0.f, -2000.f, 0.f), FVector(0.f, -100.f, 0.f), FVector(2000.f, -100.f, 0.f), FVector(0.f, 0.f, 0.f) };
		const FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Path, FVector(5000.f, 0.f, 0.f), &bFromPath);
		TestEqual(TEXT("first entry: from the south"), Dir.Y, -1.0, 0.0001);
	}
	// A bent path whose last leg enters: the bearing of that entry, not of the spawn.
	{
		const TArray<FVector> Path = { FVector(-3000.f, -3000.f, 0.f), FVector(0.f, -3000.f, 0.f), FVector(0.f, 0.f, 0.f) };
		const FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Path, Path[0], &bFromPath);
		TestTrue(TEXT("bent path: from the path"), bFromPath);
		TestEqual(TEXT("bent path: enters from the south, not the south-west spawn"), Dir.Y, -1.0, 0.0001);
	}
	// No entry: the direction to the spawn.
	{
		const TArray<FVector> Never = { FVector(-2000.f, 1000.f, 0.f), FVector(2000.f, 1000.f, 0.f) };
		FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Never, FVector(0.f, 2000.f, 0.f), &bFromPath);
		TestFalse(TEXT("never enters: the spawn"), bFromPath);
		TestEqual(TEXT("never enters: toward the spawn"), Dir.Y, 1.0, 0.0001);
		const TArray<FVector> Inside = { FVector(100.f, 0.f, 0.f), FVector(-2000.f, 0.f, 0.f) };
		Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Inside, FVector(0.f, -2000.f, 0.f), &bFromPath);
		TestFalse(TEXT("starts inside: the spawn"), bFromPath);
		TestEqual(TEXT("starts inside: toward the spawn"), Dir.Y, -1.0, 0.0001);
		Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, TArray<FVector>(), FVector(-2000.f, 0.f, 0.f), &bFromPath);
		TestFalse(TEXT("no path: the spawn"), bFromPath);
		TestEqual(TEXT("no path: toward the spawn"), Dir.X, -1.0, 0.0001);
		Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, TArray<FVector>(), Centre, &bFromPath);
		TestEqual(TEXT("spawn on the centre: +X"), Dir.X, 1.0, 0.0001);
	}
	// Heights do not matter (planar).
	{
		const TArray<FVector> Path = { FVector(-2000.f, 0.f, 900.f), FVector(0.f, 0.f, -300.f) };
		const FVector2D Dir = PmcZoneCover::ApproachDirection(Centre, 600.f, Path, FVector(5000.f, 0.f, 0.f), &bFromPath);
		TestTrue(TEXT("planar: from the path"), bFromPath);
		TestEqual(TEXT("planar: from the west"), Dir.X, -1.0, 0.0001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcZoneCoverLayoutTest, "CodexTactics.AI.PmcZoneCover.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcZoneCoverLayoutTest::RunTest(const FString&)
{
	const FPmcZoneCoverConfig Config; // 3 blocks, 3.5 m / 4.0 m at +-60 deg, 200 x 40 x 110 cm, margin 20 cm
	const FVector Centre(1000.f, 2000.f, 50.f);
	// The default layout in a 6 m zone, approach from +X.
	{
		TArray<FString> Dropped;
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 600.f, Config, &Dropped);
		TestEqual(TEXT("6 m zone: three blocks"), Blocks.Num(), 3);
		TestEqual(TEXT("6 m zone: none dropped"), Dropped.Num(), 0);
		if (Blocks.Num() == 3)
		{
			TestEqual(TEXT("slot 0 on the approach bearing"), Blocks[0].Slot, 0);
			TestTrue(TEXT("slot 0 3.5 m out toward the attackers"), Blocks[0].Centre.Equals(FVector(1350.f, 2000.f, 50.f), 0.01f));
			TestEqual(TEXT("slot 0 faces the approach"), Blocks[0].YawDeg, 0.f, 0.01f);
			TestEqual(TEXT("slot 0 distance"), Blocks[0].DistCm, 350.f, 0.01f);
			TestFalse(TEXT("slot 0 not pulled in"), Blocks[0].bPulledIn);
			TestTrue(TEXT("slot 1 at +60 deg, 4 m"), Blocks[1].Centre.Equals(FVector(1200.f, 2000.f + 400.f * FMath::Sin(FMath::DegreesToRadians(60.f)), 50.f), 0.05f));
			TestTrue(TEXT("slot 2 at -60 deg, 4 m"), Blocks[2].Centre.Equals(FVector(1200.f, 2000.f - 400.f * FMath::Sin(FMath::DegreesToRadians(60.f)), 50.f), 0.05f));
			TestEqual(TEXT("the side blocks face the approach too (side_yaw_follow 0)"), Blocks[1].YawDeg, 0.f, 0.01f);
			TestTrue(TEXT("half sizes: depth 20, length 100, height 55"), Blocks[0].HalfExtent.Equals(FVector(20.f, 100.f, 55.f), 0.01f));
			for (const FPmcZoneCoverBlock& Block : Blocks)
			{
				TestTrue(*FString::Printf(TEXT("slot %d fully inside the zone"), Block.Slot), PmcZoneCover::IsInsideZone(Block, Centre, 600.f, Config.EdgeMarginCm));
			}
			TestFalse(TEXT("the blocks do not overlap (0-1)"), PmcZoneCover::FootprintsOverlap(Blocks[0], Blocks[1]));
			TestFalse(TEXT("the blocks do not overlap (1-2)"), PmcZoneCover::FootprintsOverlap(Blocks[1], Blocks[2]));
		}
	}
	// Rotated: approach from +Y - slot 0 at (0, 3.5 m), facing 90 deg.
	{
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(0.0, 2.0), 600.f, Config);
		TestEqual(TEXT("rotated: three blocks"), Blocks.Num(), 3);
		if (Blocks.Num() > 0)
		{
			TestTrue(TEXT("rotated: slot 0 north"), Blocks[0].Centre.Equals(FVector(1000.f, 2350.f, 50.f), 0.05f));
			TestEqual(TEXT("rotated: slot 0 faces north"), Blocks[0].YawDeg, 90.f, 0.01f);
		}
	}
	// side_yaw_follow 1: the side blocks face out along their own bearing.
	{
		FPmcZoneCoverConfig Follow = Config;
		Follow.SideYawFollow = 1.f;
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 600.f, Follow);
		TestEqual(TEXT("follow: three blocks"), Blocks.Num(), 3);
		if (Blocks.Num() == 3)
		{
			TestEqual(TEXT("follow: slot 1 faces +60"), Blocks[1].YawDeg, 60.f, 0.01f);
			TestEqual(TEXT("follow: slot 2 faces -60"), Blocks[2].YawDeg, -60.f, 0.01f);
			TestEqual(TEXT("follow: slot 0 unchanged"), Blocks[0].YawDeg, 0.f, 0.01f);
		}
	}
	// A 4 m zone: every block pulled in until it fits (limit 3.8 m to the farthest corner).
	{
		TArray<FString> Dropped;
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 400.f, Config, &Dropped);
		TestEqual(TEXT("4 m zone: three blocks"), Blocks.Num(), 3);
		if (Blocks.Num() == 3)
		{
			TestEqual(TEXT("4 m zone: slot 0 pulled to 3.25 m"), Blocks[0].DistCm, 325.f, 0.01f);
			TestTrue(TEXT("4 m zone: slot 0 marked pulled in"), Blocks[0].bPulledIn);
			TestEqual(TEXT("4 m zone: slot 1 pulled to 2.75 m"), Blocks[1].DistCm, 275.f, 0.01f);
			for (const FPmcZoneCoverBlock& Block : Blocks)
			{
				TestTrue(*FString::Printf(TEXT("4 m zone: slot %d inside"), Block.Slot), PmcZoneCover::IsInsideZone(Block, Centre, 400.f, Config.EdgeMarginCm));
				TestTrue(*FString::Printf(TEXT("4 m zone: slot %d not nearer than min_dist"), Block.Slot), Block.DistCm >= Config.MinDistCm - 0.01f);
			}
		}
	}
	// A 2 m zone: no block fits (min dist 1.5 m) - all dropped, with a reason each.
	{
		TArray<FString> Dropped;
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 200.f, Config, &Dropped);
		TestEqual(TEXT("2 m zone: no blocks"), Blocks.Num(), 0);
		TestEqual(TEXT("2 m zone: three reasons"), Dropped.Num(), 3);
		if (Dropped.Num() > 0)
		{
			TestTrue(TEXT("the reason says it does not fit"), Dropped[0].Contains(TEXT("does not fit")));
		}
	}
	// Overlapping slots: a side block at 10 deg on the centre block's distance lies on it - dropped.
	{
		FPmcZoneCoverConfig Tight = Config;
		Tight.SideAngleDeg = 10.f;
		Tight.SideDistCm = 350.f;
		TArray<FString> Dropped;
		const TArray<FPmcZoneCoverBlock> Blocks = PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 600.f, Tight, &Dropped);
		TestEqual(TEXT("overlap: only slot 0 stays"), Blocks.Num(), 1);
		TestEqual(TEXT("overlap: two dropped"), Dropped.Num(), 2);
		if (Dropped.Num() > 0)
		{
			TestTrue(TEXT("overlap: the reason names slot 0"), Dropped[0].Contains(TEXT("overlaps slot 0")));
		}
	}
	// Count 1: the centre block only.
	{
		FPmcZoneCoverConfig One = Config;
		One.Count = 1;
		TestEqual(TEXT("count 1: one block"), PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 600.f, One).Num(), 1);
	}
	// Nothing to place: no radius / no direction.
	TestEqual(TEXT("radius 0: none"), PmcZoneCover::LayoutBlocks(Centre, FVector2D(1.0, 0.0), 0.f, Config).Num(), 0);
	TestEqual(TEXT("zero approach: none"), PmcZoneCover::LayoutBlocks(Centre, FVector2D::ZeroVector, 600.f, Config).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcZoneCoverFootprintTest, "CodexTactics.AI.PmcZoneCover.Footprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcZoneCoverFootprintTest::RunTest(const FString&)
{
	using namespace PmcZoneCoverTest;
	const FPmcZoneCoverBlock Block = MakeBlock(FVector::ZeroVector, 0.f, 20.f, 100.f);
	TestEqual(TEXT("in front of the broad face"), PmcZoneCover::FootprintDistance(Block, FVector(50.f, 0.f, 0.f)), 30.f, 0.01f);
	TestEqual(TEXT("off the end"), PmcZoneCover::FootprintDistance(Block, FVector(0.f, 150.f, 0.f)), 50.f, 0.01f);
	TestEqual(TEXT("inside"), PmcZoneCover::FootprintDistance(Block, FVector(10.f, 10.f, 999.f)), 0.f, 0.01f);
	TestEqual(TEXT("off a corner"), PmcZoneCover::FootprintDistance(Block, FVector(50.f, 150.f, 0.f)), FMath::Sqrt(30.f * 30.f + 50.f * 50.f), 0.01f);
	const FPmcZoneCoverBlock Turned = MakeBlock(FVector::ZeroVector, 90.f, 20.f, 100.f);
	TestEqual(TEXT("turned 90: the broad face looks north"), PmcZoneCover::FootprintDistance(Turned, FVector(0.f, 50.f, 0.f)), 30.f, 0.01f);
	TestEqual(TEXT("turned 90: the end looks east"), PmcZoneCover::FootprintDistance(Turned, FVector(150.f, 0.f, 0.f)), 50.f, 0.01f);

	FVector2D Corners[4];
	PmcZoneCover::FootprintCorners(Block, Corners);
	TestTrue(TEXT("corner 0"), Corners[0].Equals(FVector2D(20.0, 100.0), 0.01));
	TestTrue(TEXT("corner 2"), Corners[2].Equals(FVector2D(-20.0, -100.0), 0.01));

	// Inside the zone: the farthest corner counts, with the margin.
	const FPmcZoneCoverBlock Out = MakeBlock(FVector(560.f, 0.f, 0.f), 0.f, 20.f, 100.f); // corner (580, 100): 588.6 cm
	TestTrue(TEXT("588.6 cm corner inside 6 m"), PmcZoneCover::IsInsideZone(Out, FVector::ZeroVector, 600.f, 0.f));
	TestFalse(TEXT("... not with a 20 cm margin"), PmcZoneCover::IsInsideZone(Out, FVector::ZeroVector, 600.f, 20.f));
	TestFalse(TEXT("a margin larger than the radius: never inside"), PmcZoneCover::IsInsideZone(Block, FVector::ZeroVector, 100.f, 200.f));

	// Overlap (SAT): parallel apart / touching / crossed at 90 deg / a rotated one clear of the corner.
	TestFalse(TEXT("side by side, 10 cm apart"), PmcZoneCover::FootprintsOverlap(Block, MakeBlock(FVector(50.f, 0.f, 0.f), 0.f, 20.f, 100.f)));
	TestTrue(TEXT("touching faces count"), PmcZoneCover::FootprintsOverlap(Block, MakeBlock(FVector(40.f, 0.f, 0.f), 0.f, 20.f, 100.f)));
	TestTrue(TEXT("a cross"), PmcZoneCover::FootprintsOverlap(Block, Turned));
	// A 45 deg block off the corner (20, 100): its bounding box overlaps the block's, its footprint does not (separated along its own depth axis).
	TestFalse(TEXT("a 45 deg block clear of the corner (the bounding boxes would overlap)"),
		PmcZoneCover::FootprintsOverlap(Block, MakeBlock(FVector(60.f, 100.f, 0.f), 45.f, 20.f, 100.f)));
	TestTrue(TEXT("the same block moved onto the corner overlaps"), PmcZoneCover::FootprintsOverlap(Block, MakeBlock(FVector(40.f, 80.f, 0.f), 45.f, 20.f, 100.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcZoneCoverConfigTest, "CodexTactics.AI.PmcZoneCover.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcZoneCoverConfigTest::RunTest(const FString&)
{
	using namespace PmcZoneCoverTest;
	const FPmcZoneCoverConfig Defaults;
	TestEqual(TEXT("default count"), Defaults.Count, 3);
	TestEqual(TEXT("default height: low cover under 130 cm"), Defaults.HeightCm < 130.f, true);
	TestEqual(TEXT("default height: over the 45 cm knee trace"), Defaults.HeightCm > 45.f, true);

	const TSharedPtr<FJsonObject> Json = ParseZoneCoverJson(TEXT(R"JSON({
		"count": 9, "centre_dist_m": 2.5, "side_dist_m": -1, "side_angle_deg": 45, "side_yaw_follow": 3, "length_cm": 150, "depth_cm": 1, "height_cm": 90,
		"edge_margin_cm": 10, "pull_in_step_cm": 0, "min_dist_m": 1, "spawn_clear_m": 4, "ground_max_dz_cm": 60, "ignored": 7
	})JSON"));
	TestTrue(TEXT("the object parses"), Json.IsValid());
	if (Json.IsValid())
	{
		const FPmcZoneCoverConfig Read = PmcZoneCover::ConfigFromJson(*Json);
		TestEqual(TEXT("count clamps to 3"), Read.Count, 3);
		TestEqual(TEXT("centre_dist_m in metres"), Read.CentreDistCm, 250.f, 0.01f);
		TestEqual(TEXT("side_dist_m at least 0"), Read.SideDistCm, 0.f, 0.01f);
		TestEqual(TEXT("side_angle_deg"), Read.SideAngleDeg, 45.f, 0.01f);
		TestEqual(TEXT("side_yaw_follow clamps to 1"), Read.SideYawFollow, 1.f, 0.01f);
		TestEqual(TEXT("length_cm"), Read.LengthCm, 150.f, 0.01f);
		TestEqual(TEXT("depth_cm at least 10"), Read.DepthCm, 10.f, 0.01f);
		TestEqual(TEXT("height_cm"), Read.HeightCm, 90.f, 0.01f);
		TestEqual(TEXT("edge_margin_cm"), Read.EdgeMarginCm, 10.f, 0.01f);
		TestEqual(TEXT("pull_in_step_cm at least 5"), Read.PullInStepCm, 5.f, 0.01f);
		TestEqual(TEXT("min_dist_m in metres"), Read.MinDistCm, 100.f, 0.01f);
		TestEqual(TEXT("spawn_clear_m in metres"), Read.SpawnClearCm, 400.f, 0.01f);
		TestEqual(TEXT("ground_max_dz_cm"), Read.GroundMaxDzCm, 60.f, 0.01f);
		const TSharedPtr<FJsonObject> Empty = ParseZoneCoverJson(TEXT("{}"));
		if (Empty.IsValid())
		{
			const FPmcZoneCoverConfig Kept = PmcZoneCover::ConfigFromJson(*Empty);
			TestEqual(TEXT("a missing key keeps the default"), Kept.SideDistCm, Defaults.SideDistCm, 0.01f);
			TestEqual(TEXT("a missing count keeps the default"), Kept.Count, Defaults.Count);
		}
	}

	// The shipped file mirrors the code defaults.
	const FPmcZoneCoverConfig Shipped = PmcZoneCover::LoadConfig();
	TestEqual(TEXT("shipped count"), Shipped.Count, Defaults.Count);
	TestEqual(TEXT("shipped centre_dist_m"), Shipped.CentreDistCm, Defaults.CentreDistCm, 0.01f);
	TestEqual(TEXT("shipped side_dist_m"), Shipped.SideDistCm, Defaults.SideDistCm, 0.01f);
	TestEqual(TEXT("shipped side_angle_deg"), Shipped.SideAngleDeg, Defaults.SideAngleDeg, 0.01f);
	TestEqual(TEXT("shipped side_yaw_follow"), Shipped.SideYawFollow, Defaults.SideYawFollow, 0.01f);
	TestEqual(TEXT("shipped length_cm"), Shipped.LengthCm, Defaults.LengthCm, 0.01f);
	TestEqual(TEXT("shipped depth_cm"), Shipped.DepthCm, Defaults.DepthCm, 0.01f);
	TestEqual(TEXT("shipped height_cm"), Shipped.HeightCm, Defaults.HeightCm, 0.01f);
	TestEqual(TEXT("shipped edge_margin_cm"), Shipped.EdgeMarginCm, Defaults.EdgeMarginCm, 0.01f);
	TestEqual(TEXT("shipped pull_in_step_cm"), Shipped.PullInStepCm, Defaults.PullInStepCm, 0.01f);
	TestEqual(TEXT("shipped min_dist_m"), Shipped.MinDistCm, Defaults.MinDistCm, 0.01f);
	TestEqual(TEXT("shipped spawn_clear_m"), Shipped.SpawnClearCm, Defaults.SpawnClearCm, 0.01f);
	TestEqual(TEXT("shipped ground_max_dz_cm"), Shipped.GroundMaxDzCm, Defaults.GroundMaxDzCm, 0.01f);
	return true;
}

#endif
