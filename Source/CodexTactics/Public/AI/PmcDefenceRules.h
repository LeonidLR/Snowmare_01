#pragma once

#include "CoreMinimal.h"

/**
 * PMC defender roles for an objective round (docs/design/PMC_DEFENCE.md D1, G008).
 * One access guard and the rest as sector shooters. Pure rules: UPmcSquadSubsystem gathers the roof, the layout
 * and the living men, and executes the posts. EPmcRole stays Attacker / Defender; these roles are not orders.
 *
 * The tactical file's access[] is ids only (stair:<id> / ladder:<id> / hatch:<id>). Ladder tops (xyz) exist on
 * ladder-only roofs. A stair or a hatch has no xyz there: ParseLayout reads them from the level's levelgen_layout
 * (buildings[].stairs[].top, hatches[].pos). Tested in CodexTactics.AI.PmcDefence.*.
 */

/** A defender's job on the objective. None = not assigned (an attacker, or no objective). */
enum class EPmcDefenceRole : uint8
{
	None,
	/** Stands off the roof access (the stair head, the hatch, or inside the ladder top) and watches it. */
	AccessGuard,
	/** Holds one wedge of the arc that faces the attackers. */
	SectorShooter
};

/** How the defenders reach the roof. None = no access was found. */
enum class EPmcAccessKind : uint8
{
	None,
	Stairs,
	Hatch,
	Ladder
};

/** Where the access xyz came from. */
enum class EPmcAccessSource : uint8
{
	None,
	/** ladder_tops of a ladder-only roof in the tactical file. */
	Tactical,
	/** stairs[].top / hatches[].pos (or a layout ladder top) in the levelgen layout. */
	Layout
};

/** Frozen defence numbers (pmc_commander.json, not trainer genes). Distances are cm, matching the other weights. */
struct CODEXTACTICS_API FPmcDefenceConfig
{
	/** "guard_dist_m" (4): a defender within this of the access, on its level, counts for the guard share. */
	float GuardDistCm = 400.f;
	/** "guard_inset_m" (1.5): the guard stands this far from the access toward the roof, so he is not in the hole. */
	float GuardInsetCm = 150.f;
	/** "min_spacing_m" (5): a cover slot this close to a claimed one scores x SpacingPenalty. */
	float MinSpacingCm = 500.f;
	/** "spacing_penalty" (0.25): applied once, not once per teammate. */
	float SpacingPenalty = 0.25f;
	/** "area_factor" (2): on the ground the defended area is the point's radius times this. */
	float AreaFactor = 2.f;
	/** "shift_after_s" (8): a sector shooter may relocate along his wedge after this long on the post. */
	float ShiftAfterSeconds = 8.f;
	/** "shift_no_target_s" (6): with no target seen for this long (and shift_after_s on the post), he shifts. */
	float ShiftNoTargetSeconds = 6.f;
	/** "shift_cooldown_s" (20): the least time between two shifts of one shooter. */
	float ShiftCooldownSeconds = 20.f;
	/** "sector_arc_deg" (180): the arc, centred on the yaw from the objective toward the attacker spawn. */
	float SectorArcDeg = 180.f;
	/** Same level as the objective / the access (the arena's objective level tolerance). */
	float LevelToleranceCm = 150.f;
	/** A roof's area reaches this far past the furthest overwatch spot from the spot centroid. */
	float AreaSlackCm = 200.f;
};

/** The chosen roof access for one round. IsSet() is false when nothing was found (guard share is then n/a, not 0). */
struct CODEXTACTICS_API FPmcDefenceAccess
{
	EPmcAccessKind Kind = EPmcAccessKind::None;
	EPmcAccessSource Source = EPmcAccessSource::None;
	FVector Location = FVector::ZeroVector;
	/** The layout / tactical id without the stair: / ladder: / hatch: prefix. */
	FString Id;

	bool IsSet() const { return Kind != EPmcAccessKind::None; }
};

/** One overwatch spot (tactical file spots[].pos / covered_m, or a layout stand-in). */
struct CODEXTACTICS_API FPmcDefenceSpot
{
	FVector Location = FVector::ZeroVector;
	float CoveredM = 0.f;
};

