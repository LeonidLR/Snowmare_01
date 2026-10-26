// CodexTactics.Formation.V23b.* - rules v2.3 parts 6-9 (contract docs/design/FORMATION_CONTRACT.md 13.6-13.9; mirrors
// Tools/TrainingGround/src/movement/rulesV23.test.ts): a man cut off brings the trail column back, only the rear guard walks backwards
// (Allocate step 5 + ClearLook's limit), ClearLook anticipates the man's motion, the guard names its cause; and the runtime helpers that
// feed them (FormationGuardRuntime::AllInSight / ShoulderLimit / AnticipatedPos). UE-only.

#include "Misc/AutomationTest.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationSectorRules.h"
#include "Formation/FormationShapeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_V23B_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.V23b." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationV23bTestUtil
{
	FFormationWall V23bSeg(double AX, double AY, double BX, double BY, float Thickness)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	/** A column of four in a 2.6 m corridor: both flanks are walls, K holds the front and Z the rear. */
	TArray<FFormationWall> V23bFlanks()
	{
		return { V23bSeg(-3000.0, -130.0, 1000.0, -130.0, 20.f), V23bSeg(-3000.0, 130.0, 1000.0, 130.0, 20.f) };
	}

	TArray<FFormationCoverMember> V23bColumnPlaces(const FFormationConfig& Config)
	{
		TArray<FFormationCoverMember> Out;
		for (const FFormationSlot& Slot : FormationShapeRules::Slots(EFormationKind::Column, 4, FVector2D::ZeroVector, 0.f, Config))
		{
			FFormationCoverMember Member;
			Member.Role = Slot.Role;
			Member.Pos = Slot.Position;
			Member.Back = static_cast<float>(-Slot.Position.X);
			Out.Add(Member);
		}
		return Out;
	}

	float V23bLookOf(const FFormationCoverResult& Result, EFormationRole Role)
	{
		const FFormationRoleYaw* Look = Result.Looks.FindByPredicate([Role](const FFormationRoleYaw& Each) { return Each.Role == Role; });
		return Look ? Look->Yaw : 1e6f;
	}
}

FORMATION_V23B_TEST(FFormationV23bCutOffTest, "GuardMemberCutOffBringsTheTrailColumn")
bool FFormationV23bCutOffTest::RunTest(const FString&)
{
	const FFormationConfig C;
	FFormationGuardState Start;
	Start.Requested = EFormationKind::Blizzard;
	Start.Effective = EFormationKind::Blizzard;
	TestEqual(TEXT("Nobody cut off: as requested"), FormationGuardRules::Resolve({}, EFormationKind::Blizzard, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, Start, 20.f, C).Level,
		EFormationGuardLevel::Requested);
	const FFormationGuardState Cut = FormationGuardRules::Resolve({}, EFormationKind::Blizzard, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, Start, 20.f, C, false);
	TestTrue(TEXT("A member cut off: the trail column at once, cause wall"), Cut.Level == EFormationGuardLevel::Trail && Cut.Effective == TOptional<EFormationKind>(EFormationKind::Column)
		&& Cut.Cause == TOptional<EFormationGuardCause>(EFormationGuardCause::Wall));
	FFormationGuardState State = FormationGuardRules::Resolve({}, EFormationKind::Blizzard, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, Cut, 0.5f * C.ClassifyHoldDistance, C);
	TestEqual(TEXT("In sight again: held for the hold distance"), State.Level, EFormationGuardLevel::Trail);
	State = FormationGuardRules::Resolve({}, EFormationKind::Blizzard, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, State, 0.5f * C.ClassifyHoldDistance + 1.f, C);
	TestTrue(TEXT("... then back to the request, no cause"), State.Level == EFormationGuardLevel::Requested && !State.Cause.IsSet());
	FFormationGuardState WallStart;
	WallStart.Requested = EFormationKind::Wall;
	WallStart.Effective = EFormationKind::Wall;
	TestEqual(TEXT("The wall layout is never guarded"), FormationGuardRules::Resolve({}, EFormationKind::Wall, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, WallStart, 20.f, C, false).Level,
		EFormationGuardLevel::Requested);
	// The guard names its cause: the turn fold (held while the turn is behind), a wall cut also during a turn.
	const FFormationGuardState Turn = FormationGuardRules::Resolve({}, EFormationKind::Line, 4, FVector2D::ZeroVector, 0.f, {}, C.TurnFoldDeg + 10.f, FFormationGuardState(), 0.f, C);
	TestTrue(TEXT("The turn fold: cause turn"), Turn.Level == EFormationGuardLevel::Trail && Turn.Cause == TOptional<EFormationGuardCause>(EFormationGuardCause::Turn));
	const FFormationGuardState Held = FormationGuardRules::Resolve({}, EFormationKind::Line, 4, FVector2D::ZeroVector, 0.f, {}, 0.f, Turn, 50.f, C);
	TestTrue(TEXT("Held fold: still turn"), Held.Level == EFormationGuardLevel::Trail && Held.Cause == TOptional<EFormationGuardCause>(EFormationGuardCause::Turn));
	TestEqual(TEXT("The HUD says (turn)"), FormationGuardRuntime::GuardHudText(EFormationKind::Line, Held.Effective.Get(EFormationKind::Line), Held.Level, 4, Held.Cause),
		FString(TEXT("Line -> Column (turn)")));
	// The runtime's AllInSight: a wall body on the straight line to the leader cuts the member off.
	const TArray<FFormationWall> Corner = { FormationV23bTestUtil::V23bSeg(100.0, -500.0, 100.0, 500.0, 20.f) };
	TestTrue(TEXT("AllInSight: everybody visible"), FormationGuardRuntime::AllInSight(Corner, FVector2D::ZeroVector, { FVector2D(-200.0, 50.0), FVector2D(50.0, 300.0) }));
	TestFalse(TEXT("AllInSight: one behind the wall"), FormationGuardRuntime::AllInSight(Corner, FVector2D::ZeroVector, { FVector2D(-200.0, 50.0), FVector2D(300.0, 0.0) }));
	TestTrue(TEXT("AllInSight: nobody else"), FormationGuardRuntime::AllInSight(Corner, FVector2D::ZeroVector, {}));
	return true;
}

