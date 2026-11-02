// CodexTactics.Formation.RampFile.* - the squad on a narrow ramp / stair walks it single file (the game designer's bug report 2026-11-02 on
// L_Gen_Depot_1, item 7: "the squad at a ramp's foot, one man ordered onto the roof - they all get stuck at the bottom"): RampFileRules
// (who waits behind whom, the ramp surface, the level change, no formation slot on a ramp) and FormationRuntime::YieldFromLeaderPath (a holding
// team-mate on the selected man's path steps aside). In-game check: LevelGenRampSmoke (Codex.Squad.RampFile 0 = the old behaviour).
// UE-only (no Godot reference).

#include "Formation/FormationRuntimeRules.h"
#include "Formation/RampFileRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define RAMP_FILE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.RampFile." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace RampFileTestLocal
{
	RampFileRules::FRampWalker Walker(float Along, int32 Direction, bool bProgressing, int32 Priority)
	{
		RampFileRules::FRampWalker Out;
		Out.Along = Along;
		Out.Direction = Direction;
		Out.bProgressing = bProgressing;
		Out.Priority = Priority;
		return Out;
	}

	/** The normal of a ramp rising along +X at SlopeDeg. */
	FVector RampNormal(float SlopeDeg)
	{
		const float Rad = FMath::DegreesToRadians(SlopeDeg);
		return FVector(-FMath::Sin(Rad), 0.f, FMath::Cos(Rad));
	}
}

RAMP_FILE_TEST(FRampFileSurfaceTest, "Surface")
bool FRampFileSurfaceTest::RunTest(const FString&)
{
	using namespace RampFileRules;
	using namespace RampFileTestLocal;
	const FRampFileConfig Config;
	TestTrue(TEXT("the generated maps' 35 deg external stair is a ramp"), IsRampSurface(RampNormal(35.f), false, Config.MinSlopeDeg));
	TestTrue(TEXT("the cut's 23 deg ramp is one"), IsRampSurface(RampNormal(23.f), false, Config.MinSlopeDeg));
	TestFalse(TEXT("a flat roof is none"), IsRampSurface(FVector::UpVector, false, Config.MinSlopeDeg));
	TestFalse(TEXT("a 5 deg sagging slab is none"), IsRampSurface(RampNormal(5.f), false, Config.MinSlopeDeg));
	TestFalse(TEXT("a 30 deg landscape hill is no ramp (terrain)"), IsRampSurface(RampNormal(30.f), true, Config.MinSlopeDeg));
	TestFalse(TEXT("a wall face (no floor) is none"), IsRampSurface(FVector(-1.f, 0.f, 0.f), false, Config.MinSlopeDeg));
	TestTrue(TEXT("uphill of a ramp rising along +X is +X"), UphillOf(RampNormal(35.f)).Equals(FVector2D(1.0, 0.0), 1e-4));
	TestTrue(TEXT("a flat floor has no uphill"), UphillOf(FVector::UpVector).IsNearlyZero());
	return true;
}

RAMP_FILE_TEST(FRampFileDirectionTest, "TravelDirection")
bool FRampFileDirectionTest::RunTest(const FString&)
{
	using namespace RampFileRules;
	const FRampFileConfig Config;
	TestEqual(TEXT("a goal on the 6 m roof: up"), TravelDirection(0.f, 610.f, true, Config.LevelChangeCm), 1);
	TestEqual(TEXT("a goal on the ground from the roof: down"), TravelDirection(600.f, 10.f, true, Config.LevelChangeCm), -1);
	TestEqual(TEXT("a formation slot on his own floor (the leader's capsule centre, +90 cm): no level change"),
		TravelDirection(0.f, 90.f, true, Config.LevelChangeCm), 0);
	TestEqual(TEXT("no walk: no direction"), TravelDirection(0.f, 610.f, false, Config.LevelChangeCm), 0);
	TestEqual(TEXT("on the ramp near its top, the goal just past it: his way up the ramp counts"),
		TravelDirection(520.f, 610.f, true, Config.LevelChangeCm, true, 150.f, Config.ProgressSpeed), 1);
	TestEqual(TEXT("... standing on it: none"), TravelDirection(520.f, 610.f, true, Config.LevelChangeCm, true, 5.f, Config.ProgressSpeed), 0);
	return true;
}

