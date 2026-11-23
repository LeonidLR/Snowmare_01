#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "RoofCutawaySubsystem.generated.h"

class AActor;
class ACameraZoneVolume;
class AOperativeCharacter;
class UPrimitiveComponent;

/** A roof piece was cut away (true) or restored (false): the hook for a dithered fade (Gemini, TANDEM request 2026-10-15). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRoofCutawayChanged, UPrimitiveComponent* /*Piece*/, bool /*bCutAway*/);

/** Tuning of the roof cutaway (defaults; CVar Codex.RoofCutaway 0 switches it off). */
struct FRoofCutawayConfig
{
	/** Updates per second: one pass every this many seconds (real time), s. */
	float UpdateIntervalSeconds = 0.1f;
	/** The upward roof trace starts this far above the operative's head (capsule top), cm. */
	float TraceStartAboveHeadCm = 10.f;
	/** ... and ends this high above his feet, cm. */
	float TraceTopAboveFeetCm = 1500.f;
	/** A piece is cut only when its bottom is at least this far above his head (walls reaching the floor stay), cm. */
	float HeadClearanceCm = 40.f;
	/** A cut piece comes back this long after no operative is under it any more (hysteresis), s. */
	float RestoreDelaySeconds = 0.5f;
	/** Other actors' pieces count as the same roof when they overlap its footprint by more than this on both axes, cm. */
	float FootprintInsetCm = 10.f;
	/** A "roof" wider than this on either axis is no building (a level slab, a sky mesh) and is never cut, cm. */
	float MaxFootprintCm = 6000.f;

	// Storey rule (designer bug 2026-11-22, the dressed khrushchevka showcase; CVar Codex.RoofCutaway.Storeys): in a building whose pieces
	// carry the level generator's building=<id> / floor=<n> tags (actor or component tags: the grey pieces, the kit dressing's per-storey
	// instanced components) every storey above the operative's is cut, the roof included; his own storey and those below stay.

	/** Feet (or a piece's bottom) up to this far below a storey's base count on that storey (the top of a stair flight), cm. */
	float StoreyToleranceCm = 50.f;
	/** He stays in a building he was in while within this distance of one of its floor / roof slabs (a stair hole, the doorway), cm. */
	float StoreyKeepInsideCm = 200.f;
	/** An untagged piece (a stair ramp, a prop, a chimney) belongs to a building when its centre lies within the building's slabs' bounds
	 *  grown by this and it is no wider than those bounds grown by twice this (a ground plane or a sky mesh is not), cm. */
	float StoreyClaimMarginCm = 50.f;
	/** The storey index (tags read from the level) is rebuilt this often: spawned / destroyed pieces, s. */
	float StoreyIndexRefreshSeconds = 5.f;

	// Cursor through the cutaway (designer bug 2026-11-22 on the dressed showcase: "clicks land on the invisible upper storey"; CVar
	// Codex.Cursor.ThroughCutaway): the cursor ray skips what the storey rule hides for the selected operative.

	/** A cursor hit on a piece of the selected man's own storey this close under its ceiling (a wall's top under the cut storey) is skipped, cm. */
	float CursorCeilingSlackCm = 30.f;
	/** Pieces one cursor ray may skip before it gives up (keeps the last hit). */
	int32 CursorMaxSkips = 12;
};

/** Pure rules of the roof cutaway (tested in CodexTactics.Camera.RoofCutaway.*). */
namespace RoofCutawayRules
{
	/** Actor tag: never cut this actor. */
	CODEXTACTICS_API FName OptOutTag();
	/** Actor tag: always cut this actor (whole) when it overlaps a cut roof's footprint or hangs over an operative's head. */
	CODEXTACTICS_API FName OptInTag();

	/** What is known about a piece's owner and the piece itself. */
	struct FRoofPieceTraits
	{
		/** A character / vehicle (any pawn). */
		bool bPawn = false;
		/** Landscape / terrain. */
		bool bTerrain = false;
		/** Foliage instances. */
		bool bFoliage = false;
		/** A static mesh (the only kind of piece that is cut: graybox cubes, building meshes, instanced meshes). */
		bool bStaticMesh = false;
		/** Rendered at all (visible, not hidden in game). */
		bool bVisible = true;
		bool bOptOut = false;
		bool bOptIn = false;
	};

