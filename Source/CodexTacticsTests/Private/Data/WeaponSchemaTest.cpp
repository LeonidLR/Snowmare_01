// CodexTactics.Weapons.Schema.* - the unified weapon schema (weapons framework step 1, week plan 2026-11-08;
// docs/design/WEAPONS_FRAMEWORK.md): every weapon loads, the numbers equal the pre-schema pipeline (parity), bad data is rejected.

#include "Data/WeaponDataAsset.h"
#include "Data/WeaponStatsRules.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace WeaponSchemaTest
{
	/**
	 * The weapons of weapons_tuning.json (DA_Weapon_<id>) and their kinds: the designer's roster (2026-11-08) - in play the M16 and the
	 * grenade, next the shotgun, the pistol, the sniper rifle (their data stays). Out of play (not in the file): OutOfPlay.
	 */
	struct FExpectedWeapon
	{
		const TCHAR* Id;
		EWeaponKind Kind;
	};

	static const FExpectedWeapon Weapons[] = {
		{ TEXT("grenade"), EWeaponKind::Thrown },
		{ TEXT("m16"), EWeaponKind::Hitscan },
		{ TEXT("pistol"), EWeaponKind::Hitscan },
		{ TEXT("shotgun"), EWeaponKind::Hitscan },
		{ TEXT("sniper_rifle"), EWeaponKind::Hitscan },
	};

	/** The designer removed these (2026-11-08); their assets stay on disk, unreferenced. */
	static const TCHAR* const OutOfPlay[] = { TEXT("hammer"), TEXT("flamethrower"), TEXT("knife"), TEXT("cryo_emitter"), TEXT("plasma_carbine") };

	UWeaponDataAsset* LoadWeapon(const FString& Id)
	{
		return LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), *Id, *Id));
	}

	TSharedPtr<FJsonObject> ParseWeaponJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}

	/** The project file's "weapons" block. */
	TSharedPtr<FJsonObject> LoadProjectWeapons()
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *WeaponTuning::GetDefaultPath()))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Root = ParseWeaponJson(Text);
		const TSharedPtr<FJsonObject>* Block = nullptr;
		return Root.IsValid() && Root->TryGetObjectField(TEXT("weapons"), Block) && Block ? *Block : nullptr;
	}

	/**
	 * The PRE-SCHEMA pipeline, kept verbatim as the parity oracle: the asset's flat fields, onto which the old
	 * WeaponTuning::ApplyWeapon (commit 63d5b6b1) wrote the file's keys with the same conversions.
	 */
	struct FLegacyWeapon
	{
		EDamageType DamageType;
		EWeaponHandling Handling;
		float BaseDamage, AttackRangeCm, EffectiveRangeCm, FireRate, ArmorPenetration;
		bool bUsesAmmo;
		int32 MaxClipSize, DefaultReserveAmmo;
		float ReloadTime, FullAutoInterval, SuppressionWeight;
		EStatusEffect StatusEffect;
		float StatusDuration, StatusTickDamage, SelfColdGeneration, SelfWarmthGeneration;
		EAttackShape AttackShape;
		int32 MaxRangeCells;
		TArray<float> BaseHitChances, DistanceDamageMultipliers;

		explicit FLegacyWeapon(const UWeaponDataAsset& A)
			: DamageType(A.DamageType), Handling(A.Handling), BaseDamage(A.BaseDamage), AttackRangeCm(A.AttackRangeCm), EffectiveRangeCm(A.EffectiveRangeCm),
			FireRate(A.FireRate), ArmorPenetration(A.ArmorPenetration), bUsesAmmo(A.bUsesAmmo), MaxClipSize(A.MaxClipSize),
			DefaultReserveAmmo(A.DefaultReserveAmmo), ReloadTime(A.ReloadTime), FullAutoInterval(A.FullAutoInterval), SuppressionWeight(A.SuppressionWeight),
			StatusEffect(A.StatusEffect), StatusDuration(A.StatusDuration), StatusTickDamage(A.StatusTickDamage), SelfColdGeneration(A.SelfColdGeneration),
			SelfWarmthGeneration(A.SelfWarmthGeneration), AttackShape(A.AttackShape), MaxRangeCells(A.MaxRangeCells), BaseHitChances(A.BaseHitChances),
			DistanceDamageMultipliers(A.DistanceDamageMultipliers)
		{
		}
	};

	void LegacyNumber(const FJsonObject& Json, const TCHAR* Key, float& Value, float Scale = 1.f)
	{
		double Read = 0.0;
		if (Json.TryGetNumberField(Key, Read))
		{
			Value = static_cast<float>(Read) * Scale;
		}
	}

	void LegacyInteger(const FJsonObject& Json, const TCHAR* Key, int32& Value)
	{
		double Read = 0.0;
		if (Json.TryGetNumberField(Key, Read))
		{
			Value = FMath::RoundToInt(Read);
		}
	}

	void LegacyFloats(const FJsonObject& Json, const TCHAR* Key, TArray<float>& Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Json.TryGetArrayField(Key, Array) && Array)
		{
			Values.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				Values.Add(Value.IsValid() ? static_cast<float>(Value->AsNumber()) : 0.f);
			}
		}
	}

	void LegacyApply(const FJsonObject& Json, FLegacyWeapon& W)
	{
		LegacyNumber(Json, TEXT("base_damage"), W.BaseDamage);
		LegacyNumber(Json, TEXT("attack_range_m"), W.AttackRangeCm, 100.f);
		LegacyNumber(Json, TEXT("effective_range_m"), W.EffectiveRangeCm, 100.f);
		LegacyNumber(Json, TEXT("fire_rate"), W.FireRate);
		LegacyNumber(Json, TEXT("armor_penetration"), W.ArmorPenetration);
		LegacyInteger(Json, TEXT("max_clip_size"), W.MaxClipSize);
		LegacyInteger(Json, TEXT("default_reserve_ammo"), W.DefaultReserveAmmo);
		LegacyNumber(Json, TEXT("reload_time"), W.ReloadTime);
		LegacyNumber(Json, TEXT("status_duration"), W.StatusDuration);
		LegacyNumber(Json, TEXT("status_tick_damage"), W.StatusTickDamage);
		LegacyInteger(Json, TEXT("max_range_cells"), W.MaxRangeCells);
		LegacyNumber(Json, TEXT("full_auto_interval"), W.FullAutoInterval);
		LegacyNumber(Json, TEXT("suppression_weight"), W.SuppressionWeight);
		LegacyFloats(Json, TEXT("base_hit_chances"), W.BaseHitChances);
		LegacyFloats(Json, TEXT("distance_damage_multipliers"), W.DistanceDamageMultipliers);
	}

	/** Exact (bitwise for floats) comparison of every legacy value with its schema field; returns the mismatches, counts the values. */
	TArray<FString> CompareWithLegacy(const FLegacyWeapon& Old, const FWeaponStats& New, int32& InOutCompared)
	{
		TArray<FString> Diffs;
		auto F = [&Diffs, &InOutCompared](const TCHAR* Name, float A, float B)
		{
			++InOutCompared;
			if (FMath::IsNaN(A) || A != B)
			{
				Diffs.Add(FString::Printf(TEXT("%s %.6f -> %.6f"), Name, A, B));
			}
		};
		auto I = [&Diffs, &InOutCompared](const TCHAR* Name, int64 A, int64 B)
		{
			++InOutCompared;
			if (A != B)
			{
				Diffs.Add(FString::Printf(TEXT("%s %lld -> %lld"), Name, A, B));
			}
		};
		auto Arr = [&Diffs, &InOutCompared](const TCHAR* Name, const TArray<float>& A, const TArray<float>& B)
		{
			++InOutCompared;
			if (A != B)
			{
				Diffs.Add(FString::Printf(TEXT("%s (%d entries -> %d)"), Name, A.Num(), B.Num()));
			}
		};
		I(TEXT("damage_type"), static_cast<int64>(Old.DamageType), static_cast<int64>(New.Damage.DamageType));
		I(TEXT("handling"), static_cast<int64>(Old.Handling), static_cast<int64>(New.Handling));
		F(TEXT("base_damage"), Old.BaseDamage, New.Damage.BaseDamage);
		F(TEXT("attack_range"), Old.AttackRangeCm, New.Range.MaxRangeCm);
		F(TEXT("effective_range"), Old.EffectiveRangeCm, New.Range.EffectiveRangeCm);
		F(TEXT("fire_rate"), Old.FireRate, New.Fire.ShotIntervalSeconds);
		F(TEXT("armor_penetration"), Old.ArmorPenetration, New.Damage.ArmorPenetration);
		I(TEXT("uses_ammo"), Old.bUsesAmmo, New.Ammo.bUsesAmmo);
		I(TEXT("max_clip_size"), Old.MaxClipSize, New.Ammo.MagazineSize);
		I(TEXT("default_reserve_ammo"), Old.DefaultReserveAmmo, New.Ammo.ReserveAmmo);
		F(TEXT("reload_time"), Old.ReloadTime, New.Ammo.ReloadSeconds);
		F(TEXT("full_auto_interval"), Old.FullAutoInterval, New.Fire.FullAutoIntervalSeconds);
		F(TEXT("suppression_weight"), Old.SuppressionWeight, New.Fire.SuppressionWeight);
		I(TEXT("status_effect"), static_cast<int64>(Old.StatusEffect), static_cast<int64>(New.Status.Effect));
		F(TEXT("status_duration"), Old.StatusDuration, New.Status.DurationSeconds);
		F(TEXT("status_tick_damage"), Old.StatusTickDamage, New.Status.TickDamage);
		F(TEXT("self_cold_generation"), Old.SelfColdGeneration, New.Thermal.SelfColdPerShot);
		F(TEXT("self_warmth_generation"), Old.SelfWarmthGeneration, New.Thermal.SelfWarmthPerShot);
		I(TEXT("attack_shape"), static_cast<int64>(Old.AttackShape), static_cast<int64>(New.TurnBased.AttackShape));
		I(TEXT("max_range_cells"), Old.MaxRangeCells, New.TurnBased.MaxRangeCells);
		Arr(TEXT("base_hit_chances"), Old.BaseHitChances, New.Accuracy.HitChanceByCell);
		Arr(TEXT("distance_damage_multipliers"), Old.DistanceDamageMultipliers, New.Accuracy.DamageMultiplierByCell);
		return Diffs;
	}

	/** A fresh weapon whose Stats are Base (ApplyWeapon target that touches no project asset). */
	UWeaponDataAsset* MakeScratch(const FWeaponStats& Base)
	{
		UWeaponDataAsset* Scratch = NewObject<UWeaponDataAsset>();
		Scratch->WeaponId = TEXT("scratch");
		Scratch->Stats = Base;
		return Scratch;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaEveryWeaponLoadsTest, "CodexTactics.Weapons.Schema.EveryWeaponLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaEveryWeaponLoadsTest::RunTest(const FString&)
{
	using namespace WeaponSchemaTest;
	const TSharedPtr<FJsonObject> FileWeapons = LoadProjectWeapons();
	if (!TestTrue(TEXT("weapons_tuning.json \"weapons\" block"), FileWeapons.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("the file lists the roster's five weapons"), FileWeapons->Values.Num(), static_cast<int32>(UE_ARRAY_COUNT(Weapons)));
	for (const TCHAR* Out : OutOfPlay)
	{
		TestFalse(FString::Printf(TEXT("%s: not in the file (out of play)"), Out), FileWeapons->HasField(Out));
	}
	for (const FExpectedWeapon& Expected : Weapons)
	{
		const UWeaponDataAsset* Asset = LoadWeapon(Expected.Id);
		if (!TestNotNull(FString::Printf(TEXT("DA_Weapon_%s loads"), Expected.Id), Asset))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: asset id"), Expected.Id), Asset->WeaponId, FString(Expected.Id));
		// The asset's base layer is valid data too (the fallback when the file has no / a rejected block).
		TArray<FString> BaseErrors;
		TestTrue(FString::Printf(TEXT("%s: the asset's base values are valid"), Expected.Id), WeaponStatsRules::Validate(Asset->MakeBaseStats(), BaseErrors, Expected.Id));
		for (const FString& Error : BaseErrors)
		{
			AddError(FString::Printf(TEXT("asset base: %s"), *Error));
		}
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!TestTrue(FString::Printf(TEXT("%s: listed in the file"), Expected.Id), FileWeapons->TryGetObjectField(Expected.Id, Block) && Block))
		{
			continue;
		}
		UWeaponDataAsset* Scratch = MakeScratch(Asset->MakeBaseStats());
		TArray<FString> Errors;
		TArray<FString> Unknown;
		TestTrue(FString::Printf(TEXT("%s: the file's block validates"), Expected.Id), WeaponTuning::ApplyWeapon(**Block, *Scratch, &Errors, &Unknown));
		for (const FString& Error : Errors)
		{
			AddError(FString::Printf(TEXT("%s: %s"), Expected.Id, *Error));
		}
		TestEqual(FString::Printf(TEXT("%s: no unknown keys"), Expected.Id), Unknown.Num(), 0);
		TestTrue(FString::Printf(TEXT("%s: kind %s"), Expected.Id, *WeaponStatsRules::KindName(Expected.Kind)), Scratch->Stats.Kind == Expected.Kind);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaParityTest, "CodexTactics.Weapons.Schema.ParityWithLegacyPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaParityTest::RunTest(const FString&)
{
	// For every weapon: (asset flat fields + the OLD key mapping) == (MakeBaseStats + the schema's ApplyJson), value by value, bit-exact.
	using namespace WeaponSchemaTest;
	const TSharedPtr<FJsonObject> FileWeapons = LoadProjectWeapons();
	if (!TestTrue(TEXT("weapons_tuning.json \"weapons\" block"), FileWeapons.IsValid()))
	{
		return false;
	}
	int32 Compared = 0;
	int32 Checked = 0;
	for (const FExpectedWeapon& Expected : Weapons)
	{
		const UWeaponDataAsset* Asset = LoadWeapon(Expected.Id);
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!TestNotNull(FString::Printf(TEXT("DA_Weapon_%s"), Expected.Id), Asset) || !FileWeapons->TryGetObjectField(Expected.Id, Block) || !Block)
		{
			continue;
		}
		// Before the file: the asset's base values map one to one.
		FLegacyWeapon Old(*Asset);
		TArray<FString> Diffs = CompareWithLegacy(Old, Asset->MakeBaseStats(), Compared);
		TestEqual(FString::Printf(TEXT("%s: base values identical (%s)"), Expected.Id, *FString::Join(Diffs, TEXT(", "))), Diffs.Num(), 0);
		// After the file: the numbers every reader got before the schema.
		LegacyApply(**Block, Old);
		// Intended since the D-1 (2026-11-08): the file sets handling ("shotgun": the shotgun animation layer); the old pipeline had no such key.
		FString HandlingName;
		if ((*Block)->TryGetStringField(TEXT("handling"), HandlingName))
		{
			Old.Handling = HandlingName == TEXT("shotgun") ? EWeaponHandling::Shotgun
				: HandlingName == TEXT("pistol") ? EWeaponHandling::Pistol // the pistol layer (2026-11-08)
				: (HandlingName == TEXT("sniper_rifle") ? EWeaponHandling::SniperRifle : EWeaponHandling::Standard);
		}
		UWeaponDataAsset* Scratch = MakeScratch(Asset->MakeBaseStats());
		TestTrue(FString::Printf(TEXT("%s: applied"), Expected.Id), WeaponTuning::ApplyWeapon(**Block, *Scratch));
		Diffs = CompareWithLegacy(Old, Scratch->Stats, Compared);
		TestEqual(FString::Printf(TEXT("%s: tuned values identical (%s)"), Expected.Id, *FString::Join(Diffs, TEXT(", "))), Diffs.Num(), 0);
		// The derived helpers the readers call give the same numbers too.
		TestEqual(FString::Printf(TEXT("%s: effective range"), Expected.Id), Scratch->GetEffectiveRangeCm(),
			WeaponRangeRules::ResolveEffectiveCm(Old.EffectiveRangeCm, Old.AttackRangeCm));
		for (int32 Cells = 1; Cells <= 20; ++Cells)
		{
			const float OldChance = Old.BaseHitChances.Num() == 0 ? 0.85f : Old.BaseHitChances[FMath::Clamp(Cells - 1, 0, Old.BaseHitChances.Num() - 1)];
			TestEqual(FString::Printf(TEXT("%s: hit chance at %d cells"), Expected.Id, Cells), Scratch->GetHitChanceForDistance(Cells), OldChance);
		}
		TestEqual(FString::Printf(TEXT("%s: full-auto"), Expected.Id), Scratch->HasFullAuto(), Old.FullAutoInterval > 0.f);
		TestEqual(FString::Printf(TEXT("%s: sniper handling"), Expected.Id), Scratch->IsSniperRifle(), Old.Handling == EWeaponHandling::SniperRifle);
		++Checked;
	}
	TestEqual(TEXT("every roster weapon checked"), Checked, static_cast<int32>(UE_ARRAY_COUNT(Weapons)));
	AddInfo(FString::Printf(TEXT("Parity: %d weapons, %d values compared bit-exact (22 per weapon, before + after the file)"), Checked, Compared));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaProjectFileTest, "CodexTactics.Weapons.Schema.ProjectFileApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaProjectFileTest::RunTest(const FString&)
{
	// What StartPlay does: the whole file onto the roster's five assets, schema v2, nothing rejected, no typo keys.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	const WeaponTuning::FWeaponTuningReport& Report = WeaponTuning::GetLastReport();
	TestEqual(TEXT("schema version 2"), Report.SchemaVersion, WeaponStatsRules::SchemaVersion);
	TestEqual(TEXT("five weapons applied"), Report.Applied, 5);
	TestEqual(TEXT("five weapons out of play"), Report.RosterOut, 5);
	TestEqual(TEXT("nothing rejected"), Report.Rejected.Num(), 0);
	TestEqual(TEXT("no unknown keys"), Report.Unknown.Num(), 0);
	for (const FString& Error : Report.Errors)
	{
		AddError(Error);
	}
	// Spot values the game designer knows (the file is the master copy).
	const UWeaponDataAsset* M16 = WeaponSchemaTest::LoadWeapon(TEXT("m16"));
	const UWeaponDataAsset* Sniper = WeaponSchemaTest::LoadWeapon(TEXT("sniper_rifle"));
	if (TestTrue(TEXT("assets"), M16 && Sniper))
	{
		TestEqual(TEXT("M16 max reach 60 m"), M16->Stats.Range.MaxRangeCm, 6000.f);
		TestEqual(TEXT("M16 effective 28 m"), M16->Stats.Range.EffectiveRangeCm, 2800.f);
		TestEqual(TEXT("M16 full-auto 0.1 s"), M16->Stats.Fire.FullAutoIntervalSeconds, 0.1f);
		TestEqual(TEXT("sniper 70 damage"), Sniper->Stats.Damage.BaseDamage, 70.f);
		TestTrue(TEXT("sniper handling"), Sniper->IsSniperRifle());
	}
	// Step 2: the game designer's decisions 2026-11-08 written into the file (the intended new values).
	struct FExpectedClass
	{
		const TCHAR* Id;
		EArmorClass Class;
	};
	const FExpectedClass Classes[] = {
		{ TEXT("pistol"), EArmorClass::Light }, { TEXT("shotgun"), EArmorClass::Light }, { TEXT("m16"), EArmorClass::Medium },
		{ TEXT("grenade"), EArmorClass::Medium }, { TEXT("sniper_rifle"), EArmorClass::Heavy },
	};
	for (const FExpectedClass& Expected : Classes)
	{
		const UWeaponDataAsset* Weapon = WeaponSchemaTest::LoadWeapon(Expected.Id);
		TestTrue(FString::Printf(TEXT("%s: penetration class %s"), Expected.Id, *WeaponStatsRules::ArmorClassName(Expected.Class)),
			Weapon && Weapon->Stats.Damage.PenetrationClass == Expected.Class);
	}
	const UWeaponDataAsset* Shotgun = WeaponSchemaTest::LoadWeapon(TEXT("shotgun"));
	const UWeaponDataAsset* Pistol = WeaponSchemaTest::LoadWeapon(TEXT("pistol"));
	if (TestTrue(TEXT("shotgun + pistol"), Shotgun && Pistol && M16))
	{
		TestEqual(TEXT("shotgun: 8 pellets"), Shotgun->Stats.Pellets.Count, 8);
		TestEqual(TEXT("shotgun: cone 6 deg"), Shotgun->Stats.Pellets.ConeHalfAngleDeg, 6.f);
		TestEqual(TEXT("shotgun: 45 for the whole shot"), Shotgun->Stats.Damage.BaseDamage, 45.f);
		TestTrue(TEXT("shotgun: per shell"), Shotgun->Stats.Ammo.ReloadMode == EWeaponReloadMode::PerRound);
		TestEqual(TEXT("shotgun: 0.5 s a shell"), Shotgun->Stats.Ammo.PerRoundReloadSeconds, 0.5f);
		TestEqual(TEXT("shotgun: falloff floor 0.4"), Shotgun->Stats.Damage.FalloffDamageFloor, 0.4f);
		TestEqual(TEXT("pistol: falloff floor 0.6"), Pistol->Stats.Damage.FalloffDamageFloor, 0.6f);
		TestEqual(TEXT("M16: no falloff"), M16->Stats.Damage.FalloffDamageFloor, 1.f);
		TestEqual(TEXT("M16: recoil 0.3 deg"), M16->Stats.Accuracy.RecoilPerRoundDeg, 0.3f);
		TestEqual(TEXT("M16: recovery 0.5 s"), M16->Stats.Accuracy.RecoilRecoverySeconds, 0.5f);
		TestEqual(TEXT("pistol: no recoil"), Pistol->Stats.Accuracy.RecoilPerRoundDeg, 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaRejectsBadDataTest, "CodexTactics.Weapons.Schema.RejectsBadData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaRejectsBadDataTest::RunTest(const FString&)
{
	using namespace WeaponSchemaTest;
	FWeaponStats Rifle;
	Rifle.Range.MaxRangeCm = 6000.f;
	Rifle.Range.EffectiveRangeCm = 2800.f;
	TArray<FString> BaseErrors;
	if (!TestTrue(TEXT("the base rifle is valid"), WeaponStatsRules::Validate(Rifle, BaseErrors)))
	{
		return false;
	}
	struct FBadCase
	{
		const TCHAR* What;
		const TCHAR* Json;
	};
	const FBadCase Cases[] = {
		{ TEXT("negative max range"), TEXT("{\"attack_range_m\": -5}") },
		{ TEXT("zero max range"), TEXT("{\"attack_range_m\": 0}") },
		{ TEXT("negative effective range"), TEXT("{\"effective_range_m\": -1}") },
		{ TEXT("effective range beyond the max"), TEXT("{\"attack_range_m\": 20, \"effective_range_m\": 30}") },
		{ TEXT("min range not below the effective range"), TEXT("{\"min_range_m\": 40}") },
		{ TEXT("negative damage"), TEXT("{\"base_damage\": -3}") },
		{ TEXT("zero shot interval"), TEXT("{\"fire_rate\": 0}") },
		{ TEXT("armour penetration above 1"), TEXT("{\"armor_penetration\": 1.5}") },
		{ TEXT("zero magazine on a weapon that uses ammo"), TEXT("{\"max_clip_size\": 0}") },
		{ TEXT("reserve below -1"), TEXT("{\"default_reserve_ammo\": -2}") },
		{ TEXT("no reload time with ammo"), TEXT("{\"reload_time\": 0}") },
		{ TEXT("a melee weapon with ammo"), TEXT("{\"kind\": \"melee\"}") },
		{ TEXT("hit chance above 1"), TEXT("{\"base_hit_chances\": [0.9, 1.2]}") },
		{ TEXT("negative damage multiplier"), TEXT("{\"distance_damage_multipliers\": [1, -0.5]}") },
		{ TEXT("zero grid reach"), TEXT("{\"max_range_cells\": 0}") },
		{ TEXT("a status without a duration"), TEXT("{\"status_effect\": \"burning\", \"status_duration\": 0}") },
		{ TEXT("an unknown kind"), TEXT("{\"kind\": \"laser\"}") },
		{ TEXT("an unknown damage type"), TEXT("{\"damage_type\": \"acid\"}") },
		{ TEXT("a number as a string"), TEXT("{\"base_damage\": \"18\"}") },
		{ TEXT("uses_ammo not a bool"), TEXT("{\"uses_ammo\": 1}") },
		{ TEXT("zero pellets"), TEXT("{\"pellets\": {\"count\": 0}}") },
		{ TEXT("pellet cone 60 deg"), TEXT("{\"pellets\": {\"count\": 8, \"cone_half_angle_deg\": 60}}") },
		{ TEXT("a projectile without speed"), TEXT("{\"kind\": \"projectile\"}") },
		{ TEXT("full-damage radius beyond the blast"), TEXT("{\"explosion\": {\"radius_m\": 3, \"full_damage_radius_m\": 5}}") },
		{ TEXT("a block that is not an object"), TEXT("{\"explosion\": 4}") },
		{ TEXT("negative spread"), TEXT("{\"spread_scale\": -1}") },
	};
	int32 Rejected = 0;
	for (const FBadCase& Case : Cases)
	{
		const TSharedPtr<FJsonObject> Json = ParseWeaponJson(Case.Json);
		if (!TestTrue(FString::Printf(TEXT("%s: json parsed"), Case.What), Json.IsValid()))
		{
			continue;
		}
		UWeaponDataAsset* Weapon = MakeScratch(Rifle);
		TArray<FString> Errors;
		const bool bApplied = WeaponTuning::ApplyWeapon(*Json, *Weapon, &Errors);
		TestFalse(FString::Printf(TEXT("%s: rejected"), Case.What), bApplied);
		TestTrue(FString::Printf(TEXT("%s: says why"), Case.What), Errors.Num() > 0);
		TestEqual(FString::Printf(TEXT("%s: nothing changed (max range)"), Case.What), Weapon->Stats.Range.MaxRangeCm, Rifle.Range.MaxRangeCm);
		TestEqual(FString::Printf(TEXT("%s: nothing changed (damage)"), Case.What), Weapon->Stats.Damage.BaseDamage, Rifle.Damage.BaseDamage);
		TestTrue(FString::Printf(TEXT("%s: nothing changed (kind)"), Case.What), Weapon->Stats.Kind == Rifle.Kind);
		Rejected += bApplied ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("%d / %d bad cases rejected"), Rejected, static_cast<int32>(UE_ARRAY_COUNT(Cases))));

	// Good data of every kind passes (the shapes the next days will use).
	const TCHAR* const GoodCases[] = {
		TEXT("{\"kind\": \"melee\", \"uses_ammo\": false, \"max_clip_size\": 0, \"reload_time\": 0, \"attack_range_m\": 2, \"effective_range_m\": 0}"),
		TEXT("{\"kind\": \"hitscan\", \"pellets\": {\"count\": 8, \"cone_half_angle_deg\": 6}, \"reload_mode\": \"per_round\", \"reload_per_round_s\": 0.5}"),
		TEXT("{\"kind\": \"projectile\", \"projectile\": {\"speed_mps\": 115, \"arming_distance_m\": 10}, \"explosion\": {\"radius_m\": 5, \"full_damage_radius_m\": 2.5, \"edge_damage_multiplier\": 0.1, \"back_blast_length_m\": 8, \"back_blast_half_angle_deg\": 30}}"),
		TEXT("{\"kind\": \"projectile\", \"min_range_m\": 20, \"projectile\": {\"speed_mps\": 80, \"gravity_scale\": 1, \"high_angle\": true}, \"penetration_class\": \"heavy\"}"),
		TEXT("{\"kind\": \"beam\", \"damage_type\": \"fire\", \"status_effect\": \"burning\", \"status_duration\": 4, \"self_warmth_generation\": 2}"),
	};
	for (const TCHAR* Good : GoodCases)
	{
		const TSharedPtr<FJsonObject> Json = ParseWeaponJson(Good);
		UWeaponDataAsset* Weapon = MakeScratch(Rifle);
		TArray<FString> Errors;
		TestTrue(FString::Printf(TEXT("accepted: %s"), Good), Json.IsValid() && WeaponTuning::ApplyWeapon(*Json, *Weapon, &Errors));
		for (const FString& Error : Errors)
		{
			AddError(Error);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaRoundTripTest, "CodexTactics.Weapons.Schema.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaRoundTripTest::RunTest(const FString&)
{
	// Stats -> JSON (the dump) -> Stats gives the same weapon, reserved blocks included; unknown keys are reported, not applied.
	using namespace WeaponSchemaTest;
	FWeaponStats Rocket;
	Rocket.Kind = EWeaponKind::Projectile;
	Rocket.Damage.PenetrationClass = EArmorClass::Vehicle;
	Rocket.Projectile.SpeedCmS = 11500.f;
	Rocket.Projectile.ArmingDistanceCm = 1000.f;
	Rocket.Explosion.RadiusCm = 500.f;
	Rocket.Explosion.FullDamageRadiusCm = 250.f;
	Rocket.Explosion.EdgeDamageMultiplier = 0.1f;
	Rocket.Pellets.Count = 1;
	Rocket.Ammo.ReloadMode = EWeaponReloadMode::PerRound;
	Rocket.Accuracy.HitChanceByCell = { 0.9f, 0.5f };
	const TSharedRef<FJsonObject> Dumped = WeaponStatsRules::ToJson(Rocket);
	FWeaponStats Back;
	TArray<FString> Unknown;
	TArray<FString> Errors;
	TestTrue(TEXT("dump parses back"), WeaponStatsRules::ApplyJson(*Dumped, Back, &Unknown, &Errors));
	TestEqual(TEXT("the dump has only known keys"), Unknown.Num(), 0);
	TestTrue(TEXT("kind"), Back.Kind == EWeaponKind::Projectile);
	TestTrue(TEXT("penetration class"), Back.Damage.PenetrationClass == EArmorClass::Vehicle);
	TestEqual(TEXT("speed"), Back.Projectile.SpeedCmS, 11500.f);
	TestEqual(TEXT("arming"), Back.Projectile.ArmingDistanceCm, 1000.f);
	TestEqual(TEXT("blast"), Back.Explosion.RadiusCm, 500.f);
	TestEqual(TEXT("full-damage blast"), Back.Explosion.FullDamageRadiusCm, 250.f);
	TestTrue(TEXT("reload mode"), Back.Ammo.ReloadMode == EWeaponReloadMode::PerRound);
	TestEqual(TEXT("hit table"), Back.Accuracy.HitChanceByCell.Num(), 2);

	// Every project weapon round-trips through the dump (WeaponTuning::WeaponToJson) unchanged.
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	for (const FExpectedWeapon& Expected : Weapons)
	{
		const UWeaponDataAsset* Asset = LoadWeapon(Expected.Id);
		if (!Asset)
		{
			continue;
		}
		UWeaponDataAsset* Copy = MakeScratch(FWeaponStats());
		TArray<FString> CopyUnknown;
		TestTrue(FString::Printf(TEXT("%s: dump applies"), Expected.Id), WeaponTuning::ApplyWeapon(*WeaponTuning::WeaponToJson(*Asset), *Copy, nullptr, &CopyUnknown));
		TestEqual(FString::Printf(TEXT("%s: dump keys known"), Expected.Id), CopyUnknown.Num(), 0);
		const FWeaponStats& A = Asset->Stats;
		const FWeaponStats& B = Copy->Stats;
		const bool bSame = A.Kind == B.Kind && A.Damage.BaseDamage == B.Damage.BaseDamage && A.Range.MaxRangeCm == B.Range.MaxRangeCm
			&& A.Range.EffectiveRangeCm == B.Range.EffectiveRangeCm && A.Fire.ShotIntervalSeconds == B.Fire.ShotIntervalSeconds
			&& A.Ammo.MagazineSize == B.Ammo.MagazineSize && A.Ammo.ReserveAmmo == B.Ammo.ReserveAmmo && A.Ammo.bUsesAmmo == B.Ammo.bUsesAmmo
			&& A.Status.Effect == B.Status.Effect && A.Damage.DamageType == B.Damage.DamageType && A.Handling == B.Handling
			&& A.TurnBased.AttackShape == B.TurnBased.AttackShape && A.Accuracy.HitChanceByCell.Num() == B.Accuracy.HitChanceByCell.Num();
		TestTrue(FString::Printf(TEXT("%s: round trip"), Expected.Id), bSame);
	}

	// A typo is reported and ignored (the rest applies).
	const TSharedPtr<FJsonObject> Typo = ParseWeaponJson(TEXT("{\"base_damag\": 99, \"base_damage\": 21, \"pellets\": {\"cuont\": 3}}"));
	FWeaponStats Tuned;
	TArray<FString> TypoUnknown;
	TestTrue(TEXT("typo json parsed"), Typo.IsValid() && WeaponStatsRules::ApplyJson(*Typo, Tuned, &TypoUnknown));
	TestEqual(TEXT("two unknown keys"), TypoUnknown.Num(), 2);
	TestTrue(TEXT("names the nested one"), TypoUnknown.Contains(TEXT("pellets.cuont")));
	TestEqual(TEXT("the real key applied"), Tuned.Damage.BaseDamage, 21.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponSchemaNoLegacyReadsTest, "CodexTactics.Weapons.Schema.NoLegacyReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponSchemaNoLegacyReadsTest::RunTest(const FString&)
{
	// Gameplay code reads UWeaponDataAsset::Stats. A read of a base field (e.g. CurrentWeapon->AttackRangeCm, merged in from a branch cut
	// before the schema) would silently skip weapons_tuning.json - this scan of the runtime and dev modules catches it.
	const FString SourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
	TArray<FString> Files;
	for (const TCHAR* Module : { TEXT("CodexTactics"), TEXT("CodexTacticsDev"), TEXT("CodexTacticsEditor") })
	{
		IFileManager::Get().FindFilesRecursive(Files, *(SourceDir / Module), TEXT("*.cpp"), true, false, false);
		IFileManager::Get().FindFilesRecursive(Files, *(SourceDir / Module), TEXT("*.h"), true, false, false);
	}
	if (!TestTrue(TEXT("source files found"), Files.Num() > 50))
	{
		return false;
	}
	const FRegexPattern Pattern(TEXT("\\b\\w*(?:Weapon|Sniper|Rifle|Pistol|M16)\\w*(?:\\(\\))?(?:->|\\.)(?:BaseDamage|AttackRangeCm|EffectiveRangeCm|FireRate|ArmorPenetration|bUsesAmmo|MaxClipSize|DefaultReserveAmmo|ReloadTime|FullAutoInterval|SuppressionWeight|StatusEffect|StatusDuration|StatusTickDamage|SelfColdGeneration|SelfWarmthGeneration|AttackShape|MaxRangeCells|BaseHitChances|DistanceDamageMultipliers|DamageType|Handling)\\b"));
	int32 Hits = 0;
	for (const FString& File : Files)
	{
		if (File.EndsWith(TEXT("WeaponDataAsset.cpp")) || File.EndsWith(TEXT("WeaponDataAsset.h")))
		{
			continue; // the mapping base -> Stats itself
		}
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *File);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			FRegexMatcher Matcher(Pattern, Lines[Index]);
			if (Matcher.FindNext())
			{
				++Hits;
				AddError(FString::Printf(TEXT("%s(%d): reads a weapon base field - read UWeaponDataAsset::Stats: %s"), *FPaths::GetCleanFilename(File), Index + 1,
					*Lines[Index].TrimStartAndEnd()));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d source files scanned, %d base-field reads"), Files.Num(), Hits));
	return Hits == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
