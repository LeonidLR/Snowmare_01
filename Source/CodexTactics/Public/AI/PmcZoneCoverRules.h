#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * ZONE COVER (smart enemies T-08 slice 2; designer decision 2026-11-21; UE-only, no Godot reference). A defended objective zone needs cover: in the real
 * game the level generator will place it at defended goals; in the PMC arena a test option (arg ZoneCover=1, CodexTactics.PmcArena and its smokes) spawns
 * K low-cover blocks inside the objective zone, facing the attackers' approach, so the T-08 holder / fall-back layer has covered zone posts to use.
 * This file is the PURE half (no UWorld): the numbers, the approach direction from the attackers' nav path, the block layout and the footprint tests.
 * The arena (CodexTacticsDev PmcArenaCommand.cpp) snaps the blocks to the ground, skips the ones that hit world geometry or a spawn, spawns them and
 * waits for the navmesh. Numbers: Content/Data/AI/pmc_arena_zone_cover.json (the defaults here equal the shipped file). Tested in
 * CodexTactics.AI.PmcZoneCover.*.
 *
 * Default layout (approach = the unit vector from the zone centre toward where the attackers' path enters the zone):
 *   slot 0: centred CentreDistCm from the centre on the approach bearing,
 *   slot 1 / 2: SideDistCm from the centre at +SideAngleDeg / -SideAngleDeg off the approach bearing;
 * every block's long side runs perpendicular to the approach (its broad face toward the attackers; SideYawFollow turns the side blocks toward their own
 * bearing). A block whose footprint does not fit inside the zone radius (minus EdgeMarginCm) is pulled toward the centre in PullInStepCm steps down to
 * MinDistCm, else dropped; a block overlapping an earlier one is dropped.
 */

/** Numbers of Content/Data/AI/pmc_arena_zone_cover.json (the defaults = the shipped file). Distances cm (the file has metres where the key says _m). */
struct CODEXTACTICS_API FPmcZoneCoverConfig
{
	/** "count" (3): blocks per zone, 1..3 (slot 0, then slot 1, then slot 2). */
	int32 Count = 3;
	/** "centre_dist_m" (3.5): slot 0's centre from the zone centre, on the approach bearing. */
	float CentreDistCm = 350.f;
	/** "side_dist_m" (4.0): slots 1 / 2's centres from the zone centre. */
	float SideDistCm = 400.f;
	/** "side_angle_deg" (60): slots 1 / 2's bearings off the approach. */
	float SideAngleDeg = 60.f;
	/** "side_yaw_follow" (0): 0 = the side blocks face the approach like slot 0, 1 = they face out along their own bearing. */
	float SideYawFollow = 0.f;
	/** "length_cm" (200): the long side (perpendicular to the facing). */
	float LengthCm = 200.f;
	/** "depth_cm" (40): the short side (along the facing). */
	float DepthCm = 40.f;
	/** "height_cm" (110): LOW cover (CoverTraceRules: low cover up to 130 cm, the knee trace at 45 cm hits it, a crouching man hides behind it). */
	float HeightCm = 110.f;
	/** "edge_margin_cm" (20): every footprint corner keeps this inside the zone radius. */
	float EdgeMarginCm = 20.f;
	/** "pull_in_step_cm" (25): a block over the zone's edge moves this much toward the centre per try. */
	float PullInStepCm = 25.f;
	/** "min_dist_m" (1.5): a block never comes nearer the centre than this (the point itself stays open); a block that does not fit by then is dropped. */
	float MinDistCm = 150.f;
	/** "spawn_clear_m" (3): world side - a block's footprint keeps this from either side's spawn spot (the squad line stands round it). */
	float SpawnClearCm = 300.f;
	/** "ground_max_dz_cm" (100): world side - the ground under a block may lie at most this above / below the objective point (else off level, skipped). */
	float GroundMaxDzCm = 100.f;
};

/** One planned block: its footprint (an oriented rectangle) on the zone's plane. */
struct CODEXTACTICS_API FPmcZoneCoverBlock
{
	/** 0 = on the approach bearing, 1 = +side angle, 2 = -side angle. */
	int32 Slot = 0;
	/** The footprint's centre (Z = the zone centre's; the world side snaps it to the ground). */
	FVector Centre = FVector::ZeroVector;
	/** The facing yaw, deg: the block's local X axis (its depth) points this way; its length runs along local Y. */
	float YawDeg = 0.f;
	/** The centre's planar distance from the zone centre, cm (after any pull-in). */
	float DistCm = 0.f;
	/** Half sizes: X = depth / 2, Y = length / 2, Z = height / 2. */
	FVector HalfExtent = FVector::ZeroVector;
	/** It was pulled toward the centre to fit (DistCm < the configured distance). */
	bool bPulledIn = false;
};

namespace PmcZoneCover
{
	/** Reads a zone-cover JSON object over Base (missing keys keep Base; values are clamped to sane ranges). */
	CODEXTACTICS_API FPmcZoneCoverConfig ConfigFromJson(const FJsonObject& Json, const FPmcZoneCoverConfig& Base = FPmcZoneCoverConfig());

	/** Content/Data/AI/pmc_arena_zone_cover.json; the defaults when the file is missing or broken. */
	CODEXTACTICS_API FPmcZoneCoverConfig LoadConfig();

	/**
	 * The approach direction (planar unit vector) from the zone centre: toward the point where AttackerPath (nav path points, spawn first) first crosses into
	 * the zone radius; when the path never enters the zone from outside (empty, too short, starts inside), toward FallbackFrom (the attackers' spawn); +X when
	 * that is the centre itself. bOutFromPath (optional) = the path gave it.
	 */
	CODEXTACTICS_API FVector2D ApproachDirection(const FVector& Centre, float RadiusCm, TConstArrayView<FVector> AttackerPath, const FVector& FallbackFrom,
		bool* bOutFromPath = nullptr);

	/** The footprint's four corners (planar), in order round the rectangle. */
	CODEXTACTICS_API void FootprintCorners(const FPmcZoneCoverBlock& Block, FVector2D OutCorners[4]);

	/** Every footprint corner lies within RadiusCm - MarginCm of Centre (planar). */
	CODEXTACTICS_API bool IsInsideZone(const FPmcZoneCoverBlock& Block, const FVector& Centre, float RadiusCm, float MarginCm);

	/** The planar distance from Point to the footprint (0 when Point lies on or inside it). */
	CODEXTACTICS_API float FootprintDistance(const FPmcZoneCoverBlock& Block, const FVector& Point);

	/** The two footprints overlap (separating-axis test on the plane; touching counts as overlap). */
	CODEXTACTICS_API bool FootprintsOverlap(const FPmcZoneCoverBlock& A, const FPmcZoneCoverBlock& B);

	/**
	 * The block layout for a zone (see the file comment). OutDropped (optional) gets one line per slot that could not be placed ("slot 2: does not fit ...").
	 * Count, the sizes and the distances come from Config; RadiusCm <= 0 or a zero approach places nothing.
	 */
	CODEXTACTICS_API TArray<FPmcZoneCoverBlock> LayoutBlocks(const FVector& Centre, const FVector2D& Approach, float RadiusCm, const FPmcZoneCoverConfig& Config,
		TArray<FString>* OutDropped = nullptr);
}
