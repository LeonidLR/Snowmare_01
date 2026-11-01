#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationGuardRules.h"
#include "Formation/FormationTypes.h"

class FJsonObject;

/**
 * The custom formation - pure rules (Godot reference: none (UE-only; game designer decisions 2026-11-01, docs/design/FORMATION_SECTORS.md
 * "Custom formation", contract docs/design/FORMATION_CONTRACT.md 17; reference implementation Tools/TrainingGround/src/movement/custom.ts)).
 * Tests: CodexTactics.Formation.Custom.*, parity CodexTactics.Formation.Parity.Custom*.
 *
 * The player places the squad's shape himself; the shape is stored relative to the anchor (the leader when it was confirmed): each member's
 * place (forward / right of the anchor, cm) and his look relative to the heading. In the game the slots are taken in the current leader's
 * frame (they rotate with the heading like every formation), the looks are fixed (no coverage allocator), a wall cutting the shape folds the
 * squad into the trail column (the guard's level 2) and the shape opens again after classify.hold_distance of path.
 */

/** One stored member of a custom shape (contract 17.1). */
struct FFormationCustomMember
{
	/** The operative's SquadIndex. */
	int32 Squad = 0;
	/** His place forward of the anchor along the heading, cm (negative = behind). */
	float Fwd = 0.f;
	/** His place right of the anchor (heading + 90), cm. */
	float Right = 0.f;
	/** His look relative to the heading, deg (-180, 180]. */
	float Look = 0.f;
};

/** A custom formation slot: Members[0] = the anchor (0, 0, look 0), the others by squad index; empty = an empty slot (contract 17.1). */
struct FFormationCustomShape
{
	TArray<FFormationCustomMember> Members;

	bool IsEmpty() const { return Members.IsEmpty(); }
	/** The stored member of a squad index, or null. */
	const FFormationCustomMember* Find(int32 Squad) const { return Members.FindByPredicate([Squad](const FFormationCustomMember& M) { return M.Squad == Squad; }); }
};

/** One slot of CustomSlots: who stands there, his role label, place and look (contract 17.2). */
struct FFormationCustomSlot
{
	int32 Squad = 0;
	EFormationRole Role = EFormationRole::Commander;
	FVector2D Position = FVector2D::ZeroVector;
	float Yaw = 0.f;
	/** True = a stored place (the player's look); false = an extra behind the rearmost place. */
	bool bFixed = true;
};

/** A ghost of the placement mode (contract 17.6). */
struct FFormationCustomGhost
{
	int32 Squad = 0;
	FVector2D Position = FVector2D::ZeroVector;
	float Yaw = 0.f;
};

/** CanPlace's answer (contract 17.6). */
enum class EFormationCustomPlace : uint8
{
	Ok,
	TooFar,
	TooClose
};

namespace FormationCustomRules
{
	/**
	 * A member may stand at most this far from the anchor, cm. 2026-11-01 (the game designer, bug_05: the old 10 m reach "ruins everything"): only
	 * a generous sanity reach against a mis-click on the far horizon (the wall fold traces leader -> slot lines, the camera frames the squad);
	 * 15-30 m shapes are accepted. The tactical pause's order radius never applies to the placement.
	 */
	inline constexpr double MaxRadius = 4000.0;
	/** A press-and-drag shorter than this is a plain click (the default look), cm. */
	inline constexpr double DragMin = 50.0;
	/** Custom slots per squad. */
	inline constexpr int32 SlotCount = 3;
	/** Members in a shape. */
	inline constexpr int32 MaxMembers = 4;

	/** Rounds the offsets to 0.1 cm and the looks to 0.1 deg (half away from zero; -0 -> 0; the look then normalised) - contract 17.1. */
	CODEXTACTICS_API FFormationCustomShape RoundShape(const FFormationCustomShape& Shape);

