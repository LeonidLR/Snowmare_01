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

	/**
	 * #36 (user 2026-11-04, L_Gen_Depot_1: "the operatives do not vault over the low obstacles"): the level generator's covers (Scripts/Editor/
	 * build_levelgen_map.py: cubes tagged with their layout "kind") were never tagged "Vault", so nothing on a generated map was vaultable.
	 * The low kinds a man climbs over - sandbags, jersey barriers, low walls (barrels no more: explosive, designer 2026-11-15) - are made vaultable at the start of play
	 * (ACodexTacticsGameMode::StartPlay) when the object fits the vault: MinHeight .. MaxHeight tall and its thinner side at most
	 * MaxVaultableDepthCm (he lands past it). Crates (110 cm) and wrecks / boilers stay cover: taller than MaxHeight (Godot's 105).
	 */
	constexpr float MaxVaultableDepthCm = 130.f;
	CODEXTACTICS_API bool IsVaultableCoverKind(FName Kind);
	CODEXTACTICS_API bool IsVaultableLevelObject(FName Kind, float HeightCm, float ThinSideCm);

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

	/**
	 * The walk after a vault arc (vault, obstacle step-off, mantle hop), bug 2026-11-22 (doors R1: L_Gen_StoryShowcase S03 / S09 "door_1 ->
	 * storey_0" - the story's front door locked, the route goes in through a ground-floor window and the operative stopped behind the sill).
	 * Godot vault_resume_target dropped the order when the goal lay within 0.6 m of the landing or more than ~84 deg off the vault direction
	 * (dot < 0.1): Godot walks STRAIGHT at target_position, so a goal behind the obstacle meant walking back into it and vaulting again. Here the
	 * resumed order is a new navmesh path from the landing spot (RequestMove), so the angle says nothing - a window into a room, a barricade
	 * crossed mid-turn, a step-off sideways off an obstacle top all have their goal far off the arc's direction. The rule now:
	 *  - no order (or no path) when the arc began, or the goal within ResumeArrivedCm of the landing -> the walk ends there (the old rule);
	 *  - the arc crossed BACK over the obstacle the previous arc of the same walk crossed (bRecrossing, IsRecrossing) and the goal lies behind
	 *    it (dot < ResumeBehindDot, the old angle gate) -> the walk ends (the ping-pong guard: a goal inside / on the obstacle, each arc
	 *    landing past it - at most one crossing back);
	 *  - otherwise the walk goes on to the goal, whatever the angle.
	 */
	constexpr float ResumeArrivedCm = 60.f;
	constexpr float ResumeBehindDot = 0.1f;
	/** A later vault belongs to the same walk when its goal is within this of the previous one's and it starts within RecrossWindowSeconds. */
	constexpr float RecrossGoalToleranceCm = 100.f;
	constexpr float RecrossWindowSeconds = 20.f;

	enum class EVaultResume : uint8
	{
		Resume,   // walk on to the goal
		NoOrder,  // nothing to resume
		AtGoal,   // landed on (next to) the goal
		PingPong  // crossed back over the same obstacle and the goal lies behind it again
	};

	struct FVaultResumeFacts
	{
		/** A move order with a path when the arc began (its goal = Goal). */
		bool bHasOrder = false;
		FVector Start = FVector::ZeroVector;
		FVector Landing = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;
		/** IsRecrossing of this arc. */
		bool bRecrossing = false;
	};

	CODEXTACTICS_API EVaultResume DecideResume(const FVaultResumeFacts& Facts);

	/**
	 * Whether a vault crosses back over the obstacle the previous vault of the same walk crossed: the same obstacle, the previous vault
	 * resumed its walk, the direction reversed (dot < 0), the same goal (RecrossGoalToleranceCm) and within RecrossWindowSeconds of it.
	 */
	CODEXTACTICS_API bool IsRecrossing(bool bSameObstacle, bool bPreviousResumed, const FVector& PreviousDirection, const FVector& Direction,
		const FVector& PreviousGoal, const FVector& Goal, float SecondsSincePrevious);
}
