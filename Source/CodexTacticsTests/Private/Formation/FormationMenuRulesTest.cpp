// CodexTactics.Formation.Menu.* - the hold-B radial formation menu (Formation/FormationMenuRules; design docs/design/FORMATION_SECTORS.md
// "Selection", contract docs/design/FORMATION_CONTRACT.md 9, mirrors Tools/TrainingGround/src/movement/menu.ts). UE-only (no Godot
// reference). Screen px, x right, y DOWN, 12 o'clock = up.

#include "Misc/AutomationTest.h"
#include "Formation/FormationMenuRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_MENU_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Menu." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationMenuTestUtil
{
	constexpr float DeadZone = 40.f;

	/** The cursor 100 px from the centre at a clock position (12 = up, clockwise; y down). */
	TOptional<EFormationMenuItem> AtClock(float Clock, float RadiusPx = 100.f)
	{
		const float Radians = FMath::DegreesToRadians(30.f * Clock);
		return FormationMenuRules::ItemAt(RadiusPx * FMath::Sin(Radians), -RadiusPx * FMath::Cos(Radians), DeadZone);
	}
}

FORMATION_MENU_TEST(FFormationMenuItemAtTest, "ItemAtClockSectors")
bool FFormationMenuItemAtTest::RunTest(const FString&)
{
	using namespace FormationMenuTestUtil;
	using EI = EFormationMenuItem;
	// The design's layout: 12 line, 1:30 diamond, 3 echelon right, 4:30 blizzard, 6 column, 7:30 hedgehog, 9 echelon left, 10:30 all forward.
	const EI Expected[] = { EI::Line, EI::Diamond, EI::EchelonRight, EI::Blizzard, EI::Column, EI::Hedgehog, EI::EchelonLeft, EI::AllForward };
	for (int32 Index = 0; Index < FormationMenuRules::ItemCount; ++Index)
	{
		const float Clock = 1.5f * static_cast<float>(Index);
		TestTrue(FString::Printf(TEXT("%.1f o'clock -> %s"), Clock, FormationMenuRules::ToString(Expected[Index])), AtClock(Clock) == TOptional<EI>(Expected[Index]));
		// Each item owns +-22.5 deg round its clock position.
		TestTrue(FString::Printf(TEXT("%s owns +-20 deg"), FormationMenuRules::ToString(Expected[Index])),
			AtClock(Clock + 20.f / 30.f) == TOptional<EI>(Expected[Index]) && AtClock(Clock - 20.f / 30.f) == TOptional<EI>(Expected[Index]));
		TestTrue(TEXT("Clock position of the item"), FMath::IsNearlyEqual(FormationMenuRules::ItemClock(Expected[Index]), Index == 0 ? 12.f : Clock));
	}
	// y down: the cursor above the centre (negative y) is 12.
	TestTrue(TEXT("Straight up (dy < 0): line"), FormationMenuRules::ItemAt(0.f, -100.f, DeadZone) == TOptional<EI>(EI::Line));
	TestTrue(TEXT("Straight down: column"), FormationMenuRules::ItemAt(0.f, 100.f, DeadZone) == TOptional<EI>(EI::Column));
	TestTrue(TEXT("Right: echelon right"), FormationMenuRules::ItemAt(100.f, 0.f, DeadZone) == TOptional<EI>(EI::EchelonRight));
	// The dead zone (inclusive) is Auto.
	TestFalse(TEXT("Centre: Auto"), FormationMenuRules::ItemAt(0.f, 0.f, DeadZone).IsSet());
	TestFalse(TEXT("On the dead zone's edge: Auto"), FormationMenuRules::ItemAt(DeadZone, 0.f, DeadZone).IsSet());
	TestTrue(TEXT("Just outside: an item"), FormationMenuRules::ItemAt(DeadZone + 0.5f, 0.f, DeadZone).IsSet());
	// Just past the line / diamond border (22.5 deg): diamond; just before it: line.
	TestTrue(TEXT("23 deg: diamond"), AtClock(23.f / 30.f) == TOptional<EI>(EI::Diamond) && AtClock(22.f / 30.f) == TOptional<EI>(EI::Line));
	// Contract ids round-trip; "auto" is a choice, not an item.
	for (int32 Index = 0; Index < FormationMenuRules::ItemCount; ++Index)
	{
		EI Parsed = EI::Line;
		TestTrue(TEXT("Id round trip"), FormationMenuRules::Parse(FormationMenuRules::ToString(static_cast<EI>(Index)), Parsed) && Parsed == static_cast<EI>(Index));
	}
	TOptional<EI> Choice(EI::Column);
	TestTrue(TEXT("\"auto\" parses as the Auto choice"), FormationMenuRules::ParseChoice(TEXT("auto"), Choice) && !Choice.IsSet());
	EI Ignored = EI::Line;
	TestFalse(TEXT("\"auto\" is not an item"), FormationMenuRules::Parse(TEXT("auto"), Ignored));
	return true;
}

