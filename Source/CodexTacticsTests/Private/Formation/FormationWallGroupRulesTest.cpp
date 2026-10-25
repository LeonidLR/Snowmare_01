// CodexTactics.Formation.WallGroup.* - the rules v2.2 wall group (Formation/FormationWallGroupRules: the followers' looks round the
// leader's fixed corner aim, held by the cover side or the back-to-wall pose) and the runtime glue it needs (places by travel, the 4th's end
// after a slide, where the coverage allocator applies). User playtest 2026-10-25 (docs/design/FORMATION_SECTORS.md v2.2 "A look decided
// elsewhere counts as cover"), contract docs/design/FORMATION_CONTRACT.md 11.3 / 11.5 / 10.6. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationWallGroupRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_GROUP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallGroup." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallGroupTestUtil
{
	/** A wall face along +X from X = 0 to Length at Y = 0; the squad on its +Y side (outward normal yaw 90). */
	FFormationWall MakeWallGroupWall(double FromX, double ToX, double Y = 0.0)
	{
		FFormationWall Wall;
		Wall.A = FVector2D(FromX, Y);
		Wall.B = FVector2D(ToX, Y);
		Wall.Thickness = 0.f;
		return Wall;
	}

	FWallGroupMember MakeWallGroupMember(EFormationRole Role, double X, double Y, bool bCover, TOptional<float> Fixed = TOptional<float>())
	{
		FWallGroupMember Member;
		Member.Role = Role;
		Member.Pos = FVector2D(X, Y);
		Member.bCoverPose = bCover;
		Member.WallNormalYaw = 90.f;
		Member.FixedYaw = Fixed;
		return Member;
	}

	const FWallGroupLookResult* FindWallGroupLook(const TArray<FWallGroupLookResult>& Looks, EFormationRole Role)
	{
		return Looks.FindByPredicate([Role](const FWallGroupLookResult& Look) { return Look.Role == Role; });
	}

	/** Check 10 over the result: the members' positions and looks, every cone 140 deg (standing / crouched). */
	float WallGroupGap(const TArray<FFormationWall>& Walls, const TArray<FWallGroupMember>& Members, const TArray<FWallGroupLookResult>& Looks)
	{
		TArray<FVector2D> Positions;
		TArray<float> Yaws;
		TArray<float> Fovs;
		for (const FWallGroupMember& Member : Members)
		{
			Positions.Add(Member.Pos);
			const FWallGroupLookResult* Look = FindWallGroupLook(Looks, Member.Role);
			Yaws.Add(Look ? Look->Yaw : 0.f);
			Fovs.Add(140.f);
		}
		return FormationWallGroup::LargestOpenGap(Walls, Positions, Yaws, Fovs, 260.f);
	}
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupUserCornerTest, "UserCornerSwitch")
bool FFormationWallGroupUserCornerTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	// The user's case: after the cover switch the leader holds the new wall's LEFT corner, looking left along the wall (yaw 180); the two
	// followers took the slots to his right, 1 m apart, in cover.
	const TArray<FFormationWall> Walls = { MakeWallGroupWall(0.0, 600.0) };
	const TArray<FWallGroupMember> Members = {
		MakeWallGroupMember(EFormationRole::Commander, 40.0, 45.0, true, 180.f),
		MakeWallGroupMember(EFormationRole::Right, 140.0, 45.0, true),
		MakeWallGroupMember(EFormationRole::Left, 240.0, 45.0, true) };
	// The bug: everyone hugs the wall looking the same way as the leader - the open arc beyond 12 o'clock of the wall is nobody's.
	{
		TArray<FWallGroupLookResult> Bug;
		for (const FWallGroupMember& Member : Members)
		{
			Bug.Add({ Member.Role, 180.f, EWallGroupLook::AlongWall });
		}
		TestTrue(FString::Printf(TEXT("the bug: all looking the leader's way leaves a %.0f deg gap (> 60)"), WallGroupGap(Walls, Members, Bug)),
			WallGroupGap(Walls, Members, Bug) > 60.f);
	}
	const TArray<FWallGroupLookResult> Looks = FormationWallGroup::Allocate(Walls, Members);
	const FWallGroupLookResult* K = FindWallGroupLook(Looks, EFormationRole::Commander);
	if (!TestTrue(TEXT("every member has a look"), K && Looks.Num() == 3))
	{
		return false;
	}
	TestTrue(TEXT("the leader keeps his corner aim (fixed)"), K->Kind == EWallGroupLook::Fixed && FMath::IsNearlyEqual(K->Yaw, 180.f, 0.5f));
	// One follower covers the rear - along the wall the other way, in his cover pose; the other looks away from the wall (back-to-wall pose).
	const FWallGroupLookResult* Rear = Looks.FindByPredicate([](const FWallGroupLookResult& Look) { return Look.Kind == EWallGroupLook::AlongWall; });
	const FWallGroupLookResult* Out = Looks.FindByPredicate([](const FWallGroupLookResult& Look) { return Look.Kind == EWallGroupLook::FacingOut; });
	TestTrue(FString::Printf(TEXT("one follower covers the rear along the wall the other way (yaw %.0f)"), Rear ? Rear->Yaw : -1.f),
		Rear && Rear->Role != EFormationRole::Commander && FormationMath::AngleBetween(Rear->Yaw, 0.f) < 1.f);
	TestTrue(FString::Printf(TEXT("the other looks away from the wall in the back-to-wall pose (yaw %.0f)"), Out ? Out->Yaw : -1.f),
		Out && FormationMath::AngleBetween(Out->Yaw, 90.f) <= FormationWallGroup::FacingOutMaxDeg);
	TestTrue(FString::Printf(TEXT("no open gap over coverage.max_gap_deg (%.0f deg)"), WallGroupGap(Walls, Members, Looks)), WallGroupGap(Walls, Members, Looks) <= 60.f);
	for (const FWallGroupLookResult& Look : Looks)
	{
		const FWallGroupMember* Member = Members.FindByPredicate([&Look](const FWallGroupMember& Each) { return Each.Role == Look.Role; });
		TestFalse(FString::Printf(TEXT("%s does not look into the wall"), FormationNames::ToString(Look.Role)),
			Member && FormationWallGroup::FacesWall(Walls, Member->Pos, Look.Yaw));
	}
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupMirrorTest, "RightCornerMirror")
bool FFormationWallGroupMirrorTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	// The mirror: the leader at the RIGHT corner looking right (yaw 0), the others to his left.
	const TArray<FFormationWall> Walls = { MakeWallGroupWall(0.0, 600.0) };
	const TArray<FWallGroupMember> Members = {
		MakeWallGroupMember(EFormationRole::Commander, 560.0, 45.0, true, 0.f),
		MakeWallGroupMember(EFormationRole::Right, 460.0, 45.0, true),
		MakeWallGroupMember(EFormationRole::Left, 360.0, 45.0, true) };
	const TArray<FWallGroupLookResult> Looks = FormationWallGroup::Allocate(Walls, Members);
	const FWallGroupLookResult* Rear = Looks.FindByPredicate([](const FWallGroupLookResult& Look) { return Look.Kind == EWallGroupLook::AlongWall; });
	TestTrue(TEXT("a follower covers the rear along the wall (yaw 180)"), Rear && FormationMath::AngleBetween(Rear->Yaw, 180.f) < 1.f);
	TestTrue(FString::Printf(TEXT("no gap over 60 deg (%.0f)"), WallGroupGap(Walls, Members, Looks)), WallGroupGap(Walls, Members, Looks) <= 60.f);
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupCornerAimTest, "LeaderCornerAimOutward")
bool FFormationWallGroupCornerAimTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	// The leader leans out round his corner, aiming 20 deg off the wall (yaw 160): the rear man's look snaps to his cover side (along the
	// wall, 0) and the gap it leaves is taken by the middle man.
	const TArray<FFormationWall> Walls = { MakeWallGroupWall(0.0, 600.0) };
	const TArray<FWallGroupMember> Members = {
		MakeWallGroupMember(EFormationRole::Commander, 40.0, 45.0, true, 160.f),
		MakeWallGroupMember(EFormationRole::Right, 140.0, 45.0, true),
		MakeWallGroupMember(EFormationRole::Left, 240.0, 45.0, true) };
	const TArray<FWallGroupLookResult> Looks = FormationWallGroup::Allocate(Walls, Members);
	TestEqual(TEXT("three looks"), Looks.Num(), 3);
	TestTrue(FString::Printf(TEXT("no gap over 60 deg (%.0f)"), WallGroupGap(Walls, Members, Looks)), WallGroupGap(Walls, Members, Looks) <= 60.f);
	int32 Along = 0;
	for (const FWallGroupLookResult& Look : Looks)
	{
		Along += Look.Kind == EWallGroupLook::AlongWall ? 1 : 0;
		if (Look.Kind == EWallGroupLook::AlongWall)
		{
			TestTrue(TEXT("a cover look is exactly along the wall"), FormationMath::AngleBetween(Look.Yaw, 0.f) < 1.f || FormationMath::AngleBetween(Look.Yaw, 180.f) < 1.f);
		}
	}
	TestTrue(TEXT("one follower keeps his cover pose along the wall"), Along >= 1);
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupInnerCornerTest, "InnerCornerNoStare")
bool FFormationWallGroupInnerCornerTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	// An inner corner: a second wall across the line's right end (X = 300). Along the wall to the right the last man would stare into it
	// from 60 cm: he takes the back-to-wall pose instead.
	const TArray<FFormationWall> Walls = { MakeWallGroupWall(-600.0, 300.0), { FVector2D(300.0, 0.0), FVector2D(300.0, 600.0), 0.f } };
	const TArray<FWallGroupMember> Members = {
		MakeWallGroupMember(EFormationRole::Commander, 40.0, 45.0, true, 180.f),
		MakeWallGroupMember(EFormationRole::Right, 140.0, 45.0, true),
		MakeWallGroupMember(EFormationRole::Left, 240.0, 45.0, true) };
	const TArray<FWallGroupLookResult> Looks = FormationWallGroup::Allocate(Walls, Members);
	for (const FWallGroupLookResult& Look : Looks)
	{
		const FWallGroupMember* Member = Members.FindByPredicate([&Look](const FWallGroupMember& Each) { return Each.Role == Look.Role; });
		TestFalse(FString::Printf(TEXT("%s (%.0f) does not look into a wall"), FormationNames::ToString(Look.Role), Look.Yaw),
			Member && Look.Kind != EWallGroupLook::Fixed && FormationWallGroup::FacesWall(Walls, Member->Pos, Look.Yaw));
	}
	TestFalse(TEXT("nobody is sent along the wall into the inner corner"), Looks.ContainsByPredicate([](const FWallGroupLookResult& Look)
		{ return Look.Kind == EWallGroupLook::AlongWall && FormationMath::AngleBetween(Look.Yaw, 0.f) < 1.f; }));
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupFreeTest, "OutOfCoverMembersExact")
bool FFormationWallGroupFreeTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	// Members already in the back-to-wall pose (or prone behind the cover) take the allocator's look as it is.
	const TArray<FFormationWall> Walls = { MakeWallGroupWall(0.0, 600.0) };
	const TArray<FWallGroupMember> Members = {
		MakeWallGroupMember(EFormationRole::Commander, 40.0, 45.0, true, 180.f),
		MakeWallGroupMember(EFormationRole::Right, 140.0, 45.0, false),
		MakeWallGroupMember(EFormationRole::Left, 240.0, 170.0, false) };
	const TArray<FWallGroupLookResult> Looks = FormationWallGroup::Allocate(Walls, Members);
	TestEqual(TEXT("three looks"), Looks.Num(), 3);
	for (const FWallGroupLookResult& Look : Looks)
	{
		TestTrue(TEXT("fixed or free (no cover side out of cover)"), Look.Kind == EWallGroupLook::Fixed || Look.Kind == EWallGroupLook::Free);
	}
	TestTrue(FString::Printf(TEXT("no gap over 60 deg (%.0f)"), WallGroupGap(Walls, Members, Looks)), WallGroupGap(Walls, Members, Looks) <= 60.f);
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupChecksTest, "Checks")
bool FFormationWallGroupChecksTest::RunTest(const FString&)
{
	using namespace FormationWallGroupTestUtil;
	const TArray<FFormationWall> NoWalls;
	TestTrue(TEXT("one 140 deg cone in the open: a 220 deg gap"),
		FMath::IsNearlyEqual(FormationWallGroup::LargestOpenGap(NoWalls, { FVector2D::ZeroVector }, { 0.f }, { 140.f }, 260.f), 218.f, 2.1f));
	TestTrue(TEXT("three cones round the circle: no gap"), FormationWallGroup::LargestOpenGap(NoWalls, { FVector2D::ZeroVector, FVector2D::ZeroVector,
		FVector2D::ZeroVector }, { 0.f, 120.f, -120.f }, { 140.f, 140.f, 140.f }, 260.f) < 1.f);
	TestEqual(TEXT("nobody: everything open"), FormationWallGroup::LargestOpenGap(NoWalls, {}, {}, {}, 260.f), 360.f);
	// A wall right behind: the directions within 45 deg of it are not open.
	const TArray<FFormationWall> Behind = { MakeWallGroupWall(-500.0, 500.0) };
	TestTrue(TEXT("a wall behind blocks 90 deg of the gap"), FormationWallGroup::LargestOpenGap(Behind, { FVector2D(0.0, 45.0) }, { 90.f }, { 140.f }, 260.f) < 140.f);
	TestTrue(TEXT("facing the wall from 45 cm is a stare"), FormationWallGroup::FacesWall(Behind, FVector2D(0.0, 45.0), -90.f));
	TestFalse(TEXT("looking along it is not"), FormationWallGroup::FacesWall(Behind, FVector2D(0.0, 45.0), 0.f));
	TestTrue(TEXT("one cone facing out covers the open half"), FMath::IsNearlyEqual(FormationWallGroup::CoveredFraction({ 90.f }, { 140.f }, 90.f, 90.f), 141.f / 181.f, 0.02f));
	TestTrue(TEXT("two more along the wall cover all of it"), FormationWallGroup::CoveredFraction({ 90.f, 0.f, 180.f }, { 140.f, 140.f, 140.f }, 90.f, 90.f) > 0.99f);
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupReassignTest, "ReassignPlaces")
bool FFormationWallGroupReassignTest::RunTest(const FString&)
{
	using FormationRuntime::ReassignPlaces;
	// The triad after a U-turn: place 0 (Z) is now 4.2 m behind the leader where P stands, place 1 (P) is where Z stood.
	const TArray<FVector2D> Places = { FVector2D(-420.0, 0.0), FVector2D(-280.0, 260.0) };
	TestFalse(TEXT("everyone at his place: keep"), ReassignPlaces({ FVector2D(-420.0, 0.0), FVector2D(-280.0, 260.0) }, { 0, 1 }, Places).IsSet());
	const TOptional<TArray<int32>> Swap = ReassignPlaces({ FVector2D(-280.0, 260.0), FVector2D(-420.0, 0.0) }, { 0, 1 }, Places);
	TestTrue(TEXT("crossed: the man at the rear becomes Z"), Swap.IsSet() && Swap.GetValue() == TArray<int32>({ 1, 0 }));
	TestFalse(TEXT("already the cheapest: keep"), ReassignPlaces({ FVector2D(-360.0, 120.0), FVector2D(-340.0, 140.0) }, { 0, 1 }, Places).IsSet());
	TestFalse(TEXT("a bad current place: keep"), ReassignPlaces({ FVector2D::ZeroVector, FVector2D::ZeroVector }, { 0, 5 }, Places).IsSet());
	// Four: L / P / Z round a U-turn - the man nearest the rear place walks backwards.
	const TArray<FVector2D> Diamond = { FVector2D(-280.0, -260.0), FVector2D(-280.0, 260.0), FVector2D(-560.0, 0.0) };
	const TOptional<TArray<int32>> Turned = ReassignPlaces({ FVector2D(280.0, 260.0), FVector2D(280.0, -260.0), FVector2D(-560.0, 0.0) }, { 0, 1, 2 }, Diamond);
	TestTrue(TEXT("diamond turned: L and P swap, Z keeps the rear"), Turned.IsSet() && Turned.GetValue() == TArray<int32>({ 1, 0, 2 }));
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupSlideEndTest, "KeepEndAfterSlide")
bool FFormationWallGroupSlideEndTest::RunTest(const FString&)
{
	using FormationRuntime::KeepEndAfterSlide;
	float Left = 400.f;
	float Right = 250.f;
	KeepEndAfterSlide(Left, Right, true, 4, 100.f);
	TestTrue(TEXT("4: right end with room (250 >= 200) is kept"), Right >= Left);
	Left = 400.f;
	Right = 150.f;
	KeepEndAfterSlide(Left, Right, true, 4, 100.f);
	TestTrue(TEXT("4: no room on his end (150 < 200): the roomier end"), Left > Right);
	Left = 200.f;
	Right = 400.f;
	KeepEndAfterSlide(Left, Right, false, 4, 100.f);
	TestTrue(TEXT("4: left end with room is kept"), Left > Right);
	Left = 100.f;
	Right = 400.f;
	KeepEndAfterSlide(Left, Right, false, 2, 100.f);
	TestTrue(TEXT("pair: one step of room is enough"), Left > Right);
	Left = 100.f;
	Right = 400.f;
	KeepEndAfterSlide(Left, Right, false, 3, 100.f);
	TestTrue(TEXT("3: the layout's places are fixed (unchanged)"), Left == 100.f && Right == 400.f);
	TestTrue(TEXT("the slide's room reaches past the destination"), FormationRuntime::SlideRoomCm(900.f, 100.f) >= 1200.f);
	TestTrue(TEXT("never less than the layout's own room"), FormationRuntime::SlideRoomCm(0.f, 100.f) >= FormationRuntime::RoomMaxCm);
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupAllocatorUseTest, "UsesCoverAllocator")
bool FFormationWallGroupAllocatorUseTest::RunTest(const FString&)
{
	using FormationRuntime::UsesCoverAllocator;
	TestTrue(TEXT("triad walking"), UsesCoverAllocator(EFormationKind::Wedge, EFormationMode::Walk, 3));
	TestTrue(TEXT("diamond halted"), UsesCoverAllocator(EFormationKind::Diamond, EFormationMode::Halt, 4));
	TestTrue(TEXT("column walking (the tables are replaced on the move)"), UsesCoverAllocator(EFormationKind::Column, EFormationMode::Walk, 4));
	TestFalse(TEXT("the pair keeps LookYaw"), UsesCoverAllocator(EFormationKind::Diamond, EFormationMode::Walk, 2));
	TestFalse(TEXT("a run keeps LookYaw (everyone 12)"), UsesCoverAllocator(EFormationKind::Diamond, EFormationMode::Run, 4));
	TestFalse(TEXT("the wall layout keeps its rule"), UsesCoverAllocator(EFormationKind::Wall, EFormationMode::Wall, 4));
	TestFalse(TEXT("the hedgehog ring keeps its rule"), UsesCoverAllocator(EFormationKind::Hedgehog, EFormationMode::Halt, 4));
	TestTrue(TEXT("PlaceBack: 4.2 m behind"), FMath::IsNearlyEqual(FormationRuntime::PlaceBack(FVector2D(-420.0, 0.0), FVector2D::ZeroVector, 0.f), 420.f, 0.01f));
	return true;
}

