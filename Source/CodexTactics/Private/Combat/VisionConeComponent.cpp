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

/** One rebuild's world query set-up (the game's sight trace: ECC_Visibility, simple collision, pawns / silhouettes ignored). */
struct FVisionConeTraceContext
{
	UWorld* World = nullptr;
	FCollisionQueryParams Params;
	FCollisionQueryParams ComponentParams;
	FVector Feet = FVector::ZeroVector;
	float Yaw = 0.f;
	float RangeCm = 0.f;
	float HighCm = 0.f;
	float LowCm = 0.f;
	int32 MaxLowObstacles = 0;
	float DepthProbeCm = 0.f;
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
	static TAutoConsoleVariable<int32> CVarMaxRebuildsPerFrame(TEXT("Codex.VisionCones.MaxRebuildsPerFrame"), 6,
		TEXT("Occluded vision cones: most cones re-traced in one frame (others wait a frame; a cone waiting 3 refresh periods goes anyway)."));

	static TAutoConsoleVariable<int32> CVarParallel(TEXT("Codex.VisionCones.Parallel"), 2,
		TEXT("Occluded vision cones: 2 a frame's rebuilds are traced on the task threads from the end of the actors' tick to the start of the next one (like the engine's async traces; default), 1 in parallel but waited for in the same frame, 0 inline on the game thread."));

	/** One cone's rebuild: everything the task threads need (no UObject touched there but through the scene queries). */
	struct FJob
	{
		TWeakObjectPtr<UVisionConeComponent> Cone;
		FVisionConeSightSpec Spec;
		FVisionConeTraceContext Context;
		float OutlineWidthCm = 8.f;
		TArray<FVisionConeRay> Rays;
		FVisionConeShape Shape;
		int32 Samples = 0;
		double Seconds = 0.0;
	};

	/** A frame's rebuilds. */
	struct FBatch
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FJob> Jobs;
		UE::Tasks::FTask Task;
		bool bLaunched = false;
		/** Game-thread time spent on it so far (set-up), s. */
		double GameSeconds = 0.0;
	};

	FVisionConeBuildStats Stats;
	/** Cones waiting for this frame's batch. */
	TArray<TWeakObjectPtr<UVisionConeComponent>> Pending;
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

	/** Traces one ray of a cone (any thread): the sight lines' top, then the lower obstacles' front face, top and back face. */
	void SampleRay(const FVisionConeTraceContext& Context, float AngleDeg, FVisionConeRay& Ray)
	{
		const FVector Dir = FRotator(0.f, Context.Yaw + AngleDeg, 0.f).Vector();
		FHitResult Hit;
		// 1. The sight lines' top: anything this tall hides every level behind it.
		float WallCm = Context.RangeCm;
		const FVector HighStart = Context.Feet + FVector(0.f, 0.f, Context.HighCm);
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
		const FVector LowOrigin = Context.Feet + FVector(0.f, 0.f, Context.LowCm);
		TOptional<FCollisionQueryParams> SkipParams;
		float FromCm = 0.f;
		int32 LowObstacles = 0;
		for (int32 Attempt = 0; Attempt < Context.MaxLowObstacles + 2 && LowObstacles < Context.MaxLowObstacles && FromCm < WallCm - 1.f; ++Attempt)
		{
			++Context.Traces;
			const FCollisionQueryParams& Params = SkipParams.IsSet() ? SkipParams.GetValue() : Context.Params;
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
					SkipParams.Emplace(Context.Params);
				}
				SkipParams->AddIgnoredComponent(Component);
				continue;
			}
			const FVector2D NearNormal = Context.ToLocal(Hit.ImpactNormal);
			FVector2D FarNormal = FVector2D::ZeroVector;
			FHitResult Probe;
			// Its top just behind the front face (the high trace passed above it there).
			float TopCm = Context.LowCm;
			const FVector Column = Context.Feet + Dir * (NearCm + 2.f);
			++Context.Traces;
			if (Component->LineTraceComponent(Probe, Column + FVector(0.f, 0.f, Context.HighCm), Column + FVector(0.f, 0.f, Context.LowCm - 1.f), Context.ComponentParams))
			{
				TopCm = FMath::Max(static_cast<float>(Probe.ImpactPoint.Z - Context.Feet.Z), Context.LowCm);
			}
			// Its back face (traced back towards the eye).
			float FarCm = NearCm + Context.DepthProbeCm;
			++Context.Traces;
			if (Component->LineTraceComponent(Probe, LowOrigin + Dir * (NearCm + Context.DepthProbeCm), LowOrigin + Dir * NearCm, Context.ComponentParams))
			{
				FarCm = NearCm + Context.DepthProbeCm - static_cast<float>(Probe.Distance);
				FarNormal = Context.ToLocal(Probe.ImpactNormal);
			}
			FarCm = FMath::Clamp(FarCm, NearCm, WallCm);
			Ray.Obstacles.Add({ NearCm, FarCm, TopCm, NearNormal, FarNormal });
			++LowObstacles;
			FromCm = FarCm + 1.f;
		}
	}

	/** One cone's rays and meshes (any thread). */
	void RunJob(FJob& Job)
	{
		const double Start = FPlatformTime::Seconds();
		Job.Rays = VisionConeVisibility::CastRays(Job.Spec, [&Job](float Angle, FVisionConeRay& Ray)
		{
			SampleRay(Job.Context, Angle, Ray);
		}, &Job.Samples);
		Job.Shape = VisionConeVisibility::BuildShape(Job.Rays, Job.Spec.Levels.Num(), Job.Spec.HalfAngleDeg, Job.OutlineWidthCm, 0.f, 0.5f);
		Job.Seconds = FPlatformTime::Seconds() - Start;
	}

	bool SameSpec(const FVisionConeSightSpec& A, const FVisionConeSightSpec& B)
	{
		if (A.Levels.Num() != B.Levels.Num() || FMath::Abs(A.EyeHeightCm - B.EyeHeightCm) > 1.f || FMath::Abs(A.HalfAngleDeg - B.HalfAngleDeg) > 0.1f)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Levels.Num(); ++Index)
		{
			if (FMath::Abs(A.Levels[Index].RangeCm - B.Levels[Index].RangeCm) > 1.f
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

FVector UVisionConeComponent::GetOwnerFeet() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return GetComponentLocation();
	}
	const ACharacter* Character = Cast<ACharacter>(Owner);
	return Owner->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
}

