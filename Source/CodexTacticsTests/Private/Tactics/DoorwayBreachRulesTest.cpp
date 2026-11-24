// CodexTactics.Tactics.DoorwayBreach.* - Unit tests for tactical doorway breach (docs/design/DOORWAY_BREACH.md).
// Steps 1-3: DoorwayBreachRules geometry and slot logic, UDoorwaySubsystem runtime discovery, and ghost actor visual triad.

#include "Tactics/DoorwayBreachRules.h"
#include "Interactables/DoorwaySubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define DOORWAY_BREACH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.DoorwayBreach." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DOORWAY_BREACH_TEST(FDoorwayBreachStandardDoorSlotsTest, "StandardDoorSlots")
bool FDoorwayBreachStandardDoorSlotsTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	// Standard 120 cm building door at (1000, 2000, 0), wall along X, exterior normal pointing +Y (North)
	const FVector DoorCenter(1000.f, 2000.f, 0.f);
	const float WidthCm = 120.f;
	const FVector WallTangent(1.f, 0.f, 0.f);
	const FVector OutwardNormal(0.f, 1.f, 0.f);

	const FDoorwayBreachTriad Triad = CalculateBreachSlots(DoorCenter, WidthCm, WallTangent, OutwardNormal);

	TestTrue(TEXT("Triad is valid"), Triad.IsValid());
	TestEqual(TEXT("Opening width"), Triad.OpeningWidthCm, 120.f);
	TestEqual(TEXT("Outward normal"), Triad.OutwardNormal, FVector(0.f, 1.f, 0.f));

	// Looking into door (facing -Y South): Right is -X (West, 910), Left is +X (East, 1090)
	// HalfWidth = 60 cm, CornerStandoff = 30 cm -> |dx| = 90 cm. WallOffset = 45 cm -> y = 2045
	const FDoorwayBreachSlot& Left = Triad.LeftJamb;
	TestEqual(TEXT("Left jamb role"), Left.Role, EDoorwayBreachSlotRole::LeftJamb);
	TestTrue(TEXT("Left jamb location"), Left.Location.Equals(FVector(1090.f, 2045.f, 0.f), 0.1f));
	TestEqual(TEXT("Left jamb peek facing"), Left.PeekFacing, ECoverFacing::Right);
	TestTrue(TEXT("Left jamb requires cover"), Left.bRequiresCover);
	TestTrue(TEXT("Left jamb right edge exposed"), Left.CoverSlot.bRightEdgeExposed);
	TestEqual(TEXT("Left jamb edge distance"), Left.CoverSlot.RightEdgeDistanceCm, 30.f);

	const FDoorwayBreachSlot& Right = Triad.RightJamb;
	TestEqual(TEXT("Right jamb role"), Right.Role, EDoorwayBreachSlotRole::RightJamb);
	TestTrue(TEXT("Right jamb location"), Right.Location.Equals(FVector(910.f, 2045.f, 0.f), 0.1f));
	TestEqual(TEXT("Right jamb peek facing"), Right.PeekFacing, ECoverFacing::Left);
	TestTrue(TEXT("Right jamb requires cover"), Right.bRequiresCover);
	TestTrue(TEXT("Right jamb left edge exposed"), Right.CoverSlot.bLeftEdgeExposed);
	TestEqual(TEXT("Right jamb edge distance"), Right.CoverSlot.LeftEdgeDistanceCm, 30.f);

	// Center Point: 220 cm back on outward normal (2000 + 220 = 2220)
	const FDoorwayBreachSlot& Center = Triad.CenterPoint;
	TestEqual(TEXT("Center point role"), Center.Role, EDoorwayBreachSlotRole::CenterPoint);
	TestTrue(TEXT("Center point location"), Center.Location.Equals(FVector(1000.f, 2220.f, 0.f), 0.1f));
	TestFalse(TEXT("Center point does not require cover"), Center.bRequiresCover);
	TestEqual(TEXT("Center point crouched stance"), Center.Stance, EOperativeStance::Crouching);
	TestTrue(TEXT("Center point aim into opening"), Center.AimDirection.Equals(FVector(0.f, -1.f, 0.f), 0.01f));

	// Rear Security: 350 cm back on outward normal (2000 + 350 = 2350)
	const FDoorwayBreachSlot& Rear = Triad.RearSecurity;
	TestEqual(TEXT("Rear security role"), Rear.Role, EDoorwayBreachSlotRole::RearSecurity);
	TestTrue(TEXT("Rear security location"), Rear.Location.Equals(FVector(1000.f, 2350.f, 0.f), 0.1f));
	TestFalse(TEXT("Rear security does not require cover"), Rear.bRequiresCover);
	TestTrue(TEXT("Rear security reverse aim"), Rear.AimDirection.Equals(FVector(0.f, 1.f, 0.f), 0.01f));

	return true;
}

