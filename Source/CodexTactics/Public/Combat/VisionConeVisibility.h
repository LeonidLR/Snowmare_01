#pragma once

#include "CoreMinimal.h"
#include "Combat/VisionConeRules.h"

/**
 * Occluded vision cones (game designer 2026-10-19: «the cone goes through walls - cut out what the enemy cannot see»,
 * Commandos style visibility polygon). UE-only, no Godot reference (Godot's cone was a plain sector, player.gd).
 *
 * A fan of rays from the observer's eye inside the cone; each ray reports the obstacles it meets (front / back face
 * distance and top height above the observer's feet). The sight rule of the game - a straight line from the eye to the
 * target's profile point, blocked by any sight-blocking geometry (SightRules::LineHeightAt, UTacticalSightSubsystem /
 * AEnemyCharacter::FindVisibleOperative) - is then solved exactly along each ray for every drawn sight LEVEL (a target
 * profile height + the range it is seen at), so the drawn shape and the sight check agree:
 *  - level count at a ground point = how many levels see a target standing there;
 *  - enemies draw two levels: a standing operative (profile 150 cm, the standing sight range) and a crouched one (90 cm,
 *    the crouching range). Full tone = even a crouched operative is seen, light tone = only a standing one (behind a low
 *    obstacle, or beyond the crouching range), cut out = nobody is seen (behind a wall taller than the sight lines).
 * Discontinuities between neighbouring rays (a wall's corner, a new obstacle) are refined by bisection, so a shadow edge
 * is drawn between two rays a fraction of a degree apart (the «edge rays»). Pure rules, tested in
 * CodexTactics.Combat.VisionCone.Occlusion.*; UVisionConeComponent traces the rays in the world.
 *
 * Elevated observers (game designer bug 09, 2026-11-01: «from the tower he sees everything behind the wall»): the rule is 2.5D.
 * Every height is measured from the observer's feet; a ray carries the level of the ground its targets stand on (GroundCm, 0 =
 * his own level, negative = he looks down from a perch: a tower roof, a rooftop). A target's profile point is GroundCm + its
 * profile height, so from a 4 m tower a 3 m wall (top -100 cm) hides the ground behind it for WallShadowLengthCm, and the perch
 * itself is an obstacle from the eye to its edge (bHidesInside: the ground under the roof is out of sight) whose edge hides the
 * ground near its foot (PerchDeadZoneCm). Tested in CodexTactics.Combat.VisionCone.Elevated.*.
 */

/** One sight level drawn by a cone: a target whose profile point is ProfileHeightCm above the ground, seen up to RangeCm. */
struct CODEXTACTICS_API FVisionConeLevel
{
	float ProfileHeightCm = 150.f;
	float RangeCm = 0.f;
};

/** What the cone shows and how finely it is sampled. */
struct CODEXTACTICS_API FVisionConeSightSpec
{
	/** Observer eye height above its feet, cm. */
	float EyeHeightCm = 160.f;
	/** Outer to inner: [0] the widest (standing target, full range), then lower profiles / shorter ranges. 1..4 levels. */
	TArray<FVisionConeLevel, TInlineAllocator<2>> Levels;
	/** Field of view half angle, degrees (180 = all round). */
	float HalfAngleDeg = 45.f;
	/** Angle between the base rays, degrees. */
	float RayStepDeg = 1.5f;
	/** Bisection steps at a discontinuity (each halves the gap between two rays). */
	int32 MaxRefineDepth = 6;
	/** Rays never below this angular gap when refining, degrees. */
	float MinRefineGapDeg = 0.02f;
	/** Hard cap on rays per cone (base + refined). */
	int32 MaxRays = 320;
	/** Neighbouring rays whose span ends differ by more than max(JumpToleranceCm, JumpToleranceFraction x distance) are refined. */
	float JumpToleranceCm = 25.f;
	float JumpToleranceFraction = 0.06f;
	/** Neighbouring rays whose blockers' normals differ by more than this are refined (a corner of one mesh), degrees. */
	float NormalToleranceDeg = 20.f;

	/** Widest range of the levels, cm. */
	float GetMaxRange() const;
	/**
	 * Height of the «full wall» trace: anything at least this tall hides every level behind it (max of the eye / profiles; the profiles
	 * stand on GroundCm, the target ground relative to the observer's feet).
	 */
	float GetHighTraceHeight(float GroundCm = 0.f) const;
	/** Height of the «low obstacle» trace: anything lower changes no level (min of the eye / profiles standing on GroundCm). */
	float GetLowTraceHeight(float GroundCm = 0.f) const;
};

