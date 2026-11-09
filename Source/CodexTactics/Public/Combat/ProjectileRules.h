#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponStats.h"

/**
 * The shared projectile layer's pure part (the weapons framework, 2026-11-09; docs/design/WEAPONS_FRAMEWORK.md 16; no Godot reference - new for
 * the RPG-7 and the mortar nests). The launch velocity that reaches an aim point (flat / the low arc / the high plunging arc), a fixed-step
 * flight (1/120 s steps whatever the frame rate: the same path at 30, 60 fps or a smoke's fixed step) and the arming rule. The world part is
 * AProjectileActor / UProjectileSubsystem. UE-only. Tested in CodexTactics.Weapons.Projectile.*.
 */

/** A round in flight. */
struct CODEXTACTICS_API FProjectileFlightState
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** Gravity on the round, cm/s^2 (negative = down; 0 = flat flight). */
	float GravityZ = 0.f;
	/** Fixed steps flown. */
	int32 Steps = 0;
	/** The path length flown, cm (arming). */
	float DistanceCm = 0.f;
	/** Frame time not yet flown (< one step). */
	double Accumulator = 0.0;

	float FlightSeconds() const;
};

namespace ProjectileRules
{
	/** The fixed flight step, s. */
	constexpr float StepSeconds = 1.f / 120.f;
	/** max_flight_s 0 = this, s. */
	constexpr float DefaultMaxFlightSeconds = 10.f;
	/** Steps per Advance call at most (a long hitch flies at most 0.5 s). */
	constexpr int32 MaxStepsPerAdvance = 60;

	/**
	 * The launch velocity from From that reaches Target: flat = straight at it at Speed (gravity ignored); ballistic / high-angle = the low /
	 * the high solution of the parabola under GravityZ (< 0). False when the target is out of reach at that speed (OutVelocity = the 45-degree
	 * shot towards it) or the input is degenerate. A ballistic / high-angle round with no gravity flies flat.
	 */
	CODEXTACTICS_API bool SolveLaunch(const FVector& From, const FVector& Target, float SpeedCmS, float GravityZ, EProjectileTrajectory Trajectory,
		FVector& OutVelocity);

	/** The flight time of a launch to a target HorizontalCm away (the horizontal speed is constant); 0 for a degenerate launch. */
	CODEXTACTICS_API float FlightTimeTo(const FVector& Velocity, float HorizontalCm);

	/** The gravity the round flies under: 0 for a flat trajectory, else WorldGravityZ x gravity_scale. */
	CODEXTACTICS_API float GravityFor(const FWeaponProjectileStats& Stats, float WorldGravityZ);

	/** max_flight_s (0 = the default). */
	CODEXTACTICS_API float MaxFlightSeconds(const FWeaponProjectileStats& Stats);

	/** One fixed step: exact constant-gravity kinematics (the step's samples lie on the true parabola). */
	CODEXTACTICS_API void Step(FProjectileFlightState& State);

	/**
	 * Flies DeltaSeconds of frame time as whole fixed steps (the rest stays in the accumulator). OnSegment(From, To, StepIndex) is called for
	 * every step; returning true stops the flight there (an impact). Returns true when a segment stopped it.
	 */
	CODEXTACTICS_API bool Advance(FProjectileFlightState& State, float DeltaSeconds,
		TFunctionRef<bool(const FVector& From, const FVector& To, int32 StepIndex)> OnSegment);

	/** The warhead is armed once the round has flown ArmingDistanceCm. */
	CODEXTACTICS_API bool IsArmed(const FProjectileFlightState& State, float ArmingDistanceCm);

	/** High-angle fire's band (SQUAD_SIX_FIRE_TEAMS.md 7.3: min 30 m, max 250 m): HorizontalCm within [MinCm, MaxCm] (MaxCm 0 = no limit). */
	CODEXTACTICS_API bool IsWithinFireBand(float HorizontalCm, float MinCm, float MaxCm);
}
