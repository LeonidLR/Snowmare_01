#pragma once

#include "CoreMinimal.h"

/**
 * Pure minimap math (user decisions 2026-10-18, docs/design/MINIMAP.md; UE-only, no Godot reference): the isometric
 * orthographic projection the floor captures are taken with (world <-> map UV, the inverse along the view ray onto a
 * height plane), the panel / full-map view on screen (zoom, pan, clicks), floor bands (auto-detected from stacked walkable
 * surfaces or a level JSON list), which geometry each floor's capture hides, the fog of war grid (reveal discs, base64 for
 * the save) and the fading last-known enemy icon. Everything is testable without a world (CodexTactics.UI.Minimap.*).
 */

/** What the top-right panel (the message feed's place) shows. */
enum class EMinimapPanelMode : uint8
{
	Messages,
	Map
};

/**
 * Orthographic view of the level from the start camera's yaw / pitch (never rotates with the player's camera). Map UV
 * (0, 0) = the image's top-left corner, (1, 1) its bottom-right; U follows the view's right axis, V its down axis.
 */
struct CODEXTACTICS_API FMinimapIsoProjection
{
	/** The world point at the image centre. */
	FVector Center = FVector::ZeroVector;
	float YawDeg = 45.f;
	/** Negative = looking down (the tactical camera's -35.26). */
	float PitchDeg = -35.26f;
	/** Ortho width / height of the image, cm. */
	float WidthCm = 10000.f;
	float HeightCm = 10000.f;

	/** View direction (into the screen). */
	FVector GetForward() const;
	/** Image right axis. */
	FVector GetRight() const;
	/** Image up axis. */
	FVector GetUp() const;
	/** Width / height. */
	float GetAspect() const { return HeightCm > KINDA_SMALL_NUMBER ? WidthCm / HeightCm : 1.f; }

	FVector2D WorldToMap(const FVector& World) const;
	/** The point of the image plane (through Center, facing the view) at Map. */
	FVector MapToImagePlane(const FVector2D& Map) const;
	/** Where the view ray through Map crosses the horizontal plane Z = PlaneZ (the inverse of WorldToMap at that height). */
	FVector MapToWorldOnPlane(const FVector2D& Map, float PlaneZ) const;

	/** The smallest image (plus MarginCm on every side) that shows all of Bounds from that view. */
	static FMinimapIsoProjection Fit(const FBox& Bounds, float YawDeg, float PitchDeg, float MarginCm);
	/** Same view, height changed so Width / Height = SizeX / SizeY exactly (the render target's pixels stay square). */
	FMinimapIsoProjection WithPixelAspect(int32 SizeX, int32 SizeY) const;
	/** Render target size for the projection: the longer side MaxSide, the other one by the aspect (>= 64). */
	FIntPoint ChooseTextureSize(int32 MaxSide) const;
};

/** A window onto the map image drawn in a screen rectangle (the corner panel or the full-screen map). */
struct CODEXTACTICS_API FMinimapView
{
	FVector2D ScreenOrigin = FVector2D::ZeroVector;
	FVector2D ScreenSize = FVector2D(100.f, 100.f);
	/** The map UV at the rectangle's centre. */
	FVector2D CenterUV = FVector2D(0.5f, 0.5f);
	/** Visible map UV span (1 = the whole image width; smaller = zoomed in). */
	float SpanU = 1.f;
	float SpanV = 1.f;

	/** A view whose pixels are square in world cm: SpanV follows the rectangle's and the projection's aspects. */
	static FMinimapView Make(const FVector2D& Origin, const FVector2D& Size, const FVector2D& CenterUV, float SpanU, float ProjectionAspect);
	/** SpanU showing the whole image (both directions) in that rectangle. */
	static float FitSpanU(const FVector2D& Size, float ProjectionAspect);

