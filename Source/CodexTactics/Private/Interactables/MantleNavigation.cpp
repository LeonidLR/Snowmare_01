// Mantling slice 1 - navigation, the site probe and the world state (docs/design/MANTLING.md 11.4, designer answers 11.6;
// docs/port/MANTLING_SLICE1_REPORT.md). Pattern: Falls/DropLinkSubsystem.cpp (generated smart links, FDropLinkReach, the link callback).
// UE-only (Godot had no mantling).

#include "Interactables/MantleNavigation.h"

#include "AIController.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkReach.h"
#include "Falls/DropLinkSubsystem.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Interactables/VaultNavigation.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace MantleNavInternal
{
	static TAutoConsoleVariable<int32> CVarMantleBatch2(TEXT("Codex.Mantle.Batch2"), 1,
		TEXT("Mantling batch 2 (designer report 2026-11-16, docs/port/MANTLING_BATCH2_REPORT.md): 1 (default) = the wall guard, the clip exit at its ")
		TEXT("settled pose, the walk-off descent over the face nearest the goal, the double click on an obstacle face and the hover ghost; 0 = the ")
		TEXT("behaviour before it (A/B)."));

	static TAutoConsoleVariable<int32> CVarMantleLinks(TEXT("Codex.Mantle.Links"), 1,
		TEXT("Mantling slice 1: 1 (default) = generated path links over the eligible obstacles (an order path over one mantles by itself, Solo's ")
		TEXT("commanded operative only); 0 = none (the click on a ledge top still orders the climb)."));

	/** The face is looked for at this height over the feet (knee height: over a step, under every class), cm. */
	constexpr float MantleFaceTraceHeightCm = 45.f;
	/** The top is measured this far past the face (inside the thinnest fence), cm. */
	constexpr float MantleTopProbeInCm = 4.f;
	/** The ledge scan's step up the face, cm. */
	constexpr float MantleLedgeScanStepCm = 8.f;
	/** The top must carry the capsule this wide across the climb (fraction of its radius each side). */
	constexpr float MantleTopWidthFraction = 0.7f;
	/** The top's depth is followed this far, cm (deeper = deep). */
	constexpr float MantleDepthMaxCm = 400.f;
	/** A top sample counts as the same top within this height, cm. */
	constexpr float MantleDepthStepToleranceCm = 12.f;
	/** The landing footprint test: a short capsule of this half height on the landing (the headroom is traced separately), cm. */
	constexpr float MantleFootprintHalfHeightCm = 44.f;
	/** A path link starts this far in front of the face (between the standing clips' start distances), cm. */
	constexpr float MantleLinkBaseCm = 80.f;
	/** A link start / end is projected onto the navmesh within this box, cm. */
	const FVector MantleLinkProjectExtent(40.f, 40.f, 60.f);
	/** Two links closer than this (start and end) are one, cm. */
	constexpr float MantleLinkDedupCm = 60.f;
	/** Click order: the base point in front of the face, cm. */
	constexpr float MantleClickBaseCm = 80.f;
	/** At most this many links on a map before batch 2 (Codex.Mantle.Batch2 0); batch 2: mantle.json links.max_total. */
	constexpr int32 MantleMaxLinks = 900;

	static bool MantleTraceFirst(const UWorld& World, const FVector& From, const FVector& To, const FCollisionQueryParams& Params, FHitResult& Out)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		TArray<FHitResult> Hits;
		World.LineTraceMultiByObjectType(Hits, From, To, Objects, Params);
		for (const FHitResult& Hit : Hits)
		{
			if (MantleNavigation::IsBlockingHit(Hit))
			{
				Out = Hit;
				return true;
			}
		}
		return false;
	}

	/** The landing's footprint (a short capsule on it) and the free height over it (-1 = open sky within the probe). */
	static void MantleMeasureLanding(const UWorld& World, const FVector& Landing, float Radius, float HalfHeight, const FCollisionQueryParams& Params,
		const FMantleConfig& Config, FMantleProbe& Probe)
	{
		const FVector Centre = Landing + FVector(0.f, 0.f, MantleFootprintHalfHeightCm + 3.f);
		Probe.bLandingFree = !World.OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 2.f), MantleFootprintHalfHeightCm), Params);
		FHitResult Roof;
		const FVector From = Landing + FVector(0.f, 0.f, 5.f);
		const FVector To = Landing + FVector(0.f, 0.f, 2.f * HalfHeight + Config.HeadroomMarginCm + 60.f);
		Probe.FreeHeadroomCm = MantleTraceFirst(World, From, To, Params, Roof) ? static_cast<float>(Roof.ImpactPoint.Z - Landing.Z) : -1.f;
	}

	/** The operative behind a path-following component (a link callback). */
	static AOperativeCharacter* MantleOperativeOf(UObject* PathComp)
	{
		const UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathComp);
		const AController* Controller = Following ? Cast<AController>(Following->GetOwner()) : nullptr;
		return Controller ? Cast<AOperativeCharacter>(Controller->GetPawn()) : nullptr;
	}
}

