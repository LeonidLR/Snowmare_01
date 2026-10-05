#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

/** Why an operative cannot lift an object. */
enum class ELiftBlocker : uint8
{
	None,
	TooCold,
	Wounded
};

/**
 * Rules of moving (pushing / carrying) objects around the level.
 * Godot reference: main.gd `can_relocate_objects_now`, `_get_relocate_radius_for_worker`, `_process_relocate_preview`,
 * player.gd `can_lift_objects` (max_cold_to_lift_objects 80, min_health_percent_to_lift 0.5).
 */
namespace RelocationRules
{
	/** Stage-1 distance at which the worker reaches the object, cm (Godot 2.2 m; 3.2 m when stuck). */
	constexpr float ReachDistance = 220.f;
	constexpr float ReachDistanceStuck = 320.f;
	/** Stage-2 arrival distance to the target, cm (Godot 1.8 m; 3.0 m when stuck). */
	constexpr float ArriveDistance = 180.f;
	constexpr float ArriveDistanceStuck = 300.f;
	/** The pushed object rides this far in front of the worker, cm (Godot 1.35 m). */
	constexpr float PushOffset = 135.f;
	/** The worker steps back this far from the placed object, cm (Godot 1.6 m; 1.5 m when dropped). */
	constexpr float StepBackPlaced = 160.f;
	constexpr float StepBackDropped = 150.f;
	/** Rotation step of the placement preview, degrees. */
	constexpr float RotationStep = 45.f;
	/** Radius used where Godot has no limit (preparation: 9999 m). */
	constexpr float UnlimitedRadius = 999900.f;

	/**
	 * Objects can be moved outside a wave, in preparation and in the tactical pause (not in live combat) — and by a
	 * leader exploring a camera zone alone (Godot is_in_camera_zone "zone solo").
	 */
	CODEXTACTICS_API bool CanRelocateNow(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bLeaderZoneSolo = false);

	/** Placement radius for the current mode, cm. */
	CODEXTACTICS_API float GetPlacementRadius(ECodexGamePhase Phase, ECodexCombatMode Mode, float PauseRadius, float WorkerPlacementRadius);

	CODEXTACTICS_API bool IsWithinRadius(const FVector& Origin, const FVector& Point, float Radius);

	/**
	 * User decision 2026-10-05: in the preparation the squad's set-up items are shared and the free operative closest to
	 * the marked spot (planar) runs to set it up. Index into Positions, INDEX_NONE when nobody is available.
	 */
	CODEXTACTICS_API int32 ChooseNearestWorker(const TArray<FVector>& Positions, const TArray<bool>& Available, const FVector& Target);

	CODEXTACTICS_API ELiftBlocker GetLiftBlocker(float ColdLevel, float HealthFraction, float MaxColdToLift, float MinHealthFractionToLift);
}
