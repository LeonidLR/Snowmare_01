#include "Misc/AutomationTest.h"
#include "Interactables/CorpseLootRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// A fallen operative's remains (play-test bug 2026-10-26): the click box round the lying body and the gear it holds.
// Godot reference: Scenes/movements/player.gd get_items_list / loot_single_item (corpse_loot).

#define CORPSE_LOOT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Loot.Remains." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CorpseLootRulesTest
{
	/** Is Point inside the oriented box (with a tolerance)? */
	bool InsideRemainsBox(const CorpseLootRules::FRemainsBox& Box, const FVector& Point, float Tolerance = 0.1f)
	{
		const FVector Local = FRotator(0.f, Box.Yaw, 0.f).UnrotateVector(Point - Box.Center);
		return FMath::Abs(Local.X) <= Box.Extent.X + Tolerance && FMath::Abs(Local.Y) <= Box.Extent.Y + Tolerance
			&& FMath::Abs(Local.Z) <= Box.Extent.Z + Tolerance;
	}
}

CORPSE_LOOT_TEST(FCorpseLootFitBodyTest, "BoxFitsTheLyingBody")
bool FCorpseLootFitBodyTest::RunTest(const FString&)
{
	using namespace CorpseLootRulesTest;
	// A body lying on its back diagonally (45 deg), feet at the capsule (1000, 0), head 160 cm away, 5-25 cm above the snow at z 0.
	const FVector Axis = FVector(1.f, 1.f, 0.f).GetSafeNormal();
	const FVector Feet(1000.f, 0.f, 8.f);
	TArray<FVector> Bones = { Feet, Feet + Axis * 45.f + FVector(0.f, 0.f, 6.f), Feet + Axis * 95.f + FVector(0.f, 0.f, 15.f),
		Feet + Axis * 140.f + FVector(0.f, 0.f, 18.f), Feet + Axis * 160.f + FVector(0.f, 0.f, 12.f),
		Feet + Axis * 120.f + FVector(-Axis.Y, Axis.X, 0.f) * 30.f, Feet + Axis * 120.f - FVector(-Axis.Y, Axis.X, 0.f) * 30.f };
	const CorpseLootRules::FRemainsBox Box = CorpseLootRules::FitBoxToBody(Bones);
	TestTrue(TEXT("Valid"), Box.bValid);
	for (const FVector& Bone : Bones)
	{
		TestTrue(*FString::Printf(TEXT("Bone %s inside"), *Bone.ToCompactString()), InsideRemainsBox(Box, Bone));
	}
	// The long side runs along the body (45 / -135 deg), not across it; the old crate (60 cm half length at the feet) missed the head.
	TestTrue(TEXT("Long axis along the body"), FMath::IsNearlyEqual(FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Box.Yaw - 45.f))), 0.f, 0.02f));
	TestTrue(TEXT("Length = 160 + 2 x padding"), FMath::IsNearlyEqual(Box.Extent.X, 80.f + CorpseLootRules::BodyPaddingCm, 1.f));
	TestTrue(TEXT("Head inside, ~1.6 m from the feet"), InsideRemainsBox(Box, Feet + Axis * 160.f));
	TestFalse(TEXT("Not 1 m past the head"), InsideRemainsBox(Box, Feet + Axis * 260.f));
	// Low body: the minimum half height, the bottom half a padding under the lowest bone.
	TestTrue(TEXT("Min half height"), FMath::IsNearlyEqual(Box.Extent.Z, CorpseLootRules::MinHalfHeightCm, 0.1f));
	TestTrue(TEXT("Bottom just under the body"), FMath::IsNearlyEqual(Box.Center.Z - Box.Extent.Z, 8.f - CorpseLootRules::BodyPaddingCm * 0.5f, 0.1f));
	return true;
}

CORPSE_LOOT_TEST(FCorpseLootFitEdgeTest, "BoxMinimumAndEmpty")
bool FCorpseLootFitEdgeTest::RunTest(const FString&)
{
	TestFalse(TEXT("No points: invalid"), CorpseLootRules::FitBoxToBody(TArray<FVector>()).bValid);
	const CorpseLootRules::FRemainsBox One = CorpseLootRules::FitBoxToBody(TArray<FVector>{ FVector(10.f, 20.f, 30.f) });
	TestTrue(TEXT("One point: valid"), One.bValid);
	TestTrue(TEXT("One point: minimum box"), One.Extent.Equals(FVector(CorpseLootRules::MinHalfLengthCm, CorpseLootRules::MinHalfWidthCm,
		CorpseLootRules::MinHalfHeightCm), 0.01f));
	TestTrue(TEXT("One point: centred on it"), FVector2D(One.Center).Equals(FVector2D(10.f, 20.f), 0.01f));
	return true;
}