RAMP_FILE_TEST(FRampFileWaitTest, "SingleFile")
bool FRampFileWaitTest::RunTest(const FString&)
{
	using namespace RampFileRules;
	using namespace RampFileTestLocal;
	const FRampFileConfig Config;
	// Three men going up: the leader 300 cm up the ramp, B 100 cm behind him, C 20 cm behind B (shoulder to shoulder in the mouth).
	{
		const TArray<FRampWalker> Walkers = { Walker(300.f, 1, true, 0), Walker(200.f, 1, true, 2), Walker(180.f, 1, true, 3) };
		TestEqual(TEXT("the front man walks on"), FindWaitBehind(0, Walkers, Config), INDEX_NONE);
		TestEqual(TEXT("B waits behind the leader (100 cm ahead of him)"), FindWaitBehind(1, Walkers, Config), 0);
		TestEqual(TEXT("C waits for B, the nearest ahead"), FindWaitBehind(2, Walkers, Config), 1);
	}
	{
		// Exactly level: the earlier priority is ahead - one of them waits, never both.
		const TArray<FRampWalker> Walkers = { Walker(100.f, 1, true, 3), Walker(102.f, 1, true, 2) };
		TestEqual(TEXT("level, later priority: waits"), FindWaitBehind(0, Walkers, Config), 1);
		TestEqual(TEXT("level, earlier priority: walks"), FindWaitBehind(1, Walkers, Config), INDEX_NONE);
	}
	{
		const TArray<FRampWalker> Walkers = { Walker(400.f, 1, true, 0), Walker(200.f, 1, true, 2) };
		TestEqual(TEXT("the gap is 200 cm (> FileGapCm): B walks on"), FindWaitBehind(1, Walkers, Config), INDEX_NONE);
		TestEqual(TEXT("... but already waiting he waits for FileGapCm + ReleaseMarginCm (210)"), FindWaitBehind(1, Walkers, Config, true), 0);
	}
	{
		const TArray<FRampWalker> Walkers = { Walker(300.f, 1, false, 0), Walker(200.f, 1, true, 2) };
		TestEqual(TEXT("the man ahead stands (no progress): no wait for him"), FindWaitBehind(1, Walkers, Config), INDEX_NONE);
	}
	{
		const TArray<FRampWalker> Walkers = { Walker(300.f, -1, true, 0), Walker(200.f, 1, true, 2) };
		TestEqual(TEXT("coming the other way: not the same file"), FindWaitBehind(1, Walkers, Config), INDEX_NONE);
	}
	{
		// Going DOWN: ahead = lower along the uphill axis.
		const TArray<FRampWalker> Walkers = { Walker(100.f, -1, true, 1), Walker(200.f, -1, true, 0) };
		TestEqual(TEXT("down the ramp: the man above waits behind the one below (even the leader)"), FindWaitBehind(1, Walkers, Config), 0);
		TestEqual(TEXT("... the one below walks on"), FindWaitBehind(0, Walkers, Config), INDEX_NONE);
	}
	{
		// A chain: the front man walks, the second waits (progressing in the file), the third waits behind the second.
		const TArray<FRampWalker> Walkers = { Walker(300.f, 1, true, 0), Walker(180.f, 1, true, 1), Walker(60.f, 1, true, 2) };
		TestEqual(TEXT("the third waits behind the second (120 cm)"), FindWaitBehind(2, Walkers, Config), 1);
	}
	{
		const TArray<FRampWalker> Walkers = { Walker(200.f, 1, true, 0), Walker(200.f, 0, true, 1) };
		TestEqual(TEXT("not changing level: never in the file"), FindWaitBehind(1, Walkers, Config), INDEX_NONE);
	}
	TestFalse(TEXT("4 s waited: still waiting"), HoldExpired(4.f, Config));
	TestTrue(TEXT("6 s: the wait ends"), HoldExpired(Config.MaxHoldSeconds, Config));
	return true;
}

RAMP_FILE_TEST(FRampFileMouthTest, "Mouth")
bool FRampFileMouthTest::RunTest(const FString&)
{
	using namespace RampFileRules;
	const FRampFileConfig Config;
	const FVector2D Uphill(1.0, 0.0); // the ramp rises along +X; a man on it at the origin
	TestTrue(TEXT("150 cm behind him at the foot, going up: in the mouth"), IsInMouth(FVector2D(-150.0, 20.0), -60.f, Uphill, 1, Config));
	TestFalse(TEXT("beside the ramp (1.2 m off its line): not"), IsInMouth(FVector2D(-100.0, 120.0), -40.f, Uphill, 1, Config));
	TestFalse(TEXT("ahead of him (past him up the ramp's line, off it): not"), IsInMouth(FVector2D(150.0, 0.0), 60.f, Uphill, 1, Config));
	TestTrue(TEXT("going down: the man above him (behind in their way) is in the mouth"), IsInMouth(FVector2D(150.0, 0.0), 60.f, Uphill, -1, Config));
	TestFalse(TEXT("too far (3 m)"), IsInMouth(FVector2D(-300.0, 0.0), -60.f, Uphill, 1, Config));
	TestFalse(TEXT("a level below (2.5 m)"), IsInMouth(FVector2D(-100.0, 0.0), -250.f, Uphill, 1, Config));
	TestFalse(TEXT("no level change: never"), IsInMouth(FVector2D(-100.0, 0.0), 0.f, Uphill, 0, Config));
	return true;
}

