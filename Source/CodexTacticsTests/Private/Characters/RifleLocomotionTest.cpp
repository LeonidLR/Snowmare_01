// CodexTactics.Characters.RifleLocomotion.* — Rifle_2 starts / stops / turn-in-place choices (ABP_Operative_Rifle2).

#include "Characters/RifleLocomotionRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRifleLocoSectorTest, "CodexTactics.Characters.RifleLocomotion.SectorsAndFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRifleLocoSectorTest::RunTest(const FString& Parameters)
{
	using namespace RifleLocomotionRules;
	TestEqual(TEXT("0 -> F"), FString(SectorName(DirectionSector(0.f))), FString(TEXT("F")));
	TestEqual(TEXT("20 -> F"), DirectionSector(20.f), 0);
	TestEqual(TEXT("30 -> FR"), FString(SectorName(DirectionSector(30.f))), FString(TEXT("FR")));
	TestEqual(TEXT("90 -> RR"), FString(SectorName(DirectionSector(90.f))), FString(TEXT("RR")));
	TestEqual(TEXT("135 -> BR"), FString(SectorName(DirectionSector(135.f))), FString(TEXT("BR")));
	TestEqual(TEXT("180 -> B"), FString(SectorName(DirectionSector(180.f))), FString(TEXT("B")));
	TestEqual(TEXT("-180 -> B"), FString(SectorName(DirectionSector(-180.f))), FString(TEXT("B")));
	TestEqual(TEXT("-135 -> BL"), FString(SectorName(DirectionSector(-135.f))), FString(TEXT("BL")));
	TestEqual(TEXT("-90 -> LL"), FString(SectorName(DirectionSector(-90.f))), FString(TEXT("LL")));
	TestEqual(TEXT("-45 -> FL"), FString(SectorName(DirectionSector(-45.f))), FString(TEXT("FL")));
	TestEqual(TEXT("-23 -> FL"), FString(SectorName(DirectionSector(-23.f))), FString(TEXT("FL")));
	TestEqual(TEXT("right sectors start with the right foot"), StartFoot(2), 1);
	TestEqual(TEXT("forward starts with the left foot"), StartFoot(0), 0);
	TestEqual(TEXT("left starts with the left foot"), StartFoot(6), 0);
	TestEqual(TEXT("stop foot: first half cycle left"), StopFoot(0.2f, 1.f), 0);
	TestEqual(TEXT("stop foot: second half right"), StopFoot(0.7f, 1.f), 1);
	TestEqual(TEXT("clip index: B right foot"), ClipIndex(4, 1), 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRifleLocoTurnTest, "CodexTactics.Characters.RifleLocomotion.TurnInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRifleLocoTurnTest::RunTest(const FString& Parameters)
{
	using namespace RifleLocomotionRules;
	TestEqual(TEXT("delta 350 -> 10 = +20"), DeltaYaw(350.f, 10.f), 20.f, 0.01f);
	TestEqual(TEXT("delta 10 -> 350 = -20"), DeltaYaw(10.f, 350.f), -20.f, 0.01f);
	TestEqual(TEXT("delta 0 -> 180 = +180"), DeltaYaw(0.f, 180.f), 180.f, 0.01f);
	TestFalse(TEXT("30° off: no turn"), ShouldTurnInPlace(30.f, 0.f, false));
	TestTrue(TEXT("50° off: turn"), ShouldTurnInPlace(50.f, 0.f, false));
	TestFalse(TEXT("moving: no turn"), ShouldTurnInPlace(90.f, 120.f, true));
	bool bRight = false;
	TestEqual(TEXT("-50 -> 45 bucket"), TurnBucket(-50.f, bRight), 0);
	TestTrue(TEXT("-50: the actor turned right -> R clip"), bRight);
	TestEqual(TEXT("+95 -> 90 bucket"), TurnBucket(95.f, bRight), 1);
	TestFalse(TEXT("+95 -> L clip"), bRight);
	TestEqual(TEXT("-130 -> 135 bucket"), TurnBucket(-130.f, bRight), 2);
	TestEqual(TEXT("flip +180 -> 180 bucket"), TurnBucket(180.f, bRight), 3);
	TestTrue(TEXT("a flip turns right"), bRight);
	TestEqual(TEXT("-170 -> 180 bucket"), TurnBucket(-170.f, bRight), 3);
	TestEqual(TEXT("bucket degrees"), BucketDegrees(2), 135.f);
	TestTrue(TEXT("start: intent + speed"), ShouldStart(30.f, true));
	TestFalse(TEXT("no start without intent"), ShouldStart(30.f, false));
	TestTrue(TEXT("stop when the intent is gone"), ShouldStop(false));
	return true;
}
