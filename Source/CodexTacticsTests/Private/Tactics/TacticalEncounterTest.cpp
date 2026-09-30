#include "Misc/AutomationTest.h"
#include "Tactics/TacticalEncounterRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot tactical_encounter_selector.gd select_participants parity (radius 15 m, cap 6, diversity first).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTacticalEncounterTest, "CodexTactics.Tactics.Encounter.SelectEnemies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTacticalEncounterTest::RunTest(const FString&)
{
	using TacticalEncounterRules::FCandidate;
	const FName Hound(TEXT("FrostHound"));
	const FName Brute(TEXT("Brute"));
	const FName Cutter(TEXT("Cutter"));

	// Few enough: everyone within the radius, the far one stays out.
	const TArray<int32> Small = TacticalEncounterRules::SelectEnemies(
		{ FCandidate{ Hound, 300.f }, FCandidate{ Brute, 900.f }, FCandidate{ Hound, 1600.f } }, 1500.f, 6);
	TestEqual(TEXT("Within 15 m only"), Small, TArray<int32>{ 0, 1 });

	// Eight hounds close by, a brute and a cutter further away: both keep a slot, then the four nearest hounds.
	TArray<FCandidate> Pack;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Pack.Add(FCandidate{ Hound, 200.f + 100.f * Index }); // indices 0..7, 200..900 cm
	}
	Pack.Add(FCandidate{ Brute, 1400.f }); // 8
	Pack.Add(FCandidate{ Cutter, 1300.f }); // 9
	Pack.Add(FCandidate{ Cutter, 1450.f }); // 10
	const TArray<int32> Picked = TacticalEncounterRules::SelectEnemies(Pack, 1500.f, 6);
	TestEqual(TEXT("Capped at 6"), Picked.Num(), 6);
	TestTrue(TEXT("Every species represented (nearest cutter)"), Picked.Contains(8) && Picked.Contains(9) && !Picked.Contains(10));
	TestTrue(TEXT("Then the nearest hounds"), Picked.Contains(0) && Picked.Contains(1) && Picked.Contains(2) && Picked.Contains(3)
		&& !Picked.Contains(4));

	// More species than slots: one per species in order of appearance.
	TArray<FCandidate> Zoo;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Zoo.Add(FCandidate{ FName(*FString::Printf(TEXT("Species%d"), Index)), 100.f * (8 - Index) });
	}
	TestEqual(TEXT("First six species"), TacticalEncounterRules::SelectEnemies(Zoo, 1500.f, 6), TArray<int32>{ 0, 1, 2, 3, 4, 5 });
	return true;
}

#endif
