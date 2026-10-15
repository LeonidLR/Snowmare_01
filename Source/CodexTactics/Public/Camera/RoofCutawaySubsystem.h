#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
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

	/** A cut piece no operative is under any more is restored once RestoreDelaySeconds have passed since he last was. */
	CODEXTACTICS_API bool ShouldRestore(double LastCoveredSeconds, double NowSeconds, float RestoreDelaySeconds);
}

/**
 * Roofs disappear while operatives are inside (Demo 01 rev 5h, game designer 2026-10-15; every level, UE-only). A building
 * without a fixed interior camera hid the squad under its roof / ceiling (walls already show them through the silhouette).
 * ~10 times a second, for every squad operative: a trace straight up from his head finds the roof (the first static mesh
 * above him); every piece of that actor whose bottom is above his head + 40 cm is cut away, and for graybox buildings made
 * of separate actors also every piece of other actors overlapping the roof's footprint above his head (ceilings, upper
 * floors). Cut away = not drawn in the main / depth pass (SetRenderInMainPass / SetRenderInDepthPass false) but still
 * casting its shadow (the interior does not turn sunlit) and ignored by visibility traces (clicks reach the floor inside, no
 * silhouette through it); restored 0.5 s after no operative is under it. Skipped for an operative inside a camera zone whose
 * fixed camera is the view (the M outside view cuts as usual). Never cut: pawns (characters, vehicles), landscape, foliage,
 * anything tagged NoRoofCutaway; a piece tagged RoofCutaway is always cut with its roof. Instant hide / show now; the fade is
 * Gemini's (OnCutawayChanged is the hook for a dithered fade material function, TANDEM request 2026-10-15).
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

	/** Number of pieces cut away now. */
	int32 GetCutAwayCount() const;

	/** One pass now (smokes; the tick runs it every UpdateIntervalSeconds). */
	void UpdateNow();

	/** Restores every cut piece at once (switching the system off, the world going away). */
	void RestoreAll();

	/** Cut / restore notifications (the dithered fade hook). */
	FOnRoofCutawayChanged OnCutawayChanged;

	FRoofCutawayConfig Config;

private:
	struct FCutPiece
	{
		TWeakObjectPtr<UPrimitiveComponent> Piece;
		/** Its own settings, put back on restore. */
		ECollisionResponse VisibilityResponse = ECR_Block;
		bool bRenderInMainPass = true;
		bool bRenderInDepthPass = true;
		double LastCoveredSeconds = 0.0;
	};

	/** The pieces over Operative's head that must be cut now (none when his zone camera is the view). */
	void CollectRoofPieces(const AOperativeCharacter& Operative, const TArray<ACameraZoneVolume*>& ShownZones, TSet<UPrimitiveComponent*>& OutPieces) const;
	/** Traits of Piece (its owner's class / tags). */
	static RoofCutawayRules::FRoofPieceTraits GetTraits(const UPrimitiveComponent& Piece);
	void CutPiece(UPrimitiveComponent& Piece, double Now);
	void RestorePiece(FCutPiece& Cut);
	/** Actors tagged RoofCutaway (refreshed once a second). */
	void RefreshOptInActors(double Now);

	TArray<FCutPiece> CutPieces;
	TArray<TWeakObjectPtr<AActor>> OptInActors;
	double OptInRefreshSeconds = -1.0e9;
	float UpdateAccumulator = 0.f;
};
