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

#endif
