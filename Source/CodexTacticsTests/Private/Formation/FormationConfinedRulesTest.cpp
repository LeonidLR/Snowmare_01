// CodexTactics.Formation.Confined.* - the formation compresses in a confined space (the game designer's decision 2026-11-21: "the whole squad
// must enter" a ~5 m2 khrushchevka flat hall and the 2.5 m gable-vestibule corridors): FormationConfinedRules + FormationConfinedTuning.
// In-game check: LevelGenBuildingsWalkSmoke flat-entry squad legs (Codex.Formation.Confined 0 = the old slots, A/B). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Formation/FormationConfinedRules.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_CONFINED_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Confined." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationConfinedTestLocal
{
	/**
	 * A navmesh pocket probed from Center inside an axis-aligned room [-HalfX, HalfX] x [-HalfY, HalfY] (the agent-eroded navmesh, room at the
	 * origin). DoorHalf > 0: a door gap |y| <= DoorHalf in the -X wall onto open ground (a ray through it runs the full probe length).
	 */
	FFormationConfinedPocket RoomPocket(const FVector2D& Center, float HalfX, float HalfY, float DoorHalf, const FFormationConfinedTuning& Tuning)
	{
		FFormationConfinedPocket Pocket;
		Pocket.Center = Center;
		constexpr double Eps = 1e-6;
		for (int32 Ray = 0; Ray < Tuning.ProbeRays; ++Ray)
		{
			const FVector2D Dir = FormationConfinedRules::RayDir(Ray, Tuning.ProbeRays);
			const double TX = Dir.X > Eps ? (HalfX - Center.X) / Dir.X : Dir.X < -Eps ? (-HalfX - Center.X) / Dir.X : TNumericLimits<double>::Max();
			const double TY = Dir.Y > Eps ? (HalfY - Center.Y) / Dir.Y : Dir.Y < -Eps ? (-HalfY - Center.Y) / Dir.Y : TNumericLimits<double>::Max();
			double T = FMath::Min(TX, TY);
			if (DoorHalf > 0.f && Dir.X < -Eps && TX <= TY && FMath::Abs(Center.Y + Dir.Y * TX) <= DoorHalf)
			{
				T = Tuning.ProbeCm; // out through the door onto open ground
			}
			Pocket.Free.Add(static_cast<float>(FMath::Min(T, static_cast<double>(Tuning.ProbeCm))));
		}
		return Pocket;
	}

	/** The navmesh agent radius (DefaultEngine.ini): an eroded room's real walls lie this much farther out (a door-ward place may use it), cm. */
	constexpr float AgentRadiusCm = 35.f;

	FFormationConfinedSlot Slot(const FVector2D& Desired, const FVector2D& Occupant)
	{
		FFormationConfinedSlot Out;
		Out.Desired = Desired;
		Out.Occupant = Occupant;
		return Out;
	}

	bool InRoom(const FVector2D& Point, float HalfX, float HalfY)
	{
		return FMath::Abs(Point.X) <= HalfX + 0.01 && FMath::Abs(Point.Y) <= HalfY + 0.01;
	}

	float MinPairDistance(const TArray<FVector2D>& Points, const FVector2D& Leader)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Points.Num(); ++A)
		{
			Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(Points[A], Leader)));
			for (int32 B = A + 1; B < Points.Num(); ++B)
			{
				Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(Points[A], Points[B])));
			}
		}
		return Best;
	}
}

