#pragma once

#include "CoreMinimal.h"

/**
 * Run-and-peek (#55, user request 2026-11-07: "the peek works only after hugging the wall first - that badly hurts the pace of the fight; I
 * want the peek at once: the operative is running, gets the order to peek round a corner in front of him, runs straight to that spot and
 * peeks immediately"). A click on the END of a peekable wall (the manual corner peek's end region: its last ~75 cm, its end face or its top
 * there) by an operative who is NOT in cover at that wall - standing, walking, running in the open or in cover at another wall - sends him
 * to the corner's cover slot on the face of that wall that faces him and turns the cover entry straight into the corner peek
 * (AOperativeCharacter::OrderRunCornerPeek). Real time only (turn-based and the tactical pause keep their rules). UE-only (Godot reference:
 * none). Pure; tests CodexTactics.Tactics.Cover.RunCornerPeek.*. The runtime (face traces, slots, orders) is
 * ACodexTacticsPlayerController::TryHandleRunCornerPeekClick / FindRunCornerPeekForHit (CodexTacticsPlayerControllerRunPeek.cpp).
 */
namespace RunCornerPeekRules
{
	/** A face narrower than this between its two exposed ends (an end face, a thin post) is no run-and-peek face, cm. */
	inline constexpr float MinFaceWidthCm = 100.f;
	/** Two probed faces whose normals agree this much (dot) are one face. */
	inline constexpr float SameFaceDot = 0.95f;
	/** A cover entry within this of the ordered corner slot (on the same wall) is the run-and-peek's arrival, cm (the arrival tolerance of a cover walk). */
	inline constexpr float ArrivalToleranceCm = 150.f;
	/** A wall top counts only this far above the operative's feet (the ground at his level is a move, not a wall), cm. */
	inline constexpr float MinTopAboveFeetCm = 40.f;

	/** What a click is and who gets it, for the run-and-peek decision. */
	struct FClickFacts
	{
		/** Codex.Cover.RunCornerPeek is on. */
		bool bEnabled = true;
		/** The turn-based grid is active (its own cover rules). */
		bool bTurnBased = false;
		/** The tactical pause plans the orders (they run on the release): the old cover click there. */
		bool bPausePlanning = false;
		/** The squad-wide modifier (Alt) is held: Alt keeps its "take cover at that object" meaning. */
		bool bSquadWide = false;
		/** The tutorial has not taught the corner peek yet (PlayerCommands::CoverCornerPeek locked). */
		bool bTutorialLocked = false;
		/** The click hit a body (an operative, an enemy, any pawn). */
		bool bHitPawn = false;
		/** The click hit an object with its own click meaning (barricade / barrel / crate menus, the APC hull, a silhouette, an enemy). */
		bool bHitObject = false;
		/** He is in cover at the clicked object already: the in-cover corner peek / shimmy / wrap of that object decide. */
		bool bInCoverAtClickedObject = false;
		/**
		 * He stands at a wall edge and the clicked cover is a wall-edge cover switch target (CoverSwitchRules: in range, Cover.Switch taught): the
		 * switch keeps its preview + double-click dash there (2026-10-20, approved) - no run-and-peek.
		 */
		bool bCoverSwitchTarget = false;
	};

	/** The click may be a run-and-peek (the wall end itself is checked by the traces). */
	CODEXTACTICS_API bool MayRunToPeek(const FClickFacts& Facts);

	/**
	 * A face's slot spans at least MinWidthCm between its exposed ends (a closed side counts as wide: the wall goes on past the corner probes).
	 * LeftCm / RightCm = the slot's distance to each exposed end.
	 */
	CODEXTACTICS_API bool IsFaceWideEnough(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, float MinWidthCm = MinFaceWidthCm);

	/** Normal is a face not in Known yet (no known normal within SameFaceDot of it). Planar unit normals. */
	CODEXTACTICS_API bool IsNewFace(const TArray<FVector2D>& Known, const FVector2D& Normal);

	/** One peekable face of the clicked wall at the click (the probe found its slot, the click lies at its end, it is wide enough). */
	struct FFace
	{
		/** The face's outward planar unit normal. */
		FVector2D Normal = FVector2D(1.0, 0.0);
		/** A point on the face (its slot's wall point). */
		FVector2D Point = FVector2D::ZeroVector;
	};

	/**
	 * Design decision 3 (#55): the face of the wall that faces the operative - the one whose normal points most towards him from its point
	 * (dot of the normal with the unit direction to him); a tie (within 0.01) goes to the nearer face point. INDEX_NONE without faces.
	 */
	CODEXTACTICS_API int32 ChooseNearFace(const TArray<FFace>& Faces, const FVector2D& Operative);

	/**
	 * The planar directions the clicked wall is probed from to find its faces round the click: Count directions evenly round the circle,
	 * the first = Primary (the clicked face's normal, or the camera's side for a top hit). Unit vectors; Count < 1 gives Primary alone.
	 */
	CODEXTACTICS_API TArray<FVector2D> ProbeDirections(const FVector2D& Primary, int32 Count = 8);

	/**
	 * A cover entry is the run-and-peek's arrival (the peek starts at once): the run-and-peek is still armed, the entered slot lies on the
	 * ordered corner's wall and within ToleranceCm of the ordered corner slot.
	 */
	CODEXTACTICS_API bool IsPeekArrival(bool bArmed, bool bSameWall, float DistanceFromOrderedCm, float ToleranceCm = ArrivalToleranceCm);

	/** Who carries a run-and-peek order out. */
	enum class EGroup : uint8
	{
		/** The ordered operative alone (solo mode - the primary play mode - or no group around him). */
		Single,
		/** A box selection he belongs to: the member nearest the corner takes it, the others stack behind him (PlanGroupCornerPeek). */
		BoxSelection,
		/** The squad following him in formation: the group wall order at that corner, the squad's corner peek there once it stands at it. */
		Formation,
	};

	/**
	 * The group a run-and-peek click orders: a box selection of 2+ that includes him -> BoxSelection (when the group corner peek is on, else
	 * Single); else in formation (not solo, followers that join a group cover order) -> Formation; else Single.
	 */
	CODEXTACTICS_API EGroup DecideGroup(bool bMultiSelection, bool bOperativeSelected, bool bGroupCornerPeekOn, bool bSoloMode, int32 GroupCoverOthers);
}