FORMATION_WALL_GROUP_TEST(FFormationWallGroupRearCatchUpTest, "RearGuardCatchUpLook")
bool FFormationWallGroupRearCatchUpTest::RunTest(const FString&)
{
	using FormationRuntime::RearGuardCatchUpLook;
	// Heading 0, his rear band 180 +- 40 (fov 140 / 2 - the rear sector 30).
	TestEqual(TEXT("moving backwards straight to the rear: the look stays"), RearGuardCatchUpLook(180.f, 0.f, 40.f, 180.f), 180.f);
	const float Side = RearGuardCatchUpLook(180.f, 0.f, 40.f, 56.f);
	TestTrue(FString::Printf(TEXT("catching up forward-right (56): the look turns within the band (%.0f) so the move is not backwards"), Side),
		Side >= 140.f && Side <= 180.f && FormationMath::AngleBetween(56.f, Side) <= FormationRuntime::BackwardAngleDeg - FormationRuntime::RearCatchUpMarginDeg);
	TestTrue(TEXT("5-7 o'clock stays in his cone"), FormationMath::AngleBetween(Side, 150.f) <= 70.f && FormationMath::AngleBetween(Side, 210.f) <= 70.f);
	const float Left = RearGuardCatchUpLook(190.f, 0.f, 40.f, -60.f);
	TestTrue(FString::Printf(TEXT("forward-left: the band's left side (%.0f)"), Left), FormationMath::AngleBetween(Left, 180.f) <= 40.5f && FRotator::NormalizeAxis(Left) < 0.f);
	TestEqual(TEXT("straight forward: the band cannot face it - he walks backwards (look kept)"), RearGuardCatchUpLook(170.f, 0.f, 40.f, 0.f), 170.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
