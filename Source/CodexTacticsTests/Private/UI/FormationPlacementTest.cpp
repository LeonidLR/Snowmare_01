// CodexTactics.UI.FormationPlacement.* - the custom formation's placement mode state (FFormationPlacement; game designer decisions 2026-11-01,
// docs/design/FORMATION_SECTORS.md "Custom formation", contract 17.6). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "UI/FormationPlacementRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FormationPlacementTestLocal
{
	TArray<FFormationCustomGhost> Squad()
	{
		TArray<FFormationCustomGhost> Ghosts;
		const float Rows[3][3] = { { 0.f, 0.f, 0.f }, { -280.f, 260.f, 0.f }, { -280.f, -260.f, 0.f } };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FFormationCustomGhost& Ghost = Ghosts.AddDefaulted_GetRef();
			Ghost.Squad = Index;
			Ghost.Position = FVector2D(Rows[Index][0], Rows[Index][1]);
			Ghost.Yaw = Rows[Index][2];
		}
		return Ghosts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationPlacementFlowTest, "CodexTactics.UI.FormationPlacement.ClickDragConfirm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationPlacementFlowTest::RunTest(const FString&)
{
	FFormationPlacement Placement;
	Placement.Begin(2, 0, FormationPlacementTestLocal::Squad());
	TestTrue(TEXT("active for slot 2"), Placement.IsActive() && Placement.GetSlot() == 2);
	TestTrue(TEXT("a ground press with nobody selected asks for an operative"), Placement.Press(FVector2D(500.0, 0.0)) == EFormationPlacementResult::NoOperative);
	TestTrue(TEXT("a click near a ghost finds him"), Placement.GhostAt(FVector2D(-250.0, 250.0), 60.0) == TOptional<int32>(1));
	Placement.Select(1);
	TestTrue(TEXT("press = the spot"), Placement.Press(FVector2D(100.0, 400.0)) == EFormationPlacementResult::Pressed);
	TestEqual(TEXT("the drag preview looks along the drag"), Placement.PreviewLook(FVector2D(100.0, 600.0)).Get(0.f), 90.f, 0.01f);
	TestTrue(TEXT("release = placed"), Placement.Release(FVector2D(100.0, 600.0)) == EFormationPlacementResult::Placed);
	const FFormationCustomGhost* Moved = Placement.FindGhost(1);
	TestTrue(TEXT("his ghost moved and looks along the drag"), Moved && Moved->Position.Equals(FVector2D(100.0, 400.0)) && FMath::IsNearlyEqual(Moved->Yaw, 90.f));
	// A plain click: the squad's heading (the anchor ghost's look).
	Placement.Select(2);
	Placement.Press(FVector2D(-400.0, -300.0));
	Placement.Release(FVector2D(-390.0, -300.0));
	TestEqual(TEXT("a plain click looks along the heading"), Placement.FindGhost(2)->Yaw, 0.f, 0.01f);
	// Refusals keep the ghost where it was.
	Placement.Select(2);
	Placement.Press(FVector2D(40.0, 0.0));
	TestTrue(TEXT("too close to the leader's ghost"), Placement.Release(FVector2D(40.0, 0.0)) == EFormationPlacementResult::TooClose);
	TestTrue(TEXT("refused: the ghost stays"), Placement.FindGhost(2)->Position.Equals(FVector2D(-400.0, -300.0)));
	// The anchor moves the frame: everyone else keeps his world spot, his offset changes.
	Placement.Select(0);
	Placement.Press(FVector2D(100.0, 0.0));
	TestTrue(TEXT("the anchor moved"), Placement.Release(FVector2D(300.0, 0.0)) == EFormationPlacementResult::Placed);
	const FFormationCustomShape Shape = Placement.Shape();
	TestTrue(TEXT("three members, the anchor first"), Shape.Members.Num() == 3 && Shape.Members[0].Squad == 0);
	TestEqual(TEXT("member 1 now 300 cm forward of the anchor"), Shape.Members[1].Fwd, 0.f, 0.05f);
	TestEqual(TEXT("member 1 400 right"), Shape.Members[1].Right, 400.f, 0.05f);
	TestEqual(TEXT("member 1 looks right"), Shape.Members[1].Look, 90.f, 0.05f);
	Placement.End();
	TestFalse(TEXT("ended"), Placement.IsActive());
	TestTrue(TEXT("no ghosts after the end"), Placement.GetGhosts().IsEmpty());
	return true;
}

// Bug_05 (the game designer's PIE 2026-11-01): select operative 1, place him; select operative 2, place him; ... - every placed ghost stays
// where it was put while the next one is placed (the log showed "#2 placed" twice: the pick had not moved on).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationPlacementOneByOneTest, "CodexTactics.UI.FormationPlacement.OneAfterAnother",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationPlacementOneByOneTest::RunTest(const FString&)
{
	FFormationPlacement Placement;
	Placement.Begin(1, 0, FormationPlacementTestLocal::Squad());
	const FVector2D Spots[3] = { FVector2D(150.0, 0.0), FVector2D(-100.0, 300.0), FVector2D(-100.0, -300.0) };
	const float Looks[3] = { 0.f, 90.f, -90.f };
	for (int32 Squad = 0; Squad < 3; ++Squad)
	{
		TestTrue(FString::Printf(TEXT("#%d selected"), Squad), Placement.Select(Squad) == EFormationPlacementResult::Selected && Placement.GetSelected() == TOptional<int32>(Squad));
		Placement.Press(Spots[Squad]);
		TestTrue(FString::Printf(TEXT("#%d placed"), Squad), Placement.Release(Spots[Squad] + FVector2D(FMath::Cos(FMath::DegreesToRadians(Looks[Squad])),
			FMath::Sin(FMath::DegreesToRadians(Looks[Squad]))) * 200.0) == EFormationPlacementResult::Placed);
		for (int32 Earlier = 0; Earlier <= Squad; ++Earlier)
		{
			const FFormationCustomGhost* Ghost = Placement.FindGhost(Earlier);
			TestTrue(FString::Printf(TEXT("after #%d: #%d's ghost stays at its spot, looking its way"), Squad, Earlier), Ghost && Ghost->Position.Equals(Spots[Earlier], 0.01)
				&& FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Ghost->Yaw, Looks[Earlier]), 0.f, 0.01f));
		}
	}
	TestTrue(TEXT("the selection stays on the last one placed"), Placement.GetSelected() == TOptional<int32>(2));
	// Re-picking an earlier operative and placing him again moves only him.
	Placement.Select(1);
	Placement.Press(FVector2D(-200.0, 300.0));
	Placement.Release(FVector2D(-200.0, 300.0));
	TestTrue(TEXT("#1 moved again, #0 and #2 kept"), Placement.FindGhost(1)->Position.Equals(FVector2D(-200.0, 300.0)) && Placement.FindGhost(0)->Position.Equals(Spots[0])
		&& Placement.FindGhost(2)->Position.Equals(Spots[2]));
	return true;
}

