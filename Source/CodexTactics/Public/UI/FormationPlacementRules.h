#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationCustomRules.h"

/**
 * The custom formation's placement mode - pure state (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md "Custom formation",
 * contract 17.6). Godot reference: none (UE-only). Tests: CodexTactics.UI.FormationPlacement.*.
 *
 * Every member has a ghost from the start (the slot's shape when editing it, else where he stands now): what the player sees is what is stored.
 * A click on an operative (or his ghost) selects him; LMB down on the ground = his spot, the drag to the release = his look (a release within
 * CUSTOM_DRAG_MIN = a plain click: he looks along the squad's heading = the anchor ghost's look). Placing him again replaces his ghost; the anchor
 * (the leader when the mode began) moves the frame. A spot too close to another ghost or out of the 40 m sanity reach is refused (CanPlace; the 10 m reach was lifted 2026-11-01, bug_05). Confirm =
 * the shape in the anchor ghost's frame (ShapeFrom).
 */

/** What a placement input did (the controller reacts: HUD line, ghost flash). */
enum class EFormationPlacementResult : uint8
{
	None,
	Selected,
	Pressed,
	Placed,
	TooFar,
	TooClose,
	/** A ground press with nobody selected. */
	NoOperative
};

class CODEXTACTICS_API FFormationPlacement
{
public:
	/** Starts the mode for custom slot Slot (1..3), the anchor = AnchorSquad (the leader), one ghost per member (the anchor's included). */
	void Begin(int32 Slot, int32 AnchorSquad, const TArray<FFormationCustomGhost>& Ghosts);
	/** Ends it (confirmed or cancelled): nothing selected, no ghosts. */
	void End();

	bool IsActive() const { return bActive; }
	int32 GetSlot() const { return Slot; }
	int32 GetAnchorSquad() const { return AnchorSquad; }
	TOptional<int32> GetSelected() const { return Selected; }
	const TArray<FFormationCustomGhost>& GetGhosts() const { return Ghosts; }
	const FFormationCustomGhost* FindGhost(int32 Squad) const;
	/** LMB is down on a spot (the drag's start); unset otherwise. */
	TOptional<FVector2D> GetPressPoint() const { return PressPoint; }
	/** A plain click's look: the squad's heading = the anchor ghost's look (0 without one). */
	float DefaultYaw() const;

	/** A click on operative / ghost Squad: he is selected (a press in progress is dropped). */
	EFormationPlacementResult Select(int32 Squad);
	/** The ghost whose feet are within Radius of Ground (the nearest), or unset. */
	TOptional<int32> GhostAt(const FVector2D& Ground, double Radius) const;
	/** LMB down on the ground: the selected operative's spot (NoOperative when nobody is selected). */
	EFormationPlacementResult Press(const FVector2D& Ground);
	/** While pressed: the look the release at Cursor would set (the arrow preview); unset when not pressed. */
	TOptional<float> PreviewLook(const FVector2D& Cursor) const;
	/** LMB up at Cursor: the selected ghost goes to the press point looking DragLook(press, Cursor, DefaultYaw) unless CanPlace refuses. */
	EFormationPlacementResult Release(const FVector2D& Cursor, const FFormationConfig& Config = FFormationConfig());
	/** Drops a press in progress (the cursor left the ground, RMB, ...). */
	void CancelPress() { PressPoint.Reset(); }
	/** The shape the ghosts make (ShapeFrom in the anchor ghost's frame). */
	FFormationCustomShape Shape() const { return FormationCustomRules::ShapeFrom(Ghosts, AnchorSquad); }

private:
	bool bActive = false;
	int32 Slot = 0;
	int32 AnchorSquad = 0;
	TOptional<int32> Selected;
	TOptional<FVector2D> PressPoint;
	TArray<FFormationCustomGhost> Ghosts;
};

/**
 * Pure helpers of the placement mode's input and ghosts (bug_05, the game designer's PIE 2026-11-01). Tests: CodexTactics.UI.FormationPlacement.*.
 */
namespace FormationPlacementRules
{
	/** A ghost pose at least this far (its largest bone rotation) from the mesh's reference pose is a real pose, not the T-pose, deg. */
	inline constexpr float PosedMinDeg = 10.f;

	/** The largest bone rotation of Pose away from RefPose (local bone space, the bones both have), deg; 0 for an empty pose. */
	CODEXTACTICS_API float PoseOffRefDeg(TConstArrayView<FTransform> Pose, TConstArrayView<FTransform> RefPose);
	/** True when a pose PoseOffRefDeg away from the reference pose is a real pose (>= PosedMinDeg). */
	CODEXTACTICS_API bool IsPosed(float OffRefDeg);

	/** What a B tap (released before the hold-B menu opened) does. */
	enum class EBTap : uint8
	{
		/** The solo / squad toggle (the old B). */
		SoloToggle,
		/** The placement mode is running: the tap confirms it (as Space) - never the solo toggle, which split the squad and cancelled it. */
		ConfirmPlacement
	};
	CODEXTACTICS_API EBTap ResolveBTap(bool bPlacementActive);
}
