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
	/** Hides the glass of the window over Sill; returns how many pieces (pane actors + kit instances) were hidden. */
	CODEXTACTICS_API int32 HideBrokenWindow(AActor& Sill);

	/**
	 * The ground-floor window sill (tag "window_sill") a cursor Hit means: the hit actor itself, else the sill whose window volume holds the
	 * hit point (on a kit-dressed map the cursor lands on the kit window / wall mesh in front of the hidden grey sill - designer 2026-11-22:
	 * "no silhouette, they never climb"). Null when none. The sills of the world are cached (refreshed when the world changes).
	 */
	CODEXTACTICS_API AActor* FindWindowSillAt(UWorld& World, const FHitResult& Hit);

	/** Is Point inside the window's volume over SillBox (XY grown by MarginCm, Z from the sill's bottom up to HeightCm above its top)? */
	CODEXTACTICS_API bool IsOverSill(const FBox& SillBox, const FVector& Point, float MarginCm = 40.f, float HeightCm = 260.f);
}