CORPSE_LOOT_TEST(FCorpseLootBonesTest, "BodyBonesOnly")
bool FCorpseLootBonesTest::RunTest(const FString&)
{
	for (const TCHAR* Bone : { TEXT("pelvis"), TEXT("spine_03"), TEXT("head"), TEXT("hand_r"), TEXT("foot_l"), TEXT("calf_twist_01_l") })
	{
		TestTrue(*FString::Printf(TEXT("%s is a body bone"), Bone), CorpseLootRules::IsBodyBone(Bone));
	}
	for (const TCHAR* Bone : { TEXT("root"), TEXT("ik_hand_gun"), TEXT("ik_foot_root"), TEXT("VB hand_r_to_ik"), TEXT("attach_weapon"), TEXT("interaction"),
		TEXT("center_of_mass"), TEXT("") })
	{
		TestFalse(*FString::Printf(TEXT("'%s' is a helper"), Bone), CorpseLootRules::IsBodyBone(Bone));
	}
	return true;
}

CORPSE_LOOT_TEST(FCorpseLootContentsTest, "ContentsAreHisGear")
bool FCorpseLootContentsTest::RunTest(const FString&)
{
	CorpseLootRules::FCarriedGear Gear;
	Gear.Items.Add(ETransferItem::Medkit, 2);
	Gear.Items.Add(ETransferItem::Chocolate, 3);
	Gear.Items.Add(ETransferItem::RifleAmmo, 62); // clip 17 + reserve 45
	Gear.Items.Add(ETransferItem::PistolAmmo, 0);
	Gear.Items.Add(ETransferItem::Mine, 1);
	Gear.Grenades = 2;
	Gear.WeaponId = TEXT("m16");
	const FLootContents Contents = CorpseLootRules::BuildRemainsContents(Gear);
	TestEqual(TEXT("Medkits"), Contents.GetCount(ELootItem::Medkit), 2);
	TestEqual(TEXT("Chocolate"), Contents.GetCount(ELootItem::Chocolate), 3);
	TestEqual(TEXT("M16 rounds"), Contents.GetCount(ELootItem::RifleAmmo), 62);
	TestEqual(TEXT("Mine"), Contents.GetCount(ELootItem::Mine), 1);
	TestEqual(TEXT("Grenades (Godot get_items_list)"), Contents.GetCount(ELootItem::Grenade), 2);
	TestEqual(TEXT("His weapon (Godot get_items_list)"), Contents.BonusWeaponId, FString(TEXT("m16")));
	// No supply-crate defaults (a default crate holds 60 M16 rounds, 24 pistol rounds, a heat pack...).
	TestEqual(TEXT("No pistol rounds"), Contents.GetCount(ELootItem::PistolAmmo), 0);
	TestEqual(TEXT("No heat pack"), Contents.GetCount(ELootItem::HeatPack), 0);
	TestEqual(TEXT("No shotgun shells"), Contents.GetCount(ELootItem::ShotgunAmmo), 0);
	TestEqual(TEXT("Six stacks: medkit, chocolate, M16, mine, grenades, weapon"), Contents.GetItems().Num(), 6);
	const TArray<FLootEntry> Items = Contents.GetItems();
	TestTrue(TEXT("Grenades before the weapon"), Items.Num() == 6 && Items[4].Item == ELootItem::Grenade && Items[5].Item == ELootItem::BonusWeapon);
	FLootContents Taken = Contents;
	TestEqual(TEXT("Take the grenades"), Taken.Take(ELootItem::Grenade), 2);
	TestEqual(TEXT("Grenades gone"), Taken.GetCount(ELootItem::Grenade), 0);
	// The knife / a grenade in hand is no weapon item; nothing carried = empty remains.
	TestFalse(TEXT("Knife"), CorpseLootRules::IsLootableWeapon(TEXT("knife")));
	TestFalse(TEXT("Grenade"), CorpseLootRules::IsLootableWeapon(TEXT("grenade")));
	TestTrue(TEXT("Pistol"), CorpseLootRules::IsLootableWeapon(TEXT("pistol")));
	TestTrue(TEXT("Nothing carried: empty"), CorpseLootRules::BuildRemainsContents(CorpseLootRules::FCarriedGear()).IsEmpty());
	return true;
}

CORPSE_LOOT_TEST(FCorpseLootTextTest, "NameAndPrompt")
bool FCorpseLootTextTest::RunTest(const FString&)
{
	const FText Name = FText::FromString(TEXT("Medic-Sapper"));
	TestEqual(TEXT("Crate name"), CorpseLootRules::GetRemainsName(Name).ToString(), FString(TEXT("Remains: Medic-Sapper")));
	TestEqual(TEXT("Prompt"), CorpseLootRules::GetSearchPrompt(Name, false), FString(TEXT("[Click] Search the body: Medic-Sapper")));
	TestEqual(TEXT("Searched"), CorpseLootRules::GetSearchPrompt(Name, true), FString(TEXT("[Searched] Body: Medic-Sapper (empty)")));
	return true;
}

#undef CORPSE_LOOT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
