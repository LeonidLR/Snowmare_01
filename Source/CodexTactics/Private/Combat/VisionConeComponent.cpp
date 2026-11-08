#include "Combat/VisionConeComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Combat/EnemyGhostActor.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Async/ParallelFor.h"
#include "Misc/App.h"
#include "Tasks/Task.h"
#include "UObject/GarbageCollection.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <intrin.h>
#endif

/** One rebuild's world query set-up (the game's sight trace: ECC_Visibility, simple collision, pawns / silhouettes ignored). */
struct FVisionConeTraceContext
{
	UWorld* World = nullptr;
	FCollisionQueryParams Params;
	FCollisionQueryParams ComponentParams;
	FVector Feet = FVector::ZeroVector;
	float Yaw = 0.f;
	float RangeCm = 0.f;
	int32 MaxLowObstacles = 0;
	/**
	 * Low obstacles measured per ray from a perch (2026-11-02, L_Gen_Depot_1): from a roof every wall below his eye is a "low" obstacle and
	 * competes with the crates / barrels in front of it - with MaxLowObstacles (3) a wall behind three crates was never measured, so its
	 * shadow was not cut.
	 */
	int32 MaxLowObstaclesElevated = 0;
	/**
	 * The elevated sampler of 2026-11-02 (Codex.VisionCones.ElevatedFix 1, default): the perch's supports skipped, its edge followed across
	 * blocks at his level, roofs further out / over lower buildings measured, the bigger low-obstacle budget. 0 = the 2026-11-01 sampler (A/B).
	 */
	bool bElevatedFix = true;
	float DepthProbeCm = 0.f;
	/**
	 * Elevated observer (bug 09, 2026-11-01): the block he stands on (a tower, a roof) when ground lies well below it (ProbePerch),
	 * else null - the flat model, nothing changes for him.
	 */
	UPrimitiveComponent* Perch = nullptr;
	/** Z of the perch's walking surface under his feet. */
	float PerchSurfaceZ = 0.f;
	/**
	 * Params with the perch and its supports ignored (the wall tops / slabs right under his feet, less than ElevatedMinDropCm below the
	 * roof): the low trace runs below its roof, from its edge on (the perch is its own obstacle).
	 */
	FCollisionQueryParams PerchParams;
	mutable int32 Traces = 0;

	/** A world direction in the cone frame (planar). */
	FVector2D ToLocal(const FVector& WorldDirection) const
	{
		const FVector Local = FRotator(0.f, -Yaw, 0.f).RotateVector(WorldDirection);
		return FVector2D(Local.X, Local.Y);
	}
};

namespace VisionConeConsole
{
	static TAutoConsoleVariable<int32> CVarVisionCones(TEXT("Codex.Debug.VisionCones"), -1,
		TEXT("Vision cones on the ground (operatives and enemies): 0 all off, 1 all on, -1 each character's bShowVisionCone (default)."));
	static TAutoConsoleVariable<int32> CVarPlayerEnemyCones(TEXT("Codex.VisionCones.Enemies"), 0,
		TEXT("The player's enemy vision-cone toggle (O / pause menu): 1 every living enemy's cone is drawn, 0 each enemy's own setting (default)."));
	static TAutoConsoleVariable<int32> CVarPlayerSquadCones(TEXT("Codex.VisionCones.Squad"), 0,
		TEXT("The player's squad vision-cone toggle (Shift+O): 1 every living operative's cone is drawn, 0 each operative's own setting (default)."));
	static TAutoConsoleVariable<int32> CVarMaxRebuildsPerFrame(TEXT("Codex.VisionCones.MaxRebuildsPerFrame"), 6,
		TEXT("Occluded vision cones: most cones re-traced in one frame (others wait a frame; a cone waiting 3 refresh periods goes anyway)."));

	static TAutoConsoleVariable<int32> CVarElevatedFix(TEXT("Codex.VisionCones.ElevatedFix"), 1,
		TEXT("Elevated vision cones (2026-11-02, L_Gen_Depot_1): 1 (default) the perch's supports (a wall top flush with the roof) are skipped, its edge is followed across the roof's slabs, roofs further out and over lower buildings hide the ground under them, 12 low obstacles per ray; 0 = the 2026-11-01 sampler (A/B)."));

	static TAutoConsoleVariable<float> CVarMaxMsInFlight(TEXT("Codex.VisionCones.MaxMsInFlight"), 150.f,
		TEXT("Occluded vision cones (bug #38): a batch not done by the next actor tick keeps running on the task threads up to this long, ms (its meshes swapped in whole when done; then waited for). 0 = waited for at the next tick (the 2026-10-19 behaviour)."));

	static TAutoConsoleVariable<float> CVarRefineGapCm(TEXT("Codex.VisionCones.RefineGapCm"), -1.f,
		TEXT("Occluded vision cones (fix/cone-rebuild-cost 2026-11-08): edge rays stop once the gap's arc at its far drawn end is under twice this, cm. -1 (default) = each cone's MinRefineGapCm (20), 0 = the angle floor only (the fan before 2026-11-08, A/B)."));

	static TAutoConsoleVariable<int32> CVarParallel(TEXT("Codex.VisionCones.Parallel"), 2,
		TEXT("Occluded vision cones: 2 a frame's rebuilds are traced on the task threads from the end of the actors' tick to the start of the next one (like the engine's async traces; default), 1 in parallel but waited for in the same frame, 0 inline on the game thread."));

	/** One cone's rebuild: everything the task threads need (no UObject touched there but through the scene queries). */
	struct FJob
	{
		TWeakObjectPtr<UVisionConeComponent> Cone;
		FVisionConeSightSpec Spec;
		FVisionConeTraceContext Context;
		float OutlineWidthCm = 8.f;
		/** Re-traced; else a merged layer's member re-cut from its last rays (Source as it was, Context at its last build). */
		bool bTrace = true;
		TSharedPtr<TArray<FVisionConeRay>, ESPMode::ThreadSafe> Source;
		/** The layer's share of Source (bMerged). */
		TArray<FVisionConeRay> Drawn;
		bool bMerged = false;
		int32 MergeGroup = 0;
		int32 Priority = 0;
		float MergeSampleCm = 150.f;
		FVisionConeWeaponZone Weapon;
		FVisionConeShape Shape;
		int32 Samples = 0;
		double Seconds = 0.0;
		/** Seconds as the thread's CPU time (FVisionConeBuildStats::TaskCpuSeconds). */
		double CpuSeconds = 0.0;
		/** The breakdown of Seconds (FVisionConeBuildStats). */
		double TraceSeconds = 0.0;
		double SampleSeconds = 0.0;
		double ClipSeconds = 0.0;
		double ShapeSeconds = 0.0;
	};

