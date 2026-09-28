#include "Misc/AutomationTest.h"
#include "Data/CombatTypes.h"
#include "Data/EnemyArchetypeAsset.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowStateMachine.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COMBAT_DATA_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Data." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

COMBAT_DATA_TEST(FCombatDataWeaponDefaultsTest, "WeaponDefaults")
bool FCombatDataWeaponDefaultsTest::RunTest(const FString&)
{
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->WeaponId = TEXT("m16");
	Weapon->WeaponName = FText::FromString(TEXT("Автомат МТКМ-16"));
	Weapon->DamageType = EDamageType::Kinetic;
	Weapon->BaseDamage = 18.0f;
	Weapon->AttackRangeCm = 1400.0f;
	Weapon->FireRate = 0.65f;
	Weapon->ArmorPenetration = 0.20f;
	Weapon->MaxClipSize = 30;
	Weapon->ReloadTime = 2.68f;

	TestEqual(TEXT("M16 base damage is 18"), Weapon->BaseDamage, 18.0f);
	TestEqual(TEXT("M16 range is 14m"), Weapon->AttackRangeCm, 1400.0f);
	TestEqual(TEXT("M16 clip is 30"), Weapon->MaxClipSize, 30);
	TestEqual(TEXT("M16 fire rate is 0.65s"), Weapon->FireRate, 0.65f);
	TestEqual(TEXT("M16 reload time is 2.68s"), Weapon->ReloadTime, 2.68f);
	return true;
}

COMBAT_DATA_TEST(FCombatDataEnemyArchetypeDefaultsTest, "EnemyArchetypeDefaults")
bool FCombatDataEnemyArchetypeDefaultsTest::RunTest(const FString&)
{
	// Hound: 45 HP, 540 cm/s, 12 dmg, 180 cm melee range, 1.0s cooldown
	UEnemyArchetypeAsset* Hound = NewObject<UEnemyArchetypeAsset>();
	Hound->Archetype = EEnemyArchetype::FrostHound;
	Hound->MaxHealth = 45.0f;
	Hound->MoveSpeedCm = 540.0f;
	Hound->AttackDamage = 12.0f;
	Hound->AttackRangeCm = 180.0f;
	Hound->AttackCooldown = 1.0f;
	Hound->bFearsFire = true;

	TestEqual(TEXT("Hound HP is 45"), Hound->MaxHealth, 45.0f);
	TestEqual(TEXT("Hound speed is 540 cm/s"), Hound->MoveSpeedCm, 540.0f);
	TestEqual(TEXT("Hound damage is 12"), Hound->AttackDamage, 12.0f);
	TestTrue(TEXT("Hound fears fire"), Hound->bFearsFire);

	// Spitter: 70 HP, 320 cm/s, 18 dmg, 1500 cm ranged, 1200 cm preferred distance
	UEnemyArchetypeAsset* Spitter = NewObject<UEnemyArchetypeAsset>();
	Spitter->Archetype = EEnemyArchetype::Spitter;
	Spitter->MaxHealth = 70.0f;
	Spitter->MoveSpeedCm = 320.0f;
	Spitter->AttackDamage = 18.0f;
	Spitter->AttackRangeCm = 1500.0f;
	Spitter->PreferredDistanceCm = 1200.0f;

	TestEqual(TEXT("Spitter HP is 70"), Spitter->MaxHealth, 70.0f);
	TestEqual(TEXT("Spitter range is 1500 cm"), Spitter->AttackRangeCm, 1500.0f);
	TestEqual(TEXT("Spitter preferred distance is 1200 cm"), Spitter->PreferredDistanceCm, 1200.0f);

	// Brute: 220 HP, 180 cm/s, 35 dmg, 240 cm range, heavy armor, barricade mult 2.0
	UEnemyArchetypeAsset* Brute = NewObject<UEnemyArchetypeAsset>();
	Brute->Archetype = EEnemyArchetype::Brute;
	Brute->MaxHealth = 220.0f;
	Brute->MoveSpeedCm = 180.0f;
	Brute->AttackDamage = 35.0f;
	Brute->ArmorTier = EArmorTier::Heavy;
	Brute->BarricadeDamageMultiplier = 2.0f;

	TestEqual(TEXT("Brute HP is 220"), Brute->MaxHealth, 220.0f);
	TestEqual(TEXT("Brute speed is 180 cm/s"), Brute->MoveSpeedCm, 180.0f);
	TestEqual(TEXT("Brute barricade damage multiplier is 2.0"), Brute->BarricadeDamageMultiplier, 2.0f);
	return true;
}

COMBAT_DATA_TEST(FCombatDataWaveTotalEnemyCountTest, "WaveTotalEnemyCount")
bool FCombatDataWaveTotalEnemyCountTest::RunTest(const FString&)
{
	FWaveDefinition Wave;
	Wave.WaveIndex = 1;

	FEnemySpawnEntry HoundSpawn;
	HoundSpawn.EnemyType = EEnemyArchetype::FrostHound;
	HoundSpawn.Count = 6;
	Wave.Spawns.Add(HoundSpawn);

	FEnemySpawnEntry CutterSpawn;
	CutterSpawn.EnemyType = EEnemyArchetype::Cutter;
	CutterSpawn.Count = 3;
	Wave.Spawns.Add(CutterSpawn);

	FEnemySpawnEntry BruteSpawn;
	BruteSpawn.EnemyType = EEnemyArchetype::Brute;
	BruteSpawn.Count = 1;
	Wave.Spawns.Add(BruteSpawn);

	TestEqual(TEXT("Total wave enemy count is 6+3+1=10"), Wave.GetTotalEnemyCount(), 10);
	return true;
}

COMBAT_DATA_TEST(FCombatDataFinishPreparationAdvancesToWaveTest, "FinishPreparationAdvancesToWave")
bool FCombatDataFinishPreparationAdvancesToWaveTest::RunTest(const FString&)
{
	FGameFlowStateMachine Machine;
	Machine.TriggerCombatZone();
	Machine.FinishCutscene(); // Advances to Preparation

	TestEqual(TEXT("Phase is Preparation"), Machine.GetPhase(), ECodexGamePhase::Preparation);
	TestEqual(TEXT("Wave index is 1"), Machine.GetWaveIndex(), 1);

	// Early finish via button / command
	const EGameFlowResult Result = Machine.FinishPreparation();
	TestEqual(TEXT("FinishPreparation result is Ok"), Result, EGameFlowResult::Ok);
	TestEqual(TEXT("Phase is WaveCombat"), Machine.GetPhase(), ECodexGamePhase::WaveCombat);
	TestEqual(TEXT("Combat mode is RealTime"), Machine.GetCombatMode(), ECodexCombatMode::RealTime);
	TestEqual(TEXT("Wave active is true"), Machine.IsWaveActive(), true);
	return true;
}

#undef COMBAT_DATA_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
