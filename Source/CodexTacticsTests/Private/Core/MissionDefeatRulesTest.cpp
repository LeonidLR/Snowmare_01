#include "Misc/AutomationTest.h"
#include "Core/MissionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Defeat rule (user decision 2026-10-08, replaces Godot main.gd _check_squad_vital_signs "any member down = game over"):
// only the commander's death fails the mission; the other members are permanent losses.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionDefeatCommanderOnlyTest, "CodexTactics.Mission.DefeatRule.CommanderOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMissionDefeatCommanderOnlyTest::RunTest(const FString&)
{
	using namespace MissionRules;
	TestTrue(TEXT("Commander dies, two alive: defeat"), ShouldFailMission(true, 2));
	TestTrue(TEXT("Commander dies last: defeat"), ShouldFailMission(true, 0));
	TestFalse(TEXT("Engineer dies, commander + medic alive: the fight goes on"), ShouldFailMission(false, 2));
	TestFalse(TEXT("Medic dies, only the commander left: the fight goes on"), ShouldFailMission(false, 1));
	TestFalse(TEXT("Recruit dies: the fight goes on"), ShouldFailMission(false, 3));
	TestTrue(TEXT("Nobody left (roster without a commander): defeat"), ShouldFailMission(false, 0));
	TestEqual(TEXT("Defeat line"), GetSquadFallenText().ToString(), FString(TEXT("THE SQUAD HAS FALLEN")));
	TestTrue(TEXT("Member lost line names him"), GetMemberLostRadio(FText::FromString(TEXT("Engineer"))).ToString().StartsWith(TEXT("Engineer is down")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
