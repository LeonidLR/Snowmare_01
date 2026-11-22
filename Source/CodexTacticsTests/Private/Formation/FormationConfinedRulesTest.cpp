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

FORMATION_CONFINED_TEST(FFormationConfinedLinesTest, "NoPlaceRoundTheJambCorner")
bool FFormationConfinedLinesTest::RunTest(const FString&)
{
	// The 2.5 m hall, entered through its -X door; the runtime's entry test (navmesh line + jamb clearance) sees every place but the far
	// corners (x > 20, |y| > 50: round a corner from the entry point). No place there - with the test off the cheapest places are there.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	const FVector2D Entry(-150.0, 0.0);
	const auto BehindJamb = [](const FVector2D& Place) { return Place.X > 20.0 && FMath::Abs(Place.Y) > 50.0; };
	FFormationConfinedLineTests Lines;
	Lines.FromEntry = [&BehindJamb](const FVector2D& Place) { return !BehindJamb(Place); };
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-200.0, 10.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, -10.0)) };
	const FFormationConfinedResult Without = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning);
	TestTrue(TEXT("without the test a place lies behind the jamb (the walk smoke's snag)"),
		BehindJamb(Without.Places[0]) || BehindJamb(Without.Places[1]));
	const FFormationConfinedResult With = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning, &Lines);
	TestEqual(TEXT("both placed"), With.ReplacedCount, 2);
	for (const FVector2D& Place : With.Places)
	{
		TestFalse(*FString::Printf(TEXT("(%.0f, %.0f) not behind the jamb"), Place.X, Place.Y), BehindJamb(Place));
		TestTrue(TEXT("inside the pocket"), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
	}
	// A test that sees nothing (a bad entry point) is dropped: the men still get places.
	FFormationConfinedLineTests Blind;
	Blind.FromEntry = [](const FVector2D&) { return false; };
	TestEqual(TEXT("blind entry test: still placed"),
		FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning, &Blind).ReplacedCount, 2);
	// A man already inside walks his own straight line: a place he cannot see is not "reachable" - another one is chosen.
	FFormationConfinedLineTests Own;
	Own.Straight = [](const FVector2D& From, const FVector2D& To) { return To.Y <= 0.0; }; // a wall across his way north
	const TArray<FFormationConfinedSlot> Inside{ Slot(FVector2D(-330.0, 0.0), FVector2D(0.0, 40.0)) };
	const FFormationConfinedResult Seen = FormationConfinedRules::CompressSlots(Pocket, Inside, TOptional<FVector2D>(), TOptional<FVector2D>(), Tuning, &Own);
	TestTrue(*FString::Printf(TEXT("his place (%.0f, %.0f) is one he sees"), Seen.Places[0].X, Seen.Places[0].Y), Seen.Places[0].Y <= 0.0);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedGapTest, "NoPlaceInTheDoorway")
bool FFormationConfinedGapTest::RunTest(const FString&)
{
	// The grey showcase S03 hall's probe (2.5 m hall, its 150 cm door north seen through by ray 4 alone): no place out along that ray in the
	// door (the man stood there - the leg's 5.84 ratio); a corridor's axis (long neighbours) keeps its reach.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	FFormationConfinedPocket Hall;
	Hall.Free = { 450.f, 200.f, 113.f, 89.f, 450.f, 149.f, 81.f, 62.f, 57.f, 62.f, 81.f, 84.f, 450.f, 199.f, 107.f, 268.f };
	TestEqual(TEXT("the door ray reaches only its shorter neighbour"), FormationConfinedRules::RayReach(Hall, 4, Tuning), 89.f - Tuning.WallMarginCm);
	TestFalse(TEXT("(0, 160) out in the door is outside"), FormationConfinedRules::IsInside(Hall, FVector2D(0.0, 160.0), Tuning));
	TestTrue(TEXT("(0, 79) in the hall is inside"), FormationConfinedRules::IsInside(Hall, FVector2D(0.0, 79.0), Tuning));
	for (const FVector2D& Place : FormationConfinedRules::Candidates(Hall, Tuning))
	{
		if (FMath::Abs(Place.X) < 1.0 && Place.Y > 0.0)
		{
			TestTrue(*FString::Printf(TEXT("door-ray candidate (%.0f, %.0f) stays in the hall"), Place.X, Place.Y), Place.Y <= 79.01);
		}
	}
	const FFormationConfinedPocket Narrow = RoomPocket(FVector2D::ZeroVector, 215.f, 40.f, 40.f, Tuning);
	TestEqual(TEXT("a narrow hall's axis keeps the pocket radius"), FormationConfinedRules::RayReach(Narrow, 0, Tuning),
		FormationConfinedRules::PocketRadius(Narrow, Tuning));
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedHeldTest, "HeldPlaceStaysThroatPlaceGoes")
bool FFormationConfinedHeldTest::RunTest(const FString&)
{
	// The 2.5 m hall entered from -X: a man walking in keeps the place he was given while it is valid (no swap as he moves); a held place that
	// became invalid (beyond a throat now) is replaced; a candidate beyond a throat (InRoom false: y < -60) is never given.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	const FVector2D Entry(-150.0, 0.0);
	FFormationConfinedSlot Walker = Slot(FVector2D(-330.0, 0.0), FVector2D(-200.0, 60.0));
	Walker.Held = FormationConfinedRules::RayDir(13, Tuning.ProbeRays) * 80.0; // ray 13 at 80 cm, a valid place
	const FFormationConfinedResult Kept = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Walker }, TOptional<FVector2D>(), Entry, Tuning);
	TestTrue(TEXT("the held place stays (the cheapest would be elsewhere now)"), Kept.Places[0].Equals(Walker.Held.GetValue(), 1e-6));
	FFormationConfinedLineTests Lines;
	Lines.InRoom = [](const FVector2D& Place) { return Place.Y >= -60.0; };
	const FFormationConfinedResult Moved = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Walker }, TOptional<FVector2D>(), Entry, Tuning, &Lines);
	TestFalse(TEXT("a held place beyond a throat is dropped"), Moved.Places[0].Equals(Walker.Held.GetValue(), 1.0));
	TestTrue(*FString::Printf(TEXT("the new place (%.0f, %.0f) is in the room"), Moved.Places[0].X, Moved.Places[0].Y), Moved.Places[0].Y >= -60.0);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedEntryOnPathTest, "Queue.EntryInsideTheDoorway")
