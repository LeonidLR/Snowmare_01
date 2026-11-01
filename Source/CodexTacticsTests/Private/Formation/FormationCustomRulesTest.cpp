// CodexTactics.Formation.Custom.* - the custom formation's pure rules (game designer decisions 2026-11-01; docs/design/FORMATION_CONTRACT.md 17,
// TS reference Tools/TrainingGround/src/movement/custom.ts; the golden parity is CodexTactics.Formation.Parity.Custom*). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationCustomRules.h"
#include "Formation/FormationMenuRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FormationCustomRulesTestLocal
{
	/** The contract's sample: the anchor 0, P 250 back / 300 right look 60, L 250 back / 300 left look -60, Z 450 back look 180. */
	FFormationCustomShape Sample()
	{
		FFormationCustomShape Shape;
		const float Rows[4][4] = { { 0.f, 0.f, 0.f, 0.f }, { 1.f, -250.f, 300.f, 60.f }, { 2.f, -250.f, -300.f, -60.f }, { 3.f, -450.f, 0.f, 180.f } };
		for (const auto& Row : Rows)
		{
			FFormationCustomMember& Member = Shape.Members.AddDefaulted_GetRef();
			Member.Squad = static_cast<int32>(Row[0]);
			Member.Fwd = Row[1];
			Member.Right = Row[2];
			Member.Look = Row[3];
		}
		return Shape;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationCustomSlotsTest, "CodexTactics.Formation.Custom.Slots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationCustomSlotsTest::RunTest(const FString&)
{
	using namespace FormationCustomRulesTestLocal;
	const FFormationCustomShape Shape = Sample();
	// The anchor leads, heading +Y (yaw 90): P 250 back / 300 right -> (-300, -250); his look 60 relative -> 150.
	TArray<FFormationCustomSlot> Slots = FormationCustomRules::CustomSlots(Shape, { 0, 1, 2, 3 }, FVector2D::ZeroVector, 90.f);
	TestEqual(TEXT("four slots"), Slots.Num(), 4);
	TestTrue(TEXT("P rotates with the heading"), Slots[1].Position.Equals(FVector2D(-300.0, -250.0), 0.01));
	TestEqual(TEXT("P keeps his look relative to the heading"), Slots[1].Yaw, 150.f, 0.01f);
	TestTrue(TEXT("Z behind"), Slots[3].Position.Equals(FVector2D(0.0, -450.0), 0.01));
	// Squad 2 leads: he takes the anchor's place, the anchor (squad 0) takes squad 2's stored place.
	Slots = FormationCustomRules::CustomSlots(Shape, { 2, 0, 1, 3 }, FVector2D::ZeroVector, 0.f);
	TestEqual(TEXT("the leader first"), Slots[0].Squad, 2);
	TestTrue(TEXT("the leader at the anchor"), Slots[0].Position.Equals(FVector2D::ZeroVector, 0.01));
	TestTrue(TEXT("the old anchor at the leader's stored place"), Slots[1].Squad == 0 && Slots[1].Position.Equals(FVector2D(-250.0, -300.0), 0.01));
	TestEqual(TEXT("... with that place's look"), Slots[1].Yaw, -60.f, 0.01f);
	// A dead member is skipped; the others keep their places.
	Slots = FormationCustomRules::CustomSlots(Shape, { 0, 3 }, FVector2D::ZeroVector, 0.f);
	TestTrue(TEXT("Z keeps his place without P / L"), Slots.Num() == 2 && Slots[1].Squad == 3 && Slots[1].Position.Equals(FVector2D(-450.0, 0.0), 0.01));
	// A member the shape lacks walks behind the rearmost place, looking back.
	FFormationCustomShape Three = Shape;
	Three.Members.SetNum(3);
	Slots = FormationCustomRules::CustomSlots(Three, { 0, 1, 2, 3 }, FVector2D::ZeroVector, 0.f);
	const FFormationConfig Config;
	TestFalse(TEXT("the extra is not a fixed look"), Slots[3].bFixed);
	TestTrue(TEXT("the extra behind the rearmost place"), Slots[3].Position.Equals(FVector2D(-250.0 - Config.ColumnSpacing, 0.0), 0.01));
	TestEqual(TEXT("the extra looks back"), FMath::Abs(Slots[3].Yaw), 180.f, 0.01f);
	TestEqual(TEXT("an empty shape gives no slots"), FormationCustomRules::CustomSlots(FFormationCustomShape(), { 0, 1 }, FVector2D::ZeroVector, 0.f).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationCustomGuardTest, "CodexTactics.Formation.Custom.GuardFoldsAndHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationCustomGuardTest::RunTest(const FString&)
{
	using namespace FormationCustomRulesTestLocal;
	const FFormationCustomShape Shape = Sample();
	const FFormationConfig Config;
	// A wall along the leader's right, between him and P.
	FFormationWall Wall;
	Wall.A = FVector2D(-600.0, 150.0);
	Wall.B = FVector2D(200.0, 150.0);
	Wall.Thickness = 20.f;
	const TArray<FFormationWall> Walls = { Wall };
	FFormationGuardState State = FormationCustomRules::Resolve(Walls, Shape, { 0, 1, 2, 3 }, FVector2D::ZeroVector, 0.f, TOptional<FFormationPose>(),
		FFormationGuardState(), 0.f, Config);
	TestTrue(TEXT("a wall cutting P's place folds into the trail column"), State.Level == EFormationGuardLevel::Trail && State.Effective == EFormationKind::Column);
	// Past the wall: still folded until the hold distance is walked.
	State = FormationCustomRules::Resolve(Walls, Shape, { 0, 1, 2, 3 }, FVector2D(3000.0, 0.0), 0.f, TOptional<FFormationPose>(), State, 100.f, Config);
	TestTrue(TEXT("held after 100 cm"), State.Level == EFormationGuardLevel::Trail);
	State = FormationCustomRules::Resolve(Walls, Shape, { 0, 1, 2, 3 }, FVector2D(3000.0, 0.0), 0.f, TOptional<FFormationPose>(), State, Config.ClassifyHoldDistance, Config);
	TestTrue(TEXT("unfolds into the custom shape after the hold"), State.Level == EFormationGuardLevel::Requested && State.Effective == EFormationKind::Custom);
	// A man cut off folds at once.
	State = FormationCustomRules::Resolve(TArray<FFormationWall>(), Shape, { 0, 1 }, FVector2D::ZeroVector, 0.f, TOptional<FFormationPose>(), State, 0.f, Config, false);
	TestTrue(TEXT("not all in sight -> trail"), State.Level == EFormationGuardLevel::Trail);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationCustomLookTest, "CodexTactics.Formation.Custom.LookOnlyAvoidsWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationCustomLookTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	FFormationWall Wall;
	Wall.A = FVector2D(150.0, -500.0);
	Wall.B = FVector2D(150.0, 500.0);
	const TArray<FFormationWall> Walls = { Wall };
	TestEqual(TEXT("a look away from the wall is kept exactly"), FormationCustomRules::Look(Walls, FVector2D::ZeroVector, 120.f, 120.f, Config), 120.f, 0.001f);
	const float Cleared = FormationCustomRules::Look(Walls, FVector2D::ZeroVector, 0.f, 0.f, Config);
	TestTrue(TEXT("a look into a wall 1.5 m away is turned out of the blocked span"), FMath::Abs(Cleared) >= Config.LookWallAngle - 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationCustomPlacementTest, "CodexTactics.Formation.Custom.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationCustomPlacementTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	TestEqual(TEXT("a click is the default look"), FormationCustomRules::DragLook(FVector2D(100.0, 100.0), FVector2D(120.0, 110.0), 45.f), 45.f, 0.001f);
	TestEqual(TEXT("a drag sets the look"), FormationCustomRules::DragLook(FVector2D::ZeroVector, FVector2D(0.0, 200.0), 0.f), 90.f, 0.001f);
	TArray<FFormationCustomGhost> Ghosts;
	FFormationCustomGhost Anchor;
	Anchor.Squad = 0;
	Ghosts.Add(Anchor);
	FFormationCustomGhost Other;
	Other.Squad = 1;
	Other.Position = FVector2D(-300.0, 0.0);
	Ghosts.Add(Other);
	TestTrue(TEXT("too close"), FormationCustomRules::CanPlace(Ghosts, 0, 2, FVector2D(-300.0, 50.0), Config) == EFormationCustomPlace::TooClose);
	TestTrue(TEXT("too far"), FormationCustomRules::CanPlace(Ghosts, 0, 2, FVector2D(1200.0, 0.0), Config) == EFormationCustomPlace::TooFar);
	TestTrue(TEXT("the anchor may not leave a man out of reach"), FormationCustomRules::CanPlace(Ghosts, 0, 0, FVector2D(900.0, 0.0), Config) == EFormationCustomPlace::TooFar);
	TestTrue(TEXT("ok"), FormationCustomRules::CanPlace(Ghosts, 0, 2, FVector2D(-300.0, 300.0), Config) == EFormationCustomPlace::Ok);
	// The shape in the anchor's frame (anchor at (100, 0) facing +Y): a man at (100, 300) is 300 forward.
	Ghosts[0].Position = FVector2D(100.0, 0.0);
	Ghosts[0].Yaw = 90.f;
	Ghosts[1].Position = FVector2D(100.0, 300.0);
	Ghosts[1].Yaw = 0.f;
	const FFormationCustomShape Shape = FormationCustomRules::ShapeFrom(Ghosts, 0);
	TestTrue(TEXT("anchor first"), Shape.Members.Num() == 2 && Shape.Members[0].Squad == 0);
	TestEqual(TEXT("forward"), Shape.Members[1].Fwd, 300.f, 0.05f);
	TestEqual(TEXT("look relative"), Shape.Members[1].Look, -90.f, 0.05f);
	// JSON round trip (save game / campaign carry).
	const FFormationCustomShape Back = FormationCustomRules::FromJsonString(FormationCustomRules::ToJsonString(Shape));
	TestTrue(TEXT("JSON round trip"), Back.Members.Num() == 2 && Back.Members[1].Squad == 1 && FMath::IsNearlyEqual(Back.Members[1].Fwd, 300.f, 0.05f));
	TestTrue(TEXT("an empty shape is an empty string"), FormationCustomRules::ToJsonString(FFormationCustomShape()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationCustomMenuTest, "CodexTactics.Formation.Custom.Menu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationCustomMenuTest::RunTest(const FString&)
{
	using namespace FormationMenuRules;
	TestTrue(TEXT("the middle disc"), CustomItemAt(0.f, CustomRowY) == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom2));
	TestTrue(TEXT("the left disc"), CustomItemAt(-CustomRowSpacing, CustomRowY + 30.f) == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom1));
	TestTrue(TEXT("the ring below is still the column"), HitAt(0.f, 150.f, 40.f) == TOptional<EFormationMenuItem>(EFormationMenuItem::Column));
	FFormationMenuState State;
	Open(State);
	Move(State, CustomRowSpacing, CustomRowY, 40.f);
	TStaticArray<bool, 3> Filled(InPlace, false);
	TestTrue(TEXT("an empty slot opens the placement"), Release(State, 3, Filled, false) == EFormationMenuEffect::Edit && !State.Choice.IsSet());
	Filled[2] = true;
	Open(State);
	Move(State, CustomRowSpacing, CustomRowY, 40.f);
	TestTrue(TEXT("a filled slot is picked"), Release(State, 3, Filled, false) == EFormationMenuEffect::Picked && State.Choice == TOptional<EFormationMenuItem>(EFormationMenuItem::Custom3));
	Open(State);
	Move(State, CustomRowSpacing, CustomRowY, 40.f);
	TestTrue(TEXT("Ctrl on a filled slot edits it"), Release(State, 3, Filled, true) == EFormationMenuEffect::Edit);
	const FFormationMenuResolved Resolved = Resolve(EFormationMenuItem::Custom3, 3);
	TestTrue(TEXT("resolves to the custom formation, slot 3"), Resolved.ManualFormation == TOptional<EFormationKind>(EFormationKind::Custom) && Resolved.CustomSlot == 3);
	TestFalse(TEXT("one operative: Auto"), Resolve(EFormationMenuItem::Custom1, 1).ManualFormation.IsSet());
	EFormationMenuItem Parsed = EFormationMenuItem::Line;
	TestTrue(TEXT("custom ids parse"), Parse(TEXT("custom_2"), Parsed) && Parsed == EFormationMenuItem::Custom2);
	return true;
}

#endif