	/** The piece may be cut at all (a visible static mesh of no pawn / terrain / foliage, not opted out). */
	CODEXTACTICS_API bool IsCuttableKind(const FRoofPieceTraits& Traits);

	/** The piece's bottom is at least HeadClearanceCm above the head (a wall standing on the floor is not). */
	CODEXTACTICS_API bool IsAboveHead(float PieceBottomZ, float HeadZ, float HeadClearanceCm);

	/** A piece of the roof's own actor / of an overlapping actor is cut: opted in, or above the head. */
	CODEXTACTICS_API bool ShouldCutPiece(const FRoofPieceTraits& Traits, float PieceBottomZ, float HeadZ, float HeadClearanceCm);

	/** Piece overlaps Roof's footprint (XY) by more than InsetCm on both axes. */
	CODEXTACTICS_API bool OverlapsFootprint(const FBox& Roof, const FBox& Piece, float InsetCm);

	/** The roof is building-sized (its footprint no wider than MaxFootprintCm on either axis). */
	CODEXTACTICS_API bool IsBuildingSized(const FBox& Roof, float MaxFootprintCm);

	/**
	 * An operative's roof is cut unless he stands in a camera zone whose fixed interior camera is the view (it sees the
	 * interior on its own; in the M outside view the zone camera is not shown, so the roof is cut there too).
	 */
	CODEXTACTICS_API bool ShouldCutForOperative(bool bInsideShownZoneCamera);

	/**
	 * Whose roof is cut (game designer bug report 2026-11-02, item 8: "the cutaway must follow the operative I am WORKING WITH"): with
	 * bFollowSelected only the selected operative's - the squad leader (the selected man; the leader of a box-selected group; the active
	 * unit in turn-based combat) - so a selected man on a roof keeps it drawn while a team-mate stands under it; without it every
	 * operative's (the old rule, Codex.RoofCutaway.FollowSelected 0).
	 */
	CODEXTACTICS_API bool ShouldEvaluateOperative(bool bIsSelected, bool bFollowSelected);

	/** A cut piece no operative is under any more is restored once RestoreDelaySeconds have passed since he last was. */
	CODEXTACTICS_API bool ShouldRestore(double LastCoveredSeconds, double NowSeconds, float RestoreDelaySeconds);

	/**
	 * Shadow of a cut piece (designer 2026-11-22: "interiors must not stay dark"): it keeps casting its shadow (and its distance-field
	 * shadow / AO) only with bHiddenShadow (Codex.RoofCutaway.HiddenShadow 1, the old rule) and if it cast one at all; otherwise none.
	 * Restored, a piece gets its own setting back (bOriginal).
	 */
	CODEXTACTICS_API bool CastsShadowWhileCut(bool bOriginal, bool bHiddenShadow);

	// --- Storey rule (designer bug 2026-11-22: "the squad on the ground floor -> storeys 2, 3, 4 and the roof invisible; on storey 2 ->
	// 3 and 4 invisible, 1 and 2 visible") ---

	/** A storey of a tagged building: its index (tag floor=<n>; the roof's storey = the building's storey count) and base height, cm. */
	struct FStoreyBase
	{
		int32 Storey = 0;
		float BaseZ = 0.f;
	};

	/**
	 * The building and storey a piece's tags name: "building=<id>" and "floor=<n>" (the level generator's tags, kit_dress's component tags).
	 * False without a building tag; OutStorey is INDEX_NONE without a (numeric) floor tag.
	 */
	CODEXTACTICS_API bool ParseStoreyTags(TConstArrayView<FName> Tags, FString& OutBuilding, int32& OutStorey);

	/** A building's floor / roof slab (tag floor_slab / roof_slab): what "inside the building" is measured against. */
	CODEXTACTICS_API bool IsSlabTag(const FName& Tag);

	/** Storey bases from the bottoms of the pieces tagged per storey: the median bottom of each storey (walls dominate), sorted by storey. */
	CODEXTACTICS_API TArray<FStoreyBase> StoreyBasesFromBottoms(const TMap<int32, TArray<float>>& BottomsByStorey);