	FVector2D MapToScreen(const FVector2D& UV) const;
	FVector2D ScreenToMap(const FVector2D& Screen) const;
	bool Contains(const FVector2D& Screen) const;
	/** Pixels per map U unit. */
	float PixelsPerU() const { return SpanU > KINDA_SMALL_NUMBER ? ScreenSize.X / SpanU : 0.f; }
};

namespace MinimapRules
{
	/** Wheel zoom: one notch scales the span by 1.25 (positive notches = zoom out), clamped. */
	CODEXTACTICS_API float StepZoomSpan(float SpanU, float Notches, float MinSpan, float MaxSpan);
	/** The view zoomed to NewSpanU keeping the map point under Cursor where it is. */
	CODEXTACTICS_API FMinimapView ZoomAround(const FMinimapView& View, const FVector2D& Cursor, float NewSpanU, float ProjectionAspect);
	/** Centre clamped so the window stays on the image (a window wider than the image is centred). */
	CODEXTACTICS_API FVector2D ClampCenter(const FVector2D& CenterUV, float SpanU, float SpanV);

	/** Where a camera ray hits the plane Z = PlaneZ; rays that never reach it (upwards) stop MaxDistanceCm out, flattened. */
	CODEXTACTICS_API FVector RayToPlane(const FVector& Origin, const FVector& Direction, float PlaneZ, float MaxDistanceCm);
	/**
	 * The ground footprint of a perspective camera (4 corners: top-left, top-right, bottom-right, bottom-left of the screen) on
	 * the plane Z = PlaneZ. HorizontalFovDeg over the screen width, Aspect = width / height.
	 */
	CODEXTACTICS_API TArray<FVector> FrustumFootprint(const FVector& Location, const FRotator& Rotation, float HorizontalFovDeg, float Aspect,
		float PlaneZ, float MaxDistanceCm);

	/**
	 * Sutherland-Hodgman: a convex polygon (screen points + their texture UVs) cut to an axis-aligned rectangle; UVs are
	 * interpolated along the cut edges. Empty when nothing is inside. (The canvas has no clip rectangle for triangles.)
	 */
	CODEXTACTICS_API void ClipPolygonToRect(const TArray<FVector2D>& Points, const TArray<FVector2D>& UVs, const FBox2D& Rect,
		TArray<FVector2D>& OutPoints, TArray<FVector2D>& OutUVs);
	/** Liang-Barsky: the part of segment A-B inside Rect; false when none. */
	CODEXTACTICS_API bool ClipSegmentToRect(FVector2D& A, FVector2D& B, const FBox2D& Rect);

	// --- Floors --------------------------------------------------------------------------------------------------------

	struct CODEXTACTICS_API FFloorParams
	{
		/** An upper floor's walking surface stands at least this far above the lowest surface under it, cm. */
		float StoreyMinCm = 220.f;
		/** Height histogram bin for the auto-detection, cm. */
		float BinCm = 50.f;
		/** Samples (grid columns) a height needs to count as a floor (ramps / stairs spread over many bins). */
		int32 MinSamples = 3;
		/** Two floors are at least this far apart, cm. */
		float MinSeparationCm = 220.f;
		int32 MaxUpperFloors = 3;
		/** Feet this far below an upper floor's surface already count as that floor (top of the stairs), cm. */
		float EnterBelowCm = 120.f;
		/** Band edge hysteresis against flicker on the stairs, cm. */
		float HysteresisCm = 30.f;
		/** Feet at least this far above the lowest surface below can be on an upper floor (else: the ground), cm. */
		float MinAboveLowestCm = 110.f;
		/** A floor's capture hides geometry whose bottom is this far above that floor's walking surface, cm. */
		float CutClearanceCm = 200.f;
	};

