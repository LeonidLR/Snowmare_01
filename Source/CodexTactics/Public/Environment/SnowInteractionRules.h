#pragma once

#include "CoreMinimal.h"
#include "Environment/SnowInteractionTypes.h"

enum class EEnemyArchetype : uint8;

/**
 * Pure logic of the interactive snow, phases 1 + 3 (UE-only; docs/systems/environment-weather.md §14). Unit-tested by
 * CodexTactics.Environment.SnowInteraction.*; USnowInteractionSubsystem does the rendering and the polling.
 * Coordinates: world cm. The RT window covers [Origin, Origin + Size) in world X / Y; UV = (World - Origin) / Size; RT texel
 * (u x N, v x N) = canvas pixel (x right = world +X, y down = world +Y).
 */
namespace SnowInteractionRules
{
	/** The print fills this share of its quad (the berm lies in the margin); the shader uses the same constant (M_SnowStamp). */
	constexpr float PrintFill = 0.72f;
	/** The RT holds up to this per channel (clamp pass). */
	constexpr float MaxChannelValue = 4.f;
	/** The window scrolls in whole steps of this many texels (no resampling blur, rare scrolls). */
	constexpr int32 SnapTexels = 256;

	/** Codex.Env.SnowTracksRes: 1024 / 2048 / 4096 as given; anything else = auto (EffectsQuality Low (0) -> 1024, else 2048). */
	CODEXTACTICS_API int32 ResolveResolution(int32 Requested, int32 EffectsQuality);

	/** Window size (cm) for a resolution: 1024 -> 48 m (Low), else 64 m. */
	CODEXTACTICS_API double GetWindowSizeCm(int32 Resolution);

	/** The scroll step (cm): SnapTexels texels (2048 / 64 m -> 800 cm, 1024 / 48 m -> 1200 cm). */
	CODEXTACTICS_API double GetSnapStepCm(int32 Resolution, double WindowSizeCm);

	/** The window centre: the view focus moved LeadCm along the view yaw (XY). */
	CODEXTACTICS_API FVector2D ComputeWindowCentre(const FVector& Focus, float ViewYawDeg, float LeadCm);

	/** Origin (min corner) of a window of SizeCm centred near Centre, snapped to StepCm (round to nearest). */
	CODEXTACTICS_API FVector2D SnapWindowOrigin(const FVector2D& Centre, double WindowSizeCm, double StepCm);

	/**
	 * The window origin for Centre with hysteresis: the current (valid) origin is kept while Centre stays within KeepSteps steps of the
	 * window's centre on both axes (rounding alone keeps it within 0.5 step: 0.75 leaves 0.25 step = 2 m of slack, so a focus resting on a
	 * step boundary never scrolls back and forth); else SnapWindowOrigin.
	 */
	CODEXTACTICS_API FVector2D UpdateWindowOrigin(const FVector2D& Current, bool bCurrentValid, const FVector2D& Centre, double WindowSizeCm,
		double StepCm, double KeepSteps = 0.75);

	/** Texels the content moves when the origin goes Old -> New (whole texels for snapped origins): new(i) = old(i + shift). */
	CODEXTACTICS_API FIntPoint ComputeShiftTexels(const FVector2D& OldOrigin, const FVector2D& NewOrigin, int32 Resolution, double WindowSizeCm);

	/** UV of a world point in the window (not clamped). */
	CODEXTACTICS_API FVector2D WorldToUV(const FVector2D& World, const FVector2D& Origin, double WindowSizeCm);
	CODEXTACTICS_API FVector2D UVToWorld(const FVector2D& UV, const FVector2D& Origin, double WindowSizeCm);

	/** MPC_Environment.SnowTrackWindow = (origin X, origin Y, 1 / size, enabled 0 / 1). */
	CODEXTACTICS_API FLinearColor MakeWindowParam(const FVector2D& Origin, double WindowSizeCm, bool bEnabled);

	/** World point at least MarginCm inside the window. */
	CODEXTACTICS_API bool IsInsideWindow(const FVector2D& World, const FVector2D& Origin, double WindowSizeCm, double MarginCm = 0.);

	/** Yaw (degrees) of a motion; Fallback when it is shorter than MinLengthCm. */
	CODEXTACTICS_API float YawFromDelta(const FVector2D& Delta, float FallbackYaw, float MinLengthCm = 0.5f);