	/** A frame's rebuilds. */
	struct FBatch
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FJob> Jobs;
		UE::Tasks::FTask Task;
		bool bLaunched = false;
		double LaunchSeconds = 0.0;
		/** Game-thread time spent on it so far (set-up), s. */
		double GameSeconds = 0.0;
	};

	FVisionConeBuildStats Stats;
	/** Cones waiting for this frame's batch. */
	TArray<TWeakObjectPtr<UVisionConeComponent>> Pending;
	/** Cones with a merge group (the layers). */
	TArray<TWeakObjectPtr<UVisionConeComponent>> Layer;
	/** Batches traced on the task threads, completed at the next actor tick of their world. */
	TArray<TSharedPtr<FBatch, ESPMode::ThreadSafe>> InFlight;
	FDelegateHandle FlushHandle;
	FDelegateHandle CompleteHandle;
	FDelegateHandle CleanupHandle;
	uint64 BudgetFrame = MAX_uint64;
	int32 BudgetUsed = 0;

	/** Takes one rebuild of this frame's budget (bUrgent always gets one). */
	bool TakeBudget(bool bUrgent)
	{
		if (BudgetFrame != GFrameCounter)
		{
			BudgetFrame = GFrameCounter;
			BudgetUsed = 0;
		}
		if (!bUrgent && BudgetUsed >= FMath::Max(1, CVarMaxRebuildsPerFrame.GetValueOnGameThread()))
		{
			++Stats.Deferred;
			return false;
		}
		++BudgetUsed;
		Stats.PeakPerFrame = FMath::Max(Stats.PeakPerFrame, BudgetUsed);
		return true;
	}

	/** How far below a perch's roof its ground is searched, cm. */
	constexpr float PerchMaxDropCm = 5000.f;
	/** Blocks right under his feet less than ElevatedMinDropCm below the roof that ProbePerch skips as the perch's supports. */
	constexpr int32 MaxPerchSupports = 4;
	/** Blocks at his level a ray's perch edge is followed across (the next roof slab, the wall top flush with the roof's edge). */
	constexpr int32 MaxPerchHops = 4;
	/** Roofs at about his level further out along a ray (a roof hole's far side, a neighbouring roof) measured as obstacles. */
	constexpr int32 MaxFarRoofs = 2;

	/**
	 * Elevated observer (bug 09; any thread): what he stands on, and whether ground lies at least ElevatedMinDropCm below it right under
	 * him - then Context.Perch is set and every ray finds the perch's edge and the ground past it (SamplePerch). 2026-11-02 (L_Gen_Depot_1,
	 * "the cone does not cut the wall shadows from a roof"): the column under his feet may first cross the perch's own SUPPORTS - the generated
	 * halls' 9 m walls end flush with the roof slab (a man at the roof's edge lies over the wall), a ruin's upper interior walls end under its
	 * roof - each less than ElevatedMinDropCm below the surface: the old probe took that for "no drop" and drew the FLAT model (targets assumed
	 * at the roof's level, every sight line above every wall: no shadow at all). Now they are skipped (and ignored by the low trace) until a
	 * real drop is found. Two traces per cone for a ground-level observer (a floor, a landscape: nothing under it), as before.
	 */
	void ProbePerch(FVisionConeTraceContext& Context)
	{
		const FVector Feet = Context.Feet;
		FHitResult Hit;
		++Context.Traces;
		if (!Context.World->LineTraceSingleByChannel(Hit, Feet + FVector(0.f, 0.f, 40.f), Feet - FVector(0.f, 0.f, 60.f), ECC_Visibility, Context.Params)
			|| Hit.bStartPenetrating || !Hit.GetComponent())
		{
			return;
		}
		UPrimitiveComponent* Under = Hit.GetComponent();
		const float SurfaceZ = static_cast<float>(Hit.ImpactPoint.Z);
		FCollisionQueryParams Below(Context.Params);
		Below.AddIgnoredComponent(Under);
		for (int32 Support = 0; Support <= (Context.bElevatedFix ? MaxPerchSupports : 0); ++Support)
		{
			FHitResult Ground;
			++Context.Traces;
			if (!Context.World->LineTraceSingleByChannel(Ground, FVector(Feet.X, Feet.Y, SurfaceZ - 1.f), FVector(Feet.X, Feet.Y, SurfaceZ - PerchMaxDropCm),
					ECC_Visibility, Below)
				|| !Ground.GetComponent())
			{
				return; // nothing under him: a ground-level observer
			}
			if (!Ground.bStartPenetrating && SurfaceZ - Ground.ImpactPoint.Z >= VisionConeVisibility::ElevatedMinDropCm)
			{
				Context.Perch = Under;
				Context.PerchSurfaceZ = SurfaceZ;
				Context.PerchParams = MoveTemp(Below);
				return;
			}
			Below.AddIgnoredComponent(Ground.GetComponent()); // a support of his roof (a wall top, a second slab): part of the perch
		}
	}

	/**
	 * One ray from a perch (any thread): its edge along the ray, the ground past it (the lower of two columns 40 / 120 cm out - a ladder or a
	 * wall's foot right at the edge never reads as the ground) and the rim's top (a parapet, else the roof). The edge is the block under him
	 * traced back towards the feet just under its roof, FOLLOWED across every block that carries the walking surface on at his level (2026-11-02:
	 * a generated roof is several slabs round its stairwell / roof holes and its walls' tops are flush with it - the old edge was the first
	 * slab's, the "ground" past it the next slab at his own level, so the ray fell back to the flat model and no wall below cast a shadow).
	 * Elevated (the ground at least ElevatedMinDropCm below): Ray.GroundCm, the perch obstacle (0 .. edge, hides what stands under it) and the
	 * roofs at his level further out (a hole's far side, a neighbouring roof: they hide the ground under them) are set and the edge distance
	 * returned; else 0 - the ray keeps the flat model (a ray that never leaves the roof within the range: targets on the roof).
	 */
	float SamplePerch(const FVisionConeSightSpec& Spec, const FVisionConeTraceContext& Context, const FVector& Dir, FVisionConeRay& Ray)
	{
		const float ProbeZ = Context.PerchSurfaceZ - 5.f;
		const FVector Under(Context.Feet.X, Context.Feet.Y, ProbeZ);
		UPrimitiveComponent* Current = Context.Perch;
		float EdgeCm = 0.f;
		FVector EdgeNormal = FVector::ZeroVector;
		for (int32 Hop = 0; Hop <= MaxPerchHops; ++Hop)
		{
			FHitResult Probe;
			++Context.Traces;
			if (!Current->LineTraceComponent(Probe, Under + Dir * Context.RangeCm, Under + Dir * EdgeCm, Context.ComponentParams) || Probe.bStartPenetrating)
			{
				return 0.f; // this block reaches past the range: the ray stays on the roof
			}
			EdgeCm = Context.RangeCm - static_cast<float>(Probe.Distance);
			EdgeNormal = Probe.ImpactNormal;
			if (EdgeCm >= Context.RangeCm - 1.f)
			{
				return 0.f;
			}
			// Another block carrying the walking surface on just past this edge (within ElevatedMinDropCm below it): follow it.
			const FVector Next = Context.Feet + Dir * (EdgeCm + 5.f);
			if (Hop == MaxPerchHops || !Context.bElevatedFix)
			{
				break;
			}
			FHitResult Carry;
			++Context.Traces;
			if (!Context.World->LineTraceSingleByChannel(Carry, FVector(Next.X, Next.Y, Context.PerchSurfaceZ + 10.f),
					FVector(Next.X, Next.Y, Context.PerchSurfaceZ - VisionConeVisibility::ElevatedMinDropCm + 1.f), ECC_Visibility, Context.Params)
				|| Carry.bStartPenetrating || !Carry.GetComponent() || Carry.GetComponent() == Current)
			{
				break;
			}
			Current = Carry.GetComponent();
		}
		if (EdgeCm <= 1.f)
		{
			return 0.f;
		}
		float GroundZ = TNumericLimits<float>::Max();
		for (const float PastCm : { 40.f, 120.f })
		{
			const FVector Column = Context.Feet + Dir * (EdgeCm + PastCm);
			FHitResult Hit;
			++Context.Traces;
			if (Context.World->LineTraceSingleByChannel(Hit, FVector(Column.X, Column.Y, Context.PerchSurfaceZ + 10.f),
				FVector(Column.X, Column.Y, Context.PerchSurfaceZ - PerchMaxDropCm), ECC_Visibility, Context.Params))
			{
				GroundZ = FMath::Min(GroundZ, static_cast<float>(Hit.ImpactPoint.Z));
			}
		}
		const float GroundCm = GroundZ < TNumericLimits<float>::Max()
			? VisionConeVisibility::ElevatedGroundCm(static_cast<float>(Context.Feet.Z), GroundZ) : 0.f;
		if (GroundCm >= 0.f)
		{
			return 0.f;
		}
		// The rim: a parapet below the eye (one above it is a full wall for the high trace), else the roof itself.
		float TopCm = Context.PerchSurfaceZ - static_cast<float>(Context.Feet.Z);
		const FVector Rim = Context.Feet + Dir * FMath::Max(EdgeCm - 3.f, 0.f);
		FHitResult Hit;
		++Context.Traces;
		if (Context.World->LineTraceSingleByChannel(Hit, FVector(Rim.X, Rim.Y, Context.Feet.Z + Spec.GetHighTraceHeight(GroundCm)),
			FVector(Rim.X, Rim.Y, Context.PerchSurfaceZ - 10.f), ECC_Visibility, Context.Params) && !Hit.bStartPenetrating)
		{
			TopCm = static_cast<float>(Hit.ImpactPoint.Z - Context.Feet.Z);
		}
		Ray.GroundCm = GroundCm;
		FVisionConeObstacle Roof;
		Roof.NearCm = 0.f;
		Roof.FarCm = EdgeCm;
		Roof.TopCm = TopCm;
		Roof.FarNormal = Context.ToLocal(EdgeNormal);
		Roof.bHidesInside = true;
		Ray.Obstacles.Add(Roof);
		// Roofs at about his level further out (2026-11-02): the far side of a hole in his roof, a neighbouring roof - the traces of the
		// sight lines run above them (his eye) and below them (the targets' profiles on the ground), yet they hide the ground under them.
		float FromCm = EdgeCm + 2.f;
		for (int32 Far = 0; Context.bElevatedFix && Far < MaxFarRoofs && FromCm < Context.RangeCm - 1.f; ++Far)
		{
			const FVector Start = Context.Feet + Dir * FromCm;
			const FVector End = Context.Feet + Dir * Context.RangeCm;
			FHitResult Face;
			++Context.Traces;
			if (!Context.World->LineTraceSingleByChannel(Face, FVector(Start.X, Start.Y, ProbeZ), FVector(End.X, End.Y, ProbeZ), ECC_Visibility, Context.Params)
				|| Face.bStartPenetrating || !Face.GetComponent())
			{
				break;
			}
			UPrimitiveComponent* Block = Face.GetComponent();
			const float NearCm = FromCm + static_cast<float>(Face.Distance);
			// Its top just past the face must be at his level (a taller block is a full wall for the high trace: stop here).
			const FVector Column = Context.Feet + Dir * (NearCm + 3.f);
			FHitResult Top;
			++Context.Traces;
			if (!Block->LineTraceComponent(Top, FVector(Column.X, Column.Y, Context.PerchSurfaceZ + VisionConeVisibility::ElevatedMinDropCm),
					FVector(Column.X, Column.Y, Context.PerchSurfaceZ - VisionConeVisibility::ElevatedMinDropCm), Context.ComponentParams)
				|| Top.bStartPenetrating)
			{
				break;
			}
			const float TopZ = static_cast<float>(Top.ImpactPoint.Z);
			if (TopZ > Context.PerchSurfaceZ + VisionConeVisibility::ElevatedMinDropCm - 1.f)
			{
				break;
			}
			// Its back face (traced back towards the eye at the same height).
			float FarCm = Context.RangeCm;
			FVector2D FarNormal = FVector2D::ZeroVector;
			FHitResult Back;
			++Context.Traces;
			if (Block->LineTraceComponent(Back, FVector(End.X, End.Y, ProbeZ), FVector(Column.X, Column.Y, ProbeZ), Context.ComponentParams) && !Back.bStartPenetrating)
			{
				FarCm = Context.RangeCm - static_cast<float>(Back.Distance);
				FarNormal = Context.ToLocal(Back.ImpactNormal);
			}
			FVisionConeObstacle Other;
			Other.NearCm = NearCm;
			Other.FarCm = FMath::Max(FarCm, NearCm);
			Other.TopCm = TopZ - static_cast<float>(Context.Feet.Z);
			Other.NearNormal = Context.ToLocal(Face.ImpactNormal);
			Other.FarNormal = FarNormal;
			Other.bHidesInside = true;
			Ray.Obstacles.Add(Other);
			FromCm = Other.FarCm + 2.f;
		}
		return EdgeCm;
	}

	/**
	 * Traces one ray of a cone (any thread): from a perch its edge and the ground below first (SamplePerch), then the sight lines' top,
	 * then the lower obstacles' front face, top and back face (heights from the observer's feet, the profiles on the ray's ground).
	 */
	void SampleRay(const FVisionConeSightSpec& Spec, const FVisionConeTraceContext& Context, float AngleDeg, FVisionConeRay& Ray)
	{
		const FVector Dir = FRotator(0.f, Context.Yaw + AngleDeg, 0.f).Vector();
		FHitResult Hit;
		// 0. Elevated (bug 09): the perch's edge, the ground below; the low trace then starts past the edge, the perch ignored.
		const float PerchEdgeCm = Context.Perch ? SamplePerch(Spec, Context, Dir, Ray) : 0.f;
	if (Context.Perch && PerchEdgeCm <= 0.f && Context.bElevatedFix)
	{
		// Bug #38: no edge / no ground below it found along this ray (a roof's hop limit, the next slab at his level): the old flat model
		// drew it at the roof's level over the whole range - the needles. Marked, not traced further, dropped by CastRays.
		Ray.bNoGround = true;
		return;
	}
		const float HighCm = Spec.GetHighTraceHeight(Ray.GroundCm);
		const float LowCm = Spec.GetLowTraceHeight(Ray.GroundCm);
		// 1. The sight lines' top: anything this tall hides every level behind it.
		float WallCm = Context.RangeCm;
		const FVector HighStart = Context.Feet + FVector(0.f, 0.f, HighCm);
		++Context.Traces;
		if (Context.World->LineTraceSingleByChannel(Hit, HighStart, HighStart + Dir * Context.RangeCm, ECC_Visibility, Context.Params))
		{
			WallCm = Hit.bStartPenetrating ? 0.f : static_cast<float>(Hit.Distance);
			const UPrimitiveComponent* Component = Hit.GetComponent();
			Ray.BlockerKey = Component ? static_cast<uint64>(Component->GetUniqueID()) + 1 : 1;
			Ray.BlockerNormal = Context.ToLocal(Hit.ImpactNormal);
			Ray.Obstacles.Add({ WallCm, WallCm, VisionConeVisibility::FullHeightCm, Ray.BlockerNormal, Ray.BlockerNormal });
		}
		// 2. Lower obstacles (under the sight lines): front face, top and back face of each, nearest first.
		const FVector LowOrigin = Context.Feet + FVector(0.f, 0.f, LowCm);
		const FCollisionQueryParams& LowParams = PerchEdgeCm > 0.f ? Context.PerchParams : Context.Params;
		TOptional<FCollisionQueryParams> SkipParams;
		float FromCm = PerchEdgeCm > 0.f ? PerchEdgeCm + 1.f : 0.f;
		int32 LowObstacles = 0;
		// From a perch every wall below his eye is a "low" obstacle here (2026-11-02): a bigger budget, so a wall behind crates still casts its shadow.
		const int32 MaxLow = PerchEdgeCm > 0.f && Context.bElevatedFix ? FMath::Max(Context.MaxLowObstacles, Context.MaxLowObstaclesElevated) : Context.MaxLowObstacles;
		for (int32 Attempt = 0; Attempt < MaxLow + 2 && LowObstacles < MaxLow && FromCm < WallCm - 1.f; ++Attempt)
		{
			++Context.Traces;
			const FCollisionQueryParams& Params = SkipParams.IsSet() ? SkipParams.GetValue() : LowParams;
			if (!Context.World->LineTraceSingleByChannel(Hit, LowOrigin + Dir * FromCm, LowOrigin + Dir * WallCm, ECC_Visibility, Params))
			{
				break;
			}
			UPrimitiveComponent* Component = Hit.GetComponent();
			const float NearCm = FromCm + static_cast<float>(Hit.Distance);
			if (!Component || NearCm >= WallCm - 1.f)
			{
				break;
			}
			if (Hit.bStartPenetrating || (Hit.Distance < 0.5f && FromCm > 0.f))
			{
				// Started inside a deep obstacle (past its probed depth): skip that component.
				if (!SkipParams.IsSet())
				{
					SkipParams.Emplace(LowParams);
				}
				SkipParams->AddIgnoredComponent(Component);
				continue;
			}
			const FVector2D NearNormal = Context.ToLocal(Hit.ImpactNormal);
			FVector2D FarNormal = FVector2D::ZeroVector;
			FHitResult Probe;
			// Its top just behind the front face (the high trace passed above it there).
			float TopCm = LowCm;
			const FVector Column = Context.Feet + Dir * (NearCm + 2.f);
			++Context.Traces;
			if (Component->LineTraceComponent(Probe, Column + FVector(0.f, 0.f, HighCm), Column + FVector(0.f, 0.f, LowCm - 1.f), Context.ComponentParams))
			{
				TopCm = FMath::Max(static_cast<float>(Probe.ImpactPoint.Z - Context.Feet.Z), LowCm);
			}
			// Its back face (traced back towards the eye).
			float FarCm = NearCm + Context.DepthProbeCm;
			bool bBackFace = false;
			++Context.Traces;
			if (Component->LineTraceComponent(Probe, LowOrigin + Dir * (NearCm + Context.DepthProbeCm), LowOrigin + Dir * NearCm, Context.ComponentParams))
			{
				FarCm = NearCm + Context.DepthProbeCm - static_cast<float>(Probe.Distance);
				FarNormal = Context.ToLocal(Probe.ImpactNormal);
				bBackFace = true;
			}
			FarCm = FMath::Clamp(FarCm, NearCm, WallCm);
			Ray.Obstacles.Add({ NearCm, FarCm, TopCm, NearNormal, FarNormal });
			++LowObstacles;
			// From a perch (2026-11-02): a roof carried on at this wall's top (a roofed building below him) hides the ground under it - the
			// sight lines run above it (his eye) and below it (the targets' profiles on the ground), so it is measured like his own roof.
			if (PerchEdgeCm > 0.f && Context.bElevatedFix && bBackFace && FarCm < WallCm - 1.f)
			{
				const float TopZ = static_cast<float>(Context.Feet.Z) + TopCm;
				const FVector Past = Context.Feet + Dir * (FarCm + 30.f);
				FHitResult Cover;
				++Context.Traces;
				if (Context.World->LineTraceSingleByChannel(Cover, FVector(Past.X, Past.Y, TopZ + 10.f), FVector(Past.X, Past.Y, TopZ - 60.f), ECC_Visibility, LowParams)
					&& !Cover.bStartPenetrating && Cover.GetComponent() && Cover.GetComponent() != Component && FMath::Abs(Cover.ImpactPoint.Z - TopZ) < 30.f)
				{
					UPrimitiveComponent* RoofBlock = Cover.GetComponent();
					const float RoofZ = static_cast<float>(Cover.ImpactPoint.Z) - 3.f;
					const FVector End = Context.Feet + Dir * WallCm;
					float RoofFarCm = WallCm;
					FVector2D RoofFarNormal = FVector2D::ZeroVector;
					FHitResult Back;
					++Context.Traces;
					if (RoofBlock->LineTraceComponent(Back, FVector(End.X, End.Y, RoofZ), FVector(Past.X, Past.Y, RoofZ), Context.ComponentParams) && !Back.bStartPenetrating)
					{
						RoofFarCm = WallCm - static_cast<float>(Back.Distance);
						RoofFarNormal = Context.ToLocal(Back.ImpactNormal);
					}
					FVisionConeObstacle Roofed;
					Roofed.NearCm = FarCm;
					Roofed.FarCm = FMath::Clamp(RoofFarCm, FarCm, WallCm);
					Roofed.TopCm = static_cast<float>(Cover.ImpactPoint.Z - Context.Feet.Z);
					Roofed.NearNormal = FarNormal;
					Roofed.FarNormal = RoofFarNormal;
					Roofed.bHidesInside = true;
					Ray.Obstacles.Add(Roofed);
				}
			}
			FromCm = FarCm + 1.f;
		}
	}

	/**
	 * CPU time of the calling thread, s (Windows: QueryThreadCycleTime - counted in time-stamp-counter ticks - over the TSC rate measured against
	 * the performance counter since the module loaded); a negative value where there is no such counter. Unlike FPlatformTime::Seconds it
	 * does not run on while the OS has the thread's core given to another process (the loaded verification machine).
	 */
	double ThreadCpuSeconds()
	{
#if PLATFORM_WINDOWS
		struct FCalibration
		{
			uint64 Tsc = __rdtsc();
			double Seconds = FPlatformTime::Seconds();
		};
		static const FCalibration Start;
		const double Elapsed = FPlatformTime::Seconds() - Start.Seconds;
		ULONG64 Cycles = 0;
		if (Elapsed < 0.5 || !::QueryThreadCycleTime(::GetCurrentThread(), &Cycles))
		{
			return -1.0;
		}
		const double TscPerSecond = static_cast<double>(__rdtsc() - Start.Tsc) / Elapsed;
		return TscPerSecond > 0.0 ? static_cast<double>(Cycles) / TscPerSecond : -1.0;
#else
		return -1.0;
#endif
	}

	/** One cone's rays (any thread). */
	void RunTrace(FJob& Job)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VisionCone_Trace);
		const double CpuStart = ThreadCpuSeconds();
		const double Start = FPlatformTime::Seconds();
		ProbePerch(Job.Context);
		double SampleSeconds = 0.0;
		Job.Source = MakeShared<TArray<FVisionConeRay>, ESPMode::ThreadSafe>(VisionConeVisibility::CastRays(Job.Spec, [&Job, &SampleSeconds](float Angle, FVisionConeRay& Ray)
		{
			const double RayStart = FPlatformTime::Seconds();
			SampleRay(Job.Spec, Job.Context, Angle, Ray);
			SampleSeconds += FPlatformTime::Seconds() - RayStart;
		}, &Job.Samples));
		Job.Seconds = FPlatformTime::Seconds() - Start;
		const double CpuEnd = ThreadCpuSeconds();
		Job.CpuSeconds = CpuStart >= 0.0 && CpuEnd >= 0.0 ? CpuEnd - CpuStart : 0.0;
		Job.TraceSeconds = Job.Seconds;
		Job.SampleSeconds = SampleSeconds;
	}

	FVector2D ToWorld(const FVector2D& Origin, float YawDeg, const FVector2D& Local)
	{
		float Sin = 0.f;
		float Cos = 1.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(YawDeg));
		return Origin + FVector2D(Local.X * Cos - Local.Y * Sin, Local.X * Sin + Local.Y * Cos);
	}

	/** One cone's meshes (any thread, after every job of the batch has its rays): its share of its layer, the outline round the layer, the weapon zone. */
	void RunShape(FJob& Job, const TArray<FJob>& Jobs)
	{
		if (!Job.Source.IsValid())
		{
			return;
		}
		const double CpuStart = ThreadCpuSeconds();
		const double Start = FPlatformTime::Seconds();
		const FVector2D Origin(Job.Context.Feet.X, Job.Context.Feet.Y);
		const float Yaw = Job.Context.Yaw;
		TArray<FVisionConeLayerMember, TInlineAllocator<4>> Others;
		for (const FJob& Other : Jobs)
		{
			if (&Other != &Job && Job.MergeGroup != 0 && Other.MergeGroup == Job.MergeGroup && Other.Source.IsValid())
			{
				FVisionConeLayerMember Member;
				Member.Rays = Other.Source;
				Member.HalfAngleDeg = Other.Spec.HalfAngleDeg;
				Member.MaxRangeCm = Other.Spec.GetMaxRange();
				Member.Origin = FVector2D(Other.Context.Feet.X, Other.Context.Feet.Y);
				Member.YawDeg = Other.Context.Yaw;
				Member.Priority = Other.Priority;
				Others.Add(MoveTemp(Member));
			}
		}
		FVisionConeShapeOptions Options;
		Options.OutlineWidthCm = Job.OutlineWidthCm;
		Options.Weapon = Job.Weapon;
		const TArray<FVisionConeRay>& Own = *Job.Source;
		if (Others.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VisionCone_ClipToLayer);
			const double ClipStart = FPlatformTime::Seconds();
			Job.Drawn = Own;
			VisionConeVisibility::ClipToLayer(Job.Drawn, Origin, Yaw, Job.Priority, Others, Job.MergeSampleCm);
			Job.ClipSeconds = FPlatformTime::Seconds() - ClipStart;
			Job.bMerged = true;
			Options.OutlineRays = &Own;
			Options.WeaponRays = &Own;
			Options.OutlineHidden = [&Others, Origin, Yaw](const FVector2D& Local)
			{
				const FVector2D World = ToWorld(Origin, Yaw, Local);
				return Others.ContainsByPredicate([&World](const FVisionConeLayerMember& Other) { return Other.LevelAt(World) > 0; });
			};
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(VisionCone_BuildShape);
			const double ShapeStart = FPlatformTime::Seconds();
			Job.Shape = VisionConeVisibility::BuildShape(Job.bMerged ? Job.Drawn : Own, Job.Spec.Levels.Num(), Job.Spec.HalfAngleDeg, Options);
			Job.ShapeSeconds = FPlatformTime::Seconds() - ShapeStart;
		}
		Job.Seconds += FPlatformTime::Seconds() - Start;
		const double CpuEnd = ThreadCpuSeconds();
		Job.CpuSeconds += CpuStart >= 0.0 && CpuEnd >= 0.0 ? CpuEnd - CpuStart : 0.0;
	}

	bool IsSameRange(float A, float B)
	{
		return FMath::Abs(A - B) <= FMath::Max(1.f, 0.01f * FMath::Max(A, B));
	}

	bool SameSpec(const FVisionConeSightSpec& A, const FVisionConeSightSpec& B)
	{
		if (A.Levels.Num() != B.Levels.Num() || FMath::Abs(A.EyeHeightCm - B.EyeHeightCm) > 1.f || FMath::Abs(A.HalfAngleDeg - B.HalfAngleDeg) > 0.1f)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Levels.Num(); ++Index)
		{
			// 2026-11-04: the range follows the weather / time of day continuously - a drift under 1 % waits for the normal cadence
			// (moved / turned / StaticRefreshSeconds) instead of an urgent rebuild every frame of a dusk or a weather blend.
			if (!IsSameRange(A.Levels[Index].RangeCm, B.Levels[Index].RangeCm)
				|| FMath::Abs(A.Levels[Index].ProfileHeightCm - B.Levels[Index].ProfileHeightCm) > 1.f)
			{
				return false;
			}
		}
		return true;
	}
}


