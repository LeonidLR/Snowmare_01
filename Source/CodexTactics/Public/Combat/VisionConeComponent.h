#pragma once

#include "CoreMinimal.h"
#include "Combat/VisionConeRules.h"
#include "Combat/VisionConeVisibility.h"
#include "ProceduralMeshComponent.h"
#include "VisionConeComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
namespace VisionConeConsole { struct FBatch; }

/** Cost of the occluded cone rebuilds since the last ResetBuildStats (smokes, the perf report). */
struct CODEXTACTICS_API FVisionConeBuildStats
{
	int64 Builds = 0;
	int64 Rays = 0;
	int64 Traces = 0;
	/** Game-thread time of the rebuilds (set-up, the parallel ray work it waits for, the mesh upload), s. */
	double Seconds = 0.0;
	/** Of Seconds: waiting for the task threads (an async batch not done when the next actor tick starts), s. */
	double WaitSeconds = 0.0;
	/** Of Seconds: the mesh upload (procedural mesh sections), s. */
	double ApplySeconds = 0.0;
	/** Ray / mesh work summed over the worker threads, s (> Seconds: the batch ran in parallel). */
	double TaskSeconds = 0.0;
	/** Batches (one per frame with rebuilds). */
	int64 Batches = 0;
	/** Rebuilds put off a frame by the per-frame budget. */
	int64 Deferred = 0;
	/** Rebuilds skipped while the cone was off screen / too far from the camera. */
	int64 SkippedOffscreen = 0;
	/** Most rebuilds in one frame. */
	int32 PeakPerFrame = 0;
};