/** A cover slot the sector picker may use when the wedge has no overwatch spot. */
struct CODEXTACTICS_API FPmcDefenceSlot
{
	FVector Location = FVector::ZeroVector;
	float Score = 0.f;
};

/** One living defender as the role assignment sees him. */
struct CODEXTACTICS_API FPmcDefenceMan
{
	/** Squad index. Ties (nearest the access, the previous guard) go to the lowest index. */
	int32 Index = 0;
	FVector Feet = FVector::ZeroVector;
	/** The role he already had. A living guard keeps it; sectors are split again because N changed. */
	EPmcDefenceRole PreviousRole = EPmcDefenceRole::None;
};

/** One man's post. Sector is INDEX_NONE for the guard and for a sole defender (he has no wedge). */
struct CODEXTACTICS_API FPmcDefencePost
{
	int32 Index = 0;
	EPmcDefenceRole Role = EPmcDefenceRole::None;
	int32 Sector = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	/** Degrees, UE yaw. The guard looks at the access (a ladder guard therefore looks out through the top). */
	float Yaw = 0.f;
	bool bHasPost = false;
	/** The overwatch spot he stands on (INDEX_NONE: a cover slot, the guard inset, or the wedge fallback). */
	int32 SpotIndex = INDEX_NONE;
};

/** One stair of a layout building. Top is the landing on `To` (world cm, no Z in the file). */
struct CODEXTACTICS_API FPmcLayoutStair
{
	FString Id;
	FVector2D Top = FVector2D::ZeroVector;
	int32 From = 0;
	int32 To = 0;
};

/** One roof hatch. Pos is world cm, no Z in the file. */
struct CODEXTACTICS_API FPmcLayoutHatch
{
	FString Id;
	FVector2D Pos = FVector2D::ZeroVector;
	int32 Floor = 0;
};

/**
 * One exterior wall opening of a layout building (openings[] kind door / window / loophole, a legacy doors[] entry, or a roof_holes[]
 * entry as kind "roof_hole", state "open", on the top storey). State is the optional "state" text of the entry (open / closed /
 * locked / broken ...); empty when the file has none (the generator writes none today). Read by Survival/ColdShelterRules (the cold
 * indoors on generated maps).
 */
struct CODEXTACTICS_API FPmcLayoutOpening
{
	FString Id;
	/** "door" / "window" / "loophole" (a legacy doors[] entry is "door"). */
	FString Kind;
	/** The entry's "state" text, lower case; empty when absent. */
	FString State;
	int32 Floor = 0;
	/** World cm centre (no Z in the file). A roof hole's is its first corner. Valid only with bHasPos. */
	FVector2D Pos = FVector2D::ZeroVector;
	bool bHasPos = false;
};

/**
 * One building. Footprint is the unrotated [min, max] rectangle; RotationDeg spins it about its centre
 * (levelgen rotation.ts, UE yaw). Stairs and hatches are already world coordinates.
 */
struct CODEXTACTICS_API FPmcLayoutBuilding
{
	FString Id;
	FVector2D Min = FVector2D::ZeroVector;
	FVector2D Max = FVector2D::ZeroVector;
	float RotationDeg = 0.f;
	int32 Floors = 1;
	bool bRoofWalkable = false;
	TArray<FPmcLayoutStair> Stairs;
	TArray<FPmcLayoutHatch> Hatches;
	/** Exterior openings (openings[]; the legacy doors[] only when openings[] has no door; roof_holes[] as "roof_hole"). Partition openings are not read. */
	TArray<FPmcLayoutOpening> Openings;
};

/** A layout ladder. To is "roof:<building>" for a roof climb. Top is world cm, no Z in the file. */
struct CODEXTACTICS_API FPmcLayoutLadder
{
	FString Id;
	FString To;
	FVector2D Top = FVector2D::ZeroVector;
	int32 LevelTo = 0;
};

/** The access-relevant slice of a levelgen layout. */
struct CODEXTACTICS_API FPmcLayoutAccess
{
	float GroundZ = 0.f;
	/** One storey, cm (levelgen LEVEL_HEIGHT). Not a trainer gene. */
	float LevelHeightCm = 300.f;
	TArray<FPmcLayoutBuilding> Buildings;
	TArray<FPmcLayoutLadder> Ladders;
};

