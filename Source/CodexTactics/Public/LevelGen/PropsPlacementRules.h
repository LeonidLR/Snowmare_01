#pragma once

#include "CoreMinimal.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsLayoutView.h"

class FJsonObject;

/**
 * Pure deterministic props placement: the common algorithm (docs/design/levelgen/PROPS_LAYERS.md 2.2) + crates and barrels (2.3, task
 * C3 of section 5) + turrets, mines (doorways, crates, field) and loot (2.3, task C4, PropsPlacementC4.cpp) + the static validation
 * subset (2.4) + the failure report (2.5).
 * No world, no floats in any decision: integer cm, a 50 cm lattice, mulberry32 (PropsRng.h), ties by (y, x, layer name).
 * The TS port (task T2) follows "TS port notes" in PROPS_LAYERS.md - the exact candidate order, filter order and RNG call order -
 * and must reproduce the golden outputs Content/Data/LevelGen/props/fixtures/golden_*.json byte for byte.
 * Godot reference: none (UE-only level authoring). Tests: CodexTactics.LevelGen.Props.Placement.* (C3), CodexTactics.LevelGen.Props.C4.*.
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

/**
 * The rules every lattice / anchor placement shares (count, where, spacing, gaps, exclusions, sizes). The layer's own rules and the
 * mines layer's "field" sub-rule are each one of these.
 */
struct FPropsCommonRules
{
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
	/** where.anchors (C4 turrets / loot): ambush_points | roof_edges | behind_tall_cover | dead_ends | roof_centres | building_interiors. */
	TArray<FString> Anchors;
	/** variant -> half footprint (cm) from the balance kinds: radius_cm, or ceil(max(size_cm x, y) / 2). Unknown variants: 0. */
	TMap<FString, int32> HalfSizeCm;
};

/** Turrets (2.3): coverage of a route from anchor candidates. */
struct FPropsTurretRules
{
	/** cover_route.route; empty = the layout's first mandatory route. */
	FString RouteId;
	/** cover_route.range_cm [min, max] (max clamped to FPropsLayoutView::MaxRadiusCm). */
	int32 RangeMinCm = 0;
	int32 RangeMaxCm = 0;
	/** cover_route.arc_deg, 1..360. */
	int32 ArcDeg = 360;
	/** cover_route.min_covered_m: candidates scoring below are dropped (score = covered metres = covered 1 m samples). */
	int32 MinCoveredM = 0;
	/** cover_route.behind_cover_gap_cm: a "behind_tall_cover" anchor stands this far off the cover's side. */
	int32 BehindCoverGapCm = 0;
};

/** Mines, doorway sub-rule. */
struct FPropsMineDoorRules
{
	bool bEnabled = false;
	/** count | percent. */
	FString Mode = TEXT("percent");
	int32 Value = 0;
	/** building_door | gate (empty = both). */
	TArray<FString> Kinds;
	/** mandatory_route: forbid (default) | allow (designer 6.1 #3: opt-in per layer). */
	bool bAllowMandatory = false;
	int32 InsideCm = 0;
	FString Trigger;
	FString Variant;
};

/** Mines, crate sub-rule: a share of the source layer's unlocked crates carries a trap (a mined crate is red, 6.1 #1). */
struct FPropsMineCrateRules
{
	bool bEnabled = false;
	FString SourceLayer = TEXT("crates");
	int32 Percent = 0;
	FString Trigger;
	FString Variant;
};

/** Mines, field sub-rule: the common algorithm with its own count / where / spacing / exclude. */
struct FPropsMineFieldRules
{
	bool bEnabled = false;
	FPropsCommonRules Common;
	FString Trigger;
	FString Variant;
};

/** Loot (2.3): containers in crates, then loose piles at anchors, until the budget is spent. */
struct FPropsLootRules
{
	int32 BudgetValue = 0;
	/** The layer's tables (known ones only, in rules order) and their loot_tables values (> 0). */
	TArray<FString> Tables;
	TArray<int32> TableValues;
	/** containers.crates_share in thousandths; containers.source_layer. */
	int32 ContainersShareMilli = 0;
	/** containers.budget_share in thousandths: the crates spend at most floor(budget_value x share) (kept ones included); 1000 = no cap. */
	int32 ContainersBudgetMilli = 1000;
	FString SourceLayer = TEXT("crates");
	/** loose.roof_table (piles on roofs use only it; empty = roofs draw from the tables like the ground); its value. */
	FString RoofTable;
	int32 RoofTableValue = 0;
	/** loose.prefer_far_from_route_cm [min, max] and loose.weights { near, band, far }. */
	int32 FarMinCm = 0;
	int32 FarMaxCm = 0;
	int32 WeightNear = 1;
	int32 WeightBand = 1;
	int32 WeightFar = 1;
	/** containers.variant / loose.variant. */
	FString ContainerVariant = TEXT("loot_crate");
	FString PileVariant = TEXT("loot_pile");
};

