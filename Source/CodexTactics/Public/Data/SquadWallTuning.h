#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The squad at walls - tunables (UE-only, no Godot reference; the game designer's bug report 2026-11-02 items 5 / 3 / 2 on L_Gen_Depot_1,
 * docs/systems/squad-and-orders.md "The squad at walls (2026-11-03)"). Content/Data/AI/squad_walls.json, snake_case keys, a missing key
 * keeps the default; read once on the first Get() (Reload re-reads it). The defaults are the agent's proposal, pending the game designer's
 * confirmation.
 */
struct CODEXTACTICS_API FSquadWallTuning
{
	/** Item 5: an end man of the wall layout at an open corner looks this far off the outward normal round it, deg (wall_corner_look_deg). */
	float WallCornerLookDeg = 112.f;
	/**
	 * Item 3: a group cover follower who finds no slot at the leader's cover searches another cover this far round the leader's slot before
	 * he goes prone behind it, cm (follower_cover_radius_m, metres in the file).
	 */
	float FollowerCoverRadiusCm = 600.f;
	/** Item 3: that cover must face the same threat side - its outward normal within this of the leader's slot's, deg (follower_cover_side_deg). */
	float FollowerCoverSideDeg = 60.f;
	/**
	 * Item 2: on the wall-leadership re-form a crouched follower whose place in the stack is farther than this stands up for the walk (he
	 * crouches again at his place), cm (reform_stand_m, metres in the file).
	 */
	float ReformStandCm = 300.f;
	/**
	 * Item 2: the path braking distance of a follower's move into a wall order's place, cm (wall_path_braking_cm; an own order keeps the
	 * movement config's 140 cm). The formation's own arrival law (FormationRuntime::FollowerMove, speed = distance x FollowGain) already slows
	 * him; the path's braking on top of it made the last metre a crawl (2-40 cm/s for seconds - the "shuffle in place").
	 */
	float WallPathBrakingCm = 30.f;
};

namespace SquadWallTuning
{
	/** Content/Data/AI/squad_walls.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The tuning in force (the file on the first call, else the defaults). */
	CODEXTACTICS_API const FSquadWallTuning& Get();
	/** Parameters of Json over Base (clamped to sane ranges). */
	CODEXTACTICS_API FSquadWallTuning FromJson(const FJsonObject& Json, const FSquadWallTuning& Base = FSquadWallTuning());
	/** Re-reads the file into Get(); false (the defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool Reload();
}