/** One overwatch roof as the defence needs it (gathered from the tactical file, or empty on Depot_1). */
struct CODEXTACTICS_API FPmcRoofRef
{
	FString Building;
	float Z = 0.f;
	EPmcAccessKind AccessKind = EPmcAccessKind::None;
	/** access[] tokens, prefixes included ("ladder:LAD_B_gallery"). */
	TArray<FString> AccessIds;
	TArray<FPmcDefenceSpot> Spots;
};

/** One ladder top from ladder_only_roofs (xyz, index-aligned with that roof's ladders[]). */
struct CODEXTACTICS_API FPmcLadderTop
{
	FString Building;
	FString LadderId;
	FVector Location = FVector::ZeroVector;
};

/** One guard-share sample. bApplicable is false when the round has no access: the metric is n/a, not 0. */
struct CODEXTACTICS_API FPmcGuardSample
{
	bool bApplicable = false;
	/** Any living defender within guard_dist of the access on its level. Meaningful only when bApplicable. */
	bool bNear = false;
};

/** The access, spots and roof an objective point would defend. Access unset: not a walkable roof with a way up. */
struct CODEXTACTICS_API FPmcDefenceSite
{
	FPmcDefenceAccess Access;
	TArray<FPmcDefenceSpot> Spots;
	float LevelZ = 0.f;
	/** A tactical overwatch roof was chosen, so the area is the spot span. */
	bool bRoof = false;
	FVector Inward = FVector::ZeroVector;
	bool bHasInward = false;
};

/** One sample of a defending team: living feet, and the cover objects men are actually in. */
struct CODEXTACTICS_API FPmcDefenceSnapshot
{
	bool bActive = false;
	FPmcDefenceAccess Access;
	TArray<FVector> Feet;
	/** Cover-object keys of the men who are in cover. A man on a post and not in cover adds none. */
	TArray<FString> CoverKeys;
};

namespace PmcDefenceRules
{
	CODEXTACTICS_API const TCHAR* RoleName(EPmcDefenceRole Role);
	CODEXTACTICS_API const TCHAR* AccessKindName(EPmcAccessKind Kind);
	CODEXTACTICS_API const TCHAR* AccessSourceName(EPmcAccessSource Source);

	/** "stairs" / "stair" / "hatch" / "ladder" (anything else is None). */
	CODEXTACTICS_API EPmcAccessKind ParseAccessKind(const FString& Name);

	/**
	 * The levelgen layout's buildings, stairs, hatches and ladders. False when the text is not a JSON object
	 * (Out is then empty). A skipped building is listed in Errors; the rest are still returned.
	 */
	CODEXTACTICS_API bool ParseLayout(const FString& JsonText, FPmcLayoutAccess& Out, TArray<FString>& Errors);

	/** The building whose footprint contains Point (planar). The smallest area wins a tie. INDEX_NONE if none. */
	CODEXTACTICS_API int32 BuildingAt(const FPmcLayoutAccess& Layout, const FVector& Point);

	/** Roof slab Z: ground_z + floors * level height. */
	CODEXTACTICS_API float RoofZ(const FPmcLayoutAccess& Layout, const FPmcLayoutBuilding& Building);

	/**
	 * The site sits on that building's walkable roof (the slab, within ToleranceCm). A ground point in the
	 * footprint is not a roof, so the roofs scenario skips it.
	 */
	CODEXTACTICS_API bool IsWalkableRoofSite(const FPmcLayoutAccess& Layout, int32 BuildingIndex, float SiteZ, float ToleranceCm);

	/**
	 * No tactical file (Depot_1). The roof stair (to >= floors) nearest the objective, else the nearest hatch,
	 * else the layout ladder whose `to` is roof:<building> nearest the objective. Z is the roof slab.
	 * None when the building has no way up.
	 */
	CODEXTACTICS_API FPmcDefenceAccess ResolveLayoutAccess(const FPmcLayoutAccess& Layout, int32 BuildingIndex, const FVector& Objective);

	/**
	 * A tactical roof. Ladder: xyz from LadderTops (source tactical). Stairs / hatch: the layout entry with that id,
	 * Z = Roof.Z (source layout). None when the xyz cannot be found.
	 */
	CODEXTACTICS_API FPmcDefenceAccess ResolveTacticalAccess(const FPmcRoofRef& Roof, TConstArrayView<FPmcLadderTop> LadderTops,
		const FPmcLayoutAccess* Layout);