/** An obstacle met along a ray: front / back face distances from the eye (planar) and its top above the observer's feet. */
struct CODEXTACTICS_API FVisionConeObstacle
{
	float NearCm = 0.f;
	float FarCm = 0.f;
	float TopCm = 0.f;
	/** Normals of the front / back face in the cone frame (zero = unknown): a change between rays = a corner, refined. */
	FVector2D NearNormal = FVector2D::ZeroVector;
	FVector2D FarNormal = FVector2D::ZeroVector;
	/**
	 * The observer's perch (the roof he stands on, NearCm 0 .. its edge): a target inside its footprint stands on the ground under
	 * the roof - hidden. Other obstacles keep the old rule (a target inside a low wall is judged by its faces only).
	 */
	bool bHidesInside = false;
};

/** A stretch of a ray seen at Level (= the number of sight levels that see a target there, >= 1). */
struct CODEXTACTICS_API FVisionConeSpan
{
	float StartCm = 0.f;
	float EndCm = 0.f;
	uint8 Level = 0;
};

/** One ray of the fan (cone frame: angle from +X, positive turns to +Y). */
struct CODEXTACTICS_API FVisionConeRay
{
	float AngleDeg = 0.f;
	/**
	 * Ground the targets of this ray stand on, relative to the observer's feet, cm (sampler): 0 = his own level (the old flat
	 * model), negative = he looks down from a perch. The drawn spans lie on it.
	 */
	float GroundCm = 0.f;
	/** Filled by the sampler (any order). */
	TArray<FVisionConeObstacle, TInlineAllocator<4>> Obstacles;
	/** The first full-height blocker (0 = none) and its surface normal in the cone frame (refinement hints, sampler). */
	uint64 BlockerKey = 0;
	FVector2D BlockerNormal = FVector2D::ZeroVector;
	/** Filled by ClassifyRay: the seen stretches, in order, hidden gaps left out. */
	TArray<FVisionConeSpan, TInlineAllocator<4>> Spans;
};

/** The meshes of an occluded cone (cone frame, +Z up). */
struct CODEXTACTICS_API FVisionConeShape
{
	/** Seen at every level (full tone). */
	FVisionConeMesh Full;
	/** Seen only at the outer level(s) (light tone: «only a standing operative is seen here»). */
	FVisionConeMesh Light;
	/** Band along the outer boundary of everything seen. */
	FVisionConeMesh Outline;
};

namespace VisionConeVisibility
{
	/** Top height of a wall that hides everything behind it (the «full wall» trace hit). */
	inline constexpr float FullHeightCm = 1.0e6f;
	/** Stretches shorter than this are dropped (noise), cm. */
	inline constexpr float MinSpanCm = 1.f;
	/**
	 * An observer counts as elevated along a ray when the ground just past his perch's edge lies at least this far below his
	 * feet, cm: a crate / step / kerb stays the flat model (ground-level cones unchanged), a tower or a roof does not.
	 */
	inline constexpr float ElevatedMinDropCm = 100.f;
	/** Rays whose ground levels differ by more than this are refined (the edge of a perch), cm. */
	inline constexpr float GroundToleranceCm = 5.f;

	/**
	 * The target ground of a ray from a perch: GroundZ - FeetZ when the ground lies at least MinDropCm below the feet, else 0 (the
	 * observer stands on that ground - the flat model).
	 */
	CODEXTACTICS_API float ElevatedGroundCm(float FeetZ, float GroundZ, float MinDropCm = ElevatedMinDropCm);

	/**
	 * Length of the shadow a wall casts for an eye EyeAboveGroundCm high (heights above the target ground): a target whose profile
	 * point is ProfileCm high, standing beyond a wall WallTopCm high whose (far) face is WallDistanceCm away (planar), is hidden for
	 * WallDistanceCm x (WallTopCm - ProfileCm) / (EyeAboveGroundCm - WallTopCm) behind that face. The ground itself (ProfileCm 0):
	 * h_w x d / (h_e - h_w). 0 when the profile is at least as high as the wall; FLT_MAX when the eye is not above the wall
	 * (everything behind it is hidden).
	 */
	CODEXTACTICS_API float WallShadowLengthCm(float EyeAboveGroundCm, float WallTopCm, float WallDistanceCm, float ProfileCm = 0.f);

	/**
	 * The dead zone round a perch's foot: an eye EyeAboveRoofCm over a roof RoofAboveGroundCm high, the roof's edge EdgeCm away
	 * (planar): a target whose profile point is ProfileCm over the ground is hidden by the edge up to this distance from the eye,
	 * EdgeCm x (EyeAboveRoofCm + RoofAboveGroundCm - ProfileCm) / EyeAboveRoofCm (EdgeCm when the profile reaches the roof; FLT_MAX
	 * for an eye on the roof surface).
	 */
	CODEXTACTICS_API float PerchDeadZoneCm(float EyeAboveRoofCm, float RoofAboveGroundCm, float EdgeCm, float ProfileCm = 0.f);

	/**
	 * The sight line from an eye EyeCm high to a target profile ProfileCm high, TargetCm away (planar), passes a thin
	 * obstacle FaceCm away with its top TopCm high: blocked unless the line passes above the top there
	 * (SightRules::ClearsCover, the game's rule). Targets nearer than the face are never blocked by it.
	 */
	CODEXTACTICS_API bool IsBlockedByFace(float EyeCm, float ProfileCm, float FaceCm, float TopCm, float TargetCm);