	/**
	 * Upper-floor walking heights (ascending, world Z cm) from the heights of walkable surfaces found ABOVE another walkable
	 * surface (stacked) on a sampling grid: histogram peaks with at least MinSamples, MinSeparationCm apart, the strongest
	 * first, each refined to the mean of its samples within one bin; at most MaxUpperFloors (the lowest kept).
	 */
	CODEXTACTICS_API TArray<float> DetectFloorBases(TConstArrayView<float> StackedSurfaceZ, const FFloorParams& Params);
	/** A level JSON list cleaned up: sorted, near duplicates (closer than MinSeparationCm) dropped, at most MaxUpperFloors. */
	CODEXTACTICS_API TArray<float> NormalizeFloorBases(TConstArrayView<float> Bases, const FFloorParams& Params);
	/**
	 * Floor of a character: 0 = the ground (also on hills: nothing walkable below him); k >= 1 = the highest upper floor k with
	 * FeetZ >= UpperBases[k-1] - EnterBelowCm, the band edge moved by HysteresisCm in favour of PreviousFloor.
	 */
	CODEXTACTICS_API int32 SelectFloor(float FeetZ, float AboveLowestCm, TConstArrayView<float> UpperBases, int32 PreviousFloor, const FFloorParams& Params);
	/**
	 * Is a piece of geometry left out of Floor's capture? Floor 0: its bottom stands CutClearanceCm above the ground under it
	 * (roofs, ceilings, upper storeys - terrain independent); floor k: its bottom is CutClearanceCm above that floor's surface.
	 */
	CODEXTACTICS_API bool HideOnFloor(int32 Floor, float BottomZ, float BottomAboveGroundCm, TConstArrayView<float> UpperBases, const FFloorParams& Params);
	/** Display name of a floor: "GROUND", "FLOOR 2", ... */
	CODEXTACTICS_API FString FloorLabel(int32 Floor);

	// --- Enemies -------------------------------------------------------------------------------------------------------

	/** Icon alpha: 1 while seen; afterwards the last known spot fades from LastKnownAlpha to 0 over FadeSeconds. */
	CODEXTACTICS_API float EnemyIconAlpha(bool bSeenNow, double SecondsSinceSeen, float FadeSeconds, float LastKnownAlpha = 0.65f);
}

/**
 * Fog of war: one revealed-bit per CellCm square over the level's XY bounds, one layer per floor (an operative reveals the
 * floor he walks on). Persistent per level (save: Encode / Decode).
 */
class CODEXTACTICS_API FMinimapFogGrid
{
public:
	/** Cells grow when the bounds would need more than MaxCells per side. */
	void Init(const FBox2D& Bounds, float CellCm, int32 NumFloors, int32 MaxCellsPerSide = 512);
	void Reset();
	bool IsValid() const { return Size.X > 0 && Size.Y > 0 && Layers.Num() > 0; }

	/** Reveals every cell whose centre lies within RadiusCm of Center on Floor; returns how many were new. */
	int32 RevealDisc(int32 Floor, const FVector2D& Center, float RadiusCm);
	bool IsRevealed(int32 Floor, const FVector2D& Point) const;
	bool IsCellRevealed(int32 Floor, int32 X, int32 Y) const;
	int32 GetRevealedCount(int32 Floor) const;

	FIntPoint GetSize() const { return Size; }
	float GetCellCm() const { return CellCm; }
	const FBox2D& GetBounds() const { return Bounds; }
	int32 GetNumFloors() const { return Layers.Num(); }
	/** Bumped on every change (texture uploads). */
	uint32 GetRevision() const { return Revision; }

	/** Base64 of the floor's bit set. */
	FString Encode(int32 Floor) const;
	/** False (nothing changed) when the data does not fit this grid. */
	bool Decode(int32 Floor, const FString& Data);

private:
	int32 CellIndex(int32 X, int32 Y) const { return Y * Size.X + X; }
	FBox2D Bounds = FBox2D(ForceInit);
	float CellCm = 200.f;
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<TArray<uint8>> Layers;
	TArray<int32> RevealedCounts;
	uint32 Revision = 0;
};