UVisionConeComponent::UVisionConeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false; // the owner drives it (UpdateCone)
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	bUseAsyncCooking = false;
	// The occluded shape keeps the heading it was traced at (its shadows stay on their walls between rebuilds).
	SetUsingAbsoluteRotation(true);
	SetVisibility(false);
	FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent")));
}

int32 UVisionConeComponent::GetGlobalMode()
{
	return VisionConeConsole::CVarVisionCones.GetValueOnGameThread();
}

bool UVisionConeComponent::IsPlayerEnemyConesOn()
{
	return VisionConeConsole::CVarPlayerEnemyCones.GetValueOnGameThread() != 0;
}

void UVisionConeComponent::SetPlayerEnemyCones(bool bOn)
{
	VisionConeConsole::CVarPlayerEnemyCones->Set(bOn ? 1 : 0, ECVF_SetByCode);
}

bool UVisionConeComponent::IsPlayerSquadConesOn()
{
	return VisionConeConsole::CVarPlayerSquadCones.GetValueOnGameThread() != 0;
}

void UVisionConeComponent::SetPlayerSquadCones(bool bOn)
{
	VisionConeConsole::CVarPlayerSquadCones->Set(bOn ? 1 : 0, ECVF_SetByCode);
}

const FVisionConeBuildStats& UVisionConeComponent::GetBuildStats()
{
	return VisionConeConsole::Stats;
}