	/** A target TargetCm away along the ray is seen at level Level (within its range, no obstacle face blocks the line). */
	CODEXTACTICS_API bool IsSeenAt(const FVisionConeSightSpec& Spec, const FVisionConeRay& Ray, int32 Level, float TargetCm);

	/**
	 * The target distances where the face hides a target (the analytic boundary of IsBlockedByFace): adds FaceCm and the
	 * crossing distance (if any) to OutBreaks.
	 */
	CODEXTACTICS_API void AddFaceBreaks(float EyeCm, float ProfileCm, float FaceCm, float TopCm, TArray<float, TInlineAllocator<16>>& OutBreaks);

	/** Sorts Ray.Obstacles by distance and fills Ray.Spans (exact along the ray: breakpoints from AddFaceBreaks, level by IsSeenAt). */
	CODEXTACTICS_API void ClassifyRay(const FVisionConeSightSpec& Spec, FVisionConeRay& Ray);

	/** Level shown at TargetCm along a classified ray (0 = hidden / out of range). */
	CODEXTACTICS_API uint8 LevelAlong(const FVisionConeRay& Ray, float TargetCm);

	/** Base ray angles: -Half .. +Half every StepDeg (both ends included, at least 2; all round: -180 .. +180). */
	CODEXTACTICS_API TArray<float> BaseAngles(float HalfAngleDeg, float StepDeg, int32 MaxRays);

	/** Same span count and levels (their quads can be joined). */
	CODEXTACTICS_API bool SameStructure(const FVisionConeRay& A, const FVisionConeRay& B);

	/**
	 * A discontinuity lies between A and B: another span structure, blocker or obstacle count, a corner (a face normal turns),
	 * another obstacle top, or a jump of a span end / obstacle face.
	 */
	CODEXTACTICS_API bool NeedsRefine(const FVisionConeSightSpec& Spec, const FVisionConeRay& A, const FVisionConeRay& B);

	/**
	 * The whole fan: Sample(AngleDeg, Ray) fills Ray's obstacles and hints (Ray.AngleDeg is set before); every ray is
	 * classified; a discontinuity between neighbours is bisected (MaxRefineDepth, MinRefineGapDeg, MaxRays). Rays sorted by
	 * angle. OutSamples (optional) = Sample calls.
	 */
	CODEXTACTICS_API TArray<FVisionConeRay> CastRays(const FVisionConeSightSpec& Spec, TFunctionRef<void(float, FVisionConeRay&)> Sample,
		int32* OutSamples = nullptr);

	/**
	 * Fill + outline meshes of a fan. Neighbours of the same structure are joined span by span (quads between their hit
	 * points, the polygon follows the hits); otherwise each keeps its own spans up to the middle angle. Spans at level
	 * LevelCount go to Full, lower ones to Light. Both windings, at HeightCm (outline at OutlineHeightCm) plus each
	 * ray's GroundCm (an elevated observer's cone lies on the ground below him).
	 */
	CODEXTACTICS_API FVisionConeShape BuildShape(const TArray<FVisionConeRay>& Rays, int32 LevelCount, float HalfAngleDeg,
		float OutlineWidthCm, float HeightCm = 0.f, float OutlineHeightCm = 0.5f);

	/** The level the shape shows at a planar point of the cone frame (exactly the mesh's quads / split), 0 = not drawn. */
	CODEXTACTICS_API uint8 LevelAtPoint(const TArray<FVisionConeRay>& Rays, float HalfAngleDeg, const FVector2D& Point);

	/**
	 * An operative's cone (game designer 2026-10-20: «the obstacle isn't cut out of the operatives' vision cones»): the squad's sight rule
	 * (UTacticalSightSubsystem::CanOperativeSee - a clear line from HIS eye, EyeHeightCm for his stance or the peek eye, to the enemy's
	 * profile point) drawn at two enemy profiles like the enemies' cones: level [0] a standing enemy (SightRules::ProfileHeight Standing,
	 * 150 cm), level [1] a crouched one (Crouching, 90 cm), both to RangeCm. Full tone = he would see even a crouched enemy there, light
	 * tone = only a standing one, cut = nobody. LevelCount 1 = the old single 150 cm level (Codex.VisionCones.SquadLevels 1, A/B).
	 */
	CODEXTACTICS_API FVisionConeSightSpec MakeOperativeSightSpec(float EyeHeightCm, float RangeCm, int32 LevelCount = 2);

	/** Distance along the direction AngleDeg to the chord between (AngleA, DistanceA) and (AngleB, DistanceB), cm. */
	CODEXTACTICS_API float ChordDistance(float AngleA, float DistanceA, float AngleB, float DistanceB, float AngleDeg);
}
