// CodexTactics.Interactables.TrapSpot.* — Demo 01 rev 5h (game designer play-test 2026-10-15): a spotted hidden trap stops the
// squad and switches it to Solo (TrapSpotRules::DecideSquadResponse); the corner peek reveals hidden traps in its sight cone
// (TrapSpotRules::IsInPeekSightCone); tagged trap groups for the tutorial ("TrapSpotted.<tag>", stat "traps_spotted.<tag>").

#include "Interactables/TrapSpotRules.h"
#include "Misc/AutomationTest.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTypes.h"
#include "Tutorial/TutorialRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TRAP_SPOT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Interactables.TrapSpot." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TRAP_SPOT_TEST(FTrapSpotSquadResponseTest, "SquadResponse")
bool FTrapSpotSquadResponseTest::RunTest(const FString&)
{
	using namespace TrapSpotRules;
	TestTrue(TEXT("exploring, player control, three operatives: hold + Solo"),
		DecideSquadResponse(true, false, 3) == ETrapSpotResponse::HoldAndSolo);
	TestTrue(TEXT("two operatives: hold + Solo"), DecideSquadResponse(true, false, 2) == ETrapSpotResponse::HoldAndSolo);
	TestTrue(TEXT("AUTO mode: its paths go round known traps - hold only"), DecideSquadResponse(true, true, 3) == ETrapSpotResponse::HoldOnly);
	TestTrue(TEXT("preparation / fight (no formation): hold only"), DecideSquadResponse(false, false, 3) == ETrapSpotResponse::HoldOnly);
	TestTrue(TEXT("a squad of one: hold only"), DecideSquadResponse(true, false, 1) == ETrapSpotResponse::HoldOnly);
	return true;
}

TRAP_SPOT_TEST(FTrapSpotPeekConeTest, "PeekCone")
bool FTrapSpotPeekConeTest::RunTest(const FString&)
{
	using namespace TrapSpotRules;
	const FTrapPeekSpotConfig Config;
	TestEqual(TEXT("default range 12 m"), Config.RangeCm, 1200.f);
	TestEqual(TEXT("default cone 60 deg"), Config.HalfAngleDeg * 2.f, 60.f);
	TestTrue(TEXT("line of sight on by default"), Config.bRequireLineOfSight);

	const FVector Origin(0.f, 0.f, 140.f);
	const FVector Forward(1.f, 0.f, 0.f);
	TestTrue(TEXT("straight ahead 8 m"), IsInPeekSightCone(Origin, Forward, FVector(800.f, 0.f, 30.f), 1200.f, 30.f));
	TestTrue(TEXT("29 deg off, 10 m"), IsInPeekSightCone(Origin, Forward, FVector(1000.f * FMath::Cos(FMath::DegreesToRadians(29.f)),
		1000.f * FMath::Sin(FMath::DegreesToRadians(29.f)), 0.f), 1200.f, 30.f));
	TestFalse(TEXT("31 deg off"), IsInPeekSightCone(Origin, Forward, FVector(1000.f * FMath::Cos(FMath::DegreesToRadians(31.f)),
		1000.f * FMath::Sin(FMath::DegreesToRadians(31.f)), 0.f), 1200.f, 30.f));
	TestFalse(TEXT("beyond 12 m"), IsInPeekSightCone(Origin, Forward, FVector(1250.f, 0.f, 0.f), 1200.f, 30.f));
	TestFalse(TEXT("behind him"), IsInPeekSightCone(Origin, Forward, FVector(-500.f, 0.f, 0.f), 1200.f, 30.f));
	TestFalse(TEXT("at the origin"), IsInPeekSightCone(Origin, Forward, Origin, 1200.f, 30.f));
	TestTrue(TEXT("height does not count (planar)"), IsInPeekSightCone(Origin, Forward, FVector(1100.f, 0.f, -800.f), 1200.f, 30.f));

	// The corner-aim direction the peek looks along (CoverFacingRules::CornerAimDirection, 100 deg outward): a wall along Y
	// facing -X (the squad hides on the -X side; his right = -Y), peeking round either end looks beyond the wall (+X).
	FCoverSlot Slot;
	Slot.WallNormal = FVector(-1.f, 0.f, 0.f);
	Slot.bRightEdgeExposed = true;
	Slot.bLeftEdgeExposed = true;
	const FVector RightAim = CoverFacingRules::CornerAimDirection(Slot, ECoverFacing::Right, 100.f);
	const FVector LeftAim = CoverFacingRules::CornerAimDirection(Slot, ECoverFacing::Left, 100.f);
	TestTrue(TEXT("the right corner is at -Y"), Slot.RightTangent().Y < -0.99f);
	const FVector RightCorner(0.f, -150.f, 140.f);
	const FVector LeftCorner(0.f, 150.f, 140.f);
	TestTrue(TEXT("right corner: a mine 8 m beyond the wall is seen"), IsInPeekSightCone(RightCorner, RightAim, FVector(800.f, -100.f, 0.f), 1200.f, 30.f));
	TestFalse(TEXT("right corner: a mine on the near side is not"), IsInPeekSightCone(RightCorner, RightAim, FVector(-600.f, -150.f, 0.f), 1200.f, 30.f));
	TestFalse(TEXT("right corner: a mine 8 m off along the wall line is not"), IsInPeekSightCone(RightCorner, RightAim, FVector(0.f, -950.f, 0.f), 1200.f, 30.f));
	TestTrue(TEXT("left corner: beyond the wall too"), IsInPeekSightCone(LeftCorner, LeftAim, FVector(800.f, 100.f, 0.f), 1200.f, 30.f));
	return true;
}

