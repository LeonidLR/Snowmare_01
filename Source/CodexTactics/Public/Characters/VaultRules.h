#pragma once

#include "CoreMinimal.h"

/**
 * Vaulting over low obstacles (barricades and objects tagged "Vault"): the numbers and the arc.
 * Godot reference: Scripts/components/locomotion_controller.gd check_vault_obstacle / start_vault / process_locomotion
 * (vault_* exports), Scenes/movements/player.gd try_vault_obstacle.
 */
namespace VaultRules
{
	/** Godot vault_* exports, in cm / s. */
	constexpr float MinHeight = 25.f;
	constexpr float MaxHeight = 105.f;
	constexpr float ProbeHeight = 50.f;
	constexpr float ApproachDistance = 125.f;
	constexpr float LandingDistance = 135.f;
	constexpr float MaxLandingStep = 100.f;
	constexpr float ApexOffset = 15.f;
	constexpr float MinApex = 40.f;
	constexpr float Cooldown = 1.2f;

	/** Obstacle Height (above the feet) and the landing floor LandingStep (relative to the feet) allow a vault. */
	CODEXTACTICS_API bool CanVault(float Height, bool bHasLanding, float LandingStep);

	/**
	 * Seconds the vault takes: Godot plays the clip stretched to vault_run_duration 0.95 s (running) or
	 * vault_walk_duration 1.25 s, sped up by vault_speed_scale 1.15.
	 */
	CODEXTACTICS_API float Duration(bool bRunning);

	/** Position at Alpha (0..1): smoothstep along the ground from Start to Landing, lifted by the sine arc over Height. */
	CODEXTACTICS_API FVector Position(const FVector& Start, const FVector& Landing, float Height, float Alpha);

	/** Margin of an obstacle's vault nav area round its collision: an enemy capsule radius, so the enemies' paths round it clear the box. */
	constexpr float NavAreaPaddingCm = 40.f;

	/**
	 * Half-size of the vault nav area of an obstacle (VaultNavigation::MakeVaultable; the nav modifier's failsafe box, centred on the
	 * actor, in its rotated frame, world units), 2026-10-25: the collision bounds in actor space (LocalBounds) scaled by the actor's
	 * Scale, mirrored to cover both sides of the pivot, plus PaddingCm on X / Y and 50 cm on Z (down through the navmesh surface).
	 * An invalid LocalBounds gives the engine's failsafe (100, 100, 100) - which every vault area was before: the last 50 cm of
	 * each end of a 3 m barricade lay outside it, open navmesh for the enemies under a solid box.
	 */
	CODEXTACTICS_API FVector NavAreaExtent(const FBox& LocalBounds, const FVector& Scale, float PaddingCm = NavAreaPaddingCm);
}
