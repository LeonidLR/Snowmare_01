// CodexTactics.Interactables.Tripwire.* — Sprint 09 tripwire mine (МУВ-3 + 2 Ф-1).

#include "Combat/SightRules.h"
#include "Interactables/TripwireRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireSpanTest, "CodexTactics.Interactables.Tripwire.SpanAndTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireSpanTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestFalse(TEXT("0.8 m too short"), IsSpanValid(80.f));
	TestTrue(TEXT("1 m"), IsSpanValid(100.f));
	TestTrue(TEXT("5 m"), IsSpanValid(500.f));
	TestFalse(TEXT("5.2 m too long"), IsSpanValid(520.f));

	// Stance heights of Sprint 08: standing / crouched trip the 30 cm wire, prone crawls under it.
	TestTrue(TEXT("standing trips"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Standing)));
	TestTrue(TEXT("crouched trips"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Crouching)));
	TestFalse(TEXT("prone passes under"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Prone)));

	const FVector2D A(0.f, 0.f);
	const FVector2D B(400.f, 0.f);
	TestEqual(TEXT("distance to the middle"), DistanceToWire(FVector2D(200.f, 30.f), A, B), 30.f, 0.01f);
	TestEqual(TEXT("distance past the end"), DistanceToWire(FVector2D(500.f, 0.f), A, B), 100.f, 0.01f);
	TestTrue(TEXT("a body on the wire"), TouchesWire(FVector2D(200.f, 20.f), 35.f, A, B));
	TestFalse(TEXT("a body beside it"), TouchesWire(FVector2D(200.f, 60.f), 35.f, A, B));
	TestFalse(TEXT("a body beyond the anchor"), TouchesWire(FVector2D(-20.f, 0.f), 35.f, A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireBlastTest, "CodexTactics.Interactables.Tripwire.CostBlastDisarm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireBlastTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestEqual(TEXT("2 grenades"), GrenadeCost, 2);
	TestEqual(TEXT("wire at 30 cm"), WireHeightCm, 30.f);
	TestEqual(TEXT("fuse 0.25 s"), FuseDelaySeconds, 0.25f);
	TestEqual(TEXT("140 damage"), BlastDamage, 140.f);
	TestEqual(TEXT("4.5 m radius"), BlastRadiusCm, 450.f);
	TestEqual(TEXT("100 % armour"), ArmorPenetration, 1.f);
	TestEqual(TEXT("rig 2 s"), RigSeconds, 2.f);
	TestEqual(TEXT("disarm 3 s"), DisarmSeconds, 3.f);
	TestEqual(TEXT("clean disarm returns 2"), GrenadesReturned(false), 2);
	TestEqual(TEXT("fumble returns 1"), GrenadesReturned(true), 1);
	return true;
}
