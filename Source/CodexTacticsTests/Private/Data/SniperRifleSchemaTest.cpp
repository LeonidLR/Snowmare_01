// CodexTactics.Weapons.Sniper.* - the sniper rifle onto the framework (2026-11-09): the Medic-Sapper's rifle data (heavy class, its numbers),
// the armour rule against the Brute, and the Marksman enemy's rifle read through the weapon schema with the same numbers as before.

#include "Characters/MarksmanAIRules.h"
#include "Combat/ArmorRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponStatsRules.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SniperRifleSchemaTest
{
	TSharedPtr<FJsonObject> ProjectFile()
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapons_tuning.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			return Root;
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> Block(const TSharedPtr<FJsonObject>& Root, const TCHAR* Group, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* GroupObject = nullptr;
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Root.IsValid() && Root->TryGetObjectField(Group, GroupObject) && GroupObject && (*GroupObject)->TryGetObjectField(Key, Object) && Object
			? *Object : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperRifleDataTest, "CodexTactics.Weapons.Sniper.RifleData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSniperRifleDataTest::RunTest(const FString&)
{
	using namespace SniperRifleSchemaTest;
	const TSharedPtr<FJsonObject> Rifle = Block(ProjectFile(), TEXT("weapons"), TEXT("sniper_rifle"));
	if (!TestTrue(TEXT("weapons.sniper_rifle"), Rifle.IsValid()))
	{
		return false;
	}
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->WeaponId = TEXT("sniper_rifle");
	Weapon->Stats = Weapon->MakeBaseStats();
	TArray<FString> Errors;
	TestTrue(TEXT("the block applies"), WeaponTuning::ApplyWeapon(*Rifle, *Weapon, &Errors));
	const FWeaponStats& Stats = Weapon->Stats;
	TestTrue(TEXT("handling: sniper rifle (kneel / prone, the bolt)"), Stats.IsSniperRifle());
	TestTrue(TEXT("armour class heavy (decision 2026-11-08)"), Stats.Damage.PenetrationClass == EArmorClass::Heavy);
	TestEqual(TEXT("70 damage"), Stats.Damage.BaseDamage, 70.f);
	TestEqual(TEXT("5 rounds"), Stats.Ammo.MagazineSize, 5);
	TestEqual(TEXT("3.5 s reload"), Stats.Ammo.ReloadSeconds, 3.5f);
	TestEqual(TEXT("2.4 s between shots"), Stats.Fire.ShotIntervalSeconds, 2.4f);
	TestEqual(TEXT("reach 120 m"), Stats.Range.MaxRangeCm, 12000.f);
	TestEqual(TEXT("effective 90 m"), Stats.Range.GetEffectiveRangeCm(), 9000.f);
	// The class rule: heavy reaches the Brute (heavy 0.75): 0.75 x (1 - 0.6) = 0.30 stopped; the M16 (light) does not: 0.75.
	TestEqual(TEXT("vs the Brute: 30 % stopped (heavy beats heavy)"), ArmorRules::EffectiveReduction(0.75f, false, Stats.Damage.ArmorPenetration, Stats.Damage.PenetrationClass,
		EArmorClass::Heavy), 0.30f, 0.001f);
	TestEqual(TEXT("... a light weapon: 75 % stopped"), ArmorRules::EffectiveReduction(0.75f, false, 0.25f, EArmorClass::Light, EArmorClass::Heavy), 0.75f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarksmanRifleSchemaTest, "CodexTactics.Weapons.Sniper.MarksmanOnSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarksmanRifleSchemaTest::RunTest(const FString&)
{
	using namespace SniperRifleSchemaTest;
	const TSharedPtr<FJsonObject> Rifle = Block(ProjectFile(), TEXT("enemy_weapons"), TEXT("marksman_rifle"));
	if (!TestTrue(TEXT("enemy_weapons.marksman_rifle"), Rifle.IsValid()))
	{
		return false;
	}
	// Parity: the schema block gives the numbers the old block gave (45 damage, 2.5 s, 80 / 110 m, accuracy 0.6, aim 2 s, crit 25 % x2,
	// prone x1.35, crouch x1.15, band 20-35 m).
	FMarksmanConfig Config;
	FWeaponStats Stats;
	TArray<FString> Errors;
	WeaponTuning::ApplyMarksmanRifleJson(*Rifle, Config, &Stats, &Errors);
	TestEqual(TEXT("no errors"), Errors.Num(), 0);
	TestEqual(TEXT("damage 45 (base_damage)"), Config.ShotDamage, 45.f);
	TestEqual(TEXT("shot interval 2.5 s (fire_rate)"), Config.ShotCooldown, 2.5f);
	TestEqual(TEXT("effective 80 m"), Config.EffectiveRangeCm, 8000.f);
	TestEqual(TEXT("reach 110 m (attack_range_m)"), Config.MaxRangeCm, 11000.f);
	TestEqual(TEXT("accuracy 0.6"), Config.BaseAccuracy, 0.6f);
	TestEqual(TEXT("aim 2 s"), Config.AimDuration, 2.f);
	TestEqual(TEXT("crit 25 %"), Config.CritChance, 0.25f);
	TestEqual(TEXT("crit x2"), Config.CritMultiplier, 2.f);
	TestEqual(TEXT("prone x1.35"), Config.ProneAccuracyBonus, 1.35f);
	TestEqual(TEXT("crouch x1.15"), Config.CrouchAccuracyBonus, 1.15f);
	TestEqual(TEXT("band 20 m"), Config.PreferredMinRange, 2000.f);
	TestEqual(TEXT("... to 35 m"), Config.PreferredMaxRange, 3500.f);
	TestTrue(TEXT("the stats: hitscan, 45, 2.5 s, 110 m"), Stats.Kind == EWeaponKind::Hitscan && Stats.Damage.BaseDamage == 45.f
		&& Stats.Fire.ShotIntervalSeconds == 2.5f && Stats.Range.MaxRangeCm == 11000.f);
	// Old files (the keys before 2026-11-09) still apply.
	const TSharedRef<FJsonObject> Old = MakeShared<FJsonObject>();
	Old->SetNumberField(TEXT("damage"), 50.0);
	Old->SetNumberField(TEXT("shot_cooldown"), 3.0);
	Old->SetNumberField(TEXT("max_range_m"), 100.0);
	FMarksmanConfig OldConfig;
	WeaponTuning::ApplyMarksmanRifleJson(*Old, OldConfig);
	TestTrue(TEXT("an old block: damage / shot_cooldown / max_range_m still read"), OldConfig.ShotDamage == 50.f && OldConfig.ShotCooldown == 3.f
		&& OldConfig.MaxRangeCm == 10000.f);
	// An invalid schema value is rejected: the rifle keeps its values.
	const TSharedRef<FJsonObject> Bad = MakeShared<FJsonObject>();
	Bad->SetNumberField(TEXT("base_damage"), 99.0);
	Bad->SetNumberField(TEXT("fire_rate"), 0.0);
	FMarksmanConfig BadConfig;
	TArray<FString> BadErrors;
	WeaponTuning::ApplyMarksmanRifleJson(*Bad, BadConfig, nullptr, &BadErrors);
	TestTrue(TEXT("fire_rate 0 is rejected: damage / interval unchanged"), BadErrors.Num() > 0 && BadConfig.ShotDamage == FMarksmanConfig().ShotDamage
		&& BadConfig.ShotCooldown == FMarksmanConfig().ShotCooldown);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
