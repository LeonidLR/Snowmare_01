// G016: a fighter fires through an existing window opening from cover (both faces of the wall). Passage through the
// window stays in WindowOrderRules. UE-only.
#pragma once

#include "CoreMinimal.h"

class AActor;
struct FCollisionQueryParams;
class UPrimitiveComponent;
class UWorld;
struct FHitResult;

/**
 * Window cover (G016). The opening, the floor and the wall side come from the window rules already in the game
 * (WindowBreakage tolerances). A confirmed window is a local pose: crouch, and the over-the-top fire set when the
 * clips exist. It does not authorize a climb.
 */
namespace WindowCoverRules
{
	enum class EWindowCoverReject : uint8
	{
		None,
		OtherFloor,
		OtherWall,
		OutsideInterval,
		FirePointOutside,
		SolidWall,
		CornerFallback
	};

	enum class EWindowCoverTurn : uint8
	{
		Outward,
		Inward
	};

	enum class EWindowGlassState : uint8
	{
		Intact,
		Broken,
		Missing
	};

	enum class EWindowIntervalPlace : uint8
	{
		Outside,
		Inside
	};

	/** One sill's opening, bound to a floor, a wall and an interval along that wall. */
	struct FWindowCoverOpening
	{
		int32 Floor = INDEX_NONE;
		uint32 WallId = 0;
		FVector WallNormal = FVector::ForwardVector;
		float AlongMinCm = 0.f;
		float AlongMaxCm = 0.f;
		FBox Opening;
		bool bPresent = false;
		FString BuildingId;
	};

	/** The slot being tested against an opening. */
	struct FWindowCoverSlotFacts
	{
		int32 Floor = INDEX_NONE;
		uint32 WallId = 0;
		FVector WallNormal = FVector::ForwardVector;
		float AlongCm = 0.f;
		FVector FirePoint = FVector::ZeroVector;
		bool bAtCorner = false;
		FString BuildingId;
	};

	struct FWindowCoverMatch
	{
		bool bConfirmed = false;
		bool bInsideInterval = false;
		EWindowCoverReject Reject = EWindowCoverReject::SolidWall;
	};

	/** What a shot (or an aim / a shimmy) does to the glass and the target. */
	struct FWindowShotQuery
	{
		bool bConfirmed = false;
		/** False for aim and shimmy: the glass stays intact. */
		bool bIsShot = false;
		bool bThroughOpening = false;
		bool bHitsFrameOrSolid = false;
		EWindowGlassState Glass = EWindowGlassState::Missing;
		bool bBreakable = false;
		int32 GlassRoundsSpent = 0;
	};

	struct FWindowShotResult
	{
		bool bBlocked = false;
		bool bSpendRound = false;
		bool bBreakGlass = false;
		bool bHitTarget = false;
	};

	/** One fighter's own slot. The leader flag is not a window. */
	struct FWindowCoverFighter
	{
		int32 Id = 0;
		bool bOwnSlotConfirmed = false;
		bool bLeader = false;
	};

	/** The cached window of one operative. Not part of FCoverSlot (that struct is copied and saved). */
	struct FWindowCoverBinding
	{
		TWeakObjectPtr<AActor> Sill;
		int32 Floor = INDEX_NONE;
		uint32 WallId = 0;
		FVector SideNormal = FVector::ForwardVector;
		FVector AlongAxis = FVector::RightVector;
		FVector IntervalOrigin = FVector::ZeroVector;
		float AlongMinCm = 0.f;
		float AlongMaxCm = 0.f;
		FBox Opening;
		bool bConfirmed = false;
		bool bInsideInterval = false;
		bool bHasInterval = false;
		int32 GlassRounds = 0;
		/** Sampled when the sill is bound. A glazed pane ignores visibility, so the shot line cannot see it. */
		EWindowGlassState Glass = EWindowGlassState::Missing;
		EWindowCoverReject Reject = EWindowCoverReject::SolidWall;
	};

	/** A traced line after broken / hidden glass has been stepped past. */
	struct FWindowLineView
	{
		bool bBlocking = false;
		bool bIntactGlass = false;
		FVector ImpactPoint = FVector::ZeroVector;
		float Distance = 0.f;
		TWeakObjectPtr<AActor> Actor;
	};