// --- Areas and filters -------------------------------------------------------------------------------------------------------

UNavArea_Mantle::UNavArea_Mantle()
{
	DefaultCost = 1.f;
	// A climb is taken on a path only when it saves real walking (the way round is preferred up to links.entering_cost_cm longer).
	FixedAreaEnteringCost = MantleRules::MakeDefaultConfig().LinkEnteringCostCm;
	DrawColor = FColor(120, 220, 255);
}

UNavArea_MantleDown::UNavArea_MantleDown()
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 200.f;
	DrawColor = FColor(80, 160, 220);
}

void MantleNavigation::CloseMantleArea(TArray<FNavigationFilterArea>& Areas, bool bAlsoDown)
{
	FNavigationFilterArea& Up = Areas.AddDefaulted_GetRef();
	Up.AreaClass = UNavArea_Mantle::StaticClass();
	Up.bIsExcluded = true;
	if (bAlsoDown)
	{
		FNavigationFilterArea& Down = Areas.AddDefaulted_GetRef();
		Down.AreaClass = UNavArea_MantleDown::StaticClass();
		Down.bIsExcluded = true;
	}
}

UNavFilter_OperativeNoMantle::UNavFilter_OperativeNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false);
}

UNavFilter_NoCrawlNoMantle::UNavFilter_NoCrawlNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false);
}

UNavFilter_TrapLiteralNoMantle::UNavFilter_TrapLiteralNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false);
}

UNavFilter_NoCrawlTrapLiteralNoMantle::UNavFilter_NoCrawlTrapLiteralNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false);
}

TSubclassOf<UNavigationQueryFilter> MantleNavigation::NoMantleVariant(TSubclassOf<UNavigationQueryFilter> Filter)
{
	if (!Filter)
	{
		return UNavFilter_OperativeNoMantle::StaticClass();
	}
	if (Filter == UNavFilter_NoCrawl::StaticClass())
	{
		return UNavFilter_NoCrawlNoMantle::StaticClass();
	}
	if (Filter == UNavFilter_TrapLiteral::StaticClass())
	{
		return UNavFilter_TrapLiteralNoMantle::StaticClass();
	}
	if (Filter == UNavFilter_NoCrawlTrapLiteral::StaticClass())
	{
		return UNavFilter_NoCrawlTrapLiteralNoMantle::StaticClass();
	}
	return Filter;
}

// --- The probe -------------------------------------------------------------------------------------------------------------------

bool MantleNavigation::IsBlockingHit(const FHitResult& Hit)
{
	const UPrimitiveComponent* Component = Hit.GetComponent();
	const AActor* Actor = Hit.GetActor();
	return Component && !(Actor && Actor->IsA<APawn>()) && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
}

