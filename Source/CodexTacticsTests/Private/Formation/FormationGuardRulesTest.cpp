// CodexTactics.Formation.{Guard,Trail,Assign,Bypass,Corner}.* + Terrain.SideWidth / Sector.ClearLook - v2.1 / v2.1.1: the squad never splits
// round a wall, the bypass side by threat, slicing the pie, places by minimal travel, the turn fold, nobody looks into a wall (user
// decisions 2026-10-25 in docs/design/FORMATION_SECTORS.md, contract docs/design/FORMATION_CONTRACT.md 10; mirrors
// Tools/TrainingGround/src/movement/rulesV21.test.ts). UE-only (no Godot reference). UE axes: X forward, +Y right.

#include "Misc/AutomationTest.h"
#include "Formation/FormationAssignRules.h"
#include "Formation/FormationBypassRules.h"
#include "Formation/FormationCornerRules.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTerrainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_V21_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationV21TestUtil
{
	using ER = EFormationRole;
	using EK = EFormationKind;

	FFormationWall Seg(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	/** The user's case (lab lane B): a long wall 3 m to the LEFT of the leader, heading +X. */
	TArray<FFormationWall> WallLeft()
	{
		return { Seg(-3000.0, -300.0, 3000.0, -300.0) };
	}

	FFormationSlot Member(ER Role, double X, double Y)
	{
		FFormationSlot Out;
		Out.Role = Role;
		Out.Position = FVector2D(X, Y);
		return Out;
	}

	FVector2D SlotPosOf(const TArray<FFormationSlot>& Slots, ER Role)
	{
		const FFormationSlot* Slot = Slots.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
		return Slot ? Slot->Position : FVector2D(1e9, 1e9);
	}
}

FORMATION_V21_TEST(FFormationGuardMirrorTest, "Guard.CutEchelonTakesTheMirror")
bool FFormationGuardMirrorTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	TestTrue(TEXT("Echelon left along a wall on the left: P, L, Z cut off"),
		FormationGuardRules::BlockedRoles(WallLeft(), EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, C) == TArray<ER>({ ER::Right, ER::Left, ER::Rear }));
	TestEqual(TEXT("Echelon right there: whole"), FormationGuardRules::BlockedRoles(WallLeft(), EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, C).Num(), 0);
	const FFormationGuardState State = FormationGuardRules::Resolve(WallLeft(), EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("The request is never cleared"), State.Requested == TOptional<EK>(EK::EchelonLeft));
	TestTrue(TEXT("The mirror echelon right in effect, level 1"), State.Effective == TOptional<EK>(EK::EchelonRight) && State.Level == EFormationGuardLevel::Mirror);

	// Walls on both sides: both echelons cut -> the trail column; any cut formation -> the column; the wall layout is never guarded.
	const TArray<FFormationWall> Corridor = { Seg(-3000.0, -250.0, 3000.0, -250.0), Seg(-3000.0, 250.0, 3000.0, 250.0) };
	const FFormationGuardState Trail = FormationGuardRules::Resolve(Corridor, EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C);
	const FFormationGuardEffective Effective = FormationGuardRules::Effective(Trail);
	TestTrue(TEXT("Both echelons cut: the trail column (level 2)"), Trail.Level == EFormationGuardLevel::Trail && Effective.Formation == TOptional<EK>(EK::Column) && Effective.bTrail);
	TestTrue(TEXT("A cut diamond: the column"), FormationGuardRules::Resolve(Corridor, EK::Diamond, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Column));
	TestTrue(TEXT("The wall layout is never guarded"), FormationGuardRules::Resolve(Corridor, EK::Wall, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Wall));
	return true;
}

