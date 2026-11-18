// CodexTactics.Tactics.EnemyTurn.* — turn-based enemy tactics (EnemyTurnRules, UE-only).

#include "Misc/AutomationTest.h"
#include "Tactics/EnemyTurnRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTurnProfilesTest, "CodexTactics.Tactics.EnemyTurn.Profiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTurnProfilesTest::RunTest(const FString& Parameters)
{
	const FEnemyTurnProfile Hound = EnemyTurnRules::ProfileFor(EEnemyArchetype::FrostHound);
	TestEqual(TEXT("hound: balance AP"), EnemyTurnRules::MaxAP(Hound, 6), 6);
	TestEqual(TEXT("hound: balance damage"), EnemyTurnRules::BaseDamage(Hound, 18.f), 18.f);
	TestTrue(TEXT("hound bites and runs"), Hound.bHitAndRun && !Hound.bRanged);
	const FEnemyTurnProfile Brute = EnemyTurnRules::ProfileFor(EEnemyArchetype::Brute);
	TestEqual(TEXT("brute: 5 AP"), EnemyTurnRules::MaxAP(Brute, 6), 5);
	TestTrue(TEXT("brute hits harder and stays"), EnemyTurnRules::BaseDamage(Brute, 18.f) > 30.f && !Brute.bHitAndRun);
	TestEqual(TEXT("cutter: 7 AP"), EnemyTurnRules::MaxAP(EnemyTurnRules::ProfileFor(EEnemyArchetype::Cutter), 6), 7);
	TestTrue(TEXT("spitter shoots"), EnemyTurnRules::ProfileFor(EEnemyArchetype::Spitter).bRanged);
	TestTrue(TEXT("marksman shoots far"), EnemyTurnRules::ProfileFor(EEnemyArchetype::Marksman).MaxRange >= 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTurnRangedTest, "CodexTactics.Tactics.EnemyTurn.RangedHitAndFiringCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTurnRangedTest::RunTest(const FString& Parameters)
{
	const FEnemyTurnProfile Spitter = EnemyTurnRules::ProfileFor(EEnemyArchetype::Spitter);
	const float Near = EnemyTurnRules::RangedHitChance(Spitter, 2, EOperativeStance::Standing, false);
	const float Far = EnemyTurnRules::RangedHitChance(Spitter, 6, EOperativeStance::Standing, false);
	TestTrue(TEXT("falls off with range"), Near > Far);
	TestTrue(TEXT("prone + cover is much harder"),
		EnemyTurnRules::RangedHitChance(Spitter, 2, EOperativeStance::Prone, true) < Near * 0.5f);
	TestTrue(TEXT("never below 10 %"), EnemyTurnRules::RangedHitChance(Spitter, 30, EOperativeStance::Prone, true) >= 0.1f);

	// Cells: here without a line of fire, one step to a clear shot at 4, two steps to a clear shot next to an operative.
	TArray<FEnemyFiringCell> Cells;
	auto Add = [&Cells](int32 Cost, int32 Distance, bool bLos, bool bNext, float Chance)
	{
		FEnemyFiringCell& Cell = Cells.AddDefaulted_GetRef();
		Cell.Cell = FIntPoint(Cells.Num(), 0);
		Cell.PathCost = Cost;
		Cell.Distance = Distance;
		Cell.bLineOfFire = bLos;
		Cell.bNextToOperative = bNext;
		Cell.HitChance = Chance;
	};
	Add(0, 5, false, false, 0.6f);
	Add(1, 4, true, false, 0.6f);
	Add(2, 2, true, true, 0.75f);
	TestEqual(TEXT("clear shot in the band, not point-blank"), EnemyTurnRules::ChooseFiringCell(Spitter, Cells, 6), 1);
	TestEqual(TEXT("AP short of walk + shot: no shot"), EnemyTurnRules::ChooseFiringCell(Spitter, Cells, 3), static_cast<int32>(INDEX_NONE));
	Cells[1].bLineOfFire = false;
	TestEqual(TEXT("only the point-blank one left"), EnemyTurnRules::ChooseFiringCell(Spitter, Cells, 6), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTurnMeleeTest, "CodexTactics.Tactics.EnemyTurn.MeleeArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTurnMeleeTest::RunTest(const FString& Parameters)
{
	// Front cell 1 AP away (x1), back cell 3 AP away (x1.5 arc).
	TArray<FEnemyMeleeCell> Cells;
	FEnemyMeleeCell Front;
	Front.Cell = FIntPoint(1, 0);
	Front.PathCost = 1;
	Front.ArcMultiplier = 1.f;
	FEnemyMeleeCell Back;
	Back.Cell = FIntPoint(-1, 0);
	Back.PathCost = 3;
	Back.ArcMultiplier = 1.5f;
	Cells = { Front, Back };
	const FEnemyTurnProfile Cutter = EnemyTurnRules::ProfileFor(EEnemyArchetype::Cutter);
	const FEnemyTurnProfile Horde = EnemyTurnRules::ProfileFor(EEnemyArchetype::Frostbitten);
	TestEqual(TEXT("cutter goes round to the back"), EnemyTurnRules::ChooseMeleeCell(Cutter, Cells, 7), 1);
	TestEqual(TEXT("frostbitten takes the front"), EnemyTurnRules::ChooseMeleeCell(Horde, Cells, 4), 0);
	TestEqual(TEXT("cutter without AP for the back bite takes the front"), EnemyTurnRules::ChooseMeleeCell(Cutter, Cells, 4), 0);
	Cells[0].PathCost = 5;
	Cells[1].PathCost = 6;
	TestEqual(TEXT("nothing affordable: closes in on the nearest"), EnemyTurnRules::ChooseMeleeCell(Cutter, Cells, 4), 0);
	Cells[0].PathCost = -1;
	Cells[1].PathCost = -1;
	TestEqual(TEXT("unreachable: none"), EnemyTurnRules::ChooseMeleeCell(Cutter, Cells, 4), static_cast<int32>(INDEX_NONE));

	// G007 final review 2: the enemy already stands next to the target but below a 1.1 m ledge (cost 0, no bite): it repaths to
	// the reachable bite cell instead of parking there; with no bite cell at all there is no choice (the target is unusable).
	FEnemyMeleeCell Below;
	Below.Cell = FIntPoint(0, 1);
	Below.PathCost = 0;
	Below.bCanBite = false;
	FEnemyMeleeCell Ramp;
	Ramp.Cell = FIntPoint(0, -1);
	Ramp.PathCost = 3;
	Cells = { Below, Ramp };
	TestEqual(TEXT("below the ledge: repaths to the bite cell (frostbitten)"), EnemyTurnRules::ChooseMeleeCell(Horde, Cells, 6), 1);
	TestEqual(TEXT("below the ledge: repaths to the bite cell (cutter)"), EnemyTurnRules::ChooseMeleeCell(Cutter, Cells, 6), 1);
	TestEqual(TEXT("below the ledge, too little AP: closes in on the bite cell"), EnemyTurnRules::ChooseMeleeCell(Horde, Cells, 2), 1);
	Cells[1].bCanBite = false;
	TestEqual(TEXT("no bite cell: none"), EnemyTurnRules::ChooseMeleeCell(Horde, Cells, 6), static_cast<int32>(INDEX_NONE));
	return true;
}