/**
 * Ground vision cone (user request 2026-10-11; Godot Scenes/movements/player.gd _setup_vision_cone,
 * _rebuild_vision_cone_mesh, _update_vision_cone). A flat shape (fill) plus an outline band, a few cm above the owner's
 * feet. The owner drives it every tick through UpdateCone (shape from its perception / weapon, colour from its state,
 * visibility from its bShowVisionCone and the console variable Codex.Debug.VisionCones).
 *
 * Occlusion (game designer 2026-10-19, Commandos style): given the owner's sight spec the cone is a visibility polygon —
 * a fan of rays from the eye traced against sight-blocking geometry (ECC_Visibility, simple collision, pawns and stasis
 * silhouettes ignored: the trace of the game's sight check), cut where nothing is seen and drawn in a light tone where
 * only the outer level (a standing operative) is seen (VisionConeVisibility). The rays are re-traced at RefreshHz at most,
 * only when the owner moved / turned (or every StaticRefreshSeconds), only while the cone is on screen and near the
 * camera, and at most Codex.VisionCones.MaxRebuildsPerFrame cones per frame; the rebuilds of a frame are traced together
 * on the task threads from the end of the actors' tick (FWorldDelegates::OnWorldPostActorTick, where the engine launches
 * its own async traces) to the start of the next one, then the meshes are uploaded (one frame later;
 * Codex.VisionCones.Parallel 1 = waited for in the same frame, 0 = inline on the game thread). Between rebuilds the shape
 * keeps its world heading (shadows stay on their walls) and follows the owner's feet.
 * Geometry and colours: VisionConeRules / VisionConeVisibility (tests CodexTactics.Combat.VisionCone.*).
 *
 * Look: ConeMaterial (Gemini's slot; TANDEM request) — a translucent unlit material with a vector parameter
 * ColorParameterName (rgb = colour, a = opacity) and optionally a scalar OpacityParameterName. Without one an engine
 * translucent material is used (FallbackMaterial), tinted through the same parameters and the vertex colours. The light
 * tone is the same material with its own instance (paler colour, lower opacity).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UVisionConeComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UVisionConeComponent(const FObjectInitializer& ObjectInitializer);

	/** The cone's material (fill, light tone and outline). Empty: FallbackMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	TObjectPtr<UMaterialInterface> ConeMaterial;

	/** Engine material used while ConeMaterial is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial;

	/** Vector parameter that receives the colour (rgb) and the opacity (a). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	FName ColorParameterName = TEXT("Color");

	/** Scalar parameter that receives the opacity (none = skip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	FName OpacityParameterName = TEXT("Opacity");

	/** Arc segments of the plain (unoccluded) cone (Godot 32). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "3", ClampMax = "128"))
	int32 Segments = VisionConeRules::GodotSegments;

	/** Height of the cone above the owner's feet, cm (Godot 8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0"))
	float HeightAboveFeetCm = VisionConeRules::GodotHeightCm;

	/** Fill opacity (Godot 0.22). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0", ClampMax = "1"))
	float FillAlpha = VisionConeRules::GodotFillAlpha;

	/** Draw the outline band (Godot line strip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone")
	bool bDrawOutline = true;

	/** Outline opacity (Godot 0.85). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0", ClampMax = "1"))
	float OutlineAlpha = VisionConeRules::GodotLineAlpha;

	/** Outline band width, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "1"))
	float OutlineWidthCm = 8.f;

	/** Colour change speed (Godot 14). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone", meta = (ClampMin = "0"))
	float ColorLerpSpeed = VisionConeRules::GodotColorLerpSpeed;

	// --- Occlusion (game designer 2026-10-19) ---

	/** Cut the cone where its owner cannot see (walls) and draw the «standing only» light tone; off: the plain sector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion")
	bool bClipToSight = true;

	/** Angle between the base rays, degrees (shadow edges get extra rays by bisection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0.25", ClampMax = "10"))
	float RayStepDeg = 1.5f;

	/** Bisection steps at a shadow edge / corner (each halves the angle between two rays). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0", ClampMax = "10"))
	int32 EdgeRefineDepth = 6;

	/** Hard cap on rays per rebuild (base + edge rays). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "8", ClampMax = "1024"))
	int32 MaxRaysPerCone = 320;

	/** Low obstacles (below the sight lines) measured per ray. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxLowObstaclesPerRay = 3;

	/** How deep a low obstacle is probed for its back face, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "10"))
	float ObstacleDepthProbeCm = 400.f;

	/** Most rebuilds per second while the owner moves / turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "1", ClampMax = "60"))
	float RefreshHz = 12.f;

	/** Rebuild once the owner moved this far since the last one, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0"))
	float MoveRebuildCm = 15.f;

	/** Rebuild once the owner turned this far since the last one, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0"))
	float TurnRebuildDeg = 2.f;

	/** A still owner re-traces this often anyway (doors, moved props), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0.1"))
	float StaticRefreshSeconds = 2.f;

	/** No rebuild while the camera is farther than this from the owner, cm (0 = any distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0"))
	float MaxUpdateDistanceCm = 9000.f;

	/** Light tone («only a standing operative is seen here»): opacity = FillAlpha x this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0", ClampMax = "1"))
	float LightToneAlphaScale = 0.45f;

	/** Light tone colour = the state colour blended towards white by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Cone|Occlusion", meta = (ClampMin = "0", ClampMax = "1"))
	float LightToneWhiten = 0.35f;

	/**
	 * Called by the owner every tick: shows / hides the cone, rebuilds the mesh when the shape changed (occluded: when the
	 * owner moved / turned, throttled), eases the colour towards TargetColor and keeps the cone at the feet. Sight: the
	 * owner's eye and the levels to draw (nullptr or bClipToSight off: the plain sector of RangeCm).
	 */
	void UpdateCone(bool bShow, float RangeCm, float HalfAngleDeg, const FLinearColor& TargetColor, float DeltaSeconds,
		const FVisionConeSightSpec* Sight = nullptr);

	/** The next UpdateCone rebuilds at once (ignores the throttle and the budget). */
	void RequestRebuild() { bRebuildRequested = true; }

	/**
	 * Draws the cone from another point and direction than the owner's feet and facing (peek sight, 2026-10-19: a peeking
	 * operative's cone opens from his leaned-out eye point round the corner): FeetOffset = world offset (planar) from the owner's
	 * feet, Yaw = the cone's world yaw. Kept until ClearViewOverride.
	 */
	void SetViewOverride(const FVector& FeetOffset, float Yaw);
	void ClearViewOverride();
	bool HasViewOverride() const { return bViewOverride; }

	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	bool IsConeShown() const { return bShown; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	float GetConeRangeCm() const { return BuiltRange; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	float GetConeHalfAngleDeg() const { return BuiltHalfAngle; }
	UFUNCTION(BlueprintPure, Category = "Vision Cone")
	FLinearColor GetShownColor() const { return ShownColor; }
	/** Vertices of the full-tone fill section (smokes). */
	int32 GetFillVertexCount() const;
	/** Vertices of the light-tone section (smokes). */
	int32 GetLightVertexCount() const;
	/** The shape is the occluded one (rays traced). */
	bool IsOccluded() const { return bBuiltOccluded; }
	/** Sight levels drawn (1 = one tone; 2 = full / light). */
	int32 GetLevelCount() const { return bBuiltOccluded ? BuiltSpec.Levels.Num() : 1; }
	/** The sight spec of the last occluded rebuild. */
	const FVisionConeSightSpec& GetBuiltSpec() const { return BuiltSpec; }
	/** Rays of the last occluded rebuild (cone frame). */
	const TArray<FVisionConeRay>& GetBuiltRays() const { return BuiltRays; }
	/** Rebuilds of this cone so far. */
	int32 GetBuildCount() const { return BuildCount; }

	/**
	 * What the drawn cone shows at a world point (planar): 0 not covered, else the number of sight levels that see a target
	 * there (occluded: GetLevelCount() = full tone, lower = light tone; plain sector: 1 inside).
	 */
	uint8 GetDrawnLevelAt(const FVector& WorldPoint) const;

	/** Console Codex.Debug.VisionCones: 0 all off, 1 all on, -1 (default) each actor's bShowVisionCone. */
	static int32 GetGlobalMode();

	/**
	 * The player's enemy-cone toggle (user decision 2026-10-13, Commandos style): O / the pause menu entry / console
	 * Codex.VisionCones.Enemies. On, every living enemy's cone is drawn (operatives keep their own setting); the debug CVar
	 * 0 still hides everything. Not saved between sessions (a session setting).
	 */
	static bool IsPlayerEnemyConesOn();
	static void SetPlayerEnemyCones(bool bOn);

	static const FVisionConeBuildStats& GetBuildStats();
	static void ResetBuildStats();

private:
	void RebuildPlain(float RangeCm, float HalfAngleDeg);
	/** Rebuilds now (inline). */
	void RebuildOccluded(const FVisionConeSightSpec& Spec);
	/** Rebuilds with this frame's batch (or inline when batching is off). */
	void QueueOccludedRebuild(const FVisionConeSightSpec& Spec);
	/**
	 * Traces the pending specs of Cones in parallel on the task threads; bAsync: returns at once and the meshes are uploaded
	 * at the world's next actor tick (CompleteBatches), else waits and uploads them now.
	 */
	static void RebuildBatch(UWorld* World, TArrayView<UVisionConeComponent* const> Cones, bool bAsync);
	/** Runs the batch of World's pending cones (FWorldDelegates::OnWorldPostActorTick). */
	static void FlushPending(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	/** Waits for World's batches in flight and uploads their meshes (next actor tick, world clean-up, before a new batch). */
	static void CompleteBatches(UWorld* World);
	static void CompleteOnTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	static void CompleteOnCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	static void ApplyBatch(VisionConeConsole::FBatch& Batch);
	void ApplyOccludedBuild(const FVisionConeSightSpec& Spec, const FVector& Feet, float Yaw, TArray<FVisionConeRay>&& Rays, const FVisionConeShape& Shape);
	/** Worth re-tracing now: on screen (owner or cone rendered recently; always without rendering) and near the camera. */
	bool IsWorthUpdating() const;
	void AddSection(int32 Index, const FVisionConeMesh& Mesh, float Alpha);
	void EnsureMaterials();
	void ApplyColor();
	void PlaceAtFeet();
	FVector GetOwnerFeet() const;
	float GetOwnerYaw() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FillMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LineMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LightMaterial;

	bool bViewOverride = false;
	FVector ViewOverrideOffset = FVector::ZeroVector;
	float ViewOverrideYaw = 0.f;

	float BuiltRange = -1.f;
	float BuiltHalfAngle = -1.f;
	bool bShown = false;
	bool bColorInitialised = false;
	FLinearColor ShownColor = FLinearColor::White;

	bool bBuiltOccluded = false;
	bool bRebuildRequested = false;
	bool bPendingBuild = false;
	uint64 PendingFrame = 0;
	FVisionConeSightSpec PendingSpec;
	FVisionConeSightSpec BuiltSpec;
	TArray<FVisionConeRay> BuiltRays;
	FVector BuiltFeet = FVector::ZeroVector;
	float BuiltYaw = 0.f;
	float SinceBuildSeconds = 0.f;
	int32 BuildCount = 0;
};
