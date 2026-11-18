#include "Characters/CoverSlotHandoverRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverSlotHandoverTest, "CodexTactics.Tactics.Cover.SlotHandover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverSlotHandoverTest::RunTest(const FString& Parameters)
{
	using namespace CoverSlotHandoverRules;
	TestFalse(TEXT("A stationary occupant is not vacating"), IsVacating(false, 200.f, 90.f));
	TestFalse(TEXT("A move ending inside the shared capsule radius does not clear the slot"), IsVacating(true, 89.f, 90.f));
	TestTrue(TEXT("A move ending outside the shared capsule radius clears the slot"), IsVacating(true, 90.f, 90.f));
	TestEqual(TEXT("Keep the order while the destination overlaps"), DecideWait(true, 1.0, 3.0), EWaitResult::Wait);
	TestEqual(TEXT("Enter when the destination is free"), DecideWait(false, 2.0, 3.0), EWaitResult::Enter);
	TestEqual(TEXT("A clear slot at the deadline still admits the operative"), DecideWait(false, 3.0, 3.0), EWaitResult::Enter);
	TestEqual(TEXT("An occupied slot times out at the deadline"), DecideWait(true, 3.0, 3.0), EWaitResult::Timeout);
	return true;
}
