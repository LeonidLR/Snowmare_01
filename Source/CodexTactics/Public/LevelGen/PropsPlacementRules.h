#pragma once

#include "CoreMinimal.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsLayoutView.h"

class FJsonObject;

/**
 * Pure deterministic props placement: the common algorithm (docs/design/levelgen/PROPS_LAYERS.md 2.2) + crates and barrels (2.3) + the
 * static validation subset (2.4) + the failure report (2.5); task C3 of section 5. Turrets / mines / loot are C4.
 * No world, no floats in any decision: integer cm, a 50 cm lattice, mulberry32 (PropsRng.h), ties by (y, x, layer name).
 * The TS port (task T2) follows "TS port notes" in PROPS_LAYERS.md - the exact candidate order, filter order and RNG call order -
 * and must reproduce the golden outputs Content/Data/LevelGen/props/fixtures/golden_*.json byte for byte.
 * Godot reference: none (UE-only level authoring). Tests: CodexTactics.LevelGen.Props.Placement.*.
 */

/** A "near" weight clause: the candidate's weight is multiplied by Weight when the obstacle face is within (half size + MaxCm). */
struct FPropsNearClause
{
	/** wall (the walk layer's walls) | cover (layout covers, ground only). */
	FString What;
	int32 MaxCm = 0;
	/** Clamped to 1..PropsPlacementRules::MaxWeight. */
	int32 Weight = 1;
};

/** A layer's rules merged with the balance preset, as integers. Built by PropsPlacementRules::BuildRules. */
struct FPropsRules
{
	/** crates | barrels (others: C4; Place reports them as unsupported). */
	FString Kind;
	/** count | density | per_zone. */
	FString CountMode = TEXT("count");
	/** count: objects; per_zone: objects per matched zone. */
	int32 CountValue = 0;
	/** density: objects per 100 m2 of eligible area, in thousandths (0.25 -> 250). */
	int32 DensityMilli = 0;
	int32 CountMax = MAX_int32;
	/** props_balance.json budgets[kind]. */
	int32 BudgetCap = MAX_int32;
	/** Zone ids; empty or containing "*" = everywhere. */
	TArray<FString> Zones;
	/** Walk layer patterns: an exact name or a prefix ending in '*' ("floor:*"). Default ["ground"]. */
	TArray<FString> WalkLayers;
	TArray<FPropsNearClause> Near;
	/** Variant draw weights (> 0) sorted by key, ordinal (the canonical writer stores rules sorted, so this is the file's order). */
	TArray<TPair<FString, int32>> Variants;
	int32 MinSpacingCm = 0;
	int32 MinGapAnyCm = 0;
	/** Gap to the layout's blocked geometry (walls, footprints, covers); absent = MinGapAnyCm. The designer 2026-11-10: crates may stand right against walls (crates_default wall_gap_cm 5), doorways stay clear through exclude.door_cm. */
	int32 WallGapCm = 0;
	FPropsExclusionParams Exclude;
	/** Barrels: members per cluster (min >= 1, max >= min) and the member ring (cm) around the anchor. */
	int32 ClusterMin = 1;
	int32 ClusterMax = 1;
	int32 RingMinCm = 0;
	int32 RingMaxCm = 0;
	/** Barrels: share of explosive clusters in thousandths (0.3 -> 300). */
	int32 ExplosiveShareMilli = 0;
	int32 ExplosiveMinGapCm = 0;
	int32 ExplosiveNeverSpawnCm = 0;
	int32 ExplosiveNeverEndCm = 0;
	/** variant -> half footprint (cm) from the balance kinds: radius_cm, or ceil(max(size_cm x, y) / 2). Unknown variants: 0. */
	TMap<FString, int32> HalfSizeCm;
};

/** Candidates rejected by the hard filters, by the first failing filter (section 2.5 "rejected"). */
struct FPropsRejectCounts
{
	int32 Blocked = 0;
	int32 Spacing = 0;
	int32 Route = 0;
	int32 Spawn = 0;
	int32 Ladder = 0;
	int32 Door = 0;
	int32 Keepout = 0;
	int32 OtherLayer = 0;
	int32 Other = 0;
};

/** Section 2.5 report. Requested = the target total (kept included), Placed = new objects, Kept = locked / pinned / hand objects. */
struct FPropsPlacementReport
{
	int32 Requested = 0;
	int32 Placed = 0;
	int32 Kept = 0;
	int32 ShortBy = 0;
	/** Lattice points that pass "where" (walk layer + zones) before the hard filters: the density area is Eligible x 0.25 m2. */
	int32 Eligible = 0;
	FPropsRejectCounts Rejected;
	/** The nav gate is the TS fast gate (2.4); the C++ placement always reports "n/a". */
	FString Gate = TEXT("n/a");
	/** Fixed codes (see PROPS_LAYERS.md "TS port notes"), e.g. "explosive_short 1". */
	TArray<FString> Warnings;

