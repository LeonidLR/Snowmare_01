// CodexTactics.Formation.Terrain.* - the terrain classifier and the class -> formation mapping (Formation/FormationTerrainRules; design
// docs/design/FORMATION_SECTORS.md "Selection", mirrors Tools/TrainingGround/src/movement/rules.test.ts). UE-only (no Godot reference).
// The leader stands at the origin heading +X: left = -Y, right = +Y.

#include "Misc/AutomationTest.h"
#include "Formation/FormationTerrainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_TERRAIN_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Terrain." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationTerrainTestUtil
{
	FFormationWall MakeWall(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	EFormationTerrainClass Raw(const TArray<FFormationWall>& Walls, const FVector2D& Pos = FVector2D::ZeroVector, float Yaw = 0.f,
		const TOptional<FVector2D>& Destination = TOptional<FVector2D>())
	{
		return FormationTerrainRules::ClassifyRaw(Walls, Pos, Yaw, FFormationConfig(), Destination);
	}
}

FORMATION_TERRAIN_TEST(FFormationTerrainClassesTest, "Classes")
bool FFormationTerrainClassesTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	TestEqual(TEXT("No walls: open"), Raw({}), EFormationTerrainClass::Open);
	const FFormationWall Left = MakeWall(-2000.0, -300.0, 2000.0, -300.0);
	const FFormationWall Right = MakeWall(-2000.0, 300.0, 2000.0, 300.0);
	TestEqual(TEXT("A wall 3 m to the left: wall_left"), Raw({ Left }), EFormationTerrainClass::WallLeft);
	TestEqual(TEXT("A wall 3 m to the right: wall_right"), Raw({ Right }), EFormationTerrainClass::WallRight);
	TestEqual(TEXT("Both, 6 m apart (5.6 m free): corridor"), Raw({ Left, Right }), EFormationTerrainClass::Corridor);
	// v1.2 (user decision): ANY passage walled on both sides within classify.probe_side is a column, whatever its width.
	const TArray<FFormationWall> Street = { MakeWall(-2000.0, -380.0, 2000.0, -380.0), MakeWall(-2000.0, 380.0, 2000.0, 380.0) };
	TestEqual(TEXT("A 7.6 m street: corridor (column)"), Raw(Street), EFormationTerrainClass::Corridor);
	const TOptional<float> StreetWidth = FormationTerrainRules::PassageWidth(Street, FVector2D::ZeroVector, 0.f, FFormationConfig());
	TestTrue(TEXT("... with the wide (herringbone) sectors: 720 cm >= column.narrow_width"), StreetWidth.IsSet()
		&& FMath::IsNearlyEqual(StreetWidth.GetValue(), 720.f, 0.01f) && StreetWidth.GetValue() >= FFormationConfig().ColumnNarrowWidth);
	// v1.2: at_wall only on ARRIVAL at the move's destination.
	const TArray<FFormationWall> Ahead = { MakeWall(120.0, -500.0, 120.0, 500.0, 20.f) };
	TestEqual(TEXT("A wall 1.2 m ahead, arriving: at_wall"), Raw(Ahead, FVector2D::ZeroVector, 0.f, FVector2D(20.0, 0.0)), EFormationTerrainClass::AtWall);
	TestEqual(TEXT("The same wall, walking on (destination 10 m away): not at_wall"), Raw(Ahead, FVector2D::ZeroVector, 0.f, FVector2D(-1000.0, 0.0)),
		EFormationTerrainClass::Open);
	TestEqual(TEXT("The same wall, no move: not at_wall"), Raw(Ahead), EFormationTerrainClass::Open);
	TestTrue(TEXT("Arriving within classify.arrive_dist"), FormationTerrainRules::IsArriving(FVector2D::ZeroVector, FVector2D(50.0, 0.0), FFormationConfig())
		&& !FormationTerrainRules::IsArriving(FVector2D::ZeroVector, FVector2D(51.0, 0.0), FFormationConfig())
		&& !FormationTerrainRules::IsArriving(FVector2D::ZeroVector, TOptional<FVector2D>(), FFormationConfig()));
	TestEqual(TEXT("A wall 5 m ahead: not at_wall yet"), Raw({ MakeWall(500.0, -100.0, 500.0, 100.0, 20.f) }), EFormationTerrainClass::Open);
	TestEqual(TEXT("A wall 6 m to the left: out of the probe, open"), Raw({ MakeWall(-2000.0, -600.0, 2000.0, -600.0) }), EFormationTerrainClass::Open);
	// Rotated + shifted scene: the same left wall seen from a leader heading +Y (left = +X).
	TestEqual(TEXT("Heading +Y, wall at +X: wall_left"), Raw({ MakeWall(1300.0, -2000.0, 1300.0, 2000.0) }, FVector2D(1000.0, 0.0), 90.f),
		EFormationTerrainClass::WallLeft);
	// Lateral distance, not ray length: the side clearance of a wall 3 m off is 3 m minus half its thickness.
	const TOptional<float> Clearance = FormationTerrainRules::SideClearance({ Left }, FVector2D::ZeroVector, 0.f, -1, 400.f);
	TestTrue(TEXT("Left clearance 280 cm"), Clearance.IsSet() && FMath::IsNearlyEqual(Clearance.GetValue(), 280.f, 0.01f));
	TestFalse(TEXT("No right clearance"), FormationTerrainRules::SideClearance({ Left }, FVector2D::ZeroVector, 0.f, 1, 400.f).IsSet());
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainGapTest, "WallGapIsBridged")
bool FFormationTerrainGapTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	// Wall - 2 m gap - wall on the left, the leader right at the gap: the slanted side rays still see the wall (no flicker to open).
	TestEqual(TEXT("2 m gap: still wall_left"), Raw({ MakeWall(-2000.0, -300.0, -100.0, -300.0), MakeWall(100.0, -300.0, 2000.0, -300.0) }),
		EFormationTerrainClass::WallLeft);
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainRayTest, "RayMeetsTheWallFace")
bool FFormationTerrainRayTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const TArray<FFormationWall> Walls = { MakeWall(500.0, -100.0, 500.0, 100.0, 40.f) };
	const TOptional<float> Hit = FormationTerrainRules::RayHit(Walls, FVector2D::ZeroVector, FVector2D(1.0, 0.0), 1000.f);
	TestTrue(TEXT("Hit at the near face (480 cm)"), Hit.IsSet() && FMath::IsNearlyEqual(Hit.GetValue(), 480.f, 0.01f));
	TestFalse(TEXT("Out of range"), FormationTerrainRules::RayHit(Walls, FVector2D::ZeroVector, FVector2D(1.0, 0.0), 400.f).IsSet());
	TestFalse(TEXT("A ray along a thin wall (parallel): no hit"), FormationTerrainRules::RayHit({ MakeWall(100.0, 0.0, 900.0, 0.0, 0.f) }, FVector2D::ZeroVector,
		FVector2D(1.0, 0.0), 1000.f).IsSet());
	const TOptional<float> Inside = FormationTerrainRules::RayHit(Walls, FVector2D(505.0, 0.0), FVector2D(1.0, 0.0), 1000.f);
	TestTrue(TEXT("Inside the wall body: 0"), Inside.IsSet() && Inside.GetValue() == 0.f);
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainHysteresisTest, "PathHoldTighterAtOnceLooserAfterTheHoldDistance")
bool FFormationTerrainHysteresisTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const FFormationConfig Config;
	const float Hold = Config.ClassifyHoldDistance;
	using EC = EFormationTerrainClass;
	const auto State = [](EC Current)
	{
		FFormationClassifyState Out;
		Out.Current = Current;
		return Out;
	};
	TestTrue(TEXT("Ranks open < wall beside < passage < at wall"), FormationTerrainRules::ClassRank(EC::Open) == 0 && FormationTerrainRules::ClassRank(EC::WallLeft) == 1
		&& FormationTerrainRules::ClassRank(EC::WallRight) == 1 && FormationTerrainRules::ClassRank(EC::Corridor) == 2 && FormationTerrainRules::ClassRank(EC::AtWall) == 3);
	// No history: the raw class at once.
	TestTrue(TEXT("Start: the raw class at once"), FormationTerrainRules::AdvanceState(EC::WallLeft, TOptional<float>(), FFormationClassifyState(), 0.f, Config).Current
		== TOptional<EC>(EC::WallLeft));
	// Tighter at once, even standing still.
	TestTrue(TEXT("Open -> wall beside at once"), FormationTerrainRules::AdvanceState(EC::WallLeft, TOptional<float>(), State(EC::Open), 0.f, Config).Current == TOptional<EC>(EC::WallLeft));
	const FFormationClassifyState IntoPassage = FormationTerrainRules::AdvanceState(EC::Corridor, TOptional<float>(460.f), State(EC::WallLeft), 10.f, Config);
	TestTrue(TEXT("Wall beside -> passage at once, with its width"), IntoPassage.Current == TOptional<EC>(EC::Corridor) && IntoPassage.PassageWidth == TOptional<float>(460.f)
		&& !IntoPassage.Candidate.IsSet());
	// Looser only after the hold distance of PATH.
	FFormationClassifyState Walk = State(EC::WallLeft);
	float Walked = 0.f;
	while (Walk.Current == TOptional<EC>(EC::WallLeft) && Walked < 5000.f)
	{
		Walk = FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), Walk, 50.f, Config);
		Walked += 50.f;
	}
	TestTrue(FString::Printf(TEXT("Wall beside -> open after %.0f cm of path (hold %.0f)"), Walked, Hold), FMath::IsNearlyEqual(Walked, Hold));
	FFormationClassifyState Waiting = FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), State(EC::WallLeft), Hold - 1.f, Config);
	TestTrue(TEXT("1 cm short: still the wall, open is the candidate"), Waiting.Current == TOptional<EC>(EC::WallLeft) && Waiting.Candidate == TOptional<EC>(EC::Open)
		&& FMath::IsNearlyEqual(Waiting.CandidateDistance, Hold - 1.f));
	// Standing still accumulates nothing.
	const FFormationClassifyState Still = FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), Waiting, 0.f, Config);
	TestTrue(TEXT("Standing still: no progress"), Still.Current == TOptional<EC>(EC::WallLeft) && FMath::IsNearlyEqual(Still.CandidateDistance, Hold - 1.f));
	// A different raw class restarts the candidate (equal rank also waits).
	const FFormationClassifyState Other = FormationTerrainRules::AdvanceState(EC::WallRight, TOptional<float>(), Waiting, 20.f, Config);
	TestTrue(TEXT("wall_left -> wall_right (equal rank) waits, the candidate restarts"), Other.Current == TOptional<EC>(EC::WallLeft)
		&& Other.Candidate == TOptional<EC>(EC::WallRight) && FMath::IsNearlyEqual(Other.CandidateDistance, 20.f));
	// The raw class back to the current one drops the candidate (a short gap in a wall never flips the echelon).
	const FFormationClassifyState Back = FormationTerrainRules::AdvanceState(EC::WallLeft, TOptional<float>(), Waiting, 50.f, Config);
	TestTrue(TEXT("Back to the wall: candidate dropped"), Back.Current == TOptional<EC>(EC::WallLeft) && !Back.Candidate.IsSet() && Back.CandidateDistance == 0.f);
	// Leaving at_wall (a new move) at once.
	TestTrue(TEXT("Leaving the wall layout: at once"), FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), State(EC::AtWall), 0.f, Config).Current
		== TOptional<EC>(EC::Open));
	TestTrue(TEXT("Arrival at a wall from a passage: at once"), FormationTerrainRules::AdvanceState(EC::AtWall, TOptional<float>(), State(EC::Corridor), 0.f, Config).Current
		== TOptional<EC>(EC::AtWall));
	// The passage width is held with the corridor class after the leader has left the passage (the column's tail keeps narrow sectors).
	FFormationClassifyState Column = IntoPassage;
	Column = FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), Column, 100.f, Config);
	TestTrue(TEXT("Out of the passage, within the hold: still corridor, width 460 held"), Column.Current == TOptional<EC>(EC::Corridor)
		&& Column.PassageWidth == TOptional<float>(460.f));
	Column = FormationTerrainRules::AdvanceState(EC::Open, TOptional<float>(), Column, Hold, Config);
	TestTrue(TEXT("After the hold: open, no width"), Column.Current == TOptional<EC>(EC::Open) && !Column.PassageWidth.IsSet());
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainTrackerTest, "TrackerAlongARoute")
bool FFormationTerrainTrackerTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const FFormationConfig Config;
	const TOptional<FVector2D> NoMove;
	// A left wall for x in [-2000, 1000], then open ground; the leader walks +X in 50 cm steps.
	const TArray<FFormationWall> Walls = { MakeWall(-2000.0, -300.0, 1000.0, -300.0) };
	FFormationTerrainTracker Tracker;
	TestEqual(TEXT("Before the first Step: open"), Tracker.GetClass(), EFormationTerrainClass::Open);
	TestEqual(TEXT("First Step: the raw class"), Tracker.Step(Walls, FVector2D(0.0, 0.0), 0.f, NoMove, 0.f, Config), EFormationTerrainClass::WallLeft);
	double SwitchX = -1.0;
	for (double X = 50.0; X <= 3000.0; X += 50.0)
	{
		if (Tracker.Step(Walls, FVector2D(X, 0.0), 0.f, NoMove, 50.f, Config) == EFormationTerrainClass::Open && SwitchX < 0.0)
		{
			SwitchX = X;
		}
	}
	// The slanted side rays see the wall up to ~x = 1000 + 400 / tan 60; then open needs hold_distance of path.
	const double LastSeen = 1000.0 + 280.0 / FMath::Tan(FMath::DegreesToRadians(60.0));
	TestTrue(FString::Printf(TEXT("Open only after the hold distance past the wall's end (at x = %.0f, wall seen to ~%.0f)"), SwitchX, LastSeen),
		SwitchX >= LastSeen + Config.ClassifyHoldDistance - 50.0 && SwitchX <= LastSeen + Config.ClassifyHoldDistance + 100.0);
	// Standing at the wall's end does not wear the hold down.
	FFormationTerrainTracker Standing;
	Standing.Step(Walls, FVector2D(0.0, 0.0), 0.f, NoMove, 0.f, Config);
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		Standing.Step(Walls, FVector2D(2000.0, 0.0), 0.f, NoMove, 0.f, Config);
	}
	TestEqual(TEXT("Standing still in the open: still wall_left"), Standing.GetClass(), EFormationTerrainClass::WallLeft);
	// Arrival at a wall: at once; Reset forgets.
	TestEqual(TEXT("Arrival: at_wall at once"), Standing.Step({ MakeWall(120.0, -500.0, 120.0, 500.0, 20.f) }, FVector2D::ZeroVector, 0.f, FVector2D::ZeroVector, 0.f, Config),
		EFormationTerrainClass::AtWall);
	Standing.Reset();
	TestFalse(TEXT("Reset: no history"), Standing.State.Current.IsSet());
	// The passage width follows the corridor class.
	FFormationTerrainTracker Passage;
	Passage.Step({ MakeWall(-2000.0, -250.0, 2000.0, -250.0), MakeWall(-2000.0, 250.0, 2000.0, 250.0) }, FVector2D::ZeroVector, 0.f, NoMove, 0.f, Config);
	TestTrue(TEXT("In a 4.6 m passage: corridor with its width"), Passage.GetClass() == EFormationTerrainClass::Corridor && Passage.GetPassageWidth().IsSet()
		&& FMath::IsNearlyEqual(Passage.GetPassageWidth().GetValue(), 460.f, 0.01f));
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainMappingTest, "ClassToFormation")
bool FFormationTerrainMappingTest::RunTest(const FString&)
{
	TestEqual(TEXT("Open, 4: diamond"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 4), EFormationKind::Diamond);
	TestEqual(TEXT("Open, 3: wedge"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 3), EFormationKind::Wedge);
	TestEqual(TEXT("Open, 2: wedge (= the pair)"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 2), EFormationKind::Wedge);
	TestEqual(TEXT("Wall on the left: echelon AWAY from it (right)"), FormationTerrainRules::FormationFor(EFormationTerrainClass::WallLeft, 4),
		EFormationKind::EchelonRight);
	TestEqual(TEXT("Wall on the right: echelon left"), FormationTerrainRules::FormationFor(EFormationTerrainClass::WallRight, 3), EFormationKind::EchelonLeft);
	TestEqual(TEXT("Narrow: column"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Corridor, 4), EFormationKind::Column);
	TestEqual(TEXT("Arrival at a wall: the wall layout"), FormationTerrainRules::FormationFor(EFormationTerrainClass::AtWall, 4), EFormationKind::Wall);
	// v1.2 (user decision): open ground in a blizzard -> the blizzard tight diamond; walls / passages keep their rules.
	TestEqual(TEXT("Open in a blizzard: blizzard diamond"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 4, true), EFormationKind::Blizzard);
	TestEqual(TEXT("Open in a blizzard, 3: blizzard"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Open, 3, true), EFormationKind::Blizzard);
	TestEqual(TEXT("Corridor in a blizzard: still the column"), FormationTerrainRules::FormationFor(EFormationTerrainClass::Corridor, 4, true), EFormationKind::Column);
	TestEqual(TEXT("Wall left in a blizzard: still echelon right"), FormationTerrainRules::FormationFor(EFormationTerrainClass::WallLeft, 4, true),
		EFormationKind::EchelonRight);
	return true;
}