bool MantleNavigation::ProbeSite(UWorld& World, const FVector& Feet, const FVector& Dir, float MaxReachCm, float CapsuleRadius, float CapsuleHalfHeight,
	const FCollisionQueryParams& Params, const FMantleConfig& Config, FMantleSite& Out)
{
	using namespace MantleNavInternal;
	Out = FMantleSite();
	Out.Feet = Feet;
	Out.Probe.CapsuleHeightCm = 2.f * CapsuleHalfHeight;
	const FVector Along = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		return false;
	}
	// 1. The face at knee height.
	const FVector FaceFrom = Feet + FVector(0.f, 0.f, MantleFaceTraceHeightCm);
	FHitResult Face;
	if (!MantleTraceFirst(World, FaceFrom, FaceFrom + Along * MaxReachCm, Params, Face) || FMath::Abs(Face.ImpactNormal.Z) > 0.5f)
	{
		return false; // nothing ahead, or a ramp / slope
	}
	FVector Normal = FVector(Face.ImpactNormal.X, Face.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return false;
	}
	Out.Normal = Normal;
	Out.Dir = -Normal; // straight at the face
	Out.FacePoint = Face.ImpactPoint;
	Out.Obstacle = Face.GetActor();
	Out.Probe.FaceDistanceCm = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Feet - Face.ImpactPoint, Normal)));
	bool bMarked = false;
	const AActor* Obstacle = Face.GetActor();
	Out.bVaultable = VaultNavigation::IsVaultable(Obstacle);
	Out.Probe.bEligible = Obstacle && !Out.bVaultable && MantleRules::IsEligibleTags(Obstacle->Tags, bMarked);
	Out.Probe.bMarkedEdge = bMarked;
	Out.bValid = true;
	// 2. The ledge: scan up the face until a trace at that height passes over it (an overhang / a beam above the top never fools the
	//    height); still solid over the traversal ceiling = a wall.
	float FreeAbove = -1.f;
	for (float Height = MantleFaceTraceHeightCm + MantleLedgeScanStepCm; Height <= Config.CeilingCm + MantleLedgeScanStepCm; Height += MantleLedgeScanStepCm)
	{
		const FVector From(Feet.X, Feet.Y, Feet.Z + Height);
		FHitResult Blocked;
		if (!MantleTraceFirst(World, From, From + Out.Dir * (Out.Probe.FaceDistanceCm + MantleTopProbeInCm + 15.f), Params, Blocked))
		{
			FreeAbove = Height;
			break;
		}
	}
	if (FreeAbove < 0.f)
	{
		Out.Probe.HeightCm = Config.CeilingCm + 5.f;
		return true;
	}
	// 3. The top, measured just past the face (inside a 10 cm fence), from just over the ledge down.
	const FVector TopXY = Face.ImpactPoint + Out.Dir * MantleTopProbeInCm;
	FHitResult Top;
	if (!MantleTraceFirst(World, FVector(TopXY.X, TopXY.Y, Feet.Z + FreeAbove + 2.f), FVector(TopXY.X, TopXY.Y, Feet.Z + 5.f), Params, Top)
		|| Top.bStartPenetrating)
	{
		Out.bValid = false;
		return false; // the face was something thin without a top in reach
	}
	Out.TopZ = static_cast<float>(Top.ImpactPoint.Z);
	Out.Probe.HeightCm = Out.TopZ - static_cast<float>(Feet.Z);
	Out.EdgeTop = FVector(Face.ImpactPoint.X, Face.ImpactPoint.Y, Out.TopZ);
	// 4. The top's depth along the climb.
	float Depth = 0.f;
	for (float Step = 5.f; Step <= MantleDepthMaxCm; Step += Step < 60.f ? 5.f : 15.f)
	{
		const FVector Sample = Out.EdgeTop + Out.Dir * Step;
		FHitResult Surface;
		if (!MantleTraceFirst(World, Sample + FVector(0.f, 0.f, 40.f), Sample - FVector(0.f, 0.f, 40.f), Params, Surface)
			|| Surface.bStartPenetrating || FMath::Abs(Surface.ImpactPoint.Z - Out.TopZ) > MantleDepthStepToleranceCm)
		{
			break;
		}
		Depth = Step;
	}
	Out.Probe.DepthCm = Depth;
	const TOptional<EMantleDestination> Destination = MantleRules::DestinationForDepth(Config, Depth);
	Out.Destination = Destination.Get(EMantleDestination::Top);
	// 5. The landing.
	if (Out.Destination == EMantleDestination::Top)
	{
		const float MaxInset = FMath::Max(CapsuleRadius * 0.75f, Depth - CapsuleRadius - 5.f);
		const float Inset = FMath::Clamp(Config.TopInsetCm, CapsuleRadius * 0.75f, MaxInset);
		Out.Landing = Out.EdgeTop + Out.Dir * Inset;
		MantleMeasureLanding(World, Out.Landing, CapsuleRadius, CapsuleHalfHeight, Params, Config, Out.Probe);
		// Wide enough to stand on across the climb (not the 10 cm top of a fence climbed along its length).
		const FVector Across = FVector::CrossProduct(FVector::UpVector, Out.Dir);
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Sample = Out.Landing + Across * (Side * CapsuleRadius * MantleTopWidthFraction);
			FHitResult Surface;
			if (!MantleTraceFirst(World, Sample + FVector(0.f, 0.f, 40.f), Sample - FVector(0.f, 0.f, 40.f), Params, Surface)
				|| FMath::Abs(Surface.ImpactPoint.Z - Out.TopZ) > MantleDepthStepToleranceCm)
			{
				Out.Probe.bLandingFree = false;
			}
		}
	}
	else
	{
		const FVector Far = Face.ImpactPoint + Out.Dir * (Depth + CapsuleRadius + 30.f);
		FHitResult Ground;
		if (MantleTraceFirst(World, FVector(Far.X, Far.Y, Feet.Z + 60.f), FVector(Far.X, Far.Y, Feet.Z - 200.f), Params, Ground) && !Ground.bStartPenetrating)
		{
			Out.Landing = Ground.ImpactPoint;
			Out.Probe.LandingStepCm = static_cast<float>(Ground.ImpactPoint.Z - Feet.Z);
			MantleMeasureLanding(World, Out.Landing, CapsuleRadius, CapsuleHalfHeight, Params, Config, Out.Probe);
		}
		else
		{
			Out.Landing = FVector(Far.X, Far.Y, Feet.Z);
			Out.Probe.bLandingFree = false;
		}
	}
	return true;
}

