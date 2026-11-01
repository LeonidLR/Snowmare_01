#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * One route round an obstacle for a group move into the wall layout (the game designer's play-test 2026-11-01 on L_FormationLab, approved
 * fixes d / e; behind Codex.Formation.Sectors + Codex.Formation.WallRoute). Godot reference: none (UE-only).
 *
 * The bug: the squad crouched at one wall, ordered to the far side of the OPPOSITE wall, split on the run - the Commander round one end, the
 * Engineer and the Medic-Sapper round the other. Cause: the group cover order in group mode is the formation wall layout
 * (USquadSubsystem::OrderFormationWall); the leader runs to his slot, every follower walks to his OWN wall slot with his own navmesh path
 * (UpdateSectorFollower -> FollowTo), so men whose slots lie either side of the wall's middle take different ends.
 * The rule: a follower whose straight way to his wall slot is cut by a wall body (NeedsRoute) walks the leader's route instead - his place
 * in a column ON the leader's path since the order (PlacesAlongTrail: the column's distances back along it, never past where he started)
 * - until his way to the slot is clear (he is round the obstacle), then into his slot; once everyone is clear the places are re-dealt by
 * travel (no crossing at the far face). Pure; tests CodexTactics.Formation.WallRoute.*.
 */
namespace FormationWallRoute
{
	/** The straight way counts as cut when it passes this close to a wall body (its half thickness + this), cm. */
	inline constexpr double RouteClearanceCm = 15.0;

	/** Member's straight way to Slot is cut by a wall body (he would pick his own way round): he walks the leader's route. */
	CODEXTACTICS_API bool NeedsRoute(const FVector2D& Member, const FVector2D& Slot, const TArray<FFormationWall>& Walls);

	/** A place on the leader's route and the route's travel direction there (UE yaw). */
	struct FPlace
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float Yaw = 0.f;
	};

	/**
	 * Places column places ON the leader's path TrailWithLeader (oldest first, the last point = the leader now): place i (0-based) lies
	 * FirstBackCm + i * SpacingCm back along it, clamped to its first point (a short path: never extrapolated - behind its start may be a wall).
	 * Yaw = the path's direction at the place (its first segment's at the start; 0 for a single point). Empty for no path / Places <= 0.
	 */
	CODEXTACTICS_API TArray<FPlace> PlacesAlongTrail(const TArray<FVector2D>& TrailWithLeader, int32 Places, float FirstBackCm, float SpacingCm);

	/**
	 * The trail-column places for the routed members: the first Routed.Num() of ColumnPlaces (ordered from the leader backwards) dealt by
	 * minimal travel (FormationRuntime::AssignByTravel). Returns per routed member the index into ColumnPlaces; empty when there are fewer
	 * places than routed members or more than 4 places.
	 */
	CODEXTACTICS_API TArray<int32> AssignColumnPlaces(const TArray<FVector2D>& Routed, const TArray<FVector2D>& ColumnPlaces);
}
