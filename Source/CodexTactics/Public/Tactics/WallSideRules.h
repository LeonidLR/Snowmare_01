#pragma once

#include "CoreMinimal.h"

/**
 * Which face of a wall a cursor click means, and when a click picks an operative (the game designer's play-test 2026-11-01 on
 * L_FormationLab, bug_02: "the squad is at a wall and I want it at the FAR side of the same wall - my clicks land on the wall top, the near
 * face or the operatives; the cover ghost snaps to the near face"). Approved fixes a) and b); UE-only (Godot reference: none). Pure; tests
 * CodexTactics.Tactics.WallSide.*. The runtime (traces, the cover slot) is ACodexTacticsPlayerController::ResolveWallSideSlot
 * (CodexTacticsPlayerControllerWallSide.cpp).
 *
 * a) The side is decided by the cursor, as the player sees the wall (along the cursor ray, planar): a hit on the TOP of a high wall
 *    beyond its middle line, or on the ground right behind it (within the band the wall hides from the camera, see FarGroundReachCm) with
 *    the leader on the other side -> the FAR face; the top's near half -> the near face; a face hit stays the face that was hit.
 * b) While the squad is at a wall a click picks an operative only on his body (the trace hit him, or the ground within TightPickRadiusCm
 *    of him), never by the wide select radius - a wall's top / face next to the squad is a cover click; the portraits / number keys still
 *    select.
 */

/** The face of the wall a click means. */
enum class EWallClickSide : uint8
{
	/** No wall side (no wall, a low wall, the rule is off): the click keeps its old meaning. */
	None,
	/** The face the camera sees. */
	Near,
	/** The face hidden from the camera (the wall between it and the player). */
	Far
};

namespace WallSideRules
{
	/** A hit whose normal's Z is above this lies on a top (or the ground). */
	inline constexpr float TopNormalZ = 0.7f;
	/** A face hit's normal along the planar view must be at least this (the far face points away from the camera). */
	inline constexpr float FaceAlongViewMin = 0.5f;
	/** Two faces farther apart than this along the view are no wall (a building's two sides), cm. */
	inline constexpr float MaxWallThicknessCm = 400.f;
	/** The far-ground band never ends nearer the far face than this, cm. */
	inline constexpr float FarGroundReachMinCm = 150.f;
	/** ... nor farther than this, cm (the iso camera, ~25-35 deg down along the cursor's ray, hides 4.3-6.4 m behind a 3 m wall). */
	inline constexpr float FarGroundReachMaxCm = 1000.f;
	/** The band reaches this much past the ground the wall hides from the camera, cm. */
	inline constexpr float FarGroundMarginCm = 100.f;

	/**
	 * The top of a wall split at its middle line (positions measured along the camera's planar view direction, cm): beyond the middle
	 * of NearFaceAlong .. FarFaceAlong -> Far, else Near. None when the faces are in the wrong order or farther apart than
	 * MaxWallThicknessCm.
	 */
	CODEXTACTICS_API EWallClickSide SideOfTopHit(float HitAlong, float NearFaceAlong, float FarFaceAlong);

	/**
	 * How far behind a wall's far face the ground still means that face, cm: the ground the wall hides along the cursor's ray, which
	 * looks down at CameraPitchDeg (negative = down; the ray's own depression, not the camera's centre pitch) - WallHeightCm / tan(pitch)
	 * - plus FarGroundMarginCm, clamped to FarGroundReachMinCm .. FarGroundReachMaxCm (a flat ray: the maximum).
	 */
	CODEXTACTICS_API float FarGroundReachCm(float WallHeightCm, float CameraPitchDeg);

	/**
	 * A ground hit means the FAR face of the wall found looking back from it towards the camera: the face DistanceCm away (within
	 * ReachCm), its normal pointing away from the camera (FaceNormalAlongView >= FaceAlongViewMin), a high wall (cover a man stands
	 * behind), and the leader on the wall's other side (on his own side the ground is a move / a slide along it).
	 */
	CODEXTACTICS_API bool IsGroundBehindWall(float DistanceCm, float ReachCm, float FaceNormalAlongView, bool bHighWall, bool bLeaderOnOtherSide);
}

namespace ClickPickRules
{
	/** At a wall the ground within this of an operative's body centre (his feet, his shadow) still picks him, cm. */
	inline constexpr float TightPickRadiusCm = 45.f;
	/** A hit counts as the ground (not a wall's top) when it lies at most this far above the leader's feet, cm. */
	inline constexpr float GroundHitMaxAboveFeetCm = 40.f;

	/**
	 * The proximity pick radius of a click that did not hit a body: SelectRadius normally; while the squad is at a wall (b) the tight
	 * TightPickRadiusCm on the ground and 0 on anything else (a wall's top / face is a cover click, never a selection).
	 */
	CODEXTACTICS_API float PickRadius(bool bSquadAtWall, bool bGroundHit, float SelectRadius);
}
