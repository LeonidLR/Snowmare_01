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
}