	/** Deterministic 0..1 from a world point and a salt (stamp variation without touching the game's random stream). */
	CODEXTACTICS_API float HashUnit(const FVector2D& Point, uint32 Salt);

	/** A print / imprint at Position facing YawDeg (+ the spec's deterministic yaw / size variation). */
	CODEXTACTICS_API FSnowStamp MakePrint(const FSnowStampSpec& Spec, const FVector2D& Position, float YawDeg, float Scale = 1.f);

	/**
	 * Footprints along the walk: one print every Spec.SpacingCm of distance, alternating Spec.SideOffsetCm left / right of the path,
	 * aligned with the motion. The first call (or a move longer than MaxJumpCm: a teleport) only places the tracker. Returns the
	 * number of prints appended to Out.
	 */
	CODEXTACTICS_API int32 AdvanceStride(FSnowStrideState& State, const FVector2D& Position, const FSnowStampSpec& Spec, float MaxJumpCm,
		TArray<FSnowStamp>& Out, float Scale = 1.f);

	/**
	 * A trail (furrow / rut / drag): once the walker is Spec.SpacingCm from the last point, one Segment stamp from it to Position
	 * (width Spec.WidthCm). The first call (or a move longer than MaxJumpCm) only places the tracker. Returns the segments appended.
	 */
	CODEXTACTICS_API int32 AdvanceTrail(FSnowTrailState& State, const FVector2D& Position, const FSnowStampSpec& Spec, float MaxJumpCm,
		TArray<FSnowStamp>& Out);

	/** A Segment stamp from A to B. */
	CODEXTACTICS_API FSnowStamp MakeSegment(const FSnowStampSpec& Spec, const FVector2D& A, const FVector2D& B);

	/**
	 * The stamp's quad corners (world cm) in the order (back, left) (front, left) (front, right) (back, right) = local UV (0, 0) (1, 0)
	 * (1, 1) (0, 1); u along the length. Prints get the berm margin (size / PrintFill); a Segment is exactly its length long (adjacent
	 * segments do not overlap) and Width / PrintFill wide.
	 */
	CODEXTACTICS_API void BuildStampQuad(const FSnowStamp& Stamp, FVector2D OutCorners[4]);

	/** Keeps at most Max stamps, dropping the farthest from Focus first (order of the kept ones preserved). Returns the dropped count. */
	CODEXTACTICS_API int32 CapStamps(TArray<FSnowStamp>& Stamps, const FVector2D& Focus, int32 Max);

	/**
	 * The vertex colour M_SnowStamp decodes (linear; the canvas stores it as sRGB 8-bit): R = shape id x 16 + 8, G = Depth / 4, B = Berm / 4
	 * (clamped). Decode* read the 8-bit colour the GPU sees.
	 */
	CODEXTACTICS_API FLinearColor EncodeStampColor(ESnowStampShape Shape, float Depth, float Berm);
	CODEXTACTICS_API int32 DecodeStampShape(const FColor& Stored);
	CODEXTACTICS_API float DecodeStampDepth(const FColor& Stored);
	CODEXTACTICS_API float DecodeStampBerm(const FColor& Stored);

	/** The fade pass multiplier for Dt seconds: exp(-BaseRate x (1 + Boost x saturate(Refill)) x Dt). */
	CODEXTACTICS_API float ComputeFadeFactor(float Dt, float BaseRate, float Boost, float Refill);

	/**
	 * The smallest fade step a pass applies (1 - factor). The RT is 16-bit float (10-bit mantissa, ~0.1 % relative precision): a factor
	 * closer to 1 than ~0.05 % would round back to the same value and never fade, so slow fades wait until they have this much to do.
	 */
	constexpr float MinFadeStep = 0.004f;

	/** A fade pass is due: at least 1 / FadeHz since the last one AND the decay over Since (Rate x Since) reaches MinFadeStep. */
	CODEXTACTICS_API bool IsFadeDue(float SinceSeconds, float FadeHz, float Rate);

	/** MPC FreshSnow: rises with IsSnowing (0..1) over RiseSeconds, falls over FadeSeconds when it is not snowing; 0..1. */
	CODEXTACTICS_API float StepFreshSnow(float Current, float IsSnowing, float Dt, float RiseSeconds, float FadeSeconds);