RAMP_FILE_TEST(FRampFileSlotTest, "NoSlotOnRamp")
bool FRampFileSlotTest::RunTest(const FString&)
{
	using namespace RampFileRules;
	TestTrue(TEXT("the leader on the roof, the slot behind him on the ramp: dropped (a stand-in on the roof)"), ShouldDropRampSlot(false, true));
	TestFalse(TEXT("the leader himself on the ramp: the column trails him on it"), ShouldDropRampSlot(true, true));
	TestFalse(TEXT("a slot on a floor: kept"), ShouldDropRampSlot(false, false));
	return true;
}

RAMP_FILE_TEST(FRampFileYieldPathTest, "HolderYieldOnPath")
bool FRampFileYieldPathTest::RunTest(const FString&)
{
	// The selected man at the back (0, -260) walks to the ramp's foot (0, 0) and up it along +Y; his path then turns +X on the roof.
	const TArray<FVector2D> Path = { FVector2D(0.0, -260.0), FVector2D(0.0, 0.0), FVector2D(0.0, 860.0), FVector2D(300.0, 860.0) };
	// A holder in the ramp's mouth 60 cm left of the path, 140 cm ahead.
	const TArray<FVector2D> Aside = FormationRuntime::YieldFromLeaderPath(Path, FVector2D(-60.0, -140.0));
	TestEqual(TEXT("in the mouth: two points to step aside to"), Aside.Num(), 2);
	if (Aside.Num() == 2)
	{
		TestTrue(TEXT("his own side first (left, -X), ClearCm off the path"), Aside[0].Equals(FVector2D(-FormationRuntime::YieldClearCm, -140.0), 0.01));
	}
	// The straight line to the goal (300, 860) passes 120+ cm off him - YieldFromLeaderWay would not see him in the way.
	TestEqual(TEXT("the straight line to the goal misses him"),
		FormationRuntime::YieldFromLeaderWay(Path[0], Path.Last(), FVector2D(-60.0, -140.0), FVector2D(-60.0, -140.0)).Num(), 0);
	TestEqual(TEXT("beside the ramp 2 m off the path: no yield"), FormationRuntime::YieldFromLeaderPath(Path, FVector2D(-200.0, -100.0)).Num(), 0);
	TestEqual(TEXT("past the look-ahead (on the roof 9 m on): no yield"), FormationRuntime::YieldFromLeaderPath(Path, FVector2D(250.0, 860.0), 80.f, 120.f, 600.f).Num(), 0);
	TestEqual(TEXT("no path: no yield"), FormationRuntime::YieldFromLeaderPath(TArray<FVector2D>(), FVector2D(0.0, 0.0)).Num(), 0);
	return true;
}

RAMP_FILE_TEST(FRampFileYieldSpotsTest, "YieldSpots")
bool FRampFileYieldSpotsTest::RunTest(const FString&)
{
	// The selected man at (0, -260) walks north to the ramp's foot (0, 0) and up it; a holder stands 60 cm east of his path.
	const TArray<FVector2D> Path = { FVector2D(0.0, -260.0), FVector2D(0.0, 0.0), FVector2D(0.0, 860.0) };
	const FVector2D Here(60.0, -140.0);
	const float Clear = RampFileRules::FRampFileConfig().YieldSpotClearCm;
	const TArray<FVector2D> Spots = RampFileRules::YieldSpots(Path, Here, Clear, FormationRuntime::YieldAheadCm);
	TestTrue(TEXT("spots found"), Spots.Num() > 0);
	if (Spots.Num() > 0)
	{
		TestTrue(TEXT("the first is on his own (east) side - no walk across the selected man's way"), Spots[0].X > 0.0);
		TestTrue(TEXT("... at the nearest ring (130 cm)"), FMath::IsNearlyEqual(FVector2D::Distance(Spots[0], Here), 130.0, 0.5));
		TestTrue(TEXT("... at least YieldSpotClearCm off his path (both bodies pass)"), Spots[0].X >= Clear);
	}
	bool bAllClear = true;
	bool bOtherSideLast = true;
	bool bSeenOther = false;
	for (const FVector2D& Spot : Spots)
	{
		bAllClear &= FormationRuntime::YieldFromLeaderPath(Path, Spot, Clear).IsEmpty();
		bSeenOther |= Spot.X < 0.0;
		bOtherSideLast &= !(bSeenOther && Spot.X > 0.0);
	}
	TestTrue(TEXT("every spot is off his way"), bAllClear);
	TestTrue(TEXT("the other side only after all of his own"), bOtherSideLast);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
