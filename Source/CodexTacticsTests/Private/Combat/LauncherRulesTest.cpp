// CodexTactics.Weapons.Rpg.* - the RPG-7 on the shared projectile + explosion layer (2026-11-09; docs/systems/weapons.md "The RPG-7"): its data
// (a flat rocket arming at 10 m, class vehicle, one rocket, the back-blast cone), the stance rule (no prone), the back-blast check (a wall right
// behind, a closed room), the back-blast's damage on a body behind, the shot's refusals in order, the miss scatter; the roster keeps it "next"
// and out of every loadout.

#include "Characters/OperativeMovementRules.h"
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
	TestEqual(TEXT("arms after 10 m"), Stats.Projectile.ArmingDistanceCm, 1000.f);
	TestTrue(TEXT("a max flight time"), Stats.Projectile.MaxFlightSeconds > 0.f);
	TestTrue(TEXT("explosive"), Stats.Damage.DamageType == EDamageType::Explosive);
	TestTrue(TEXT("class vehicle (it hurts hulls; decision 1)"), Stats.Damage.PenetrationClass == EArmorClass::Vehicle);
	TestEqual(TEXT("its penetration applies to a vehicle body"), ArmorRules::EffectiveReduction(0.5f, false, Stats.Damage.ArmorPenetration,
		Stats.Damage.PenetrationClass, EArmorClass::Vehicle), 0.5f * (1.f - Stats.Damage.ArmorPenetration), 1e-5f);
	TestTrue(TEXT("a blast with a full-damage core"), Stats.Explosion.RadiusCm > 0.f && Stats.Explosion.FullDamageRadiusCm > 0.f);
	TestTrue(TEXT("a direct-hit share"), Stats.Explosion.DirectHitDamage > 0.f);
	TestEqual(TEXT("one rocket in the tube"), Stats.Ammo.MagazineSize, 1);
	TestTrue(TEXT("a reload per rocket"), Stats.Ammo.ReloadSeconds > 0.f);
	TestTrue(TEXT("a back-blast cone behind"), Stats.Explosion.BackBlastLengthCm > 0.f && Stats.Explosion.BackBlastHalfAngleDeg > 0.f
		&& Stats.Explosion.BackBlastDamage > 0.f);
	TestTrue(TEXT("a wall within the clearance refuses the shot"), Stats.Explosion.BackBlastClearanceCm > 0.f && Stats.Explosion.bBackBlastRefuses);
	TestEqual(TEXT("no target closer than the arming distance"), LauncherRules::MinimumRangeCm(Stats), 1000.f);

	// The roster: "next", in no loadout yet (any fighter can be the grenadier: the designer decides later).
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
		for (const TPair<FString, TArray<FString>>& Role : WeaponLoadoutRules::Parse(**LoadoutBlock, &LoadoutErrors))
		{
			TestFalse(FString::Printf(TEXT("the %s's loadout has no rpg7 yet"), *Role.Key), Role.Value.Contains(TEXT("rpg7")));
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
	Stats.Projectile.ArmingDistanceCm = 1000.f;
	Stats.Explosion.BackBlastLengthCm = 800.f;
	Stats.Explosion.BackBlastHalfAngleDeg = 30.f;
	Stats.Explosion.BackBlastDamage = 60.f;
	Stats.Explosion.BackBlastClearanceCm = 200.f;
	Stats.Explosion.bBackBlastRefuses = true;

	// Stances.
	TestTrue(TEXT("standing: fires"), LauncherRules::StanceAllows(EOperativeStance::Standing));
	TestTrue(TEXT("kneeling: fires"), LauncherRules::StanceAllows(EOperativeStance::Crouching));
	TestFalse(TEXT("prone: never"), LauncherRules::StanceAllows(EOperativeStance::Prone));

	// The back-blast check.
	const FWeaponExplosionStats& Blast = Stats.Explosion;
	TestTrue(TEXT("nothing behind: clear"), LauncherRules::ClassifyBackblast(-1.f, false, Blast) == ELauncherBackblast::Clear);
	TestTrue(TEXT("a wall 1 m behind: blocked"), LauncherRules::ClassifyBackblast(100.f, false, Blast) == ELauncherBackblast::WallBehind);
	TestTrue(TEXT("a wall 1 m behind under a roof: still the wall"), LauncherRules::ClassifyBackblast(100.f, true, Blast) == ELauncherBackblast::WallBehind);
	TestTrue(TEXT("a wall 5 m behind in the open: clear"), LauncherRules::ClassifyBackblast(500.f, false, Blast) == ELauncherBackblast::Clear);
	TestTrue(TEXT("a wall 5 m behind under a roof: a closed room"), LauncherRules::ClassifyBackblast(500.f, true, Blast) == ELauncherBackblast::ClosedRoom);
	TestTrue(TEXT("a roof and nothing behind: clear (a porch)"), LauncherRules::ClassifyBackblast(-1.f, true, Blast) == ELauncherBackblast::Clear);
	FWeaponExplosionStats NoBlast;
	TestTrue(TEXT("no back-blast: always clear"), LauncherRules::ClassifyBackblast(50.f, true, NoBlast) == ELauncherBackblast::Clear);

	// The back-blast's damage behind him.
	const FVector Launcher(0.f, 0.f, 100.f);
	const FVector Aim(1.f, 0.f, 0.f);
	TestEqual(TEXT("a mate 2 m right behind: 75 %"), LauncherRules::BackblastDamageAt(Blast, Launcher, Aim, Launcher - FVector(200.f, 0.f, 0.f)), 45.f, 0.01f);
	TestEqual(TEXT("a mate 6 m behind: 25 %"), LauncherRules::BackblastDamageAt(Blast, Launcher, Aim, Launcher - FVector(600.f, 0.f, 0.f)), 15.f, 0.01f);
	TestEqual(TEXT("a mate beside him: nothing"), LauncherRules::BackblastDamageAt(Blast, Launcher, Aim, Launcher + FVector(0.f, 200.f, 0.f)), 0.f);
	TestEqual(TEXT("a mate in front: nothing"), LauncherRules::BackblastDamageAt(Blast, Launcher, Aim, Launcher + FVector(300.f, 0.f, 0.f)), 0.f);
	TestEqual(TEXT("a mate 9 m behind: nothing"), LauncherRules::BackblastDamageAt(Blast, Launcher, Aim, Launcher - FVector(900.f, 0.f, 0.f)), 0.f);

	// The refusals, in the player's order.
	using ELR = ELauncherRefusal;
	TestTrue(TEXT("empty first"), LauncherRules::Refusal(false, EOperativeStance::Prone, 50.f, ELauncherBackblast::WallBehind, Stats) == ELR::Empty);
	TestTrue(TEXT("then prone"), LauncherRules::Refusal(true, EOperativeStance::Prone, 50.f, ELauncherBackblast::WallBehind, Stats) == ELR::Prone);
	TestTrue(TEXT("then too close"), LauncherRules::Refusal(true, EOperativeStance::Standing, 900.f, ELauncherBackblast::WallBehind, Stats) == ELR::TooClose);
	TestTrue(TEXT("then the back-blast"), LauncherRules::Refusal(true, EOperativeStance::Crouching, 3000.f, ELauncherBackblast::ClosedRoom, Stats) == ELR::Backblast);
	TestTrue(TEXT("all clear: it fires"), LauncherRules::Refusal(true, EOperativeStance::Standing, 3000.f, ELauncherBackblast::Clear, Stats) == ELR::None);
	Stats.Explosion.bBackBlastRefuses = false;
	TestTrue(TEXT("back_blast_refuses false: a warning only, it fires"),
		LauncherRules::Refusal(true, EOperativeStance::Standing, 3000.f, ELauncherBackblast::WallBehind, Stats) == ELR::None);
	TestFalse(TEXT("a refusal has a warning text"), LauncherRules::RefusalText(ELR::Backblast, ELauncherBackblast::WallBehind).IsEmpty());

	// The miss scatter: off the aim point by 1.5 .. 3.5 m, both sides.
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
