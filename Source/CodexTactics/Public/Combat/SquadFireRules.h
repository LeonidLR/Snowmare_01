#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

class UGodotBalanceAsset;

/** What the line of fire from an operative to an enemy hit first. */
enum class EShotLineHit : uint8
{
	/** Nothing, the target itself or another enemy. */
	Clear,
	/** A barricade (Godot group "barricades"). */
	Barricade,
	/** A wall or any other blocker. */
	Blocked
};

/** Result of the barricade rules for one line of fire (Godot _find_shoot_target). */
struct CODEXTACTICS_API FShotLineVerdict
{
	bool bCanHit = false;
	/** Damage factor: 0.8 crouched behind a barricade, else 1. */
	float Cover = 1.f;
	/** Prone behind a barricade: the shot is blocked ("🚫 Barricade blocks the shot"). */
	bool bBarricadeBlocked = false;
};

/** Godot get_elevation_advantage. */
struct CODEXTACTICS_API FElevationAdvantage
{
	bool bElevated = false;
	float RangeMultiplier = 1.f;
	float DamageMultiplier = 1.f;
	/** Shots from above fly over low cover. */
	bool bBypassLowCover = false;
};

/** Stance-dependent target switching (Godot stance_target_switch_delay_* / stance_switch_distance_ratio_*). */
struct CODEXTACTICS_API FSquadFireConfig
{
	float SwitchDelay[3] = { 0.15f, 0.5f, 1.2f };
	float SwitchRatio[3] = { 0.9f, 0.8f, 0.65f };
};

/**
 * Real-time squad fire rules (Godot Scenes/movements/player.gd _find_shoot_target, _shoot_at_target,
 * get_elevation_advantage, is_target_in_dead_zone, _apply_stance_modifiers). Heights are in cm from the
 * operative's feet; Godot measures from the capsule centre (1 m above the feet) to the enemy's feet.
 */
namespace SquadFireRules
{
	/** Godot game_balance_config.tres stance switching values (defaults above when a key is missing). */
	CODEXTACTICS_API FSquadFireConfig ConfigFromBalance(const UGodotBalanceAsset* Balance);

	/** Effective attack range factor: standing 1, crouching 1.15, prone 1.35. */
	CODEXTACTICS_API float GetPostureRangeMultiplier(EOperativeStance Stance);

	/** Damage factor of the stance: standing 1, crouching 1.25, prone 1.6. */
	CODEXTACTICS_API float GetStanceDamageMultiplier(EOperativeStance Stance);

	/** Elevated when the shooter's centre is >= 1.5 m above the target (+25 % range, +15 % damage, over low cover). */
	CODEXTACTICS_API FElevationAdvantage GetElevationAdvantage(float ShooterFeetZ, float TargetFeetZ);

	/** Under a platform: >= 1.8 m above, within 2.4 m horizontally and >= 45° down. */
	CODEXTACTICS_API bool IsInDeadZone(const FVector& ShooterFeet, const FVector& TargetFeet);

	/** Barricade rules: elevated shots bypass it, prone is blocked, crouched gets cover 0.8, standing shoots over. */
	CODEXTACTICS_API FShotLineVerdict JudgeLine(EShotLineHit Hit, EOperativeStance Stance, const FElevationAdvantage& Elevation);

	/** The closest candidate replaces the current target when much closer (ratio) or within 3.5 m. */
	CODEXTACTICS_API bool IsSignificantlyCloser(float ClosestDistanceCm, float CurrentDistanceCm, float SwitchRatio);

	/** Crit on luck % (Godot randf() * 100 < luck). */
	CODEXTACTICS_API bool IsCrit(float Luck, float Roll01);

	/** weapon damage * stance * cover * (crit 2) * (elevated 1.15) * distance factor. */
	CODEXTACTICS_API float ComputeShotDamage(float WeaponDamage, EOperativeStance Stance, float Cover, bool bCrit,
		const FElevationAdvantage& Elevation, float DistanceMultiplier);

	/** Distance in tactical cells (1.5 m, at least 1). */
	CODEXTACTICS_API int32 GetDistanceCells(float DistanceCm);
}
