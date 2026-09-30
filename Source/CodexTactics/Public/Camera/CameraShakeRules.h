#pragma once

#include "CoreMinimal.h"

class UGodotBalanceAsset;

/** Turn-based shot shake tuning (Godot camera.gd camera_shake_*; game_balance_config camera_shake_*). Amplitude in m. */
struct CODEXTACTICS_API FCameraShakeConfig
{
	float Amplitude = 0.18f;
	float PowerRifle = 0.35f;
	float PowerPistol = 0.20f;
	float PowerTurret = 0.28f;
	/** Trauma lost per second. */
	float Decay = 4.f;
	/** Noise time advance per second. */
	float Frequency = 45.f;
};

/**
 * Pure trauma-based camera shake rules.
 * Godot reference: Scenes/movements/camera.gd add_trauma, trigger_weapon_shake, _process_shake.
 */
namespace CameraShakeRules
{
	CODEXTACTICS_API FCameraShakeConfig ConfigFromBalance(const UGodotBalanceAsset* Balance);

	/** Trauma a shot adds: «pistol» / «пистолет» -> pistol, «turret» / «турел» -> turret, anything else the rifle. */
	CODEXTACTICS_API float GetPower(const FCameraShakeConfig& Config, const FString& WeaponType);

	/** Trauma after a shot, clamped to [0, 1]. */
	CODEXTACTICS_API float AddTrauma(float Trauma, float Amount);

	/** Trauma after DeltaSeconds of decay (not below 0). */
	CODEXTACTICS_API float Decay(const FCameraShakeConfig& Config, float Trauma, float DeltaSeconds);

	/** Offset magnitude scale: amplitude x trauma² (times a noise sample in [-1, 1]). */
	CODEXTACTICS_API float GetOffsetScale(const FCameraShakeConfig& Config, float Trauma);
}