	/**
	 * The overwatch roof on the objective's level whose spot lies closest to the objective (planar).
	 * INDEX_NONE when none is on that level.
	 */
	CODEXTACTICS_API int32 PickRoof(TConstArrayView<FPmcRoofRef> Roofs, const FVector& Objective, float LevelToleranceCm);

	/** Mean of the spot positions (zero when there are none). */
	CODEXTACTICS_API FVector SpotCentroid(TConstArrayView<FPmcDefenceSpot> Spots);

	/**
	 * The guard's feet: GuardInsetCm from the access toward InwardTarget (the spot centroid, or the building centre),
	 * on the access's Z. The yaw looks from those feet back at the access. A ladder's top is outside the roof, so
	 * that look faces out through the top; a stair or a hatch is the hole, so the same look faces the hole.
	 */
	CODEXTACTICS_API FVector GuardPostLocation(const FPmcDefenceAccess& Access, const FVector& InwardTarget, float GuardInsetCm);

	/** UE yaw (degrees) of the direction From -> To. 0 when the two points are the same. */
	CODEXTACTICS_API float YawDegrees(const FVector& From, const FVector& To);

	/**
	 * Which wedge of the arc (centred on ApproachYawDeg) contains Point, as seen from Origin.
	 * 0 .. SectorCount-1, or INDEX_NONE outside the arc / on the origin. One sector is the whole arc.
	 */
	CODEXTACTICS_API int32 SectorIndex(const FVector& Origin, const FVector& Point, float ApproachYawDeg, int32 SectorCount, float ArcDeg);

	/** The overwatch spot in the wedge with the highest covered_m (the lowest index breaks a tie). INDEX_NONE if none. */
	CODEXTACTICS_API int32 PickSectorSpot(TConstArrayView<FPmcDefenceSpot> Spots, const FVector& Origin, float ApproachYawDeg,
		int32 Sector, int32 SectorCount, float ArcDeg);

	/**
	 * The next spot of the same wedge after CurrentSpot, walking the wedge by bearing (wraps). The current spot
	 * itself when it is the only one. INDEX_NONE when the wedge has no spot.
	 */
	CODEXTACTICS_API int32 NextEdgeSpot(TConstArrayView<FPmcDefenceSpot> Spots, const FVector& Origin, float ApproachYawDeg,
		int32 Sector, int32 SectorCount, float ArcDeg, int32 CurrentSpot);

	/**
	 * Roles for the living men. A previous guard who is still here keeps the access; otherwise the man nearest
	 * the access (lowest index on a tie). One living defender with an access is the guard and has no sector.
	 * Two defenders: one guard and one shooter who owns the whole arc. No access: every man is a sector shooter
	 * (the guard share is then n/a). Posts are filled from spots, else slots, else a point on the wedge bisector.
	 * InwardTarget null: the spot centroid, else the objective.
	 */
	CODEXTACTICS_API TArray<FPmcDefencePost> AssignRoles(TConstArrayView<FPmcDefenceMan> Men, const FPmcDefenceAccess& Access,
		const FVector& Objective, float ApproachYawDeg, TConstArrayView<FPmcDefenceSpot> Spots, TConstArrayView<FPmcDefenceSlot> Slots,
		const FVector* InwardTarget, const FPmcDefenceConfig& Config);

	/**
	 * On a roof (spots not empty): the same level as LevelZ and within the furthest spot from the spot centroid
	 * plus the slack. On the ground: the same level and within RadiusCm * AreaFactor of the objective.
	 * Off the level is outside even when the planar distance is small.
	 */
	CODEXTACTICS_API bool IsInsideDefendedArea(const FVector& Feet, const FVector& Objective, float LevelZ, float RadiusCm,
		TConstArrayView<FPmcDefenceSpot> Spots, const FPmcDefenceConfig& Config);

	/**
	 * The test / no-plan fallback: on the objective's level and DistanceCm within the area. DistanceCm is the
	 * commander's level-aware distance, so the caller must also say whether the feet are on the level.
	 */
	CODEXTACTICS_API bool IsInsideAreaFallback(bool bOnObjectiveLevel, float DistanceCm, float RadiusCm, float AreaFactor);