FORMATION_V21_TEST(FFormationGuardHysteresisTest, "Guard.FallbackAtOnceBackAfterTheHoldDistance")
bool FFormationGuardHysteresisTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	const float Hold = C.ClassifyHoldDistance;
	const TArray<FFormationWall> None;
	FFormationGuardState State = FormationGuardRules::Resolve(WallLeft(), EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("The fallback at once"), State.Level == EFormationGuardLevel::Mirror);
	// The wall is gone: whole again, but it waits for the path hold (no flicker at a short gap).
	State = FormationGuardRules::Resolve(None, EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.5f * Hold, C);
	TestTrue(TEXT("Half the hold: still the mirror, the request waits"), State.Effective == TOptional<EK>(EK::EchelonRight)
		&& State.Candidate == TOptional<EFormationGuardLevel>(EFormationGuardLevel::Requested));
	State = FormationGuardRules::Resolve(None, EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.f, C);
	TestTrue(TEXT("Standing still adds no path"), State.Effective == TOptional<EK>(EK::EchelonRight));
	State = FormationGuardRules::Resolve(None, EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.5f * Hold, C);
	TestTrue(TEXT("Back by itself after the hold"), State.Effective == TOptional<EK>(EK::EchelonLeft) && State.Level == EFormationGuardLevel::Requested);
	// A new request starts over at its own level.
	FFormationGuardState Folded = State;
	Folded.Level = EFormationGuardLevel::Trail;
	Folded.Effective = EK::Column;
	TestTrue(TEXT("A new request starts over"), FormationGuardRules::Resolve(WallLeft(), EK::Diamond, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, Folded, 0.f, C).Effective
		== TOptional<EK>(EK::Diamond));
	// The lookahead folds before a passage entrance (whole here, cut 5 m ahead).
	const TArray<FFormationWall> Entrance = { Seg(300.0, -250.0, 3000.0, -250.0), Seg(300.0, 250.0, 3000.0, 250.0) };
	FFormationPose Ahead;
	Ahead.Pos = FVector2D(C.GuardLookahead, 0.0);
	TestTrue(TEXT("No lookahead: the line stays"), FormationGuardRules::Resolve(Entrance, EK::Line, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Line));
	TestTrue(TEXT("Cut 5 m ahead: folds now"), FormationGuardRules::Resolve(Entrance, EK::Line, 4, FVector2D::ZeroVector, 0.f, Ahead, 0.f, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Column));
	return true;
}

FORMATION_V21_TEST(FFormationGuardChainTest, "Guard.ChainPairs")
bool FFormationGuardChainTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	using FPair = TPair<ER, ER>;
	TestTrue(TEXT("Echelon right K-L-P-Z"), FormationGuardRules::ChainPairs(EK::EchelonRight, 4) == TArray<FPair>({ FPair(ER::Commander, ER::Left), FPair(ER::Left, ER::Right), FPair(ER::Right, ER::Rear) }));
	TestTrue(TEXT("Column of 3: K-P-L"), FormationGuardRules::ChainPairs(EK::Column, 3) == TArray<FPair>({ FPair(ER::Commander, ER::Right), FPair(ER::Right, ER::Left) }));
	TestTrue(TEXT("Diamond: P-Z, L-Z"), FormationGuardRules::ChainPairs(EK::Diamond, 4) == TArray<FPair>({ FPair(ER::Right, ER::Rear), FPair(ER::Left, ER::Rear) }));
	TestTrue(TEXT("Hedgehog: the ring"), FormationGuardRules::ChainPairs(EK::Hedgehog, 4) == TArray<FPair>({ FPair(ER::Commander, ER::Right), FPair(ER::Right, ER::Rear),
		FPair(ER::Rear, ER::Left), FPair(ER::Left, ER::Commander) }));
	TestEqual(TEXT("A pair: none"), FormationGuardRules::ChainPairs(EK::Line, 2).Num(), 0);
	return true;
}

