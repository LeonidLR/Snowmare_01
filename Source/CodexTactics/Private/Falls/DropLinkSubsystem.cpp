// UDropLinkSubsystem: generated one-way drop nav links off navmesh edges (game designer items 6 / 10 / 11a, 2026-11-03).
// UE-only system (no Godot reference).

#include "Falls/DropLinkSubsystem.h"

#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Components/SceneComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkNavigation.h"
#include "Falls/LedgeFallComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NavAreas/NavArea_Null.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace DropLinkGenInternal
{
	static TAutoConsoleVariable<int32> CVarFallDropLinks(
		TEXT("Codex.Fall.DropLinks"),
		1,
		TEXT("Generated drop nav links (2026-11-03, docs/systems/stances-and-movement.md 'Falls and drops'): 0 none; 1 (default) on maps whose level JSON says ")
		TEXT("\"drop_links\": true (the level generator's maps); 2 on every map. Read when the level starts (CodexTactics.DropLinks regenerates)."),
		ECVF_Default);

	/** The operatives' capsule for the flight arc check (radius / half height, a little slimmer than the body). */
	constexpr float ArcRadiusCm = 33.f;
	constexpr float ArcHalfHeightCm = 80.f;
	constexpr float BodyHalfHeightCm = 88.f;

	void IgnoreAllPawns(UWorld& World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<APawn> It(&World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
	}
}

// --- The link holder -------------------------------------------------------------------------------------------------------

ADropLinkActor::ADropLinkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

UNavLinkCustomComponent* ADropLinkActor::AddLink(const FVector& Start, const FVector& End, TSubclassOf<UNavArea> Area)
{
	UNavLinkCustomComponent* Link = NewObject<UNavLinkCustomComponent>(this, NAME_None, RF_Transient);
	Link->SetLinkData(Start, End, ENavLinkDirection::LeftToRight);
	Link->SetEnabledArea(Area);
	Link->SetDisabledArea(UNavArea_Null::StaticClass());
	// The infantry navmesh only (agent 0 "Default"; the APC's navmesh never gets a drop).
	FNavAgentSelector Agents;
	Agents.Empty();
	Agents.Set(0);
	Link->SetSupportedAgents(Agents);
	AddInstanceComponent(Link);
	Link->RegisterComponent();
	return Link;
}

// --- The subsystem ---------------------------------------------------------------------------------------------------------

bool UDropLinkSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

bool UDropLinkSubsystem::IsEnabledForWorld() const
{
	const int32 Mode = DropLinkGenInternal::CVarFallDropLinks.GetValueOnGameThread();
	if (Mode <= 0)
	{
		return false;
	}
	if (Mode >= 2)
	{
		return true;
	}
	UWorld* World = GetWorld();
	ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->PeekLevelConfig() : nullptr;
	return Level && Level->Config.bDropLinks;
}

void UDropLinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FallRules::ReloadConfig();
	if (!IsEnabledForWorld())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[DropLinks] off on %s (Codex.Fall.DropLinks %d, level JSON \"drop_links\" not set)"), *InWorld.GetMapName(),
			DropLinkGenInternal::CVarFallDropLinks.GetValueOnGameThread());
		return;
	}
	WaitedSeconds = 0.f;
	InWorld.GetTimerManager().SetTimer(PollTimer, FTimerDelegate::CreateUObject(this, &UDropLinkSubsystem::PollNavigationReady), 0.5f, true, 1.f);
}

void UDropLinkSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Links.Reset();
	LinkActor = nullptr;
	Super::Deinitialize();
}

void UDropLinkSubsystem::PollNavigationReady()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	WaitedSeconds += 0.5f;
	const ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!Recast || Nav->IsNavigationBuildInProgress())
	{
		if (WaitedSeconds > 180.f)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[DropLinks] the navmesh was not ready after %.0f s - no drop links"), WaitedSeconds);
			World->GetTimerManager().ClearTimer(PollTimer);
		}
		return;
	}
	World->GetTimerManager().ClearTimer(PollTimer);
	Generate();
}