	/**
	 * The ground gate: a landscape or a SnowGround-tagged floor always takes stamps; a static mesh floor only while
	 * Codex.Env.SnowTracksAnyGround is 1 (default until the levels have a real terrain); nothing else (no floor, a prop, a pawn).
	 */
	CODEXTACTICS_API bool IsSnowGround(ESnowGroundKind Kind, bool bTaggedSnowGround, bool bAnyGround);

	/** How an enemy archetype marks the snow. */
	enum class EEnemyTrack : uint8
	{
		None,
		Prints,
		Trail
	};

	/** The walk spec of an archetype: boots (Base / Spitter / Marksman), Brute boots, Hound / Cutter paws, a Frostbitten drag; a Cryo Drone flies (None). */
	CODEXTACTICS_API EEnemyTrack GetEnemyTrack(EEnemyArchetype Archetype, const FSnowStampTable& Table, const FSnowStampSpec*& OutSpec);

	/** The knockdown / death imprint of an archetype (beast / brute / humanoid). */
	CODEXTACTICS_API const FSnowStampSpec& GetEnemyImprint(EEnemyArchetype Archetype, const FSnowStampTable& Table);

	// --- Phase 3: event stamps (explosions, bullet dents, real-time drag) -------------------------------------------------------

	/**
	 * The crater spec of a GameplayEvents::Explosion kind: Grenade / Tripwire (two grenades) -> GrenadeCrater, Barrel -> BarrelCrater,
	 * Mine -> MineCrater, Cannon -> CannonCrater, anything else (Trap, unknown) -> Crater.
	 */
	CODEXTACTICS_API const FSnowStampSpec& GetCraterSpec(FName Kind, const FSnowStampTable& Table);

	/**
	 * The crater of an explosion: diameter = RadiusCm x Tuning.CraterDiameterPerRadius clamped to [CraterMinCm, CraterMaxCm] (no radius:
	 * the kind spec's LengthCm, clamped the same way), depth / rim from the kind spec, the thrown-snow ring CraterSprayScale x the crater
	 * with Table.CraterSpray's berm, Tuning.CraterClods clods.
	 */
	CODEXTACTICS_API FSnowCraterParams ComputeCrater(FName Kind, float RadiusCm, const FSnowStampTable& Table, const FSnowInteractionTuning& Tuning);

	/** The steep-walled pit inside a crater bowl is this share of the crater's diameter. */
	constexpr float CraterPitScale = 0.55f;

	/**
	 * The stamps of a crater at Centre: the bowl with its rim (Crater shape), a steep-walled pit in it (Body shape, CraterPitScale x the
	 * diameter, 0.6 x the depth), the thrown-snow ring (Crater shape, no bowl) and Params.Clods
	 * clods of Table.CraterClod on a ring 0.6..1.15 x the diameter from the centre (angles / distances from a hash of the centre:
	 * deterministic). Returns the stamps appended (3 + clods).
	 */
	CODEXTACTICS_API int32 MakeCraterStamps(const FSnowCraterParams& Params, const FSnowStampTable& Table, const FVector2D& Centre,
		TArray<FSnowStamp>& Out);

	/**
	 * The dent trace of a tracer: from BackCm before End along Start -> End to ExtendCm past End (a miss flies on). False (no trace) when
	 * the tracer has no length.
	 */
	CODEXTACTICS_API bool ComputeShotRay(const FVector& Start, const FVector& End, float ExtendCm, float BackCm, FVector& OutFrom, FVector& OutTo);

	/**
	 * A bullet dent at Hit: Spec's size / depth / berm (+ the deterministic size jitter); a grazing ray (RayDir against the surface
	 * Normal) stretches it along the ray up to MaxStretch x (1 / sin of the grazing angle).
	 */
	CODEXTACTICS_API FSnowStamp MakeDent(const FSnowStampSpec& Spec, const FVector& Hit, const FVector& RayDir, const FVector& Normal, float MaxStretch);

	/** Queues a tracer for its dent trace; a full queue (MaxQueue) drops its OLDEST first. Returns the number dropped. */
	CODEXTACTICS_API int32 EnqueueShot(TArray<FSnowShot>& Queue, const FSnowShot& Shot, int32 MaxQueue);

	/** Dent traces this frame: min(Queued, MaxPerFrame), never negative. */
	CODEXTACTICS_API int32 GetShotBudget(int32 Queued, int32 MaxPerFrame);
}