	/**
	 * A defender's Objective score. Inside the area it is 0 (including a push and a late clock): he is not pulled
	 * into the capture ring. Outside it is max(weight, 0) * 0.7 * max(0.5, health). The attacker's score is elsewhere.
	 */
	CODEXTACTICS_API float DefenderObjectiveScore(bool bInsideArea, float ObjectiveWeight, float HealthFrac);

	/**
	 * A sector shooter leaves his post along the wedge (review finding 2). All three must hold:
	 * - he has been on the post for at least shift_after_s (never at SecondsOnPost 0);
	 * - he is under fire, or has had no target for shift_no_target_s;
	 * - shift_cooldown_s has passed since his last shift, so two spots do not ping-pong.
	 * shift_after_s <= 0 disables the shift.
	 */
	CODEXTACTICS_API bool ShouldSectorShift(float SecondsOnPost, bool bUnderFire, float SecondsNoTarget, float SecondsSinceShift,
		const FPmcDefenceConfig& Config);

	/**
	 * G008 lead review 2 (B): the goal of a FallBack or TakeCover (or plain Relocate) walk for a defender. While he has a
	 * defence post, the goal must lie on the post's level (|GoalZ - PostZ| <= ToleranceCm). Otherwise a roof guard shot
	 * mid-walk falls back down the stair and Ambush walks him up again. Without a post, any goal is allowed.
	 * ToleranceCm <= 0 is planar (always true), the same as PmcCommanderRules::IsSameLevel.
	 */
	CODEXTACTICS_API bool IsGoalOnPostLevel(bool bHasDefencePost, float GoalZ, float PostZ, float ToleranceCm);

	/**
	 * The cover-object key. A named wall actor is its own key (two slots of one actor match). An empty actor does
	 * not match another empty actor: the key is the point quantized to 100 cm, so only the same object matches.
	 */
	CODEXTACTICS_API FString CoverObjectKey(const FString& WallActorId, const FVector& Point);

	/**
	 * Multiplier for a cover slot. The same cover object as a claimed slot scores 0. A slot within MinSpacingCm
	 * of any claimed slot scores SpacingPenalty once. Otherwise 1.
	 */
	CODEXTACTICS_API float SpacingFactor(const FVector& Slot, const FString& SlotKey, TConstArrayView<FVector> Claimed,
		TConstArrayView<FString> ClaimedKeys, const FPmcDefenceConfig& Config);

	/** Pairs of defenders on one non-empty cover key. Empty keys are not a shared object. */
	CODEXTACTICS_API int32 SharedCoverPairs(TConstArrayView<FString> CoverKeys);

	/** Mean pairwise planar distance of the feet, metres. Fewer than two men is 0 (no pair). */
	CODEXTACTICS_API float MeanSpreadM(TConstArrayView<FVector> Feet);

	/**
	 * One 1 Hz spread sample. False when fewer than two defenders are alive: there is no pair, so that second is
	 * not a bunch of 0 m (G010: a last man standing was pulling defender_spread_m under 5 m). Out is left untouched.
	 */
	CODEXTACTICS_API bool TrySpreadSample(TConstArrayView<FVector> Feet, float& OutSpreadM);

	/** One sample of the guard share. Not applicable (n/a, not a zero) when Access is unset. */
	CODEXTACTICS_API FPmcGuardSample SampleGuard(TConstArrayView<FVector> LivingFeet, const FPmcDefenceAccess& Access,
		float GuardDistCm, float LevelToleranceCm);

	/**
	 * Near samples / applicable samples. False when ApplicableSamples is 0 (no access all round): Out is left
	 * untouched, so a missing metric cannot be read back as 0.
	 */
	CODEXTACTICS_API bool TryGuardShare(int32 NearSamples, int32 ApplicableSamples, float& OutShare);

	/** Objective orders / all orders. 0 when there are no orders. */
	CODEXTACTICS_API float ObjectiveOrderShare(int32 ObjectiveOrders, int32 AllOrders);

	/**
	 * One line for the round log. "[PmcDefence] team N access <kind> at (x, y, z) source=tactical|layout"
	 * or "[PmcDefence] team N access none".
	 */
	CODEXTACTICS_API FString AccessLogLine(int32 Team, const FPmcDefenceAccess& Access);
}