void UDropLinkSubsystem::DisableLink(int32 Index, const TCHAR* Why)
{
	if (!Links.IsValidIndex(Index))
	{
		return;
	}
	if (UNavLinkCustomComponent* Component = Links[Index].Link.Get(); Component && Component->IsEnabled())
	{
		Component->SetEnabled(false); // the disabled area is Null: nothing paths over it
		UE_LOG(LogCodexTactics, Warning, TEXT("[DropLinks] link %d (%s -> %s) switched off: %s"), Index, *Links[Index].Start.ToCompactString(),
			*Links[Index].End.ToCompactString(), Why);
	}
}

void UDropLinkSubsystem::ClearLinks()
{
	if (LinkActor)
	{
		LinkActor->Destroy();
	}
	LinkActor = nullptr;
	Links.Reset();
	bGenerated = false;
}

int32 UDropLinkSubsystem::Generate()
{
	using namespace FallRules;
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	ARecastNavMesh* Recast = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!Recast)
	{
		return -1;
	}
	const double StartTime = FPlatformTime::Seconds();
	ClearLinks();
	const FFallConfig& Config = GetConfig();
	FRecastDebugGeometry Geometry;
	Geometry.bGatherNavMeshEdges = true;
	Recast->GetDebugGeometryForTile(Geometry, FNavTileRef());
	const TArray<FVector>& Edges = Geometry.NavMeshEdges;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(DropLinkGen), false);
	DropLinkGenInternal::IgnoreAllPawns(*World, Params);
	TArray<FVector> KeptStarts;
	TArray<float> KeptLandZ;
	int32 Verdicts[static_cast<int32>(EProfileVerdict::Count)] = {};
	Sampled = 0;
	const int32 MaxLinks = FMath::Max(1, FMath::RoundToInt(Config.MaxLinks));
	const float Spacing = FMath::Max(30.f, Config.LinkSpacingCm);
	bool bCapped = false;
	for (int32 Index = 0; Index + 1 < Edges.Num() && !bCapped; Index += 2)
	{
		const FVector A = Edges[Index];
		const FVector B = Edges[Index + 1];
		const FVector Flat(B.X - A.X, B.Y - A.Y, 0.f);
		const float Length = static_cast<float>(Flat.Size());
		if (Length < 10.f)
		{
			continue;
		}
		const FVector Normal = FVector(-Flat.Y, Flat.X, 0.f) / Length;
		for (const float Fraction : EdgeSampleFractions(Length, Spacing))
		{
			const FVector Point = FMath::Lerp(A, B, Fraction);
			// One link per spacing: skip a point next to a kept start before the (dear) probes.
			bool bNearKept = false;
			for (const FVector& Kept : KeptStarts)
			{
				bNearKept |= FMath::Abs(Kept.Z - Point.Z) < 100.f && FVector::DistSquared2D(Kept, Point) < FMath::Square(0.8f * Spacing);
			}
			if (bNearKept)
			{
				continue;
			}
			++Sampled;
			FDropLink Link;
			EProfileVerdict Verdict = EProfileVerdict::NoEdge;
			if (BuildLinkAt(Point, Normal, Config, Params, Link, Verdict) && !IsDuplicateStart(Link.Start, static_cast<float>(Link.End.Z), KeptStarts, KeptLandZ, Spacing))
			{
				KeptStarts.Add(Link.Start);
				KeptLandZ.Add(static_cast<float>(Link.End.Z));
				Links.Add(Link);
				if (Links.Num() >= MaxLinks)
				{
					bCapped = true;
					break;
				}
			}
			++Verdicts[FMath::Clamp(static_cast<int32>(Verdict), 0, static_cast<int32>(EProfileVerdict::Count) - 1)];
		}
	}

	if (!Links.IsEmpty())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = TEXT("DropLinks");
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		LinkActor = World->SpawnActor<ADropLinkActor>(ADropLinkActor::StaticClass(), FTransform::Identity, SpawnParams);
		for (FDropLink& Link : Links)
		{
			UNavLinkCustomComponent* Component = LinkActor ? LinkActor->AddLink(Link.Start, Link.End,
				Link.bHurt ? UNavArea_DropHurt::StaticClass() : UNavArea_Drop::StaticClass()) : nullptr;
			if (Component)
			{
				Component->SetMoveReachedLink(this, &UDropLinkSubsystem::HandleLinkReached);
				Link.Link = Component;
				Link.LinkId = Component->GetId();
			}
		}
	}
	bGenerated = true;
	GenerationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	int32 Hurt = 0;
	for (const FDropLink& Link : Links)
	{
		Hurt += Link.bHurt ? 1 : 0;
	}
	UE_LOG(LogCodexTactics, Display,
		TEXT("[DropLinks] %s: %d drop links (%d hurt) from %d edge segments, %d points probed in %.0f ms%s - rejected: no edge %d, blocked %d, low step %d, gap %d, ")
		TEXT("no lower surface %d, too shallow %d, unreachable %d, no landing %d, arc blocked %d"),
		*World->GetMapName(), Links.Num(), Hurt, Edges.Num() / 2, Sampled, GenerationMs, bCapped ? TEXT(" (CAPPED at link.max_links)") : TEXT(""),
		Verdicts[static_cast<int32>(EProfileVerdict::NoEdge)], Verdicts[static_cast<int32>(EProfileVerdict::Blocked)], Verdicts[static_cast<int32>(EProfileVerdict::LowStep)],
		Verdicts[static_cast<int32>(EProfileVerdict::GapTooWide)], Verdicts[static_cast<int32>(EProfileVerdict::NoLowerSurface)],
		Verdicts[static_cast<int32>(EProfileVerdict::TooShallow)], Verdicts[static_cast<int32>(EProfileVerdict::Unreachable)],
		Verdicts[static_cast<int32>(EProfileVerdict::NoLanding)], Verdicts[static_cast<int32>(EProfileVerdict::ArcBlocked)]);
	return Links.Num();
}

