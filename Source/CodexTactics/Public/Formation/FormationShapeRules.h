#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Formation slots (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md "Roles and sectors" / "Formations",
 * contract docs/design/FORMATION_CONTRACT.md 2; reference implementation Tools/TrainingGround/src/movement/rules.ts Slots)). Pure:
 * where each member of a squad stands for a formation shape, relative to the leader's position and heading.
 * Tests: CodexTactics.Formation.Shape.*, parity CodexTactics.Formation.Parity.Slots.
 *
 * Roles by squad size (Roles): <= 1 = K; 2 = K, Z (the pair's partner); 3 = K, P, Z (v2.2: the triad's rear man walks backwards like
 * the four's, P sweeps both flanks); >= 4 = K, P, L, Z. With three: Z at TriadRearBack straight back (diamond / wedge), one spacing
 * behind P (column), on the left (line), last in both echelons. Offsets below are
 * (forward, right) from the leader, cm; K is always on the leader (0, 0):
 * - Diamond / wedge: P (-SlotBackOffset, +SlotSideOffset), L (-SlotBackOffset, -SlotSideOffset), Z (-DiamondRearBack, 0); with 2 members
 *   the partner Z (-PairBackOffset, +PairSideOffset) behind the right shoulder.
 * - Blizzard (tight diamond): P / L (-BlizzardBackOffset, +-BlizzardSideOffset), Z (-2 * BlizzardBackOffset, 0); pair Z as P.
 * - Column (herringbone): P (-ColumnBaseBack, +ColumnStagger), L (-ColumnBaseBack - ColumnSpacing, -ColumnStagger),
 *   Z (-ColumnBaseBack - 2 * ColumnSpacing, 0); pair Z as P.
 * - Line: P (0, +LineSpacing), L (0, -LineSpacing), Z (0, +2 * LineSpacing); pair Z as P.
 * - Echelon right (obstacle on the left): order K, L, P, Z - from the diamond the LEFT man goes second (minimal travel); the n-th man
 *   (-n * EchelonStep, +n * EchelonStep). Echelon left: order K, P, L, Z, mirrored to the left. Pair: K, Z.
 * - Hedgehog: a ring of radius HedgehogRadius whose centre lies HedgehogRadius behind the leader (the leader stays on his spot, the
 *   others close the ring behind him): 4 = P (-r, +r), L (-r, -r), Z (-2r, 0); 3 = P / L at +-120 deg round the centre; 2 = Z (-2r, 0).
 * - Wall: no slots (empty) - FormationWallRules::Layout owns the wall.
 */
namespace FormationShapeRules
{
	/** Roles of a squad of MemberCount in slot order (index 0 = the leader). */
	CODEXTACTICS_API TArray<EFormationRole> Roles(int32 MemberCount);

	/** Echelon order index of Role (echelon right K, L, P, Z; left K, P, L, Z; 2 members K, Z); 0 for a role not in the order. */
	CODEXTACTICS_API int32 EchelonIndex(EFormationKind Formation, EFormationRole Role, int32 MemberCount);

	/** Local (forward, right) offset of Role's slot from the leader, cm (the header comment's table). */
	CODEXTACTICS_API FVector2D LocalSlot(EFormationKind Formation, EFormationRole Role, int32 MemberCount, const FFormationConfig& Config = FFormationConfig());

	/**
	 * World slots of every member in slot order (Roles). Position = the slot; Yaw = the member's halt look yaw
	 * (FormationSectorRules::LookYaw in Halt, no passage width; not part of the contract's output). Wall: empty.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> Slots(EFormationKind Formation, int32 MemberCount, const FVector2D& LeaderPos, float LeaderYaw,
		const FFormationConfig& Config = FFormationConfig());

	/**
	 * v1.2: the hedgehog round a centre point (the heat source; design "backs to the fire, faces out"): the leader stands HedgehogRadius
	 * in front of Center along Yaw, then Slots(Hedgehog) - every member on the ring with his back to the centre; Yaw = the look yaw
	 * (LookYaw halt). The runtime moves the leader to slot 0 so the ring's centre is the fire.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> HedgehogSlots(const FVector2D& Center, int32 MemberCount, float Yaw,
		const FFormationConfig& Config = FFormationConfig());

	/**
	 * v2.1: the TRAIL column - the column's offsets (LocalSlot(Column): ColumnBaseBack + i * ColumnSpacing back, ColumnStagger aside)
	 * measured ALONG the leader's path instead of straight back, so the column bends round a corner and passes a wall on his side.
	 * Trail = the leader's recent positions, oldest first, the last one = the leader now (empty: no slots); a trail shorter than the
	 * column is extended straight back from its oldest point (along its oldest segment, or against LeaderYaw for a single point).
	 * Slot order = Roles; Yaw = the trail's travel direction at the slot (the leader: LeaderYaw) - the reference for its look. Who
	 * stands at which place: FormationAssignRules::AssignPlaces.
	 * v2.3 (red team #7): Walls given - the stagger aside shrinks in 5 cm steps (down to 0 = on the trail) until the slot keeps
	 * ColumnStaggerClear from every wall surface and the line from its trail point to it is clear (no walls = the full stagger).
	 */
	CODEXTACTICS_API TArray<FFormationSlot> TrailSlots(const TArray<FVector2D>& Trail, float LeaderYaw, int32 MemberCount,
		const FFormationConfig& Config = FFormationConfig(), const TArray<FFormationWall>& Walls = TArray<FFormationWall>());
}
