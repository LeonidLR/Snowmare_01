// CodexTactics.Weapons.Armour.* / CodexTactics.Weapons.Ballistics.* - weapons framework step 2: armour classes and the class rule,
// damage falloff, shotgun pellets, per-shell reload, full-auto recoil (game designer decisions 2026-11-08;
// docs/design/WEAPONS_FRAMEWORK.md sections 11-12).

#include "Characters/EnemyAIRules.h"
#include "Combat/ArmorRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponBallisticsRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponStats.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace WeaponBallisticsTest
{
	/** The pre-class armour formula of UHealthComponent::TakeDamage (commit 617a874b), verbatim: the parity oracle. */
	float LegacyReduction(float Base, bool bShredded, float Penetration)
	{
		float EffectiveArmor = Base;
		if (bShredded)
		{
			EffectiveArmor = FMath::Max(0.0f, EffectiveArmor * 0.3f);
		}
		EffectiveArmor *= (1.0f - FMath::Clamp(Penetration, 0.0f, 1.0f));
		return EffectiveArmor;
	}

	UWeaponDataAsset* LoadTunedWeapon(const TCHAR* Id)
	{
		return LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), Id, Id));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponArmourClassRuleTest, "CodexTactics.Weapons.Armour.ClassRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponArmourClassRuleTest::RunTest(const FString&)
{
	// Decision 1: the penetration counts when the weapon's class is at least the body's.
	const EArmorClass Classes[] = { EArmorClass::None, EArmorClass::Light, EArmorClass::Medium, EArmorClass::Heavy, EArmorClass::Vehicle };
	for (const EArmorClass Weapon : Classes)
	{
		for (const EArmorClass Body : Classes)
		{
			TestEqual(FString::Printf(TEXT("weapon %d vs body %d"), static_cast<int32>(Weapon), static_cast<int32>(Body)),
				ArmorRules::PenetrationApplies(Weapon, Body), static_cast<uint8>(Weapon) >= static_cast<uint8>(Body));
		}
	}
	TestTrue(TEXT("tier -> class"), ArmorRules::ClassFromTier(EArmorTier::Light) == EArmorClass::Light
		&& ArmorRules::ClassFromTier(EArmorTier::Medium) == EArmorClass::Medium && ArmorRules::ClassFromTier(EArmorTier::Heavy) == EArmorClass::Heavy);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponArmourFormulaParityTest, "CodexTactics.Weapons.Armour.FormulaParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponArmourFormulaParityTest::RunTest(const FString&)
{
	// When the class rule passes, the reduction is bit-identical to the pre-class formula; when it fails, it is the body's full reduction.
	int32 Compared = 0;
	for (const float Base : { 0.f, 0.05f, 0.1f, 0.15f, 0.4f, 0.75f, 0.9f })
	{
		for (const float Pen : { 0.f, 0.1f, 0.15f, 0.2f, 0.3f, 0.5f, 0.6f, 0.65f, 1.f, 1.5f })
		{
			for (const bool bShred : { false, true })
			{
				const float Old = WeaponBallisticsTest::LegacyReduction(Base, bShred, Pen);
				TestTrue(TEXT("rule passes: identical"), ArmorRules::EffectiveReduction(Base, bShred, Pen, EArmorClass::Heavy, EArmorClass::Medium) == Old);
				TestTrue(TEXT("default spec (no class limit): identical"), ArmorRules::EffectiveReduction(Base, bShred, Pen, EArmorClass::Vehicle, EArmorClass::Heavy) == Old);
				const float Full = bShred ? FMath::Max(0.f, Base * 0.3f) : Base;
				TestTrue(TEXT("rule fails: full reduction"), ArmorRules::EffectiveReduction(Base, bShred, Pen, EArmorClass::Light, EArmorClass::Heavy) == Full);
				Compared += 3;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d armour cases compared"), Compared));

	// Through the health component: 100 damage, kinetic (x1), a heavy 0.75 body.
	UHealthComponent* Brute = NewObject<UHealthComponent>();
	Brute->SetMaxHealth(1000.f);
	Brute->BaseArmorReduction = 0.75f;
	Brute->SetArmorClass(EArmorClass::Heavy);
	FDamageSpec Shot;
	Shot.Amount = 100.f;
	Shot.ArmorPenetration = 0.2f;
	Shot.PenetrationClass = EArmorClass::Medium; // the M16
	TestEqual(TEXT("M16 on the brute: 25 (was 40)"), Brute->TakeDamage(Shot), 25.f, 0.001f);
	Shot.PenetrationClass = EArmorClass::Heavy; // a heavy weapon with the same penetration
	TestEqual(TEXT("a heavy-class weapon: 40 as before"), Brute->TakeDamage(Shot), 40.f, 0.001f);
	FDamageSpec Claw; // a non-weapon source (default spec): no class limit
	Claw.Amount = 100.f;
	Claw.ArmorPenetration = 0.2f;
	TestEqual(TEXT("non-weapon damage unchanged: 40"), Brute->TakeDamage(Claw), 40.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponArmourBodiesTest, "CodexTactics.Weapons.Armour.BodiesFromFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponArmourBodiesTest::RunTest(const FString&)
{
	// The project file's "armor.bodies": every body, its reduction = the C++ value it replaces (no reduction changed), its class = the tier.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	TestEqual(TEXT("nine bodies"), WeaponTuning::GetLastReport().ArmorBodies, 9);
	TestEqual(TEXT("no armour errors"), WeaponTuning::GetLastReport().ArmorErrors.Num(), 0);
	struct FExpectedBody
	{
		EEnemyArchetype Archetype;
		EArmorClass Class;
	};
	const FExpectedBody Enemies[] = {
		{ EEnemyArchetype::Base, EArmorClass::Light }, { EEnemyArchetype::FrostHound, EArmorClass::Light },
		{ EEnemyArchetype::Frostbitten, EArmorClass::Light }, { EEnemyArchetype::Cutter, EArmorClass::Light },
		{ EEnemyArchetype::Spitter, EArmorClass::Medium }, { EEnemyArchetype::CryoDrone, EArmorClass::Medium },
		{ EEnemyArchetype::Marksman, EArmorClass::Medium }, { EEnemyArchetype::Brute, EArmorClass::Heavy },
	};
	for (const FExpectedBody& Expected : Enemies)
	{
		const FString Key = ArmorRules::BodyKey(Expected.Archetype);
		FBodyArmor Body{ EArmorClass::None, -1.f };
		TestTrue(FString::Printf(TEXT("%s listed"), *Key), ArmorRules::ApplyBody(Key, Body));
		TestTrue(FString::Printf(TEXT("%s: class = its tier"), *Key), Body.Class == Expected.Class);
		TestEqual(FString::Printf(TEXT("%s: reduction unchanged"), *Key), Body.Reduction, EnemyAIRules::GetBaseArmor(Expected.Archetype));
	}
	FBodyArmor Operative{ EArmorClass::None, -1.f };
	TestTrue(TEXT("operative listed"), ArmorRules::ApplyBody(ArmorRules::OperativeBodyKey, Operative));
	TestTrue(TEXT("operative: light 0.10 (the constructor's value)"), Operative.Class == EArmorClass::Light && Operative.Reduction == 0.10f);

	// Bad entries are skipped and named.
	TSharedPtr<FJsonObject> Bad;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"bodies\": {\"a\": {\"class\": \"titanium\", \"reduction\": 0.2}, \"b\": {\"class\": \"heavy\", \"reduction\": 1.2}, \"c\": {\"class\": \"heavy\", \"reduction\": \"0.5\"}, \"d\": {\"class\": \"medium\", \"reduction\": 0.3}}}")), Bad);
	TMap<FString, FBodyArmor> Bodies;
	TArray<FString> Errors;
	TestEqual(TEXT("one good body"), Bad.IsValid() ? ArmorRules::ParseBodies(*Bad, Bodies, &Errors) : -1, 1);
	TestEqual(TEXT("three errors"), Errors.Num(), 3);
	TestTrue(TEXT("the good one kept"), Bodies.Contains(TEXT("d")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponBallisticsFalloffTest, "CodexTactics.Weapons.Ballistics.DamageFalloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponBallisticsFalloffTest::RunTest(const FString&)
{
	using WeaponBallisticsRules::DamageFalloffMultiplier;
	// Decision 4: x1 to the effective range, linear to the floor at the max reach.
	TestEqual(TEXT("pistol 10 m"), DamageFalloffMultiplier(1000.f, 1500.f, 2500.f, 0.6f), 1.f);
	TestEqual(TEXT("pistol 15 m"), DamageFalloffMultiplier(1500.f, 1500.f, 2500.f, 0.6f), 1.f);
	TestEqual(TEXT("pistol 20 m"), DamageFalloffMultiplier(2000.f, 1500.f, 2500.f, 0.6f), 0.8f, 0.0001f);
	TestEqual(TEXT("pistol 25 m"), DamageFalloffMultiplier(2500.f, 1500.f, 2500.f, 0.6f), 0.6f, 0.0001f);
	TestEqual(TEXT("shotgun 11.5 m"), DamageFalloffMultiplier(1150.f, 800.f, 1500.f, 0.4f), 0.7f, 0.0001f);
	TestEqual(TEXT("shotgun 15 m"), DamageFalloffMultiplier(1500.f, 800.f, 1500.f, 0.4f), 0.4f, 0.0001f);
	TestEqual(TEXT("beyond the max: the floor"), DamageFalloffMultiplier(3000.f, 800.f, 1500.f, 0.4f), 0.4f, 0.0001f);
	TestEqual(TEXT("floor 1 = none"), DamageFalloffMultiplier(5900.f, 2800.f, 6000.f, 1.f), 1.f);

	// The project weapons (the file in force): only the shotgun and the pistol fall off.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	for (const TCHAR* Id : { TEXT("m16"), TEXT("sniper_rifle"), TEXT("plasma_carbine"), TEXT("flamethrower"), TEXT("cryo_emitter"), TEXT("knife") })
	{
		const UWeaponDataAsset* Weapon = WeaponBallisticsTest::LoadTunedWeapon(Id);
		if (TestNotNull(Id, Weapon))
		{
			TestEqual(FString::Printf(TEXT("%s: no falloff at its max reach"), Id), DamageFalloffMultiplier(Weapon->Stats, Weapon->Stats.Range.MaxRangeCm), 1.f);
		}
	}
	const UWeaponDataAsset* Pistol = WeaponBallisticsTest::LoadTunedWeapon(TEXT("pistol"));
	const UWeaponDataAsset* Shotgun = WeaponBallisticsTest::LoadTunedWeapon(TEXT("shotgun"));
	if (TestTrue(TEXT("pistol + shotgun"), Pistol && Shotgun))
	{
		TestEqual(TEXT("pistol at 25 m x0.6"), DamageFalloffMultiplier(Pistol->Stats, 2500.f), 0.6f, 0.0001f);
		TestEqual(TEXT("shotgun at 15 m x0.4"), DamageFalloffMultiplier(Shotgun->Stats, 1500.f), 0.4f, 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponBallisticsPelletsTest, "CodexTactics.Weapons.Ballistics.Pellets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponBallisticsPelletsTest::RunTest(const FString&)
{
	using namespace WeaponBallisticsRules;
	// One pellet = the old single roll: exactly one random number, the same comparison.
	int32 Calls = 0;
	TestEqual(TEXT("single hit at 0.5 <= 0.6"), RollPelletHits(1, 0.6f, [&Calls]() { ++Calls; return 0.5f; }), 1);
	TestEqual(TEXT("one random number"), Calls, 1);
	TestEqual(TEXT("single miss at 0.7 > 0.6"), RollPelletHits(1, 0.6f, []() { return 0.7f; }), 0);
	Calls = 0;
	TestEqual(TEXT("forced hit draws nothing"), RollPelletHits(1, 0.f, [&Calls]() { ++Calls; return 0.f; }, true), 1);
	TestEqual(TEXT("forced: no random number"), Calls, 0);

	// Eight pellets roll each: a fixed sequence 0.1, 0.2 .. 0.8 against 0.45 -> 4 hit.
	float Next = 0.f;
	TestEqual(TEXT("8 pellets, 4 hit"), RollPelletHits(8, 0.45f, [&Next]() { Next += 0.1f; return Next; }), 4);
	TestEqual(TEXT("share 4 / 8"), PelletDamageShare(4, 8), 0.5f);
	TestEqual(TEXT("share 8 / 8"), PelletDamageShare(8, 8), 1.f);
	TestEqual(TEXT("share 1 / 1"), PelletDamageShare(1, 1), 1.f);
	// Decision 2: 45 over 8 pellets = 5.625 each.
	TestEqual(TEXT("one pellet of the shotgun"), 45.f * PelletDamageShare(1, 8), 5.625f);

	// The expected damage of a shot keeps 45 x chance (the average is unchanged, the spread of outcomes narrows).
	FRandomStream Stream(1234);
	double Sum = 0.0;
	const int32 Shots = 20000;
	for (int32 Index = 0; Index < Shots; ++Index)
	{
		Sum += 45.0 * PelletDamageShare(RollPelletHits(8, 0.55f, [&Stream]() { return Stream.FRand(); }), 8);
	}
	TestEqual(TEXT("mean damage at 55 %: 45 x 0.55"), static_cast<float>(Sum / Shots), 45.f * 0.55f, 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponBallisticsReloadTest, "CodexTactics.Weapons.Ballistics.PerShellReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponBallisticsReloadTest::RunTest(const FString&)
{
	using namespace WeaponBallisticsRules;
	FWeaponAmmoStats Rifle; // magazine 30, 2.68 s
	TestFalse(TEXT("rifle: magazine reload"), ReloadsPerRound(Rifle));
	TestEqual(TEXT("rifle: one step = the whole reload"), ReloadStepSeconds(Rifle), 2.68f);
	TestEqual(TEXT("rifle: 5 left, 100 reserve -> 25 in"), ReloadStepRounds(Rifle, 5, 100), 25);
	TestEqual(TEXT("rifle: 5 left, 10 reserve -> 10 in"), ReloadStepRounds(Rifle, 5, 10), 10);
	TestFalse(TEXT("rifle: one step only"), ContinueReload(Rifle, 15, 100));
	TestFalse(TEXT("rifle: never interrupted"), InterruptReload(Rifle, 15, true));

	FWeaponAmmoStats Shotgun;
	Shotgun.MagazineSize = 8;
	Shotgun.ReloadSeconds = 2.4f;
	Shotgun.ReloadMode = EWeaponReloadMode::PerRound;
	Shotgun.PerRoundReloadSeconds = 0.5f;
	TestEqual(TEXT("shotgun: 0.5 s a shell"), ReloadStepSeconds(Shotgun), 0.5f);
	TestEqual(TEXT("shotgun: one shell a step"), ReloadStepRounds(Shotgun, 0, 40), 1);
	TestTrue(TEXT("shotgun: carries on"), ContinueReload(Shotgun, 3, 40));
	TestFalse(TEXT("shotgun: stops full"), ContinueReload(Shotgun, 8, 40));
	TestFalse(TEXT("shotgun: stops out of reserve"), ContinueReload(Shotgun, 3, 0));
	TestTrue(TEXT("shotgun: fires once a shell is in"), InterruptReload(Shotgun, 1, true));
	TestFalse(TEXT("shotgun: no target, keeps loading"), InterruptReload(Shotgun, 1, false));
	TestFalse(TEXT("shotgun: empty, keeps loading"), InterruptReload(Shotgun, 0, true));
	Shotgun.PerRoundReloadSeconds = 0.f;
	TestEqual(TEXT("shotgun without a per-shell time: 2.4 / 8"), ReloadStepSeconds(Shotgun), 0.3f, 0.0001f);

	// The project's shotgun: 8 shells from empty = 4 s (was one 2.4 s reload).
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	if (const UWeaponDataAsset* Project = WeaponBallisticsTest::LoadTunedWeapon(TEXT("shotgun")); TestNotNull(TEXT("shotgun"), Project))
	{
		TestTrue(TEXT("per shell"), ReloadsPerRound(Project->Stats.Ammo));
		TestEqual(TEXT("empty -> full 4.0 s"), ReloadStepSeconds(Project->Stats.Ammo) * Project->Stats.Ammo.MagazineSize, 4.f, 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponBallisticsRecoilTest, "CodexTactics.Weapons.Ballistics.Recoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponBallisticsRecoilTest::RunTest(const FString&)
{
	using WeaponBallisticsRules::SettleRecoil;
	// Decision 5: +0.3 deg a round, one round settles in 0.5 s (0.6 deg / s).
	TestEqual(TEXT("0.1 s after one round"), SettleRecoil(0.3f, 0.1f, 0.3f, 0.5f), 0.24f, 0.0001f);
	TestEqual(TEXT("0.5 s after one round: gone"), SettleRecoil(0.3f, 0.5f, 0.3f, 0.5f), 0.f, 0.0001f);
	TestEqual(TEXT("no recoil data: nothing"), SettleRecoil(2.f, 0.f, 0.f, 0.f), 0.f);
	// A 10-round full-auto burst at 0.1 s: the bloom before each round, the last one = 9 x 0.24 = 2.16 deg extra spread.
	float Bloom = 0.f;
	for (int32 Round = 0; Round < 10; ++Round)
	{
		Bloom = SettleRecoil(Bloom, 0.1f, 0.3f, 0.5f);
		if (Round == 9)
		{
			TestEqual(TEXT("10th round of a burst: +2.16 deg"), Bloom, 2.16f, 0.001f);
		}
		Bloom += 0.3f;
	}
	TestEqual(TEXT("settled 4.1 s after the burst (2.46 deg at 0.6 deg / s)"), SettleRecoil(Bloom, 4.1f, 0.3f, 0.5f), 0.f, 0.0001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
