#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the gradual snow cover overlay (environment phase 4c, docs/systems/environment-weather.md §6 / §12): which static
 * meshes get the M_SnowOverlay overlay material and in which order. UE-only (no Godot reference). Tested in
 * CodexTactics.Environment.SnowOverlay.*; used by UWeatherSnowOverlayComponent.
 */
namespace SnowOverlayRules
{
	/** Actor / component tag: never put snow on this (indoor props, set dressing the artist wants clean). */
	CODEXTACTICS_API FName OptOutTag();

	/** What is known about one mesh component (gathered by the component from the world; plain values here). */
	struct FSnowTargetTraits
	{
		/** A UStaticMeshComponent with a mesh (the only kind that gets the overlay). */
		bool bStaticMesh = false;
		/** An instanced / hierarchical instanced mesh or instanced foliage (many instances = an expensive second pass). */
		bool bInstanced = false;
		/** Owned by a pawn (characters, vehicles). */
		bool bPawn = false;
		/** Landscape / terrain (its own material handles snow). */
		bool bLandscape = false;
		/** Drawn at all (visible, owner not hidden, rendered in the main pass - a roof cut away by the cutaway still counts). */
		bool bVisible = true;
		/** Already has an overlay material (component, per slot or the mesh asset's default) - never replaced. */
		bool bHasOverlay = false;
		/** Tagged NoSnow (actor or component). */
		bool bOptOut = false;
		/** Every material of the mesh is translucent (ice sheets, glass, water): an opaque snow layer would look wrong. */
		bool bTranslucentOnly = false;
		/** Drawn by Nanite: UE 5.8 Nanite proxies never draw an overlay material (only the non-Nanite fallback proxy does). */
		bool bNanite = false;
		/** Inside an interior: a closed / shelter camera zone box contains it (centre inside, top below the zone's top). */
		bool bInterior = false;
		/** Something static is straight above its top (a roof, a ceiling, an overhang): under cover, no snow. */
		bool bCovered = false;
		/** Bounds sphere radius, cm. */
		float BoundsRadiusCm = 0.f;
		/** Widest horizontal extent of the bounds (full width), cm. */
		float FootprintCm = 0.f;
		/** Height of the bounds (full), cm. */
		float HeightCm = 0.f;
	};

	/** Thresholds of the filter. */
	struct FSnowTargetConfig
	{
		/** Smaller meshes (debris, cans, cables) are skipped: they are not worth a second pass. */
		float MinBoundsRadiusCm = 35.f;
		/** Wider meshes are ground slabs / sky meshes, not objects (same 60 m limit as the roof cutaway). */
		float MaxFootprintCm = 6000.f;
		/** A flat slab at least this wide is ground (floor tiles of a graybox map): the landscape / ground material owns its snow, cm. */
		float GroundMinFootprintCm = 2000.f;
		/** ... "flat" = height below this share of the footprint. */
		float GroundMaxHeightShare = 0.05f;
		/** Nanite meshes may take part: the caller switches them to their fallback mesh while they hold the overlay (Codex.Env.SnowOverlayNanite). */
		bool bAllowNaniteFallback = false;
	};

	/** Why a mesh is (not) a target. Accept first; the rest in the order they are checked. */
	enum class ESnowTargetVerdict : uint8
	{
		Accept,
		NotStaticMesh,
		Instanced,
		Pawn,
		Landscape,
		Hidden,
		HasOverlay,
		OptOut,
		TranslucentOnly,
		Nanite,
		TooSmall,
		TooLarge,
		Ground,
		Interior,
		Covered
	};

	/** The verdict for one mesh (cheap checks first; Interior / Covered come last - they need world queries the caller does lazily). */
	CODEXTACTICS_API ESnowTargetVerdict Classify(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config);

	/** Classify(...) == Accept. */
	CODEXTACTICS_API bool IsTarget(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config);

	/** "Accept", "Covered", ... for logs. */
	CODEXTACTICS_API const TCHAR* VerdictToString(ESnowTargetVerdict Verdict);

	/** The cheap part of the filter passed (everything but Interior / Covered): the world queries are worth running. */
	CODEXTACTICS_API bool PassesCheapChecks(const FSnowTargetTraits& Traits, const FSnowTargetConfig& Config);

	/**
	 * Mesh bounds (MeshMin / MeshMax, world space) inside an interior zone box (ZoneTransform without scale, half extent ZoneHalfExtent):
	 * the bounds centre lies inside the box and the mesh top is not more than TopToleranceCm above the box top - a roof slab resting on
	 * (or above) the zone's top stays outdoor.
	 */
	CODEXTACTICS_API bool IsInsideInterior(const FVector& MeshMin, const FVector& MeshMax, const FTransform& ZoneTransform, const FVector& ZoneHalfExtent,
		float TopToleranceCm = 50.f);

	/**
	 * A mesh inside an interior zone is indoor unless it is the zone's roof: its top reaches the highest top of the zone's meshes
	 * (RoofTopZ) within ToleranceCm. Zone boxes often enclose the whole building, roof included.
	 */
	CODEXTACTICS_API bool IsBelowInteriorRoof(double MeshTopZ, double RoofTopZ, double ToleranceCm = 30.0);

	/** The overlay is worth drawing at all: snow + frost above a hair (0 = remove it everywhere, no extra pass). */
	CODEXTACTICS_API bool ShouldShowOverlay(float SnowAmount, float FrostAmount);

	/** One candidate for the selection: its squared distance to the view focus. */
	struct FSnowCandidate
	{
		int32 Index = INDEX_NONE;
		double DistanceSq = 0.0;
	};

	/**
	 * Selection order: candidates within MaxDistanceCm of the focus (0 = no limit), nearest first; ties keep the input order. At most
	 * MaxCount indices are returned when MaxCount > 0 (the caller may reject some later and take the next ones, so it gets the whole
	 * sorted list when bKeepAll).
	 */
	CODEXTACTICS_API TArray<int32> OrderCandidates(const TArray<FSnowCandidate>& Candidates, float MaxDistanceCm, int32 MaxCount, bool bKeepAll = false);
}
