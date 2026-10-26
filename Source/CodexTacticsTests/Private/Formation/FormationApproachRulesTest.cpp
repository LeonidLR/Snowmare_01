// CodexTactics.Formation.Approach.* - v2.5 the covered approach to the wall (FormationWallRules::Approach; new user rule 2026-10-26: on the
// order to the wall everyone dropped his sector and dashed in, the rear wide open; docs/design/FORMATION_CONTRACT.md 15; mirrors
// Tools/TrainingGround/src/movement/rulesV25.test.ts). UE-only (no Godot reference).
// The wall face runs along Y at x 0 (40 cm), the squad comes from +X: outward normal yaw 0.

#include "Misc/AutomationTest.h"
#include "Formation/FormationWallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_APPROACH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Approach." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationApproachTestUtil
{
	using AR = EFormationRole;

	FFormationWall Face()
	{
		FFormationWall Wall;
		Wall.A = FVector2D(0.0, -1500.0);
		Wall.B = FVector2D(0.0, 1500.0);
		Wall.Thickness = 40.f;
		return Wall;
	}

	TArray<FFormationSlot> Layout(int32 Count)
	{
		return FormationWallRules::Layout(Face(), FVector2D(65.0, 0.0), Count, 500.f, 500.f, FFormationConfig());
	}

	/** Everyone at his slot except the given positions. */
	TArray<FFormationSlot> At(const TArray<FFormationSlot>& Slots, const TMap<AR, FVector2D>& Where)
	{
		TArray<FFormationSlot> Members;
		for (const FFormationSlot& Slot : Slots)
		{
			FFormationSlot Member;
			Member.Role = Slot.Role;
			const FVector2D* Pos = Where.Find(Slot.Role);
			Member.Position = Pos ? *Pos : Slot.Position;
			Members.Add(Member);
		}
		return Members;
	}

	const FFormationApproachLook& ApproachLookOf(const FFormationApproachResult& Result, AR Role)
	{
		static const FFormationApproachLook Missing;
		const FFormationApproachLook* Look = Result.Looks.FindByPredicate([Role](const FFormationApproachLook& Each) { return Each.Role == Role; });
		return Look ? *Look : Missing;
	}
}

FORMATION_APPROACH_TEST(FFormationApproachThreeTest, "ThreeMen")
bool FFormationApproachThreeTest::RunTest(const FString&)
{
	using namespace FormationApproachTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationWall> Walls = { Face() };
	const TArray<FFormationSlot> Slots = Layout(3);
	const FFormationApproachResult Start = FormationWallRules::Approach(Walls, Slots, 0.f,
		At(Slots, { { AR::Commander, FVector2D(400.0, 0.0) }, { AR::Right, FVector2D(680.0, 260.0) }, { AR::Rear, FVector2D(960.0, 0.0) } }), {}, Config);
	TestTrue(TEXT("the leader runs in"), Start.State.Runner == AR::Commander && ApproachLookOf(Start, AR::Commander).Duty == EFormationApproachDuty::Run
		&& !ApproachLookOf(Start, AR::Commander).Yaw.IsSet());
	TestTrue(TEXT("the man farthest back covers the rear (away from the wall)"), Start.State.Rear == AR::Rear && ApproachLookOf(Start, AR::Rear).Duty == EFormationApproachDuty::Rear
		&& FMath::IsNearlyEqual(ApproachLookOf(Start, AR::Rear).Yaw.Get(999.f), 0.f, 0.01f));
	TestTrue(TEXT("the other looks to his flank (+90)"), ApproachLookOf(Start, AR::Right).Duty == EFormationApproachDuty::Flank
		&& FMath::IsNearlyEqual(ApproachLookOf(Start, AR::Right).Yaw.Get(999.f), 90.f, 0.01f));
	TestEqual(TEXT("the leader will look along the wall the other way"), Start.State.AlongSide, -1);
	const FFormationApproachResult RunnerIn = FormationWallRules::Approach(Walls, Slots, 0.f,
		At(Slots, { { AR::Right, FVector2D(520.0, 200.0) }, { AR::Rear, FVector2D(800.0, 0.0) } }), Start.State, Config);
	TestTrue(TEXT("the first at the wall looks along it (-90)"), ApproachLookOf(RunnerIn, AR::Commander).Duty == EFormationApproachDuty::Along
		&& FMath::IsNearlyEqual(ApproachLookOf(RunnerIn, AR::Commander).Yaw.Get(999.f), -90.f, 0.01f));
	const FFormationApproachResult FlankIn = FormationWallRules::Approach(Walls, Slots, 0.f, At(Slots, { { AR::Rear, FVector2D(600.0, 0.0) } }), RunnerIn.State, Config);
	TestTrue(TEXT("the flank man at his slot: his layout look"), ApproachLookOf(FlankIn, AR::Right).Duty == EFormationApproachDuty::Layout
		&& FMath::IsNearlyEqual(ApproachLookOf(FlankIn, AR::Right).Yaw.Get(999.f), Slots[1].Yaw, 0.01f));
	TestTrue(TEXT("the rear holds until everyone is in"), ApproachLookOf(FlankIn, AR::Rear).Duty == EFormationApproachDuty::Rear);
	const FFormationApproachResult Done = FormationWallRules::Approach(Walls, Slots, 0.f, At(Slots, {}), FlankIn.State, Config);
	TestTrue(TEXT("everyone in: done"), Done.State.bDone);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		TestTrue(TEXT("the plain wall layout"), Done.Looks[Index].Duty == EFormationApproachDuty::Layout && FMath::IsNearlyEqual(Done.Looks[Index].Yaw.Get(999.f), Slots[Index].Yaw, 0.01f));
	}
	const FFormationApproachResult After = FormationWallRules::Approach(Walls, Slots, 0.f, At(Slots, { { AR::Rear, FVector2D(600.0, 0.0) } }), Done.State, Config);
	TestTrue(TEXT("done is sticky"), After.State.bDone && ApproachLookOf(After, AR::Rear).Duty == EFormationApproachDuty::Layout);
	// The roles are held: the positions swapping on the way reshuffle nobody.
	const FFormationApproachResult Swapped = FormationWallRules::Approach(Walls, Slots, 0.f,
		At(Slots, { { AR::Commander, FVector2D(300.0, 0.0) }, { AR::Right, FVector2D(990.0, 260.0) }, { AR::Rear, FVector2D(500.0, 0.0) } }), Start.State, Config);
	TestTrue(TEXT("roles held"), Swapped.State.Rear == AR::Rear && ApproachLookOf(Swapped, AR::Rear).Duty == EFormationApproachDuty::Rear);
	return true;
}

