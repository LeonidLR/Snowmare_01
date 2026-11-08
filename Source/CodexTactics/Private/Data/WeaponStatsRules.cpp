#include "Data/WeaponStatsRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace WeaponStatsRulesInternal
{
	template <typename EnumType>
	struct TWeaponEnumName
	{
		EnumType Value;
		const TCHAR* Name;
	};

	static const TWeaponEnumName<EWeaponKind> WeaponKindNames[] = {
		{ EWeaponKind::Hitscan, TEXT("hitscan") },
		{ EWeaponKind::Projectile, TEXT("projectile") },
		{ EWeaponKind::Thrown, TEXT("thrown") },
		{ EWeaponKind::Melee, TEXT("melee") },
		{ EWeaponKind::Beam, TEXT("beam") },
	};

	static const TWeaponEnumName<EWeaponHandling> WeaponHandlingNames[] = {
		{ EWeaponHandling::Standard, TEXT("standard") },
		{ EWeaponHandling::SniperRifle, TEXT("sniper_rifle") },
		{ EWeaponHandling::Shotgun, TEXT("shotgun") },
	};

	static const TWeaponEnumName<EDamageType> WeaponDamageTypeNames[] = {
		{ EDamageType::Kinetic, TEXT("kinetic") },
		{ EDamageType::Melee, TEXT("melee") },
		{ EDamageType::Fire, TEXT("fire") },
		{ EDamageType::Cryo, TEXT("cryo") },
		{ EDamageType::Energy, TEXT("energy") },
		{ EDamageType::Explosive, TEXT("explosive") },
	};

	static const TWeaponEnumName<EStatusEffect> WeaponStatusNames[] = {
		{ EStatusEffect::None, TEXT("none") },
		{ EStatusEffect::Burning, TEXT("burning") },
		{ EStatusEffect::Frozen, TEXT("frozen") },
		{ EStatusEffect::Stagger, TEXT("stagger") },
		{ EStatusEffect::Bleeding, TEXT("bleeding") },
		{ EStatusEffect::ArmorShred, TEXT("armor_shred") },
		{ EStatusEffect::Shocked, TEXT("shocked") },
	};

	static const TWeaponEnumName<EAttackShape> WeaponShapeNames[] = {
		{ EAttackShape::Rays8, TEXT("rays8") },
		{ EAttackShape::Rays4, TEXT("rays4") },
		{ EAttackShape::MeleeAdj, TEXT("melee_adj") },
		{ EAttackShape::FreeTarget, TEXT("free_target") },
	};

	static const TWeaponEnumName<EArmorClass> WeaponArmorClassNames[] = {
		{ EArmorClass::None, TEXT("none") },
		{ EArmorClass::Light, TEXT("light") },
		{ EArmorClass::Medium, TEXT("medium") },
		{ EArmorClass::Heavy, TEXT("heavy") },
		{ EArmorClass::Vehicle, TEXT("vehicle") },
	};

	static const TWeaponEnumName<EWeaponReloadMode> WeaponReloadModeNames[] = {
		{ EWeaponReloadMode::Magazine, TEXT("magazine") },
		{ EWeaponReloadMode::PerRound, TEXT("per_round") },
	};

	template <typename EnumType, int32 N>
	FString EnumToName(const TWeaponEnumName<EnumType> (&Table)[N], EnumType Value)
	{
		for (const TWeaponEnumName<EnumType>& Entry : Table)
		{
			if (Entry.Value == Value)
			{
				return Entry.Name;
			}
		}
		return Table[0].Name;
	}

	template <typename EnumType, int32 N>
	bool NameToEnum(const TWeaponEnumName<EnumType> (&Table)[N], const FString& Name, EnumType& Out)
	{
		for (const TWeaponEnumName<EnumType>& Entry : Table)
		{
			if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				Out = Entry.Value;
				return true;
			}
		}
		return false;
	}

	/** The schema's top-level keys (core + reserved + the per-kind block names). */
	static const TCHAR* const WeaponTopKeys[] = {
		TEXT("comment"), TEXT("name"),
		// Common core (Godot weapon_data.gd names; the Wave Editor edits these).
		TEXT("base_damage"), TEXT("attack_range_m"), TEXT("effective_range_m"), TEXT("fire_rate"), TEXT("armor_penetration"),
		TEXT("max_clip_size"), TEXT("default_reserve_ammo"), TEXT("reload_time"), TEXT("status_duration"), TEXT("status_tick_damage"),
		TEXT("max_range_cells"), TEXT("full_auto_interval"), TEXT("suppression_weight"), TEXT("base_hit_chances"),
		TEXT("distance_damage_multipliers"),
		// Core, schema v2 (were asset-only).
		TEXT("kind"), TEXT("handling"), TEXT("damage_type"), TEXT("status_effect"), TEXT("attack_shape"), TEXT("uses_ammo"),
		TEXT("self_cold_generation"), TEXT("self_warmth_generation"),
		// Reserved (read from 2026-11-09 on).
		TEXT("penetration_class"), TEXT("damage_falloff_floor"), TEXT("min_range_m"), TEXT("reload_mode"), TEXT("reload_per_round_s"),
		TEXT("spread_scale"), TEXT("recoil_per_round_deg"), TEXT("recoil_recovery_s"),
		// Reserved per-kind blocks.
		TEXT("pellets"), TEXT("projectile"), TEXT("explosion"),
		// Presentation (WeaponTuning::ApplyWeaponVisual: the model in the hand), not stats.
		TEXT("visual"),
	};

	static const TCHAR* const WeaponPelletKeys[] = { TEXT("comment"), TEXT("count"), TEXT("cone_half_angle_deg") };
	static const TCHAR* const WeaponProjectileKeys[] = { TEXT("comment"), TEXT("speed_mps"), TEXT("gravity_scale"), TEXT("arming_distance_m"), TEXT("high_angle") };
	static const TCHAR* const WeaponExplosionKeys[] = { TEXT("comment"), TEXT("radius_m"), TEXT("full_damage_radius_m"), TEXT("edge_damage_multiplier"),
		TEXT("back_blast_length_m"), TEXT("back_blast_half_angle_deg") };

	template <int32 N>
	void CollectUnknown(const FJsonObject& Json, const TCHAR* const (&Known)[N], const FString& Prefix, TArray<FString>* OutUnknown)
	{
		if (!OutUnknown)
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Json.Values)
		{
			bool bKnown = false;
			for (const TCHAR* Key : Known)
			{
				bKnown |= Pair.Key.Equals(Key, ESearchCase::CaseSensitive);
			}
			if (!bKnown)
			{
				OutUnknown->Add(Prefix + Pair.Key);
			}
		}
	}

	/** The parse context: reports a wrong-typed value as an error, leaves the field unchanged. */
	struct FWeaponJsonReader
	{
		const FJsonObject& Json;
		FString Prefix;
		TArray<FString>* Errors;
		bool bOk = true;

		void Fail(const FString& Key, const TCHAR* Expected)
		{
			bOk = false;
			if (Errors)
			{
				Errors->Add(FString::Printf(TEXT("\"%s%s\": expected %s"), *Prefix, *Key, Expected));
			}
		}

		const TSharedPtr<FJsonValue>* Find(const TCHAR* Key) const
		{
			return Json.Values.Find(Key);
		}

		/** Same conversion as the pre-schema WeaponTuning::Number: float(value) x Scale. */
		void Number(const TCHAR* Key, float& Value, float Scale = 1.f)
		{
			if (const TSharedPtr<FJsonValue>* Found = Find(Key))
			{
				double Read = 0.0;
				if (Found->IsValid() && (*Found)->Type == EJson::Number && (*Found)->TryGetNumber(Read))
				{
					Value = static_cast<float>(Read) * Scale;
				}
				else
				{
					Fail(Key, TEXT("a number"));
				}
			}
		}

		/** Same conversion as the pre-schema WeaponTuning::Integer: round to the nearest integer. */
		void Integer(const TCHAR* Key, int32& Value)
		{
			if (const TSharedPtr<FJsonValue>* Found = Find(Key))
			{
				double Read = 0.0;
				if (Found->IsValid() && (*Found)->Type == EJson::Number && (*Found)->TryGetNumber(Read))
				{
					Value = FMath::RoundToInt(Read);
				}
				else
				{
					Fail(Key, TEXT("a number"));
				}
			}
		}

		void Bool(const TCHAR* Key, bool& Value)
		{
			if (const TSharedPtr<FJsonValue>* Found = Find(Key))
			{
				bool Read = false;
				if (Found->IsValid() && (*Found)->Type == EJson::Boolean && (*Found)->TryGetBool(Read))
				{
					Value = Read;
				}
				else
				{
					Fail(Key, TEXT("true / false"));
				}
			}
		}

		/** Same conversion as the pre-schema WeaponTuning::Floats (a non-number entry reads 0, the array replaces the old one). */
		void Floats(const TCHAR* Key, TArray<float>& Values)
		{
			if (const TSharedPtr<FJsonValue>* Found = Find(Key))
			{
				const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
				if (Found->IsValid() && (*Found)->TryGetArray(Array) && Array)
				{
					Values.Reset();
					for (const TSharedPtr<FJsonValue>& Value : *Array)
					{
						Values.Add(Value.IsValid() ? static_cast<float>(Value->AsNumber()) : 0.f);
					}
				}
				else
				{
					Fail(Key, TEXT("an array of numbers"));
				}
			}
		}

		template <typename EnumType, int32 N>
		void Enum(const TCHAR* Key, const TWeaponEnumName<EnumType> (&Table)[N], EnumType& Value)
		{
			if (const TSharedPtr<FJsonValue>* Found = Find(Key))
			{
				FString Name;
				EnumType Parsed = Value;
				if (Found->IsValid() && (*Found)->Type == EJson::String && (*Found)->TryGetString(Name) && NameToEnum(Table, Name, Parsed))
				{
					Value = Parsed;
				}
				else
				{
					FString Expected = TEXT("one of");
					for (const TWeaponEnumName<EnumType>& Entry : Table)
					{
						Expected += FString::Printf(TEXT(" \"%s\""), Entry.Name);
					}
					Fail(Key, *Expected);
				}
			}
		}

		/** The nested block Key (an object) or null; a non-object value is an error. */
		const FJsonObject* Block(const TCHAR* Key)
		{
			const TSharedPtr<FJsonValue>* Found = Find(Key);
			if (!Found)
			{
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Found->IsValid() && (*Found)->TryGetObject(Object) && Object && Object->IsValid())
			{
				return Object->Get();
			}
			Fail(Key, TEXT("an object"));
			return nullptr;
		}
	};

	/** 4 decimals: no float noise (0.6499999) in the file the user edits (the pre-schema dump's rounding). */
	double WeaponRound4(double Value)
	{
		return FMath::RoundToDouble(Value * 10000.0) / 10000.0;
	}

	TArray<TSharedPtr<FJsonValue>> WeaponFloatArray(const TArray<float>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const float Value : Values)
		{
			Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Value * 1000.f) / 1000.f));
		}
		return Out;
	}
}

