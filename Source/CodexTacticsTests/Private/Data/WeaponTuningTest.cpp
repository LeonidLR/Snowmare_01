// CodexTactics.Data.WeaponTuning.* — the Wave Editor's weapon power (weapons_tuning.json) onto a weapon asset.

#include "Data/WeaponDataAsset.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
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