	/** The storey at height Z: the highest storey whose base is at most ToleranceCm above Z; INDEX_NONE below the lowest one. */
	CODEXTACTICS_API int32 StoreyAt(TConstArrayView<FStoreyBase> Bases, float Z, float ToleranceCm);

	/** A piece of the operative's building is cut when its storey lies above his (upper storeys, their partitions, the roof). */
	CODEXTACTICS_API bool ShouldCutStorey(int32 PieceStorey, int32 OperativeStorey);

	/** XY distance from P to Box (0 inside it), cm. */
	CODEXTACTICS_API float DistanceToBox2D(const FBox& Box, const FVector2D& P);

	/**
	 * He is in the building: over or under one of its slabs (XY), or within KeepCm of one while he was in it the last pass (a stair hole,
	 * the doorway: no flicker on the stairs). A man in the street under a balcony is not (balconies are no slabs).
	 */
	CODEXTACTICS_API bool IsInsideBuilding(float DistanceToNearestSlabCm, bool bWasInside, float KeepCm);

	/** An untagged piece belongs to the building whose slabs span SlabBounds: its centre within MarginCm of them, no wider than them + 2 x MarginCm. */
	CODEXTACTICS_API bool ClaimsUntaggedPiece(const FBox& SlabBounds, const FBox& Piece, float MarginCm);

	// --- Cursor through the cutaway (designer bug 2026-11-22: "clicks hit the hidden storeys", "a click on the flat's floor hugs the
	// exterior wall") ---

	/**
	 * The cursor ray passes through this hit (traced on): the piece is cut away now; or it is a piece of the selected man's building
	 * (PieceStorey != INDEX_NONE, his storey SelectedStorey != INDEX_NONE) on a storey above his - hidden, a kit-dressed map's grey proxies
	 * included (they carry the collision the kit lacks); or a piece of his storey hit within CeilingSlackCm under its ceiling (SelectedCeilingZ:
	 * the base of the storey above) - the top of a wall under the cut storeys - unless it is a stair (bStairPiece: the flight up is visible).
	 */
	CODEXTACTICS_API bool ShouldSkipCursorHit(bool bCutAway, int32 PieceStorey, int32 SelectedStorey, float HitZ, float SelectedCeilingZ, bool bStairPiece,
		float CeilingSlackCm);

	/**
	 * A hit is the floor INSIDE a building (it beats the cover hug on a wall: designer 2026-11-22): a floor-like surface (ImpactNormalZ >= 0.7)
	 * over one of the building's slabs (DistanceToSlabsCm <= 0), from its lowest storey's base (- ToleranceCm) up to under its roof top
	 * (RoofTopZ - ToleranceCm: the roof itself is outside).
	 */
	CODEXTACTICS_API bool IsInteriorFloorHit(float ImpactNormalZ, float DistanceToSlabsCm, float HitZ, float BottomZ, float RoofTopZ, float ToleranceCm);
}

/**
 * Roofs disappear while operatives are inside (Demo 01 rev 5h, game designer 2026-10-15; every level, UE-only). A building
 * without a fixed interior camera hid the squad under its roof / ceiling (walls already show them through the silhouette).
 * ~10 times a second, for the SELECTED operative (the squad leader: the selected man, a group's leader, the turn-based active unit -
 * bug report 2026-11-02 item 8; Codex.RoofCutaway.FollowSelected 0 = every operative, the old rule): a trace straight up from his head
 * finds the roof (the first static mesh
 * above him); every piece of that actor whose bottom is above his head + 40 cm is cut away, and for graybox buildings made
 * of separate actors also every piece of other actors overlapping the roof's footprint above his head (ceilings, upper
 * floors). Cut away = not drawn in the main / depth pass (SetRenderInMainPass / SetRenderInDepthPass false) but still
 * casting its shadow only with Codex.RoofCutaway.HiddenShadow 1 (default 0 since the designer's decision 2026-11-22: the interior is
 * lit, no shadow of the cut storeys / roof; set when a piece is cut, its own setting back on restore) and ignored by visibility traces (clicks reach the floor inside, no
 * silhouette through it); restored 0.5 s after no operative is under it. Skipped for an operative inside a camera zone whose
 * fixed camera is the view (the M outside view cuts as usual). Never cut: pawns (characters, vehicles), landscape, foliage,
 * anything tagged NoRoofCutaway; a piece tagged RoofCutaway is always cut with its roof. Instant hide / show now; the fade is
 * Gemini's (OnCutawayChanged is the hook for a dithered fade material function, TANDEM request 2026-10-15).
 * Storey rule (designer bug 2026-11-22; Codex.RoofCutaway.Storeys 0 = the head-clearance rule alone): the level's static meshes tagged
 * building=<id> / floor=<n> (actor tags of the generator's grey pieces, component tags of kit_dress's per-storey instanced components;
 * untagged pieces over a building's slabs by their bottom) form a storey index; the selected operative inside such a building (over / under
 * one of its floor_slab / roof_slab pieces) is on the storey his feet stand on (the floor he left while on a ladder), and every visible piece
 * of that building on a higher storey is cut; pieces of his storey and below are never cut (the head-clearance rule spares them too).
 */