bool MantleNavigation::PlanTopClick(UWorld& World, const FHitResult& TopHit, const FVector& FromFeet, float CapsuleRadius, FVector& OutBaseFeet, FVector& OutDir)
{
	const AActor* Obstacle = TopHit.GetActor();
	bool bMarked = false;
	if (!Obstacle || TopHit.ImpactNormal.Z < 0.7f || VaultNavigation::IsVaultable(Obstacle) || !MantleRules::IsEligibleTags(Obstacle->Tags, bMarked))
	{
		return false;
	}
	const FBox Local = Obstacle->CalculateComponentsBoundingBoxInLocalSpace(false);
	if (!Local.IsValid)
	{
		return false;
	}
	const FTransform Transform = Obstacle->GetActorTransform();
	const FVector Centre = Local.GetCenter();
	const FVector Extent = Local.GetExtent();
	const FVector From = Transform.InverseTransformPosition(FromFeet) - Centre;
	const FVector Click = Transform.InverseTransformPosition(TopHit.ImpactPoint) - Centre;
	// The face he looks at from where he stands: the largest normalised side offset.
	int32 Axis = 0;
	float Sign = 1.f;
	float Best = -TNumericLimits<float>::Max();
	for (int32 Each = 0; Each < 2; ++Each)
	{
		const float Ext = FMath::Max(1.f, static_cast<float>(Extent[Each]));
		const float Score = FMath::Abs(static_cast<float>(From[Each])) / Ext;
		if (Score > Best)
		{
			Best = Score;
			Axis = Each;
			Sign = From[Each] >= 0.f ? 1.f : -1.f;
		}
	}
	const int32 Tangent = 1 - Axis;
	FVector FaceLocal = FVector::ZeroVector;
	FaceLocal[Axis] = Sign * Extent[Axis];
	// Along the face: under the clicked point, the capsule kept off the face's ends (in world units through the scale).
	const float Scale = FMath::Max(0.01f, static_cast<float>(FMath::Abs(Transform.GetScale3D()[Tangent])));
	const float Margin = FMath::Min(static_cast<float>(Extent[Tangent]), (CapsuleRadius + 5.f) / Scale);
	FaceLocal[Tangent] = FMath::Clamp(static_cast<float>(Click[Tangent]), static_cast<float>(-Extent[Tangent]) + Margin, static_cast<float>(Extent[Tangent]) - Margin);
	FaceLocal.Z = -Extent.Z;
	FVector NormalLocal = FVector::ZeroVector;
	NormalLocal[Axis] = Sign;
	const FVector FaceWorld = Transform.TransformPosition(FaceLocal + Centre);
	const FVector NormalWorld = Transform.TransformVectorNoScale(NormalLocal).GetSafeNormal2D();
	if (NormalWorld.IsNearlyZero())
	{
		return false;
	}
	OutBaseFeet = FVector(FaceWorld.X, FaceWorld.Y, FromFeet.Z) + NormalWorld * MantleNavInternal::MantleClickBaseCm;
	OutDir = -NormalWorld;
	return true;
}

bool MantleNavigation::IsBatch2Enabled()
{
	return MantleNavInternal::CVarMantleBatch2.GetValueOnGameThread() != 0;
}