	/**
	 * Contract 17.2: the slots for the members present (squad indices, the LEADER first). The leader takes the anchor's place; the operative
	 * stored at the anchor takes the place the leader had in the shape (an extra when the leader is not stored); everybody else his own place;
	 * a member the shape lacks walks behind the rearmost place (k x column.spacing, looking back); a stored member not present is skipped.
	 * Roles by output order (Roles(count)). Present beyond MaxMembers is cut. Empty shape or nobody present -> none.
	 */
	CODEXTACTICS_API TArray<FFormationCustomSlot> CustomSlots(const FFormationCustomShape& Shape, const TArray<int32>& Present, const FVector2D& LeaderPos,
		float LeaderYaw, const FFormationConfig& Config = FFormationConfig());

	/** Contract 17.3: the roles (slot order, never the leader) whose line from the leader passes within guard.clearance of a wall body. */
	CODEXTACTICS_API TArray<EFormationRole> BlockedRoles(const TArray<FFormationWall>& Walls, const TArray<FFormationCustomSlot>& Slots, const FVector2D& LeaderPos,
		const FFormationConfig& Config = FFormationConfig());

	/**
	 * Contract 17.4: the guard for the custom shape - level Requested (Custom) or Trail (the trail column, Effective Column, cause Wall) when a
	 * wall cuts the shape here or at Ahead, or a member is cut off; no mirror, no turn fold; back to the shape after classify.hold_distance.
	 */
	CODEXTACTICS_API FFormationGuardState Resolve(const TArray<FFormationWall>& Walls, const FFormationCustomShape& Shape, const TArray<int32>& Present,
		const FVector2D& LeaderPos, float LeaderYaw, const TOptional<FFormationPose>& Ahead, const FFormationGuardState& State, float StepDistance,
		const FFormationConfig& Config = FFormationConfig(), bool bAllInSight = true);

	/** Contract 17.5: the player's fixed look, unless it points into a wall within look.wall_dist - then FormationSectorRules::ClearLook. */
	CODEXTACTICS_API float Look(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float CurrentYaw, const FFormationConfig& Config = FFormationConfig(),
		const TOptional<float>& Held = TOptional<float>());

	/** Contract 17.6: the look a placement press-and-drag sets (shorter than DragMin = DefaultYaw, the squad's heading). */
	CODEXTACTICS_API float DragLook(const FVector2D& GhostPos, const FVector2D& CursorPos, float DefaultYaw);

	/** Contract 17.6: may Squad's ghost go to Pos (TooClose: within slot.min_spacing of another ghost; TooFar: someone beyond MaxRadius of the anchor). */
	CODEXTACTICS_API EFormationCustomPlace CanPlace(const TArray<FFormationCustomGhost>& Ghosts, int32 AnchorSquad, int32 Squad, const FVector2D& Pos,
		const FFormationConfig& Config = FFormationConfig());

	/** Contract 17.6: the shape the ghosts make in the anchor ghost's frame (rounded); no anchor ghost -> empty. */
	CODEXTACTICS_API FFormationCustomShape ShapeFrom(const TArray<FFormationCustomGhost>& Ghosts, int32 AnchorSquad);

	/** Contract 17.1 JSON: { "members": [ { squad, fwd, right, look } ] }; an empty shape -> null. */
	CODEXTACTICS_API TSharedPtr<FJsonObject> ToJson(const FFormationCustomShape& Shape);
	/** Parses ToJson's object (null / malformed -> an empty shape; members beyond MaxMembers and repeated squads dropped). */
	CODEXTACTICS_API FFormationCustomShape FromJson(const TSharedPtr<FJsonObject>& Object);
	/** The shape as a one-line JSON string ("" for an empty one) - the campaign carry / smokes. */
	CODEXTACTICS_API FString ToJsonString(const FFormationCustomShape& Shape);
	/** Parses ToJsonString's text ("" / malformed -> empty). */
	CODEXTACTICS_API FFormationCustomShape FromJsonString(const FString& Text);
}