FORMATION_TERRAIN_TEST(FFormationTerrainPassageWidthTest, "PassageWidthAndDeadEnd")
bool FFormationTerrainPassageWidthTest::RunTest(const FString&)
{
	using namespace FormationTerrainTestUtil;
	const FFormationConfig Config;
	const FFormationWall Left = MakeWall(-2000.0, -250.0, 2000.0, -250.0);
	const FFormationWall Right = MakeWall(-2000.0, 250.0, 2000.0, 250.0);
	// v1.1: the free width between the walls (centre lines 5 m apart, 40 cm thick -> 4.6 m free).
	const TOptional<float> Width = FormationTerrainRules::PassageWidth({ Left, Right }, FVector2D::ZeroVector, 0.f, Config);
	TestTrue(TEXT("Passage 460 cm"), Width.IsSet() && FMath::IsNearlyEqual(Width.GetValue(), 460.f, 0.01f));
	TestFalse(TEXT("One side open: no passage"), FormationTerrainRules::PassageWidth({ Left }, FVector2D::ZeroVector, 0.f, Config).IsSet());
	TestFalse(TEXT("No walls: no passage"), FormationTerrainRules::PassageWidth({}, FVector2D::ZeroVector, 0.f, Config).IsSet());

	// Across the heading: a wall within classify.side_ignore_perp_deg of perpendicular is ahead / behind, not beside.
	const FFormationWall Across = MakeWall(180.0, -1000.0, 180.0, 1000.0);
	TestTrue(TEXT("A perpendicular wall is across the heading"), FormationTerrainRules::IsAcrossHeading(Across, 0.f, Config.ClassifySideIgnorePerpDeg));
	TestTrue(TEXT("Its direction does not matter (B -> A)"), FormationTerrainRules::IsAcrossHeading(MakeWall(180.0, 1000.0, 180.0, -1000.0), 0.f, 30.f));
	TestFalse(TEXT("A parallel wall is beside"), FormationTerrainRules::IsAcrossHeading(Left, 0.f, Config.ClassifySideIgnorePerpDeg));
	const auto Slanted = [](double Deg) { return MakeWall(0.0, 0.0, 1000.0 * FMath::Cos(FMath::DegreesToRadians(Deg)), 1000.0 * FMath::Sin(FMath::DegreesToRadians(Deg))); };
	TestTrue(TEXT("65 deg off parallel (band 30): across"), FormationTerrainRules::IsAcrossHeading(Slanted(65.0), 0.f, 30.f));
	TestFalse(TEXT("55 deg off parallel: beside"), FormationTerrainRules::IsAcrossHeading(Slanted(55.0), 0.f, 30.f));
	// A dead end 1.6 m ahead (face), just outside at_wall: the slanted side rays used to read it as two side walls 2.8 m off -> corridor.
	TestEqual(TEXT("Dead end ahead: still open (no column flip before arriving)"), Raw({ Across }), EFormationTerrainClass::Open);
	FFormationConfig Unfiltered = Config;
	Unfiltered.ClassifySideIgnorePerpDeg = 0.f;
	TestEqual(TEXT("Without the v1.1 filter the dead end read as a corridor"), FormationTerrainRules::ClassifyRaw({ Across }, FVector2D::ZeroVector, 0.f, Unfiltered),
		EFormationTerrainClass::Corridor);
	TestFalse(TEXT("No passage at a dead end"), FormationTerrainRules::PassageWidth({ Across }, FVector2D::ZeroVector, 0.f, Config).IsSet());
	// Closer, the at_wall rays (first, unfiltered) see it.
	TestEqual(TEXT("Arriving at the dead end: at_wall"), Raw({ Across }, FVector2D(40.0, 0.0), 0.f, FVector2D(40.0, 0.0)), EFormationTerrainClass::AtWall);
	return true;
}

#undef FORMATION_TERRAIN_TEST

#endif