UCLASS()
class CODEXTACTICS_API URoofCutawaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** Piece is cut away now. */
	bool IsCutAway(const UPrimitiveComponent* Piece) const;

	/**
	 * A trace that passes the pieces cut away now (designer bug 2026-11-23: cut pieces keep blocking Visibility for sight / fire; only the view-
	 * side traces - the see-through silhouette, drops onto the floor - look past them). False = nothing behind.
	 */
	bool LineTraceSkippingCut(FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel Channel, FCollisionQueryParams Params) const;

	/** Number of pieces cut away now. */
	int32 GetCutAwayCount() const;

	/** One pass now (smokes; the tick runs it every UpdateIntervalSeconds). */
	void UpdateNow();

	/** Restores every cut piece at once (switching the system off, the world going away). */
	void RestoreAll();

	/** The building and storey the storey rule puts Operative in now (false: in none; the index as of the last pass). */
	bool GetOperativeStorey(const AOperativeCharacter& Operative, FString& OutBuilding, int32& OutStorey) const;

	/** The buildings of the storey index: id and storey count (highest storey + 1, the roof included). */
	TArray<TPair<FString, int32>> GetStoreyBuildings() const;

	/** Building's indexed pieces and their storeys (smokes). */
	TArray<TPair<UPrimitiveComponent*, int32>> GetStoreyPieces(const FString& Building) const;

	/** Building's floor / roof slab boxes (smokes: a spot inside it). */
	TArray<FBox> GetStoreySlabs(const FString& Building) const;

	/** Rebuilds the storey index now (smokes; a pass rebuilds it every StoreyIndexRefreshSeconds). */
	void RebuildStoreyIndex();

	/**
	 * The cursor hit through the cutaway (designer bug 2026-11-22; Codex.Cursor.ThroughCutaway 0 = the hit as it is): while InOutHit is a piece
	 * the ray must pass (RoofCutawayRules::ShouldSkipCursorHit for Selected, the selected operative: cut pieces, the hidden storeys above his
	 * in his building, the wall tops under them), the ray (InOutHit.TraceStart -> TraceEnd) is traced again past it on Channel. Returns
	 * whether a blocking hit is left. Every cursor trace of the player controller goes through it (move, cover, door, hover).
	 */
	bool RefineCursorHit(FHitResult& InOutHit, ECollisionChannel Channel, const AOperativeCharacter* Selected) const;

	/** A cursor ray Start -> End on Channel, refined as RefineCursorHit (smokes: a click from any camera without a viewport). */
	bool TraceCursorRay(const FVector& Start, const FVector& End, ECollisionChannel Channel, const AOperativeCharacter* Selected, FHitResult& OutHit) const;

	/** Hit is the floor inside a building of the storey index (RoofCutawayRules::IsInteriorFloorHit); OutBuilding / OutStorey when it is. */
	bool IsInteriorFloor(const FHitResult& Hit, FString* OutBuilding = nullptr, int32* OutStorey = nullptr) const;

	/**
	 * Designer 2026-11-22: inside, the storeys above are cut - how to send the squad up a floor he cannot see. When Hit is a stair piece
	 * (tag stair=<id>) of the building Selected stands in, the navmesh spot of the NEXT storey up (or the walkable roof) over the hit -
	 * the landing the flight leads to - is OutFeet (the controller shows a silhouette there; a click walks the squad up).
	 */
	bool FindStairUpTarget(const FHitResult& Hit, const AOperativeCharacter& Selected, FVector& OutFeet, FString* OutWhy = nullptr) const;

	/** The base height of Building's Storey (false when unknown). */
	bool GetStoreyBaseZ(const FString& Building, int32 Storey, float& OutZ) const;

	/** Cut / restore notifications (the dithered fade hook). */
	FOnRoofCutawayChanged OnCutawayChanged;

	FRoofCutawayConfig Config;

