#include "Data/WeaponTuning.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/MarksmanAIRules.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Combat/ArmorRules.h"
#include "Combat/WeaponRangeRules.h"
#include "Core/CodexTacticsGameMode.h"
#include "Tactics/EnemyTurnRules.h"
#include "Tactics/TurnBasedRules.h"
#include "Vehicles/VehicleCombatRules.h"
#include "Vehicles/VehicleHullRules.h"
#include "CodexTactics.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponStatsRules.h"
#include "Data/WeaponRoster.h"
#include "Data/WeaponLoadoutRules.h"
#include "Combat/SidearmRules.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Data/GodotBalanceAsset.h"
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
		TSharedPtr<FJsonObject> VehicleTurretBlock;
		TSharedPtr<FJsonObject> VehicleRamBlock;
		TSharedPtr<FJsonObject> VehicleHullBlock;
		FWeaponTuningReport LastWeaponReport;

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

	bool ApplyWeapon(const FJsonObject& Json, UWeaponDataAsset& Weapon, TArray<FString>* OutErrors, TArray<FString>* OutUnknown)
	{
		// The schema (WeaponStatsRules) parses onto a copy; only valid data replaces the weapon's stats.
		FWeaponStats Tuned = Weapon.Stats;
		TArray<FString> Errors;
		WeaponStatsRules::ApplyJson(Json, Tuned, OutUnknown, &Errors);
		WeaponStatsRules::Validate(Tuned, Errors, Weapon.WeaponId);
		if (OutErrors)
		{
			OutErrors->Append(Errors);
		}
		if (Errors.Num() > 0)
		{
			return false;
		}
		Weapon.Stats = MoveTemp(Tuned);
		return true;
	}

	TSharedRef<FJsonObject> WeaponToJson(const UWeaponDataAsset& Weapon)
	{
		TSharedRef<FJsonObject> Json = WeaponStatsRules::ToJson(Weapon.Stats);
		Json->SetStringField(TEXT("name"), Weapon.WeaponName.ToString());
		return Json;
	}

	const FWeaponTuningReport& GetLastReport()
	{
		return LastWeaponReport;
	}

	bool ApplyWeaponPresentation(const FJsonObject& Json, UWeaponDataAsset& Weapon)
	{
		bool bChanged = false;
		FString Name;
		if (Json.TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			Weapon.WeaponName = FText::FromString(Name); // "D-1 Shotgun" (the id stays "shotgun": saves and loadouts keep working)
			bChanged = true;
		}
		const TSharedPtr<FJsonObject>* Visual = nullptr;
		if (!Json.TryGetObjectField(TEXT("visual"), Visual) || !Visual || !Visual->IsValid())
		{
			return bChanged;
		}
		FString MeshPath;
		if ((*Visual)->TryGetStringField(TEXT("hand_mesh"), MeshPath) && !MeshPath.IsEmpty())
		{
			// A static model goes to HandMesh, a skeletal one (the D-1 is a Fab skeletal mesh with its own pump clips) to HandSkeletalMesh.
			if (UObject* Loaded = LoadObject<UObject>(nullptr, *MeshPath))
			{
				if (UStaticMesh* Static = Cast<UStaticMesh>(Loaded))
				{
					Weapon.HandMesh = Static;
					Weapon.HandSkeletalMesh.Reset();
				}
				else if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Loaded))
				{
					Weapon.HandSkeletalMesh = Skeletal;
					Weapon.HandMesh.Reset();
				}
				bChanged = true;
			}
			else
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: \"%s\" visual.hand_mesh %s not found"), *Weapon.WeaponId, *MeshPath);
			}
		}
		auto ReadVector = [&Visual](const TCHAR* Key, FVector& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if ((*Visual)->TryGetArrayField(Key, Array) && Array && Array->Num() == 3)
			{
				Out = FVector((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
				return true;
			}
			return false;
		};
		FVector Location = Weapon.HandMeshAttachTransform.GetLocation();
		FVector Rotation(Weapon.HandMeshAttachTransform.Rotator().Pitch, Weapon.HandMeshAttachTransform.Rotator().Yaw,
			Weapon.HandMeshAttachTransform.Rotator().Roll);
		FVector Scale = Weapon.HandMeshAttachTransform.GetScale3D();
		const bool bLocation = ReadVector(TEXT("attach_location"), Location);
		const bool bRotation = ReadVector(TEXT("attach_rotation_pyr"), Rotation);
		const bool bScale = ReadVector(TEXT("attach_scale"), Scale);
		if (bLocation || bRotation || bScale)
		{
			Weapon.bOverrideHandMeshTransform = true;
			Weapon.HandMeshAttachTransform = FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Location, Scale);
			bChanged = true;
		}
		FVector Muzzle;
		if (ReadVector(TEXT("muzzle_offset"), Muzzle))
		{
			Weapon.HandMeshMuzzleOffset = Muzzle;
			bChanged = true;
		}
		return bChanged;
	}

	int32 ApplyFile(const FString& Path)
	{
		GrenadeBlock.Reset();
		MarksmanRifleBlock.Reset();
		TurnRulesBlock.Reset();
		LastWeaponReport = FWeaponTuningReport();
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return 0;
		}
		double FileSchema = 1.0;
		LastWeaponReport.SchemaVersion = Root->TryGetNumberField(TEXT("schema_version"), FileSchema) ? FMath::RoundToInt(FileSchema) : 1;
		// The squad's per-role loadouts (designer decision 21+: the Engineer = the D-1 shotgun); no block = the Blueprint loadouts.
		{
			const TSharedPtr<FJsonObject>* LoadoutBlock = nullptr;
			TArray<FString> LoadoutErrors;
			WeaponLoadoutRules::Set(Root->TryGetObjectField(TEXT("loadouts"), LoadoutBlock) && LoadoutBlock && LoadoutBlock->IsValid()
				? WeaponLoadoutRules::Parse(**LoadoutBlock, &LoadoutErrors) : TMap<FString, TArray<FString>>());
			for (const FString& Error : LoadoutErrors)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: %s"), *Error);
			}
			LastWeaponReport.Loadouts = WeaponLoadoutRules::Get().Num();
		}
		// The sidearm switch (the whole pistol, 2026-11-08): no block = the defaults (on, the pistol, 10 m, 0.35 s draw, back after 3 s).
		{
			const TSharedPtr<FJsonObject>* SidearmBlock = nullptr;
			TArray<FString> SidearmErrors;
			SidearmRules::Set(Root->TryGetObjectField(TEXT("sidearm"), SidearmBlock) && SidearmBlock && SidearmBlock->IsValid()
				? SidearmRules::Parse(**SidearmBlock, &SidearmErrors) : FSidearmConfig());
			for (const FString& Error : SidearmErrors)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: %s"), *Error);
			}
		}
		// The designer's roster (2026-11-08): "roster.out" weapons are out of play (WeaponRoster); no block = the roster stays as it is.
		{
			const TSharedPtr<FJsonObject>* RosterBlock = nullptr;
			if (Root->TryGetObjectField(TEXT("roster"), RosterBlock) && RosterBlock && RosterBlock->IsValid())
			{
				FWeaponRoster Roster;
				TArray<FString> RosterErrors;
				WeaponRoster::Parse(**RosterBlock, Roster, &RosterErrors);
				for (const FString& Error : RosterErrors)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: %s"), *Error);
				}
				WeaponRoster::Set(Roster);
			}
			LastWeaponReport.RosterOut = WeaponRoster::Get().Out.Num();
		}
		// Body armour (weapons framework, decision 1 2026-11-08): "armor.bodies" - class + reduction per body; read by the enemies /
		// operatives as they spawn (ArmorRules::ApplyBody). No block = the C++ fallbacks.
		{
			TMap<FString, FBodyArmor> Bodies;
			const TSharedPtr<FJsonObject>* Armor = nullptr;
			if (Root->TryGetObjectField(TEXT("armor"), Armor) && Armor && Armor->IsValid())
			{
				ArmorRules::ParseBodies(**Armor, Bodies, &LastWeaponReport.ArmorErrors);
			}
			for (const FString& Error : LastWeaponReport.ArmorErrors)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: %s (that body keeps its code value)"), *Error);
			}
			LastWeaponReport.ArmorBodies = Bodies.Num();
			ArmorRules::SetBodies(Bodies);
		}
		const TSharedPtr<FJsonObject>* Grenade = nullptr;
		if (Root->TryGetObjectField(TEXT("grenade"), Grenade) && Grenade)
		{
			GrenadeBlock = *Grenade;
		}
		// Bug #39: the accuracy falloff from a weapon's effective range to its max reach (shared by every weapon).
		const TSharedPtr<FJsonObject>* RangeFalloff = nullptr;
		if (Root->TryGetObjectField(TEXT("range_falloff"), RangeFalloff) && RangeFalloff)
		{
			FWeaponRangeFalloff Falloff;
			Number(**RangeFalloff, TEXT("floor_multiplier"), Falloff.FloorMultiplier);
			Number(**RangeFalloff, TEXT("exponent"), Falloff.Exponent);
			WeaponRangeRules::SetFalloff(Falloff);
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
		const TSharedPtr<FJsonObject>* VehicleWeapons = nullptr;
		if (Root->TryGetObjectField(TEXT("vehicle_weapons"), VehicleWeapons) && VehicleWeapons)
		{
			const TSharedPtr<FJsonObject>* Block = nullptr;
			if ((*VehicleWeapons)->TryGetObjectField(TEXT("apc_turret"), Block) && Block)
			{
				VehicleTurretBlock = *Block;
			}
			if ((*VehicleWeapons)->TryGetObjectField(TEXT("apc_ram"), Block) && Block)
			{
				VehicleRamBlock = *Block;
			}
			if ((*VehicleWeapons)->TryGetObjectField(TEXT("apc_hull"), Block) && Block)
			{
				VehicleHullBlock = *Block;
			}
		}
		const TSharedPtr<FJsonObject>* Weapons = nullptr;
		if (!Root->TryGetObjectField(TEXT("weapons"), Weapons) || !Weapons)
		{
			return 0;
		}
		// By name (DA_Weapon_<id>): at StartPlay of an uncooked game the asset registry may still be scanning, so a folder
		// listing can come back empty (it did: «0 weapons», the editor's tuning never reached the game).
		int32 Applied = 0;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Weapons)->Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Object) || !Object)
			{
				continue;
			}
			if (WeaponRoster::IsOutOfPlay(Entry.Key))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: \"%s\" is out of play (roster) - its block is ignored"), *Entry.Key);
				continue;
			}
			const FString Name = FString::Printf(TEXT("DA_Weapon_%s"), *Entry.Key);
			UWeaponDataAsset* Weapon = LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/%s.%s"), *Name, *Name));
			if (!Weapon)
			{
				for (UWeaponDataAsset* Candidate : LoadWeapons())
				{
					Weapon = Candidate->WeaponId == Entry.Key ? Candidate : Weapon;
				}
			}
			if (Weapon)
			{
				// Stats = the asset's base + this file (not on top of an earlier run's tuning: a key removed from the file reverts).
				Weapon->RebuildStatsFromAsset();
				TArray<FString> Errors;
				TArray<FString> Unknown;
				if (ApplyWeapon(**Object, *Weapon, &Errors, &Unknown))
				{
					++Applied;
				}
				else
				{
					LastWeaponReport.Rejected.Add(Entry.Key);
					UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: \"%s\" REJECTED, the asset's own values stay: %s"), *Entry.Key, *FString::Join(Errors, TEXT("; ")));
				}
				for (const FString& Key : Unknown)
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: \"%s\" has an unknown key \"%s\" (ignored; typo?)"), *Entry.Key, *Key);
				}
				LastWeaponReport.Errors.Append(Errors);
				ApplyWeaponPresentation(**Object, *Weapon); // display name + the model in the hand (not stats: never rejected)
				LastWeaponReport.Unknown.Append(Unknown);
			}
			else
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Weapon tuning: no weapon asset for \"%s\""), *Entry.Key);
			}
		}
		LastWeaponReport.Applied = Applied;
		UE_LOG(LogCodexTactics, Display, TEXT("Weapon tuning: %d weapons%s from %s (schema v%d, %d rejected)"), Applied, GrenadeBlock.IsValid() ? TEXT(" + grenades") : TEXT(""),
			*Path, LastWeaponReport.SchemaVersion, LastWeaponReport.Rejected.Num());
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
		Number(Json, TEXT("effective_range_m"), Config.EffectiveRangeCm, 100.f); // bug #39
		Number(Json, TEXT("max_range_m"), Config.MaxRangeCm, 100.f);
	}

	void ApplyVehicleWeapons(FVehicleTurretConfig* Turret, FVehicleCrushConfig* Crush, FVehicleHullConfig* Hull)
	{
		if (Turret && VehicleTurretBlock.IsValid())
		{
			const FJsonObject& Json = *VehicleTurretBlock;
			Number(Json, TEXT("cannon_damage"), Turret->CannonDamage);
			Number(Json, TEXT("cannon_splash_radius_m"), Turret->CannonSplashRadiusCm, 100.f);
			Number(Json, TEXT("cannon_splash_damage"), Turret->CannonSplashDamage);
			Number(Json, TEXT("cannon_shot_interval"), Turret->CannonShotIntervalSeconds);
			Integer(Json, TEXT("cannon_burst_shots"), Turret->CannonBurstShots);
			Number(Json, TEXT("cannon_burst_pause"), Turret->CannonBurstPauseSeconds);
			Number(Json, TEXT("cannon_range_m"), Turret->CannonRangeCm, 100.f);
			Integer(Json, TEXT("shells_per_map"), Turret->CannonShellsPerMap);
			Number(Json, TEXT("cannon_hit_chance"), Turret->CannonHitChance);
			Number(Json, TEXT("cannon_armour_factor"), Turret->CannonArmourFactor);
			Number(Json, TEXT("mg_damage"), Turret->MgDamage);
			Number(Json, TEXT("mg_rounds_per_second"), Turret->MgRoundsPerSecond);
			Number(Json, TEXT("mg_range_m"), Turret->MgRangeCm, 100.f);
			Number(Json, TEXT("mg_hit_chance"), Turret->MgHitChance);
			Number(Json, TEXT("mg_overheat_seconds"), Turret->MgOverheatSeconds);
			Number(Json, TEXT("mg_cooldown_seconds"), Turret->MgCooldownSeconds);
			Number(Json, TEXT("traverse_deg_per_sec"), Turret->TraverseDegPerSec);
			Number(Json, TEXT("dead_zone_m"), Turret->DeadZoneCm, 100.f);
			Number(Json, TEXT("defensive_radius_m"), Turret->DefensiveRadiusCm, 100.f);
		}
		if (Crush && VehicleRamBlock.IsValid())
		{
			const FJsonObject& Json = *VehicleRamBlock;
			Number(Json, TEXT("min_crush_speed_mps"), Crush->MinCrushSpeedCmS, 100.f);
			Number(Json, TEXT("crush_speed_factor"), Crush->CrushSpeedFactor);
			Number(Json, TEXT("ram_knockdown_speed_mps"), Crush->RamKnockdownSpeedCmS, 100.f);
			Number(Json, TEXT("ram_damage"), Crush->RamDamage);
			Number(Json, TEXT("ram_speed_factor"), Crush->RamSpeedFactor);
			Number(Json, TEXT("ram_knockdown_play_rate"), Crush->RamKnockdownPlayRate);
			Number(Json, TEXT("ram_downed_time_scale"), Crush->RamDownedTimeScale);
			Number(Json, TEXT("hull_ram_damage"), Crush->HullRamDamage);
			bool bCrushDowned = Crush->bCrushDownedRammed;
			if (Json.TryGetBoolField(TEXT("crush_downed_brute"), bCrushDowned))
			{
				Crush->bCrushDownedRammed = bCrushDowned;
			}
		}
		if (Hull && VehicleHullBlock.IsValid())
		{
			const FJsonObject& Json = *VehicleHullBlock;
			Number(Json, TEXT("max_hp"), Hull->MaxHealth);
			Number(Json, TEXT("small_melee_factor"), Hull->SmallMeleeFactor);
			Number(Json, TEXT("heavy_melee_factor"), Hull->HeavyMeleeFactor);
			Number(Json, TEXT("acid_factor"), Hull->AcidFactor);
			Number(Json, TEXT("rifle_factor"), Hull->RifleFactor);
			Number(Json, TEXT("explosion_factor"), Hull->ExplosionFactor);
			Number(Json, TEXT("mine_damage"), Hull->MineDamage);
			Number(Json, TEXT("other_factor"), Hull->OtherFactor);
			Number(Json, TEXT("low_health_fraction"), Hull->LowHealthFraction);
			Number(Json, TEXT("repair_seconds"), Hull->RepairSeconds);
			Number(Json, TEXT("other_repair_seconds"), Hull->OtherRepairSeconds);
			Number(Json, TEXT("repair_fraction"), Hull->RepairFraction);
			Number(Json, TEXT("enemy_weight_small"), Hull->EnemyTargetWeightSmall);
			Number(Json, TEXT("enemy_weight_large"), Hull->EnemyTargetWeightLarge);
			Number(Json, TEXT("enemy_awareness_m"), Hull->EnemyAwarenessCm, 100.f);
		}
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
		// Damage an operative takes crouched / prone (user decision 2026-10-04: tuned in the Wave Editor, over DA_Balance).
		Number(*TurnRulesBlock, TEXT("crouch_damage_multiplier"), Balance.CrouchDamageMultiplier);
		Number(*TurnRulesBlock, TEXT("prone_damage_multiplier"), Balance.ProneDamageMultiplier);
		// Grid blasts on enemies / mines (user decision 2026-10-11, docs/port/tb_parity_2026-10-11.md): over the code defaults.
		Number(*TurnRulesBlock, TEXT("barrel_damage_enemies"), Balance.BarrelDamageEnemies);
		Number(*TurnRulesBlock, TEXT("mine_damage_enemies"), Balance.MineDamageEnemies);
		Number(*TurnRulesBlock, TEXT("mine_damage_operatives"), Balance.MineDamageOperatives);
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

	bool Dump(const FString& Path, const AOperativeCharacter* GrenadeDefaults, const FMarksmanConfig* MarksmanDefaults,
		const FTurnBasedBalance* TurnBalanceDefaults)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("comment"), TEXT("Weapon power (Wave Editor \"Weapons\"). Applied at game start onto DA_Weapon_* and the squad's grenades. Metres / seconds."));
		Root->SetNumberField(TEXT("schema_version"), WeaponStatsRules::SchemaVersion);
		TSharedRef<FJsonObject> Weapons = MakeShared<FJsonObject>();
		TArray<UWeaponDataAsset*> All = LoadWeapons();
		All.Sort([](const UWeaponDataAsset& A, const UWeaponDataAsset& B) { return A.WeaponId < B.WeaponId; });
		for (const UWeaponDataAsset* Weapon : All)
		{
			if (!WeaponRoster::IsOutOfPlay(Weapon->WeaponId)) // the designer's roster: the out-of-play assets stay on disk, not in the file
			{
				Weapons->SetObjectField(Weapon->WeaponId, WeaponToJson(*Weapon));
			}
		}
		{
			const FWeaponRoster& Roster = WeaponRoster::Get();
			TSharedRef<FJsonObject> RosterJson = MakeShared<FJsonObject>();
			auto List = [](const TArray<FString>& Ids)
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				for (const FString& Id : Ids)
				{
					Out.Add(MakeShared<FJsonValueString>(Id));
				}
				return Out;
			};
			RosterJson->SetArrayField(TEXT("in_play"), List(Roster.InPlay));
			RosterJson->SetArrayField(TEXT("next"), List(Roster.Next));
			RosterJson->SetArrayField(TEXT("out"), List(Roster.Out));
			Root->SetObjectField(TEXT("roster"), RosterJson);
		}
		Root->SetObjectField(TEXT("weapons"), Weapons);
		// Body armour: the table in force (the file's, read at the last ApplyFile).
		{
			TSharedRef<FJsonObject> Armor = MakeShared<FJsonObject>();
			Armor->SetStringField(TEXT("comment"), TEXT("Armour per body (decision 1, 2026-11-08): class none / light / medium / heavy / vehicle + the share of damage it stops; a weapon of a lower penetration_class gets no armor_penetration against it."));
			TSharedRef<FJsonObject> Bodies = MakeShared<FJsonObject>();
			TArray<FString> Keys;
			ArmorRules::GetBodies().GetKeys(Keys);
			Keys.Sort();
			for (const FString& Key : Keys)
			{
				const FBodyArmor& Body = ArmorRules::GetBodies()[Key];
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("class"), WeaponStatsRules::ArmorClassName(Body.Class));
				Entry->SetNumberField(TEXT("reduction"), Round4(Body.Reduction));
				Bodies->SetObjectField(Key, Entry);
			}
			Armor->SetObjectField(TEXT("bodies"), Bodies);
			Root->SetObjectField(TEXT("armor"), Armor);
		}
		TSharedRef<FJsonObject> RangeFalloff = MakeShared<FJsonObject>();
		RangeFalloff->SetNumberField(TEXT("floor_multiplier"), Round4(WeaponRangeRules::GetFalloff().FloorMultiplier));
		RangeFalloff->SetNumberField(TEXT("exponent"), Round4(WeaponRangeRules::GetFalloff().Exponent));
		Root->SetObjectField(TEXT("range_falloff"), RangeFalloff);
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
		Rifle->SetNumberField(TEXT("effective_range_m"), Round4(Marksman.EffectiveRangeCm / 100.f));
		Rifle->SetNumberField(TEXT("max_range_m"), Round4(Marksman.MaxRangeCm / 100.f));
		Rifle->SetNumberField(TEXT("tb_damage_scale"), Round4(Turn.DamageScale));
		Rifle->SetNumberField(TEXT("tb_attack_ap"), Round4(Turn.AttackAPCost));
		Rifle->SetNumberField(TEXT("tb_min_range_cells"), Round4(Turn.MinRange));
		Rifle->SetNumberField(TEXT("tb_max_range_cells"), Round4(Turn.MaxRange));
		Rifle->SetNumberField(TEXT("tb_base_hit_chance"), Round4(Turn.BaseHitChance));
		Rifle->SetNumberField(TEXT("tb_hit_falloff_per_cell"), Round4(Turn.HitFalloffPerCell));
		// The turn-based balance the game uses (DA_Balance through the game mode), else the C++ defaults.
		FTurnBasedBalance Rules;
		if (TurnBalanceDefaults)
		{
			Rules = *TurnBalanceDefaults;
		}
		TSharedRef<FJsonObject> TurnRules = MakeShared<FJsonObject>();
		TurnRules->SetNumberField(TEXT("crouch_move_cost_multiplier"), Round4(Rules.CrouchMoveCostMultiplier));
		TurnRules->SetNumberField(TEXT("cover_fire_accuracy_multiplier"), Round4(Rules.CoverFireAccuracyMultiplier));
		TurnRules->SetNumberField(TEXT("enemy_fire_at_cover_multiplier"), Round4(Rules.EnemyFireAtCoverMultiplier));
		TurnRules->SetNumberField(TEXT("crouch_damage_multiplier"), Round4(Rules.CrouchDamageMultiplier));
		TurnRules->SetNumberField(TEXT("prone_damage_multiplier"), Round4(Rules.ProneDamageMultiplier));
		Root->SetObjectField(TEXT("turn_based_rules"), TurnRules);
		TSharedRef<FJsonObject> EnemyWeapons = MakeShared<FJsonObject>();
		EnemyWeapons->SetObjectField(TEXT("marksman_rifle"), Rifle);
		Root->SetObjectField(TEXT("enemy_weapons"), EnemyWeapons);
		// The APC turret and ram (P2): the C++ defaults with the file values applied (the vehicle Blueprint may differ).
		FVehicleTurretConfig Turret;
		FVehicleCrushConfig Crush;
		FVehicleHullConfig Hull;
		ApplyVehicleWeapons(&Turret, &Crush, &Hull);
		TSharedRef<FJsonObject> ApcTurret = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ApcRam = MakeShared<FJsonObject>();
		ApcTurret->SetNumberField(TEXT("cannon_damage"), Round4(Turret.CannonDamage));
		ApcTurret->SetNumberField(TEXT("cannon_splash_radius_m"), Round4(Turret.CannonSplashRadiusCm / 100.f));
		ApcTurret->SetNumberField(TEXT("cannon_splash_damage"), Round4(Turret.CannonSplashDamage));
		ApcTurret->SetNumberField(TEXT("cannon_shot_interval"), Round4(Turret.CannonShotIntervalSeconds));
		ApcTurret->SetNumberField(TEXT("cannon_burst_shots"), Turret.CannonBurstShots);
		ApcTurret->SetNumberField(TEXT("cannon_burst_pause"), Round4(Turret.CannonBurstPauseSeconds));
		ApcTurret->SetNumberField(TEXT("cannon_range_m"), Round4(Turret.CannonRangeCm / 100.f));
		ApcTurret->SetNumberField(TEXT("shells_per_map"), Turret.CannonShellsPerMap);
		ApcTurret->SetNumberField(TEXT("cannon_hit_chance"), Round4(Turret.CannonHitChance));
		ApcTurret->SetNumberField(TEXT("cannon_armour_factor"), Round4(Turret.CannonArmourFactor));
		ApcTurret->SetNumberField(TEXT("mg_damage"), Round4(Turret.MgDamage));
		ApcTurret->SetNumberField(TEXT("mg_rounds_per_second"), Round4(Turret.MgRoundsPerSecond));
		ApcTurret->SetNumberField(TEXT("mg_range_m"), Round4(Turret.MgRangeCm / 100.f));
		ApcTurret->SetNumberField(TEXT("mg_hit_chance"), Round4(Turret.MgHitChance));
		ApcTurret->SetNumberField(TEXT("mg_overheat_seconds"), Round4(Turret.MgOverheatSeconds));
		ApcTurret->SetNumberField(TEXT("mg_cooldown_seconds"), Round4(Turret.MgCooldownSeconds));
		ApcTurret->SetNumberField(TEXT("traverse_deg_per_sec"), Round4(Turret.TraverseDegPerSec));
		ApcTurret->SetNumberField(TEXT("dead_zone_m"), Round4(Turret.DeadZoneCm / 100.f));
		ApcTurret->SetNumberField(TEXT("defensive_radius_m"), Round4(Turret.DefensiveRadiusCm / 100.f));
		ApcRam->SetNumberField(TEXT("min_crush_speed_mps"), Round4(Crush.MinCrushSpeedCmS / 100.f));
		ApcRam->SetNumberField(TEXT("crush_speed_factor"), Round4(Crush.CrushSpeedFactor));
		ApcRam->SetNumberField(TEXT("ram_knockdown_speed_mps"), Round4(Crush.RamKnockdownSpeedCmS / 100.f));
		ApcRam->SetNumberField(TEXT("ram_damage"), Round4(Crush.RamDamage));
		ApcRam->SetNumberField(TEXT("ram_speed_factor"), Round4(Crush.RamSpeedFactor));
		ApcRam->SetNumberField(TEXT("ram_knockdown_play_rate"), Round4(Crush.RamKnockdownPlayRate));
		ApcRam->SetNumberField(TEXT("ram_downed_time_scale"), Round4(Crush.RamDownedTimeScale));
		ApcRam->SetBoolField(TEXT("crush_downed_brute"), Crush.bCrushDownedRammed);
		ApcRam->SetNumberField(TEXT("hull_ram_damage"), Round4(Crush.HullRamDamage));
		TSharedRef<FJsonObject> ApcHull = MakeShared<FJsonObject>();
		ApcHull->SetNumberField(TEXT("max_hp"), Round4(Hull.MaxHealth));
		ApcHull->SetNumberField(TEXT("small_melee_factor"), Round4(Hull.SmallMeleeFactor));
		ApcHull->SetNumberField(TEXT("heavy_melee_factor"), Round4(Hull.HeavyMeleeFactor));
		ApcHull->SetNumberField(TEXT("acid_factor"), Round4(Hull.AcidFactor));
		ApcHull->SetNumberField(TEXT("rifle_factor"), Round4(Hull.RifleFactor));
		ApcHull->SetNumberField(TEXT("explosion_factor"), Round4(Hull.ExplosionFactor));
		ApcHull->SetNumberField(TEXT("mine_damage"), Round4(Hull.MineDamage));
		ApcHull->SetNumberField(TEXT("other_factor"), Round4(Hull.OtherFactor));
		ApcHull->SetNumberField(TEXT("low_health_fraction"), Round4(Hull.LowHealthFraction));
		ApcHull->SetNumberField(TEXT("repair_seconds"), Round4(Hull.RepairSeconds));
		ApcHull->SetNumberField(TEXT("other_repair_seconds"), Round4(Hull.OtherRepairSeconds));
		ApcHull->SetNumberField(TEXT("repair_fraction"), Round4(Hull.RepairFraction));
		ApcHull->SetNumberField(TEXT("enemy_weight_small"), Round4(Hull.EnemyTargetWeightSmall));
		ApcHull->SetNumberField(TEXT("enemy_weight_large"), Round4(Hull.EnemyTargetWeightLarge));
		ApcHull->SetNumberField(TEXT("enemy_awareness_m"), Round4(Hull.EnemyAwarenessCm / 100.f));
		TSharedRef<FJsonObject> VehicleWeapons = MakeShared<FJsonObject>();
		VehicleWeapons->SetObjectField(TEXT("apc_turret"), ApcTurret);
		VehicleWeapons->SetObjectField(TEXT("apc_ram"), ApcRam);
		VehicleWeapons->SetObjectField(TEXT("apc_hull"), ApcHull);
		Root->SetObjectField(TEXT("vehicle_weapons"), VehicleWeapons);
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
			// The turn-based balance from the game mode's DA_Balance (BalanceFromGodot), the stance damage values with it.
			TOptional<FTurnBasedBalance> TurnBalance;
			if (const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr)
			{
				if (const UGodotBalanceAsset* BalanceAsset = GameMode->TurnBasedBalance.LoadSynchronous())
				{
					TurnBalance = TurnBasedRules::BalanceFromGodot(BalanceAsset);
				}
			}
			const FString Path = Args.Num() > 0 ? Args[0] : GetDefaultPath();
			UE_LOG(LogCodexTactics, Display, TEXT("Weapon tuning dump -> %s: %s"), *Path,
				Dump(Path, Operative, Marksman, TurnBalance.IsSet() ? &TurnBalance.GetValue() : nullptr) ? TEXT("ok") : TEXT("FAILED"));
		}));
}
