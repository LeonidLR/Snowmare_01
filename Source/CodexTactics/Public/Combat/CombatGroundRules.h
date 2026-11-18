#pragma once

#include "CoreMinimal.h"

/**
 * Combat on uneven ground (G007, designer bug B17-6: on ramps / stairs the shots were held by the platform dead zone and the bites
 * measured the height from the feet + 1 m). UE-only, no Godot reference beyond the two rules it refines (player.gd
 * is_target_in_dead_zone, enemy_base.gd can_melee_attack). Pure rules + the tunables; the world probes are CombatGroundQuery.
 * Tunables: Content/Data/Combat/fire_gate.json "dead_zone" / "ground_probe" / "melee_reach", loaded by FireGateRules with the rest
 * of that file (the defaults below mirror it: CodexTactics.Combat.FireGate.TuningFileMirrorsDefaults). Heights / distances cm.
 */

/** The platform dead zone (Godot is_target_in_dead_zone), fire_gate.json "dead_zone". */
struct CODEXTACTICS_API FDeadZoneConfig
{
	/** Shooter centre above his feet (Godot measures from the capsule centre). "chest_offset_cm" */
	float ChestOffsetCm = 100.f;
	/** Drop from that centre to the target's feet. "min_delta_z_cm" */
	float MinDeltaZCm = 180.f;
	/** Horizontal limit. "max_horizontal_cm" */
	float MaxHorizontalCm = 240.f;
	/** How steep the shot is, degrees down. "min_angle_deg" */
	float MinAngleDeg = 45.f;
	/**
	 * Floor-to-floor drop between two neighbouring floor samples that is a level edge (a platform lip). Above a walkable step and
	 * a stair's riser, below the lowest platform (0.8 m). Not the chest-to-feet min_delta_z_cm. "edge_drop_cm"
	 */
	float EdgeDropCm = 60.f;
};

/** The floor probe along the line between two pairs of feet, fire_gate.json "ground_probe". */
struct CODEXTACTICS_API FGroundProbeConfig
{
	/** Distance between two floor probes along the line. "spacing_cm" */
	float SpacingCm = 25.f;
	/** A probe that misses / lands off the previous floor retries this far to each side (a narrow stair seen diagonally). "lateral_cm" */
	float LateralCm = 30.f;
	/** A probe starts this far above the higher pair of feet. "up_cm" */
	float UpCm = 60.f;
	/** ... and ends this far below the lower pair (no floor there = unknown, never an edge). "down_cm" */
	float DownCm = 60.f;
	/** At most this many probes; a longer line spreads them wider. "max_probes" */
	float MaxProbes = 12.f;
	/**
	 * The melee shared-ground verdict between one attacker and one target is kept this long (CombatGround::CanMelee /
	 * MeleeReachPlanar share it: the flank bite reach, the bite and the close-in step probe once, not three times a tick).
	 * 0 = no cache. "cache_s"
	 */
	float CacheS = 0.25f;
};

/** The enemy's blow on uneven ground (Godot can_melee_attack), fire_gate.json "melee_reach". */
struct CODEXTACTICS_API FMeleeReachConfig
{
	/** The Godot body point above the feet: off shared ground the blow is measured to it. "body_offset_cm" */
	float BodyOffsetCm = 100.f;
	/** Feet-to-feet height difference a blow can bridge at most (Godot 1.2 m). "height_cap_cm" */
	float HeightCapCm = 120.f;
	/** Shared ground: no rise between two neighbouring floor samples above this (a walkable step / stair riser). "step_max_cm" */
	float StepMaxCm = 45.f;
	/**
	 * On shared ground the body-point measure blends into the feet-to-feet one as the slope steepens; from this angle on it is
	 * feet-to-feet. Flat shared ground is exactly the old body-point rule, so the reach has no jump. "slope_full_deg"
	 */
	float SlopeFullDeg = 15.f;
};

/** One floor sample on the line from the first pair of feet (DistanceCm 0) to the second. */
struct CODEXTACTICS_API FFloorSample
{
	/** Horizontal distance from the first pair of feet. */
	float DistanceCm = 0.f;
	/** Floor height; meaningless when !bHit. */
	float Z = 0.f;
	/** False = the probe found no floor (open riser, grating, off the side): unknown. */
	bool bHit = false;
};

namespace CombatGroundRules
{
	/**
	 * Probe distances strictly between 0 and HorizontalCm, evenly spaced at most SpacingCm apart (more than MaxProbes needed: wider).
	 * OutSpacingCm = the spacing used (neighbouring samples are at most this far apart). Short lines (< SpacingCm) get none: the two
	 * pairs of feet are neighbours already.
	 */
	CODEXTACTICS_API TArray<float> ProbeDistances(float HorizontalCm, const FGroundProbeConfig& Config, float& OutSpacingCm);

	/** Two samples are neighbours when both hit and lie at most SpacingCm (+1 %) apart - a missed probe between them breaks that. */
	CODEXTACTICS_API bool AreNeighbours(const FFloorSample& A, const FFloorSample& B, float SpacingCm);

	/**
	 * A level edge between the shooter (Profile[0]) and the target (Profile.Last()): two NEIGHBOURING samples whose floor drops by at
	 * least EdgeDropCm toward the target. A missed probe is unknown: it never makes an edge, and the samples on either side of it are
	 * not neighbours. A slope / stair falls a little per sample and is no edge.
	 */
	CODEXTACTICS_API bool HasLevelEdge(TConstArrayView<FFloorSample> Profile, float EdgeDropCm, float SpacingCm);

	/**
	 * The two stand on shared ground (melee only): no rise / drop between neighbouring hit samples above StepMaxCm. An isolated
	 * missed probe (open riser / grated stair, final review 6) is bridged: the hits on both sides of it must be within StepMaxCm of
	 * each other. Two misses in a row, or a missed first / last sample, are unknown = not shared. A ramp or a stair (open or not)
	 * is shared; a ledge, a crate or a gap is not. The dead-zone edge (HasLevelEdge) keeps every miss unknown.
	 */
	CODEXTACTICS_API bool IsSharedGround(TConstArrayView<FFloorSample> Profile, float StepMaxCm);
}
