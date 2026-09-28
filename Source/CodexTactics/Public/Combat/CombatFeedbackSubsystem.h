#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatFeedbackSubsystem.generated.h"

class ACombatFeedbackActor;

/**
 * Visual combat feedback: shot tracers with a muzzle flash, planned-order markers of the tactical pause and the
 * Ctrl + click target flash. Markers are cleared whenever the combat mode changes (pause start / release).
 * Godot reference: player.gd `_spawn_muzzle_tracer`, deployables/turret.gd `_spawn_muzzle_tracer`,
 * main.gd `_spawn_waypoint_marker`, `_clear_planned_markers`, `_highlight_target_feedback`.
 */
UCLASS()
class CODEXTACTICS_API UCombatFeedbackSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Operative shot: glow 5 (fire 6, energy 8), fade 0.1 s (fire / cryo 0.2 s), muzzle flash 4 m fading in 0.08 s. */
	void SpawnTracer(const FVector& Muzzle, const FVector& End, const FLinearColor& Color, EDamageType DamageType = EDamageType::Kinetic);

	/** Turret shot: green glow 8, fade 0.1 s, flash 2.5 m fading in 0.05 s. */
	void SpawnTurretTracer(const FVector& Muzzle, const FVector& End);

	/** Cyan disc (0.35 m radius) marking a planned order; stays until ClearPlannedMarkers. */
	void SpawnWaypointMarker(const FVector& GroundLocation);

	void ClearPlannedMarkers();

	int32 GetPlannedMarkerCount() const;

	/** Red aim flash: light above the target (4.5 m, 0.45 s) and a glowing overlay on its meshes (0.4 s). */
	void HighlightTarget(AActor* Target);

	/** Godot default tracer colour (weapon_tracer_color / rifle_m16.tres). */
	static FLinearColor DefaultTracerColor() { return FLinearColor(0.2f, 1.f, 0.4f); }

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	ACombatFeedbackActor* SpawnFeedback(const FVector& Location) const;

	TArray<TWeakObjectPtr<ACombatFeedbackActor>> PlannedMarkers;
};