namespace WeaponStatsRules
{
	using namespace WeaponStatsRulesInternal;

	FString KindName(EWeaponKind Kind)
	{
		return EnumToName(WeaponKindNames, Kind);
	}

	bool ParseKind(const FString& Name, EWeaponKind& Out)
	{
		return NameToEnum(WeaponKindNames, Name, Out);
	}

	FString ArmorClassName(EArmorClass Class)
	{
		return EnumToName(WeaponArmorClassNames, Class);
	}

	bool ParseArmorClass(const FString& Name, EArmorClass& Out)
	{
		return NameToEnum(WeaponArmorClassNames, Name, Out);
	}

	const TArray<FString>& KnownKeys()
	{
		static const TArray<FString> Keys = []()
		{
			TArray<FString> Out;
			for (const TCHAR* Key : WeaponTopKeys)
			{
				Out.Add(Key);
			}
			return Out;
		}();
		return Keys;
	}

	bool ApplyJson(const FJsonObject& Json, FWeaponStats& InOut, TArray<FString>* OutUnknown, TArray<FString>* OutErrors)
	{
		CollectUnknown(Json, WeaponTopKeys, FString(), OutUnknown);
		FWeaponJsonReader Read{ Json, FString(), OutErrors };

		// Common core: the pre-schema WeaponTuning::ApplyWeapon keys and conversions, unchanged (parity).
		Read.Number(TEXT("base_damage"), InOut.Damage.BaseDamage);
		Read.Number(TEXT("attack_range_m"), InOut.Range.MaxRangeCm, 100.f);
		Read.Number(TEXT("effective_range_m"), InOut.Range.EffectiveRangeCm, 100.f); // bug #39
		Read.Number(TEXT("fire_rate"), InOut.Fire.ShotIntervalSeconds);
		Read.Number(TEXT("armor_penetration"), InOut.Damage.ArmorPenetration);
		Read.Integer(TEXT("max_clip_size"), InOut.Ammo.MagazineSize);
		Read.Integer(TEXT("default_reserve_ammo"), InOut.Ammo.ReserveAmmo);
		Read.Number(TEXT("reload_time"), InOut.Ammo.ReloadSeconds);
		Read.Number(TEXT("status_duration"), InOut.Status.DurationSeconds);
		Read.Number(TEXT("status_tick_damage"), InOut.Status.TickDamage);
		Read.Integer(TEXT("max_range_cells"), InOut.TurnBased.MaxRangeCells);
		Read.Number(TEXT("full_auto_interval"), InOut.Fire.FullAutoIntervalSeconds); // suppressive fire (2026-10-26)
		Read.Number(TEXT("suppression_weight"), InOut.Fire.SuppressionWeight);
		Read.Floats(TEXT("base_hit_chances"), InOut.Accuracy.HitChanceByCell);
		Read.Floats(TEXT("distance_damage_multipliers"), InOut.Accuracy.DamageMultiplierByCell);

		// Core, schema v2: values that used to live only on the asset.
		Read.Enum(TEXT("kind"), WeaponKindNames, InOut.Kind);
		Read.Enum(TEXT("handling"), WeaponHandlingNames, InOut.Handling);
		Read.Enum(TEXT("damage_type"), WeaponDamageTypeNames, InOut.Damage.DamageType);
		Read.Enum(TEXT("status_effect"), WeaponStatusNames, InOut.Status.Effect);
		Read.Enum(TEXT("attack_shape"), WeaponShapeNames, InOut.TurnBased.AttackShape);
		Read.Bool(TEXT("uses_ammo"), InOut.Ammo.bUsesAmmo);
		Read.Number(TEXT("self_cold_generation"), InOut.Thermal.SelfColdPerShot);
		Read.Number(TEXT("self_warmth_generation"), InOut.Thermal.SelfWarmthPerShot);

		// Reserved (parsed + validated; the ballistics / penetration code reads them from 2026-11-09 on).
		Read.Enum(TEXT("penetration_class"), WeaponArmorClassNames, InOut.Damage.PenetrationClass);
		Read.Number(TEXT("damage_falloff_floor"), InOut.Damage.FalloffDamageFloor);
		Read.Number(TEXT("min_range_m"), InOut.Range.MinRangeCm, 100.f);
		Read.Enum(TEXT("reload_mode"), WeaponReloadModeNames, InOut.Ammo.ReloadMode);
		Read.Number(TEXT("reload_per_round_s"), InOut.Ammo.PerRoundReloadSeconds);
		Read.Number(TEXT("spread_scale"), InOut.Accuracy.SpreadScale);
		Read.Number(TEXT("recoil_per_round_deg"), InOut.Accuracy.RecoilPerRoundDeg);
		Read.Number(TEXT("recoil_recovery_s"), InOut.Accuracy.RecoilRecoverySeconds);

		if (const FJsonObject* Pellets = Read.Block(TEXT("pellets")))
		{
			CollectUnknown(*Pellets, WeaponPelletKeys, TEXT("pellets."), OutUnknown);
			FWeaponJsonReader Block{ *Pellets, TEXT("pellets."), OutErrors };
			Block.Integer(TEXT("count"), InOut.Pellets.Count);
			Block.Number(TEXT("cone_half_angle_deg"), InOut.Pellets.ConeHalfAngleDeg);
			Read.bOk &= Block.bOk;
		}
		if (const FJsonObject* Projectile = Read.Block(TEXT("projectile")))
		{
			CollectUnknown(*Projectile, WeaponProjectileKeys, TEXT("projectile."), OutUnknown);
			FWeaponJsonReader Block{ *Projectile, TEXT("projectile."), OutErrors };
			Block.Number(TEXT("speed_mps"), InOut.Projectile.SpeedCmS, 100.f);
			Block.Number(TEXT("gravity_scale"), InOut.Projectile.GravityScale);
			Block.Number(TEXT("arming_distance_m"), InOut.Projectile.ArmingDistanceCm, 100.f);
			Block.Bool(TEXT("high_angle"), InOut.Projectile.bHighAngle);
			Read.bOk &= Block.bOk;
		}
		if (const FJsonObject* Explosion = Read.Block(TEXT("explosion")))
		{
			CollectUnknown(*Explosion, WeaponExplosionKeys, TEXT("explosion."), OutUnknown);
			FWeaponJsonReader Block{ *Explosion, TEXT("explosion."), OutErrors };
			Block.Number(TEXT("radius_m"), InOut.Explosion.RadiusCm, 100.f);
			Block.Number(TEXT("full_damage_radius_m"), InOut.Explosion.FullDamageRadiusCm, 100.f);
			Block.Number(TEXT("edge_damage_multiplier"), InOut.Explosion.EdgeDamageMultiplier);
			Block.Number(TEXT("back_blast_length_m"), InOut.Explosion.BackBlastLengthCm, 100.f);
			Block.Number(TEXT("back_blast_half_angle_deg"), InOut.Explosion.BackBlastHalfAngleDeg);
			Read.bOk &= Block.bOk;
		}
		return Read.bOk;
	}

