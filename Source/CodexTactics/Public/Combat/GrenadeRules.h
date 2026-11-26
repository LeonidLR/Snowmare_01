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
	/** G023: pre-throw turn speed before the throw montage starts, deg/s. */
	constexpr float TurnRateDegPerSec = 900.f;
	/** G023: skip the pre-turn wait if the thrower already faces within this, deg. */
	constexpr float PreTurnTriggerDeg = 15.f;
	/** G023: start the throw montage once the thrower faces the target within this, deg. */
	constexpr float PreTurnDoneDeg = 5.f;
	/** G023: safety cap on the pre-turn phase so a blocked rotation never stalls the throw, s. */
	constexpr float MaxPreTurnSeconds = 0.5f;
	/** G023: safety cap on holding the throw facing lock after ThrowAt, s. */
	constexpr float MaxFacingHoldSeconds = 2.5f;

	/**
	 * Seconds after the throw starts until the grenade leaves the hand: the measured release time of the clip
	 * (ReleaseSeconds > 0, clamped to the clip length) or, without one, Godot's 70 % of the clip length.
	 */
	CODEXTACTICS_API float ReleaseDelay(float ClipLength, float ReleaseSeconds);

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

	/** G023: 2D yaw from From to Target (CurrentYaw preserved if Target coincides with From). */
	CODEXTACTICS_API float ThrowYaw(const FVector& From, const FVector& Target, float FallbackYaw = 0.f);

	/** G023: unsigned horizontal angle error (0..180 deg) between CurrentYaw and TargetYaw. */
	CODEXTACTICS_API float ThrowErrorDeg(float CurrentYaw, float TargetYaw);

	/** G023: whether the thrower must smoothly pre-turn before the throw montage begins. */
	CODEXTACTICS_API bool NeedsPreTurn(float ErrorDeg, float TriggerDeg = PreTurnTriggerDeg);

	/** G023: step yaw toward TargetYaw at InTurnRateDegPerSec over DeltaSeconds. */
	CODEXTACTICS_API float StepThrowYaw(float CurrentYaw, float TargetYaw, float InTurnRateDegPerSec, float DeltaSeconds);
}
