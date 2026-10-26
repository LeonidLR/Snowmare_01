// CodexTactics.Formation.V23Runtime.* - the rules v2.3 runtime glue (Formation/FormationGuardRuntimeRules, contract
// docs/design/FORMATION_CONTRACT.md 13; reference Tools/TrainingGround/src/movement/sim.ts at df3b3da): ClearLook's held look per member
// (clearHeld: reset whenever ClearLook does not apply), the trail column's slots with the world's walls, the narrow column's decided looks
// as the allocator's fixed looks, the fallback stand-in at a floor edge (13.5; the FormationLabSmoke R_v2_gaps case) and the HUD's "(turn)"
// for the sharp-turn fold (user decision 2026-10-26). UE-only.

#include "Misc/AutomationTest.h"
#include "Characters/SquadFormation.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_V23_RUNTIME_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.V23Runtime." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationV23RuntimeTestUtil
{
	FFormationWall V23RuntimeSeg(double AX, double AY, double BX, double BY, float Thickness)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	const FFormationSlot* V23RuntimeSlotOf(const TArray<FFormationSlot>& Slots, EFormationRole Role)
	{
		return Slots.FindByPredicate([Role](const FFormationSlot& Slot) { return Slot.Role == Role; });
	}
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeClearHeldTest, "ClearHeldMemory")
bool FFormationV23RuntimeClearHeldTest::RunTest(const FString&)
{
	using namespace FormationGuardRuntime;
	TMap<int32, FFormationClearHeld> Memory;
	TestFalse(TEXT("Nothing remembered: no held look"), HeldClearLook(Memory, 1).IsSet());
	// Frame 1: ClearLook applied to 1 (-> 30) and 2 (-> -60); not to 3.
	AgeClearHeld(Memory);
	RememberClearLook(Memory, 1, TOptional<float>(30.f));
	RememberClearLook(Memory, 2, TOptional<float>(-60.f));
	RememberClearLook(Memory, 3, TOptional<float>());
	TestFalse(TEXT("The result of this frame is not this frame's held look"), HeldClearLook(Memory, 1).IsSet());
	// Frame 2: 1 is held at 30; ClearLook does not apply to 2 (the corner watcher, a run): reset at once.
	AgeClearHeld(Memory);
	TestEqual(TEXT("Frame 2: 1 holds his frame-1 result"), HeldClearLook(Memory, 1).Get(0.f), 30.f);
	TestEqual(TEXT("Frame 2: 2 holds his frame-1 result"), HeldClearLook(Memory, 2).Get(0.f), -60.f);
	TestFalse(TEXT("Frame 2: 3 never had one"), HeldClearLook(Memory, 3).IsSet());
	RememberClearLook(Memory, 1, TOptional<float>(35.f));
	RememberClearLook(Memory, 2, TOptional<float>());
	// Frame 3: 1's look is not computed at all (a fight, an own order): his memory survives only into frame 3.
	AgeClearHeld(Memory);
	TestEqual(TEXT("Frame 3: 1 holds his frame-2 result"), HeldClearLook(Memory, 1).Get(0.f), 35.f);
	TestFalse(TEXT("Frame 3: 2 was reset when ClearLook did not apply"), HeldClearLook(Memory, 2).IsSet());
	// Frame 4: 1 was not refreshed in frame 3 -> gone.
	AgeClearHeld(Memory);
	TestFalse(TEXT("Frame 4: a frame without ClearLook drops the held look"), HeldClearLook(Memory, 1).IsSet());
	TestEqual(TEXT("Frame 4: the memory is empty"), Memory.Num(), 0);
	return true;
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeClearHeldLookTest, "ClearHeldKeepsTheRearMansLook")
bool FFormationV23RuntimeClearHeldLookTest::RunTest(const FString&)
{
	using namespace FormationV23RuntimeTestUtil;
	using namespace FormationGuardRuntime;
	// The rear man, wanted 180, a wall 1.5 m behind him: frame by frame through the memory his held look stays (the v2.2 flip is gone),
	// and once ClearLook did not apply (he was the corner watcher a frame) he starts afresh at the nearest edge.
	const FFormationConfig C;
	const TArray<FFormationWall> Behind = { V23RuntimeSeg(-170.0, -2000.0, -170.0, 2000.0, 20.f) };
	TMap<int32, FFormationClearHeld> Memory;
	const auto Frame = [&](float Facing, bool bApplies)
	{
		AgeClearHeld(Memory);
		if (!bApplies)
		{
			RememberClearLook(Memory, 7, TOptional<float>());
			return 180.f;
		}
		const float Got = FormationSectorRules::ClearLook(Behind, FVector2D::ZeroVector, 180.f, Facing, C, HeldClearLook(Memory, 7));
		RememberClearLook(Memory, 7, TOptional<float>(Got));
		return Got;
	};
	// He comes in holding -120 (55 deg off the wall: a free look he had).
	AgeClearHeld(Memory);
	RememberClearLook(Memory, 7, TOptional<float>(-120.f));
	TestEqual(TEXT("Held -120 is kept"), Frame(-150.f, true), -120.f);
	TestEqual(TEXT("... and the next frame too"), Frame(-125.f, true), -120.f);
	Frame(-120.f, false);
	const float Afresh = Frame(-120.f, true);
	TestTrue(FString::Printf(TEXT("After a frame without ClearLook: the nearest edge (%.1f)"), Afresh), FMath::IsNearlyEqual(FMath::Abs(Afresh), 180.f - C.LookWallAngle, 1e-3f));
	return true;
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeTrailWallsTest, "TrailFrameSlotsWithWalls")
bool FFormationV23RuntimeTrailWallsTest::RunTest(const FString&)
{
	using namespace FormationV23RuntimeTestUtil;
	const FFormationConfig C;
	const TArray<FVector2D> Trail = FormationGuardRuntime::TrailWithLeader({ FVector2D(-3000.0, 0.0), FVector2D(-1500.0, 0.0) }, FVector2D::ZeroVector);
	const TArray<FFormationWall> Corridor = { V23RuntimeSeg(-4000.0, -100.0, 1000.0, -100.0, 20.f), V23RuntimeSeg(-4000.0, 100.0, 1000.0, 100.0, 20.f) };
	const TOptional<float> Passage(200.f);
	const TArray<FFormationSlot> Free = FormationGuardRuntime::TrailFrameSlots(Trail, 0.f, 4, {}, EFormationMode::Walk, 2.f, Passage, C);
	const TArray<FFormationSlot> Tight = FormationGuardRuntime::TrailFrameSlots(Trail, 0.f, 4, Corridor, EFormationMode::Walk, 2.f, Passage, C);
	TestEqual(TEXT("4 slots"), Tight.Num(), 4);
	// No walls = the v2.1 column (TrailSlots without walls); the walls go into TrailSlots (the stagger keeps column.stagger_clear).
	const TArray<FFormationSlot> V21 = FormationShapeRules::TrailSlots(Trail, 0.f, 4, C);
	const TArray<FFormationSlot> V23 = FormationShapeRules::TrailSlots(Trail, 0.f, 4, C, Corridor);
	for (int32 Index = 0; Index < Tight.Num() && Index < V23.Num() && Index < Free.Num() && Index < V21.Num(); ++Index)
	{
		const FString Role = FormationNames::ToString(Tight[Index].Role);
		TestTrue(FString::Printf(TEXT("%s: no walls -> the v2.1 slot"), *Role), FVector2D::Distance(Free[Index].Position, V21[Index].Position) < 1e-6);
		TestTrue(FString::Printf(TEXT("%s: the walls reach TrailSlots"), *Role), FVector2D::Distance(Tight[Index].Position, V23[Index].Position) < 1e-6);
		for (const FFormationWall& Wall : Corridor)
		{
			TestTrue(FString::Printf(TEXT("%s keeps column.stagger_clear off the corridor walls"), *Role),
				FormationMath::DistToWall(Tight[Index].Position, Wall) >= C.ColumnStaggerClear - 1e-6);
		}
	}
	const FFormationSlot* PFree = V23RuntimeSlotOf(Free, EFormationRole::Right);
	const FFormationSlot* PTight = V23RuntimeSlotOf(Tight, EFormationRole::Right);
	TestTrue(TEXT("P's stagger: 60 in the open, 45 in the 2 m corridor"), PFree && PTight && FMath::IsNearlyEqual(FMath::Abs(PFree->Position.Y), 60.0, 1e-6)
		&& FMath::IsNearlyEqual(FMath::Abs(PTight->Position.Y), 45.0, 1e-6));
	// Looks: LookYaw of the column by the held corridor width; Z keeps the squad's 6 o'clock (the heading as his reference).
	const FFormationSlot* Z = V23RuntimeSlotOf(Tight, EFormationRole::Rear);
	TestTrue(TEXT("Z's look = LookYaw(Z, column) on the heading"), Z && FMath::IsNearlyEqual(Z->Yaw,
		FormationSectorRules::LookYaw(EFormationRole::Rear, EFormationKind::Column, EFormationMode::Walk, 4, 2.f, 0.f, Passage, C), 1e-3f));
	return true;
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeDecidedTest, "DecidedLooksAreFixed")
bool FFormationV23RuntimeDecidedTest::RunTest(const FString&)
{
	using namespace FormationV23RuntimeTestUtil;
	using FormationGuardRuntime::DecidedFixedLooks;
	const FFormationConfig C;
	TArray<FFormationSlot> Slots = FormationShapeRules::Slots(EFormationKind::Column, 4, FVector2D::ZeroVector, 0.f, C);
	const TOptional<float> Narrow(200.f);
	for (FFormationSlot& Slot : Slots)
	{
		Slot.Yaw = FormationSectorRules::LookYaw(Slot.Role, EFormationKind::Column, EFormationMode::Walk, 4, 1.f, 0.f, Narrow, C);
	}
	const auto Roles = [](const TArray<FFormationRoleYaw>& Looks)
	{
		FString Out;
		for (const FFormationRoleYaw& Look : Looks)
		{
			Out += FormationNames::ToString(Look.Role);
		}
		return Out;
	};
	const TArray<FFormationRoleYaw> Looks = DecidedFixedLooks(Slots, EFormationKind::Column, false, Narrow, {}, C);
	TestEqual(TEXT("Narrow column: K, P, L fixed (1A); Z is the allocator's rear guard"), Looks.Num(), 3);
	TestFalse(TEXT("... Z not among them"), Looks.ContainsByPredicate([](const FFormationRoleYaw& Look) { return Look.Role == EFormationRole::Rear; }));
	for (const FFormationRoleYaw& Look : Looks)
	{
		const FFormationSlot* Slot = V23RuntimeSlotOf(Slots, Look.Role);
		TestTrue(FString::Printf(TEXT("%s: his slot look (LookYaw)"), FormationNames::ToString(Look.Role)), Slot && Slot->Yaw == Look.Yaw);
	}
	const FFormationSlot* P = V23RuntimeSlotOf(Slots, EFormationRole::Right);
	const TArray<FFormationRoleYaw> AfterWatcher = DecidedFixedLooks(Slots, EFormationKind::Column, false, Narrow, { { EFormationRole::Right, 90.f } }, C);
	TestFalse(TEXT("A role already fixed (the corner watcher P) is not added twice"),
		AfterWatcher.ContainsByPredicate([](const FFormationRoleYaw& Look) { return Look.Role == EFormationRole::Right; }));
	TestEqual(TEXT("... the others still are"), AfterWatcher.Num(), 2);
	TestEqual(TEXT("A wide column: nothing decided"), DecidedFixedLooks(Slots, EFormationKind::Column, false, TOptional<float>(900.f), {}, C).Num(), 0);
	TestEqual(TEXT("No corridor width: nothing decided"), DecidedFixedLooks(Slots, EFormationKind::Column, false, TOptional<float>(), {}, C).Num(), 0);
	TestEqual(TEXT("The trail column in a narrow corridor counts as the column (asked echelon right)"),
		Roles(DecidedFixedLooks(Slots, EFormationKind::EchelonRight, true, Narrow, {}, C)), Roles(Looks));
	TestEqual(TEXT("An echelon (no trail column): nothing decided"), DecidedFixedLooks(Slots, EFormationKind::EchelonRight, false, Narrow, {}, C).Num(), 0);
	TestEqual(TEXT("The pair: nothing decided"), DecidedFixedLooks({ Slots[0], Slots[1] }, EFormationKind::Column, false, Narrow, {}, C).Num(), 0);
	// The allocator keeps them: P looks forward over the leader's shoulder (user decision 1A).
	TArray<FFormationCoverMember> Members;
	for (const FFormationSlot& Slot : Slots)
	{
		FFormationCoverMember Member;
		Member.Role = Slot.Role;
		Member.Pos = Slot.Position;
		Member.Back = static_cast<float>(-Slot.Position.X);
		Members.Add(Member);
	}
	const TArray<FFormationWall> Corridor = { V23RuntimeSeg(-3000.0, -100.0, 1000.0, -100.0, 20.f), V23RuntimeSeg(-3000.0, 100.0, 1000.0, 100.0, 20.f) };
	const TOptional<FFormationCoverResult> Result = FormationCoverRules::Allocate(Corridor, FVector2D::ZeroVector, 0.f, Members, EFormationMode::Walk, 1.f, C, Looks);
	const FFormationRoleYaw* PLook = Result.IsSet() ? Result->Looks.FindByPredicate([](const FFormationRoleYaw& Look) { return Look.Role == EFormationRole::Right; }) : nullptr;
	TestTrue(TEXT("The allocator keeps P's decided look"), P && PLook && FMath::IsNearlyEqual(PLook->Yaw, P->Yaw, 1e-3f));
	TestTrue(TEXT("Z is the rear guard"), Result.IsSet() && Result->RearGuard == TOptional<EFormationRole>(EFormationRole::Rear));
	return true;
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeFloorEdgeTest, "FallbackStandInAtTheFloorEdge")
bool FFormationV23RuntimeFloorEdgeTest::RunTest(const FString&)
{
	using namespace FormationV23RuntimeTestUtil;
	// FormationLabSmoke R_v2_gaps (2026-10-26): the echelon right walks 3.6 m from the lab floor's edge - Z's slot (5.3 m out on the right
	// diagonal) lies past it. Before v2.3 his stand-in was the nearest candidate (his bearing at 60 %) - 50 cm from P's slot: Z walked on P.
	// Contract 13.5 skips it (< slot.min_spacing) and gives him the next one, on the leader's back diagonal - behind his raw slot.
	const FFormationConfig C;
	const FVector2D Leader(0.0, 0.0);
	const double FloorEdgeY = 363.0; // the navmesh ends 3.63 m to the right (+Y) of the route
	const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(EFormationKind::EchelonRight, 4, Leader, 0.f, C);
	const FFormationSlot* Z = V23RuntimeSlotOf(Slots, EFormationRole::Rear);
	if (!TestNotNull(TEXT("Z's slot"), Z))
	{
		return false;
	}
	TestTrue(TEXT("Z's slot is past the floor edge"), Z->Position.Y > FloorEdgeY);
	TArray<FVector2D> Given = { Leader };
	for (const FFormationSlot& Slot : Slots)
	{
		if (Slot.Role != EFormationRole::Commander && Slot.Role != EFormationRole::Rear)
		{
			TestTrue(FString::Printf(TEXT("%s's slot is on the floor"), FormationNames::ToString(Slot.Role)), Slot.Position.Y < FloorEdgeY);
			Given.Add(Slot.Position);
		}
	}
	TArray<FVector> Usable;
	for (const FVector& Candidate : SquadFormation::ComputeFallbackSlots(FVector(Leader, 0.0), FVector(Z->Position, 0.0), 150.f))
	{
		if (Candidate.Y < FloorEdgeY)
		{
			Usable.Add(Candidate);
		}
	}
	const auto Nearest = [&Given](const FVector& At)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector2D& Each : Given)
		{
			Best = FMath::Min(Best, FVector2D::Distance(Each, FVector2D(At)));
		}
		return Best;
	};
	TestTrue(TEXT("The usable stand-ins"), Usable.Num() > 1);
	// The port's first reading of "this frame" (only the slots resolved BEFORE him): Z resolved before P takes the stand-in on P's spot - the
	// full FormationLabSmoke run, where Z then walked ~3-4 m forward facing back. Hence every walkable slot of the frame is given out first.
	const TArray<FVector2D> LeaderOnly = { Leader };
	TestEqual(TEXT("Only the leader given: the stand-in on P's spot"), SquadFormation::ChooseFallbackSlot(Usable, LeaderOnly, C.SlotMinSpacing), 0);
	TestTrue(FString::Printf(TEXT("v2.2's pick (the nearest) shares P's spot (%.0f cm)"), Nearest(Usable[0])), Nearest(Usable[0]) < C.SlotMinSpacing);
	const int32 Chosen = SquadFormation::ChooseFallbackSlot(Usable, Given, C.SlotMinSpacing);
	TestTrue(TEXT("v2.3 gives Z another stand-in"), Chosen > 0);
	if (Chosen > 0)
	{
		TestTrue(TEXT("... slot.min_spacing off every slot given out"), Nearest(Usable[Chosen]) >= C.SlotMinSpacing);
		const double BehindRaw = Z->Position.X - Usable[Chosen].X;
		TestTrue(FString::Printf(TEXT("... %.0f cm behind his raw slot along the heading: the pace check measures his GIVEN slot (checks.ts 6)"), BehindRaw),
			BehindRaw > 150.0);
	}
	return true;
}