bool MantleNavigation::TopHitFromFace(UWorld& World, const FHitResult& FaceHit, FHitResult& OutTopHit)
{
	AActor* Obstacle = FaceHit.GetActor();
	bool bMarked = false;
	if (!Obstacle || FMath::Abs(FaceHit.ImpactNormal.Z) >= 0.3f || VaultNavigation::IsVaultable(Obstacle) || !MantleRules::IsEligibleTags(Obstacle->Tags, bMarked))
	{
		return false;
	}
	const FVector Out = FVector(FaceHit.ImpactNormal.X, FaceHit.ImpactNormal.Y, 0.f).GetSafeNormal();
	if (Out.IsNearlyZero())
	{
		return false;
	}
	FVector Origin;
	FVector Extent;
	Obstacle->GetActorBounds(false, Origin, Extent);
	const FVector Inside = FaceHit.ImpactPoint - Out * MantleRules::GetConfig().OrderFaceInsetCm;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MantleFaceToTop), false);
	FHitResult Top;
	if (!World.LineTraceSingleByChannel(Top, FVector(Inside.X, Inside.Y, Origin.Z + Extent.Z + 50.f), FVector(Inside.X, Inside.Y, FaceHit.ImpactPoint.Z),
		ECC_Visibility, Params) || Top.GetActor() != Obstacle || Top.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	OutTopHit = Top;
	return true;
}

// --- The link holder --------------------------------------------------------------------------------------------------------------

AMantleLinkActor::AMantleLinkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

UNavLinkCustomComponent* AMantleLinkActor::AddLink(const FVector& Start, const FVector& End, TSubclassOf<UNavArea> Area)
{
	UNavLinkCustomComponent* Link = NewObject<UNavLinkCustomComponent>(this, NAME_None, RF_Transient);
	Link->SetLinkData(Start, End, ENavLinkDirection::LeftToRight);
	Link->SetEnabledArea(Area);
	Link->SetDisabledArea(UNavArea_Null::StaticClass());
	FNavAgentSelector Agents;
	Agents.Empty();
	Agents.Set(0); // the infantry navmesh only
	Link->SetSupportedAgents(Agents);
	AddInstanceComponent(Link);
	Link->RegisterComponent();
	return Link;
}

// --- The subsystem ----------------------------------------------------------------------------------------------------------------

bool UMantleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UMantleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	MantleRules::ReloadConfig();
	if (MantleNavInternal::CVarMantleLinks.GetValueOnGameThread() <= 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] path links off (Codex.Mantle.Links 0) on %s"), *InWorld.GetMapName());
		return;
	}
	WaitedSeconds = 0.f;
	InWorld.GetTimerManager().SetTimer(PollTimer, FTimerDelegate::CreateUObject(this, &UMantleSubsystem::PollReady), 0.5f, true, 1.5f);
}

void UMantleSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Links.Reset();
	Reservations.Reset();
	LinkActor = nullptr;
	Super::Deinitialize();
}

void UMantleSubsystem::PollReady()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	WaitedSeconds += 0.5f;
	const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	// After the drop links (their reach graph counts in ours: a tall top left by a drop is not a dead end).
	const UDropLinkSubsystem* Drops = World ? World->GetSubsystem<UDropLinkSubsystem>() : nullptr;
	const bool bDropsReady = !Drops || !Drops->IsEnabledForWorld() || Drops->HasGenerated() || WaitedSeconds > 60.f;
	if (!Recast || Nav->IsNavigationBuildInProgress() || !bDropsReady)
	{
		if (WaitedSeconds > 180.f && World)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] the navmesh was not ready after %.0f s - no mantle links"), WaitedSeconds);
			World->GetTimerManager().ClearTimer(PollTimer);
		}
		return;
	}
	World->GetTimerManager().ClearTimer(PollTimer);
	Generate();
}

void UMantleSubsystem::ClearLinks()
{
	if (LinkActor)
	{
		LinkActor->Destroy();
	}
	LinkActor = nullptr;
	Links.Reset();
	bGenerated = false;
}