void UVisionConeComponent::ResetBuildStats()
{
	VisionConeConsole::Stats = FVisionConeBuildStats();
}

int32 UVisionConeComponent::GetFillVertexCount() const
{
	const FProcMeshSection* Section = const_cast<UVisionConeComponent*>(this)->GetProcMeshSection(0);
	return Section ? Section->ProcVertexBuffer.Num() : 0;
}

int32 UVisionConeComponent::GetLightVertexCount() const
{
	const FProcMeshSection* Section = const_cast<UVisionConeComponent*>(this)->GetProcMeshSection(2);
	return Section ? Section->ProcVertexBuffer.Num() : 0;
}

const TArray<FVisionConeRay>& UVisionConeComponent::GetBuiltRays() const
{
	static const TArray<FVisionConeRay> Empty;
	return SourceRays.IsValid() ? *SourceRays : Empty;
}

int32 UVisionConeComponent::GetWeaponZoneVertexCount() const
{
	int32 Count = 0;
	for (const int32 Index : { 3, 4 })
	{
		const FProcMeshSection* Section = const_cast<UVisionConeComponent*>(this)->GetProcMeshSection(Index);
		Count += Section ? Section->ProcVertexBuffer.Num() : 0;
	}
	return Count;
}

void UVisionConeComponent::SetMergeGroup(int32 Group)
{
	if (Group == MergeGroup)
	{
		return;
	}
	RequestLayerReshape(); // the old layer fills the hole
	MergeGroup = Group;
	VisionConeConsole::Layer.RemoveAll([this](const TWeakObjectPtr<UVisionConeComponent>& Weak) { return !Weak.IsValid() || Weak.Get() == this; });
	if (Group != 0)
	{
		VisionConeConsole::Layer.Add(this);
	}
	bReshapeRequested = true;
	RequestLayerReshape();
}