private:
	struct FCutPiece
	{
		TWeakObjectPtr<UPrimitiveComponent> Piece;
		/** Its own settings, put back on restore. */
		ECollisionResponse VisibilityResponse = ECR_Block;
		/** Its Visibility response was switched off by the cut (Codex.RoofCutaway.CutIgnoresVisibility 1). */
		bool bVisibilityChanged = false;
		bool bRenderInMainPass = true;
		bool bRenderInDepthPass = true;
		bool bCastShadow = true;
		bool bAffectDistanceFieldLighting = true;
		double LastCoveredSeconds = 0.0;
	};

	/** The pieces that must be cut now for Operative (none when his zone camera is the view): the head-clearance rule + the storey rule. */
	void CollectRoofPieces(const AOperativeCharacter& Operative, const TArray<ACameraZoneVolume*>& ShownZones, TSet<UPrimitiveComponent*>& OutPieces);
	/** The head-clearance rule: the roof over his head and the pieces over its footprint. */
	void CollectHeadClearancePieces(const AOperativeCharacter& Operative, const FVector& Location, TSet<UPrimitiveComponent*>& OutPieces) const;

	/** One building of the storey index. */
	struct FStoreyBuilding
	{
		FString Id;
		TArray<RoofCutawayRules::FStoreyBase> Bases;
		/** Its floor_slab / roof_slab pieces' boxes and their bounds. */
		TArray<FBox> Slabs;
		FBox SlabBounds = FBox(ForceInit);
		TArray<TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>> Pieces;
	};
	/** Index into StoreyBuildings of the building whose slabs he is over / under (INDEX_NONE: none) and his storey there. */
	int32 FindOperativeStorey(const FVector& FloorLocation, float FeetZ, const FString* WasInside, int32& OutStorey) const;
	/** The ceiling of a building's storey: the base of the storey above (the roof top over the top one), cm. */
	float GetStoreyCeilingZ(int32 BuildingIndex, int32 Storey) const;
	/** Feet height of Operative at his floor reference (the floor he left while on a ladder). */
	static float GetFloorFeetZ(const AOperativeCharacter& Operative, FVector& OutLocation);
	/** Traits of Piece (its owner's class / tags). */
	static RoofCutawayRules::FRoofPieceTraits GetTraits(const UPrimitiveComponent& Piece);
	void CutPiece(UPrimitiveComponent& Piece, double Now);
	void RestorePiece(FCutPiece& Cut);
	/** Actors tagged RoofCutaway (refreshed once a second). */
	void RefreshOptInActors(double Now);

	TArray<FCutPiece> CutPieces;
	TArray<FStoreyBuilding> StoreyBuildings;
	/** A piece's building (index into StoreyBuildings) and storey. */
	TMap<TObjectKey<UPrimitiveComponent>, TPair<int32, int32>> StoreyOfPiece;
	/** The building each evaluated operative is in this pass (the keep-inside hysteresis). */
	TMap<TWeakObjectPtr<const AOperativeCharacter>, FString> LastStoreyBuilding;
	/** ... and the pass before (read while this pass fills LastStoreyBuilding). */
	TMap<TWeakObjectPtr<const AOperativeCharacter>, FString> PreviousStoreyBuilding;
	double StoreyIndexSeconds = -1.0e9;
	TArray<TWeakObjectPtr<AActor>> OptInActors;
	double OptInRefreshSeconds = -1.0e9;
	float UpdateAccumulator = 0.f;
};
