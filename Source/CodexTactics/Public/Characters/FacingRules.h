#pragma once

#include "CoreMinimal.h"

/**
 * Body turning shared by operatives and enemies (Godot player.gd _safe_look_at / enemy_base.gd _smooth_look_at:
 * rotation.y = lerp_angle(rotation.y, target, turn_speed * delta) — fast at first, easing out). The engine's
 * bOrientRotationToMovement is not used: it chased every wobble of the velocity at low speed (braking at the goal,
 * crowd separation) at a constant rate, and the bodies trembled / spun on the spot.
 */
namespace FacingRules
{
	/** One Godot lerp_angle step, degrees; TurnSpeed is Godot's turn_speed (1/s). */
	inline float StepYaw(float CurrentYaw, float TargetYaw, float TurnSpeed, float DeltaSeconds)
	{
		const float Alpha = FMath::Clamp(TurnSpeed * DeltaSeconds, 0.f, 1.f);
		return FRotator::NormalizeAxis(CurrentYaw + FRotator::NormalizeAxis(TargetYaw - CurrentYaw) * Alpha);
	}

	/** Low-pass of the planar velocity (time constant ~0.1 s) so a single-frame wobble does not turn the body. */
	inline FVector SmoothVelocity(const FVector& Smoothed, const FVector& Velocity, float DeltaSeconds)
	{
		const float Alpha = 1.f - FMath::Exp(-10.f * DeltaSeconds);
		return FMath::Lerp(Smoothed, FVector(Velocity.X, Velocity.Y, 0.f), Alpha);
	}
}