void UVisionConeComponent::RequestLayerReshape() const
{
	if (MergeGroup == 0)
	{
		return;
	}
	for (const TWeakObjectPtr<UVisionConeComponent>& Weak : VisionConeConsole::Layer)
	{
		if (UVisionConeComponent* Other = Weak.Get(); Other && Other != this && Other->MergeGroup == MergeGroup)
		{
			Other->bReshapeRequested = true;
		}
	}
}

void UVisionConeComponent::OnUnregister()
{
	RequestLayerReshape();
	VisionConeConsole::Layer.RemoveAll([this](const TWeakObjectPtr<UVisionConeComponent>& Weak) { return !Weak.IsValid() || Weak.Get() == this; });
	Super::OnUnregister();
}

void UVisionConeComponent::SetWeaponZone(const FVisionConeWeaponZone& Zone)
{
	WeaponZone = Zone;
	WeaponZone.EdgeWidthCm = WeaponEdgeWidthCm;
	if (!Zone.IsSet())
	{
		WeaponZone = FVisionConeWeaponZone();
	}
}

uint8 UVisionConeComponent::GetDrawnMergedLevelAt(const FVector& WorldPoint) const
{
	if (!bShown || !bBuiltOccluded || !bDrawnMerged)
	{
		return GetDrawnLevelAt(WorldPoint);
	}
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(WorldPoint);
	return VisionConeVisibility::LevelAtPoint(DrawnRays, BuiltHalfAngle, FVector2D(Local.X, Local.Y));
}

FVisionConeMeshAudit UVisionConeComponent::AuditMesh(float FarCm) const
{
	FVisionConeMeshAudit Audit;
	UVisionConeComponent* Self = const_cast<UVisionConeComponent*>(this);
	for (int32 Section = 0; Section < Self->GetNumSections(); ++Section)
	{
		const FProcMeshSection* Mesh = Self->GetProcMeshSection(Section);
		if (!Mesh || !Mesh->bSectionVisible)
		{
			continue;
		}
		const TArray<FProcMeshVertex>& Vertices = Mesh->ProcVertexBuffer;
		const TArray<uint32>& Indices = Mesh->ProcIndexBuffer;
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const FVector Corners[3] = { Vertices[Indices[Index]].Position, Vertices[Indices[Index + 1]].Position, Vertices[Indices[Index + 2]].Position };
			++Audit.Triangles;
			const double MinZ = FMath::Min3(Corners[0].Z, Corners[1].Z, Corners[2].Z);
			const double MaxZ = FMath::Max3(Corners[0].Z, Corners[1].Z, Corners[2].Z);
			Audit.Slanted += MaxZ - MinZ > 30.0 ? 1 : 0;
			Audit.Above += MaxZ > 5.0 ? 1 : 0;
			for (const FVector& Corner : Corners)
			{
				if (FMath::Abs(Corner.Z) < 30.0 && FVector2D(Corner.X, Corner.Y).Size() > FarCm)
				{
					++Audit.Floating;
					break;
				}
			}
		}
	}
	return Audit;
}

FVector UVisionConeComponent::GetOwnerFeet() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return GetComponentLocation();
	}
	const ACharacter* Character = Cast<ACharacter>(Owner);
	const FVector Feet = Owner->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	return bViewOverride ? Feet + ViewOverrideOffset : Feet;
}

float UVisionConeComponent::GetOwnerYaw() const
{
	if (bViewOverride)
	{
		return ViewOverrideYaw;
	}
	const AActor* Owner = GetOwner();
	return Owner ? static_cast<float>(Owner->GetActorRotation().Yaw) : static_cast<float>(GetComponentRotation().Yaw);
}

