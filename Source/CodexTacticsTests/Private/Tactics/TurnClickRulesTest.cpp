// CodexTactics.Tactics.TurnBased.CtrlClick* / HeldEnemyGuard — user request 2026-10-06: Ctrl + click is the attack
// order in turn-based combat too (it replaced Godot's Shift); bug fix 2026-10-06: a frozen enemy pushed off its cell or
// off the grid is detected (TurnClickRules).

#include "Misc/AutomationTest.h"
#include "Tactics/TurnClickRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TURN_CLICK_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.TurnBased." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TURN_CLICK_TEST(FTurnCtrlClickAttacksTest, "CtrlClickAttacks")
bool FTurnCtrlClickAttacksTest::RunTest(const FString&)
{
	using namespace TurnClickRules;
	// Ctrl + click = attack: enemy, barrel and barricade (near or far), never a walk / relocation / selection.
	TestEqual(TEXT("Ctrl + enemy"), ResolveClick(EGorkyOccupantType::Enemy, true, false, false), ETurnClickAction::Attack);
	TestEqual(TEXT("Ctrl + adjacent barrel: shot, not a push"), ResolveClick(EGorkyOccupantType::Barrel, true, true, false), ETurnClickAction::Attack);
	TestEqual(TEXT("Ctrl + far barrel"), ResolveClick(EGorkyOccupantType::Barrel, true, false, false), ETurnClickAction::Attack);
	TestEqual(TEXT("Ctrl + adjacent barricade: attack"), ResolveClick(EGorkyOccupantType::Barricade, true, true, false), ETurnClickAction::Attack);
	TestEqual(TEXT("Ctrl + own turret: nothing"), ResolveClick(EGorkyOccupantType::Turret, true, true, false), ETurnClickAction::None);
	TestEqual(TEXT("Ctrl + squad mate: nothing"), ResolveClick(EGorkyOccupantType::Squad, true, false, false), ETurnClickAction::None);
	TestEqual(TEXT("Ctrl + empty cell: hint, no walk"), ResolveClick(EGorkyOccupantType::None, true, false, false), ETurnClickAction::NoTargetForAttackOrder);
	TestEqual(TEXT("Ctrl + mine cell: hint, no walk"), ResolveClick(EGorkyOccupantType::Mine, true, false, false), ETurnClickAction::NoTargetForAttackOrder);
	return true;
}

TURN_CLICK_TEST(FTurnPlainClickTest, "CtrlClickPlainClickUnchanged")
bool FTurnPlainClickTest::RunTest(const FString&)
{
	using namespace TurnClickRules;
	// Without Ctrl the Godot grid rules stay (an enemy is still attacked by a plain click).
	TestEqual(TEXT("Enemy"), ResolveClick(EGorkyOccupantType::Enemy, false, false, false), ETurnClickAction::Attack);
	TestEqual(TEXT("Adjacent barrel: relocation"), ResolveClick(EGorkyOccupantType::Barrel, false, true, false), ETurnClickAction::Relocate);
	TestEqual(TEXT("Far barrel: approach hint"), ResolveClick(EGorkyOccupantType::Barrel, false, false, false), ETurnClickAction::NeedApproach);
	TestEqual(TEXT("Adjacent barricade: relocation"), ResolveClick(EGorkyOccupantType::Barricade, false, true, false), ETurnClickAction::Relocate);
	TestEqual(TEXT("Adjacent turret: relocation"), ResolveClick(EGorkyOccupantType::Turret, false, true, false), ETurnClickAction::Relocate);
	TestEqual(TEXT("Squad mate: select"), ResolveClick(EGorkyOccupantType::Squad, false, false, false), ETurnClickAction::SelectUnit);
	TestEqual(TEXT("Empty: walk"), ResolveClick(EGorkyOccupantType::None, false, false, false), ETurnClickAction::Walk);
	TestEqual(TEXT("Empty in attack mode: no walk"), ResolveClick(EGorkyOccupantType::None, false, false, true), ETurnClickAction::NoTargetInAttackMode);
	return true;
}

TURN_CLICK_TEST(FTurnHeldEnemyGuardTest, "HeldEnemyGuard")
bool FTurnHeldEnemyGuardTest::RunTest(const FString&)
{
	using namespace TurnClickRules;
	const float Cell = 150.f;
	const float Tolerance = GetHeldUnitTolerance(Cell);
	TestEqual(TEXT("Half a cell"), Tolerance, 75.f);
	const FVector Anchor(1000.f, 1000.f, 90.f);
	TestFalse(TEXT("Standing on its cell"), IsHeldUnitDisplaced(Anchor + FVector(20.f, -30.f, 50.f), Anchor, Tolerance));
	TestFalse(TEXT("Height changes alone do not count (standing up from prone)"), IsHeldUnitDisplaced(Anchor + FVector(0.f, 0.f, 60.f), Anchor, Tolerance));
	TestTrue(TEXT("Thrown 3 m back"), IsHeldUnitDisplaced(Anchor + FVector(-300.f, 0.f, 0.f), Anchor, Tolerance));
	// The 14 x 14 grid of 1.5 m: 21 m square from its corner.
	const FVector Origin(0.f, 0.f, 0.f);
	TestTrue(TEXT("Inside"), IsInsideGrid(FVector(1050.f, 2000.f, 0.f), Origin, 14, Cell));
	TestFalse(TEXT("Beyond the far edge"), IsInsideGrid(FVector(2100.f, 100.f, 0.f), Origin, 14, Cell));
	TestFalse(TEXT("Behind the corner"), IsInsideGrid(FVector(-1.f, 100.f, 0.f), Origin, 14, Cell));
	return true;
}

#undef TURN_CLICK_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
