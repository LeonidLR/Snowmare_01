#pragma once

#include "CoreMinimal.h"
#include "AI/PmcDefenceRules.h"

class FJsonObject;

/**
 * Cold indoors on GENERATED maps (T-021, designer bug 2026-11-19 on L_Gen_Depot_1_X6: the cold does not go away even in a fully
 * closed building; designer model of the same day: the more broken doors / windows and breached walls, the colder the building;
 * all closed = 100 % airtight; the further from an open door, the warmer). Hand-made maps lower the cold through ACameraZoneVolume
 * (Closed 0, Shelter 0.5); the level-generator builder places none. This is the pure rule that replaces the zone volume from the map's
 * levelgen layout (PmcDefenceRules::ParseLayout / BuildingAt / RoofZ are reused, not re-parsed). Godot reference: none (UE-only).
 *
 * Model (numbers in Content/Data/Survival/cold_shelter.json, FColdShelterConfig):
 *  - INDOORS: the feet are inside a building footprint, above its ground and below its roof slab.
 *  - Each building's openings carry a weight (door 3, window 1, loophole 0.5, wall breach / roof hole 4). leak = open weight / all weight
 *    (open = open, kicked out, broken, breached; an intact closed door / glazed or boarded window is sealed). Airtightness = 1 - leak.
 *  - Base multiplier = leak * leak_to_cold: all sealed 0, a fully open building 1 (= open air).
 *  - Draught: near an open opening on the SAME storey the multiplier is 1 within draught_full_m (3.5 m) and falls linearly to the base at
 *    draught_fade_m (10 m). Indoor multiplier = max(base, the strongest draught). Openings on other storeys only count in the leak.
 * A camera zone, when one holds the operative, still wins (the caller checks it first, ResolveZoneMultiplier).
 */

/** Numbers of the indoor model (cold_shelter.json; every field has a commented key there). */
struct CODEXTACTICS_API FColdShelterConfig
{
	/** "roof_margin_cm": the feet must be below the roof slab by at least this much (on the roof they stand ON the slab), cm. */
	float RoofMarginCm = 30.f;
	/** "ground_margin_cm": the feet may be this far below the layout's ground (a sunken threshold), cm. */
	float GroundMarginCm = 100.f;
	/** "storey_tolerance_cm": a storey index counts the feet this much below the next slab as already on it (ramps, stairs), cm. */
	float StoreyToleranceCm = 50.f;
	/** "weights": the share of one opening in the leak. */
	float WeightDoor = 3.f;
	float WeightWindow = 1.f;
	float WeightLoophole = 0.5f;
	/** A wall breach and a hole in the roof. */
	float WeightBreach = 4.f;
	/** "leak_to_cold": base multiplier = leak * this (1: a fully open building is open air). */
	float LeakToCold = 1.f;
	/** "draught_full_m": the multiplier is 1 within this distance of an open opening on the same storey, cm. */
	float DraughtFullCm = 350.f;
	/** "draught_fade_m": the draught has fallen to the base multiplier at this distance, cm. */
	float DraughtFadeCm = 1000.f;
};

/** What the indoor model says about one position. */
struct CODEXTACTICS_API FColdShelterSample
{
	/** Layout building index the feet are in; INDEX_NONE = outside (multiplier 1, not indoors). */
	int32 Building = INDEX_NONE;
	/** Share of the building's opening weight that is open, 0..1. */
	float Leak = 0.f;
	/** leak * leak_to_cold. */
	float Base = 1.f;
	/** The final indoor multiplier max(Base, draught); 1 outside. */
	float Multiplier = 1.f;
	/** Planar distance to the nearest open opening on this storey, cm; negative = none. */
	float NearestOpenCm = -1.f;

	bool IsIndoors() const { return Building != INDEX_NONE; }
};

namespace ColdShelterRules
{
	/** Values of Json (cold_shelter.json) over Base; missing keys keep Base. Comment keys ("comment", "_comment*") are ignored. */
	CODEXTACTICS_API FColdShelterConfig ConfigFromJson(const FJsonObject& Json, const FColdShelterConfig& Base = FColdShelterConfig());

	/**
	 * State text: open / ajar / broken / smashed / kicked / breached / destroyed / blown / missing -> true (the cold gets in);
	 * closed / locked / sealed / boarded / barred / welded / glazed / intact and empty / unknown (the generator writes none: a shut door,
	 * a glazed window) -> false.
	 */
	CODEXTACTICS_API bool IsStateOpen(const FString& State);

	/** The weight of one opening by its kind ("door" / "window" / "loophole" / "breach" / "roof_hole"; unknown = window). */
	CODEXTACTICS_API float GetOpeningWeight(const FPmcLayoutOpening& Opening, const FColdShelterConfig& Config);

	/** The layout's own opening states (index-aligned with Building.Openings). */
	CODEXTACTICS_API TArray<bool> InitialOpenFlags(const FPmcLayoutBuilding& Building);

	/** open weight / all weight of the building (0 when it has no openings). OpenFlags is index-aligned with Building.Openings. */
	CODEXTACTICS_API float ComputeLeak(const FPmcLayoutBuilding& Building, TConstArrayView<bool> OpenFlags, const FColdShelterConfig& Config);

	/** Base multiplier of a leak: max(0, Leak * LeakToCold). */
	CODEXTACTICS_API float GetBaseMultiplier(float Leak, const FColdShelterConfig& Config);

	/** The draught of one open opening at DistanceCm: 1 up to DraughtFull, Base from DraughtFade on, linear between. */
	CODEXTACTICS_API float GetDraughtMultiplier(float DistanceCm, float Base, const FColdShelterConfig& Config);

	/** Storey index of the feet: floor((FeetZ - GroundZ + StoreyTolerance) / level height), at least 0. */
	CODEXTACTICS_API int32 GetStoreyIndex(const FPmcLayoutAccess& Layout, float FeetZ, const FColdShelterConfig& Config);

	/** Feet between the building's ground - GroundMargin and its roof slab - RoofMargin (the planar test is BuildingAt's). */
	CODEXTACTICS_API bool IsInside(const FPmcLayoutAccess& Layout, const FPmcLayoutBuilding& Building, const FVector& Feet, const FColdShelterConfig& Config);

	/**
	 * The indoor model at Feet (world cm, FEET not the capsule centre). OpenFlags / Leak are supplied per building by the caller (the
	 * runtime state: UColdShelterSubsystem keeps them and recomputes a leak only when an opening changes); OpenFlagsOf returns
	 * the flags of a layout building index. Outside: Building INDEX_NONE, multiplier 1.
	 */
	CODEXTACTICS_API FColdShelterSample Sample(const FPmcLayoutAccess& Layout, const FVector& Feet, const FColdShelterConfig& Config,
		TFunctionRef<TConstArrayView<bool>(int32 BuildingIndex)> OpenFlagsOf, TFunctionRef<float(int32 BuildingIndex)> LeakOf);

	/** Same with the layout's own states (tests, no runtime changes). */
	CODEXTACTICS_API FColdShelterSample SampleLayout(const FPmcLayoutAccess& Layout, const FVector& Feet, const FColdShelterConfig& Config);

	/**
	 * The zone multiplier of FColdEnvironment: a camera zone that holds the operative wins (its own multiplier, so hand-made maps are
	 * unchanged); otherwise the indoor multiplier; otherwise WaveMultiplier (the wave's cold_drain_mult).
	 */
	CODEXTACTICS_API float ResolveZoneMultiplier(bool bInCameraZone, float CameraZoneMultiplier, const FColdShelterSample& Shelter, float WaveMultiplier);
}