TRAP_SPOT_TEST(FTrapSpotGroupTest, "GroupEventAndStat")
bool FTrapSpotGroupTest::RunTest(const FString&)
{
	using namespace TrapSpotRules;
	TestEqual(TEXT("group event"), GroupEventName(TEXT("A3PeekMines")), FName(TEXT("TrapSpotted.A3PeekMines")));
	TestTrue(TEXT("no tag, no event"), GroupEventName(NAME_None).IsNone());
	TestEqual(TEXT("stat tag"), ParseSpottedStatTag(TEXT("traps_spotted.A3PeekMines")), FName(TEXT("A3PeekMines")));
	TestTrue(TEXT("no tag"), ParseSpottedStatTag(TEXT("traps_spotted.")).IsNone());
	TestTrue(TEXT("another stat"), ParseSpottedStatTag(TEXT("squad_cold_max")).IsNone());
	TestTrue(TEXT("the tutorial knows the group stat"), TutorialRules::IsKnownStat(TEXT("traps_spotted.A3PeekMines")));
	TestFalse(TEXT("... but not a bare prefix"), TutorialRules::IsKnownStat(TEXT("traps_spotted.")));
	TestTrue(TEXT("the old stats stay known"), TutorialRules::IsKnownStat(TEXT("heat_packs_given")));
	return true;
}

// Rev 5i (game designer 2026-10-15): the A3 lesson mines can be spotted only by the peek until the peek lesson starts.
TRAP_SPOT_TEST(FTrapSpotProximityGateTest, "ProximityGate")
bool FTrapSpotProximityGateTest::RunTest(const FString&)
{
	using namespace TrapSpotRules;
	const FName Step(TEXT("A3_peek"));
	TestTrue(TEXT("no requirement: the normal proximity rule"), AllowsProximitySpot(NAME_None, true, true, false));
	TestFalse(TEXT("the lesson step not started yet: only a peek reveals it"), AllowsProximitySpot(Step, true, true, false));
	TestTrue(TEXT("the lesson step started (shown / done): proximity spots it again"), AllowsProximitySpot(Step, true, true, true));
	TestTrue(TEXT("tutorial OFF / skipped / an ordinary level: the normal rule"), AllowsProximitySpot(Step, false, false, false));
	TestTrue(TEXT("a step the steps file does not have: the normal rule (never a trap nobody can spot)"), AllowsProximitySpot(Step, true, false, false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