bool UDropLinkSubsystem::BuildLinkAt(const FVector& EdgePoint, const FVector& EdgeNormal, const FallRules::FFallConfig& Config, const FCollisionQueryParams& Params,
	FDropLink& OutLink, FallRules::EProfileVerdict& OutVerdict) const
{
	using namespace FallRules;
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	OutVerdict = EProfileVerdict::NoEdge;
	if (!NavData)
	{
		return false;
	}
	// The floor under the boundary point.
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, EdgePoint + FVector(0.f, 0.f, 60.f), EdgePoint - FVector(0.f, 0.f, 120.f), ECC_Pawn, Params))
	{
		return false;
	}
	const float FloorZ = static_cast<float>(Hit.ImpactPoint.Z);
	const FVector Base(EdgePoint.X, EdgePoint.Y, FloorZ);
	// Outward = the side with no navmesh next to the boundary.
	auto OnMeshAt = [Nav, NavData](const FVector& Query)
	{
		FNavLocation Found;
		return Nav->ProjectPointToNavigation(Query, Found, FVector(10.f, 10.f, 50.f), NavData) && FVector::Dist2D(Found.Location, Query) < 12.f
			&& FMath::Abs(Found.Location.Z - Query.Z) < 50.f;
	};
	FVector Dir = EdgeNormal;
	const bool bAhead = OnMeshAt(Base + EdgeNormal * 25.f);
	const bool bBehind = OnMeshAt(Base - EdgeNormal * 25.f);
	if (bAhead && bBehind)
	{
		return false;
	}
	if (bAhead)
	{
		Dir = -EdgeNormal;
	}
	// A parapet / railing / wall at the edge: no link (a knee-high curb too - he cannot walk over it).
	for (const float Height : { 45.f, 110.f })
	{
		FHitResult Wall;
		const FVector From = Base + FVector(0.f, 0.f, Height);
		if (World->SweepSingleByChannel(Wall, From, From + Dir * Config.EdgeSearchCm, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(20.f), Params))
		{
			OutVerdict = EProfileVerdict::Blocked;
			return false;
		}
	}
	// The profile beyond the boundary: the edge, the gap, the lower surface.
	TArray<FProfileSample> Samples;
	const float Step = FMath::Max(5.f, Config.ProbeStepCm);
	const float MaxDist = Config.EdgeSearchCm + Config.LinkMaxGapCm + 450.f;
	float EdgeAt = -1.f;
	float NearAt = -1.f;
	float NearDrop = 0.f;
	for (float D = Step; D <= MaxDist; D += Step)
	{
		const FVector At = Base + Dir * D;
		FProfileSample Sample;
		Sample.DistCm = D;
		FHitResult Down;
		Sample.bHit = World->LineTraceSingleByChannel(Down, At + FVector(0.f, 0.f, 150.f), At - FVector(0.f, 0.f, Config.LinkMaxDropCm + 80.f), ECC_Pawn, Params);
		Sample.DropCm = Sample.bHit ? FloorZ - static_cast<float>(Down.ImpactPoint.Z) : 0.f;
		Samples.Add(Sample);
		// Stop as soon as the verdict cannot change (AnalyseProfile decides it).
		const bool bSameFloor = Sample.bHit && FMath::Abs(Sample.DropCm) <= Config.StepToleranceCm;
		if (EdgeAt < 0.f)
		{
			if (!bSameFloor)
			{
				EdgeAt = D;
			}
			else if (D > Config.EdgeSearchCm)
			{
				break;
			}
		}
		if (EdgeAt >= 0.f && NearAt < 0.f)
		{
			if (Sample.bHit && Sample.DropCm >= Config.LinkMinDropCm && Sample.DropCm <= Config.LinkMaxDropCm)
			{
				NearAt = D;
				NearDrop = Sample.DropCm;
			}
			else if ((Sample.bHit && Sample.DropCm < Config.LinkMinDropCm && !bSameFloor) || D - EdgeAt > Config.LinkMaxGapCm + Step)
			{
				break;
			}
		}
		else if (NearAt >= 0.f && (!Sample.bHit || FMath::Abs(Sample.DropCm - NearDrop) > Config.StepToleranceCm || D - NearAt > 450.f))
		{
			FDropProfile Current;
			Current.EdgeCm = EdgeAt;
			Current.DropCm = NearDrop;
			if (D - NearAt <= 450.f && IsHigherLanding(Sample, Current, Config))
			{
				NearAt = D; // the lower roof beyond the gap's ground (AnalyseProfile takes it the same way)
				NearDrop = Sample.DropCm;
				continue;
			}
			break;
		}
	}
	const FDropProfile Profile = AnalyseProfile(Samples, Config);
	OutVerdict = Profile.Verdict;
	if (!Profile.IsOk())
	{
		return false;
	}
	// A run-up at the reference speed must reach the lower surface.
	const float Gravity = FMath::Abs(World->GetGravityZ());
	const FTakeoffPlan Plan = PlanTakeoff(0.f, Config.ReferenceSpeedCms, Profile.EdgeCm, Profile.NearCm, Profile.FarCm, Profile.DropCm, Gravity, Config);
	if (!Plan.bReaches)
	{
		OutVerdict = EProfileVerdict::Unreachable;
		return false;
	}
	// The landing on the lower surface's navmesh.
	const float LowerZ = FloorZ - Profile.DropCm;
	FNavLocation Landing;
	bool bLanding = false;
	for (const float Extra : { 0.f, 40.f, 80.f })
	{
		const float LandAt = Plan.LandingCm + Extra;
		if (Profile.FarCm > 0.f && LandAt > Profile.FarCm - Config.LandingMarginCm * 0.5f)
		{
			break;
		}
		const FVector Query = Base + Dir * LandAt + FVector(0.f, 0.f, LowerZ - FloorZ);
		if (Nav->ProjectPointToNavigation(Query, Landing, FVector(40.f, 40.f, 80.f), NavData) && FMath::Abs(Landing.Location.Z - LowerZ) < 60.f)
		{
			bLanding = true;
			break;
		}
	}
	if (!bLanding)
	{
		OutVerdict = EProfileVerdict::NoLanding;
		return false;
	}
	// The flight arc is clear (a slim capsule along the run-up to the edge and the parabola down to just above the landing).
	{
		using namespace DropLinkGenInternal;
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(ArcRadiusCm, ArcHalfHeightCm);
		TArray<FVector> Arc;
		const float Lift = BodyHalfHeightCm + 5.f;
		Arc.Add(Base + FVector(0.f, 0.f, Lift));
		Arc.Add(Base + Dir * Profile.EdgeCm + FVector(0.f, 0.f, Lift));
		for (int32 K = 1; K <= 6; ++K)
		{
			const float Time = Plan.Seconds * K / 6.f;
			const float Z = FMath::Max(Lift - 0.5f * Gravity * Time * Time, -Profile.DropCm + Lift + 12.f);
			Arc.Add(Base + Dir * (Profile.EdgeCm + Plan.SpeedCms * Time) + FVector(0.f, 0.f, Z));
		}
		for (int32 K = 1; K < Arc.Num(); ++K)
		{
			FHitResult Block;
			if (World->SweepSingleByChannel(Block, Arc[K - 1], Arc[K], FQuat::Identity, ECC_Pawn, Capsule, Params))
			{
				OutVerdict = EProfileVerdict::ArcBlocked;
				return false;
			}
		}
	}
	OutLink.Start = FVector(EdgePoint.X, EdgePoint.Y, FMath::Max(static_cast<float>(EdgePoint.Z), FloorZ));
	OutLink.End = Landing.Location;
	OutLink.Dir = Dir;
	OutLink.EdgeCm = Profile.EdgeCm;
	OutLink.NearCm = Profile.NearCm;
	OutLink.FarCm = Profile.FarCm;
	OutLink.DropCm = Profile.DropCm;
	OutLink.bHurt = IsHurtingDrop(Profile.DropCm, Config);
	OutVerdict = EProfileVerdict::Ok;
	return true;
}