void UVisionConeComponent::SetViewOverride(const FVector& FeetOffset, float Yaw)
{
	// The cone jumps to the peek point at once; later moves / turns rebuild as the owner's own (UpdateCone's moved / turned check).
	bRebuildRequested |= !bViewOverride;
	bViewOverride = true;
	ViewOverrideOffset = FVector(FeetOffset.X, FeetOffset.Y, 0.f);
	ViewOverrideYaw = Yaw;
}

void UVisionConeComponent::ClearViewOverride()
{
	if (bViewOverride)
	{
		bViewOverride = false;
		ViewOverrideOffset = FVector::ZeroVector;
		bRebuildRequested = true; // back to his own eyes at once
	}
}

bool UVisionConeComponent::IsWorthUpdating() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}
	// Headless runs (smokes, -nullrhi) render nothing: always up to date there.
	if (FApp::CanEverRender() && !Owner->WasRecentlyRendered(0.25f) && !WasRecentlyRendered(0.25f))
	{
		return false;
	}
	if (MaxUpdateDistanceCm > 0.f)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager
			&& FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Owner->GetActorLocation()) > MaxUpdateDistanceCm)
		{
			return false;
		}
	}
	return true;
}

void UVisionConeComponent::UpdateCone(bool bShow, float RangeCm, float HalfAngleDeg, const FLinearColor& TargetColor, float DeltaSeconds,
	const FVisionConeSightSpec* Sight)
{
	if (!bShow || RangeCm <= 0.f)
	{
		if (bShown)
		{
			bShown = false;
			SetVisibility(false);
			RequestLayerReshape(); // the rest of the layer draws what this cone drew
		}
		return;
	}
	const float Half = VisionConeRules::ClampHalfAngle(HalfAngleDeg);
	const bool bOccluded = bClipToSight && Sight && Sight->Levels.Num() > 0 && GetWorld();
	if (!bOccluded)
	{
		if (bBuiltOccluded || !VisionConeConsole::IsSameRange(RangeCm, BuiltRange) || FMath::Abs(Half - BuiltHalfAngle) > 0.1f)
		{
			RebuildPlain(RangeCm, Half);
		}
		SetWorldRotation(FRotator(0.f, GetOwnerYaw(), 0.f));
	}
	else
	{
		FVisionConeSightSpec Spec = *Sight;
		Spec.HalfAngleDeg = Half;
		Spec.RayStepDeg = RayStepDeg;
		Spec.MaxRefineDepth = EdgeRefineDepth;
		Spec.MaxRays = MaxRaysPerCone;
		Spec.RaySpacingCm = RaySpacingCm;
		Spec.MinRayStepDeg = MinRayStepDeg;
		Spec.bWorldGrid = true;
		Spec.SliverMinWidthCm = SliverMinWidthCm;
		Spec.SliverFromCm = SliverFromCm;
		const float RefineGapOverride = VisionConeConsole::CVarRefineGapCm.GetValueOnGameThread();
		Spec.MinRefineGapCm = RefineGapOverride >= 0.f ? RefineGapOverride : MinRefineGapCm;
		SinceBuildSeconds += DeltaSeconds;
		// While a batch with this cone is still running, the changes are measured against what it was launched with (no second trace of the same pose).
		const FVisionConeSightSpec& RefSpec = bInFlight ? InFlightSpec : BuiltSpec;
		const FVector RefFeet = bInFlight ? InFlightFeet : BuiltFeet;
		const float RefYaw = bInFlight ? InFlightYaw : BuiltYaw;
		const bool bShapeChanged = (!bBuiltOccluded && !bInFlight) || !VisionConeConsole::SameSpec(Spec, RefSpec);
		const float Period = 1.f / FMath::Max(RefreshHz, 1.f);
		const bool bMoved = FVector::Dist2D(GetOwnerFeet(), RefFeet) > MoveRebuildCm
			|| FMath::Abs(FMath::FindDeltaAngleDegrees(RefYaw, GetOwnerYaw())) > TurnRebuildDeg
			|| FMath::Abs(GetOwnerFeet().Z - RefFeet.Z) > 30.f;
		const bool bDue = SinceBuildSeconds >= Period && (bMoved || (StaticRefreshSeconds > 0.f && SinceBuildSeconds >= StaticRefreshSeconds));
		const bool bUrgent = bShapeChanged || bRebuildRequested || !bShown;
		const bool bReshape = bBuiltOccluded && (bReshapeRequested || !(WeaponZone == BuiltWeaponZone));
		if (bPendingBuild && GFrameCounter - PendingFrame > 1)
		{
			// Queued in a frame whose batch never ran (no actor tick after it, e.g. a paused world): build it now.
			bPendingBuild = false;
			RebuildOccluded(PendingSpec);
		}
		if (bPendingBuild)
		{
			if (bPendingTrace)
			{
				PendingSpec = Spec; // already in a batch to come: the latest spec
			}
			else if (bUrgent || bDue)
			{
				PendingSpec = Spec; // a re-cut waiting: make it a re-trace
				bPendingTrace = true;
			}
		}
		else if (bUrgent || bDue)
		{
			if (!bUrgent && !IsWorthUpdating())
			{
				++VisionConeConsole::Stats.SkippedOffscreen;
			}
			else if (VisionConeConsole::TakeBudget(bUrgent || SinceBuildSeconds >= 3.f * Period))
			{
				QueueOccludedRebuild(Spec);
			}
		}
		else if (bReshape && !bInFlight)
		{
			QueueOccludedRebuild(BuiltSpec, /*bTrace*/ false);
		}
		SetWorldRotation(FRotator(0.f, BuiltYaw, 0.f));
	}
	ShownColor = bColorInitialised ? VisionConeRules::StepColor(ShownColor, TargetColor, DeltaSeconds, ColorLerpSpeed) : TargetColor;
	bColorInitialised = true;
	ApplyColor();
	PlaceAtFeet();
	if (!bShown)
	{
		bShown = true;
		SetVisibility(true);
	}
}

void UVisionConeComponent::AddSection(int32 Index, const FVisionConeMesh& Mesh, float Alpha)
{
	if (Mesh.Vertices.Num() == 0)
	{
		ClearMeshSection(Index);
		return;
	}
	TArray<FVector> Normals;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Normals.Init(FVector::UpVector, Mesh.Vertices.Num());
	Colors.Init(FLinearColor(1.f, 1.f, 1.f, Alpha), Mesh.Vertices.Num());
	Tangents.Init(FProcMeshTangent(1.f, 0.f, 0.f), Mesh.Vertices.Num());
	UV.Reserve(Mesh.Vertices.Num());
	for (const FVector& Vertex : Mesh.Vertices)
	{
		UV.Add(FVector2D(Vertex.X / FMath::Max(BuiltRange, 1.f), 0.5f + Vertex.Y / FMath::Max(2.f * BuiltRange, 1.f)));
	}
	CreateMeshSection_LinearColor(Index, Mesh.Vertices, Mesh.Triangles, Normals, UV, Colors, Tangents, /*bCreateCollision*/ false);
}

void UVisionConeComponent::RebuildPlain(float RangeCm, float HalfAngleDeg)
{
	BuiltRange = RangeCm;
	BuiltHalfAngle = HalfAngleDeg;
	bBuiltOccluded = false;
	SourceRays.Reset();
	DrawnRays.Reset();
	bDrawnMerged = false;
	ClearAllMeshSections();
	// The mesh lies at local Z 0; PlaceAtFeet moves the component to the feet + HeightAboveFeetCm.
	AddSection(0, VisionConeRules::BuildFill(RangeCm, HalfAngleDeg, Segments), FillAlpha);
	if (bDrawOutline)
	{
		// A hair above the fill (Godot 0.5 cm) so the band never z-fights with it.
		AddSection(1, VisionConeRules::BuildOutline(RangeCm, HalfAngleDeg, Segments, OutlineWidthCm, 0.5f), OutlineAlpha);
	}
	++BuildCount;
	EnsureMaterials();
}

void UVisionConeComponent::RebuildOccluded(const FVisionConeSightSpec& Spec)
{
	PendingSpec = Spec;
	UVisionConeComponent* Self = this;
	RebuildBatch(GetWorld(), MakeArrayView(&Self, 1), /*bAsync*/ false);
}