FORMATION_CONFINED_TEST(FFormationConfinedBasicsTest, "Basics")
bool FFormationConfinedBasicsTest::RunTest(const FString&)
{
	TestTrue(TEXT("ray 0 is +X"), FormationConfinedRules::RayDir(0, 16).Equals(FVector2D(1.0, 0.0), 1e-6));
	TestTrue(TEXT("ray 4 of 16 is +Y (counter-clockwise)"), FormationConfinedRules::RayDir(4, 16).Equals(FVector2D(0.0, 1.0), 1e-6));
	const TArray<float> Odd{ 5.f, 1.f, 3.f };
	const TArray<float> Even{ 4.f, 1.f, 3.f, 10.f };
	TestEqual(TEXT("median of an odd count"), FormationConfinedRules::Median(Odd), 3.f);
	TestEqual(TEXT("median of an even count: the mean of the middle two"), FormationConfinedRules::Median(Even), 3.5f);
	TestEqual(TEXT("median of nothing"), FormationConfinedRules::Median(TArray<float>()), 0.f);
	TestEqual(TEXT("point to segment"), FormationConfinedRules::DistanceToSegment(FVector2D(5.0, 3.0), FVector2D(0.0, 0.0), FVector2D(10.0, 0.0)), 3.f);
	TestEqual(TEXT("point beyond the segment end"), FormationConfinedRules::DistanceToSegment(FVector2D(13.0, 4.0), FVector2D(0.0, 0.0), FVector2D(10.0, 0.0)), 5.f);

	// FreeToward: on a ray that ray alone, between two rays the smaller.
	FFormationConfinedPocket Pocket;
	Pocket.Free = { 100.f, 300.f, 200.f, 400.f };
	TestEqual(TEXT("on ray 1 (+Y)"), FormationConfinedRules::FreeToward(Pocket, FVector2D(0.0, 1.0)), 300.f);
	TestEqual(TEXT("between ray 0 and ray 1: the smaller"), FormationConfinedRules::FreeToward(Pocket, FVector2D(1.0, 1.0)), 100.f);
	TestEqual(TEXT("between ray 3 and ray 0 (wraps)"), FormationConfinedRules::FreeToward(Pocket, FVector2D(1.0, -1.0)), 100.f);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedOpenGroundTest, "OpenGroundUnchanged")
bool FFormationConfinedOpenGroundTest::RunTest(const FString&)
{
	// Outdoors (no ray hits, or one wall beside the squad, or a 4.66 m passage): never confined - the formation's slots exactly as given.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	FFormationConfinedPocket Open;
	Open.Free.Init(Tuning.ProbeCm, Tuning.ProbeRays);
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-250.0, 20.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-400.0, -20.0)) };
	for (const FFormationConfinedPocket& Pocket : { Open, RoomPocket(FVector2D::ZeroVector, 2000.f, 100.f + 2000.f, 0.f, Tuning),
		RoomPocket(FVector2D::ZeroVector, 2000.f, 233.f, 0.f, Tuning) })
	{
		TestFalse(TEXT("not confined"), FormationConfinedRules::IsConfined(Pocket, Tuning));
		const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, FVector2D(-100.0, 0.0), TOptional<FVector2D>(), Tuning);
		TestFalse(TEXT("result not confined"), Result.bConfined);
		TestEqual(TEXT("nothing replaced"), Result.ReplacedCount, 0);
		TestTrue(TEXT("slot 0 as given"), Result.Places.Num() == 2 && Result.Places[0].Equals(Slots[0].Desired, 1e-6));
		TestTrue(TEXT("slot 1 as given"), Result.Places.Num() == 2 && Result.Places[1].Equals(Slots[1].Desired, 1e-6));
	}
	// A wall 100 cm to one side of the squad on open ground: half the rays hit it, the median still the full probe.
	FFormationConfinedPocket OneWall;
	for (int32 Ray = 0; Ray < Tuning.ProbeRays; ++Ray)
	{
		const FVector2D Dir = FormationConfinedRules::RayDir(Ray, Tuning.ProbeRays);
		OneWall.Free.Add(Dir.Y > 1e-6 ? FMath::Min(Tuning.ProbeCm, static_cast<float>(100.0 / Dir.Y)) : Tuning.ProbeCm);
	}
	TestFalse(TEXT("one wall beside him is open ground"), FormationConfinedRules::IsConfined(OneWall, Tuning));
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedHallTest, "HallSlotsMoveInside")
bool FFormationConfinedHallTest::RunTest(const FString&)
{
	// A 2.5 x 2.5 m flat hall (agent-eroded navmesh +-90 cm), its door in the -X wall onto the landing. The leader stands in the middle; the
	// trail column put his men 330 / 480 cm back along his trail - out through the door, on the landing (the walk smoke's failure).
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	TestTrue(TEXT("the hall is confined (its door's rays do not lift the median)"), FormationConfinedRules::IsConfined(Pocket, Tuning));
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-160.0, 10.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, -10.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestTrue(TEXT("confined"), Result.bConfined);
	TestEqual(TEXT("both slots moved"), Result.ReplacedCount, 2);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(TEXT("inside the pocket"), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
	}
	TestTrue(TEXT("two bodies apart from the leader and each other"), MinPairDistance(Result.Places, FVector2D::ZeroVector) >= Tuning.MinSpacingCm - 0.1f);
	// They came in through the door (from the landing at -X): nobody parks in its way in (the man behind still gets through).
	const FVector2D Landing(-300.0, 0.0);
	const FFormationConfinedResult Entered = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Landing, Tuning);
	TestEqual(TEXT("entered: both moved"), Entered.ReplacedCount, 2);
	for (const FVector2D& Place : Entered.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) off the door's way in"), Place.X, Place.Y),
			FormationConfinedRules::DistanceToSegment(Place, FVector2D::ZeroVector, FVector2D(-Tuning.ProbeCm, 0.0)) >= Tuning.WayHalfWidthCm);
		TestFalse(TEXT("not on the entry line"), FormationConfinedRules::IsOnEntry(Pocket, Landing, Place, Tuning));
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the hall"), Place.X, Place.Y), InRoom(Place, 90.f - Tuning.WallMarginCm, 90.f - Tuning.WallMarginCm));
	}
	TestTrue(TEXT("entered: spaced"), MinPairDistance(Entered.Places, FVector2D::ZeroVector) >= Tuning.MinSpacingCm - 0.1f);

	// Deterministic: the same input -> the same places.
	const FFormationConfinedResult Again = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestTrue(TEXT("same places again"), Again.Places.Num() == 2 && Again.Places[0].Equals(Result.Places[0], 1e-6) && Again.Places[1].Equals(Result.Places[1], 1e-6));

	// Settled: the men walk to their places (the next frames ask again from there) - within three steps the places hold for good (no shuffle
	// once they are in).
	TArray<FVector2D> Places = Result.Places;
	bool bHeld = false;
	for (int32 Step = 0; Step < 3 && !bHeld; ++Step)
	{
		const TArray<FFormationConfinedSlot> Settled{ Slot(FVector2D(-330.0, 0.0), Places[0]), Slot(FVector2D(-480.0, 0.0), Places[1]) };
		const FFormationConfinedResult Next = FormationConfinedRules::CompressSlots(Pocket, Settled, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
		bHeld = Next.Places[0].Equals(Places[0], 1e-6) && Next.Places[1].Equals(Places[1], 1e-6);
		Places = Next.Places;
	}
	TestTrue(TEXT("settled men keep their places"), bHeld);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedSmallHallTest, "FiveSquareMetreHallTakesTheSquad")