int32 UMantleSubsystem::Generate()
{
	using namespace MantleNavInternal;
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!Recast)
	{
		return -1;
	}
	const double StartTime = FPlatformTime::Seconds();
	ClearLinks();
	Sampled = 0;
	const FMantleConfig& Config = MantleRules::GetConfig();
	// The operatives' capsule (the class default when none is spawned yet).
	float Radius = 40.f;
	float HalfHeight = 90.f;
	if (const AOperativeCharacter* Default = GetDefault<AOperativeCharacter>(); Default && Default->GetCapsuleComponent())
	{
		Radius = Default->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
		HalfHeight = Default->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MantleLinks), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	TArray<FMantleLink> Candidates;
	int32 Refused[static_cast<int32>(EMantleRefusal::Count)] = {};
	int32 NoNav = 0;
	auto Project = [Nav, Recast](const FVector& Point, float WantZ, FVector& Out)
	{
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(Point, OnNav, MantleLinkProjectExtent, Recast) || FMath::Abs(OnNav.Location.Z - WantZ) > 40.f)
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	};
	auto IsDuplicate = [&Candidates](const FVector& Start, const FVector& End)
	{
		return Candidates.ContainsByPredicate([&Start, &End](const FMantleLink& Link)
		{
			return FVector::Dist(Link.Start, Start) < MantleLinkDedupCm && FVector::Dist(Link.End, End) < MantleLinkDedupCm;
		});
	};
	// Batch 2 item 9: the slice-1 cap of 900 stopped the actor scan on every map bigger than the Depot (L_Gen_Depot_X4 / _1_X6: links over the
	// first ~900 faces only - "at a low fence they run around it").
	const int32 MaxLinks = MantleNavigation::IsBatch2Enabled() ? FMath::Max(1, FMath::RoundToInt(Config.LinkMaxTotal)) : MantleMaxLinks;
	for (TActorIterator<AActor> It(World); It && Candidates.Num() < MaxLinks; ++It)
	{
		AActor* Obstacle = *It;
		bool bMarked = false;
		if (Obstacle->IsA<APawn>() || VaultNavigation::IsVaultable(Obstacle) || !MantleRules::IsEligibleTags(Obstacle->Tags, bMarked))
		{
			continue;
		}
		const FBox Local = Obstacle->CalculateComponentsBoundingBoxInLocalSpace(false);
		if (!Local.IsValid)
		{
			continue;
		}
		const FTransform Transform = Obstacle->GetActorTransform();
		const FVector Centre = Local.GetCenter();
		const FVector Extent = Local.GetExtent();
		const FVector Scale = Transform.GetScale3D().GetAbs();
		const float BaseZ = static_cast<float>(Transform.TransformPosition(FVector(Centre.X, Centre.Y, Centre.Z - Extent.Z)).Z);
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const int32 Tangent = 1 - Axis;
			const float FaceLength = static_cast<float>(2.0 * Extent[Tangent] * Scale[Tangent]);
			const TArray<float> Offsets = MantleRules::LinkOffsets(Config, FaceLength, Radius + 5.f);
			for (const float Sign : { 1.f, -1.f })
			{
				FVector NormalLocal = FVector::ZeroVector;
				NormalLocal[Axis] = Sign;
				const FVector Normal = Transform.TransformVectorNoScale(NormalLocal).GetSafeNormal2D();
				FVector TangentLocal = FVector::ZeroVector;
				TangentLocal[Tangent] = 1.f;
				const FVector Along = Transform.TransformVectorNoScale(TangentLocal).GetSafeNormal2D();
				FVector FaceLocal = Centre;
				FaceLocal[Axis] += Sign * Extent[Axis];
				FVector FaceMid = Transform.TransformPosition(FaceLocal);
				FaceMid.Z = BaseZ;
				for (const float Offset : Offsets)
				{
					++Sampled;
					const FVector BaseGuess = FaceMid + Along * Offset + Normal * MantleLinkBaseCm;
					FVector Base;
					if (!Project(BaseGuess + FVector(0.f, 0.f, 30.f), BaseZ, Base))
					{
						++NoNav;
						continue;
					}
					// Measured from the real floor under the link start (the navmesh lies a few cm above it: a 135 +- 5 fence would read short).
					FVector ProbeFeet = Base;
					FHitResult Floor;
					if (MantleTraceFirst(*World, Base + FVector(0.f, 0.f, 50.f), Base - FVector(0.f, 0.f, 100.f), Params, Floor) && !Floor.bStartPenetrating)
					{
						ProbeFeet.Z = Floor.ImpactPoint.Z;
					}
					FMantleSite Site;
					if (!MantleNavigation::ProbeSite(*World, ProbeFeet, -Normal, MantleLinkBaseCm + 80.f, Radius, HalfHeight, Params, Config, Site)
						|| Site.Obstacle.Get() != Obstacle)
					{
						++Refused[static_cast<int32>(EMantleRefusal::NoObstacle)];
						continue;
					}
					const FMantleDecision Decision = MantleRules::Decide(Config, Site.Probe);
					if (!Decision.IsGo())
					{
						++Refused[static_cast<int32>(Decision.Refusal)];
						continue;
					}
					FVector End;
					const float WantZ = static_cast<float>(Site.Landing.Z);
					if (!Project(Site.Landing + FVector(0.f, 0.f, 30.f), WantZ, End))
					{
						++NoNav; // no navmesh on this top / landing (too small for the agent): the click order still climbs it
						continue;
					}
					if (IsDuplicate(Base, End))
					{
						continue;
					}
					FMantleLink Up;
					Up.Start = Base;
					Up.End = End;
					Up.Dir = Site.Dir;
					Up.HeightCm = Site.Probe.HeightCm;
					Up.ClassId = Config.Classes[Decision.ClassIndex].Id;
					Up.Obstacle = Obstacle;
					Candidates.Add(Up);
					if (Site.Destination == EMantleDestination::Top && MantleRules::NeedsDownLink(Config, Site.Probe.HeightCm))
					{
						FMantleLink Down = Up;
						Down.Start = End;
						Down.End = Base;
						Down.Dir = -Site.Dir;
						Down.bDown = true;
						Candidates.Add(Down);
					}
				}
			}
		}
	}
	bLinkCapReached = Candidates.Num() >= MaxLinks;
	if (bLinkCapReached)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: the link cap (%d) is reached - the obstacles after it get no mantle links (mantle.json links.max_total)"),
			*World->GetMapName(), MaxLinks);
	}
	// No climb onto a top that has no way back to the main area (FDropLinkReach over the navmesh, its ladders / drop links and the kept links).
	int32 DeadEnds = 0;
	FDropLinkReach Reach;
	if (!Candidates.IsEmpty() && Reach.Build(*World))
	{
		TArray<FReachGraph::FCandidate> Graph;
		for (const FMantleLink& Link : Candidates)
		{
			Graph.Add({ Reach.NodeOf(Reach.FindPoly(*World, Link.Start)), Reach.NodeOf(Reach.FindPoly(*World, Link.End)) });
		}
		const TArray<bool> Kept = Reach.GetGraph().SelectLinks(Graph);
		for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
		{
			if (!Kept[Index])
			{
				Candidates.RemoveAt(Index);
				++DeadEnds;
			}
		}
	}
	Links = MoveTemp(Candidates);
	if (!Links.IsEmpty())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = TEXT("MantleLinks");
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		LinkActor = World->SpawnActor<AMantleLinkActor>(AMantleLinkActor::StaticClass(), FTransform::Identity, SpawnParams);
		for (FMantleLink& Link : Links)
		{
			UNavLinkCustomComponent* Component = LinkActor ? LinkActor->AddLink(Link.Start, Link.End,
				Link.bDown ? UNavArea_MantleDown::StaticClass() : UNavArea_Mantle::StaticClass()) : nullptr;
			if (Component)
			{
				Component->SetMoveReachedLink(this, &UMantleSubsystem::HandleLinkReached);
				Link.Link = Component;
				Link.LinkId = Component->GetId();
			}
		}
	}
	bGenerated = true;
	GenerationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	int32 Down = 0;
	for (const FMantleLink& Link : Links)
	{
		Down += Link.bDown ? 1 : 0;
	}
	TMap<FName, int32> PerClass;
	for (const FMantleLink& Link : Links)
	{
		if (!Link.bDown)
		{
			++PerClass.FindOrAdd(Link.ClassId);
		}
	}
	FString Classes;
	for (const TPair<FName, int32>& Pair : PerClass)
	{
		Classes += FString::Printf(TEXT("%s%s %d"), Classes.IsEmpty() ? TEXT("") : TEXT(", "), *Pair.Key.ToString(), Pair.Value);
	}
	FString Reasons;
	for (int32 Index = 1; Index < static_cast<int32>(EMantleRefusal::Count); ++Index)
	{
		if (Refused[Index] > 0)
		{
			Reasons += FString::Printf(TEXT("%s%s %d"), Reasons.IsEmpty() ? TEXT("") : TEXT(", "), MantleRules::RefusalName(static_cast<EMantleRefusal>(Index)), Refused[Index]);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: %d mantle links (%d up / over [%s], %d down) from %d samples in %.0f ms - dead ends %d, no navmesh %d; refused: %s"),
		*World->GetMapName(), Links.Num(), Links.Num() - Down, *Classes, Down, Sampled, GenerationMs, DeadEnds, NoNav, Reasons.IsEmpty() ? TEXT("none") : *Reasons);
	return Links.Num();
}