// --- Queries -----------------------------------------------------------------------------------------------------------------

int32 UDropLinkSubsystem::FindLinkById(FNavLinkId LinkId) const
{
	if (!LinkId.IsValid())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		if (Links[Index].LinkId == LinkId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UDropLinkSubsystem::FindLinkByComponent(const UNavLinkCustomComponent* Component) const
{
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		if (Component && Links[Index].Link.Get() == Component)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UDropLinkSubsystem::FindLinkStartNear(const FVector& Feet, float RadiusCm) const
{
	int32 Best = INDEX_NONE;
	float BestDist = FMath::Square(RadiusCm);
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		const float Dist = static_cast<float>(FVector::DistSquared2D(Links[Index].Start, Feet));
		if (Dist <= BestDist && FMath::Abs(Links[Index].Start.Z - Feet.Z) <= 60.f)
		{
			Best = Index;
			BestDist = Dist;
		}
	}
	return Best;
}

void UDropLinkSubsystem::FindLinksOnPath(const FNavigationPath& Path, TArray<int32>& OutLinks) const
{
	OutLinks.Reset();
	if (Links.IsEmpty())
	{
		return;
	}
	for (const FNavPathPoint& Point : Path.GetPathPoints())
	{
		const int32 Index = FindLinkById(Point.CustomNavLinkId);
		if (Index != INDEX_NONE)
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
		const int32 Index = FindLinkById(Recast->GetNavLinkUserId(Ref));
		if (Index != INDEX_NONE)
		{
			OutLinks.AddUnique(Index);
		}
	}
}

void UDropLinkSubsystem::GatherSquadMarkers(TArray<FDropMarker>& OutMarkers) const
{
	OutMarkers.Reset();
	const UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad || Links.IsEmpty())
	{
		return;
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	TArray<int32> OnPath;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		const ULedgeFallComponent* Fall = Member ? Member->LedgeFall.Get() : nullptr;
		if (!Fall || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
		{
			continue;
		}
		OnPath.Reset();
		const bool bActive = Fall->GetActiveLink() != INDEX_NONE && Fall->GetKind() == ELedgeFallKind::DropLink;
		if (bActive)
		{
			OnPath.Add(Fall->GetActiveLink());
		}
		else if (const AAIController* Controller = Cast<AAIController>(Member->GetController()))
		{
			const UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
			if (Following && Following->GetStatus() == EPathFollowingStatus::Moving && Following->GetPath().IsValid() && Following->GetPath()->IsValid())
			{
				FindLinksOnPath(*Following->GetPath(), OnPath);
			}
		}
		for (const int32 Index : OnPath)
		{
			const FDropLink* Link = GetLink(Index);
			if (!Link || OutMarkers.ContainsByPredicate([Index](const FDropMarker& Marker) { return Marker.LinkIndex == Index; }))
			{
				continue;
			}
			const FallRules::FTakeoffPlan Plan = bActive ? FallRules::FTakeoffPlan{ Fall->GetPlannedSpeed(), 0.f, 0.f, Fall->WasPlannedToReach(), !Fall->WasPlannedToReach() }
				: Fall->PredictDrop(*Link);
			const float Damage = FallRules::FallDamage(Link->DropCm, Config);
			FDropMarker& Marker = OutMarkers.AddDefaulted_GetRef();
			Marker.LinkIndex = Index;
			Marker.Who = Member;
			Marker.Start = Link->Start;
			Marker.Edge = Link->Start + Link->Dir * Link->EdgeCm;
			Marker.End = Link->End;
			Marker.Label = FallRules::DropMarkerLabel(Link->DropCm, Damage, Plan.bTooSlow);
			Marker.bDanger = Damage > 0.f || Plan.bTooSlow;
			Marker.bActive = bActive;
		}
	}
}

// --- One at a time -----------------------------------------------------------------------------------------------------------

bool UDropLinkSubsystem::IsLeasedByOther(int32 Index, const AActor* Who) const
{
	if (!Links.IsValidIndex(Index))
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float Radius = FallRules::GetConfig().LeaseRadiusCm;
	for (const FDropLink& Other : Links)
	{
		const AActor* Holder = Other.LeaseHolder.Get();
		if (!Holder || Holder == Who || (Other.LeaseUntil > 0.0 && Other.LeaseUntil < Now))
		{
			continue;
		}
		if (FallRules::LandingsConflict(Other.End, Links[Index].End, Radius))
		{
			return true;
		}
	}
	return false;
}

bool UDropLinkSubsystem::TryLease(int32 Index, AActor* Who)
{
	if (!Links.IsValidIndex(Index) || IsLeasedByOther(Index, Who))
	{
		return false;
	}
	Links[Index].LeaseHolder = Who;
	Links[Index].LeaseUntil = 0.0;
	return true;
}

void UDropLinkSubsystem::ReleaseLease(int32 Index, const AActor* Who, float HoldSeconds)
{
	if (!Links.IsValidIndex(Index) || Links[Index].LeaseHolder.Get() != Who)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (HoldSeconds > 0.f && World)
	{
		Links[Index].LeaseUntil = World->GetTimeSeconds() + HoldSeconds;
		return;
	}
	Links[Index].LeaseHolder = nullptr;
	Links[Index].LeaseUntil = 0.0;
}

void UDropLinkSubsystem::HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint)
{
	const int32 Index = FindLinkByComponent(ThisComp);
	UPathFollowingComponent* Following = Cast<UPathFollowingComponent>(PathComp);
	AController* Controller = Following ? Cast<AController>(Following->GetOwner()) : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	ULedgeFallComponent* Fall = Pawn ? Pawn->FindComponentByClass<ULedgeFallComponent>() : nullptr;
	if (Fall && Index != INDEX_NONE && Fall->NotifyDropLinkReached(Index))
	{
		return; // the body's fall component owns the move now (it stops the path and resumes it at the landing)
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[DropLinks] %s reached drop link %d without a fall component - path stopped"), *GetNameSafe(Pawn), Index);
	if (Controller)
	{
		Controller->StopMovement();
	}
}

// --- Console ---------------------------------------------------------------------------------------------------------------

namespace DropLinkGenInternal
{
	static FAutoConsoleCommandWithWorldAndArgs RegenerateCommand(
		TEXT("CodexTactics.DropLinks"),
		TEXT("Regenerates the drop nav links of this map now (whatever Codex.Fall.DropLinks says) and logs the count; 'clear' removes them."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDropLinkSubsystem* Links = World ? World->GetSubsystem<UDropLinkSubsystem>() : nullptr;
			if (!Links)
			{
				return;
			}
			if (Args.Num() > 0 && Args[0] == TEXT("clear"))
			{
				Links->ClearLinks();
				return;
			}
			Links->Generate();
		}));
}