bool FFormationConfinedSmallHallTest::RunTest(const FString&)
{
	// The ~5 m2 khrushchevka hall: 2.0 x 2.5 m (eroded +-65 x +-90 cm), door in the -X wall. Two followers (a squad of three) and three (four).
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 65.f, 90.f, 45.f, Tuning);
	TestTrue(TEXT("confined"), FormationConfinedRules::IsConfined(Pocket, Tuning));
	TestTrue(TEXT("candidate places exist"), FormationConfinedRules::Candidates(Pocket, Tuning).Num() > 0);
	const TArray<FFormationConfinedSlot> Three{ Slot(FVector2D(-330.0, 0.0), FVector2D(-150.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, 0.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Three, TOptional<FVector2D>(), FVector2D(-300.0, 0.0), Tuning);
	TestEqual(TEXT("both moved"), Result.ReplacedCount, 2);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the 5 m2 hall"), Place.X, Place.Y), InRoom(Place, 65.f, 90.f));
	}
	TestTrue(TEXT("spaced"), MinPairDistance(Result.Places, FVector2D::ZeroVector) >= Tuning.MinSpacingCm - 0.1f);

	const TArray<FFormationConfinedSlot> Four{ Slot(FVector2D(-330.0, 0.0), FVector2D(-150.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, 0.0)),
		Slot(FVector2D(-630.0, 0.0), FVector2D(-450.0, 0.0)) };
	const FFormationConfinedResult Squad4 = FormationConfinedRules::CompressSlots(Pocket, Four, TOptional<FVector2D>(), FVector2D(-300.0, 0.0), Tuning);
	TestEqual(TEXT("all three moved"), Squad4.ReplacedCount, 3);
	// Four bodies do not fit a 2 x 2.5 m hall: at least the first two followers stand inside its walls, the third may wait in the doorway.
	int32 InsideWalls = 0;
	for (const FVector2D& Place : Squad4.Places)
	{
		InsideWalls += InRoom(Place, 65.f + AgentRadiusCm, 90.f + AgentRadiusCm) ? 1 : 0;
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the pocket (squad of four)"), Place.X, Place.Y), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
	}
	TestTrue(*FString::Printf(TEXT("%d of 3 inside the hall's walls"), InsideWalls), InsideWalls >= 2);
	TestTrue(TEXT("no two men on one place"), MinPairDistance(Squad4.Places, FVector2D::ZeroVector) > 30.f);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedWayTest, "ArrivingLeaderWayKeptClear")