int32 UMantleSubsystem::FindLinkByComponent(const UNavLinkCustomComponent* Component) const
{
	return Component ? Links.IndexOfByPredicate([Component](const FMantleLink& Link) { return Link.Link.Get() == Component; }) : INDEX_NONE;
}

int32 UMantleSubsystem::FindLinkById(FNavLinkId LinkId) const
{
	return LinkId.IsValid() ? Links.IndexOfByPredicate([LinkId](const FMantleLink& Link) { return Link.LinkId == LinkId; }) : INDEX_NONE;
}

void UMantleSubsystem::FindLinksOnPath(const FNavigationPath& Path, TArray<int32>& OutLinks) const
{
	OutLinks.Reset();
	if (Links.IsEmpty())
	{
		return;
	}
	for (const FNavPathPoint& Point : Path.GetPathPoints())
	{
		if (const int32 Index = FindLinkById(Point.CustomNavLinkId); Index != INDEX_NONE)
		{
			OutLinks.AddUnique(Index);
		}
	}
	const FNavMeshPath* NavPath = Path.CastPath<FNavMeshPath>();
	const ARecastNavMesh* Recast = NavPath ? Cast<ARecastNavMesh>(NavPath->GetNavigationDataUsed()) : nullptr;
	if (!Recast)
	{
		return;
	}
	for (const NavNodeRef Ref : NavPath->PathCorridor)
	{
		if (const int32 Index = FindLinkById(Recast->GetNavLinkUserId(Ref)); Index != INDEX_NONE)
		{
			OutLinks.AddUnique(Index);
		}
	}
}