DOORWAY_BREACH_TEST(FDoorwayBreachWideGateSlotsTest, "WideGateSlots")
bool FDoorwayBreachWideGateSlotsTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	// 300 cm compound fence gate at (0, 0, 0), wall along Y, approach from +X (East)
	const FVector GateCenter(0.f, 0.f, 0.f);
	const float WidthCm = 300.f;
	const FVector WallTangent(0.f, 1.f, 0.f);
	const FVector OutwardNormal(1.f, 0.f, 0.f);

	const FDoorwayBreachTriad Triad = CalculateBreachSlots(GateCenter, WidthCm, WallTangent, OutwardNormal);

	TestEqual(TEXT("Gate width"), Triad.OpeningWidthCm, 300.f);

	// HalfWidth = 150 cm, CornerStandoff = 30 cm -> |dy| = 180 cm. WallOffset = 45 cm -> x = 45
	// Looking into gate (-X West): Right is +Y (North, 180), Left is -Y (South, -180)
	TestTrue(TEXT("Gate Left jamb at -Y"), Triad.LeftJamb.Location.Equals(FVector(45.f, -180.f, 0.f), 0.1f));
	TestTrue(TEXT("Gate Right jamb at +Y"), Triad.RightJamb.Location.Equals(FVector(45.f, 180.f, 0.f), 0.1f));
	TestTrue(TEXT("Gate Center at +X 220"), Triad.CenterPoint.Location.Equals(FVector(220.f, 0.f, 0.f), 0.1f));
	TestTrue(TEXT("Gate Rear at +X 350"), Triad.RearSecurity.Location.Equals(FVector(350.f, 0.f, 0.f), 0.1f));

	return true;
}

DOORWAY_BREACH_TEST(FDoorwayBreachSoloModeAssignmentTest, "SoloModeNearestJamb")
bool FDoorwayBreachSoloModeAssignmentTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	const FVector DoorCenter(1000.f, 2000.f, 0.f);
	const FDoorwayBreachTriad Triad = CalculateBreachSlots(DoorCenter, 120.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f));

	// Operative 1 at (850, 2100, 0) - closer to Right Jamb (910, 2045)
	const TArray<FVector> SingleOpRight = { FVector(850.f, 2100.f, 0.f) };
	const TArray<FDoorwaySlotAssignment> AssignRight = AssignSlots(SingleOpRight, Triad, true);

	TestEqual(TEXT("Solo assignment count 1"), AssignRight.Num(), 1);
	TestEqual(TEXT("Assigned to right jamb"), AssignRight[0].Slot.Role, EDoorwayBreachSlotRole::RightJamb);

	// Operative 2 at (1150, 2100, 0) - closer to Left Jamb (1090, 2045)
	const TArray<FVector> SingleOpLeft = { FVector(1150.f, 2100.f, 0.f) };
	const TArray<FDoorwaySlotAssignment> AssignLeft = AssignSlots(SingleOpLeft, Triad, true);

	TestEqual(TEXT("Solo assignment count 1"), AssignLeft.Num(), 1);
	TestEqual(TEXT("Assigned to left jamb"), AssignLeft[0].Slot.Role, EDoorwayBreachSlotRole::LeftJamb);

	return true;
}

DOORWAY_BREACH_TEST(FDoorwayBreachMinimalTravelSquadTest, "MinimalTravelSquad")
bool FDoorwayBreachMinimalTravelSquadTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	const FVector DoorCenter(1000.f, 2000.f, 0.f);
	const FDoorwayBreachTriad Triad = CalculateBreachSlots(DoorCenter, 120.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f));

	// LeftJamb is at (1090, 2045), RightJamb is at (910, 2045), CenterPoint is at (1000, 2220)
	// Op0 approaches from Left (+X): (1200, 2300)
	// Op1 approaches from Right (-X): (800, 2300)
	// Op2 approaches straight down center: (1000, 2600)
	const TArray<FVector> SquadPositions = {
		FVector(1200.f, 2300.f, 0.f),
		FVector(800.f, 2300.f, 0.f),
		FVector(1000.f, 2600.f, 0.f)
	};

	const TArray<FDoorwaySlotAssignment> Assignments = AssignSlots(SquadPositions, Triad, false);

	TestEqual(TEXT("3 assignments"), Assignments.Num(), 3);
	TestEqual(TEXT("Op0 gets Left Jamb"), Assignments[0].Slot.Role, EDoorwayBreachSlotRole::LeftJamb);
	TestEqual(TEXT("Op1 gets Right Jamb"), Assignments[1].Slot.Role, EDoorwayBreachSlotRole::RightJamb);
	TestEqual(TEXT("Op2 gets Center Point"), Assignments[2].Slot.Role, EDoorwayBreachSlotRole::CenterPoint);

	return true;
}

