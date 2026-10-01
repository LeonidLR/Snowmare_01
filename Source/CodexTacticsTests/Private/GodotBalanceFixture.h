#pragma once

#include "CoreMinimal.h"
#include "Data/GameBalanceConfig.h"
#include "UObject/Package.h"

/**
 * Godot balance values for parity tests. The project assets (DA_GameBalanceConfig / DA_Balance) are tuned in Unreal
 * since 2026-10-01, so rule tests use these fixtures instead: UGameBalanceConfig defaults are the Godot
 * game_balance_config.gd exports, plus the overrides of the matching Godot .tres file.
 */
namespace GodotBalanceFixture
{
	/** Godot resources/game_balance_config.tres. */
	inline UGameBalanceConfig* MakeGameBalanceConfig()
	{
		UGameBalanceConfig* Config = NewObject<UGameBalanceConfig>(GetTransientPackage());
		const TPair<const TCHAR*, float> Overrides[] = {
			{TEXT("exp_reward_hound"), 15.f}, {TEXT("exp_reward_spitter"), 20.f}, {TEXT("exp_reward_brute"), 35.f},
			{TEXT("exp_reward_wave_complete"), 25.f}, {TEXT("exp_reward_loot_crate"), 5.f},
			{TEXT("commander_max_health"), 140.f}, {TEXT("commander_fortitude"), 20.f},
			{TEXT("engineer_speed"), 7.5f}, {TEXT("medic_speed"), 7.5f}, {TEXT("susanin_stress_gain_multiplier"), 0.1f},
			{TEXT("cold_accumulation_rate"), 1.f}, {TEXT("max_campaign_waves"), 2.f},
			{TEXT("camera_rotation_return_duration"), 1.45f}, {TEXT("formation_wander_amplitude_side"), 1.01f},
			{TEXT("formation_wander_amplitude_back"), 1.f}, {TEXT("follower_idle_roam_delay"), 30.f},
			{TEXT("show_vision_cones"), 0.f}, {TEXT("panic_max_flee_radius"), 25.f}, {TEXT("panic_flee_distance"), 7.5f},
			{TEXT("anim_walk_speed"), 2.2f}, {TEXT("anim_run_speed"), 7.25f}, {TEXT("anim_crouch_speed"), 1.25f},
			{TEXT("anim_crouch_timescale"), 1.75f}, {TEXT("anim_max_turn_speed_deg"), 90.f},
			{TEXT("tactical_squad_max_ap"), 3.f}, {TEXT("tactical_enemy_max_ap"), 4.f},
			{TEXT("tactical_turret_shot_delay"), 1.f},
		};
		for (const TPair<const TCHAR*, float>& Override : Overrides)
		{
			Config->SetNumber(Override.Key, Override.Value);
		}
		return Config;
	}

	/** Godot resources/balance.tres (the turn-based values the tests check). */
	inline UGameBalanceConfig* MakeTurnBasedBalance()
	{
		UGameBalanceConfig* Balance = NewObject<UGameBalanceConfig>(GetTransientPackage());
		const TPair<const TCHAR*, float> Overrides[] = {
			{TEXT("tactical_step_duration"), 0.52f}, {TEXT("tactical_squad_max_ap"), 8.f},
			{TEXT("tactical_enemy_max_ap"), 6.f}, {TEXT("tactical_ap_cost_attack"), 3.f},
			{TEXT("tactical_ap_cost_push_barrel"), 2.f}, {TEXT("tactical_barrel_damage"), 80.f},
			{TEXT("tactical_rear_attack_multiplier"), 1.75f}, {TEXT("tactical_flank_attack_multiplier"), 1.25f},
			{TEXT("tactical_enemy_attack_duration"), 0.f},
		};
		for (const TPair<const TCHAR*, float>& Override : Overrides)
		{
			Balance->SetNumber(Override.Key, Override.Value);
		}
		return Balance;
	}
}
