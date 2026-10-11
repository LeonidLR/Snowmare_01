// CodexTactics.Data.WeaponTuning.* — the Wave Editor's weapon power (weapons_tuning.json) onto a weapon asset.

#include "Data/WeaponDataAsset.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tactics/TurnBasedRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponTuningApplyTest, "CodexTactics.Data.WeaponTuning.ApplyAndRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponTuningApplyTest::RunTest(const FString& Parameters)
{
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->BaseDamage = 18.f;
	Weapon->AttackRangeCm = 1400.f;
	Weapon->MaxClipSize = 30;
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(
		TEXT("{\"base_damage\": 25, \"attack_range_m\": 16.5, \"max_clip_size\": 40, \"base_hit_chances\": [0.9, 0.8, 0.5]}")), Json);
	if (!TestTrue(TEXT("json parsed"), Json.IsValid()))
	{
		return false;
	}
	WeaponTuning::ApplyWeapon(*Json, *Weapon);
	TestEqual(TEXT("damage"), Weapon->BaseDamage, 25.f);
	TestEqual(TEXT("range metres -> cm"), Weapon->AttackRangeCm, 1650.f);
	TestEqual(TEXT("clip"), Weapon->MaxClipSize, 40);
	TestEqual(TEXT("hit chances"), Weapon->BaseHitChances.Num(), 3);
	TestEqual(TEXT("missing keys keep the asset value"), Weapon->ReloadTime, GetDefault<UWeaponDataAsset>()->ReloadTime);

	// The dump of the tuned weapon applies back to the same numbers.
	const TSharedRef<FJsonObject> Dumped = WeaponTuning::WeaponToJson(*Weapon);
	UWeaponDataAsset* Copy = NewObject<UWeaponDataAsset>();
	WeaponTuning::ApplyWeapon(*Dumped, *Copy);
	TestEqual(TEXT("round trip: range"), Copy->AttackRangeCm, Weapon->AttackRangeCm);
	TestEqual(TEXT("round trip: damage"), Copy->BaseDamage, Weapon->BaseDamage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponTuningTurnRulesTest, "CodexTactics.Data.WeaponTuning.TurnBasedRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponTuningTurnRulesTest::RunTest(const FString& Parameters)
{
	// A file with only the rules block (no weapons: no asset is touched).
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/turn_rules_test.json");
	FFileHelper::SaveStringToFile(TEXT("{\"turn_based_rules\": {\"crouch_move_cost_multiplier\": 3, \"cover_fire_accuracy_multiplier\": 0.5, \"enemy_fire_at_cover_multiplier\": 0.4, \"crouch_damage_multiplier\": 0.8, \"prone_damage_multiplier\": 0.3}}"), *Path);
	WeaponTuning::ApplyFile(Path);
	FTurnBasedBalance Balance;
	WeaponTuning::ApplyTurnRules(Balance);
	TestEqual(TEXT("crouched step x3"), Balance.CrouchMoveCostMultiplier, 3);
	TestEqual(TEXT("fire past cover x0.5"), Balance.CoverFireAccuracyMultiplier, 0.5f);
	TestEqual(TEXT("enemy fire at cover x0.4"), Balance.EnemyFireAtCoverMultiplier, 0.4f);
	TestEqual(TEXT("crouched damage x0.8"), Balance.CrouchDamageMultiplier, 0.8f);
	TestEqual(TEXT("prone damage x0.3"), Balance.ProneDamageMultiplier, 0.3f);
	// Back to the project's file for the rest of the session.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponTuningM16RangeTest, "CodexTactics.Data.WeaponTuning.M16Range25m",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponTuningM16RangeTest::RunTest(const FString& Parameters)
{
	// User decision 2026-10-11: M16 range 14 -> 25 m (weapons_tuning.json is the master copy, applied at StartPlay).
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("weapons_tuning.json read"), FFileHelper::LoadFileToString(Text, *WeaponTuning::GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Weapons = nullptr;
	const TSharedPtr<FJsonObject>* M16Json = nullptr;
	const TSharedPtr<FJsonObject>* SniperJson = nullptr;
	if (!TestTrue(TEXT("m16 + sniper_rifle entries"), Root->TryGetObjectField(TEXT("weapons"), Weapons) && Weapons
		&& (*Weapons)->TryGetObjectField(TEXT("m16"), M16Json) && M16Json && (*Weapons)->TryGetObjectField(TEXT("sniper_rifle"), SniperJson) && SniperJson))
	{
		return false;
	}
	UWeaponDataAsset* M16 = NewObject<UWeaponDataAsset>();
	UWeaponDataAsset* Sniper = NewObject<UWeaponDataAsset>();
	WeaponTuning::ApplyWeapon(**M16Json, *M16);
	WeaponTuning::ApplyWeapon(**SniperJson, *Sniper);

	TestEqual(TEXT("M16 real-time range 25 m"), M16->AttackRangeCm, 2500.f);
	TestTrue(TEXT("the sniper rifle (30 m) still outranges it"), Sniper->AttackRangeCm > M16->AttackRangeCm);
	TestEqual(TEXT("M16 turn-based range 8 cells (was 5)"), M16->MaxRangeCells, 8);
	TestTrue(TEXT("the sniper still outranges it on the grid"), Sniper->MaxRangeCells > M16->MaxRangeCells);

	// The hit table (per 1.5 m cell; real time reads it by round(m / 1.5), clamped to the last entry) covers the whole
	// 25 m and keeps every chance inside the old 14 m range (old table 0.95 0.85 0.75 0.65 0.5, then 0.5 clamped).
	TestTrue(TEXT("table covers 25 m"), M16->BaseHitChances.Num() >= FMath::RoundToInt(25.f / 1.5f));
	const float Old[] = { 0.95f, 0.85f, 0.75f, 0.65f, 0.5f };
	for (int32 Metres = 1; Metres <= 14; ++Metres)
	{
		const int32 Cells = FMath::Max(1, FMath::RoundToInt(Metres / 1.5f));
		TestEqual(FString::Printf(TEXT("%d m: unchanged chance"), Metres), M16->GetHitChanceForDistance(Cells), Old[FMath::Min(Cells, 5) - 1], 0.001f);
	}
	bool bFalling = true;
	for (int32 Index = 1; Index < M16->BaseHitChances.Num(); ++Index)
	{
		bFalling &= M16->BaseHitChances[Index] <= M16->BaseHitChances[Index - 1];
	}
	TestTrue(TEXT("never better further out"), bFalling);
	TestEqual(TEXT("25 m: 0.26"), M16->GetHitChanceForDistance(FMath::RoundToInt(25.f / 1.5f)), 0.26f, 0.001f);
	TestTrue(TEXT("the sniper is the better long shot"), Sniper->GetHitChanceForDistance(17) > M16->GetHitChanceForDistance(17));
	TestEqual(TEXT("damage table as long as the hit table"), M16->DistanceDamageMultipliers.Num(), M16->BaseHitChances.Num());
	return true;
}
