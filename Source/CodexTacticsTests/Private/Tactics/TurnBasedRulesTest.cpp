#include "Misc/AutomationTest.h"
#include "Data/WeaponDataAsset.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Parity with Godot Scripts/tactics/turn_based_combat_manager.gd calculate_hit_chance / get_weapon_attack_cells.

#define TURN_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.TurnBased." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TURN_TEST(FTurnHitChanceTest, "HitChanceByStanceAndDistance")
bool FTurnHitChanceTest::RunTest(const FString&)
{
	const FTurnBasedBalance Balance;
	TestEqual(TEXT("Fallback 1 cell standing"), TurnBasedRules::CalculateHitChance(nullptr, 1, EOperativeStance::Standing, Balance), 0.95f);
	TestEqual(TEXT("Fallback 3 cells crouching: 0.75 + 0.15"), TurnBasedRules::CalculateHitChance(nullptr, 3, EOperativeStance::Crouching, Balance), 0.9f, 0.0001f);
	TestEqual(TEXT("Fallback 9 cells prone: 0.50 + 0.30"), TurnBasedRules::CalculateHitChance(nullptr, 9, EOperativeStance::Prone, Balance), 0.8f, 0.0001f);
	TestEqual(TEXT("Clamped at 99 %"), TurnBasedRules::CalculateHitChance(nullptr, 1, EOperativeStance::Prone, Balance), 0.99f);
	TestEqual(TEXT("Same cell"), TurnBasedRules::CalculateHitChance(nullptr, 0, EOperativeStance::Standing, Balance), 1.f);

	UWeaponDataAsset* Pistol = NewObject<UWeaponDataAsset>();
	Pistol->BaseHitChances = { 0.9f, 0.6f };
	Pistol->BaseDamage = 20.f;
	Pistol->DistanceDamageMultipliers = { 1.f, 0.5f };
	TestEqual(TEXT("Weapon curve beyond its table uses the last entry"), TurnBasedRules::CalculateHitChance(Pistol, 4, EOperativeStance::Standing, Balance), 0.6f);
	TestEqual(TEXT("Damage for distance"), TurnBasedRules::GetDamageForDistance(Pistol, 3, 30.f), 10.f);
	TestEqual(TEXT("Turret 2 cells"), TurnBasedRules::CalculateTurretHitChance(2), 0.85f);
	return true;
}

TURN_TEST(FTurnAttackCellsTest, "WeaponAttackCells")
bool FTurnAttackCellsTest::RunTest(const FString&)
{
	const FTurnBasedBalance Balance;
	UGorkyGridManager* Grid = NewObject<UGorkyGridManager>();
	Grid->Setup(FVector::ZeroVector, FIntPoint(14, 14), 150.f, 0.f);
	AActor* Dummy = NewObject<AActor>();
	const FIntPoint From(6, 6);

	// Default rifle: 8 rays of 5 cells = 40 cells on an empty grid.
	TestEqual(TEXT("8 rays x 5"), TurnBasedRules::GetWeaponAttackCells(*Grid, From, nullptr, EOperativeStance::Standing, Balance).Num(), 40);

	// An enemy 2 cells east stops that ray: the enemy cell stays targetable, the cells behind it do not.
	Grid->SetOccupant(FIntPoint(8, 6), Dummy, EGorkyOccupantType::Enemy);
	TMap<FIntPoint, FTurnBasedAttackCell> Cells = TurnBasedRules::GetWeaponAttackCells(*Grid, From, nullptr, EOperativeStance::Standing, Balance);
	TestTrue(TEXT("Enemy cell targetable"), Cells.Contains(FIntPoint(8, 6)));
	TestFalse(TEXT("Behind the enemy blocked"), Cells.Contains(FIntPoint(9, 6)));
	TestEqual(TEXT("Distance recorded"), Cells[FIntPoint(8, 6)].Distance, 2);

	// A mine does not stop the ray.
	Grid->SetOccupant(FIntPoint(6, 8), Dummy, EGorkyOccupantType::Mine);
	Cells = TurnBasedRules::GetWeaponAttackCells(*Grid, From, nullptr, EOperativeStance::Standing, Balance);
	TestTrue(TEXT("Behind a mine still reachable"), Cells.Contains(FIntPoint(6, 10)));

	UWeaponDataAsset* Knife = NewObject<UWeaponDataAsset>();
	Knife->AttackShape = EAttackShape::MeleeAdj;
	TestEqual(TEXT("Melee: 8 neighbours"), TurnBasedRules::GetWeaponAttackCells(*Grid, From, Knife, EOperativeStance::Standing, Balance).Num(), 8);

	UWeaponDataAsset* Grenade = NewObject<UWeaponDataAsset>();
	Grenade->AttackShape = EAttackShape::FreeTarget;
	Grenade->MaxRangeCells = 2;
	TestEqual(TEXT("Free target radius 2: 5 x 5 - 1"), TurnBasedRules::GetWeaponAttackCells(*Grid, From, Grenade, EOperativeStance::Standing, Balance).Num(), 24);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