bool FFormationConfinedWayTest::RunTest(const FString&)
{
	// The leader still walks in through the door (at (-150, 0), 150 cm from the hall's centre): no place on his remaining way, and a slot that
	// lies inside the hall but on that way moves off it.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	const FVector2D LeaderAt(-150.0, 0.0);
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-80.0, 0.0), FVector2D(-260.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-400.0, 0.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, LeaderAt, TOptional<FVector2D>(), Tuning);
	TestEqual(TEXT("both moved (one was on his way, one outside)"), Result.ReplacedCount, 2);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) off his way"), Place.X, Place.Y),
			FormationConfinedRules::DistanceToSegment(Place, LeaderAt, Pocket.Center) >= Tuning.WayHalfWidthCm);
		TestTrue(TEXT("inside"), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
	}
	// Standing at the centre (LeaderAt within way_min_cm) the way is gone: an inside, spaced slot stays exactly.
	const TArray<FFormationConfinedSlot> Inside{ Slot(FVector2D(0.0, -80.0), FVector2D(-200.0, 0.0)) };
	const FFormationConfinedResult Stays = FormationConfinedRules::CompressSlots(Pocket, Inside, FVector2D(10.0, 0.0), TOptional<FVector2D>(), Tuning);
	TestEqual(TEXT("an inside slot stays"), Stays.ReplacedCount, 0);
	TestTrue(TEXT("exactly"), Stays.Places[0].Equals(FVector2D(0.0, -80.0), 1e-6));
	// ... but an inside slot on the entry line (the door they came through) moves off it; with no known entry it stays.
	const TArray<FFormationConfinedSlot> Doorway{ Slot(FVector2D(-80.0, 0.0), FVector2D(-80.0, 0.0)) };
	TestEqual(TEXT("a slot in the door's way in moves"),
		FormationConfinedRules::CompressSlots(Pocket, Doorway, TOptional<FVector2D>(), FVector2D(-300.0, 0.0), Tuning).ReplacedCount, 1);
	TestEqual(TEXT("no entry known: it stays"),
		FormationConfinedRules::CompressSlots(Pocket, Doorway, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning).ReplacedCount, 0);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedNarrowHallTest, "NarrowHallNobodyPastTheLeader")
