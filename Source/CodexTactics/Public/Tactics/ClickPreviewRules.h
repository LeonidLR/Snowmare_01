#pragma once

#include "CoreMinimal.h"

/**
 * R17-3 (designer request 2026-11-17, decisions the same evening): ONE building interaction in every control mode (Solo, Solo with a
 * box-selected group, Formation). Hovering a surface shows nothing; the FIRST click on a wall / corner / obstacle / doorway shows the ghost
 * that fits that surface (wall face = hug it, near a corner = peek round it, obstacle top = climb it, doorway opening = the breach stack);
 * moving the cursor along the same building moves the ghost with it (at a corner it turns into the peek); the SECOND click on the same
 * building executes; a click elsewhere on the ground cancels the ghost and is the ordinary move; a double click on an obstacle = run there
 * and climb at once. Pure decisions (no world); the runtime is ACodexTacticsPlayerController::HandlePlayerClick / TickClickPreview
 * (CodexTacticsPlayerControllerClickPreview.cpp). UE-only (Godot reference: none). Tests CodexTactics.Tactics.ClickPreview.*.
 */

/** What the ghost under a click shows (the surface's own meaning). */
enum class EClickPreviewKind : uint8
{
	None,
	/** Hug the wall / obstacle face (a cover slot; along his own wall: the shimmy; another face of his own cover: round it). */
	Cover,
	/** Peek round the corner (in cover at that wall, the squad's corner, or the run-and-peek from the open). */
	CornerPeek,
	/** Climb the obstacle (its top). */
	Mantle,
	/** The doorway breach stack (DoorwayBreachRules slots). */
	Breach,
};

/** What a left click does with the click preview. */
enum class EClickPreviewAction : uint8
{
	/** Not a building click and no ghost up: the ordinary click (HandleWorldHit). */
	PassThrough,
	/** Show the ghost of this surface (the first click; a click on another building re-arms there). */
	Arm,
	/** The second click on the previewed building (or on the ghost's feet): carry out what the ghost shows. */
	Execute,
	/** A ghost is up but the click is elsewhere: the ghost goes, the click is the ordinary click. */
	CancelAndPass,
};

/** A confirmed cover ghost must issue an order or explain why it cannot. */
enum class ECoverPreviewCommit : uint8
{
	Order,
	AlreadyThere,
	InvalidTarget,
	NoApproach,
};

/** The facts of one left click for DecideClick. */
struct FClickPreviewClickFacts
{
	/** Codex.Click.Preview is on (0: the old rules - hover ghosts, a peek / climb click acts at once). */
	bool bEnabled = true;
	/** A ghost of an earlier click is up. */
	bool bArmed = false;
	/** The click is a modifier click (Ctrl / Shift / Alt) or one of the ordinary click's own cases (turn-based grid, order lock, ...). */
	bool bBypass = false;
	/** The clicked surface has a building meaning (a ghost could be shown there). */
	bool bSurface = false;
	/** The click is on the previewed building (the same piece, a piece touching it, the same building id). */
	bool bOnArmedStructure = false;
	/** The click is on the ground right at the ghost's feet. */
	bool bOnGhost = false;
};

/** One piece of a structure the cursor may be on (built from a cursor hit by the controller). */
struct FClickPreviewPiece
{
	/** The hit component's identity (0: none). */
	uint64 ComponentId = 0;
	/** The hit actor's identity (0: none). */
	uint64 ActorId = 0;
	/** The level generator's "building=<id>" tag of the actor (NAME_None: none). */
	FName Building = NAME_None;
	/** The hit component's world bounds. */
	FBox Bounds = FBox(ForceInit);
	/** The hit surface faces up (a floor, a roof, a wall's top): |normal.Z| >= FloorNormalZ. */
	bool bFloorLike = false;
};

/** The previewed structure: the pieces the ghost was shown on, their union bounds and building ids. */
struct FClickPreviewStructure
{
	TSet<uint64> Components;
	TSet<uint64> Actors;
	TSet<FName> Buildings;
	FBox Bounds = FBox(ForceInit);

	bool IsEmpty() const { return Components.IsEmpty() && Actors.IsEmpty(); }
	void Reset() { Components.Reset(); Actors.Reset(); Buildings.Reset(); Bounds = FBox(ForceInit); }
};

