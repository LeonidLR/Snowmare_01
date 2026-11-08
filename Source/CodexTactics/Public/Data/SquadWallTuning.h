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
	 * Bug 2026-11-03 (the game designer, L_Gen_Depot_1: a corner peek at the far end of the same 31 m wall - all three stood up and faced
	 * their walk, the rear wide open): a member within this of the new stack's face line walks into it ALONG THE SAME WALL - he keeps his
	 * crouch and his look sector (the rear-looker walks backwards / sideways); ReformStandCm and the face-the-walk rule apply only to a walk
	 * that leaves the wall. 0 = item 2's rules everywhere, cm (reform_along_wall_off_m, metres in the file).
	 */
	float ReformAlongWallOffCm = 120.f;
	/**
	 * Item 2: the path braking distance of a follower's move into a wall order's place, cm (wall_path_braking_cm; an own order keeps the
	 * movement config's 140 cm). The formation's own arrival law (FormationRuntime::FollowerMove, speed = distance x FollowGain) already slows
	 * him; the path's braking on top of it made the last metre a crawl (2-40 cm/s for seconds - the "shuffle in place").
	 */
	float WallPathBrakingCm = 30.f;
	/**
	 * #57 (user request 2026-11-07): both corners of a wall held, the members between them sweep their look from one corner's peek direction to
	 * the other's in this long (one way; there and back = twice this), eased at the ends, s (corner_sweep_s).
	 */
	float CornerSweepSeconds = 6.f;
	/**
	 * The game designer's answer 3 (2026-11-08, "6 s, option V"): CornerSweepSeconds is the sweep in combat / alert (an enemy seen or heard by
	 * the squad, a member under fire / suppressed); out of combat it runs slowly, one way in this long, s (corner_sweep_calm_s).
	 */
	float CornerSweepCalmSeconds = 12.f;
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