bool FFormationConfinedNarrowHallTest::RunTest(const FString&)
{
	// A 1.5 x 5 m hall (eroded +-215 along X, +-40 across: one body wide), its door at the -X end; the leader stands in the middle. Nobody is
	// sent past him to the far end (he would push into the leader for good - the walk smoke's STUCK followers): both men line up between the
	// door and him, inside the hall.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 215.f, 40.f, 40.f, Tuning);
	TestTrue(TEXT("confined"), FormationConfinedRules::IsConfined(Pocket, Tuning));
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-300.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-450.0, 0.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), FVector2D(-300.0, 0.0), Tuning);
	TestEqual(TEXT("both moved"), Result.ReplacedCount, 2);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) on the door side of the leader"), Place.X, Place.Y), Place.X < 0.0);
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the hall"), Place.X, Place.Y), InRoom(Place, 215.f, 40.f));
	}
	TestTrue(TEXT("spaced"), MinPairDistance(Result.Places, FVector2D::ZeroVector) >= Tuning.MinSpacingCm - 0.1f);

	// The hall is one body wide (80 cm through the leader): from the door (-X) every place on his far side is past him; a 2.5 m wide one is not.
	const FVector2D Door(-300.0, 0.0);
	TestTrue(TEXT("a place ahead is past the leader"), FormationConfinedRules::IsPastLeader(Pocket, Door, FVector2D(80.0, 0.0), Tuning));
	TestFalse(TEXT("a place behind is not"), FormationConfinedRules::IsPastLeader(Pocket, Door, FVector2D(-80.0, 0.0), Tuning));
	TestFalse(TEXT("no entry known: nothing is past him"), FormationConfinedRules::IsPastLeader(Pocket, TOptional<FVector2D>(), FVector2D(80.0, 0.0), Tuning));
	const FFormationConfinedPocket Wide = RoomPocket(FVector2D::ZeroVector, 215.f, 125.f, 40.f, Tuning);
	TestFalse(TEXT("a 2.5 m wide hall has room beside him"), FormationConfinedRules::IsPastLeader(Wide, Door, FVector2D(80.0, 0.0), Tuning));
	// Occupants who stand beside the hall (a straight line would pass nobody) still never get a place past him.
	const TArray<FFormationConfinedSlot> Around{ Slot(FVector2D(-330.0, 0.0), FVector2D(-120.0, 150.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-120.0, -150.0)) };
	const FFormationConfinedResult Behind = FormationConfinedRules::CompressSlots(Pocket, Around, TOptional<FVector2D>(), Door, Tuning);
	for (const FVector2D& Place : Behind.Places)
	{
		TestFalse(*FString::Printf(TEXT("(%.0f, %.0f) not past the leader"), Place.X, Place.Y), FormationConfinedRules::IsPastLeader(Pocket, Door, Place, Tuning));
	}
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedCorridorTest, "GableCorridorTakesTheColumn")
bool FFormationConfinedCorridorTest::RunTest(const FString&)
{
	// The 2.5 m gable-vestibule corridor (eroded +-90 across, 4 m long), entered from its -X end: confined; the column's men stand in it near
	// the leader (within the pocket radius), not back out on the street.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D(100.0, 0.0), 200.f, 90.f, 90.f, Tuning);
	TestTrue(TEXT("the corridor is confined"), FormationConfinedRules::IsConfined(Pocket, Tuning));
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-230.0, 0.0), FVector2D(-120.0, 0.0)), Slot(FVector2D(-380.0, 0.0), FVector2D(-260.0, 0.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestEqual(TEXT("both moved"), Result.ReplacedCount, 2);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the corridor"), Place.X, Place.Y), InRoom(Place, 200.f, 90.f));
		TestTrue(TEXT("within the pocket radius of the leader"),
			FVector2D::Distance(Place, Pocket.Center) <= FormationConfinedRules::PocketRadius(Pocket, Tuning) + 0.1);
	}
	TestTrue(TEXT("spaced"), MinPairDistance(Result.Places, Pocket.Center) >= Tuning.MinSpacingCm - 0.1f);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedTooTightTest, "TooTightNeverStacksMen")
bool FFormationConfinedTooTightTest::RunTest(const FString&)
{
	// A closet (eroded +-25 x +-100): two places keep the spacing (beside the leader along it) and take two men; a third finds no room that
	// keeps half the spacing - he keeps his slot (waits outside) instead of standing on a team-mate; no candidate at all: the slots stay.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Closet = RoomPocket(FVector2D::ZeroVector, 25.f, 100.f, 0.f, Tuning);
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-200.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, 0.0)) };
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Closet, Slots, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestEqual(TEXT("both moved"), Result.ReplacedCount, 2);
	TestTrue(TEXT("two different places"), !Result.Places[0].Equals(Result.Places[1], 1.0));
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside the closet"), Place.X, Place.Y), InRoom(Place, 25.f, 100.f));
	}
	const TArray<FFormationConfinedSlot> Three{ Slot(FVector2D(-330.0, 0.0), FVector2D(-200.0, 0.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, 0.0)),
		Slot(FVector2D(-630.0, 0.0), FVector2D(-400.0, 0.0)) };
	const FFormationConfinedResult Crowded = FormationConfinedRules::CompressSlots(Closet, Three, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestEqual(TEXT("two moved in"), Crowded.ReplacedCount, 2);
	TestFalse(TEXT("the third keeps his slot"), Crowded.Replaced[2]);
	TestTrue(TEXT("the third exactly at his slot"), Crowded.Places[2].Equals(Three[2].Desired, 1e-6));
	FFormationConfinedPocket None;
	None.Free.Init(0.f, Tuning.ProbeRays);
	const FFormationConfinedResult Stays = FormationConfinedRules::CompressSlots(None, Slots, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning);
	TestTrue(TEXT("confined"), Stays.bConfined);
	TestEqual(TEXT("no candidate: nothing moved"), Stays.ReplacedCount, 0);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedTuningTest, "TuningFromJsonAndFile")