bool FFormationConfinedEntryOnPathTest::RunTest(const FString&)
{
	// The 2.5 m hall (eroded +-90), its door in the -X wall: the leader's navmesh way in from the landing at (-300, 0). The pocket alone reaches
	// out through the door (its door rays are a corridor's axis), the throat test (InRoom: here x > -85) keeps the entry point inside the
	// doorway; the door side lies entry_back_cm back along the way.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	const TArray<FVector2D> Way{ FVector2D(-300.0, 0.0), FVector2D(-120.0, 0.0), FVector2D(0.0, 0.0) };
	const auto InRoom = [](const FVector2D& Point) { return Point.X > -85.0; };
	const TOptional<FFormationConfinedEntry> Entry = FormationConfinedRules::EntryOnPath(Pocket, Way, Tuning, InRoom);
	if (!TestTrue(TEXT("an entry on the way in"), Entry.IsSet()))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("inside (%.0f, %.0f) just inside the doorway"), Entry->Inside.X, Entry->Inside.Y),
		Entry->Inside.Equals(FVector2D(-80.0, 0.0), 0.5));
	TestTrue(*FString::Printf(TEXT("door side (%.0f, %.0f) 60 cm back along the way"), Entry->Outside.X, Entry->Outside.Y),
		Entry->Outside.Equals(FVector2D(-140.0, 0.0), 0.5));
	// Without the throat test the pocket's own reach decides (out on the landing within the pocket radius).
	const TOptional<FFormationConfinedEntry> Loose = FormationConfinedRules::EntryOnPath(Pocket, Way, Tuning);
	TestTrue(TEXT("no throat test: an entry still"), Loose.IsSet() && Loose->Inside.X <= -150.0);
	// A way that starts inside (no door crossed) or never gets in: none.
	TestFalse(TEXT("starts inside: none"), FormationConfinedRules::EntryOnPath(Pocket, TArray<FVector2D>{ FVector2D(-40.0, 0.0), FVector2D(0.0, 0.0) }, Tuning, InRoom).IsSet());
	TestFalse(TEXT("never inside: none"), FormationConfinedRules::EntryOnPath(Pocket, TArray<FVector2D>{ FVector2D(-400.0, 0.0), FVector2D(-300.0, 0.0) }, Tuning, InRoom).IsSet());
	// A way that crosses the pocket's edge, leaves it (round a pillar) and comes in again: the last run counts.
	const TArray<FVector2D> Wiggle{ FVector2D(-300.0, 0.0), FVector2D(-60.0, 0.0), FVector2D(-200.0, 0.0), FVector2D(0.0, 0.0) };
	const TOptional<FFormationConfinedEntry> Again = FormationConfinedRules::EntryOnPath(Pocket, Wiggle, Tuning, InRoom);
	TestTrue(TEXT("the last run inside"), Again.IsSet() && Again->Inside.Equals(FVector2D(-80.0, 0.0), 0.5));
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedQueueOrderTest, "Queue.OrderBlockAndStepRound")
bool FFormationConfinedQueueOrderTest::RunTest(const FString&)
{
	// The nearer the door the earlier; the last order holds until the later man is hysteresis nearer.
	const auto Is = [this](const TCHAR* What, const TArray<int32>& Got, const TArray<int32>& Want)
	{
		FString Text;
		for (const int32 Each : Got)
		{
			Text += FString::Printf(TEXT(" %d"), Each);
		}
		TestTrue(*FString::Printf(TEXT("%s (got%s)"), What, *Text), Got == Want);
	};
	const TArray<float> Keys{ 300.f, 100.f, 200.f };
	const TArray<int32> NoRank{ INDEX_NONE, INDEX_NONE, INDEX_NONE };
	Is(TEXT("new men by their way to the door"), FormationConfinedRules::QueueOrder(Keys, NoRank, 40.f), TArray<int32>{ 1, 2, 0 });
	const TArray<int32> Ranked{ 0, 1, 2 };
	Is(TEXT("20 cm nearer: the order holds"), FormationConfinedRules::QueueOrder(TArray<float>{ 100.f, 80.f, 300.f }, Ranked, 40.f), TArray<int32>{ 0, 1, 2 });
	Is(TEXT("50 cm nearer: they flip"), FormationConfinedRules::QueueOrder(TArray<float>{ 100.f, 50.f, 300.f }, Ranked, 40.f), TArray<int32>{ 1, 0, 2 });
	Is(TEXT("the last man far ahead moves up"), FormationConfinedRules::QueueOrder(TArray<float>{ 200.f, 300.f, 10.f }, Ranked, 40.f), TArray<int32>{ 2, 0, 1 });
	Is(TEXT("a new man queues behind the ranked ones with a like key"), FormationConfinedRules::QueueOrder(TArray<float>{ 100.f, 90.f }, TArray<int32>{ 0, INDEX_NONE }, 40.f),
		TArray<int32>{ 0, 1 });
	// Block detection: a team-mate within the radius, ahead on his way.
	const FFormationConfinedTuning Tuning;
	TestTrue(TEXT("a mate 80 cm ahead on his way blocks"), FormationConfinedRules::IsBlockedBy(FVector2D(0.0, 0.0), FVector2D(200.0, 0.0), FVector2D(80.0, 10.0), Tuning));
	TestFalse(TEXT("behind him: no"), FormationConfinedRules::IsBlockedBy(FVector2D(0.0, 0.0), FVector2D(200.0, 0.0), FVector2D(-80.0, 0.0), Tuning));
	TestFalse(TEXT("beside his way: no"), FormationConfinedRules::IsBlockedBy(FVector2D(0.0, 0.0), FVector2D(200.0, 0.0), FVector2D(40.0, 80.0), Tuning));
	TestFalse(TEXT("too far: no"), FormationConfinedRules::IsBlockedBy(FVector2D(0.0, 0.0), FVector2D(400.0, 0.0), FVector2D(150.0, 0.0), Tuning));

	// Step round a standing body (the walk smoke: the first man in clipped the leader standing 60 cm inside the hall door).
	const FVector2D Here(0.0, 0.0);
	const FVector2D Place(200.0, 0.0);
	const TArray<FVector2D> Leader{ FVector2D(100.0, -30.0) };
	const TOptional<FVector2D> Step = FormationConfinedRules::StepRoundBody(Here, Place, Leader, Tuning);
	TestTrue(*FString::Printf(TEXT("a body 30 cm off his way: step round on the way's side (%s)"), Step.IsSet() ? *Step->ToString() : TEXT("-")),
		Step.IsSet() && Step->Equals(FVector2D(100.0, -30.0 + Tuning.MinSpacingCm + FormationConfinedRules::StepRoundMarginCm), 0.01));
	const auto NotNorth = [](const FVector2D& Point) { return Point.Y < 0.0; };
	const TOptional<FVector2D> Other = FormationConfinedRules::StepRoundBody(Here, Place, Leader, Tuning, NotNorth);
	TestTrue(TEXT("that side off the navmesh: the other side"), Other.IsSet() && Other->Y < -30.0 - Tuning.MinSpacingCm);
	TestFalse(TEXT("a body 70 cm off his way (a graze): clear"), FormationConfinedRules::StepRoundBody(Here, Place, TArray<FVector2D>{ FVector2D(100.0, 70.0) }, Tuning).IsSet());
	TestFalse(TEXT("a body behind him: clear"), FormationConfinedRules::StepRoundBody(Here, Place, TArray<FVector2D>{ FVector2D(-50.0, 10.0) }, Tuning).IsSet());
	TestFalse(TEXT("both sides off the navmesh: none"),
		FormationConfinedRules::StepRoundBody(Here, Place, Leader, Tuning, [](const FVector2D&) { return false; }).IsSet());
	const TArray<FVector2D> Two{ FVector2D(150.0, 10.0), FVector2D(60.0, -10.0) };
	const TOptional<FVector2D> Nearest = FormationConfinedRules::StepRoundBody(Here, Place, Two, Tuning);
	TestTrue(TEXT("two bodies: round the first one ahead"), Nearest.IsSet() && FMath::IsNearlyEqual(Nearest->X, 60.0, 0.01));
	FFormationConfinedTuning NoStep = Tuning;
	NoStep.BodyClearCm = 0.f;
	TestFalse(TEXT("body_clear_cm 0: off"), FormationConfinedRules::StepRoundBody(Here, Place, Leader, NoStep).IsSet());
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedQueueDealTest, "Queue.FirstThroughTheDoorGoesDeepest")
bool FFormationConfinedQueueDealTest::RunTest(const FString&)
{
	// A 2.3 x 2.5 m hall (eroded +-80 x +-90), its door in the -X wall; the leader stands deep in it at (40, 0) (the pocket centre), his two
	// men come in from the landing. The man nearer the door (shorter way in) takes the deepest place his way in reaches past the leader, the
	// next one off the first man's way in; their ways swap -> their places swap; a man through the door keeps his; a block swaps places.
	using namespace FormationConfinedTestLocal;
	const FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D(40.0, 0.0), 80.f, 90.f, 45.f, Tuning);
	TestTrue(TEXT("confined"), FormationConfinedRules::IsConfined(Pocket, Tuning));
	FFormationConfinedQueue Queue;
	Queue.EntryInside = FVector2D(-70.0, 0.0);
	const FVector2D DoorSide(-130.0, 0.0);
	FFormationConfinedSlot Near = Slot(FVector2D(-330.0, 0.0), FVector2D(-200.0, 30.0));
	Near.ToEntryCm = 135.f;
	FFormationConfinedSlot Far = Slot(FVector2D(-480.0, 0.0), FVector2D(-240.0, -30.0));
	Far.ToEntryCm = 175.f;
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Near, Far }, TOptional<FVector2D>(),
		DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("queued"), Result.bQueued);
	TestEqual(TEXT("both placed"), Result.ReplacedCount, 2);
	TestEqual(TEXT("the man nearer the door is first"), Result.Rank[0], 0);
	TestEqual(TEXT("the other second"), Result.Rank[1], 1);
	const double Depth0 = FVector2D::Distance(Result.Places[0], Queue.EntryInside);
	const double Depth1 = FVector2D::Distance(Result.Places[1], Queue.EntryInside);
	TestTrue(*FString::Printf(TEXT("first (%.0f, %.0f) depth %.0f >= second (%.0f, %.0f) depth %.0f"), Result.Places[0].X, Result.Places[0].Y, Depth0,
		Result.Places[1].X, Result.Places[1].Y, Depth1), Depth0 >= Depth1 - 0.01);
	TestTrue(TEXT("the second place off the first man's way in"),
		FormationConfinedRules::DistanceToSegment(Result.Places[1], Queue.EntryInside, Result.Places[0]) >= Tuning.PassClearCm - 0.01f);
	TestTrue(TEXT("spaced"), MinPairDistance(Result.Places, Pocket.Center) >= Tuning.MinSpacingCm - 0.1f);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside"), Place.X, Place.Y), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
		TestFalse(TEXT("off the entry line"), FormationConfinedRules::IsOnEntry(Pocket, DoorSide, Place, Tuning));
	}

	// Their ways in swap: so do their places (nobody through the door yet).
	FFormationConfinedSlot NearSwapped = Near;
	NearSwapped.ToEntryCm = 175.f;
	FFormationConfinedSlot FarSwapped = Far;
	FarSwapped.ToEntryCm = 135.f;
	const FFormationConfinedResult Swapped = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ NearSwapped, FarSwapped },
		TOptional<FVector2D>(), DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("places swap with the order"), Swapped.Places[0].Equals(Result.Places[1], 1e-3) && Swapped.Places[1].Equals(Result.Places[0], 1e-3));
	// ... but within the hysteresis of their last ranks the order holds.
	NearSwapped.PrevRank = 0;
	FarSwapped.PrevRank = 1;
	NearSwapped.ToEntryCm = 160.f;
	FarSwapped.ToEntryCm = 135.f;
	const FFormationConfinedResult Held = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ NearSwapped, FarSwapped },
		TOptional<FVector2D>(), DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("25 cm: the order holds"), Held.Places[0].Equals(Result.Places[0], 1e-3) && Held.Rank[0] == 0);

	// The second man is through the door already (inside, past the entry point) on his held place: he keeps it, locked, whatever the order.
	FFormationConfinedSlot Inside = Slot(FVector2D(-480.0, 0.0), FMath::Lerp(Result.Places[1], Pocket.Center, 0.25));
	Inside.Held = Result.Places[1];
	Inside.ToEntryCm = 0.f;
	FFormationConfinedSlot Outside = Near;
	Outside.ToEntryCm = 60.f;
	const FFormationConfinedResult Locked = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Outside, Inside },
		TOptional<FVector2D>(), DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("the man inside is locked"), Locked.Locked[1]);
	TestTrue(TEXT("on his held place"), Locked.Places[1].Equals(Result.Places[1], 1e-3));
	TestFalse(TEXT("the man outside is not"), Locked.Locked[0]);
	TestTrue(TEXT("the man outside still gets a place"), Locked.Replaced[0] && !Locked.Places[0].Equals(Locked.Places[1], 1.0));

	// A block: a man through the door stands on his shallow place (-27, 67) right on the way in of the man behind him (his deep place
	// (40, 80)); with BlockedBy their held places swap - the blocker walks on to the deep place (locked there), the blocked man is dealt a
	// place again (spaced, off the blocker's way on).
	const FVector2D Shallow = Pocket.Center + FormationConfinedRules::RayDir(6, Tuning.ProbeRays) * 95.0;
	const FVector2D Deep(40.0, 80.0);
	FFormationConfinedSlot Blocked = Slot(FVector2D(-330.0, 0.0), FVector2D(-90.0, 10.0));
	Blocked.Held = Deep;
	FFormationConfinedSlot Blocker = Slot(FVector2D(-480.0, 0.0), Shallow);
	Blocker.Held = Shallow;
	Blocked.BlockedBy = 1;
	TestTrue(TEXT("the blocker stands on his way"), FormationConfinedRules::IsBlockedBy(FVector2D(-90.0, 10.0), Deep, Shallow, Tuning));
	const FFormationConfinedResult Unblocked = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Blocked, Blocker },
		TOptional<FVector2D>(), DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("swapped"), Unblocked.Swapped[0] && Unblocked.Swapped[1]);
	TestTrue(*FString::Printf(TEXT("the blocker (%.0f, %.0f) takes the deep place, locked"), Unblocked.Places[1].X, Unblocked.Places[1].Y),
		Unblocked.Places[1].Equals(Deep, 1e-3) && Unblocked.Locked[1]);
	TestTrue(*FString::Printf(TEXT("the blocked man (%.0f, %.0f) is placed again, spaced"), Unblocked.Places[0].X, Unblocked.Places[0].Y),
		Unblocked.Replaced[0] && MinPairDistance(Unblocked.Places, Pocket.Center) >= Tuning.MinSpacingCm - 0.1f);
	TestTrue(TEXT("off the blocker's way on"), FormationConfinedRules::DistanceToSegment(Unblocked.Places[0], Shallow, Deep) >= Tuning.PassClearCm - 0.01f);
	// Without the block both keep theirs (the blocker is through the door on a valid place; the man behind is dealt the deepest free one).
	Blocked.BlockedBy = INDEX_NONE;
	const FFormationConfinedResult Stays = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ Blocked, Blocker },
		TOptional<FVector2D>(), DoorSide, Tuning, nullptr, &Queue);
	TestTrue(TEXT("no block: the blocker keeps his place"), Stays.Places[1].Equals(Shallow, 1e-3) && Stays.Locked[1] && !Stays.Swapped[0]);
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedDoorMouthTest, "Queue.DoorMouthManDealtFromTheDoor")
bool FFormationConfinedDoorMouthTest::RunTest(const FString&)
{
	// The walk smoke's STUCK hall legs (S01 / S07 2026-11-22): the first man stands in the door mouth (5 cm from the inside entry point); his
	// own straight navmesh line to every place off the door axis is cut by the jamb. Counted as through the door he was dealt from there - the
	// shallowest place on the door axis - and stood in the door for good, the second man behind him on the landing. With the door mouth rule
	// he is dealt from the inside entry point: the deepest place, out of the door mouth; the second man (the last) gets a spaced place too.
	using namespace FormationConfinedTestLocal;
	FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D(40.0, 0.0), 80.f, 90.f, 45.f, Tuning);
	FFormationConfinedQueue Queue;
	Queue.EntryInside = FVector2D(-70.0, 0.0);
	const FVector2D DoorSide(-130.0, 0.0);
	FFormationConfinedSlot Mouth = Slot(FVector2D(-330.0, 0.0), FVector2D(-75.0, 5.0));
	Mouth.ToEntryCm = 5.f;
	FFormationConfinedSlot Behind = Slot(FVector2D(-480.0, 0.0), FVector2D(-200.0, 0.0));
	Behind.ToEntryCm = 130.f;
	FFormationConfinedLineTests Lines;
	// The jamb: from the door (x < -60) no straight line reaches a place off the door axis (|y| > 30).
	Lines.Straight = [](const FVector2D& From, const FVector2D& To) { return !(From.X < -60.0 && FMath::Abs(To.Y) > 30.0); };
	const TArray<FFormationConfinedSlot> Slots{ Mouth, Behind };

	FFormationConfinedTuning Before = Tuning;
	Before.DoorMouthCm = 0.f;
	const FFormationConfinedResult Old = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), DoorSide, Before, &Lines, &Queue);
	TestTrue(*FString::Printf(TEXT("door_mouth_cm 0 (the rule before): the man in the door is dealt a door-mouth place (%.0f, %.0f)"), Old.Places[0].X, Old.Places[0].Y),
		Old.Replaced[0] && FVector2D::Distance(Old.Places[0], Queue.EntryInside) < Tuning.DoorMouthCm);

	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), DoorSide, Tuning, &Lines, &Queue);
	TestTrue(TEXT("queued, both placed"), Result.bQueued && Result.ReplacedCount == 2);
	TestFalse(TEXT("the man in the door mouth is not through (not locked)"), Result.Locked[0]);
	TestEqual(TEXT("he is first"), Result.Rank[0], 0);
	const double Depth0 = FVector2D::Distance(Result.Places[0], Queue.EntryInside);
	const double Depth1 = FVector2D::Distance(Result.Places[1], Queue.EntryInside);
	TestTrue(*FString::Printf(TEXT("his place (%.0f, %.0f) depth %.0f lies out of the door mouth"), Result.Places[0].X, Result.Places[0].Y, Depth0),
		Depth0 >= Tuning.DoorMouthCm);
	TestTrue(*FString::Printf(TEXT("he goes deepest (%.0f >= %.0f)"), Depth0, Depth1), Depth0 >= Depth1 - 0.01);
	TestTrue(TEXT("spaced"), MinPairDistance(Result.Places, Pocket.Center) >= Tuning.MinSpacingCm - 0.1f);
	for (const FVector2D& Place : Result.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside"), Place.X, Place.Y), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
	}

	// Through the door (out of the mouth, on his place's way) he keeps his place, locked, as before.
	FFormationConfinedSlot In = Slot(FVector2D(-330.0, 0.0), FMath::Lerp(Queue.EntryInside, Result.Places[0], 0.6));
	In.Held = Result.Places[0];
	In.ToEntryCm = 0.f;
	const FFormationConfinedResult Kept = FormationConfinedRules::CompressSlots(Pocket, TArray<FFormationConfinedSlot>{ In, Behind }, TOptional<FVector2D>(),
		DoorSide, Tuning, &Lines, &Queue);
	TestTrue(TEXT("out of the mouth: locked on his place"), Kept.Locked[0] && Kept.Places[0].Equals(Result.Places[0], 1e-3));

	// A door-mouth place goes only to the last man: alone (the last) he may take the entry point itself when nothing else is free.
	FFormationConfinedTuning Tight = Tuning;
	Tight.MinRadiusCm = 0.f;
	FFormationConfinedPocket Small = RoomPocket(FVector2D(40.0, 0.0), 80.f, 90.f, 45.f, Tight);
	const FFormationConfinedResult Three = FormationConfinedRules::CompressSlots(Small, TArray<FFormationConfinedSlot>{ Mouth, Behind,
		Slot(FVector2D(-630.0, 0.0), FVector2D(-300.0, 0.0)) }, TOptional<FVector2D>(), DoorSide, Tight, &Lines, &Queue);
	for (int32 Each = 0; Each < 3; ++Each)
	{
		if (Three.Replaced[Each] && Three.Rank[Each] != INDEX_NONE && Three.Rank[Each] < 2)
		{
			TestTrue(*FString::Printf(TEXT("rank %d (%.0f, %.0f): not in the door mouth"), Three.Rank[Each] + 1, Three.Places[Each].X, Three.Places[Each].Y),
				FVector2D::Distance(Three.Places[Each], Queue.EntryInside) >= Tight.DoorMouthCm);
		}
	}
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedLeaderRoomTest, "Queue.LeaderMakesRoom")
bool FFormationConfinedLeaderRoomTest::RunTest(const FString&)
{
	// The walk smoke's S01 / S07 hall (2026-11-22): the inside entry point (2375, -5310), the leader standing at his click (2375, -5240) 70 cm
	// in; the first man in at the door mouth (2376, -5329) walking to his corridor place (2321, -5096) has the leader first on his way.
	using namespace FormationConfinedTestLocal;
	FFormationConfinedTuning Tuning;
	const FVector2D In(2375.0, -5310.0);
	const FVector2D Leader(2375.0, -5240.0);
	const FVector2D Mouth(2376.0, -5329.0);
	const FVector2D Place(2321.0, -5096.0);
	const TArray<FVector2D> Bodies{ Leader, FVector2D(2440.0, -5400.0) };
	TestEqual(TEXT("the leader is the first body on his way"), FormationConfinedRules::FirstBodyOnWay(Mouth, Place, Bodies, Tuning), 0);
	TestEqual(TEXT("nobody on a way clear of bodies"), FormationConfinedRules::FirstBodyOnWay(Mouth, FVector2D(2500.0, -5329.0), Bodies, Tuning), INDEX_NONE);
	// StepRoundBody walks round that same body (no walkable test: a side point).
	const TOptional<FVector2D> Round = FormationConfinedRules::StepRoundBody(Mouth, Place, Bodies, Tuning);
	TestTrue(TEXT("the step round is beside the leader"), Round.IsSet() && FMath::IsNearlyEqual(FVector2D::Distance(Round.GetValue(), Leader),
		FMath::Max(Tuning.BodyClearCm, Tuning.MinSpacingCm) + FormationConfinedRules::StepRoundMarginCm, 0.1));
	// No walkable side (the jamb, the hall wall): nothing - the runtime asks the leader to make room.
	TestFalse(TEXT("no walkable side: no step"), FormationConfinedRules::StepRoundBody(Mouth, Place, Bodies, Tuning, [](const FVector2D&) { return false; }).IsSet());

	// The leader steps straight away from the inside entry point to leader_room_cm from it (60 cm here: 70 -> 130).
	const TOptional<FVector2D> Step = FormationConfinedRules::LeaderRoomStep(Leader, In, 400.f, Tuning);
	TestTrue(TEXT("he steps"), Step.IsSet());
	if (Step.IsSet())
	{
		TestTrue(*FString::Printf(TEXT("to (%.0f, %.0f), %.0f cm from the door"), Step->X, Step->Y, FVector2D::Distance(Step.GetValue(), In)),
			FMath::IsNearlyEqual(FVector2D::Distance(Step.GetValue(), In), Tuning.LeaderRoomCm, 0.5) && FMath::IsNearlyEqual(Step->X, Leader.X, 0.5));
	}
	// At most leader_room_max_cm, at most the free navmesh distance less the wall margin.
	const TOptional<FVector2D> Far = FormationConfinedRules::LeaderRoomStep(FVector2D(2375.0, -5300.0), In, 400.f, Tuning);
	TestTrue(TEXT("capped at leader_room_max_cm"), Far.IsSet() && FMath::IsNearlyEqual(FVector2D::Distance(Far.GetValue(), FVector2D(2375.0, -5300.0)), Tuning.LeaderRoomMaxCm, 0.5));
	const TOptional<FVector2D> Wall = FormationConfinedRules::LeaderRoomStep(Leader, In, 40.f, Tuning);
	TestTrue(TEXT("capped by the free distance"), Wall.IsSet() && FMath::IsNearlyEqual(FVector2D::Distance(Wall.GetValue(), Leader), 40.0 - Tuning.WallMarginCm, 0.5));
	TestFalse(TEXT("no room ahead: no step"), FormationConfinedRules::LeaderRoomStep(Leader, In, 15.f, Tuning).IsSet());
	TestFalse(TEXT("already far enough from the door: no step"), FormationConfinedRules::LeaderRoomStep(FVector2D(2375.0, -5175.0), In, 400.f, Tuning).IsSet());
	Tuning.LeaderRoomCm = 0.f;
	TestFalse(TEXT("leader_room_cm 0: off"), FormationConfinedRules::LeaderRoomStep(Leader, In, 400.f, Tuning).IsSet());
	return true;
}