FORMATION_APPROACH_TEST(FFormationApproachPairAndFourTest, "PairAndFour")
bool FFormationApproachPairAndFourTest::RunTest(const FString&)
{
	using namespace FormationApproachTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationWall> Walls = { Face() };
	const TArray<FFormationSlot> Pair = Layout(2);
	const FFormationApproachResult Two = FormationWallRules::Approach(Walls, Pair, 0.f,
		At(Pair, { { AR::Commander, FVector2D(400.0, 0.0) }, { AR::Rear, FVector2D(680.0, 260.0) } }), {}, Config);
	TestTrue(TEXT("the pair: the partner covers the rear"), Two.State.Rear == AR::Rear && Two.State.Flanks.Num() == 0
		&& FMath::IsNearlyEqual(ApproachLookOf(Two, AR::Rear).Yaw.Get(999.f), 0.f, 0.01f));
	TestEqual(TEXT("the pair: the leader will look along the wall away from the partner's side"), Two.State.AlongSide, -1);
	const TArray<FFormationSlot> Four = Layout(4);
	const FFormationApproachResult Res = FormationWallRules::Approach(Walls, Four, 0.f, At(Four, { { AR::Commander, FVector2D(400.0, 0.0) },
		{ AR::Right, FVector2D(680.0, 260.0) }, { AR::Left, FVector2D(680.0, -260.0) }, { AR::Rear, FVector2D(960.0, 0.0) } }), {}, Config);
	TestTrue(TEXT("four: the rear"), Res.State.Rear == AR::Rear);
	TestTrue(TEXT("four: both flanks, one each side"), FMath::IsNearlyEqual(ApproachLookOf(Res, AR::Right).Yaw.Get(999.f), 90.f, 0.01f)
		&& FMath::IsNearlyEqual(ApproachLookOf(Res, AR::Left).Yaw.Get(999.f), -90.f, 0.01f));
	return true;
}

FORMATION_APPROACH_TEST(FFormationApproachNeverSplitTest, "NeverSplitFirst")
bool FFormationApproachNeverSplitTest::RunTest(const FString&)
{
	using namespace FormationApproachTestUtil;
	const FFormationConfig Config;
	FFormationWall Side;
	Side.A = FVector2D(0.0, -250.0);
	Side.B = FVector2D(1500.0, -250.0);
	Side.Thickness = 40.f;
	const TArray<FFormationWall> Walls = { Face(), Side };
	const TArray<FFormationSlot> Slots = Layout(4);
	const FFormationApproachResult Res = FormationWallRules::Approach(Walls, Slots, 0.f, At(Slots, { { AR::Commander, FVector2D(400.0, 0.0) },
		{ AR::Right, FVector2D(680.0, 260.0) }, { AR::Left, FVector2D(680.0, -400.0) }, { AR::Rear, FVector2D(960.0, 0.0) } }), {}, Config);
	TestTrue(TEXT("a man beyond the side wall runs round it (never split first)"), ApproachLookOf(Res, AR::Left).Duty == EFormationApproachDuty::Run
		&& !ApproachLookOf(Res, AR::Left).Yaw.IsSet());
	const FFormationApproachResult Back = FormationWallRules::Approach(Walls, Slots, 0.f, At(Slots, { { AR::Right, FVector2D(520.0, 200.0) },
		{ AR::Left, FVector2D(500.0, -150.0) }, { AR::Rear, FVector2D(800.0, 0.0) } }), Res.State, Config);
	TestTrue(TEXT("round the wall: his flank look again"), ApproachLookOf(Back, AR::Left).Duty == EFormationApproachDuty::Flank);
	return true;
}

#undef FORMATION_APPROACH_TEST

#endif