float UVisionConeComponent::GetOwnerYaw() const
{
	const AActor* Owner = GetOwner();
	return Owner ? static_cast<float>(Owner->GetActorRotation().Yaw) : static_cast<float>(GetComponentRotation().Yaw);
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
		}
		return;
	}
	const float Half = VisionConeRules::ClampHalfAngle(HalfAngleDeg);
	const bool bOccluded = bClipToSight && Sight && Sight->Levels.Num() > 0 && GetWorld();
	if (!bOccluded)
	{
		if (bBuiltOccluded || FMath::Abs(RangeCm - BuiltRange) > 1.f || FMath::Abs(Half - BuiltHalfAngle) > 0.1f)
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
		SinceBuildSeconds += DeltaSeconds;
		const bool bShapeChanged = !bBuiltOccluded || !VisionConeConsole::SameSpec(Spec, BuiltSpec);
		const float Period = 1.f / FMath::Max(RefreshHz, 1.f);
		const bool bMoved = FVector::Dist2D(GetOwnerFeet(), BuiltFeet) > MoveRebuildCm
			|| FMath::Abs(FMath::FindDeltaAngleDegrees(BuiltYaw, GetOwnerYaw())) > TurnRebuildDeg
			|| FMath::Abs(GetOwnerFeet().Z - BuiltFeet.Z) > 30.f;
		const bool bDue = SinceBuildSeconds >= Period && (bMoved || SinceBuildSeconds >= StaticRefreshSeconds);
		const bool bUrgent = bShapeChanged || bRebuildRequested || !bShown;
		if (bPendingBuild && PendingFrame != GFrameCounter)
		{
			// Queued in a frame whose batch never ran (no actor tick after it, e.g. a paused world): build it now.
			bPendingBuild = false;
			RebuildOccluded(PendingSpec);
		}
		if (bPendingBuild)
		{
			PendingSpec = Spec; // already in this frame's batch: the latest spec
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
	BuiltRays.Reset();
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

void UVisionConeComponent::QueueOccludedRebuild(const FVisionConeSightSpec& Spec)
{
	if (VisionConeConsole::CVarParallel.GetValueOnGameThread() <= 0)
	{
		RebuildOccluded(Spec);
		return;
	}
	PendingSpec = Spec;
	PendingFrame = GFrameCounter;
	if (!bPendingBuild)
	{
		bPendingBuild = true;
		VisionConeConsole::Pending.Add(this);
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
	CompleteBatches(World);
}

void UVisionConeComponent::CompleteOnCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	CompleteBatches(World);
}

void UVisionConeComponent::CompleteBatches(UWorld* World)
{
	for (int32 Index = 0; Index < VisionConeConsole::InFlight.Num();)
	{
		const TSharedPtr<VisionConeConsole::FBatch, ESPMode::ThreadSafe> Batch = VisionConeConsole::InFlight[Index];
		UWorld* BatchWorld = Batch->World.Get();
		if (BatchWorld && BatchWorld != World)
		{
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
			Cone->ApplyOccludedBuild(Job.Spec, Job.Context.Feet, Job.Context.Yaw, MoveTemp(Job.Rays), Job.Shape);
		}
		++Stats.Builds;
		Stats.Rays += Job.Samples;
		Stats.Traces += Job.Context.Traces;
		Stats.TaskSeconds += Job.Seconds;
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
	const TSharedPtr<VisionConeConsole::FBatch, ESPMode::ThreadSafe> Batch = MakeShared<VisionConeConsole::FBatch, ESPMode::ThreadSafe>();
	Batch->World = World;
	Batch->Jobs.SetNum(Cones.Num());
	for (int32 Index = 0; Index < Cones.Num(); ++Index)
	{
		UVisionConeComponent* Cone = Cones[Index];
		VisionConeConsole::FJob& Job = Batch->Jobs[Index];
		Job.Cone = Cone;
		Job.Spec = Cone->PendingSpec;
		Job.OutlineWidthCm = Cone->OutlineWidthCm;
		Cone->bPendingBuild = false;
		FVisionConeTraceContext& Context = Job.Context;
		Context.World = World;
		Context.Params = Shared;
		Context.ComponentParams = FCollisionQueryParams(SCENE_QUERY_STAT(VisionConeObstacle), /*bTraceComplex*/ false);
		Context.Feet = Cone->GetOwnerFeet();
		Context.Yaw = Cone->GetOwnerYaw();
		Context.RangeCm = Job.Spec.GetMaxRange();
		Context.HighCm = Job.Spec.GetHighTraceHeight();
		Context.LowCm = Job.Spec.GetLowTraceHeight();
		Context.MaxLowObstacles = Cone->MaxLowObstaclesPerRay;
		Context.DepthProbeCm = Cone->ObstacleDepthProbeCm;
	}
	// Scene queries only (nothing in the world changes): the engine's async traces run the same raycasts on task threads,
	// launched after the actors' tick and waited for before the next one - so does an async batch (CompleteOnTick).
	auto RunAll = [Batch]()
	{
		FGCScopeGuard NoGarbageCollection; // hit results resolve components: no GC while the rays are traced
		ParallelFor(Batch->Jobs.Num(), [&Batch](int32 Index)
		{
			VisionConeConsole::RunJob(Batch->Jobs[Index]);
		}, Batch->Jobs.Num() > 1 ? EParallelForFlags::Unbalanced : EParallelForFlags::ForceSingleThread);
	};
	if (bAsync)
	{
		Batch->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(RunAll));
		Batch->bLaunched = true;
		Batch->GameSeconds = FPlatformTime::Seconds() - Start;
		VisionConeConsole::InFlight.Add(Batch);
		return;
	}
	RunAll();
	ApplyBatch(*Batch);
	VisionConeConsole::Stats.Seconds += FPlatformTime::Seconds() - Start;
}

void UVisionConeComponent::ApplyOccludedBuild(const FVisionConeSightSpec& Spec, const FVector& Feet, float Yaw, TArray<FVisionConeRay>&& Rays,
	const FVisionConeShape& Shape)
{
	BuiltRays = MoveTemp(Rays);
	BuiltSpec = Spec;
	BuiltFeet = Feet;
	BuiltYaw = Yaw;
	BuiltRange = Spec.Levels[0].RangeCm;
	BuiltHalfAngle = Spec.HalfAngleDeg;
	bBuiltOccluded = true;
	bRebuildRequested = false;
	SinceBuildSeconds = 0.f;
	++BuildCount;
	SetWorldRotation(FRotator(0.f, BuiltYaw, 0.f));

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
	}
	SetMaterial(0, FillMaterial);
	SetMaterial(1, LineMaterial);
	SetMaterial(2, LightMaterial);
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
}

void UVisionConeComponent::PlaceAtFeet()
{
	const AActor* Owner = GetOwner();
	const UCapsuleComponent* Capsule = Owner ? Cast<UCapsuleComponent>(Owner->GetRootComponent()) : nullptr;
	const float FeetZ = Capsule && GetAttachParent() == Capsule ? -Capsule->GetUnscaledCapsuleHalfHeight() : 0.f;
	const FVector Wanted(0.f, 0.f, FeetZ + HeightAboveFeetCm);
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
	return VisionConeVisibility::LevelAtPoint(BuiltRays, BuiltHalfAngle, FVector2D(Local.X, Local.Y));
}
