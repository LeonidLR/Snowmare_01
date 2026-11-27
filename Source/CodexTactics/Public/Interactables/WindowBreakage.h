// UE-only (no Godot reference): the visible state of a window broken by a climb through it (designer 2026-11-22).
#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
struct FHitResult;

/**
 * Broken windows (designer 2026-11-22: "the windows I climb through should turn invisible - not destroyed, only hidden - so I see which are
 * broken"). The first climb through a window sill (tag "window_sill", OperativeCharacterVault) hides that window's glass: the grey builder's
 * "window_glass" pane actors over the sill and, on a kit-dressed map, the kit window instances there (ISM components tagged
 * "kit_part=window", the instance shrunk to nothing - its index stays, nothing is destroyed). Collision is left as it was.
 */
namespace WindowBreakage
{
	/** FindWindowSillAt face test: the hit is on the window face (glass, frame, the wall round it), cm. */
	inline constexpr float FaceMarginCm = 30.f;
	/** FindWindowSillAt face test: height above the sill top that still counts as this window, cm. */
	inline constexpr float FaceHeightCm = 140.f; // 155: stop at the opening; 260 cm also captured the next floor / roof edge.
	/** The opening box is the sill's XY grown by this, cm (a ray through the empty pane). */
	inline constexpr float OpeningPadCm = 10.f;
	/** Opening bottom above the sill top, cm. */
	inline constexpr float OpeningBottomCm = 5.f;
	/** Opening top above the sill top, cm. Not added on top of OpeningBottomCm. */
	inline constexpr float OpeningTopCm = 140.f;
	/** A ray through the opening counts only when it lands this near the sill (2D), cm. */
	inline constexpr float ThroughLandingCm = 150.f;
	/** IsOverSill: the volume starts this far below the sill box, cm. */
	inline constexpr float SillBelowCm = 20.f;

	/** The pane volume FindWindowSillAt tests a through-ray against (the same box as before these names). */
	CODEXTACTICS_API FBox MakeOpeningBox(const FBox& SillBox);

	/** A ray crossing a window belongs to it only if its landing is near it on the same storey. */
	CODEXTACTICS_API bool IsThroughLanding(const FBox& SillBox, const FVector& Start, const FVector& Landing);

	/** Hides the glass of the window over Sill; returns how many pieces (pane actors + kit instances) were hidden. */
	CODEXTACTICS_API int32 HideBrokenWindow(AActor& Sill);

	/**
	 * The ground-floor window sill (tag "window_sill") a cursor Hit means: the hit actor itself, else the sill whose window volume holds the
	 * hit point (on a kit-dressed map the cursor lands on the kit window / wall mesh in front of the hidden grey sill - designer 2026-11-22:
	 * "no silhouette, they never climb"). Null when none. The sills of the world are cached (refreshed when the world changes).
	 */
	CODEXTACTICS_API AActor* FindWindowSillAt(UWorld& World, const FHitResult& Hit);

	/**
	 * Fable review 2026-11-23: a click / hover on this actor is never a click on a window / doorway near it - a pawn (an enemy by a window, his
	 * own man under one), an interactable (a crate) or an enemy-tagged actor keeps its own meaning (target, select, use).
	 */
	CODEXTACTICS_API bool IsOwnClickTarget(const AActor* HitActor);

	/** Is Point inside the window's volume over SillBox (XY grown by MarginCm, Z from the sill's bottom up to HeightCm above its top)? */
	CODEXTACTICS_API bool IsOverSill(const FBox& SillBox, const FVector& Point, float MarginCm = 40.f, float HeightCm = 260.f);
}