FORMATION_CONFINED_TEST(FFormationConfinedQueueSwitchTest, "Queue.SwitchOffIsTheCostRule")
bool FFormationConfinedQueueSwitchTest::RunTest(const FString&)
{
	// entry_queue 0 (or no queue input: no entry on the way in) gives exactly the round-3 cost rule's places; open ground is unchanged either way.
	using namespace FormationConfinedTestLocal;
	FFormationConfinedTuning Tuning;
	const FFormationConfinedPocket Pocket = RoomPocket(FVector2D::ZeroVector, 90.f, 90.f, 45.f, Tuning);
	const TArray<FFormationConfinedSlot> Slots{ Slot(FVector2D(-330.0, 0.0), FVector2D(-160.0, 10.0)), Slot(FVector2D(-480.0, 0.0), FVector2D(-300.0, -10.0)) };
	FFormationConfinedQueue Queue;
	Queue.EntryInside = FVector2D(-80.0, 0.0);
	const FVector2D Entry(-140.0, 0.0);
	const FFormationConfinedResult Cost = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning);
	TestFalse(TEXT("no queue input: not queued"), Cost.bQueued);
	Tuning.bEntryQueue = false;
	const FFormationConfinedResult Off = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning, nullptr, &Queue);
	TestFalse(TEXT("entry_queue 0: not queued"), Off.bQueued);
	TestTrue(TEXT("the same places as the cost rule"), Off.Places.Num() == 2 && Off.Places[0].Equals(Cost.Places[0], 1e-6) && Off.Places[1].Equals(Cost.Places[1], 1e-6));
	Tuning.bEntryQueue = true;
	FFormationConfinedPocket Open;
	Open.Free.Init(Tuning.ProbeCm, Tuning.ProbeRays);
	const FFormationConfinedResult Outdoors = FormationConfinedRules::CompressSlots(Open, Slots, FVector2D(-100.0, 0.0), Entry, Tuning, nullptr, &Queue);
	TestFalse(TEXT("open ground: not confined"), Outdoors.bConfined);
	TestFalse(TEXT("open ground: not queued"), Outdoors.bQueued);
	TestTrue(TEXT("open ground: slots as given"), Outdoors.Places[0].Equals(Slots[0].Desired, 1e-6) && Outdoors.Places[1].Equals(Slots[1].Desired, 1e-6));
	// The queue in the 2.5 m hall: both placed, inside, spaced, off the entry line (the same guarantees as the cost rule).
	const FFormationConfinedResult Queued = FormationConfinedRules::CompressSlots(Pocket, Slots, TOptional<FVector2D>(), Entry, Tuning, nullptr, &Queue);
	TestTrue(TEXT("queued"), Queued.bQueued);
	TestEqual(TEXT("both placed"), Queued.ReplacedCount, 2);
	for (const FVector2D& Place : Queued.Places)
	{
		TestTrue(*FString::Printf(TEXT("(%.0f, %.0f) inside"), Place.X, Place.Y), FormationConfinedRules::IsInside(Pocket, Place, Tuning));
		TestFalse(*FString::Printf(TEXT("(%.0f, %.0f) off the entry line"), Place.X, Place.Y), FormationConfinedRules::IsOnEntry(Pocket, Entry, Place, Tuning));
	}
	TestTrue(TEXT("spaced"), MinPairDistance(Queued.Places, FVector2D::ZeroVector) >= Tuning.MinSpacingCm - 0.1f);
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
	TestEqual(TEXT("jamb_clear_cm"), FromFile.JambClearCm, Defaults.JambClearCm);
	TestEqual(TEXT("jamb_sample_cm"), FromFile.JambSampleCm, Defaults.JambSampleCm);
	TestEqual(TEXT("gap_factor"), FromFile.GapFactor, Defaults.GapFactor);
	TestEqual(TEXT("throat_clear_cm"), FromFile.ThroatClearCm, Defaults.ThroatClearCm);
	TestEqual(TEXT("hold_places"), FromFile.bHoldPlaces, Defaults.bHoldPlaces);
	TestEqual(TEXT("entry_queue"), FromFile.bEntryQueue, Defaults.bEntryQueue);
	TestEqual(TEXT("entry_back_cm"), FromFile.EntryBackCm, Defaults.EntryBackCm);
	TestEqual(TEXT("entry_step_cm"), FromFile.EntryStepCm, Defaults.EntryStepCm);
	TestEqual(TEXT("queue_hysteresis_cm"), FromFile.QueueHysteresisCm, Defaults.QueueHysteresisCm);
	TestEqual(TEXT("block_speed_cm_s"), FromFile.BlockSpeedCmS, Defaults.BlockSpeedCmS);
	TestEqual(TEXT("block_s"), FromFile.BlockSeconds, Defaults.BlockSeconds);
	TestEqual(TEXT("block_radius_cm"), FromFile.BlockRadiusCm, Defaults.BlockRadiusCm);
	TestEqual(TEXT("block_cooldown_s"), FromFile.BlockCooldownSeconds, Defaults.BlockCooldownSeconds);
	TestEqual(TEXT("body_clear_cm"), FromFile.BodyClearCm, Defaults.BodyClearCm);
	TestEqual(TEXT("door_mouth_cm"), FromFile.DoorMouthCm, Defaults.DoorMouthCm);
	TestEqual(TEXT("leader_room_cm"), FromFile.LeaderRoomCm, Defaults.LeaderRoomCm);
	TestEqual(TEXT("leader_room_max_cm"), FromFile.LeaderRoomMaxCm, Defaults.LeaderRoomMaxCm);
	const TSharedRef<FJsonObject> QueueOff = MakeShared<FJsonObject>();
	QueueOff->SetNumberField(TEXT("entry_queue"), 0.0);
	QueueOff->SetNumberField(TEXT("block_radius_cm"), 10.0);
	const FFormationConfinedTuning Off = FormationConfinedTuning::FromJson(*QueueOff);
	TestFalse(TEXT("entry_queue 0"), Off.bEntryQueue);
	TestEqual(TEXT("block_radius_cm clamped to 40"), Off.BlockRadiusCm, 40.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