FORMATION_V21_TEST(FFormationGuardTurnTest, "Guard.SharpTurnFoldsWideFormations")
bool FFormationGuardTurnTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	const float Hold = C.ClassifyHoldDistance;
	const TArray<FFormationWall> None;
	TestEqual(TEXT("A straight path: 0"), FormationGuardRules::TurnAhead({ FVector2D(0.0, 0.0), FVector2D(2000.0, 0.0) }, 0.f, C), 0.f);
	TestTrue(TEXT("A 30 deg turn 2 m ahead"), FMath::IsNearlyEqual(FormationGuardRules::TurnAhead({ FVector2D(0.0, 0.0), FVector2D(200.0, 0.0), FVector2D(1200.0, 577.35) }, 0.f, C),
		30.f, 0.01f));
	TestEqual(TEXT("A turn beyond turn.lookahead does not count"), FormationGuardRules::TurnAhead({ FVector2D(0.0, 0.0), FVector2D(C.TurnLookahead + 10.0, 0.0),
		FVector2D(C.TurnLookahead + 10.0, 900.0) }, 0.f, C), 0.f);
	const float Sharp = C.TurnFoldDeg + 5.f;
	FFormationGuardState State = FormationGuardRules::Resolve(None, EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, {}, Sharp, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("Echelon before a sharp turn: the trail column"), State.Effective == TOptional<EK>(EK::Column) && State.Level == EFormationGuardLevel::Trail);
	TestTrue(TEXT("The line folds too"), FormationGuardRules::Resolve(None, EK::Line, 4, FVector2D::ZeroVector, 0.f, {}, Sharp, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Column));
	TestTrue(TEXT("Only wide formations fold"), FormationGuardRules::Resolve(None, EK::Diamond, 4, FVector2D::ZeroVector, 0.f, {}, Sharp, FFormationGuardState(), 0.f, C).Effective
		== TOptional<EK>(EK::Diamond));
	TestTrue(TEXT("A gentle turn: no fold"), FormationGuardRules::Resolve(None, EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, {}, C.TurnFoldDeg - 5.f, FFormationGuardState(),
		0.f, C).Effective == TOptional<EK>(EK::EchelonRight));
	State = FormationGuardRules::Resolve(None, EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.5f * Hold, C);
	TestTrue(TEXT("The turn behind: still folded"), State.Effective == TOptional<EK>(EK::Column));
	State = FormationGuardRules::Resolve(None, EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.5f * Hold, C);
	TestTrue(TEXT("Opens after the hold distance"), State.Effective == TOptional<EK>(EK::EchelonRight));
	return true;
}

FORMATION_V21_TEST(FFormationTrailTest, "Trail.ColumnFollowsThePath")
bool FFormationTrailTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	// A straight trail = the straight column.
	const TArray<FFormationSlot> Straight = FormationShapeRules::TrailSlots({ FVector2D(-2000.0, 0.0), FVector2D(0.0, 0.0) }, 0.f, 4, C);
	const TArray<FFormationSlot> Column = FormationShapeRules::Slots(EK::Column, 4, FVector2D::ZeroVector, 0.f, C);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(TEXT("Straight trail = the column"), Straight[Index].Role == Column[Index].Role && FVector2D::Distance(Straight[Index].Position, Column[Index].Position) < 1e-3);
	}
	// A right turn 4 m behind the leader round the end of a wall: the column bends with the path, no neighbours have the wall between them.
	const TArray<FFormationWall> Corner = { Seg(-2000.0, 300.0, -100.0, 300.0) };
	const TArray<FFormationSlot> Bent = FormationShapeRules::TrailSlots({ FVector2D(-2000.0, 400.0), FVector2D(0.0, 400.0), FVector2D(0.0, 0.0) }, -90.f, 4, C);
	const FVector2D Z = SlotPosOf(Bent, ER::Rear);
	TestTrue(FString::Printf(TEXT("Z on the first leg (%.1f, %.1f)"), Z.X, Z.Y), FMath::IsNearlyEqual(Z.Y, 400.0, 1e-3) && Z.X < 0.0);
	for (const TPair<ER, ER>& Pair : FormationGuardRules::ChainPairs(EK::Column, 4))
	{
		TestFalse(TEXT("No chain segment cut by the wall"), FormationMath::SegmentBlocked(SlotPosOf(Bent, Pair.Key), SlotPosOf(Bent, Pair.Value), Corner, 0.0));
	}
	TestTrue(TEXT("A slot's yaw is the trail direction there (Z on the +X leg)"), FMath::IsNearlyEqual(Bent.Last().Yaw, 0.f, 1e-3f));
	TestEqual(TEXT("No trail: no slots"), FormationShapeRules::TrailSlots({}, 0.f, 4, C).Num(), 0);
	return true;
}