void UVisionConeComponent::QueueOccludedRebuild(const FVisionConeSightSpec& Spec, bool bTrace)
{
	if (VisionConeConsole::CVarParallel.GetValueOnGameThread() <= 0)
	{
		PendingSpec = bTrace ? Spec : PendingSpec;
		bPendingTrace = bTrace || !bBuiltOccluded;
		RebuildOccluded(bTrace ? Spec : BuiltSpec);
		return;
	}
	PendingSpec = Spec;
	PendingFrame = GFrameCounter;
	if (!bPendingBuild)
	{
		bPendingBuild = true;
		bPendingTrace = bTrace || !bBuiltOccluded;
		VisionConeConsole::Pending.Add(this);
	}
	else
	{
		bPendingTrace |= bTrace;
	}
	if (!VisionConeConsole::FlushHandle.IsValid())
	{
		VisionConeConsole::FlushHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&UVisionConeComponent::FlushPending);
		VisionConeConsole::CompleteHandle = FWorldDelegates::OnWorldPreActorTick.AddStatic(&UVisionConeComponent::CompleteOnTick);
		VisionConeConsole::CleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&UVisionConeComponent::CompleteOnCleanup);
	}
}

void UVisionConeComponent::FlushPending(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// A batch of this world still running on the task threads: the new rebuilds wait for it (one batch per world at a time).
	const bool bBusy = VisionConeConsole::InFlight.ContainsByPredicate([World](const TSharedPtr<VisionConeConsole::FBatch, ESPMode::ThreadSafe>& Batch)
	{
		return Batch->World.Get() == World;
	});
	TArray<UVisionConeComponent*, TInlineAllocator<16>> Batch;
	for (auto It = VisionConeConsole::Pending.CreateIterator(); It; ++It)
	{
		UVisionConeComponent* Cone = It->Get();
		if (!Cone || !Cone->bPendingBuild)
		{
			It.RemoveCurrent();
		}
		else if (Cone->GetWorld() == World)
		{
			if (bBusy)
			{
				Cone->PendingFrame = GFrameCounter; // the batch hook ran: not a paused world
				continue;
			}
			Batch.Add(Cone);
			It.RemoveCurrent();
		}
	}
	if (Batch.Num() > 0)
	{
		RebuildBatch(World, Batch, VisionConeConsole::CVarParallel.GetValueOnGameThread() >= 2);
	}
}

void UVisionConeComponent::CompleteOnTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	CompleteBatches(World, /*bForce*/ false);
}

void UVisionConeComponent::CompleteOnCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	CompleteBatches(World);
}

void UVisionConeComponent::CompleteBatches(UWorld* World, bool bForce)
{
	const double MaxSeconds = FMath::Max(0.f, VisionConeConsole::CVarMaxMsInFlight.GetValueOnGameThread()) / 1000.0;
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < VisionConeConsole::InFlight.Num();)
	{
		const TSharedPtr<VisionConeConsole::FBatch, ESPMode::ThreadSafe> Batch = VisionConeConsole::InFlight[Index];
		UWorld* BatchWorld = Batch->World.Get();
		if (BatchWorld && BatchWorld != World)
		{
			++Index;
			continue;
		}
		// Bug #38: not done yet - it keeps running (the meshes swap in whole when it is), up to Codex.VisionCones.MaxMsInFlight.
		if (!bForce && BatchWorld && Batch->bLaunched && !Batch->Task.IsCompleted() && Now - Batch->LaunchSeconds < MaxSeconds)
		{
			++VisionConeConsole::Stats.FramesInFlight;
			++Index;
			continue;
		}
		VisionConeConsole::InFlight.RemoveAt(Index);
		const double Start = FPlatformTime::Seconds();
		if (Batch->bLaunched)
		{
			Batch->Task.Wait();
		}
		VisionConeConsole::Stats.WaitSeconds += FPlatformTime::Seconds() - Start;
		if (BatchWorld && !BatchWorld->bIsTearingDown)
		{
			ApplyBatch(*Batch);
		}
		else
		{
			for (VisionConeConsole::FJob& Job : Batch->Jobs)
			{
				if (UVisionConeComponent* Cone = Job.Cone.Get())
				{
					Cone->bInFlight = false;
				}
			}
		}
		VisionConeConsole::Stats.Seconds += Batch->GameSeconds + (FPlatformTime::Seconds() - Start);
	}
}

void UVisionConeComponent::ApplyBatch(VisionConeConsole::FBatch& Batch)
{
	FVisionConeBuildStats& Stats = VisionConeConsole::Stats;
	const double Start = FPlatformTime::Seconds();
	for (VisionConeConsole::FJob& Job : Batch.Jobs)
	{
		if (UVisionConeComponent* Cone = Job.Cone.Get())
		{
			Cone->ApplyOccludedBuild(Job);
		}
		if (Job.bTrace)
		{
			++Stats.Builds;
			Stats.Rays += Job.Samples;
			Stats.Traces += Job.Context.Traces;
			Stats.TraceSeconds += Job.TraceSeconds;
			Stats.SampleSeconds += Job.SampleSeconds;
			Stats.ClipSeconds += Job.ClipSeconds;
			Stats.ShapeSeconds += Job.ShapeSeconds;
		}
		else
		{
			++Stats.Reshapes;
			Stats.ReshapeSeconds += Job.Seconds;
		}
		Stats.TaskSeconds += Job.Seconds;
		Stats.TaskCpuSeconds += Job.CpuSeconds;
	}
	++Stats.Batches;
	Stats.ApplySeconds += FPlatformTime::Seconds() - Start;
}