FORMATION_V23B_TEST(FFormationV23bShoulderTest, "OnlyTheRearGuardWalksBackwards")
bool FFormationV23bShoulderTest::RunTest(const FString&)
{
	using namespace FormationV23bTestUtil;
	const FFormationConfig C;
	const float Lim = C.LookOverShoulderMaxDeg;
	TestEqual(TEXT("look.over_shoulder_max_deg 110"), Lim, 110.f);
	FFormationConfig Off = C;
	Off.LookOverShoulderMaxDeg = 180.f;
	int32 OldBack = 0;
	for (float Time = 0.f; Time < 6.f; Time += 0.25f)
	{
		const TOptional<FFormationCoverResult> Old = FormationCoverRules::Allocate(V23bFlanks(), FVector2D::ZeroVector, 0.f, V23bColumnPlaces(C), EFormationMode::Walk, Time, Off);
		OldBack += (FMath::Abs(V23bLookOf(*Old, EFormationRole::Right)) > 120.f ? 1 : 0) + (FMath::Abs(V23bLookOf(*Old, EFormationRole::Left)) > 120.f ? 1 : 0);
		TestEqual(TEXT("180 = off: no shoulder members"), Old->Shoulder.Num(), 0);
		const TOptional<FFormationCoverResult> Now = FormationCoverRules::Allocate(V23bFlanks(), FVector2D::ZeroVector, 0.f, V23bColumnPlaces(C), EFormationMode::Walk, Time, C);
		TestTrue(TEXT("Z the rear guard"), Now->RearGuard == TOptional<EFormationRole>(EFormationRole::Rear));
		TestTrue(TEXT("Shoulder: P and L at their travel (the heading)"), Now->Shoulder.Num() == 2
			&& Now->Shoulder.ContainsByPredicate([](const FFormationRoleYaw& Each) { return Each.Role == EFormationRole::Right && FMath::IsNearlyEqual(Each.Yaw, 0.f); })
			&& Now->Shoulder.ContainsByPredicate([](const FFormationRoleYaw& Each) { return Each.Role == EFormationRole::Left && FMath::IsNearlyEqual(Each.Yaw, 0.f); }));
		TestTrue(FString::Printf(TEXT("P and L over the shoulder at t %.2f"), Time), FMath::Abs(V23bLookOf(*Now, EFormationRole::Right)) <= Lim + 1e-3f
			&& FMath::Abs(V23bLookOf(*Now, EFormationRole::Left)) <= Lim + 1e-3f);
		TestTrue(TEXT("The rear guard keeps 6 o'clock"), FMath::Abs(V23bLookOf(*Now, EFormationRole::Rear)) >= 180.f - FormationCoverRules::RearSector - 1e-3f);
		bool bSameShares = Now->Shares.Num() == Old->Shares.Num();
		for (int32 Index = 0; bSameShares && Index < Now->Shares.Num(); ++Index)
		{
			bSameShares &= Now->Shares[Index].Role == Old->Shares[Index].Role && Now->Shares[Index].From == Old->Shares[Index].From && Now->Shares[Index].To == Old->Shares[Index].To;
		}
		TestTrue(TEXT("The shares stay as allocated"), bSameShares);
	}
	TestTrue(TEXT("Precondition: v2.2 looked > 120 deg back"), OldBack > 0);
	const TOptional<FFormationCoverResult> HaltNow = FormationCoverRules::Allocate(V23bFlanks(), FVector2D::ZeroVector, 0.f, V23bColumnPlaces(C), EFormationMode::Halt, 0.f, C);
	TestEqual(TEXT("Halt: nobody over the shoulder"), HaltNow->Shoulder.Num(), 0);
	// The limit follows his place's travel (the trail column at a bend).
	TArray<FFormationCoverMember> Bent = V23bColumnPlaces(C);
	Bent.FindByPredicate([](const FFormationCoverMember& Each) { return Each.Role == EFormationRole::Right; })->Travel = 40.f;
	for (float Time = 0.f; Time < 6.f; Time += 0.5f)
	{
		const TOptional<FFormationCoverResult> Result = FormationCoverRules::Allocate(V23bFlanks(), FVector2D::ZeroVector, 0.f, Bent, EFormationMode::Walk, Time, C);
		TestTrue(TEXT("P within 40 +- 110"), FormationMath::AngleBetween(V23bLookOf(*Result, EFormationRole::Right), 40.f) <= Lim + 1e-3f);
	}
	// ClearLook with a limit: clamped, no edge outside it.
	FFormationLookLimit Limit;
	Limit.Yaw = 0.f;
	Limit.Half = 110.f;
	const TArray<FFormationWall> Right = { V23bSeg(-2000.0, 100.0, 2000.0, 100.0, 20.f) };
	TestEqual(TEXT("No limit: 150 is free"), FormationSectorRules::ClearLook(Right, FVector2D::ZeroVector, 150.f, 150.f, C), 150.f);
	TestTrue(TEXT("Limit: the edge inside it (35)"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook(Right, FVector2D::ZeroVector, 150.f, 150.f, C, {}, Limit), 90.f - C.LookWallAngle, 1e-3f));
	TestTrue(TEXT("No walls: clamped to the nearer edge"), FMath::IsNearlyEqual(FormationSectorRules::ClearLook({}, FVector2D::ZeroVector, 150.f, 150.f, C, {}, Limit), 110.f, 1e-3f)
		&& FMath::IsNearlyEqual(FormationSectorRules::ClearLook({}, FVector2D::ZeroVector, -170.f, -170.f, C, {}, Limit), -110.f, 1e-3f));
	TestEqual(TEXT("Inside the limit: unchanged"), FormationSectorRules::ClearLook({}, FVector2D::ZeroVector, 80.f, 80.f, C, {}, Limit), 80.f);
	const TArray<FFormationWall> Behind = { V23bSeg(-170.0, -2000.0, -170.0, 2000.0, 20.f) };
	TestEqual(TEXT("A held look inside the limit"), FormationSectorRules::ClearLook(Behind, FVector2D::ZeroVector, 180.f, -150.f, C, -120.f), -120.f);
	TestTrue(TEXT("A held look outside the limit is no candidate"), FMath::Abs(FormationSectorRules::ClearLook(Behind, FVector2D::ZeroVector, 180.f, -150.f, C, -120.f, Limit)) <= 110.f + 1e-3f);
	// The runtime's ShoulderLimit: his step direction while he moves (within 90 deg of the entry), else the entry's travel.
	const TArray<FFormationRoleYaw> Shoulder = { { EFormationRole::Right, 0.f } };
	const TOptional<FFormationLookLimit> Moving = FormationGuardRuntime::ShoulderLimit(Shoulder, EFormationRole::Right, FVector2D(100.0, 100.0), C);
	TestTrue(TEXT("Moving 45 deg off: the step direction"), Moving.IsSet() && FMath::IsNearlyEqual(Moving->Yaw, 45.f, 1e-3f) && Moving->Half == Lim);
	const TOptional<FFormationLookLimit> Back = FormationGuardRuntime::ShoulderLimit(Shoulder, EFormationRole::Right, FVector2D(-100.0, 0.0), C);
	TestTrue(TEXT("Stepping back to a slot behind him: the entry's travel"), Back.IsSet() && FMath::IsNearlyEqual(Back->Yaw, 0.f, 1e-3f));
	const TOptional<FFormationLookLimit> Standing = FormationGuardRuntime::ShoulderLimit(Shoulder, EFormationRole::Right, FVector2D(2.0, 5.0), C);
	TestTrue(TEXT("Standing: the entry's travel"), Standing.IsSet() && FMath::IsNearlyEqual(Standing->Yaw, 0.f, 1e-3f));
	TestFalse(TEXT("Not listed: no limit"), FormationGuardRuntime::ShoulderLimit(Shoulder, EFormationRole::Left, FVector2D(100.0, 0.0), C).IsSet());
	TestFalse(TEXT("Rule off: no limit"), FormationGuardRuntime::ShoulderLimit(Shoulder, EFormationRole::Right, FVector2D(100.0, 0.0), Off).IsSet());
	return true;
}