FORMATION_V21_TEST(FFormationAssignTest, "Assign.MinimalTravelNoCrossing")
bool FFormationAssignTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	const auto Followers = [](const TArray<FFormationSlot>& Slots)
	{
		TArray<FVector2D> Out;
		for (int32 Index = 1; Index < Slots.Num(); ++Index)
		{
			Out.Add(Slots[Index].Position);
		}
		return Out;
	};
	// Echelon right (L 1st step, P 2nd) -> the column: L takes the 1st place, P the 2nd (nobody passes through anybody).
	const TArray<FFormationSlot> ColumnSlots = FormationShapeRules::Slots(EK::Column, 3, FVector2D::ZeroVector, 0.f, C);
	const TArray<FVector2D> From = Followers(FormationShapeRules::Slots(EK::EchelonRight, 3, FVector2D::ZeroVector, 0.f, C));
	const TArray<FVector2D> To = Followers(ColumnSlots);
	const TArray<int32> Pick = FormationAssignRules::AssignPlaces(From, To);
	TestTrue(TEXT("P (far) takes the 2nd place, L (near) the 1st"), Pick.Num() == 2 && ColumnSlots[1 + Pick[0]].Role == ER::Left && ColumnSlots[1 + Pick[1]].Role == ER::Right);
	TestFalse(TEXT("The two paths do not cross"), FormationMath::SegmentBlocked(From[0], To[Pick[0]], { Seg(From[1].X, From[1].Y, To[Pick[1]].X, To[Pick[1]].Y, 0.f) }, 0.0));
	// Ties keep the identity; empty input -> empty.
	TestTrue(TEXT("Nobody moves: identity"), FormationAssignRules::AssignPlaces({ FVector2D(0.0, 0.0), FVector2D(100.0, 0.0) }, { FVector2D(0.0, 0.0), FVector2D(100.0, 0.0) })
		== TArray<int32>({ 0, 1 }));
	TestTrue(TEXT("A tie keeps the earlier"), FormationAssignRules::AssignPlaces({ FVector2D(0.0, 0.0), FVector2D(0.0, 0.0) }, { FVector2D(50.0, 0.0), FVector2D(50.0, 0.0) })
		== TArray<int32>({ 0, 1 }));
	TestEqual(TEXT("Empty"), FormationAssignRules::AssignPlaces({}, {}).Num(), 0);
	// Four men, a mirror (echelon left -> right): every path pair stays apart.
	const TArray<FVector2D> Left4 = Followers(FormationShapeRules::Slots(EK::EchelonLeft, 4, FVector2D::ZeroVector, 0.f, C));
	const TArray<FVector2D> Right4 = Followers(FormationShapeRules::Slots(EK::EchelonRight, 4, FVector2D::ZeroVector, 0.f, C));
	const TArray<int32> Mirror = FormationAssignRules::AssignPlaces(Left4, Right4);
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = I + 1; J < 3; ++J)
		{
			TestFalse(FString::Printf(TEXT("Mirror: paths %d and %d do not cross"), I, J), FormationMath::SegmentBlocked(Left4[I], Right4[Mirror[I]],
				{ Seg(Left4[J].X, Left4[J].Y, Right4[Mirror[J]].X, Right4[Mirror[J]].Y, 0.f) }, 0.0));
		}
	}
	return true;
}

FORMATION_V21_TEST(FFormationSideWidthTest, "Terrain.SideWidthCountsAWallEndNotAcross")
bool FFormationSideWidthTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	// A wall along the heading on the right, ending just behind him: the classifier's side rays (60 / 90 / 120 deg) miss its end.
	const TArray<FFormationWall> EndBeside = { Seg(-1500.0, 110.0, -90.0, 110.0) };
	TestFalse(TEXT("The side rays miss it"), FormationTerrainRules::SideClearance(EndBeside, FVector2D::ZeroVector, 0.f, 1, C.ClassifyProbeSide).IsSet());
	const TOptional<float> End = FormationTerrainRules::SideWidth(EndBeside, FVector2D::ZeroVector, 0.f, C);
	TestTrue(TEXT("SideWidth counts the wall END beside him: narrow"), End.IsSet() && End.GetValue() < C.ColumnNarrowWidth);
	// A wall across the heading (straight ahead, or its end beside him) is never a side wall (491a24d).
	TestFalse(TEXT("A wall straight ahead: no side width"), FormationTerrainRules::SideWidth({ Seg(150.0, -1000.0, 150.0, 1000.0) }, FVector2D::ZeroVector, 0.f, C).IsSet());
	TestFalse(TEXT("The end of a wall across the heading beside him: no side width"),
		FormationTerrainRules::SideWidth({ Seg(20.0, 110.0, 20.0, 1500.0) }, FVector2D::ZeroVector, 0.f, C).IsSet());
	TestFalse(TEXT("Open both sides: unset"), FormationTerrainRules::SideWidth({}, FVector2D::ZeroVector, 0.f, C).IsSet());
	return true;
}