void UVisionConeComponent::RebuildBatch(UWorld* World, TArrayView<UVisionConeComponent* const> Cones, bool bAsync)
{
	if (!World || Cones.Num() == 0)
	{
		return;
	}
	CompleteBatches(World); // never two batches of a world at once
	const double Start = FPlatformTime::Seconds();
	// Bodies never block the view (UTacticalSightSubsystem::HasClearSight / AEnemyCharacter::FindVisibleOperative).
	FCollisionQueryParams Shared(SCENE_QUERY_STAT(VisionConeOcclusion), /*bTraceComplex*/ false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Shared.AddIgnoredActor(*It);
	}
	for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
	{
		Shared.AddIgnoredActor(*It);
	}
	// A merged layer is cut as a whole (bug #38): its other shown members join the batch to re-cut their share (no re-trace).
	TArray<UVisionConeComponent*, TInlineAllocator<16>> All(Cones);
	TArray<int32, TInlineAllocator<4>> Groups;
	for (const UVisionConeComponent* Cone : Cones)
	{
		if (Cone->MergeGroup != 0)
		{
			Groups.AddUnique(Cone->MergeGroup);
		}
	}
	for (const TWeakObjectPtr<UVisionConeComponent>& Weak : VisionConeConsole::Layer)
	{
		UVisionConeComponent* Member = Weak.Get();
		if (Member && Groups.Contains(Member->MergeGroup) && !All.Contains(Member) && Member->GetWorld() == World && Member->bShown
			&& Member->bBuiltOccluded && Member->SourceRays.IsValid())
		{
			Member->bPendingTrace = false;
			All.Add(Member);
		}
	}
	const TSharedPtr<VisionConeConsole::FBatch, ESPMode::ThreadSafe> Batch = MakeShared<VisionConeConsole::FBatch, ESPMode::ThreadSafe>();
	Batch->World = World;
	Batch->Jobs.SetNum(All.Num());
	for (int32 Index = 0; Index < All.Num(); ++Index)
	{
		UVisionConeComponent* Cone = All[Index];
		VisionConeConsole::FJob& Job = Batch->Jobs[Index];
		Job.Cone = Cone;
		Job.bTrace = Cone->bPendingTrace || !Cone->bBuiltOccluded || !Cone->SourceRays.IsValid();
		Job.Spec = Job.bTrace ? Cone->PendingSpec : Cone->BuiltSpec;
		Job.OutlineWidthCm = Cone->OutlineWidthCm;
		Job.MergeGroup = Cone->bShown ? Cone->MergeGroup : 0;
		Job.Priority = static_cast<int32>(Cone->GetUniqueID());
		Job.MergeSampleCm = Cone->MergeSampleCm;
		Job.Weapon = Cone->WeaponZone;
		Cone->bPendingBuild = false;
		Cone->bPendingTrace = false;
		FVisionConeTraceContext& Context = Job.Context;
		Context.World = World;
		Context.Params = Shared;
		Context.ComponentParams = FCollisionQueryParams(SCENE_QUERY_STAT(VisionConeObstacle), /*bTraceComplex*/ false);
		Context.Feet = Job.bTrace ? Cone->GetOwnerFeet() : Cone->BuiltFeet;
		Context.Yaw = Job.bTrace ? Cone->GetOwnerYaw() : Cone->BuiltYaw;
		Context.RangeCm = Job.Spec.GetMaxRange();
		Context.MaxLowObstacles = Cone->MaxLowObstaclesPerRay;
		Context.MaxLowObstaclesElevated = Cone->MaxLowObstaclesPerRayElevated;
		Context.bElevatedFix = VisionConeConsole::CVarElevatedFix.GetValueOnGameThread() != 0;
		Context.DepthProbeCm = Cone->ObstacleDepthProbeCm;
		Job.Spec.GridYawDeg = Context.Yaw;
		if (Job.bTrace)
		{
			Cone->bRebuildRequested = false;
		}
		else
		{
			Job.Source = Cone->SourceRays;
		}
		Cone->bInFlight = true;
		Cone->InFlightSpec = Job.Spec;
		Cone->InFlightFeet = Context.Feet;
		Cone->InFlightYaw = Context.Yaw;
	}
	// Scene queries only (nothing in the world changes): the engine's async traces run the same raycasts on task threads,
	// launched after the actors' tick - so does an async batch; one not done by the next actor tick keeps running (CompleteOnTick).
	auto RunAll = [Batch]()
	{
		FGCScopeGuard NoGarbageCollection; // hit results resolve components: no GC while the rays are traced
		const EParallelForFlags Flags = Batch->Jobs.Num() > 1 ? EParallelForFlags::Unbalanced : EParallelForFlags::ForceSingleThread;
		ParallelFor(Batch->Jobs.Num(), [&Batch](int32 Index)
		{
			if (Batch->Jobs[Index].bTrace)
			{
				VisionConeConsole::RunTrace(Batch->Jobs[Index]);
			}
		}, Flags);
		ParallelFor(Batch->Jobs.Num(), [&Batch](int32 Index)
		{
			VisionConeConsole::RunShape(Batch->Jobs[Index], Batch->Jobs);
		}, Flags);
	};
	if (bAsync)
	{
		Batch->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(RunAll));
		Batch->bLaunched = true;
		Batch->LaunchSeconds = FPlatformTime::Seconds();
		Batch->GameSeconds = FPlatformTime::Seconds() - Start;
		VisionConeConsole::InFlight.Add(Batch);
		return;
	}
	RunAll();
	ApplyBatch(*Batch);
	VisionConeConsole::Stats.Seconds += FPlatformTime::Seconds() - Start;
}

void UVisionConeComponent::ApplyOccludedBuild(VisionConeConsole::FJob& Job)
{
	bInFlight = false;
	if (!Job.Source.IsValid())
	{
		return;
	}
	if (Job.bTrace)
	{
		SourceRays = Job.Source;
		BuiltSpec = Job.Spec;
		BuiltFeet = Job.Context.Feet;
		BuiltYaw = Job.Context.Yaw;
		BuiltRange = Job.Spec.Levels.Num() > 0 ? Job.Spec.Levels[0].RangeCm : 0.f;
		BuiltHalfAngle = Job.Spec.HalfAngleDeg;
		bBuiltOccluded = true;
		SinceBuildSeconds = 0.f;
		++BuildCount;
	}
	DrawnRays = Job.bMerged ? MoveTemp(Job.Drawn) : TArray<FVisionConeRay>();
	bDrawnMerged = Job.bMerged;
	BuiltWeaponZone = Job.Weapon;
	bReshapeRequested = false;
	SetWorldRotation(FRotator(0.f, BuiltYaw, 0.f));

	const FVisionConeShape& Shape = Job.Shape;
	AddSection(0, Shape.Full, FillAlpha);
	if (bDrawOutline)
	{
		AddSection(1, Shape.Outline, OutlineAlpha);
	}
	else
	{
		ClearMeshSection(1);
	}
	// The light tone a hair above the fill (they never overlap, but keep the order stable).
	FVisionConeMesh Light = Shape.Light;
	for (FVector& Vertex : Light.Vertices)
	{
		Vertex.Z += 0.2f;
	}
	AddSection(2, Light, FillAlpha * LightToneAlphaScale);
	AddSection(3, Shape.WeaponBand, WeaponBandAlpha);
	AddSection(4, Shape.WeaponEdge, WeaponEdgeAlpha);
	EnsureMaterials();
}

void UVisionConeComponent::EnsureMaterials()
{
	UMaterialInterface* Base = ConeMaterial ? ConeMaterial.Get() : (FallbackMaterial.IsNull() ? nullptr : FallbackMaterial.LoadSynchronous());
	if (!Base)
	{
		return;
	}
	if (!FillMaterial || FillMaterial->Parent != Base)
	{
		FillMaterial = UMaterialInstanceDynamic::Create(Base, this);
		LineMaterial = UMaterialInstanceDynamic::Create(Base, this);
		LightMaterial = UMaterialInstanceDynamic::Create(Base, this);
		WeaponBandMaterial = UMaterialInstanceDynamic::Create(Base, this);
		WeaponEdgeMaterial = UMaterialInstanceDynamic::Create(Base, this);
	}
	SetMaterial(0, FillMaterial);
	SetMaterial(1, LineMaterial);
	SetMaterial(2, LightMaterial);
	SetMaterial(3, WeaponBandMaterial);
	SetMaterial(4, WeaponEdgeMaterial);
	ApplyColor();
}

void UVisionConeComponent::ApplyColor()
{
	auto Push = [this](UMaterialInstanceDynamic* Material, const FLinearColor& Color, float Alpha)
	{
		if (!Material)
		{
			return;
		}
		if (!ColorParameterName.IsNone())
		{
			Material->SetVectorParameterValue(ColorParameterName, FLinearColor(Color.R, Color.G, Color.B, Alpha));
		}
		if (!OpacityParameterName.IsNone())
		{
			Material->SetScalarParameterValue(OpacityParameterName, Alpha);
		}
	};
	Push(FillMaterial, ShownColor, FillAlpha);
	Push(LineMaterial, ShownColor, OutlineAlpha);
	Push(LightMaterial, FMath::Lerp(ShownColor, FLinearColor::White, LightToneWhiten), FillAlpha * LightToneAlphaScale);
	Push(WeaponBandMaterial, FMath::Lerp(ShownColor, FLinearColor::White, WeaponBandWhiten), WeaponBandAlpha);
	Push(WeaponEdgeMaterial, FMath::Lerp(ShownColor, FLinearColor::White, WeaponEdgeWhiten), WeaponEdgeAlpha);
}

void UVisionConeComponent::PlaceAtFeet()
{
	const AActor* Owner = GetOwner();
	const UCapsuleComponent* Capsule = Owner ? Cast<UCapsuleComponent>(Owner->GetRootComponent()) : nullptr;
	const float FeetZ = Capsule && GetAttachParent() == Capsule ? -Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
	FVector Wanted(0.f, 0.f, FeetZ + HeightAboveFeetCm);
	if (bViewOverride && Capsule && GetAttachParent() == Capsule)
	{
		Wanted += Capsule->GetComponentTransform().InverseTransformVectorNoScale(ViewOverrideOffset); // the peek point (relative to the capsule)
	}
	if (!GetRelativeLocation().Equals(Wanted, 0.5f))
	{
		SetRelativeLocation(Wanted);
	}
}

uint8 UVisionConeComponent::GetDrawnLevelAt(const FVector& WorldPoint) const
{
	if (!bShown)
	{
		return 0;
	}
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(WorldPoint);
	if (!bBuiltOccluded)
	{
		return VisionConeRules::IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, FVector(Local.X, Local.Y, 0.f), BuiltRange, BuiltHalfAngle) ? 1 : 0;
	}
	return VisionConeVisibility::LevelAtPoint(GetBuiltRays(), BuiltHalfAngle, FVector2D(Local.X, Local.Y));
}
