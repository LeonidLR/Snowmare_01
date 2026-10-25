#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Wall layout of a squad in formation mode (group cover order) (Godot reference: none (UE-only, design
 * docs/design/FORMATION_SECTORS.md "Wall", contract docs/design/FORMATION_CONTRACT.md 2; reference implementation
 * Tools/TrainingGround/src/movement/rules.ts Layout / wallRoom)). Pure. Nobody looks through the wall: all stand along it
 * WallSpacing apart, facing the OPEN side, which they share out. Tests: CodexTactics.Formation.Wall.*, parity
 * CodexTactics.Formation.Parity.WallLayout.
 *
 * Frame: C = the point of the wall segment nearest to the clicked slot; the outward normal N = ClickedSlot - C (the slot on the
 * wall line: the left-hand normal of A -> B); K stands at C + N * (Thickness / 2 + WallFaceOffset); "right" = the right hand of a
 * man facing the wall (yaw N + 270); a step = WallSpacing along it. Look yaws relative to N, positive = right (UE yaw):
 * - 1: K looks N (6 o'clock of the heading into the wall: away from it).
 * - 2: the partner Z on the roomier side (RoomRight >= RoomLeft: right); right: K N + 45 (7:30), Z one step right N - 45 (4:30);
 *   left: K N - 45, Z one step left N + 45.
 * - 3: K N (6), P one step right N - WallFlankDeg (4), L one step left N + WallFlankDeg (8).
 * - 4: the 4th (Z) at the end with more room (RoomRight >= RoomLeft: right - a tie goes RIGHT; the caller breaks a travel tie with
 *   BreakRoomTie). Right: Z two steps right N - WallEndDeg (3-4), his neighbour P N - WallNeighbourDeg (5), L N + WallFlankDeg;
 *   left: Z two steps left N + WallEndDeg (8-9), L N + WallNeighbourDeg (7), P N - WallFlankDeg. K N.
 * Slots past a wall end are not refused here (the runtime's group cover rules handle a short wall).
 */
namespace FormationWallRules
{
	/** The pair's look offsets from the outward normal (7:30 / 4:30), deg. */
	inline constexpr float PairLookDeg = 45.f;

	/** The layout: slots in slot order (K, Z / K, P, L / K, P, L, Z), Yaw = the look yaw. */
	CODEXTACTICS_API TArray<FFormationSlot> Layout(const FFormationWall& Wall, const FVector2D& ClickedSlot, int32 MemberCount, float RoomLeft,
		float RoomRight, const FFormationConfig& Config = FFormationConfig());

	/** The outward normal's yaw Layout uses (ClickedSlot - the nearest wall point; on the line: the left-hand normal of A -> B). */
	CODEXTACTICS_API float OutwardYaw(const FFormationWall& Wall, const FVector2D& ClickedSlot);

	/** Free wall length beyond the nearest wall point to ClickedSlot on each side (left / right seen facing the wall), cm. */
	CODEXTACTICS_API void WallRoom(const FFormationWall& Wall, const FVector2D& ClickedSlot, float& OutRoomLeft, float& OutRoomRight);

	/**
	 * Design "tie -> the side he reaches with less travel": when RoomLeft == RoomRight and the 4th man's travel to the left end is
	 * shorter, nudges RoomLeft up by 1 cm so Layout puts him left (Layout itself sends a tie right).
	 */
	CODEXTACTICS_API void BreakRoomTie(float& RoomLeft, float& RoomRight, float TravelLeft, float TravelRight);
}