	CODEXTACTICS_API bool SameWallSide(const FVector& SlotNormal, const FVector& OpeningNormal);

	/** Faces of one wall (parallel normals, either direction) and the same wall id when both are set. */
	CODEXTACTICS_API bool SameWall(uint32 SlotWallId, uint32 OpeningWallId, const FVector& SlotNormal, const FVector& OpeningNormal,
		const FString& SlotBuilding, const FString& OpeningBuilding);

	CODEXTACTICS_API FWindowCoverMatch Match(const FWindowCoverOpening& Opening, const FWindowCoverSlotFacts& Slot);

	CODEXTACTICS_API EWindowIntervalPlace PlaceOnInterval(float AlongCm, float MinCm, float MaxCm);

	CODEXTACTICS_API bool CrossedOpeningBoundary(EWindowIntervalPlace Was, EWindowIntervalPlace Now);

	/** Crouch only once he has stopped in the opening, off the corner. */
	CODEXTACTICS_API bool WantsCrouch(bool bConfirmed, bool bShimmying, bool bAtCorner, bool bStopped);

	/** Outward: the aim is on the slot normal's side. Inward: the aim goes through the wall. */
	CODEXTACTICS_API EWindowCoverTurn TurnForAim(const FVector& From, const FVector& Aim, const FVector& SlotNormal);

	/** Horizontal yaw from From toward Aim, degrees. */
	CODEXTACTICS_API float AimYaw(const FVector& From, const FVector& Aim);

	/**
	 * Where the through-wall ray crosses the wall plane. A muzzle standing SlotOffset off the wall (along the normal)
	 * crosses at the wall point.
	 */
	CODEXTACTICS_API FVector FirePointOnPlane(const FVector& Muzzle, const FVector& SlotNormal, const FVector& WallPoint);

	/** Half the wall's thickness along the normal, clamped to the window face margin, plus the opening pad. */
	CODEXTACTICS_API float PlaneLimitCm(const FVector& BoundsExtent, const FVector& SlotNormal);

	CODEXTACTICS_API bool OnSameWallPlane(const FVector& Point, const FVector& WallPoint, const FVector& SlotNormal, float LimitCm);

	/** A sill this far from the slot (2D) is another window. Uses WindowBreakage::ThroughLandingCm. */
	CODEXTACTICS_API bool IsNeighborSill(const FVector& SillCenter, const FVector& SlotWallPoint);

	/**
	 * Floors match by tag when both are known. A missing tag falls back to the landing distance in Z, which a storey
	 * above (one level height) fails.
	 */
	CODEXTACTICS_API bool SharesFloor(int32 SlotFloor, int32 SillFloor, float SlotZ, float SillZ);

	CODEXTACTICS_API FWindowShotResult DecideShot(const FWindowShotQuery& Query);

	/** The window pose belongs to the fighter whose own slot confirmed. A leader swap does not move it. */
	CODEXTACTICS_API bool TakesWindowPose(const FWindowCoverFighter& Fighter);

	/** A cover shot never opens a climb. WindowOrderRules keeps passage. */
	CODEXTACTICS_API bool GrantsPassage();

	CODEXTACTICS_API const TCHAR* RejectName(EWindowCoverReject Reject);

	/**
	 * Glass HideBrokenWindow would hide for this sill. Broken when the sill is tagged window_broken. Intact when a visible
	 * window_glass actor or a kit window instance still stands over it. Missing when neither does. The pane often ignores
	 * visibility (the grey builder's glazed pane), so this is not a trace.
	 */
	CODEXTACTICS_API EWindowGlassState GlassOver(const AActor& Sill);

	/**
	 * The shot line, skipping glass that is already broken (a hidden pane, or a kit instance shrunk by HideBrokenWindow).
	 * Intact glass of Binding.Sill is reported when the line crosses the opening: a colliding pane the trace meets, or a
	 * pane with no visibility collision once the line has cleared the far face. A solid hit on the near side is the frame.
	 */
	CODEXTACTICS_API FWindowLineView TraceThroughWindow(UWorld& World, const AActor* Shooter, const FWindowCoverBinding& Binding,
		const FVector& From, const FVector& To, const FCollisionQueryParams& Params);
}