FORMATION_V23B_TEST(FFormationV23bAnticipateTest, "ClearLookAnticipatesTheMotion")
bool FFormationV23bAnticipateTest::RunTest(const FString&)
{
	using namespace FormationV23bTestUtil;
	const FFormationConfig C;
	// He walks +X past the end of a wall standing to his right (end at (0, 90)); in 0.5 s he is at (-10, 0).
	const TArray<FFormationWall> Wall = { V23bSeg(0.0, 90.0, 0.0, 1000.0, 0.f) };
	const FVector2D Pos(-60.0, 0.0);
	const FVector2D Ahead(-10.0, 0.0);
	const float EndNow = FMath::RadiansToDegrees(FMath::Atan2(90.f, 60.f));
	const float EndAhead = FMath::RadiansToDegrees(FMath::Atan2(90.f, 10.f));
	const float Half = C.LookWallAngle;
	TestEqual(TEXT("Without anticipation 120 is free now"), FormationSectorRules::ClearLook(Wall, Pos, 120.f, 120.f, C), 120.f);
	const float Got = FormationSectorRules::ClearLook(Wall, Pos, 120.f, 120.f, C, {}, {}, Ahead);
	TestTrue(FString::Printf(TEXT("Anticipated: clear of the end now and ahead (%.1f)"), Got), FormationMath::AngleBetween(Got, EndAhead) >= Half - 1e-3f
		&& FormationMath::AngleBetween(Got, EndNow) >= Half - 1e-3f);
	TestTrue(TEXT("The far edge, reached without sweeping across the end"), FMath::IsNearlyEqual(Got, EndAhead + Half, 1e-3f));
	const TArray<FFormationWall> Across = { V23bSeg(-30.0, -500.0, -30.0, 500.0, 20.f) };
	TestEqual(TEXT("A step line through a wall body: no anticipation"), FormationSectorRules::ClearLook(Across, Pos, 180.f, 180.f, C, {}, {}, Ahead),
		FormationSectorRules::ClearLook(Across, Pos, 180.f, 180.f, C));
	// The runtime's AnticipatedPos: pos + velocity x look.anticipate_s; none below 1 cm/s or with the rule off.
	const TOptional<FVector2D> Anticipated = FormationGuardRuntime::AnticipatedPos(Pos, FVector2D(100.0, 0.0), C);
	TestTrue(TEXT("AnticipatedPos: 0.5 s ahead"), Anticipated.IsSet() && FVector2D::Distance(Anticipated.GetValue(), FVector2D(-10.0, 0.0)) < 1e-6);
	TestFalse(TEXT("AnticipatedPos: standing"), FormationGuardRuntime::AnticipatedPos(Pos, FVector2D(0.5, 0.0), C).IsSet());
	FFormationConfig Off = C;
	Off.LookAnticipateS = 0.f;
	TestFalse(TEXT("AnticipatedPos: rule off"), FormationGuardRuntime::AnticipatedPos(Pos, FVector2D(100.0, 0.0), Off).IsSet());
	return true;
}

#undef FORMATION_V23B_TEST

#endif