FORMATION_V21_TEST(FFormationBypassTest, "Bypass.SideByThreat")
bool FFormationBypassTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	using EW = EFormationWallSide;
	TestTrue(TEXT("UE: +Y = right"), FormationBypassRules::WallSideOf({ FVector2D(0.0, 0.0), FVector2D(2000.0, 0.0) }, Seg(500.0, 300.0, 1500.0, 300.0, 0.f)) == EW::Right);
	TestTrue(TEXT("-Y = left"), FormationBypassRules::WallSideOf({ FVector2D(0.0, 0.0), FVector2D(2000.0, 0.0) }, Seg(500.0, -300.0, 1500.0, -300.0, 0.f)) == EW::Left);
	const FFormationWall Oblique = Seg(500.0, -100.0, 1500.0, 400.0);
	const FFormationBypassChoice Free = FormationBypassRules::ChooseSide(Oblique, FVector2D::ZeroVector, FVector2D(2000.0, 0.0), {}, C);
	TestTrue(TEXT("No threat: the shortest side (round the near end, the wall on the right)"), Free.Reason == EFormationBypassReason::Shortest && Free.WallOn == Free.Shortest
		&& Free.Detour == 0.f && Free.Shortest == EW::Right);
	for (int32 Index = 1; Index < Free.Path.Num(); ++Index)
	{
		TestFalse(TEXT("The path does not cross the wall"), FormationMath::SegmentBlocked(Free.Path[Index - 1], Free.Path[Index], { Oblique }, 0.0));
	}
	// A known threat: the side the wall hides it from, if the detour <= bypass.max_detour; a forgotten threat is ignored.
	const FFormationWall Long = Seg(500.0, -100.0, 1900.0, 200.0);
	FFormationThreat Threat;
	Threat.Pos = FVector2D(1200.0, -900.0);
	const FFormationBypassChoice Shielded = FormationBypassRules::ChooseSide(Long, FVector2D::ZeroVector, FVector2D(2600.0, 0.0), { Threat }, C);
	TestTrue(TEXT("Threat on the left: shielded, the wall on the left"), Shielded.WallOn == EW::Left && Shielded.Reason == EFormationBypassReason::Shielded
		&& Shielded.Detour <= C.BypassMaxDetour);
	FFormationThreat Old = Threat;
	Old.AgeS = C.BypassThreatMemoryS + 1.f;
	TestTrue(TEXT("A forgotten threat: the shortest"), FormationBypassRules::ChooseSide(Long, FVector2D::ZeroVector, FVector2D(2600.0, 0.0), { Old }, C).Reason
		== EFormationBypassReason::Shortest);
	// A lopsided wall square to the route: the shielding side ~70 % longer -> too long, the shortest stays.
	FFormationThreat Far;
	Far.Pos = FVector2D(1300.0, -900.0);
	const FFormationBypassChoice TooLong = FormationBypassRules::ChooseSide(Seg(900.0, -200.0, 900.0, 1100.0), FVector2D::ZeroVector, FVector2D(1700.0, 0.0), { Far }, C);
	TestTrue(TEXT("Too long: the shortest side"), TooLong.Reason == EFormationBypassReason::TooLong && TooLong.WallOn == TooLong.Shortest);
	return true;
}

