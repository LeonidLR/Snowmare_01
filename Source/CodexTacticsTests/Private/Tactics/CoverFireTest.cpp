// CodexTactics.Tactics.CoverFire.* — UE rules (user decisions 2026-10-04): a crouched / prone operative pays double AP
// per step on the grid; a barricade next to the shooter or the target does not block the line of fire, it lowers the
// accuracy (a barricade in the open between them still blocks).

#include "Data/WeaponDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverFireTest, "CodexTactics.Tactics.CoverFire.CrouchStepsAndBarricadeFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverFireTest::RunTest(const FString&)
{
	const FTurnBasedBalance Balance;
	TestEqual(TEXT("standing step x1"), TurnBasedRules::MoveCostMultiplier(EOperativeStance::Standing, Balance), 1);
	TestEqual(TEXT("crouched step x2"), TurnBasedRules::MoveCostMultiplier(EOperativeStance::Crouching, Balance), 2);
	TestEqual(TEXT("prone (walks crouched) x2"), TurnBasedRules::MoveCostMultiplier(EOperativeStance::Prone, Balance), 2);

	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);
	AActor* Dummy = NewObject<AActor>();
	Grid->SetOccupant(FIntPoint(3, 0), Dummy, EGorkyOccupantType::Barricade);
	bool bThrough = false;
	TestTrue(TEXT("shooter at (2,0) behind his barricade (3,0) fires to (8,0)"),
		GorkyLineOfSight::HasLineOfFireThroughCover(FIntPoint(2, 0), FIntPoint(8, 0), *Grid, bThrough));
	TestTrue(TEXT("... through cover"), bThrough);
	TestTrue(TEXT("an enemy at (8,0) fires at the operative behind it (target's cover)"),
		GorkyLineOfSight::HasLineOfFireThroughCover(FIntPoint(8, 0), FIntPoint(2, 0), *Grid, bThrough) && bThrough);
	TestFalse(TEXT("a barricade in the open between them blocks"),
		GorkyLineOfSight::HasLineOfFireThroughCover(FIntPoint(0, 0), FIntPoint(8, 0), *Grid, bThrough));
	TestFalse(TEXT("the classic line of sight still treats it as a wall"), GorkyLineOfSight::HasLineOfSight(FIntPoint(2, 0), FIntPoint(8, 0), *Grid));

	// Attack cells from behind the barricade: the cells past it, the hit chance x CoverFireAccuracyMultiplier.
	UWeaponDataAsset* Rifle = NewObject<UWeaponDataAsset>();
	Rifle->AttackShape = EAttackShape::Rays8;
	Rifle->MaxRangeCells = 5;
	const TMap<FIntPoint, FTurnBasedAttackCell> Cells = TurnBasedRules::GetWeaponAttackCells(*Grid, FIntPoint(2, 0), Rifle, EOperativeStance::Crouching, Balance);
	const FTurnBasedAttackCell* Past = Cells.Find(FIntPoint(5, 0));
	TestTrue(TEXT("a cell past his own barricade is attackable"), Past != nullptr && Past->bThroughCover);
	TestFalse(TEXT("the barricade cell itself is no target"), Cells.Contains(FIntPoint(3, 0)));
	if (Past)
	{
		const float Open = TurnBasedRules::CalculateHitChance(Rifle, 3, EOperativeStance::Crouching, Balance);
		TestTrue(TEXT("lower accuracy past the cover"), FMath::IsNearlyEqual(Past->HitChance, Open * Balance.CoverFireAccuracyMultiplier, 0.001f));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