/** A layer's rules merged with the balance preset, as integers. Built by PropsPlacementRules::BuildRules. */
struct FPropsRules : FPropsCommonRules
{
	/** crates | barrels | turrets | mines | loot. */
	FString Kind;
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
	FPropsTurretRules Turret;
	FPropsMineDoorRules MineDoors;
	FPropsMineCrateRules MineCrates;
	FPropsMineFieldRules MineField;
	FPropsLootRules Loot;
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
	/** Turrets only (written for them only): anchors scoring below cover_route.min_covered_m. */
	int32 Coverage = 0;
};

/** A sub-rule's counts (mines: doorways / crates / field; loot: containers / loose). */
struct FPropsSubReport
{
	int32 Eligible = 0;
	int32 Requested = 0;
	int32 Placed = 0;
	int32 Kept = 0;
	/** Mines crates / loot containers: kept objects dropped because their crate is gone. */
	int32 Dropped = 0;
	/** Mines doorways: mandatory-route doors removed by mandatory_route forbid. */
	int32 Forbidden = 0;
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
	/** The layer kind; ReportToJson writes the C4 sections for turrets / mines / loot only. */
	FString Kind;
	/** Mines: SubA doorways, SubB crates, SubC field. Loot: SubA containers, SubB loose. */
	FPropsSubReport SubA;
	FPropsSubReport SubB;
	FPropsSubReport SubC;
	/** Loot: budget_value, the value spent (kept included), and "a table was still affordable but no room was left". */
	int32 BudgetValue = 0;
	int32 BudgetSpent = 0;
	bool bBudgetShort = false;

	bool IsShort() const { return ShortBy > 0 || bBudgetShort; }
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
	/** The layer id of each OtherObjects entry (parallel array; C4 mines / loot read their source crates from it). */
	TArray<FString> OtherLayerIds;
	/** layer id -> kind of every layer of the file (C4: which layers are mines). */
	TMap<FString, FString> LayerKinds;
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
	constexpr const TCHAR* KindTurrets = TEXT("turrets");
	constexpr const TCHAR* KindMines = TEXT("mines");
	constexpr const TCHAR* KindLoot = TEXT("loot");
	/** Turrets: extra field "covered_m" = the score (covered route metres) at placement. */
	constexpr const TCHAR* CoveredField = TEXT("covered_m");
	/** Doorway mines: extra field "width_cm" = the opening the tripwire spans (the gap width or layout_view door_fallback_width_cm). */
	constexpr const TCHAR* WidthField = TEXT("width_cm");
	/** Sample step along the cover route: one sample per started metre of a leg. */
	constexpr int32 RouteSampleCm = 100;
	/** Turret yaw candidates: 0, 15, ..., 345. */
	constexpr int32 TurretYawStepDeg = 15;
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

	/** True for crates | barrels | turrets | mines | loot. */
	CODEXTACTICS_API bool IsPlaceableKind(const FString& Kind);

	/** Every kind (crates / barrels: C3; turrets / mines / loot: C4). Never throws; too small an area gives a short report. */
	CODEXTACTICS_API FPropsPlacementResult Place(const FPropsPlacementInput& In);

	/**
	 * Turret coverage of a point (C4): covered metres of the cover route seen from P on Layer within range and the best 15 degree arc;
	 * OutYaw = that arc's centre. 0 when the route is missing. Exact integer maths (PROPS_LAYERS.md section 7, C4 notes).
	 */
	CODEXTACTICS_API int32 TurretCoverage(const FPropsLayoutView& View, const FPropsRules& Rules, FIntPoint P, const FString& Layer, int32& OutYaw);

	/**
	 * Crate colours (designer 6.1 #1): every crate of a crates-kind layer after the overrides -> props_balance crate_colors.dangerous
	 * ("red") when an object of a mines-kind layer attaches to it, else crate_colors.safe ("blue"). Derived, never stored: a mines
	 * re-roll does not rewrite the crates layer.
	 */
	CODEXTACTICS_API TMap<FString, FString> ResolveCrateColors(const FLevelPropsFile& File, const FJsonObject& Balance);

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
	 * and the report; never touches the overrides or the other layers. Objects of other layers whose "attach" names an object of this
	 * layer that is gone are reported (warning "attach_orphaned <id>"); the mines / loot re-roll drops them. OutResult (optional) gets
	 * the placement result.
	 */
	CODEXTACTICS_API bool RerollLayer(FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, const FString& LayerId,
		uint32 Seed, TArray<FString>& Errors, FPropsPlacementResult* OutResult = nullptr);

	/**
	 * "Generate all" (2.2): re-rolls every enabled, unlocked layer with its stored seed in the fixed kind order crates -> barrels ->
	 * turrets -> mines -> loot (file order inside a kind). False when any re-roll was refused (the others still run).
	 */
	CODEXTACTICS_API bool RerollAll(FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, TArray<FString>& Errors);
}