DOORWAY_BREACH_TEST(FDoorwayBreachAsymmetricObstructionTest, "AsymmetricObstructionFallback")
bool FDoorwayBreachAsymmetricObstructionTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	const FVector DoorCenter(1000.f, 2000.f, 0.f);
	FDoorwayBreachTriad Triad = CalculateBreachSlots(DoorCenter, 120.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f));

	// Left jamb is blocked by obstacle/rubble
	ApplyJambObstruction(Triad, true, false);

	// Open right jamb is preserved
	TestFalse(TEXT("Right jamb not blocked"), Triad.RightJamb.bBlocked);
	TestTrue(TEXT("Right jamb location unchanged"), Triad.RightJamb.Location.Equals(FVector(910.f, 2045.f, 0.f), 0.1f));

	// Center point is preserved
	TestTrue(TEXT("Center point location unchanged"), Triad.CenterPoint.Location.Equals(FVector(1000.f, 2220.f, 0.f), 0.1f));

	// Third operative stacks in-line behind the open right jamb:
	// RightDir is (-1, 0, 0), so fallback stack is at (910 - 60, 2045, 0) = (850, 2045, 0)
	TestFalse(TEXT("Fallback stack slot is active"), Triad.LeftJamb.bBlocked);
	TestTrue(TEXT("Fallback in-line stack behind right jamb"), Triad.LeftJamb.Location.Equals(FVector(850.f, 2045.f, 0.f), 0.1f));

	return true;
}

DOORWAY_BREACH_TEST(FDoorwayBreachPortalDetectionTest, "PortalDetection")
bool FDoorwayBreachPortalDetectionTest::RunTest(const FString&)
{
	using namespace DoorwayBreachRules;

	const FVector DoorCenter(0.f, 0.f, 0.f);
	const float WidthCm = 120.f;
	const FVector WallTangent(1.f, 0.f, 0.f);
	const FVector OutwardNormal(0.f, 1.f, 0.f);

	// Portal has width 120 (half-width 60) + lateral slack 40 = 100 cm reach along X
	// Portal depth slack 50 = 50 cm reach along Y
	TestTrue(TEXT("Door center is inside portal"), IsPointInDoorwayPortal(FVector(0.f, 0.f, 0.f), DoorCenter, WidthCm, WallTangent, OutwardNormal));
	TestTrue(TEXT("Mid opening is inside portal"), IsPointInDoorwayPortal(FVector(50.f, 20.f, 0.f), DoorCenter, WidthCm, WallTangent, OutwardNormal));
	TestTrue(TEXT("Edge with slack is inside portal"), IsPointInDoorwayPortal(FVector(95.f, -10.f, 0.f), DoorCenter, WidthCm, WallTangent, OutwardNormal));

	TestFalse(TEXT("Outside lateral span (> 100 cm) is false"), IsPointInDoorwayPortal(FVector(115.f, 0.f, 0.f), DoorCenter, WidthCm, WallTangent, OutwardNormal));
	TestFalse(TEXT("Outside depth span (> 50 cm) is false"), IsPointInDoorwayPortal(FVector(0.f, 65.f, 0.f), DoorCenter, WidthCm, WallTangent, OutwardNormal));

	return true;
}