namespace ClickPreviewRules
{
	/** Movement / leaving cover never blocks replacement; only a settled same-wall slot is already occupied by this operative. */
	CODEXTACTICS_API ECoverPreviewCommit DecideCoverCommit(bool bValidTarget, bool bApproachAvailable, bool bInCover, bool bSameWall, float AlongCm);
	/** A hit surface whose normal's |Z| reaches this faces up (a floor, a top), not a wall. */
	inline constexpr float FloorNormalZ = 0.7f;
	/** Two pieces whose bounds come this close touch (a corner's two walls, a lintel over an opening, a sill), cm. */
	inline constexpr float TouchToleranceCm = 30.f;
	/** A piece this big in X or Y is the ground / a slab, never a part of a building's wall structure, cm. */
	inline constexpr float MaxPieceExtentCm = 4000.f;
	/** A ground click within this of the shown ghost's feet (2D) is a click on the ghost, cm. */
	inline constexpr float GhostClickRadiusCm = 60.f;
	/** ... within this height of them, cm. */
	inline constexpr float GhostClickHeightCm = 100.f;
	/** The ghost re-plans when the cursor moved this far over the building, cm ... */
	inline constexpr float TrackMoveCm = 5.f;
	/** ... at most this often (real time), s. */
	inline constexpr float TrackIntervalSeconds = 0.05f;

	/**
	 * R17-3 "find why it works on some buildings and not on others": a cursor hit high on a wall face is brought down to the operative's floor.
	 * A wall face hit more than this above his feet ...
	 */
	inline constexpr float HighHitAboveFeetCm = 160.f;
	/** ... is traced again at this height above his feet (a man's chest: the cover traces' chest height), cm ... */
	inline constexpr float LowHitChestCm = 110.f;
	/** ... starting this far out from the face, the trace this long (towards the face), cm. */
	inline constexpr float LowHitOutCm = 60.f;
	inline constexpr float LowHitReachCm = 140.f;
	/** The lower face counts when its normal agrees with the high one's this much (dot). */
	inline constexpr float LowHitSameFaceDot = 0.9f;

	/** The click's meaning (see EClickPreviewAction). */
	CODEXTACTICS_API EClickPreviewAction DecideClick(const FClickPreviewClickFacts& Facts);

	/** A second click within MaxSeconds and MaxPixels of the one that armed the ghost is a double click (SinceArmSeconds < 0: no arm click). */
	CODEXTACTICS_API bool IsDoubleClick(double SinceArmSeconds, float MouseDistancePx, float MaxSeconds, float MaxPixels);

	/** The piece is too big to be part of a building's walls (the ground, a big slab). */
	CODEXTACTICS_API bool IsGroundPiece(const FClickPreviewPiece& Piece);

	/**
	 * The cursor's piece belongs to the previewed structure: a piece the ghost was already shown on; or a wall-like piece (not floor-like, not a
	 * ground piece) of the same actor / the same building id / touching the structure's bounds (TouchToleranceCm). A floor-like piece counts
	 * only when the ghost was shown on it (a wall's own top, an obstacle's top) - the floor inside / beside a building is a move.
	 */
	CODEXTACTICS_API bool IsOnStructure(const FClickPreviewStructure& Structure, const FClickPreviewPiece& Piece);

	/** Adds the piece the ghost is shown on to the structure (a ground piece only by its identity, never its bounds). */
	CODEXTACTICS_API void AddPiece(FClickPreviewStructure& Structure, const FClickPreviewPiece& Piece);

	/** The ground click lies at the ghost's feet (GhostClickRadiusCm 2D, GhostClickHeightCm up / down). */
	CODEXTACTICS_API bool IsClickOnGhost(const FVector& Click, bool bClickFloorLike, const FVector& GhostFeet);

	/** The wall-face hit at HitZ lies high enough over the operative's feet to be traced again at his chest (HighHitAboveFeetCm). */
	CODEXTACTICS_API bool ShouldLowerWallHit(float HitZ, float NormalZ, float FeetZ);

	/** The lower trace's face stands for the high hit: a wall (not a floor) whose planar normal agrees with the high hit's (LowHitSameFaceDot). */
	CODEXTACTICS_API bool IsSameFaceLower(const FVector2D& HighNormal, const FVector2D& LowNormal, float LowNormalZ);

	/**
	 * The cursor's ray From -> To (the camera through the hit point) through a doorway's opening: the portal plane through Center with the
	 * normal Outward, Width wide along Tangent (plus SlackCm either side), Height high from Center's Z. The ray crosses the plane before
	 * its hit, or the hit lies within DepthSlackCm of the plane (a hit on the threshold, a jamb's face). OutPoint: where on the plane.
	 */
	CODEXTACTICS_API bool RayThroughPortal(const FVector& From, const FVector& To, const FVector& Center, const FVector& Tangent, const FVector& Outward,
		float WidthCm, float HeightCm, float SlackCm, float DepthSlackCm, FVector& OutPoint);

	/**
	 * Review 2026-11-17: a click ARMS a doorway's breach only on the opening itself - a jamb face or the threshold, the hit within DepthSlackCm of
	 * the portal plane. A ray through the opening onto the room's floor (or the yard beyond a gate) is a plain move through the doorway.
	 */
	CODEXTACTICS_API bool IsAtPortalPlane(const FVector& HitPoint, const FVector& Center, const FVector& Outward, float DepthSlackCm);
}
