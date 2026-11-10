// CodexTactics.Weapons.Rpg.* - the RPG-7 on the shared projectile + explosion layer (2026-11-09, the game designer's numbers the same day;
// docs/systems/weapons.md "The RPG-7"): its data (a flat 115 m/s rocket armed at once, a direct hit 140 on the target struck, the splash 60 within
// 4 m, class vehicle, one rocket, no back-blast damage), the one refusal left (a target inside the minimum range - never inside its own blast),
// the damage split, the miss scatter; the roster keeps it "next"; only the Commander carries it (FOR TESTING, pending the designer, 2026-11-09).

#include "Combat/ArmorRules.h"
#include "Combat/LauncherRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponLoadoutRules.h"
#include "Data/WeaponRoster.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace LauncherRulesTest
{
	TSharedPtr<FJsonObject> LauncherProjectFile()
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

	TSharedPtr<FJsonObject> LauncherBlock(const TSharedPtr<FJsonObject>& Root, const TCHAR* Group, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* GroupObject = nullptr;
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Root.IsValid() && Root->TryGetObjectField(Group, GroupObject) && GroupObject && (*GroupObject)->TryGetObjectField(Key, Object) && Object
			? *Object : nullptr;
	}

	/** The RPG-7 as the project file builds it (null when the block is missing / rejected). */
	UWeaponDataAsset* ProjectRpg(FAutomationTestBase& Test)
	{
		const TSharedPtr<FJsonObject> Json = LauncherBlock(LauncherProjectFile(), TEXT("weapons"), TEXT("rpg7"));
		if (!Test.TestTrue(TEXT("weapons.rpg7 in weapons_tuning.json"), Json.IsValid()))
		{
			return nullptr;
		}
		UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
		Weapon->WeaponId = TEXT("rpg7");
		Weapon->Stats = Weapon->MakeBaseStats();
		TArray<FString> Errors;
		const bool bApplied = WeaponTuning::ApplyWeapon(*Json, *Weapon, &Errors);
		Test.TestTrue(FString::Printf(TEXT("the rpg7 block applies (%s)"), *FString::Join(Errors, TEXT("; "))), bApplied);
		return bApplied ? Weapon : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpgDataTest, "CodexTactics.Weapons.Rpg.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRpgDataTest::RunTest(const FString&)
{
	const UWeaponDataAsset* Rpg = LauncherRulesTest::ProjectRpg(*this);
	if (!Rpg)
	{
		return false;
	}
	const FWeaponStats& Stats = Rpg->Stats;
	TestTrue(TEXT("kind projectile"), Stats.Kind == EWeaponKind::Projectile);
	TestTrue(TEXT("handling launcher"), Stats.Handling == EWeaponHandling::Launcher);
	TestTrue(TEXT("a flat rocket"), Stats.Projectile.Trajectory == EProjectileTrajectory::Flat);
	TestTrue(TEXT("a real launch speed"), Stats.Projectile.SpeedCmS >= 5000.f);
	TestEqual(TEXT("115 m/s (the designer)"), Stats.Projectile.SpeedCmS, 11500.f);
	TestEqual(TEXT("the fuse arms at once (the designer: no safe flight)"), Stats.Projectile.ArmingDistanceCm, 0.f);
	TestTrue(TEXT("a max flight time"), Stats.Projectile.MaxFlightSeconds > 0.f);
	TestTrue(TEXT("explosive"), Stats.Damage.DamageType == EDamageType::Explosive);
	TestTrue(TEXT("class vehicle (it hurts hulls; decision 1)"), Stats.Damage.PenetrationClass == EArmorClass::Vehicle);
	TestEqual(TEXT("its penetration applies to a vehicle body"), ArmorRules::EffectiveReduction(0.5f, false, Stats.Damage.ArmorPenetration,
		Stats.Damage.PenetrationClass, EArmorClass::Vehicle), 0.5f * (1.f - Stats.Damage.ArmorPenetration), 1e-5f);
	TestTrue(TEXT("a blast with a full-damage core"), Stats.Explosion.RadiusCm > 0.f && Stats.Explosion.FullDamageRadiusCm > 0.f);
	TestEqual(TEXT("a direct hit = 140 on the target struck (the designer)"), Stats.Explosion.DirectHitDamage, 140.f);
	TestEqual(TEXT("the splash = 60 (the designer)"), Stats.Damage.BaseDamage, 60.f);
	TestEqual(TEXT("within the 4 m blast"), Stats.Explosion.RadiusCm, 400.f);
	TestEqual(TEXT("the struck target takes 140 in all (not 140 + the splash)"), LauncherRules::DamageOn(Stats, true, 0.f), 140.f);
	TestEqual(TEXT("a body at the burst point (not struck): the splash 60"), LauncherRules::DamageOn(Stats, false, 0.f), 60.f);
	TestTrue(TEXT("a body 3 m off: less than 60"), LauncherRules::DamageOn(Stats, false, 300.f) < 60.f && LauncherRules::DamageOn(Stats, false, 300.f) > 0.f);
	TestEqual(TEXT("a body 5 m off: nothing"), LauncherRules::DamageOn(Stats, false, 500.f), 0.f);
	TestEqual(TEXT("one rocket in the tube"), Stats.Ammo.MagazineSize, 1);
	TestTrue(TEXT("a reload per rocket"), Stats.Ammo.ReloadSeconds > 0.f);
	TestEqual(TEXT("no target closer than min_range_m 10"), LauncherRules::MinimumRangeCm(Stats), 1000.f);
	TestTrue(TEXT("the minimum range keeps the shooter out of his own blast"), LauncherRules::MinimumRangeCm(Stats) > Stats.Explosion.RadiusCm);

	// The roster: "next"; the loadouts: only the Commander (the game designer 2026-11-09).
	const TSharedPtr<FJsonObject> Root = LauncherRulesTest::LauncherProjectFile();
	const TSharedPtr<FJsonObject>* RosterBlock = nullptr;
	FWeaponRoster Roster;
	if (TestTrue(TEXT("roster block"), Root.IsValid() && Root->TryGetObjectField(TEXT("roster"), RosterBlock) && RosterBlock))
	{
		WeaponRoster::Parse(**RosterBlock, Roster);
	}
	TestTrue(TEXT("rpg7 is in the roster's next list"), Roster.Next.Contains(TEXT("rpg7")));
	TestFalse(TEXT("rpg7 is not out of play"), Roster.Out.Contains(TEXT("rpg7")));
	TArray<FString> LoadoutErrors;
	const TSharedPtr<FJsonObject>* LoadoutBlock = nullptr;
	if (TestTrue(TEXT("loadouts block"), Root.IsValid() && Root->TryGetObjectField(TEXT("loadouts"), LoadoutBlock) && LoadoutBlock))
	{
		// The game designer 2026-11-09: only the Commander carries it in our squad (PMC squads: pmc_operators.json rpg_gunner).
		for (const TPair<FString, TArray<FString>>& Role : WeaponLoadoutRules::Parse(**LoadoutBlock, &LoadoutErrors))
		{
			TestEqual(FString::Printf(TEXT("the %s's loadout: rpg7 only for the Commander (the designer)"), *Role.Key), Role.Value.Contains(TEXT("rpg7")),
				Role.Key == TEXT("commander"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpgRulesTest, "CodexTactics.Weapons.Rpg.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRpgRulesTest::RunTest(const FString&)
{
	FWeaponStats Stats;
	Stats.Kind = EWeaponKind::Projectile;
	Stats.Handling = EWeaponHandling::Launcher;
	Stats.Range.MinRangeCm = 1000.f;
	Stats.Explosion.RadiusCm = 400.f;

	// The refusals left (the designer 2026-11-09): empty, then a target too close - nothing about stance or walls behind.
	using ELR = ELauncherRefusal;
	TestTrue(TEXT("empty first"), LauncherRules::Refusal(false, 50.f, Stats) == ELR::Empty);
	TestTrue(TEXT("a target 9 m away: too close"), LauncherRules::Refusal(true, 900.f, Stats) == ELR::TooClose);
	TestTrue(TEXT("a target 10 m away: fires"), LauncherRules::Refusal(true, 1000.f, Stats) == ELR::None);
	TestTrue(TEXT("a target 30 m away: fires"), LauncherRules::Refusal(true, 3000.f, Stats) == ELR::None);
	TestFalse(TEXT("a refusal has a warning text"), LauncherRules::RefusalText(ELR::TooClose).IsEmpty());

	// The minimum range is never inside the rocket's own blast, whatever min_range_m says.
	Stats.Range.MinRangeCm = 0.f;
	TestEqual(TEXT("min_range_m 0: the blast radius is the minimum"), LauncherRules::MinimumRangeCm(Stats), 400.f);
	TestTrue(TEXT("... a target 3 m away refused"), LauncherRules::Refusal(true, 300.f, Stats) == ELR::TooClose);

	// The miss scatter: off the aim point by 1.5 .. 3.5 m, both sides.
	const FVector Aim(1.f, 0.f, 0.f);
	const FVector AimPoint(5000.f, 0.f, 100.f);
	for (const float Roll : { 0.f, 0.3f, 0.7f, 0.999f })
	{
		for (const float Side : { 0.2f, 0.8f })
		{
			const float Off = static_cast<float>(FVector::Dist(LauncherRules::MissPoint(AimPoint, Aim, Roll, Side), AimPoint));
			TestTrue(FString::Printf(TEXT("miss %.2f / %.1f: %.0f cm off"), Roll, Side, Off), Off >= LauncherRules::MinScatterCm - 1.f && Off <= LauncherRules::MaxScatterCm + 1.f);
		}
	}
	TestTrue(TEXT("misses fall on both sides"), LauncherRules::MissPoint(AimPoint, Aim, 0.5f, 0.2f).Y * LauncherRules::MissPoint(AimPoint, Aim, 0.5f, 0.8f).Y < 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRpgVisualTest, "CodexTactics.Weapons.Rpg.Visual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRpgVisualTest::RunTest(const FString&)
{
	// The launcher pack model (2026-11-09): the optimized body + optic on the hand, the loaded rocket, the flying rocket nose first, the effect
	// hooks EMPTY by default (the designer drops his Niagara systems in by data).
	const TSharedPtr<FJsonObject> Json = LauncherRulesTest::LauncherBlock(LauncherRulesTest::LauncherProjectFile(), TEXT("weapons"), TEXT("rpg7"));
	if (!TestTrue(TEXT("weapons.rpg7"), Json.IsValid()))
	{
		return false;
	}
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->WeaponId = TEXT("rpg7");
	TestTrue(TEXT("the visual applies"), WeaponTuning::ApplyWeaponPresentation(*Json, *Weapon));
	TestEqual(TEXT("the body model"), Weapon->HandMesh.ToSoftObjectPath().GetAssetName(), FString(TEXT("SM_RPG7_Body")));
	TestTrue(TEXT("the optic on it"), Weapon->HandMeshParts.Num() == 1 && Weapon->HandMeshParts[0].ToSoftObjectPath().GetAssetName() == TEXT("SM_RPG7_Sight01"));
	TestEqual(TEXT("the loaded rocket"), Weapon->LoadedRoundMesh.ToSoftObjectPath().GetAssetName(), FString(TEXT("SM_RPG7_Rocket")));
	TestEqual(TEXT("the flying rocket"), Weapon->ProjectileMesh.ToSoftObjectPath().GetAssetName(), FString(TEXT("SM_RPG7_Rocket")));
	// The model's +Y (its muzzle) turned onto the flight's +X.
	const FVector Nose = Weapon->ProjectileMeshTransform.TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
	TestTrue(TEXT("the rocket flies nose first (+Y of the model = the flight)"), Nose.Equals(FVector(1.f, 0.f, 0.f), 0.01f));
	TestTrue(TEXT("the muzzle at the tube's front (+Y)"), Weapon->HandMeshMuzzleOffset.Y > 30.f);
	TestTrue(TEXT("the back-blast at its rear (-Y)"), Weapon->BackblastOffset.Y < -60.f);
	TestTrue(TEXT("the effect hooks are empty by default"), Weapon->MuzzleFlashFx.IsNull() && Weapon->BackblastFx.IsNull() && Weapon->TrailFx.IsNull());
	TestTrue(TEXT("its own attach transform"), Weapon->bOverrideHandMeshTransform);
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