FORMATION_MENU_TEST(FFormationMenuFlowTest, "OpenMoveReleaseCancel")
bool FFormationMenuFlowTest::RunTest(const FString&)
{
	using namespace FormationMenuTestUtil;
	using EI = EFormationMenuItem;
	FFormationMenuState State;
	TestTrue(TEXT("Start: closed, Auto"), !State.bOpen && !State.Choice.IsSet() && !State.Highlighted.IsSet());
	// Closed: Move / Release / Cancel do nothing.
	FormationMenuRules::Move(State, 100.f, 0.f, DeadZone);
	TestFalse(TEXT("Closed: Move does not highlight"), State.Highlighted.IsSet());
	TestEqual(TEXT("Closed: Release does nothing"), FormationMenuRules::Release(State, 4), EFormationMenuEffect::None);
	TestEqual(TEXT("Closed: Cancel does nothing"), FormationMenuRules::Cancel(State), EFormationMenuEffect::None);

	// Hold B, move to 3 o'clock, release: echelon right, held.
	TestEqual(TEXT("B held: opened"), FormationMenuRules::Open(State), EFormationMenuEffect::Opened);
	TestTrue(TEXT("Opened with Auto highlighted"), State.bOpen && !State.Highlighted.IsSet());
	FormationMenuRules::Move(State, 100.f, 0.f, DeadZone);
	TestTrue(TEXT("Highlight follows the cursor"), State.Highlighted == TOptional<EI>(EI::EchelonRight));
	TestFalse(TEXT("Nothing chosen before the release"), State.Choice.IsSet());
	TestEqual(TEXT("B released: picked"), FormationMenuRules::Release(State, 4), EFormationMenuEffect::Picked);
	TestTrue(TEXT("Closed, echelon right holds"), !State.bOpen && State.Choice == TOptional<EI>(EI::EchelonRight) && !State.Highlighted.IsSet());

	// Esc / RMB keeps the previous choice.
	FormationMenuRules::Open(State);
	TestTrue(TEXT("Re-opening keeps the choice until the release"), State.Choice == TOptional<EI>(EI::EchelonRight));
	FormationMenuRules::Move(State, 0.f, 100.f, DeadZone);
	TestEqual(TEXT("Esc: cancelled"), FormationMenuRules::Cancel(State), EFormationMenuEffect::Cancelled);
	TestTrue(TEXT("Cancel keeps echelon right"), !State.bOpen && State.Choice == TOptional<EI>(EI::EchelonRight) && !State.Highlighted.IsSet());

	// A disabled pick is refused (one operative: no formation).
	FormationMenuRules::Open(State);
	FormationMenuRules::Move(State, 0.f, 100.f, DeadZone);
	TestEqual(TEXT("Alone: the column is refused"), FormationMenuRules::Release(State, 1), EFormationMenuEffect::Disabled);
	TestTrue(TEXT("Refused: the previous choice stays, the menu closes"), !State.bOpen && State.Choice == TOptional<EI>(EI::EchelonRight));

	// The centre picks Auto.
	FormationMenuRules::Open(State);
	FormationMenuRules::Move(State, 100.f, 0.f, DeadZone);
	FormationMenuRules::Move(State, 5.f, -5.f, DeadZone);
	TestEqual(TEXT("Released in the centre: Auto"), FormationMenuRules::Release(State, 4), EFormationMenuEffect::Auto);
	TestFalse(TEXT("Back to Auto"), State.Choice.IsSet());
	return true;
}

FORMATION_MENU_TEST(FFormationMenuResolveTest, "ResolveChoice")
bool FFormationMenuResolveTest::RunTest(const FString&)
{
	using EI = EFormationMenuItem;
	const TOptional<EI> Auto;
	TestFalse(TEXT("Auto: the terrain rules"), FormationMenuRules::Resolve(Auto, 4).ManualFormation.IsSet() || FormationMenuRules::Resolve(Auto, 4).bAllForward);
	const FFormationMenuResolved Forward = FormationMenuRules::Resolve(EI::AllForward, 4);
	TestTrue(TEXT("All forward: the terrain rules + look forward"), !Forward.ManualFormation.IsSet() && Forward.bAllForward);
	TestTrue(TEXT("Diamond with 4: diamond"), FormationMenuRules::Resolve(EI::Diamond, 4).ManualFormation == TOptional<EFormationKind>(EFormationKind::Diamond));
	TestTrue(TEXT("Diamond with 3: wedge"), FormationMenuRules::Resolve(EI::Diamond, 3).ManualFormation == TOptional<EFormationKind>(EFormationKind::Wedge));
	TestTrue(TEXT("Diamond with 2: wedge (the pair's slots)"), FormationMenuRules::Resolve(EI::Diamond, 2).ManualFormation == TOptional<EFormationKind>(EFormationKind::Wedge));
	const EI Items[] = { EI::Line, EI::EchelonRight, EI::Blizzard, EI::Column, EI::Hedgehog, EI::EchelonLeft };
	const EFormationKind Kinds[] = { EFormationKind::Line, EFormationKind::EchelonRight, EFormationKind::Blizzard, EFormationKind::Column,
		EFormationKind::Hedgehog, EFormationKind::EchelonLeft };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Items)); ++Index)
	{
		const FFormationMenuResolved Resolved = FormationMenuRules::Resolve(Items[Index], 3);
		TestTrue(FString::Printf(TEXT("%s -> its formation"), FormationMenuRules::ToString(Items[Index])),
			Resolved.ManualFormation == TOptional<EFormationKind>(Kinds[Index]) && !Resolved.bAllForward);
	}
	// Members died: a choice the count no longer allows falls back to Auto.
	const FFormationMenuResolved Alone = FormationMenuRules::Resolve(EI::Column, 1);
	TestTrue(TEXT("Alone: the manual column falls back to Auto"), !Alone.ManualFormation.IsSet() && !Alone.bAllForward);
	TestFalse(TEXT("Alone: all forward falls back too"), FormationMenuRules::Resolve(EI::AllForward, 1).bAllForward);
	TestTrue(TEXT("Every item needs 2 members"), FormationMenuRules::IsEnabled(EI::Hedgehog, 2) && !FormationMenuRules::IsEnabled(EI::Hedgehog, 1));
	return true;
}

#undef FORMATION_MENU_TEST

#endif