FORMATION_V21_TEST(FFormationCornerTest, "Corner.SlicingThePie")
bool FFormationCornerTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	// Nearing a free wall end on the left: the closest member looks at the corner; past it, behind the wall; then the watch ends.
	const TArray<FFormationWall> Walls = { Seg(-3000.0, -300.0, 200.0, -300.0) };
	const TArray<FFormationSlot> Members = { Member(ER::Commander, 0.0, 0.0), Member(ER::Left, -280.0, -200.0), Member(ER::Right, -280.0, 200.0) };
	const FFormationCornerResult A = FormationCornerRules::Watch(Walls, FVector2D::ZeroVector, 0.f, Members, FFormationCornerState(), C);
	TestTrue(TEXT("The leader is the closest to the end"), A.State.Left.IsSet() && A.State.Left->Role == ER::Commander && !A.State.Right.IsSet());
	TestTrue(TEXT("He looks at the corner, grazing its end"), A.Looks.Num() == 1
		&& FormationMath::AngleBetween(A.Looks[0].Yaw, FormationMath::YawOf(FVector2D(200.0 + 20.0 + 50.0, -300.0))) < 1.f);
	TArray<FFormationSlot> Past = Members;
	Past[0].Position = FVector2D(400.0, 0.0);
	const FFormationCornerResult B = FormationCornerRules::Watch(Walls, FVector2D(400.0, 0.0), 0.f, Past, A.State, C);
	TestTrue(FString::Printf(TEXT("Past the end: he looks back-left behind the wall (%.1f)"), B.Looks.Num() ? B.Looks[0].Yaw : 0.f), B.Looks.Num() == 1
		&& B.Looks[0].Role == ER::Commander && B.Looks[0].Yaw < -90.f);
	const FFormationCornerResult Gone = FormationCornerRules::Watch(Walls, FVector2D(200.0 + C.CornerHoldDist + 10.0, 0.0), 0.f, Past, B.State, C);
	TestTrue(TEXT("Beyond hold_dist: no watch"), !Gone.State.Left.IsSet() && !Gone.State.Right.IsSet() && Gone.Looks.Num() == 0);
	// A corridor exit: two corners, two different men; a room corner (a joint) is not a free end.
	const TArray<FFormationWall> Exit = { Seg(-3000.0, -250.0, 100.0, -250.0), Seg(-3000.0, 250.0, 250.0, 250.0) };
	const TArray<FFormationSlot> Three = { Member(ER::Commander, 0.0, 0.0), Member(ER::Right, -150.0, 60.0), Member(ER::Left, -300.0, -60.0) };
	const FFormationCornerResult Both = FormationCornerRules::Watch(Exit, FVector2D::ZeroVector, 0.f, Three, FFormationCornerState(), C);
	TestTrue(TEXT("Corridor exit: both corners, two men"), Both.State.Left.IsSet() && Both.State.Right.IsSet() && Both.State.Left->Role != Both.State.Right->Role);
	const TArray<FFormationWall> Room = { Seg(-3000.0, -300.0, 200.0, -300.0), Seg(200.0, -300.0, 200.0, -1500.0) };
	const FFormationCornerResult Joint = FormationCornerRules::Watch(Room, FVector2D::ZeroVector, 0.f, Three, FFormationCornerState(), C);
	TestTrue(TEXT("A room corner is a joint: no watch"), !Joint.State.Left.IsSet() && !Joint.State.Right.IsSet());
	return true;
}

FORMATION_V21_TEST(FFormationClearLookTest, "Sector.ClearLookNeverIntoAWall")
bool FFormationClearLookTest::RunTest(const FString&)
{
	using namespace FormationV21TestUtil;
	const FFormationConfig C;
	const TArray<FFormationWall> Left = { Seg(-2000.0, -150.0, 2000.0, -150.0) };
	const float Half = C.LookWallAngle;
	TestTrue(TEXT("Straight at the wall: the back edge first on a tie"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, -90.f, -90.f, C), -90.f - Half, 1e-3f));
	TestTrue(TEXT("Inside the span, nearer the front edge"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, -60.f, -60.f, C), -90.f + Half, 1e-3f));
	TestEqual(TEXT("A free look stays"), FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, 0.f, 0.f, C), 0.f);
	TestEqual(TEXT("No walls"), FormationSectorRules::ClearLook({}, FVector2D::ZeroVector, -90.f, -90.f, C), -90.f);
	TestEqual(TEXT("A wall beyond look.wall_dist: free"), FormationSectorRules::ClearLook({ Seg(-2000.0, -C.LookWallDist - 50.0, 2000.0, -C.LookWallDist - 50.0) },
		FVector2D::ZeroVector, -90.f, -90.f, C), -90.f);
	// Facing back-left (-150): the back edge is reachable without sweeping across the wall direction (-90); the front edge is not.
	TestTrue(TEXT("From -150, wanting -95: the back edge"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, -95.f, -150.f, C), -90.f - Half, 1e-3f));
	TestTrue(TEXT("From -150, wanting -85: still the back edge (never across the wall)"),
		FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, -85.f, -150.f, C), -90.f - Half, 1e-3f));
	TestTrue(TEXT("From -20, wanting -85: the front edge"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Left, FVector2D::ZeroVector, -85.f, -20.f, C), -90.f + Half, 1e-3f));
	return true;
}

#undef FORMATION_V21_TEST

#endif