DOORWAY_BREACH_TEST(FDoorwaySubsystemDepotDoorwaysTest, "Subsystem.DepotLayoutDiscovery")
bool FDoorwaySubsystemDepotDoorwaysTest::RunTest(const FString&)
{
	const FString LayoutPath = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted/gen_ref_01_02_depot_1.json");
	if (!FPaths::FileExists(LayoutPath))
	{
		AddWarning(FString::Printf(TEXT("Layout file not found at %s"), *LayoutPath));
		return true;
	}

	UDoorwaySubsystem* Subsystem = NewObject<UDoorwaySubsystem>();
	TestNotNull(TEXT("Subsystem instantiated"), Subsystem);

	const bool bLoaded = Subsystem->LoadFromLayout(LayoutPath);
	TestTrue(TEXT("Depot layout loaded successfully"), bLoaded);

	// Depot layout must discover exactly 23 doorways (14 building doors + 9 gates)
	TestEqual(TEXT("Total doorways on Depot"), Subsystem->GetDoorwayCount(), 23);

	// Check compound fence gate (F_compound_N#g0, 300 cm)
	const FRuntimeDoorway* Gate = Subsystem->FindDoorwayById(TEXT("F_compound_N#g0"));
	if (TestNotNull(TEXT("F_compound_N#g0 found"), Gate))
	{
		TestTrue(TEXT("Gate is recognized as gate"), Gate->bIsGate);
		TestEqual(TEXT("Gate width is 300 cm"), Gate->WidthCm, 300.f);
		TestTrue(TEXT("Gate is on mandatory route"), Gate->bOnMandatoryRoute);

		FDoorwayBreachTriad Triad;
		const FVector ApproachOrigin(1400.f, -5500.f, 0.f);
		TestTrue(TEXT("Triad generated for gate"), Subsystem->GetBreachTriadForDoorway(*Gate, Triad, &ApproachOrigin));
		TestTrue(TEXT("Triad is valid"), Triad.IsValid());
		TestEqual(TEXT("Triad opening width"), Triad.OpeningWidthCm, 300.f);
	}

	// Check ruin door (B_ruin#0)
	const FRuntimeDoorway* RuinDoor = Subsystem->FindDoorwayById(TEXT("B_ruin#0"));
	if (TestNotNull(TEXT("B_ruin#0 found"), RuinDoor))
	{
		TestFalse(TEXT("Ruin door is not a gate"), RuinDoor->bIsGate);
		TestEqual(TEXT("Ruin building ID"), RuinDoor->BuildingId, FString(TEXT("B_ruin")));
	}

	// Test spatial search
	FRuntimeDoorway FoundNear;
	TestTrue(TEXT("FindDoorwayNear finds gate at (1400, -6084)"), Subsystem->FindDoorwayNear(FVector(1400.f, -6084.f, 0.f), 200.f, FoundNear));
	TestEqual(TEXT("Found doorway ID matches"), FoundNear.Id, FString(TEXT("F_compound_N#g0")));

	return true;
}

DOORWAY_BREACH_TEST(FDoorwaySubsystemStoryLayoutDoorsTest, "Subsystem.StoryLayoutDoors")
bool FDoorwaySubsystemStoryLayoutDoorsTest::RunTest(const FString&)
{
	// G018: the kept showcase layout is schema v2. The props view refuses it, so the doorway smoke registered 0 of 0. Building doors
	// (not windows, not stair mouths) register, and yaw is the outward normal (S01 east door yaw 0, south door yaw 270).
	const FString Json = TEXT("{\"schema\":\"codex.levelgen_layout/2\",\"ground_z\":0,\"buildings\":[{\"id\":\"S01\",\"openings\":[")
		TEXT("{\"id\":\"S01_d0\",\"kind\":\"door\",\"pos\":[2500,-5550],\"floor\":0,\"width\":150,\"yaw\":0,\"role\":\"entry\"},")
		TEXT("{\"id\":\"S01_d1\",\"kind\":\"door\",\"pos\":[2350,-6700],\"floor\":0,\"width\":150,\"yaw\":270,\"role\":\"room\"},")
		TEXT("{\"id\":\"S01_w0\",\"kind\":\"window\",\"pos\":[1,2],\"floor\":0,\"width\":120,\"yaw\":0}],")
		TEXT("\"stairs\":[{\"id\":\"S01_stair\",\"pos\":[10,20],\"top\":[10,80],\"from\":0,\"to\":1,\"width\":150}]}]}");

	UDoorwaySubsystem* Subsystem = NewObject<UDoorwaySubsystem>();
	TestTrue(TEXT("v2 layout loaded"), Subsystem->LoadFromLayoutJson(Json));
	TestEqual(TEXT("only the two doors"), Subsystem->GetDoorwayCount(), 2);

	const FRuntimeDoorway* East = Subsystem->FindDoorwayById(TEXT("S01_d0"));
	if (TestNotNull(TEXT("east door"), East))
	{
		TestFalse(TEXT("not a gate"), East->bIsGate);
		TestEqual(TEXT("building"), East->BuildingId, FString(TEXT("S01")));
		TestEqual(TEXT("width"), East->WidthCm, 150.f);
		TestEqual(TEXT("on the ground"), East->Center, FVector(2500.f, -5550.f, 0.f));
		TestEqual(TEXT("yaw 0 looks +X"), East->OutwardNormal, FVector(1.f, 0.f, 0.f));
		TestEqual(TEXT("street entry"), East->Role, FString(TEXT("entry")));
		TestEqual(TEXT("wall runs along Y"), East->WallTangent, FVector(0.f, 1.f, 0.f));
	}
	const FRuntimeDoorway* South = Subsystem->FindDoorwayById(TEXT("S01_d1"));
	if (TestNotNull(TEXT("south door"), South))
	{
		TestEqual(TEXT("yaw 270 looks -Y"), South->OutwardNormal, FVector(0.f, -1.f, 0.f));
	}
	TestNull(TEXT("a window is not a doorway"), Subsystem->FindDoorwayById(TEXT("S01_w0")));
	TestNull(TEXT("a stair mouth is not a doorway"), Subsystem->FindDoorwayById(TEXT("S01_stair:pos")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