	bool IsShort() const { return ShortBy > 0; }
};

struct FPropsPlacementInput
{
	/** Not owned; must outlive the call. Null: nothing is placed (warning "no_layout"). */
	const FPropsLayoutView* View = nullptr;
	FPropsRules Rules;
	FString LayerId;
	uint32 Seed = 0;
	/** This layer's kept objects (locked + pinned + hand, overrides applied), in order. They keep their ids and are counted. */
	TArray<FLevelPropsObject> Kept;
	/** This layer's keep-out disks (radius 0 = the merged MinSpacingCm). */
	TArray<FLevelPropsKeepOut> KeepOuts;
	/** The current objects of all other layers (overrides applied): obstacles for MinGapAnyCm. */
	TArray<FLevelPropsObject> OtherObjects;
	/** Ids a new object must not take (kept ids, override targets, other layers' ids; compared case-sensitively); the counter n skips them. */
	TArray<FString> ReservedIds;
};

struct FPropsPlacementResult
{
	/** Kept objects first (input order), then the new ones in pick order. */
	TArray<FLevelPropsObject> Objects;
	FPropsPlacementReport Report;
};

struct FPropsViolation
{
	FString Id;
	FString Reason;
};

namespace PropsPlacementRules
{
	/** Candidate lattice step (cm). */
	constexpr int32 LatticeCm = 50;
	/** Cap of a candidate weight (product of the near clauses) so the total weight stays below 2^32 for any lattice. */
	constexpr int32 MaxWeight = 1000;
	constexpr const TCHAR* KindCrates = TEXT("crates");
	constexpr const TCHAR* KindBarrels = TEXT("barrels");
	constexpr const TCHAR* VariantBarrel = TEXT("barrel");
	constexpr const TCHAR* VariantBarrelExplosive = TEXT("barrel_explosive");
	/** Extra field of a barrel: the id of its cluster's anchor (the anchor names itself). */
	constexpr const TCHAR* ClusterField = TEXT("cluster");

	/**
	 * Merges the balance preset (Layer.Preset, else "<kind>_default") with Layer.Rules (objects merged key by key recursively, anything
	 * else replaced; exclude.$preset = presets.<name>.exclude under the remaining exclude keys) and reads the integers. False + Errors
	 * on a missing preset or a field of the wrong type (the rest is still read). Never throws.
	 */
	CODEXTACTICS_API bool BuildRules(const FJsonObject& Balance, const FLevelPropsLayer& Layer, FPropsRules& Out, TArray<FString>& Errors);

	/**
	 * The placement input of one layer of a props file: rules (BuildRules), seed = the layer's, ApplyOverrides -> this layer's kept
	 * objects (locked / pinned / hand), its keep-outs and every other layer's objects. False + Errors when the layer does not exist;
	 * rule errors are reported but the input is still filled.
	 */
	CODEXTACTICS_API bool MakeInput(const FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, const FString& LayerId,
		FPropsPlacementInput& Out, TArray<FString>& Errors);

	/** The common algorithm for crates / barrels. Never throws; too small an area gives a short report. */
	CODEXTACTICS_API FPropsPlacementResult Place(const FPropsPlacementInput& In);

	/**
	 * Section 2.4 static checks of the NEW objects (ids not in In.Kept) of a layer: walk layer, blocked, exclusions, keep-outs, the gap to
	 * other layers, spacing (crates: all pairs; barrels: anchors / ring), explosive gap and never-within. Kept objects are the C5
	 * validator's job (LOCKED/HAND INVALID). Empty = valid.
	 */
	CODEXTACTICS_API TArray<FPropsViolation> Validate(const FPropsPlacementInput& In, const TArray<FLevelPropsObject>& Objects);

	/** The report as the layer's "report" JSON object (keys written sorted by the props file writer). */
	CODEXTACTICS_API TSharedRef<FJsonObject> ReportToJson(const FPropsPlacementReport& Report);

	/**
	 * Re-rolls one layer of a props file with Seed: refuses (false + Errors) a frozen file, a locked or missing layer, or an unsupported
	 * kind. Stores the seed, the objects (the kept objects' stored records - hand-added ones stay in the overrides - then the new ones)
	 * and the report; never touches the overrides. OutResult (optional) gets the placement result.
	 */
	CODEXTACTICS_API bool RerollLayer(FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, const FString& LayerId,
		uint32 Seed, TArray<FString>& Errors, FPropsPlacementResult* OutResult = nullptr);
}