FORMATION_V23_RUNTIME_TEST(FFormationV23RuntimeTurnHudTest, "GuardHudTurnFold")
bool FFormationV23RuntimeTurnHudTest::RunTest(const FString&)
{
	using EK = EFormationKind;
	using EL = EFormationGuardLevel;
	using EC = EFormationGuardCause;
	using FormationGuardRuntime::GuardHudText;
	// User decision 2026-10-26: the sharp-turn fold says "(turn)", a wall cut "(wall)" - rules v2.3 part 9: from the guard state's Cause.
	TestEqual(TEXT("The fold"), GuardHudText(EK::EchelonRight, EK::Column, EL::Trail, 4, TOptional<EC>(EC::Turn)), FString(TEXT("Echelon right -> Column (turn)")));
	TestEqual(TEXT("A wall cut"), GuardHudText(EK::EchelonRight, EK::Column, EL::Trail, 4, TOptional<EC>(EC::Wall)), FString(TEXT("Echelon right -> Column (wall)")));
	TestEqual(TEXT("No cause: the wall"), GuardHudText(EK::EchelonRight, EK::Column, EL::Trail, 4), FString(TEXT("Echelon right -> Column (wall)")));
	TestEqual(TEXT("The mirror is the wall"), GuardHudText(EK::EchelonLeft, EK::EchelonRight, EL::Mirror, 4, TOptional<EC>(EC::Wall)),
		FString(TEXT("Echelon left -> Echelon right (wall)")));
	TestEqual(TEXT("Level 0: the name"), GuardHudText(EK::Line, EK::Line, EL::Requested, 4, TOptional<EC>(EC::Turn)), FString(TEXT("Line")));
	// The guard's cause itself: a turn alone -> Turn; a wall cut at the same time wins (Wall).
	const FFormationConfig C;
	const FFormationGuardState Turn = FormationGuardRules::Resolve({}, EK::Line, 4, FVector2D::ZeroVector, 0.f, {}, C.TurnFoldDeg + 5.f, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("A sharp turn alone: the cause is the turn"), Turn.Level == EL::Trail && Turn.Cause == TOptional<EC>(EC::Turn));
	FFormationWall Wall;
	Wall.A = FVector2D(-3000.0, 300.0);
	Wall.B = FVector2D(3000.0, 300.0);
	Wall.Thickness = 40.f;
	const FFormationGuardState Both = FormationGuardRules::Resolve({ Wall }, EK::Line, 4, FVector2D::ZeroVector, 0.f, {}, C.TurnFoldDeg + 5.f, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("A turn and a wall cut: the wall"), Both.Level == EL::Trail && Both.Cause == TOptional<EC>(EC::Wall));
	return true;
}

#undef FORMATION_V23_RUNTIME_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
