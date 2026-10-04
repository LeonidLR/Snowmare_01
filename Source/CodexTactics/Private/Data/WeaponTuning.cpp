#include "Data/WeaponTuning.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/MarksmanAIRules.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Core/CodexTacticsGameMode.h"
#include "Tactics/EnemyTurnRules.h"
#include "Tactics/TurnBasedRules.h"
#include "CodexTactics.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace WeaponTuning
{
	namespace
	{
		TSharedPtr<FJsonObject> GrenadeBlock;
		TSharedPtr<FJsonObject> MarksmanRifleBlock;
		TSharedPtr<FJsonObject> TurnRulesBlock;

		void Number(const FJsonObject& Json, const TCHAR* Key, float& Value, float Scale = 1.f)
		{
			double Read = 0.0;
			if (Json.TryGetNumberField(Key, Read))
			{
				Value = static_cast<float>(Read) * Scale;
			}
		}

		void Integer(const FJsonObject& Json, const TCHAR* Key, int32& Value)
		{
			double Read = 0.0;
			if (Json.TryGetNumberField(Key, Read))
			{
				Value = FMath::RoundToInt(Read);
			}
		}

		void Floats(const FJsonObject& Json, const TCHAR* Key, TArray<float>& Values)
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

		TArray<TSharedPtr<FJsonValue>> ToArray(const TArray<float>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const float Value : Values)
			{
				Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Value * 1000.f) / 1000.f));
			}
			return Out;
		}

		/** 4 decimals: no float noise (0.6499999) in the file the user edits. */
		double Round4(double Value)
		{
			return FMath::RoundToDouble(Value * 10000.0) / 10000.0;
		}

		TArray<UWeaponDataAsset*> LoadWeapons()
		{
			TArray<UWeaponDataAsset*> Weapons;
			IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			TArray<FAssetData> Assets;
			Registry.GetAssetsByPath(FName(TEXT("/Game/Data/Weapons")), Assets, true);
			for (const FAssetData& Asset : Assets)
			{
				if (UWeaponDataAsset* Weapon = Cast<UWeaponDataAsset>(Asset.GetAsset()))
				{
					Weapons.Add(Weapon);
				}
			}
			return Weapons;
		}
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapons_tuning.json");
	}

	void ApplyWeapon(const FJsonObject& Json, UWeaponDataAsset& Weapon)
	{
		Number(Json, TEXT("base_damage"), Weapon.BaseDamage);
		Number(Json, TEXT("attack_range_m"), Weapon.AttackRangeCm, 100.f);
		Number(Json, TEXT("fire_rate"), Weapon.FireRate);
		Number(Json, TEXT("armor_penetration"), Weapon.ArmorPenetration);
		Integer(Json, TEXT("max_clip_size"), Weapon.MaxClipSize);
		Integer(Json, TEXT("default_reserve_ammo"), Weapon.DefaultReserveAmmo);
		Number(Json, TEXT("reload_time"), Weapon.ReloadTime);
		Number(Json, TEXT("status_duration"), Weapon.StatusDuration);
		Number(Json, TEXT("status_tick_damage"), Weapon.StatusTickDamage);
		Integer(Json, TEXT("max_range_cells"), Weapon.MaxRangeCells);
		Floats(Json, TEXT("base_hit_chances"), Weapon.BaseHitChances);
		Floats(Json, TEXT("distance_damage_multipliers"), Weapon.DistanceDamageMultipliers);
	}

	TSharedRef<FJsonObject> WeaponToJson(const UWeaponDataAsset& Weapon)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Weapon.WeaponName.ToString());
		Json->SetNumberField(TEXT("base_damage"), Round4(Weapon.BaseDamage));
		Json->SetNumberField(TEXT("attack_range_m"), Round4(Weapon.AttackRangeCm / 100.f));
		Json->SetNumberField(TEXT("fire_rate"), Round4(Weapon.FireRate));
		Json->SetNumberField(TEXT("armor_penetration"), Round4(Weapon.ArmorPenetration));
		Json->SetNumberField(TEXT("max_clip_size"), Round4(Weapon.MaxClipSize));
		Json->SetNumberField(TEXT("default_reserve_ammo"), Round4(Weapon.DefaultReserveAmmo));
		Json->SetNumberField(TEXT("reload_time"), Round4(Weapon.ReloadTime));
		Json->SetNumberField(TEXT("status_duration"), Round4(Weapon.StatusDuration));
		Json->SetNumberField(TEXT("status_tick_damage"), Round4(Weapon.StatusTickDamage));
		Json->SetNumberField(TEXT("max_range_cells"), Round4(Weapon.MaxRangeCells));
		Json->SetArrayField(TEXT("base_hit_chances"), ToArray(Weapon.BaseHitChances));
		Json->SetArrayField(TEXT("distance_damage_multipliers"), ToArray(Weapon.DistanceDamageMultipliers));
		return Json;
	}

	int32 ApplyFile(const FString& Path)
	{
		GrenadeBlock.Reset();
		MarksmanRifleBlock.Reset();
		TurnRulesBlock.Reset();
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return 0;
		}
		const TSharedPtr<FJsonObject>* Grenade = nullptr;
		if (Root->TryGetObjectField(TEXT("grenade"), Grenade) && Grenade)
		{
			GrenadeBlock = *Grenade;
		}
		const TSharedPtr<FJsonObject>* TurnRules = nullptr;
		if (Root->TryGetObjectField(TEXT("turn_based_rules"), TurnRules) && TurnRules)
		{
			TurnRulesBlock = *TurnRules;
		}
		const TSharedPtr<FJsonObject>* EnemyWeapons = nullptr;
		const TSharedPtr<FJsonObject>* Rifle = nullptr;
		if (Root->TryGetObjectField(TEXT("enemy_weapons"), EnemyWeapons) && EnemyWeapons
			&& (*EnemyWeapons)->TryGetObjectField(TEXT("marksman_rifle"), Rifle) && Rifle)
		{
			MarksmanRifleBlock = *Rifle;
		}
		const TSharedPtr<FJsonObject>* Weapons = nullptr;
		if (!Root->TryGetObjectField(TEXT("weapons"), Weapons) || !Weapons)
		{
			return 0;
		}
		int32 Applied = 0;
		for (UWeaponDataAsset* Weapon : LoadWeapons())
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if ((*Weapons)->TryGetObjectField(Weapon->WeaponId, Entry) && Entry)
			{
				ApplyWeapon(**Entry, *Weapon);
				++Applied;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Weapon tuning: %d weapons%s from %s"), Applied, GrenadeBlock.IsValid() ? TEXT(" + grenades") : TEXT(""), *Path);
		return Applied;
	}

	void ApplyGrenades(AOperativeCharacter& Operative)
	{
		if (!GrenadeBlock.IsValid())
		{
			return;
		}
		Number(*GrenadeBlock, TEXT("damage"), Operative.GrenadeDamage);
		Number(*GrenadeBlock, TEXT("effect_radius_m"), Operative.GrenadeEffectRadius, 100.f);
		Number(*GrenadeBlock, TEXT("throw_range_m"), Operative.GrenadeThrowRange, 100.f);
		Integer(*GrenadeBlock, TEXT("max_carried"), Operative.MaxGrenades);
	}

	void ApplyMarksmanRifle(FMarksmanConfig& Config)
	{
		if (!MarksmanRifleBlock.IsValid())
		{
			return;
		}
		const FJsonObject& Json = *MarksmanRifleBlock;
		Number(Json, TEXT("damage"), Config.ShotDamage);
		Number(Json, TEXT("base_accuracy"), Config.BaseAccuracy);
		Number(Json, TEXT("aim_duration"), Config.AimDuration);
		Number(Json, TEXT("shot_cooldown"), Config.ShotCooldown);
		Number(Json, TEXT("crit_chance"), Config.CritChance);
		Number(Json, TEXT("crit_multiplier"), Config.CritMultiplier);
		Number(Json, TEXT("prone_accuracy_bonus"), Config.ProneAccuracyBonus);
		Number(Json, TEXT("crouch_accuracy_bonus"), Config.CrouchAccuracyBonus);
		Number(Json, TEXT("preferred_min_range_m"), Config.PreferredMinRange, 100.f);
		Number(Json, TEXT("preferred_max_range_m"), Config.PreferredMaxRange, 100.f);
	}

	void ApplyTurnRules(FTurnBasedBalance& Balance)
	{
		if (!TurnRulesBlock.IsValid())
		{
			return;
		}
		Integer(*TurnRulesBlock, TEXT("crouch_move_cost_multiplier"), Balance.CrouchMoveCostMultiplier);
		Number(*TurnRulesBlock, TEXT("cover_fire_accuracy_multiplier"), Balance.CoverFireAccuracyMultiplier);
		Number(*TurnRulesBlock, TEXT("enemy_fire_at_cover_multiplier"), Balance.EnemyFireAtCoverMultiplier);
		Balance.CrouchMoveCostMultiplier = FMath::Max(1, Balance.CrouchMoveCostMultiplier);
	}

	void ApplyEnemyTurnWeapon(EEnemyArchetype Archetype, FEnemyTurnProfile& Profile)
	{
		if (Archetype != EEnemyArchetype::Marksman || !MarksmanRifleBlock.IsValid())
		{
			return;
		}
		const FJsonObject& Json = *MarksmanRifleBlock;
		Number(Json, TEXT("tb_damage_scale"), Profile.DamageScale);
		Integer(Json, TEXT("tb_attack_ap"), Profile.AttackAPCost);
		Integer(Json, TEXT("tb_min_range_cells"), Profile.MinRange);
		Integer(Json, TEXT("tb_max_range_cells"), Profile.MaxRange);
		Number(Json, TEXT("tb_base_hit_chance"), Profile.BaseHitChance);
		Number(Json, TEXT("tb_hit_falloff_per_cell"), Profile.HitFalloffPerCell);
		Profile.PreferredMin = FMath::Clamp(Profile.PreferredMin, Profile.MinRange, Profile.MaxRange);
		Profile.PreferredMax = FMath::Clamp(Profile.PreferredMax, Profile.PreferredMin, Profile.MaxRange);
	}

	bool Dump(const FString& Path, const AOperativeCharacter* GrenadeDefaults, const FMarksmanConfig* MarksmanDefaults)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("comment"), TEXT("Weapon power (Wave Editor «Оружие»). Applied at game start onto DA_Weapon_* and the squad's grenades. Metres / seconds."));
		TSharedRef<FJsonObject> Weapons = MakeShared<FJsonObject>();
		TArray<UWeaponDataAsset*> All = LoadWeapons();
		All.Sort([](const UWeaponDataAsset& A, const UWeaponDataAsset& B) { return A.WeaponId < B.WeaponId; });
		for (const UWeaponDataAsset* Weapon : All)
		{
			Weapons->SetObjectField(Weapon->WeaponId, WeaponToJson(*Weapon));
		}
		Root->SetObjectField(TEXT("weapons"), Weapons);
		TSharedRef<FJsonObject> Grenade = MakeShared<FJsonObject>();
		const AOperativeCharacter* Defaults = GrenadeDefaults ? GrenadeDefaults : GetDefault<AOperativeCharacter>();
		Grenade->SetNumberField(TEXT("damage"), Round4(Defaults->GrenadeDamage));
		Grenade->SetNumberField(TEXT("effect_radius_m"), Round4(Defaults->GrenadeEffectRadius / 100.f));
		Grenade->SetNumberField(TEXT("throw_range_m"), Round4(Defaults->GrenadeThrowRange / 100.f));
		Grenade->SetNumberField(TEXT("max_carried"), Round4(Defaults->MaxGrenades));
		Root->SetObjectField(TEXT("grenade"), Grenade);
		// Enemy ranged weapons: the marksman's rifle (real time from its Blueprint defaults, turn based from EnemyTurnRules).
		const FMarksmanConfig Marksman = MarksmanDefaults ? *MarksmanDefaults : FMarksmanConfig();
		const FEnemyTurnProfile Turn = EnemyTurnRules::ProfileFor(EEnemyArchetype::Marksman);
		TSharedRef<FJsonObject> Rifle = MakeShared<FJsonObject>();
		Rifle->SetNumberField(TEXT("damage"), Round4(Marksman.ShotDamage));
		Rifle->SetNumberField(TEXT("base_accuracy"), Round4(Marksman.BaseAccuracy));
		Rifle->SetNumberField(TEXT("aim_duration"), Round4(Marksman.AimDuration));
		Rifle->SetNumberField(TEXT("shot_cooldown"), Round4(Marksman.ShotCooldown));
		Rifle->SetNumberField(TEXT("crit_chance"), Round4(Marksman.CritChance));
		Rifle->SetNumberField(TEXT("crit_multiplier"), Round4(Marksman.CritMultiplier));
		Rifle->SetNumberField(TEXT("prone_accuracy_bonus"), Round4(Marksman.ProneAccuracyBonus));
		Rifle->SetNumberField(TEXT("crouch_accuracy_bonus"), Round4(Marksman.CrouchAccuracyBonus));
		Rifle->SetNumberField(TEXT("preferred_min_range_m"), Round4(Marksman.PreferredMinRange / 100.f));
		Rifle->SetNumberField(TEXT("preferred_max_range_m"), Round4(Marksman.PreferredMaxRange / 100.f));
		Rifle->SetNumberField(TEXT("tb_damage_scale"), Round4(Turn.DamageScale));
		Rifle->SetNumberField(TEXT("tb_attack_ap"), Round4(Turn.AttackAPCost));
		Rifle->SetNumberField(TEXT("tb_min_range_cells"), Round4(Turn.MinRange));
		Rifle->SetNumberField(TEXT("tb_max_range_cells"), Round4(Turn.MaxRange));
		Rifle->SetNumberField(TEXT("tb_base_hit_chance"), Round4(Turn.BaseHitChance));
		Rifle->SetNumberField(TEXT("tb_hit_falloff_per_cell"), Round4(Turn.HitFalloffPerCell));
		const FTurnBasedBalance Rules;
		TSharedRef<FJsonObject> TurnRules = MakeShared<FJsonObject>();
		TurnRules->SetNumberField(TEXT("crouch_move_cost_multiplier"), Round4(Rules.CrouchMoveCostMultiplier));
		TurnRules->SetNumberField(TEXT("cover_fire_accuracy_multiplier"), Round4(Rules.CoverFireAccuracyMultiplier));
		TurnRules->SetNumberField(TEXT("enemy_fire_at_cover_multiplier"), Round4(Rules.EnemyFireAtCoverMultiplier));
		Root->SetObjectField(TEXT("turn_based_rules"), TurnRules);
		TSharedRef<FJsonObject> EnemyWeapons = MakeShared<FJsonObject>();
		EnemyWeapons->SetObjectField(TEXT("marksman_rifle"), Rifle);
		Root->SetObjectField(TEXT("enemy_weapons"), EnemyWeapons);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	static FAutoConsoleCommandWithWorldAndArgs DumpCommand(TEXT("CodexTactics.DumpWeaponTuning"),
		TEXT("Writes Content/Data/Weapons/weapons_tuning.json from the current weapon assets (Wave Editor weapon tabs)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const AOperativeCharacter* Operative = nullptr;
			if (World)
			{
				for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
				{
					Operative = *It;
					break;
				}
			}
			// The marksman's rifle from its Blueprint (the game mode's enemy class), else the C++ defaults.
			const FMarksmanConfig* Marksman = nullptr;
			if (const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr)
			{
				if (const TSoftClassPtr<AEnemyCharacter>* Soft = GameMode->EnemyClasses.Find(EEnemyArchetype::Marksman))
				{
					if (const UClass* Class = Soft->LoadSynchronous())
					{
						if (const AMarksmanEnemyCharacter* Default = Cast<AMarksmanEnemyCharacter>(Class->GetDefaultObject()))
						{
							Marksman = &Default->MarksmanConfig;
						}
					}
				}
			}
			const FString Path = Args.Num() > 0 ? Args[0] : GetDefaultPath();
			UE_LOG(LogCodexTactics, Display, TEXT("Weapon tuning dump -> %s: %s"), *Path, Dump(Path, Operative, Marksman) ? TEXT("ok") : TEXT("FAILED"));
		}));
}
