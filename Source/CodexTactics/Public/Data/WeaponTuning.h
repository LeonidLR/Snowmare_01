#pragma once

#include "CoreMinimal.h"

class AOperativeCharacter;
class FJsonObject;
class UWeaponDataAsset;
struct FMarksmanConfig;
struct FEnemyTurnProfile;
struct FTurnBasedBalance;
struct FVehicleTurretConfig;
struct FVehicleCrushConfig;
struct FVehicleHullConfig;
enum class EEnemyArchetype : uint8;

/**
 * Weapon power tuned in the Wave Editor (user request 2026-10-04: per-weapon tabs — pistol, M16, grenades, ...).
 * Content/Data/Weapons/weapons_tuning.json is the master copy of the numbers (like the level JSON): the game mode
 * applies it at StartPlay onto the loaded DA_Weapon_* assets (in memory) and onto each operative's grenades.
 * `CodexTactics.DumpWeaponTuning` writes the file from the current assets (the editor's starting point).
 * Units: metres and seconds (Godot / editor), converted to cm here.
 *
 * {"weapons": {"m16": {"base_damage": 18, "attack_range_m": 14, "fire_rate": 0.65, "armor_penetration": 0.2,
 *   "max_clip_size": 30, "default_reserve_ammo": 100, "reload_time": 2.68, "status_duration": 0, "status_tick_damage": 0,
 *   "max_range_cells": 5, "base_hit_chances": [..], "distance_damage_multipliers": [..]}, ...},
 *  "grenade": {"damage": 85, "effect_radius_m": 4, "throw_range_m": 12, "max_carried": 4},
 *  "enemy_weapons": {"marksman_rifle": {"damage": 45, "base_accuracy": 0.6, "aim_duration": 2, "shot_cooldown": 2.5,
 *    "crit_chance": 0.25, "crit_multiplier": 2, "prone_accuracy_bonus": 1.35, "crouch_accuracy_bonus": 1.15,
 *    "preferred_min_range_m": 20, "preferred_max_range_m": 35, "tb_damage_scale": 1.6, "tb_attack_ap": 4,
 *    "tb_min_range_cells": 3, "tb_max_range_cells": 10, "tb_base_hit_chance": 0.8, "tb_hit_falloff_per_cell": 0.03}}}
 * Enemy ranged weapons (user request 2026-10-04, Wave Editor «Оружие врагов»; for now the marksman's rifle; later the
 * spitter's acid and other projectiles). The Codex.Marksman.* console variables (Jev coach) still win over the file.
 */
namespace WeaponTuning
{
	CODEXTACTICS_API FString GetDefaultPath();

	/** Applies one weapon's object onto Weapon (missing keys keep the asset value). */
	CODEXTACTICS_API void ApplyWeapon(const FJsonObject& Json, UWeaponDataAsset& Weapon);
	/** The weapon's tunable values as a JSON object (the dump). */
	CODEXTACTICS_API TSharedRef<FJsonObject> WeaponToJson(const UWeaponDataAsset& Weapon);

	/** Reads the file (cached for ApplyGrenades) and tunes every DA_Weapon_* in /Game/Data/Weapons; returns the count. */
	CODEXTACTICS_API int32 ApplyFile(const FString& Path);
	/** The file's "grenade" block onto an operative (after OperativeBalance, before BeginPlay). */
	CODEXTACTICS_API void ApplyGrenades(AOperativeCharacter& Operative);
	/** The file's enemy_weapons.marksman_rifle onto a marksman's config (BeginPlay, before the Codex.Marksman.* overrides). */
	CODEXTACTICS_API void ApplyMarksmanRifle(FMarksmanConfig& Config);
	/**
	 * The file's turn_based_rules onto the turn-based balance (user decisions 2026-10-04): crouch_move_cost_multiplier,
	 * cover_fire_accuracy_multiplier, enemy_fire_at_cover_multiplier, crouch_damage_multiplier, prone_damage_multiplier
	 * (the last two over DA_Balance's tactical_stance_*_dmg_mult).
	 */
	CODEXTACTICS_API void ApplyTurnRules(FTurnBasedBalance& Balance);
	/**
	 * The file's "vehicle_weapons" (APC P2, UE-only): "apc_turret" onto Turret (cannon_damage, cannon_splash_radius_m,
	 * cannon_splash_damage, cannon_shot_interval, cannon_burst_shots, cannon_burst_pause, cannon_range_m, shells_per_map,
	 * cannon_hit_chance, cannon_armour_factor, mg_damage, mg_rounds_per_second, mg_range_m, mg_hit_chance,
	 * mg_overheat_seconds, mg_cooldown_seconds,
	 * traverse_deg_per_sec, dead_zone_m, defensive_radius_m) and "apc_ram" onto Crush (min_crush_speed_mps,
	 * crush_speed_factor, ram_knockdown_speed_mps, ram_damage, ram_speed_factor, ram_knockdown_play_rate,
	 * ram_downed_time_scale, crush_downed_brute, hull_ram_damage) and (P4) "apc_hull" onto Hull (max_hp, small_melee_factor,
	 * heavy_melee_factor, acid_factor, rifle_factor, explosion_factor, mine_damage, other_factor, low_health_fraction,
	 * repair_seconds, other_repair_seconds, repair_fraction, enemy_weight_small, enemy_weight_large, enemy_awareness_m).
	 * Any may be null; missing keys keep the Blueprint values.
	 */
	CODEXTACTICS_API void ApplyVehicleWeapons(FVehicleTurretConfig* Turret, FVehicleCrushConfig* Crush, FVehicleHullConfig* Hull = nullptr);

	/** The file's turn-based values of the archetype's ranged weapon onto its turn profile. */
	CODEXTACTICS_API void ApplyEnemyTurnWeapon(EEnemyArchetype Archetype, FEnemyTurnProfile& Profile);

	/** Writes the current values of every weapon asset, an operative's grenades and the marksman's rifle to Path. */
	CODEXTACTICS_API bool Dump(const FString& Path, const AOperativeCharacter* GrenadeDefaults, const FMarksmanConfig* MarksmanDefaults = nullptr,
		const FTurnBasedBalance* TurnBalanceDefaults = nullptr);
}
