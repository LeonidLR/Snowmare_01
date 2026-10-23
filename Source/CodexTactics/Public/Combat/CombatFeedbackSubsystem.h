#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatFeedbackSubsystem.generated.h"

class ACombatFeedbackActor;

/**
 * A shot tracer was drawn (Start = the muzzle, End = where the tracer ends). bHit: false from SpawnTracer / SpawnTurretTracer, which do
 * not know the outcome (a hit ends on the target, a miss beside it): listeners classify the end point themselves. Visual listeners only
 * (USnowInteractionSubsystem bullet dents, interactive snow phase 3).
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnTracerSpawned, const FVector& /*Start*/, const FVector& /*End*/, bool /*bHit*/);

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

	/**
	 * Sprint 06-B: a short ping where a real-time / preparation move order sends an operative — a cyan disc fading in
	 * Duration s; a new order clears the previous pings.
	 */
	void SpawnMovePing(const FVector& GroundLocation, bool bClearPrevious, float Duration = 1.75f);

	/** Live move pings (smokes). */
	int32 GetMovePingCount() const;

	/**
	 * Quick fire (Ctrl + click on a point, 2026-10-19): a short orange disc where the selection fires (0.4 m, fading in Duration s); a new one
	 * replaces the previous one.
	 */
	void SpawnFirePing(const FVector& GroundLocation, float Duration = 1.2f);

	/** Quick fire pings spawned since the world began (smokes). */
	int32 GetFirePingsSpawned() const { return FirePingsSpawned; }

	int32 GetPlannedMarkerCount() const;

	/** Red aim flash: light above the target (4.5 m, 0.45 s) and an overlay on its meshes (0.4 s): the red see-through
	 *  fresnel edge on enemies (M_TargetFresnel, Godot tactical_target_fresnel.gdshader), a glow on objects. */
	void HighlightTarget(AActor* Target);

	/** Godot player.gd _spawn_damage_flash: red light 1.2 m above the operative's centre, 3.5 m, fading in 0.12 s. */
	void SpawnDamageFlash(AActor* Operative);

	/** Godot enemy_base.gd _flash_hit: red glow on the enemy's meshes for 0.08 s. */
	void FlashEnemyHit(AActor* Enemy);

	/** Damage flashes spawned so far (checks). */
	int32 GetDamageFlashCount() const { return DamageFlashCount; }

	/** Godot default tracer colour (weapon_tracer_color / rifle_m16.tres). */
	static FLinearColor DefaultTracerColor() { return FLinearColor(0.2f, 1.f, 0.4f); }

	/** Broadcast by SpawnTracer / SpawnTurretTracer (native, visual listeners only). */
	FOnTracerSpawned OnTracerSpawned;

private:
	int32 DamageFlashCount = 0;
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	ACombatFeedbackActor* SpawnFeedback(const FVector& Location) const;

	TArray<TWeakObjectPtr<ACombatFeedbackActor>> PlannedMarkers;

	TArray<TWeakObjectPtr<ACombatFeedbackActor>> MovePings;

	TWeakObjectPtr<ACombatFeedbackActor> FirePing;
	int32 FirePingsSpawned = 0;
};
