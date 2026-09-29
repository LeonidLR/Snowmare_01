#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

/**
 * Pure hand-grenade rules.
 * Godot reference: Scenes/weapons/grenade.gd (calculate_effective_range, throw_to, _apply_area_effect) and main.gd
 * _get_grenade_aim_info.
 */
namespace GrenadeRules
{
	/** Godot throw_force 14 m/s along the arc. */
	constexpr float ThrowSpeed = 1400.f;
	/** Godot arc_height 2.6 m. */
	constexpr float ArcHeight = 260.f;
	/** Godot fuse_time 1.2 s after landing. */
	constexpr float FuseTime = 1.2f;
	/** Godot throw_release_anim_ratio: the grenade leaves the hand at 70 % of the throw animation. */
	constexpr float ReleaseAnimRatio = 0.7f;
	/** Godot: the grenade starts 1.35 m above the thrower's origin. */
	constexpr float HandHeight = 135.f;
	/** Operatives take 65 % of the blast (Godot take_damage(applied_damage * 0.65)). */
	constexpr float SquadDamageScale = 0.65f;

	/** Godot calculate_effective_range: standing 100 %, crouching 75 %, prone 50 % of the base range. */
	CODEXTACTICS_API float EffectiveRange(float BaseRange, EOperativeStance Stance);

	/** Godot falloff: 1 - distance / radius * 0.5, clamped to 0.5..1 (half damage at the edge). */
	CODEXTACTICS_API float DamageFalloff(float Distance, float Radius);

	/** Target moved onto the MaxRange circle around From when farther (flat), keeping the target height. */
	CODEXTACTICS_API FVector ClampTarget(const FVector& From, const FVector& Target, float MaxRange);

	/** Flight time of the arc (Godot max(0.12, flat distance / throw_force)). */
	CODEXTACTICS_API float FlightDuration(float FlatDistance);

	/** Position on the arc at Progress 0..1 (Godot lerp + sin(progress * PI) * arc_height). */
	CODEXTACTICS_API FVector ArcPoint(const FVector& Start, const FVector& End, float Progress);
}