void UMantleSubsystem::DisableLink(int32 Index, const TCHAR* Why)
{
	if (!Links.IsValidIndex(Index))
	{
		return;
	}
	if (UNavLinkCustomComponent* Component = Links[Index].Link.Get(); Component && Component->IsEnabled())
	{
		Component->SetEnabled(false);
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] link %d (%s -> %s) switched off: %s"), Index, *Links[Index].Start.ToCompactString(),
			*Links[Index].End.ToCompactString(), Why);
	}
}

void UMantleSubsystem::HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint)
{
	const int32 Index = FindLinkByComponent(ThisComp);
	AOperativeCharacter* Operative = MantleNavInternal::MantleOperativeOf(PathComp);
	if (Operative && Index != INDEX_NONE && Operative->NotifyMantleLinkReached(Index))
	{
		return; // he owns the move now (the climb / hop starts on his next tick)
	}
	const UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathComp);
	AController* Controller = Following ? Cast<AController>(Following->GetOwner()) : nullptr;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s reached mantle link %d and cannot climb - path stopped"), *GetNameSafe(Controller ? Controller->GetPawn() : nullptr), Index);
	if (Controller)
	{
		Controller->StopMovement();
	}
}

bool UMantleSubsystem::IsReservedByOther(const FVector& Landing, const AActor* Who) const
{
	const float Radius = MantleRules::GetConfig().ReserveRadiusCm;
	return Reservations.ContainsByPredicate([&Landing, Who, Radius](const FReservation& Each)
	{
		return Each.Who.IsValid() && Each.Who.Get() != Who && FVector::Dist(Each.Landing, Landing) < Radius;
	});
}

bool UMantleSubsystem::TryReserve(AActor* Who, const FVector& Landing)
{
	Reservations.RemoveAll([](const FReservation& Each) { return !Each.Who.IsValid(); });
	if (!Who || IsReservedByOther(Landing, Who))
	{
		return false;
	}
	Release(Who);
	Reservations.Add({ Who, Landing });
	return true;
}

void UMantleSubsystem::Release(const AActor* Who)
{
	Reservations.RemoveAll([Who](const FReservation& Each) { return !Each.Who.IsValid() || Each.Who.Get() == Who; });
}

UAnimSequenceBase* UMantleSubsystem::GetClip(int32 ClipIndex)
{
	const FMantleConfig& Config = MantleRules::GetConfig();
	if (!Config.Clips.IsValidIndex(ClipIndex) || MissingClips.Contains(ClipIndex))
	{
		return nullptr;
	}
	if (const TObjectPtr<UAnimSequenceBase>* Cached = ClipCache.Find(ClipIndex))
	{
		return *Cached;
	}
	UAnimSequenceBase* Clip = LoadObject<UAnimSequenceBase>(nullptr, *Config.Clips[ClipIndex].Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Clip)
	{
		MissingClips.Add(ClipIndex);
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] clip missing: %s"), *Config.Clips[ClipIndex].Path);
		return nullptr;
	}
	ClipCache.Add(ClipIndex, Clip);
	return Clip;
}

namespace MantleNavInternal
{
	static FAutoConsoleCommandWithWorldAndArgs MantleRegenerateCommand(
		TEXT("CodexTactics.MantleLinks"),
		TEXT("Regenerates the mantle path links of this map now and logs the count; 'clear' removes them."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UMantleSubsystem* Mantle = World ? World->GetSubsystem<UMantleSubsystem>() : nullptr;
			if (!Mantle)
			{
				return;
			}
			if (Args.Num() > 0 && Args[0] == TEXT("clear"))
			{
				Mantle->ClearLinks();
				return;
			}
			Mantle->Generate();
		}));
}