	TSharedRef<FJsonObject> ToJson(const FWeaponStats& Stats, bool bReserved)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("kind"), KindName(Stats.Kind));
		Json->SetNumberField(TEXT("base_damage"), WeaponRound4(Stats.Damage.BaseDamage));
		Json->SetNumberField(TEXT("attack_range_m"), WeaponRound4(Stats.Range.MaxRangeCm / 100.f));
		Json->SetNumberField(TEXT("effective_range_m"), WeaponRound4(Stats.Range.EffectiveRangeCm / 100.f));
		Json->SetNumberField(TEXT("fire_rate"), WeaponRound4(Stats.Fire.ShotIntervalSeconds));
		Json->SetNumberField(TEXT("armor_penetration"), WeaponRound4(Stats.Damage.ArmorPenetration));
		Json->SetNumberField(TEXT("max_clip_size"), Stats.Ammo.MagazineSize);
		Json->SetNumberField(TEXT("default_reserve_ammo"), Stats.Ammo.ReserveAmmo);
		Json->SetNumberField(TEXT("reload_time"), WeaponRound4(Stats.Ammo.ReloadSeconds));
		Json->SetNumberField(TEXT("status_duration"), WeaponRound4(Stats.Status.DurationSeconds));
		Json->SetNumberField(TEXT("status_tick_damage"), WeaponRound4(Stats.Status.TickDamage));
		Json->SetNumberField(TEXT("max_range_cells"), Stats.TurnBased.MaxRangeCells);
		Json->SetNumberField(TEXT("full_auto_interval"), WeaponRound4(Stats.Fire.FullAutoIntervalSeconds));
		Json->SetNumberField(TEXT("suppression_weight"), WeaponRound4(Stats.Fire.SuppressionWeight));
		Json->SetArrayField(TEXT("base_hit_chances"), WeaponFloatArray(Stats.Accuracy.HitChanceByCell));
		Json->SetArrayField(TEXT("distance_damage_multipliers"), WeaponFloatArray(Stats.Accuracy.DamageMultiplierByCell));
		Json->SetStringField(TEXT("handling"), EnumToName(WeaponHandlingNames, Stats.Handling));
		Json->SetStringField(TEXT("damage_type"), EnumToName(WeaponDamageTypeNames, Stats.Damage.DamageType));
		Json->SetStringField(TEXT("status_effect"), EnumToName(WeaponStatusNames, Stats.Status.Effect));
		Json->SetStringField(TEXT("attack_shape"), EnumToName(WeaponShapeNames, Stats.TurnBased.AttackShape));
		Json->SetBoolField(TEXT("uses_ammo"), Stats.Ammo.bUsesAmmo);
		Json->SetNumberField(TEXT("self_cold_generation"), WeaponRound4(Stats.Thermal.SelfColdPerShot));
		Json->SetNumberField(TEXT("self_warmth_generation"), WeaponRound4(Stats.Thermal.SelfWarmthPerShot));
		if (!bReserved)
		{
			return Json;
		}
		Json->SetStringField(TEXT("penetration_class"), ArmorClassName(Stats.Damage.PenetrationClass));
		Json->SetNumberField(TEXT("damage_falloff_floor"), WeaponRound4(Stats.Damage.FalloffDamageFloor));
		Json->SetNumberField(TEXT("min_range_m"), WeaponRound4(Stats.Range.MinRangeCm / 100.f));
		Json->SetStringField(TEXT("reload_mode"), EnumToName(WeaponReloadModeNames, Stats.Ammo.ReloadMode));
		Json->SetNumberField(TEXT("reload_per_round_s"), WeaponRound4(Stats.Ammo.PerRoundReloadSeconds));
		Json->SetNumberField(TEXT("spread_scale"), WeaponRound4(Stats.Accuracy.SpreadScale));
		Json->SetNumberField(TEXT("recoil_per_round_deg"), WeaponRound4(Stats.Accuracy.RecoilPerRoundDeg));
		Json->SetNumberField(TEXT("recoil_recovery_s"), WeaponRound4(Stats.Accuracy.RecoilRecoverySeconds));
		TSharedRef<FJsonObject> Pellets = MakeShared<FJsonObject>();
		Pellets->SetNumberField(TEXT("count"), Stats.Pellets.Count);
		Pellets->SetNumberField(TEXT("cone_half_angle_deg"), WeaponRound4(Stats.Pellets.ConeHalfAngleDeg));
		Json->SetObjectField(TEXT("pellets"), Pellets);
		TSharedRef<FJsonObject> Projectile = MakeShared<FJsonObject>();
		Projectile->SetNumberField(TEXT("speed_mps"), WeaponRound4(Stats.Projectile.SpeedCmS / 100.f));
		Projectile->SetNumberField(TEXT("gravity_scale"), WeaponRound4(Stats.Projectile.GravityScale));
		Projectile->SetNumberField(TEXT("arming_distance_m"), WeaponRound4(Stats.Projectile.ArmingDistanceCm / 100.f));
		Projectile->SetBoolField(TEXT("high_angle"), Stats.Projectile.bHighAngle);
		Json->SetObjectField(TEXT("projectile"), Projectile);
		TSharedRef<FJsonObject> Explosion = MakeShared<FJsonObject>();
		Explosion->SetNumberField(TEXT("radius_m"), WeaponRound4(Stats.Explosion.RadiusCm / 100.f));
		Explosion->SetNumberField(TEXT("full_damage_radius_m"), WeaponRound4(Stats.Explosion.FullDamageRadiusCm / 100.f));
		Explosion->SetNumberField(TEXT("edge_damage_multiplier"), WeaponRound4(Stats.Explosion.EdgeDamageMultiplier));
		Explosion->SetNumberField(TEXT("back_blast_length_m"), WeaponRound4(Stats.Explosion.BackBlastLengthCm / 100.f));
		Explosion->SetNumberField(TEXT("back_blast_half_angle_deg"), WeaponRound4(Stats.Explosion.BackBlastHalfAngleDeg));
		Json->SetObjectField(TEXT("explosion"), Explosion);
		return Json;
	}

	bool Validate(const FWeaponStats& Stats, TArray<FString>& OutErrors, const FString& WeaponId)
	{
		const int32 Before = OutErrors.Num();
		const FString Who = WeaponId.IsEmpty() ? FString() : WeaponId + TEXT(": ");
		auto Check = [&OutErrors, &Who](bool bOk, const FString& Problem)
		{
			if (!bOk)
			{
				OutErrors.Add(Who + Problem);
			}
		};

		const FWeaponRangeStats& Range = Stats.Range;
		Check(Stats.Damage.BaseDamage >= 0.f, FString::Printf(TEXT("base_damage %.2f is negative"), Stats.Damage.BaseDamage));
		Check(Stats.Damage.ArmorPenetration >= 0.f && Stats.Damage.ArmorPenetration <= 1.f,
			FString::Printf(TEXT("armor_penetration %.2f is outside [0, 1]"), Stats.Damage.ArmorPenetration));
		Check(Stats.Damage.FalloffDamageFloor >= 0.f && Stats.Damage.FalloffDamageFloor <= 1.f,
			FString::Printf(TEXT("damage_falloff_floor %.2f is outside [0, 1]"), Stats.Damage.FalloffDamageFloor));

		Check(Range.MaxRangeCm > 0.f, FString::Printf(TEXT("attack_range_m %.2f must be > 0"), Range.MaxRangeCm / 100.f));
		Check(Range.EffectiveRangeCm >= 0.f, FString::Printf(TEXT("effective_range_m %.2f is negative"), Range.EffectiveRangeCm / 100.f));
		Check(Range.EffectiveRangeCm <= Range.MaxRangeCm, FString::Printf(TEXT("effective_range_m %.2f is beyond attack_range_m %.2f"),
			Range.EffectiveRangeCm / 100.f, Range.MaxRangeCm / 100.f));
		Check(Range.MinRangeCm >= 0.f, FString::Printf(TEXT("min_range_m %.2f is negative"), Range.MinRangeCm / 100.f));
		Check(Range.MinRangeCm <= 0.f || Range.MinRangeCm < Range.GetEffectiveRangeCm(),
			FString::Printf(TEXT("min_range_m %.2f is not below the effective range %.2f"), Range.MinRangeCm / 100.f, Range.GetEffectiveRangeCm() / 100.f));

		Check(Stats.Fire.ShotIntervalSeconds > 0.f, FString::Printf(TEXT("fire_rate (shot interval) %.3f s must be > 0"), Stats.Fire.ShotIntervalSeconds));
		Check(Stats.Fire.FullAutoIntervalSeconds >= 0.f, FString::Printf(TEXT("full_auto_interval %.3f is negative"), Stats.Fire.FullAutoIntervalSeconds));
		Check(Stats.Fire.SuppressionWeight >= 0.f, FString::Printf(TEXT("suppression_weight %.2f is negative"), Stats.Fire.SuppressionWeight));

		const FWeaponAmmoStats& Ammo = Stats.Ammo;
		Check(Ammo.MagazineSize >= 0, FString::Printf(TEXT("max_clip_size %d is negative"), Ammo.MagazineSize));
		Check(!Ammo.bUsesAmmo || Ammo.MagazineSize >= 1, FString::Printf(TEXT("max_clip_size %d: a weapon that uses ammo needs a magazine >= 1"), Ammo.MagazineSize));
		Check(Ammo.ReserveAmmo >= -1, FString::Printf(TEXT("default_reserve_ammo %d is below -1 (-1 = infinite)"), Ammo.ReserveAmmo));
		Check(Ammo.ReloadSeconds >= 0.f, FString::Printf(TEXT("reload_time %.2f is negative"), Ammo.ReloadSeconds));
		Check(!Ammo.bUsesAmmo || Ammo.ReloadSeconds > 0.f, FString::Printf(TEXT("reload_time %.2f: a weapon that uses ammo needs a reload > 0"), Ammo.ReloadSeconds));
		Check(Ammo.PerRoundReloadSeconds >= 0.f, FString::Printf(TEXT("reload_per_round_s %.2f is negative"), Ammo.PerRoundReloadSeconds));
		Check(Stats.Kind != EWeaponKind::Melee || !Ammo.bUsesAmmo, TEXT("a melee weapon uses no ammo (uses_ammo false)"));

		for (int32 Index = 0; Index < Stats.Accuracy.HitChanceByCell.Num(); ++Index)
		{
			const float Chance = Stats.Accuracy.HitChanceByCell[Index];
			Check(Chance >= 0.f && Chance <= 1.f, FString::Printf(TEXT("base_hit_chances[%d] %.3f is outside [0, 1]"), Index, Chance));
		}
		for (int32 Index = 0; Index < Stats.Accuracy.DamageMultiplierByCell.Num(); ++Index)
		{
			const float Multiplier = Stats.Accuracy.DamageMultiplierByCell[Index];
			Check(Multiplier >= 0.f, FString::Printf(TEXT("distance_damage_multipliers[%d] %.3f is negative"), Index, Multiplier));
		}
		Check(Stats.Accuracy.SpreadScale >= 0.f, FString::Printf(TEXT("spread_scale %.2f is negative"), Stats.Accuracy.SpreadScale));
		Check(Stats.Accuracy.RecoilPerRoundDeg >= 0.f && Stats.Accuracy.RecoilRecoverySeconds >= 0.f, TEXT("recoil values must not be negative"));

		Check(Stats.Status.DurationSeconds >= 0.f && Stats.Status.TickDamage >= 0.f, TEXT("status_duration / status_tick_damage must not be negative"));
		Check(Stats.Status.Effect == EStatusEffect::None || Stats.Status.DurationSeconds > 0.f,
			FString::Printf(TEXT("status_effect \"%s\" needs a status_duration > 0"), *EnumToName(WeaponStatusNames, Stats.Status.Effect)));
		Check(Stats.Thermal.SelfColdPerShot >= 0.f && Stats.Thermal.SelfWarmthPerShot >= 0.f, TEXT("self_cold / self_warmth_generation must not be negative"));
		Check(Stats.TurnBased.MaxRangeCells >= 1, FString::Printf(TEXT("max_range_cells %d must be >= 1"), Stats.TurnBased.MaxRangeCells));

		Check(Stats.Pellets.Count >= 1, FString::Printf(TEXT("pellets.count %d must be >= 1"), Stats.Pellets.Count));
		Check(Stats.Pellets.ConeHalfAngleDeg >= 0.f && Stats.Pellets.ConeHalfAngleDeg < 45.f,
			FString::Printf(TEXT("pellets.cone_half_angle_deg %.1f is outside [0, 45)"), Stats.Pellets.ConeHalfAngleDeg));
		Check(Stats.Kind != EWeaponKind::Projectile || Stats.Projectile.SpeedCmS > 0.f, TEXT("a projectile weapon needs projectile.speed_mps > 0"));
		Check(Stats.Projectile.SpeedCmS >= 0.f && Stats.Projectile.GravityScale >= 0.f && Stats.Projectile.ArmingDistanceCm >= 0.f,
			TEXT("projectile values must not be negative"));
		const FWeaponExplosionStats& Blast = Stats.Explosion;
		Check(Blast.RadiusCm >= 0.f && Blast.FullDamageRadiusCm >= 0.f && Blast.BackBlastLengthCm >= 0.f, TEXT("explosion radii must not be negative"));
		Check(Blast.FullDamageRadiusCm <= Blast.RadiusCm, FString::Printf(TEXT("explosion.full_damage_radius_m %.2f is beyond radius_m %.2f"),
			Blast.FullDamageRadiusCm / 100.f, Blast.RadiusCm / 100.f));
		Check(Blast.EdgeDamageMultiplier >= 0.f && Blast.EdgeDamageMultiplier <= 1.f,
			FString::Printf(TEXT("explosion.edge_damage_multiplier %.2f is outside [0, 1]"), Blast.EdgeDamageMultiplier));
		Check(Blast.BackBlastHalfAngleDeg >= 0.f && Blast.BackBlastHalfAngleDeg <= 90.f,
			FString::Printf(TEXT("explosion.back_blast_half_angle_deg %.1f is outside [0, 90]"), Blast.BackBlastHalfAngleDeg));
		return OutErrors.Num() == Before;
	}
}