bool FFormationConfinedTuningTest::RunTest(const FString&)
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("enabled"), 0.0);
	Json->SetNumberField(TEXT("probe_rays"), 200.0);
	Json->SetNumberField(TEXT("probe_m"), 3.0);
	Json->SetNumberField(TEXT("confined_median_m"), 1.5);
	Json->SetNumberField(TEXT("min_spacing_cm"), 5.0);
	Json->SetNumberField(TEXT("arrive_m"), 2.5);
	const FFormationConfinedTuning Tuning = FormationConfinedTuning::FromJson(*Json);
	TestFalse(TEXT("enabled 0"), Tuning.bEnabled);
	TestEqual(TEXT("probe_rays clamped to 64"), Tuning.ProbeRays, 64);
	TestEqual(TEXT("probe_m in metres"), Tuning.ProbeCm, 300.f);
	TestEqual(TEXT("confined_median_m in metres"), Tuning.ConfinedMedianCm, 150.f);
	TestEqual(TEXT("min_spacing_cm clamped to 40"), Tuning.MinSpacingCm, 40.f);
	TestEqual(TEXT("arrive_m in metres"), Tuning.ArriveCm, 250.f);
	TestEqual(TEXT("a missing key keeps the default"), Tuning.WallMarginCm, FFormationConfinedTuning().WallMarginCm);

	// The shipped file mirrors the struct defaults (the agent's proposal, pending the game designer's confirmation).
	FString Text;
	TSharedPtr<FJsonObject> File;
	if (!TestTrue(TEXT("squad_confined.json reads"), FFileHelper::LoadFileToString(Text, *FormationConfinedTuning::GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File) && File.IsValid()))
	{
		return false;
	}
	const FFormationConfinedTuning Defaults;
	const FFormationConfinedTuning FromFile = FormationConfinedTuning::FromJson(*File, FFormationConfinedTuning());
	TestEqual(TEXT("enabled"), FromFile.bEnabled, Defaults.bEnabled);
	TestEqual(TEXT("probe_rays"), FromFile.ProbeRays, Defaults.ProbeRays);
	TestEqual(TEXT("probe_m"), FromFile.ProbeCm, Defaults.ProbeCm);
	TestEqual(TEXT("confined_median_m"), FromFile.ConfinedMedianCm, Defaults.ConfinedMedianCm);
	TestEqual(TEXT("min_spacing_cm"), FromFile.MinSpacingCm, Defaults.MinSpacingCm);
	TestEqual(TEXT("wall_margin_cm"), FromFile.WallMarginCm, Defaults.WallMarginCm);
	TestEqual(TEXT("ring_step_cm"), FromFile.RingStepCm, Defaults.RingStepCm);
	TestEqual(TEXT("min_radius_cm"), FromFile.MinRadiusCm, Defaults.MinRadiusCm);
	TestEqual(TEXT("arrive_m"), FromFile.ArriveCm, Defaults.ArriveCm);
	TestEqual(TEXT("level_dz_cm"), FromFile.LevelDzCm, Defaults.LevelDzCm);
	TestEqual(TEXT("recenter_cm"), FromFile.RecenterCm, Defaults.RecenterCm);
	TestEqual(TEXT("refresh_s"), FromFile.RefreshSeconds, Defaults.RefreshSeconds);
	TestEqual(TEXT("way_half_width_cm"), FromFile.WayHalfWidthCm, Defaults.WayHalfWidthCm);
	TestEqual(TEXT("way_min_cm"), FromFile.WayMinCm, Defaults.WayMinCm);
	TestEqual(TEXT("pass_clear_cm"), FromFile.PassClearCm, Defaults.PassClearCm);
	TestEqual(TEXT("depth_weight"), FromFile.DepthWeight, Defaults.DepthWeight);
	TestEqual(TEXT("pass_free_cm"), FromFile.PassFreeCm, Defaults.PassFreeCm);
	TestEqual(TEXT("face_walk_cm"), FromFile.FaceWalkCm, Defaults.FaceWalkCm);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