// Bug_05: the ghosts stood in the T-pose (the mesh's reference pose) - the pose check the ghost actor runs after setting its idle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationPlacementPoseTest, "CodexTactics.UI.FormationPlacement.GhostPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationPlacementPoseTest::RunTest(const FString&)
{
	TArray<FTransform> Ref;
	Ref.Add(FTransform::Identity);
	Ref.Add(FTransform(FRotator(0.0, 0.0, 90.0), FVector(0.0, 20.0, 140.0))); // an arm straight out (the T)
	Ref.Add(FTransform(FRotator(0.0, 90.0, 0.0), FVector(0.0, 30.0, 0.0)));
	TestEqual(TEXT("the reference pose itself is 0 deg off"), FormationPlacementRules::PoseOffRefDeg(Ref, Ref), 0.f, 0.01f);
	TestFalse(TEXT("the reference pose is not posed (the T-pose)"), FormationPlacementRules::IsPosed(FormationPlacementRules::PoseOffRefDeg(Ref, Ref)));
	TArray<FTransform> Idle = Ref;
	Idle[1].SetRotation(FQuat(FRotator(0.0, 0.0, 15.0))); // the arm down 75 deg
	TestEqual(TEXT("the arm down: 75 deg off"), FormationPlacementRules::PoseOffRefDeg(Idle, Ref), 75.f, 0.05f);
	TestTrue(TEXT("an idle pose is posed"), FormationPlacementRules::IsPosed(FormationPlacementRules::PoseOffRefDeg(Idle, Ref)));
	TArray<FTransform> Tiny = Ref;
	Tiny[2].SetRotation(FQuat(FRotator(0.0, 94.0, 0.0)));
	TestFalse(TEXT("a 4 deg wobble is still the reference pose"), FormationPlacementRules::IsPosed(FormationPlacementRules::PoseOffRefDeg(Tiny, Ref)));
	TestEqual(TEXT("an empty pose: 0"), FormationPlacementRules::PoseOffRefDeg(TArray<FTransform>(), Ref), 0.f);
	TestEqual(TEXT("q and -q are the same rotation"), FormationPlacementRules::PoseOffRefDeg(TArray<FTransform>{ FTransform(-FQuat(FRotator(0.0, 0.0, 90.0))) },
		TArray<FTransform>{ FTransform(FQuat(FRotator(0.0, 0.0, 90.0))) }), 0.f, 0.01f);
	return true;
}

// Bug_05: a B tap in the placement mode toggled SOLO and the placement cancelled itself - the tap now confirms the placement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationPlacementBTapTest, "CodexTactics.UI.FormationPlacement.BTap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationPlacementBTapTest::RunTest(const FString&)
{
	TestTrue(TEXT("outside the placement mode a B tap is the solo toggle"), FormationPlacementRules::ResolveBTap(false) == FormationPlacementRules::EBTap::SoloToggle);
	TestTrue(TEXT("in the placement mode a B tap confirms it (never solo)"), FormationPlacementRules::ResolveBTap(true) == FormationPlacementRules::EBTap::ConfirmPlacement);
	return true;
}

#endif
