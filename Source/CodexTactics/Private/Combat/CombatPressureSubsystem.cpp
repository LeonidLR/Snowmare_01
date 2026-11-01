// The combat lab's endless enemy pressure: patrols and ambushers (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md).
// UE-only, no Godot reference. Pure rules: CombatPressureRules / AmbushWatchRules / AmbushSpotRules / AmbushPlanRules / AmbushRelocateRules
// (CodexTactics.Combat.Pressure.*, CodexTactics.AI.AmbushWatch.*, CodexTactics.Combat.AmbushSpots.*, .AmbushPlan.*, .AmbushRelocate.*).

#include "Combat/CombatPressureSubsystem.h"

#include "AI/PatrolRouteActor.h"
#include "AI/PerceptionRules.h"
#include "Algo/Reverse.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace CombatPressureLocal
{
	static TAutoConsoleVariable<int32> CVarPressureEnabled(TEXT("Codex.Pressure.Enabled"), -1,
		TEXT("Combat lab enemy pressure (docs/design/COMBAT_LAB.md): -1 = the level JSON \"pressure\" block decides, 0 = off, 1 = on (the block's values or the defaults)."));
	static TAutoConsoleVariable<float> CVarPressureInterval(TEXT("Codex.Pressure.IntervalSec"), 0.f,
		TEXT("Combat lab: seconds between spawn events; <= 0 = the level JSON \"pressure\".\"interval_sec\" (120)."));

	/** Spacing of a group's members round its point, cm. */
	constexpr float GroupSpacingCm = 220.f;
	/** Candidates tried per spawn event. */
	constexpr int32 SpotCandidates = 72;
	/** A capsule's half height above the feet when spawning, cm. */
	constexpr float SpawnLiftCm = 95.f;
	/** Ambushers are checked this often, s. */
	constexpr float AmbushStepSeconds = 0.1f;
	constexpr float RecycleStepSeconds = 0.5f;
	/** The eye height of an operative / the chest height of an enemy for the sight lines, cm. */
	constexpr float EyeLiftCm = 60.f;
	constexpr float TargetLiftCm = 110.f;
	/** A relocating group's new spot keeps this far from where it hid, cm. */
	constexpr float RelocateAvoidCm = 600.f;
	/** Name of the actor tag of a hand-placed ambush point (any actor in the map). */
	static const FName AmbushPointTag(TEXT("CodexAmbushPoint"));

	FVector2D To2D(const FVector& V) { return FVector2D(V.X, V.Y); }

	bool IsLiving(const AOperativeCharacter* Member)
	{
		return Member && Member->HealthComponent && Member->HealthComponent->IsAlive();
	}

	bool IsLivingEnemy(const AEnemyCharacter* Enemy)
	{
		return Enemy && !Enemy->IsDying() && !Enemy->IsActorBeingDestroyed();
	}

	FVector FeetOf(const AEnemyCharacter& Enemy)
	{
		return Enemy.GetActorLocation() - FVector(0.f, 0.f, Enemy.GetSimpleCollisionHalfHeight());
	}

	float YawOf(const FVector& Direction)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)));
	}

	/** Perception muted to nothing (a hidden ambusher waits for the watch rule, not for its own eyes / ears / nose). */
	FEnemyPerceptionParams Muted(const FEnemyPerceptionParams& Params)
	{
		FEnemyPerceptionParams Out = PerceptionRules::Scaled(Params, 0.f);
		Out.SmellDetectCm = 0.f;
		return Out;
	}

	const TCHAR* KindLabel(EPressureSpawnKind Kind)
	{
		switch (Kind)
		{
		case EPressureSpawnKind::Patrol: return TEXT("patrol");
		case EPressureSpawnKind::Coordinated: return TEXT("coordinated ambush");
		default: return TEXT("ambush");
		}
	}
}

bool UCombatPressureSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCombatPressureSubsystem::Deinitialize()
{
	Managed.Reset();
	Routes.Reset();
	Plans.Reset();
	Super::Deinitialize();
}

TStatId UCombatPressureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCombatPressureSubsystem, STATGROUP_Tickables);
}

void UCombatPressureSubsystem::ReadConfig()
{
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	if (!Level)
	{
		return; // the game mode's StartPlay has not applied the level data yet
	}
	bConfigRead = true;
	LevelConfig = Level->Config.Pressure;
	Config = LevelConfig;
	if (WatchHalfAngleOverride >= 0.f)
	{
		Config.Watch.WatchHalfAngleDeg = WatchHalfAngleOverride;
	}
	if (CoordinatedShareOverride >= 0.f)
	{
		Config.Coordinated.Share = CoordinatedShareOverride;
	}
	Stream.Initialize(Config.Seed);
	Clock = 0.f;
	DueAt = Config.FirstEventSeconds;
	if (IsActive())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] on: every %.0f s (first at %.0f s), groups %d-%d, max alive %d, patrols %.0f %%, seed %d, wall clearance %.0f cm"),
			Config.IntervalSeconds, Config.FirstEventSeconds, Config.GroupMin, Config.GroupMax, Config.MaxAlive, Config.PatrolShare * 100.f, Config.Seed,
			Config.WallClearanceCm);
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambushes: %d authored points, coordinated %.0f %% (%d-%d bodies, own cap %d), relocation %s after %.0f s"),
			Config.AuthoredPoints.Num(), Config.Coordinated.Share * 100.f, Config.Coordinated.BodiesMin, Config.Coordinated.BodiesMax, Config.Coordinated.MaxAlive,
			Config.Relocate.bEnabled ? TEXT("on") : TEXT("off"), Config.Relocate.AfterSeconds);
	}
}

bool UCombatPressureSubsystem::IsActive() const
{
	const int32 Override = CombatPressureLocal::CVarPressureEnabled.GetValueOnGameThread();
	return Override >= 0 ? Override > 0 : Config.bEnabled;
}

void UCombatPressureSubsystem::SetWatchHalfAngleOverride(float Degrees)
{
	WatchHalfAngleOverride = Degrees;
	Config.Watch.WatchHalfAngleDeg = Degrees >= 0.f ? Degrees : LevelConfig.Watch.WatchHalfAngleDeg;
}

void UCombatPressureSubsystem::SetCoordinatedShareOverride(float Share)
{
	CoordinatedShareOverride = Share;
	Config.Coordinated.Share = Share >= 0.f ? FMath::Clamp(Share, 0.f, 1.f) : LevelConfig.Coordinated.Share;
}

void UCombatPressureSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	if (!bConfigRead)
	{
		ReadConfig();
		if (!bConfigRead)
		{
			return;
		}
	}
	if (!IsActive())
	{
		return;
	}
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && (Flow->GetCombatMode() == ECodexCombatMode::TurnBased || Flow->GetPhase() == ECodexGamePhase::GameOver))
	{
		return; // turn-based combat stops the pressure's clock (no spawns into the grid)
	}
	const float Interval = CombatPressureLocal::CVarPressureInterval.GetValueOnGameThread() > 0.f
		? CombatPressureLocal::CVarPressureInterval.GetValueOnGameThread() : LevelConfig.IntervalSeconds;
	Config.IntervalSeconds = Interval;
	Clock += DeltaTime;
	CleanUp();

	AmbushTimer += DeltaTime;
	if (AmbushTimer >= CombatPressureLocal::AmbushStepSeconds)
	{
		TickAmbushers(AmbushTimer);
		AmbushTimer = 0.f;
	}
	RecycleTimer += DeltaTime;
	if (RecycleTimer >= CombatPressureLocal::RecycleStepSeconds)
	{
		TickRecycle(RecycleTimer);
		RecycleTimer = 0.f;
	}

	if (!bAutoSpawn)
	{
		return;
	}
	switch (CombatPressureRules::DecideEvent(Clock, DueAt, GetAliveCount(), Config.MaxAlive))
	{
	case EPressureEventDecision::NotDue:
		break;
	case EPressureEventDecision::DelayedCapFull:
		if (!bDelayLogged)
		{
			bDelayLogged = true;
			++EventsDelayed;
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] event due at %.0f s waits: %d of %d alive"), Clock, GetAliveCount(), Config.MaxAlive);
		}
		break;
	case EPressureEventDecision::Spawn:
	{
		bDelayLogged = false;
		const TArray<AEnemyCharacter*> Spawned = TriggerEvent(TOptional<EPressureSpawnKind>(), 0, false);
		// No valid spot this time (the squad in a tight corner): try again soon instead of a whole interval later.
		DueAt = Clock + (Spawned.IsEmpty() ? FMath::Min(10.f, Interval) : Interval);
		EventsFired += Spawned.IsEmpty() ? 0 : 1;
		if (!Spawned.IsEmpty() && FirstEventClock < 0.f)
		{
			FirstEventClock = Clock;
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] the first group came at %.1f s"), Clock);
		}
		break;
	}
	}
}

int32 UCombatPressureSubsystem::GetAliveCount() const
{
	int32 Alive = 0;
	for (const FManaged& Each : Managed)
	{
		Alive += CombatPressureLocal::IsLivingEnemy(Each.Enemy.Get()) ? 1 : 0;
	}
	return Alive;
}

void UCombatPressureSubsystem::CleanUp()
{
	Managed.RemoveAll([](const FManaged& Each) { return !CombatPressureLocal::IsLivingEnemy(Each.Enemy.Get()); });
	for (auto It = Routes.CreateIterator(); It; ++It)
	{
		const int32 Group = It.Key();
		const bool bGroupAlive = Managed.ContainsByPredicate([Group](const FManaged& Each) { return Each.Group == Group; });
		if (!bGroupAlive)
		{
			if (APatrolRouteActor* Route = It.Value().Get())
			{
				Route->Destroy();
			}
			It.RemoveCurrent();
		}
	}
	for (auto It = GroupBypassSeconds.CreateIterator(); It; ++It)
	{
		const int32 Group = It.Key();
		if (!Managed.ContainsByPredicate([Group](const FManaged& Each) { return Each.Group == Group; }))
		{
			It.RemoveCurrent();
		}
	}
}

void UCombatPressureSubsystem::DespawnAll()
{
	for (const FManaged& Each : Managed)
	{
		if (AEnemyCharacter* Enemy = Each.Enemy.Get())
		{
			Enemy->Destroy();
		}
	}
	Managed.Reset();
	for (const TPair<int32, TWeakObjectPtr<APatrolRouteActor>>& Route : Routes)
	{
		if (APatrolRouteActor* Actor = Route.Value.Get())
		{
			Actor->Destroy();
		}
	}
	Routes.Reset();
	GroupBypassSeconds.Reset();
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] every pressure enemy removed"));
}

TArray<FVector2D> UCombatPressureSubsystem::GatherSquadPositions() const
{
	TArray<FVector2D> Out;
	if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (CombatPressureLocal::IsLiving(Member))
			{
				Out.Add(CombatPressureLocal::To2D(Member->GetActorLocation()));
			}
		}
	}
	return Out;
}

TArray<FAmbushWatcher> UCombatPressureSubsystem::GatherWatchers() const
{
	TArray<FAmbushWatcher> Out;
	if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!CombatPressureLocal::IsLiving(Member))
			{
				continue;
			}
			const FVector2D Here = CombatPressureLocal::To2D(Member->GetActorLocation());
			// Answer 4 (2026-11-01): aiming at / fighting another enemy he still watches - his aim is one more look.
			TOptional<float> Aim;
			FVector AimPoint;
			if (Member->GetAimTargetPoint(AimPoint) && FVector2D::Distance(CombatPressureLocal::To2D(AimPoint), Here) > 1.0)
			{
				Aim = AmbushWatchRules::BearingDeg(Here, CombatPressureLocal::To2D(AimPoint));
			}
			// His formation sector look (FORMATION_SECTORS.md): where his eyes go while the body turns there.
			Out.Append(AmbushWatchRules::MemberLooks(Here, static_cast<float>(Member->GetActorRotation().Yaw), Aim, Member->GetFormationLookYaw()));
		}
	}
	return Out;
}

bool UCombatPressureSubsystem::IsVisibleToSquad(const FVector& Feet) const
{
	const UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return false;
	}
	const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatPressureSight), false);
	const FVector Target = Feet + FVector(0.f, 0.f, CombatPressureLocal::TargetLiftCm);
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (CombatPressureLocal::IsLiving(Member)
			&& !World->LineTraceTestByObjectType(Member->GetActorLocation() + FVector(0.f, 0.f, CombatPressureLocal::EyeLiftCm), Target, Statics, Params))
		{
			return true;
		}
	}
	return false;
}

bool UCombatPressureSubsystem::IsGroupSeen(int32 Group) const
{
	for (const FManaged& Each : Managed)
	{
		const AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Group == Group && CombatPressureLocal::IsLivingEnemy(Enemy) && IsVisibleToSquad(CombatPressureLocal::FeetOf(*Enemy)))
		{
			return true;
		}
	}
	return false;
}

const TArray<FPressureBox>& UCombatPressureSubsystem::GetObstacleBoxes()
{
	if (!bObstaclesRead)
	{
		GatherObstacles();
	}
	return Obstacles;
}

void UCombatPressureSubsystem::GatherObstacles()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bConfigRead)
	{
		ReadConfig(); // the bounds shrink by the level's wall clearance
	}
	bObstaclesRead = true;
	Obstacles.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<APawn>())
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || Mesh->IsA<UInstancedStaticMeshComponent>() || !Mesh->GetStaticMesh() || !Mesh->IsCollisionEnabled()
				|| Mesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				continue;
			}
			const FBox Local = Mesh->GetStaticMesh()->GetBoundingBox();
			const FTransform Transform = Mesh->GetComponentTransform();
			const FVector Scale = Transform.GetScale3D().GetAbs();
			if (Local.GetSize().Z * Scale.Z < 50.f)
			{
				continue; // a floor slab / a kerb: nothing to keep clear of
			}
			FPressureBox Box;
			Box.Center = CombatPressureLocal::To2D(Transform.TransformPosition(Local.GetCenter()));
			Box.AxisX = CombatPressureLocal::To2D(Transform.GetUnitAxis(EAxis::X)).GetSafeNormal();
			if (Box.AxisX.IsNearlyZero())
			{
				Box.AxisX = FVector2D(1.0, 0.0);
			}
			Box.HalfSize = FVector2D(Local.GetExtent().X * Scale.X, Local.GetExtent().Y * Scale.Y);
			Obstacles.Add(Box);
		}
	}
	Bounds = FBox2D(ForceInit);
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		const FBox Box = It->GetComponentsBoundingBox(true);
		Bounds += FBox2D(CombatPressureLocal::To2D(Box.Min), CombatPressureLocal::To2D(Box.Max));
	}
	if (Bounds.bIsValid)
	{
		Bounds = Bounds.ExpandBy(-Config.WallClearanceCm);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] %d obstacle boxes, bounds %s"), Obstacles.Num(), Bounds.bIsValid ? *Bounds.ToString() : TEXT("none"));
}

const TArray<FAmbushSpot>& UCombatPressureSubsystem::GetHideSpots()
{
	if (bHideSpotsRead)
	{
		return HideSpots;
	}
	GetObstacleBoxes();
	bHideSpotsRead = true;
	// Authored: the level JSON's points + any actor in the map tagged CodexAmbushPoint.
	TArray<FVector2D> Authored = Config.AuthoredPoints;
	int32 Tagged = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(CombatPressureLocal::AmbushPointTag))
			{
				Authored.Add(CombatPressureLocal::To2D(It->GetActorLocation()));
				++Tagged;
			}
		}
	}
	HideSpots = AmbushSpotRules::Candidates(Obstacles, Authored, Config.Spots);
	int32 Counts[4] = { 0, 0, 0, 0 };
	for (const FAmbushSpot& Spot : HideSpots)
	{
		++Counts[FMath::Clamp(static_cast<int32>(Spot.Kind), 0, 3)];
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] hiding spots: %d wall ends, %d doorways, %d authored (%d from tagged actors)"),
		Counts[static_cast<int32>(EAmbushSpotKind::WallEnd)], Counts[static_cast<int32>(EAmbushSpotKind::Doorway)],
		Counts[static_cast<int32>(EAmbushSpotKind::Authored)], Tagged);
	return HideSpots;
}

float UCombatPressureSubsystem::HideClearance() const
{
	return AmbushSpotRules::HideClearanceCm(Config.WallClearanceCm, Config.Spots);
}

FVector UCombatPressureSubsystem::LeaderFeet() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	return Leader ? Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight()) : FVector::ZeroVector;
}

FVector UCombatPressureSubsystem::SquadHeading() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return FVector::ForwardVector;
	}
	const FVector Velocity = Leader->GetVelocity().GetSafeNormal2D();
	return Leader->GetVelocity().Size2D() > 50.f ? Velocity : Leader->GetActorForwardVector().GetSafeNormal2D();
}

TOptional<FVector> UCombatPressureSubsystem::ProjectFeet(const FVector2D& Point, float GroundZ) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav)
	{
		return {};
	}
	// The ground under Point (the squad may stand on a tower's roof: GroundZ is only where to look from).
	FHitResult Ground;
	const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatPressureGround), false);
	if (!World->LineTraceSingleByObjectType(Ground, FVector(Point, GroundZ + 1500.f), FVector(Point, GroundZ - 1500.f), Statics, Params))
	{
		return {};
	}
	FNavLocation OnNav;
	if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(40.f, 40.f, 120.f)))
	{
		return {};
	}
	// On that ground (never a roof's edge nearby) and where it was asked for.
	if (FMath::Abs(OnNav.Location.Z - Ground.ImpactPoint.Z) > 60.f || FVector2D::Distance(CombatPressureLocal::To2D(OnNav.Location), Point) > 40.f)
	{
		return {};
	}
	return OnNav.Location;
}

TOptional<FVector> UCombatPressureSubsystem::FindSpot(EPressureSpawnKind Kind)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const TArray<FVector2D> Squad = GatherSquadPositions();
	if (!World || !Nav || Squad.IsEmpty())
	{
		return {};
	}
	const TArray<FPressureBox>& Boxes = GetObstacleBoxes();
	const TArray<FAmbushWatcher> Watchers = GatherWatchers();
	const FVector Feet = LeaderFeet();
	const FVector2D Leader2D = CombatPressureLocal::To2D(Feet);
	const FVector Heading = SquadHeading();
	const float HeadingYaw = CombatPressureLocal::YawOf(Heading);
	const bool bAmbush = Kind == EPressureSpawnKind::Ambush;
	const float MinDistance = bAmbush ? Config.AmbushMinDistanceCm : Config.MinSquadDistanceCm;
	const float MaxDistance = bAmbush ? Config.AmbushMaxDistanceCm : Config.MaxSquadDistanceCm;

	struct FCandidate
	{
		FVector Feet;
		float Score;
	};
	TArray<FCandidate> Candidates;
	for (int32 Try = 0; Try < CombatPressureLocal::SpotCandidates; ++Try)
	{
		// Mostly ahead of the squad's way (+-80 deg), the rest anywhere.
		const float Yaw = Stream.FRand() < 0.7f ? HeadingYaw + Stream.FRandRange(-80.f, 80.f) : Stream.FRandRange(-180.f, 180.f);
		const float Distance = Stream.FRandRange(MinDistance, MaxDistance);
		const FVector2D Point = Leader2D + FVector2D(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw))) * Distance;
		if (CombatPressureRules::CheckSpawnPoint(Point, Boxes, Config.WallClearanceCm, Squad, MinDistance, Bounds) != ESpawnPointVerdict::Ok)
		{
			continue;
		}
		const TOptional<FVector> OnNav = ProjectFeet(Point, static_cast<float>(Feet.Z));
		if (!OnNav.IsSet() || CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Boxes) < Config.WallClearanceCm)
		{
			continue;
		}
		FPressureSpotInput Input;
		Input.Kind = Kind;
		Input.DistanceCm = Distance;
		Input.MinDistanceCm = MinDistance;
		Input.MaxDistanceCm = MaxDistance;
		Input.AheadCos = FMath::Cos(FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(HeadingYaw, Yaw)));
		Input.bConcealed = !IsVisibleToSquad(OnNav.GetValue());
		Input.bWatched = AmbushWatchRules::IsWatched(Watchers, Point, Config.Watch);
		const float Score = CombatPressureRules::ScoreSpot(Input);
		if (Score > -100.f)
		{
			Candidates.Add({ OnNav.GetValue(), Score + static_cast<float>(Stream.FRandRange(0.f, 0.05f)) });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	// The best one the squad can be reached from on foot (a pocket closed off by walls is no use).
	for (int32 Index = 0; Index < FMath::Min(6, Candidates.Num()); ++Index)
	{
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Candidates[Index].Feet, Feet + FVector(0.f, 0.f, 50.f));
		if (Path && Path->IsValid() && !Path->IsPartial())
		{
			return Candidates[Index].Feet;
		}
	}
	return {};
}

TOptional<UCombatPressureSubsystem::FHideChoice> UCombatPressureSubsystem::FindHideSpot(float MinDistanceCm, float MaxDistanceCm, float MinAheadCos,
	const TArray<FVector2D>& Avoid, float AvoidCm)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const TArray<FVector2D> Squad = GatherSquadPositions();
	if (!World || !Nav || Squad.IsEmpty())
	{
		return {};
	}
	const TArray<FPressureBox>& Boxes = GetObstacleBoxes();
	const TArray<FAmbushSpot>& Spots = GetHideSpots();
	const TArray<FAmbushWatcher> Watchers = GatherWatchers();
	const FVector Feet = LeaderFeet();
	const FVector2D Leader2D = CombatPressureLocal::To2D(Feet);
	const FVector Heading = SquadHeading();
	const FVector2D Heading2D = CombatPressureLocal::To2D(Heading).GetSafeNormal();
	const float Clearance = HideClearance();

	struct FCandidate
	{
		FHideChoice Choice;
		float Score;
	};
	TArray<FCandidate> Candidates;
	for (const FAmbushSpot& Spot : Spots)
	{
		const float Distance = static_cast<float>(FVector2D::Distance(Leader2D, Spot.Position));
		if (Distance < MinDistanceCm || Distance > MaxDistanceCm)
		{
			continue;
		}
		if (CombatPressureRules::CheckSpawnPoint(Spot.Position, Boxes, Clearance, Squad, MinDistanceCm, Bounds) != ESpawnPointVerdict::Ok)
		{
			continue;
		}
		if (Avoid.ContainsByPredicate([&Spot, AvoidCm](const FVector2D& Each) { return FVector2D::Distance(Each, Spot.Position) < AvoidCm; }))
		{
			continue;
		}
		const float AheadCos = Distance > 1.f ? static_cast<float>(FVector2D::DotProduct(Heading2D, (Spot.Position - Leader2D) / Distance)) : 1.f;
		if (AheadCos < MinAheadCos)
		{
			continue;
		}
		const TOptional<FVector> OnNav = ProjectFeet(Spot.Position, static_cast<float>(Feet.Z));
		if (!OnNav.IsSet() || CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Boxes) < Clearance - 1.f
			|| IsVisibleToSquad(OnNav.GetValue()))
		{
			continue; // off the navmesh / too near a wall / an operative sees it
		}
		FPressureSpotInput Input;
		Input.Kind = EPressureSpawnKind::Ambush;
		Input.DistanceCm = Distance;
		Input.MinDistanceCm = MinDistanceCm;
		Input.MaxDistanceCm = MaxDistanceCm;
		Input.AheadCos = AheadCos;
		Input.bConcealed = true;
		Input.bWatched = AmbushWatchRules::IsWatched(Watchers, Spot.Position, Config.Watch);
		Input.KindBonus = AmbushSpotRules::KindBonus(Spot.Kind, Config.Spots);
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Choice.Feet = OnNav.GetValue();
		Candidate.Choice.Kind = Spot.Kind;
		Candidate.Choice.AheadCos = AheadCos;
		Candidate.Score = CombatPressureRules::ScoreSpot(Input) + static_cast<float>(Stream.FRandRange(0.f, 0.05f));
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	for (int32 Index = 0; Index < FMath::Min(6, Candidates.Num()); ++Index)
	{
		const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Candidates[Index].Choice.Feet, Feet + FVector(0.f, 0.f, 50.f));
		if (Path && Path->IsValid() && !Path->IsPartial())
		{
			return Candidates[Index].Choice;
		}
	}
	return {};
}

bool UCombatPressureSubsystem::PlanLayout(FPressurePlanInfo& OutInfo)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const TArray<FVector2D> Squad = GatherSquadPositions();
	if (!World || !Nav || Squad.IsEmpty())
	{
		return false;
	}
	const FAmbushPlanParams& Plan = Config.Coordinated;
	const TArray<FPressureBox>& Boxes = GetObstacleBoxes();
	const TArray<FAmbushSpot>& Spots = GetHideSpots();
	const FVector Feet = LeaderFeet();
	const FVector2D Leader2D = CombatPressureLocal::To2D(Feet);
	const float HeadingYaw = CombatPressureLocal::YawOf(SquadHeading());
	const FVector2D Anchor = AmbushPlanRules::Anchor(Leader2D, HeadingYaw, Plan);
	const float Clearance = HideClearance();

	// Every concealed, valid hiding spot within the plan's band.
	TArray<FVector> Feets;
	TArray<EAmbushSpotKind> Kinds;
	for (const FAmbushSpot& Spot : Spots)
	{
		if (FVector2D::Distance(Leader2D, Spot.Position) > Plan.MaxSquadDistanceCm
			|| CombatPressureRules::CheckSpawnPoint(Spot.Position, Boxes, Clearance, Squad, Plan.MinSquadDistanceCm, Bounds) != ESpawnPointVerdict::Ok)
		{
			continue;
		}
		const TOptional<FVector> OnNav = ProjectFeet(Spot.Position, static_cast<float>(Feet.Z));
		if (!OnNav.IsSet() || CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Boxes) < Clearance - 1.f
			|| IsVisibleToSquad(OnNav.GetValue()))
		{
			continue;
		}
		Feets.Add(OnNav.GetValue());
		Kinds.Add(Spot.Kind);
	}
	// Assign one per party; a spot the squad cannot be reached from is dropped and the parties re-assigned.
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		TArray<FVector2D> Points;
		for (const FVector& Each : Feets)
		{
			Points.Add(CombatPressureLocal::To2D(Each));
		}
		const TArray<int32> Assigned = AmbushPlanRules::AssignSpots(Anchor, HeadingYaw, Points, Plan);
		if (!AmbushPlanRules::IsComplete(Assigned))
		{
			return false;
		}
		int32 Unreachable = INDEX_NONE;
		for (const int32 Index : Assigned)
		{
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Feets[Index], Feet + FVector(0.f, 0.f, 50.f));
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{
				Unreachable = Index;
				break;
			}
		}
		if (Unreachable != INDEX_NONE)
		{
			Feets.RemoveAt(Unreachable);
			Kinds.RemoveAt(Unreachable);
			continue;
		}
		OutInfo.Anchor = Anchor;
		OutInfo.HeadingYawDeg = HeadingYaw;
		OutInfo.PartySpots.Reset();
		OutInfo.PartyKinds.Reset();
		for (const int32 Index : Assigned)
		{
			OutInfo.PartySpots.Add(Feets[Index]);
			OutInfo.PartyKinds.Add(Kinds[Index]);
		}
		return true;
	}
	return false;
}

TArray<FVector> UCombatPressureSubsystem::GroupSpots(const FVector& Feet, int32 Count, bool bConcealed) const
{
	TArray<FVector> Out = { Feet };
	const TArray<FVector2D> Squad = GatherSquadPositions();
	const FVector2D Center = CombatPressureLocal::To2D(Feet);
	const float Clearance = bConcealed ? HideClearance() : Config.WallClearanceCm;
	for (int32 Index = 1; Out.Num() < Count && Index < 12; ++Index)
	{
		const FVector2D Point = Center + CombatPressureRules::GroupOffset(Index, CombatPressureLocal::GroupSpacingCm);
		if (CombatPressureRules::CheckSpawnPoint(Point, Obstacles, Clearance, Squad, 0.f, Bounds) != ESpawnPointVerdict::Ok)
		{
			continue;
		}
		if (const TOptional<FVector> OnNav = ProjectFeet(Point, static_cast<float>(Feet.Z));
			OnNav.IsSet() && CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Obstacles) >= Clearance
			&& (!bConcealed || !IsVisibleToSquad(OnNav.GetValue())))
		{
			Out.Add(OnNav.GetValue());
		}
	}
	return Out;
}

APatrolRouteActor* UCombatPressureSubsystem::MakeRoute(const FVector& Feet)
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(APatrolRouteActor::StaticClass(), FTransform(Feet), Params);
	if (!Route || !Route->GetRouteSpline())
	{
		return nullptr;
	}
	Route->bIsLoop = true;
	Route->DefaultWaitTimeSeconds = Config.PatrolWaitSeconds;
	// The loop round its spawn point: the waypoints that are clear of the walls and on the navmesh (the spawn point itself first).
	TArray<FVector> Points = { Feet };
	const float Phase = Stream.FRandRange(0.f, 360.f);
	const bool bClockwise = Stream.FRand() < 0.5f;
	for (float Radius : { Config.PatrolRadiusCm, Config.PatrolRadiusCm * 0.6f })
	{
		Points.SetNum(1);
		TArray<FVector2D> Loop = CombatPressureRules::PatrolLoop(CombatPressureLocal::To2D(Feet), Radius, Config.PatrolPoints, Phase);
		if (bClockwise)
		{
			Algo::Reverse(Loop);
		}
		for (const FVector2D& Point : Loop)
		{
			if (CombatPressureRules::Clearance(Point, Obstacles) < Config.WallClearanceCm || (Bounds.bIsValid && !Bounds.IsInside(Point)))
			{
				continue;
			}
			if (const TOptional<FVector> OnNav = ProjectFeet(Point, static_cast<float>(Feet.Z)))
			{
				Points.Add(OnNav.GetValue());
			}
		}
		if (Points.Num() >= 3)
		{
			break;
		}
	}
	USplineComponent* Spline = Route->GetRouteSpline();
	Spline->ClearSplinePoints(false);
	for (const FVector& Point : Points)
	{
		Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
	}
	for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		Spline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}
	Spline->SetClosedLoop(true, false);
	Spline->UpdateSpline();
	return Route;
}

AEnemyCharacter* UCombatPressureSubsystem::SpawnOne(EEnemyArchetype Type, const FVector& Feet, const FVector& FaceTowards, EPressureSpawnKind Kind,
	int32 Group, bool bPlacedByCaller, const FSpawnTags& Tags)
{
	UWorld* World = GetWorld();
	UWaveSubsystem* Waves = World ? World->GetSubsystem<UWaveSubsystem>() : nullptr;
	if (!Waves)
	{
		return nullptr;
	}
	const FVector Facing = (FaceTowards - Feet).GetSafeNormal2D();
	AEnemyCharacter* Enemy = Waves->SpawnLevelEnemy(Type, Feet + FVector(0.f, 0.f, CombatPressureLocal::SpawnLiftCm),
		Facing.IsNearlyZero() ? FRotator::ZeroRotator : Facing.Rotation());
	if (!Enemy)
	{
		return nullptr;
	}
	FManaged& Entry = Managed.AddDefaulted_GetRef();
	Entry.Enemy = Enemy;
	Entry.Kind = Kind;
	Entry.Type = Type;
	Entry.Group = Group;
	Entry.Plan = Tags.Plan;
	Entry.Party = Tags.Party;
	FPressureSpawnRecord& Record = Records.AddDefaulted_GetRef();
	Record.Enemy = Enemy;
	Record.Kind = Kind;
	Record.Type = Type;
	Record.Group = Group;
	Record.Location = Feet;
	Record.ClearanceCm = CombatPressureRules::Clearance(CombatPressureLocal::To2D(Feet), GetObstacleBoxes());
	Record.SquadDistanceCm = AmbushWatchRules::NearestDistance(GatherSquadPositions(), CombatPressureLocal::To2D(Feet));
	Record.bConcealed = !IsVisibleToSquad(Feet);
	Record.bWatched = AmbushWatchRules::IsWatched(GatherWatchers(), CombatPressureLocal::To2D(Feet), Config.Watch);
	Record.bPlacedByCaller = bPlacedByCaller;
	Record.SpotKind = Tags.SpotKind;
	Record.Plan = Tags.Plan;
	Record.Party = Tags.Party;
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] spawned %s %s (group %d%s) at %s: clearance %.0f cm, squad %.0f cm, concealed %d, watched %d%s"),
		Kind == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambusher"), *UEnum::GetValueAsString(Type), Group,
		Tags.Plan > 0 ? *FString::Printf(TEXT(", plan %d %s"), Tags.Plan, AmbushPlanRules::PartyName(Tags.Party)) : TEXT(""), *Feet.ToCompactString(),
		Record.ClearanceCm, Record.SquadDistanceCm, Record.bConcealed ? 1 : 0, Record.bWatched ? 1 : 0,
		Tags.SpotKind != EAmbushSpotKind::None ? *FString::Printf(TEXT(", %s spot"), AmbushSpotRules::KindName(Tags.SpotKind)) : TEXT(""));
	return Enemy;
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnPatrolGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types)
{
	return SpawnPatrolGroup(Feet, Types, true);
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnAmbushGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards)
{
	return SpawnAmbushGroup(Feet, Types, FaceTowards, true, FSpawnTags());
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnPatrolGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, bool bPlacedByCaller)
{
	TArray<AEnemyCharacter*> Out;
	UWorld* World = GetWorld();
	ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	if (!Encounter || Types.IsEmpty())
	{
		return Out;
	}
	GetObstacleBoxes();
	const int32 Group = NextGroup++;
	const TArray<FVector> Spots = GroupSpots(Feet, Types.Num(), false);
	APatrolRouteActor* Route = MakeRoute(Feet);
	const FVector Ahead = Route && Route->GetNumberOfWaypoints() > 1 ? Route->GetWaypointWorldLocation(1) : Feet + FVector(100.f, 0.f, 0.f);
	AEnemyCharacter* Leader = nullptr;
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		AEnemyCharacter* Enemy = SpawnOne(Types[Index], Spots[Index], Ahead, EPressureSpawnKind::Patrol, Group, bPlacedByCaller, FSpawnTags());
		if (!Enemy)
		{
			continue;
		}
		// The leader walks the loop, the others escort him; then "placed in the map": a level enemy (ambush start, skirmish).
		Enemy->StartPatrol(Leader ? nullptr : Route, Leader);
		Encounter->MarkLevelEnemy(Enemy);
		Leader = Leader ? Leader : Enemy;
		Out.Add(Enemy);
	}
	if (Route)
	{
		Routes.Add(Group, Route);
	}
	return Out;
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnAmbushGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards,
	bool bPlacedByCaller, const FSpawnTags& Tags)
{
	TArray<AEnemyCharacter*> Out;
	UWorld* World = GetWorld();
	ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	if (!Encounter || Types.IsEmpty())
	{
		return Out;
	}
	GetObstacleBoxes();
	const int32 Group = NextGroup++;
	const TArray<FVector> Spots = GroupSpots(Feet, Types.Num(), !bPlacedByCaller);
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		AEnemyCharacter* Enemy = SpawnOne(Types[Index], Spots[Index], FaceTowards, EPressureSpawnKind::Ambush, Group, bPlacedByCaller, Tags);
		if (!Enemy)
		{
			continue;
		}
		// Hidden: its own senses muted (the watch rule decides when it strikes); unrouted level enemy = it guards the spot.
		Enemy->PerceptionOverride = CombatPressureLocal::Muted(Enemy->GetPerception());
		Enemy->bOverridePerception = true;
		Enemy->GuardLookAroundDeg = 20.f;
		Encounter->MarkLevelEnemy(Enemy);
		Out.Add(Enemy);
	}
	return Out;
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::TriggerEvent(TOptional<EPressureSpawnKind> Kind, int32 Count, bool bIgnoreCap)
{
	TArray<AEnemyCharacter*> Out;
	if (!bConfigRead)
	{
		ReadConfig();
	}
	GetObstacleBoxes();
	const float RandKind = Stream.FRand();
	const float RandCoordinated = Stream.FRand();
	EPressureSpawnKind Picked = Kind.IsSet() ? Kind.GetValue()
		: CombatPressureRules::PickEventKind(RandKind, RandCoordinated, Config.PatrolShare, Config.Coordinated.Share);
	if (Picked == EPressureSpawnKind::Coordinated)
	{
		TArray<EEnemyArchetype> Types;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Types.Add(CombatPressureRules::PickType(Config.TypeWeights, Stream.FRand()));
		}
		if (!Types.IsEmpty())
		{
			TArray<FPressureTypeWeight> Ranged = CombatPressureRules::EffectiveWeights(Config.TypeWeights);
			Ranged.RemoveAll([](const FPressureTypeWeight& Each) { return !AmbushPlanRules::IsRanged(Each.Type); });
			if (!Ranged.IsEmpty())
			{
				AmbushPlanRules::EnsureRanged(Types, CombatPressureRules::PickType(Ranged, Stream.FRand()), Config.Coordinated.MinRanged);
			}
		}
		Out = TriggerCoordinated(Types, bIgnoreCap);
		if (!Out.IsEmpty())
		{
			return Out;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no room / no layout for a coordinated ambush: a single ambush instead"));
		Picked = EPressureSpawnKind::Ambush;
	}
	const int32 Room = bIgnoreCap ? Config.GroupMax : FMath::Max(0, Config.MaxAlive - GetAliveCount());
	const int32 Size = Count > 0 ? FMath::Min(Count, FMath::Max(Room, bIgnoreCap ? Count : 0))
		: CombatPressureRules::GroupSize(Stream.FRand(), Config.GroupMin, Config.GroupMax, bIgnoreCap ? 0 : GetAliveCount(), bIgnoreCap ? Config.GroupMax : Config.MaxAlive);
	if (Size <= 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no room for an event (%d of %d alive)"), GetAliveCount(), Config.MaxAlive);
		return Out;
	}
	TArray<EEnemyArchetype> Types;
	for (int32 Index = 0; Index < Size; ++Index)
	{
		Types.Add(CombatPressureRules::PickType(Config.TypeWeights, Stream.FRand()));
	}
	if (Picked == EPressureSpawnKind::Ambush)
	{
		// Answer 1 (2026-11-01): a wall end / doorway / authored hiding spot - the best concealed one ahead along the squad's heading.
		if (const TOptional<FHideChoice> Hide = FindHideSpot(Config.AmbushMinDistanceCm, Config.AmbushMaxDistanceCm, -1.f, {}, 0.f))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] event at %.0f s: ambush of %d at a %s"), Clock, Size, AmbushSpotRules::KindName(Hide->Kind));
			FSpawnTags Tags;
			Tags.SpotKind = Hide->Kind;
			return SpawnAmbushGroup(Hide->Feet, Types, LeaderFeet(), false, Tags);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no concealed hiding spot near the squad: a patrol instead"));
		Picked = EPressureSpawnKind::Patrol;
	}
	const TOptional<FVector> Spot = FindSpot(EPressureSpawnKind::Patrol);
	if (!Spot.IsSet())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no valid spawn spot this time"));
		return Out;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] event at %.0f s: %s of %d"), Clock, CombatPressureLocal::KindLabel(Picked), Size);
	return SpawnPatrolGroup(Spot.GetValue(), Types, false);
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::TriggerCoordinated(const TArray<EEnemyArchetype>& Types, bool bIgnoreCap)
{
	TArray<AEnemyCharacter*> Out;
	if (!bConfigRead)
	{
		ReadConfig();
	}
	GetObstacleBoxes();
	const FAmbushPlanParams& Params = Config.Coordinated;
	TArray<EEnemyArchetype> Bodies = Types;
	if (Bodies.IsEmpty())
	{
		const int32 Count = AmbushPlanRules::PlanBodies(Stream.FRand(), Params, bIgnoreCap ? 0 : GetAliveCount());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Bodies.Add(CombatPressureRules::PickType(Config.TypeWeights, Stream.FRand()));
		}
		TArray<FPressureTypeWeight> Ranged = CombatPressureRules::EffectiveWeights(Config.TypeWeights);
		Ranged.RemoveAll([](const FPressureTypeWeight& Each) { return !AmbushPlanRules::IsRanged(Each.Type); });
		if (!Ranged.IsEmpty())
		{
			AmbushPlanRules::EnsureRanged(Bodies, CombatPressureRules::PickType(Ranged, Stream.FRand()), Params.MinRanged);
		}
	}
	else if (!bIgnoreCap && GetAliveCount() + Bodies.Num() > Params.MaxAlive)
	{
		Bodies.Reset();
	}
	if (Bodies.Num() < 4)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no room for a coordinated ambush (%d alive, its cap %d)"), GetAliveCount(), Params.MaxAlive);
		return Out;
	}
	FPressurePlanInfo Info;
	if (!PlanLayout(Info))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no coordinated layout round the squad (four concealed hiding spots, one per side)"));
		return Out;
	}
	Info.Id = NextPlan++;
	LastPlanId = Info.Id;
	Info.PartyGroups.Init(0, 4);
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] event at %.0f s: coordinated ambush plan %d of %d round the squad (anchor %s)"), Clock, Info.Id, Bodies.Num(),
		*Info.Anchor.ToString());
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		const EAmbushParty Party = AmbushPlanRules::PlanParty(Slot);
		TArray<EEnemyArchetype> PartyTypes;
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			if (AmbushPlanRules::PartyOf(Index) == Party)
			{
				PartyTypes.Add(Bodies[Index]);
			}
		}
		FSpawnTags Tags;
		Tags.SpotKind = Info.PartyKinds[Slot];
		Tags.Plan = Info.Id;
		Tags.Party = Party;
		Info.PartyGroups[Slot] = NextGroup;
		Out.Append(SpawnAmbushGroup(Info.PartySpots[Slot], PartyTypes, LeaderFeet(), false, Tags));
	}
	FPlan& Plan = Plans.AddDefaulted_GetRef();
	Plan.Info = Info;
	return Out;
}

bool UCombatPressureSubsystem::GetAmbushState(const AEnemyCharacter* Enemy, EPressureAmbushState& OutState, float& OutHoldSeconds) const
{
	for (const FManaged& Each : Managed)
	{
		if (Each.Enemy.Get() == Enemy && Each.Kind == EPressureSpawnKind::Ambush)
		{
			OutState = Each.AmbushState;
			OutHoldSeconds = Each.HoldSeconds;
			return true;
		}
	}
	return false;
}

bool UCombatPressureSubsystem::GetMemberInfo(const AEnemyCharacter* Enemy, int32& OutGroup, int32& OutPlan, EAmbushParty& OutParty, bool& bOutSuppressor) const
{
	for (const FManaged& Each : Managed)
	{
		if (Each.Enemy.Get() == Enemy)
		{
			OutGroup = Each.Group;
			OutPlan = Each.Plan;
			OutParty = Each.Party;
			bOutSuppressor = Each.bSuppressor;
			return true;
		}
	}
	return false;
}

bool UCombatPressureSubsystem::GetPlanInfo(int32 PlanId, FPressurePlanInfo& OutInfo) const
{
	for (const FPlan& Plan : Plans)
	{
		if (Plan.Info.Id == PlanId)
		{
			OutInfo = Plan.Info;
			return true;
		}
	}
	return false;
}

void UCombatPressureSubsystem::SpringMember(FManaged& Each, const TCHAR* Why)
{
	AEnemyCharacter* Enemy = Each.Enemy.Get();
	if (!Enemy || Each.AmbushState == EPressureAmbushState::Sprung)
	{
		return;
	}
	Each.AmbushState = EPressureAmbushState::Sprung;
	Enemy->bOverridePerception = false; // its own senses again (the archetype's data)
	// Strike at the nearest operative: Patrol -> Engage, which starts the fight / joins the skirmish (NotifyHostileContact).
	const USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Nearest = nullptr;
	float Best = MAX_flt;
	for (AOperativeCharacter* Member : SquadSystem ? SquadSystem->GetMembers() : TArray<AOperativeCharacter*>())
	{
		const float Distance = CombatPressureLocal::IsLiving(Member) ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Enemy->GetActorLocation())) : MAX_flt;
		if (Distance < Best)
		{
			Best = Distance;
			Nearest = Member;
		}
	}
	const bool bRanged = AmbushPlanRules::IsRanged(Each.Type);
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambusher %s strikes (%s; %s%s; nearest operative %.0f cm)"), *Enemy->GetName(), Why,
		AmbushPlanRules::PartyName(Each.Party), bRanged ? TEXT(", suppresses") : TEXT(""), Best);
	if (Enemy->IsOnPatrol())
	{
		Enemy->BreakPatrol(EPatrolAlertCause::Sight, Nearest ? Nearest->GetActorLocation() : Enemy->GetActorLocation());
	}
	// Answer 2 (2026-11-01): a ranged ambusher suppresses the squad (its rounds count deliberate) while the others flank.
	if (bRanged)
	{
		Each.bSuppressor = true;
		if (USuppressionSubsystem* Suppression = USuppressionSubsystem::Get(Enemy))
		{
			Suppression->SetSuppressIntent(Enemy, Nearest);
		}
	}
}

void UCombatPressureSubsystem::Spring(int32 Group, const TCHAR* Why)
{
	for (FManaged& Each : Managed)
	{
		if (Each.Group == Group)
		{
			SpringMember(Each, Why);
		}
	}
}

void UCombatPressureSubsystem::TickAmbushers(float Step)
{
	const TArray<FVector2D> Squad = GatherSquadPositions();
	const TArray<FAmbushWatcher> Watchers = GatherWatchers();
	// Single-spot groups (and the parties of a dissolved plan).
	TSet<int32> Groups;
	for (const FManaged& Each : Managed)
	{
		if (Each.Kind == EPressureSpawnKind::Ambush && Each.Plan == 0 && Each.AmbushState != EPressureAmbushState::Sprung)
		{
			Groups.Add(Each.Group);
		}
	}
	for (const int32 Group : Groups)
	{
		// Engaged otherwise (shot at, spotted, a mate down): the whole group fights.
		const AEnemyCharacter* First = nullptr;
		bool bEngaged = false;
		for (const FManaged& Each : Managed)
		{
			if (Each.Group == Group && Each.Enemy.IsValid())
			{
				First = First ? First : Each.Enemy.Get();
				bEngaged |= !Each.Enemy->IsOnPatrol();
			}
		}
		if (!First)
		{
			continue;
		}
		if (bEngaged)
		{
			Spring(Group, TEXT("engaged"));
			continue;
		}
		const EAmbushDecision Decision = AmbushWatchRules::Decide(Watchers, Squad, CombatPressureLocal::To2D(First->GetActorLocation()), Config.Watch);
		if (Decision == EAmbushDecision::Spring)
		{
			++SprungByWatch;
			Spring(Group, TEXT("its side is unwatched"));
			continue;
		}
		for (FManaged& Each : Managed)
		{
			if (Each.Group != Group)
			{
				continue;
			}
			const EPressureAmbushState Was = Each.AmbushState;
			Each.AmbushState = Decision == EAmbushDecision::Hold ? EPressureAmbushState::Holding : EPressureAmbushState::Hidden;
			Each.HoldSeconds += Decision == EAmbushDecision::Hold ? Step : 0.f;
			if (Was != Each.AmbushState && Each.AmbushState == EPressureAmbushState::Holding && Each.Enemy.Get() == First)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d holds: the squad is in range but looks its way"), Group);
			}
		}
		// Answer 3 (2026-11-01): bypassed, it moves ahead of the squad unseen.
		if (Decision == EAmbushDecision::Wait)
		{
			TickRelocation(Group, Step, Squad);
		}
		else
		{
			GroupBypassSeconds.Remove(Group);
		}
	}
	for (FPlan& Plan : Plans)
	{
		if (!Plan.Info.bDissolved)
		{
			TickPlan(Plan, Step, Squad, Watchers);
		}
	}
	TickSuppressors();
}

void UCombatPressureSubsystem::TickPlan(FPlan& Plan, float Step, const TArray<FVector2D>& Squad, const TArray<FAmbushWatcher>& Watchers)
{
	FPressurePlanInfo& Info = Plan.Info;
	const FAmbushPlanParams& Params = Config.Coordinated;
	FAmbushPlanTick Tick;
	Tick.Phase = Info.Phase;
	Tick.SecondsInPhase = Plan.PhaseSeconds;
	Tick.NearestSquadCm = AmbushWatchRules::NearestDistance(Squad, Info.Anchor);
	int32 Unsprung = 0;
	TSet<int32> UnwatchedFlankGroups;
	for (const FManaged& Each : Managed)
	{
		const AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Plan != Info.Id || !CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			continue;
		}
		const FVector2D Here = CombatPressureLocal::To2D(Enemy->GetActorLocation());
		if (AmbushPlanRules::IsFlank(Each.Party))
		{
			++Tick.FlankMembersAlive;
			Tick.FlankMembersInPlace += AmbushWatchRules::NearestDistance(Squad, Here) <= Params.BlockDistanceCm ? 1 : 0;
		}
		if (Each.AmbushState == EPressureAmbushState::Sprung)
		{
			continue;
		}
		++Unsprung;
		Tick.bAnyEngaged |= !Enemy->IsOnPatrol();
		if (AmbushPlanRules::IsFlank(Each.Party) && !AmbushWatchRules::IsWatched(Watchers, Here, Config.Watch))
		{
			UnwatchedFlankGroups.Add(Each.Group);
		}
	}
	if (Unsprung == 0)
	{
		return; // every member fights (or none is left)
	}
	Tick.FlanksUnwatched = UnwatchedFlankGroups.Num();

	// Bypassed while waiting (answer 3): the plan is laid out afresh round the squad, its parties move there unseen.
	if (Info.Phase == EAmbushPlanPhase::Waiting && !Tick.bAnyEngaged)
	{
		const FVector2D Leader2D = CombatPressureLocal::To2D(LeaderFeet());
		const float Angle = AmbushWatchRules::AngleOffDeg(CombatPressureLocal::YawOf(SquadHeading()), AmbushWatchRules::BearingDeg(Leader2D, Info.Anchor));
		const bool bBypassed = AmbushRelocateRules::IsBypassed(Tick.NearestSquadCm, Angle, Params.TriggerRangeCm, Config.Relocate);
		Plan.BypassedSeconds = AmbushRelocateRules::StepBypassedSeconds(bBypassed, Plan.BypassedSeconds, Step);
		bool bSeen = false;
		for (const int32 Group : Info.PartyGroups)
		{
			bSeen |= IsGroupSeen(Group);
		}
		if (AmbushRelocateRules::Decide(Plan.BypassedSeconds, bSeen, Config.Relocate) == EAmbushRelocateDecision::Relocate)
		{
			Plan.BypassedSeconds = 0.f;
			FPressurePlanInfo Fresh;
			if (PlanLayout(Fresh))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d bypassed: re-laid round the squad (anchor %s -> %s)"), Info.Id,
					*Info.Anchor.ToString(), *Fresh.Anchor.ToString());
				const FVector2D Heading2D = CombatPressureLocal::To2D(SquadHeading()).GetSafeNormal();
				for (int32 Slot = 0; Slot < 4 && Slot < Info.PartyGroups.Num(); ++Slot)
				{
					const FVector2D To = CombatPressureLocal::To2D(Fresh.PartySpots[Slot]);
					const float Distance = static_cast<float>(FVector2D::Distance(Leader2D, To));
					const float AheadCos = Distance > 1.f ? static_cast<float>(FVector2D::DotProduct(Heading2D, (To - Leader2D) / Distance)) : 1.f;
					MoveGroup(Info.PartyGroups[Slot], Fresh.PartySpots[Slot], Fresh.PartyKinds[Slot], AheadCos, LeaderFeet());
				}
				Info.Anchor = Fresh.Anchor;
				Info.HeadingYawDeg = Fresh.HeadingYawDeg;
				Info.PartySpots = Fresh.PartySpots;
				Info.PartyKinds = Fresh.PartyKinds;
				++Info.Relocations;
			}
			else
			{
				// No layout round the squad here: the parties go on as single groups (each relocates on its own).
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d bypassed, no layout round the squad: its parties go on as single groups"), Info.Id);
				Info.bDissolved = true;
				for (FManaged& Each : Managed)
				{
					if (Each.Plan == Info.Id)
					{
						Each.Plan = 0;
						Each.Party = EAmbushParty::Single;
					}
				}
			}
			return;
		}
	}
	else
	{
		Plan.BypassedSeconds = 0.f;
	}

	const EAmbushPlanPhase Next = AmbushPlanRules::NextPhase(Tick, Params);
	if (Next != Info.Phase)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d: %s -> %s at %.1f s (squad %.0f cm from the anchor, %d flank sides unwatched, engaged %d, flanks in place %d / %d)"),
			Info.Id, AmbushPlanRules::PhaseName(Info.Phase), AmbushPlanRules::PhaseName(Next), Clock, Tick.NearestSquadCm, Tick.FlanksUnwatched,
			Tick.bAnyEngaged ? 1 : 0, Tick.FlankMembersInPlace, Tick.FlankMembersAlive);
		if (Next == EAmbushPlanPhase::FlankBlock && Info.FlankAtClock < 0.f)
		{
			Info.FlankAtClock = Clock;
			SprungByWatch += Tick.bAnyEngaged ? 0 : 1;
		}
		if (Next == EAmbushPlanPhase::Assault && Info.AssaultAtClock < 0.f)
		{
			Info.AssaultAtClock = Clock;
		}
		Info.Phase = Next;
		Plan.PhaseSeconds = 0.f;
	}
	else
	{
		Plan.PhaseSeconds += Step;
	}
	Info.HoldSeconds += Info.Phase == EAmbushPlanPhase::Holding ? Step : 0.f;
	for (FManaged& Each : Managed)
	{
		const AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Plan != Info.Id || Each.AmbushState == EPressureAmbushState::Sprung || !CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			continue;
		}
		if (!Enemy->IsOnPatrol())
		{
			SpringMember(Each, TEXT("engaged"));
		}
		else if (AmbushPlanRules::Springs(Info.Phase, Each.Party, AmbushPlanRules::IsRanged(Each.Type)))
		{
			SpringMember(Each, Info.Phase == EAmbushPlanPhase::FlankBlock ? TEXT("the plan blocks the flanks") : TEXT("the plan's assault"));
		}
		else
		{
			Each.AmbushState = Info.Phase == EAmbushPlanPhase::Holding ? EPressureAmbushState::Holding : EPressureAmbushState::Hidden;
			Each.HoldSeconds += Info.Phase == EAmbushPlanPhase::Holding ? Step : 0.f;
		}
	}
}

void UCombatPressureSubsystem::TickRelocation(int32 Group, float Step, const TArray<FVector2D>& Squad)
{
	const AEnemyCharacter* First = nullptr;
	for (const FManaged& Each : Managed)
	{
		if (Each.Group == Group && CombatPressureLocal::IsLivingEnemy(Each.Enemy.Get()))
		{
			First = Each.Enemy.Get();
			break;
		}
	}
	if (!First || Squad.IsEmpty())
	{
		return;
	}
	const FVector2D Here = CombatPressureLocal::To2D(First->GetActorLocation());
	const FVector2D Leader2D = CombatPressureLocal::To2D(LeaderFeet());
	const float Angle = AmbushWatchRules::AngleOffDeg(CombatPressureLocal::YawOf(SquadHeading()), AmbushWatchRules::BearingDeg(Leader2D, Here));
	const float Nearest = AmbushWatchRules::NearestDistance(Squad, Here);
	float& Seconds = GroupBypassSeconds.FindOrAdd(Group);
	Seconds = AmbushRelocateRules::StepBypassedSeconds(AmbushRelocateRules::IsBypassed(Nearest, Angle, Config.Watch.StrikeRangeCm, Config.Relocate), Seconds, Step);
	const EAmbushRelocateDecision Decision = AmbushRelocateRules::Decide(Seconds, Seconds >= Config.Relocate.AfterSeconds && IsGroupSeen(Group), Config.Relocate);
	if (Decision != EAmbushRelocateDecision::Relocate)
	{
		return;
	}
	Seconds = 0.f; // a failed search retries after another AfterSeconds
	const TOptional<FHideChoice> Hide = FindHideSpot(Config.AmbushMinDistanceCm, Config.AmbushMaxDistanceCm, Config.Relocate.MinAheadCos, { Here },
		CombatPressureLocal::RelocateAvoidCm);
	if (!Hide.IsSet() || !AmbushRelocateRules::IsGoodDestination(Hide->AheadCos, !IsVisibleToSquad(Hide->Feet), Config.Relocate))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d bypassed (%.0f cm away, %.0f deg off the squad's way): no concealed spot ahead yet"), Group,
			Nearest, Angle);
		return;
	}
	MoveGroup(Group, Hide->Feet, Hide->Kind, Hide->AheadCos, LeaderFeet());
}

FPressureRelocationRecord UCombatPressureSubsystem::MoveGroup(int32 Group, const FVector& Feet, EAmbushSpotKind Kind, float AheadCos, const FVector& FaceTowards)
{
	FPressureRelocationRecord Record;
	Record.Group = Group;
	Record.To = Feet;
	Record.Kind = Kind;
	Record.AheadCos = AheadCos;
	Record.Clock = Clock;
	Record.bSeenAtMove = IsGroupSeen(Group);
	Record.bDestinationConcealed = !IsVisibleToSquad(Feet);
	TArray<AEnemyCharacter*> Members;
	for (const FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Group == Group && Each.AmbushState != EPressureAmbushState::Sprung && CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			Record.Plan = Each.Plan;
			Members.Add(Enemy);
		}
	}
	if (Members.IsEmpty())
	{
		return Record;
	}
	Record.From = CombatPressureLocal::FeetOf(*Members[0]);
	TArray<FVector> Spots = GroupSpots(Feet, Members.Num(), true);
	if (Spots.Num() < Members.Num())
	{
		Spots = GroupSpots(Feet, Members.Num(), false);
	}
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		AEnemyCharacter* Enemy = Members[Index];
		const FVector Spot = Spots[FMath::Min(Index, Spots.Num() - 1)] + (Index >= Spots.Num() ? FVector(60.f * Index, 0.f, 0.f) : FVector::ZeroVector);
		const FVector Facing = (FaceTowards - Spot).GetSafeNormal2D();
		const FRotator Rotation = Facing.IsNearlyZero() ? Enemy->GetActorRotation() : Facing.Rotation();
		const FVector Centre = Spot + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight() + 2.f);
		Enemy->SetActorLocationAndRotation(Centre, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->MoveGuardPost(Centre, static_cast<float>(Rotation.Yaw));
	}
	Record.Members = Members.Num();
	Relocations.Add(Record);
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d relocates unseen (seen %d): %s -> %s (%s spot, ahead %.2f, concealed %d, %d members)"), Group,
		Record.bSeenAtMove ? 1 : 0, *Record.From.ToCompactString(), *Record.To.ToCompactString(), AmbushSpotRules::KindName(Kind), AheadCos,
		Record.bDestinationConcealed ? 1 : 0, Record.Members);
	return Record;
}

void UCombatPressureSubsystem::TickSuppressors()
{
	USuppressionSubsystem* Suppression = USuppressionSubsystem::Get(this);
	const USquadSubsystem* SquadSystem = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Suppression || !SquadSystem)
	{
		return;
	}
	for (const FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (!Each.bSuppressor || !CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			continue;
		}
		// Its current victim when that is an operative, else the nearest one: the rounds it fires at him count deliberate.
		AActor* Target = Cast<AOperativeCharacter>(Enemy->GetCurrentTarget());
		if (!Target || !CombatPressureLocal::IsLiving(Cast<AOperativeCharacter>(Target)))
		{
			Target = nullptr;
			float Best = MAX_flt;
			for (AOperativeCharacter* Member : SquadSystem->GetMembers())
			{
				const float Distance = CombatPressureLocal::IsLiving(Member) ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Enemy->GetActorLocation())) : MAX_flt;
				if (Distance < Best)
				{
					Best = Distance;
					Target = Member;
				}
			}
		}
		if (Target && Suppression->GetSuppressIntent(Enemy) != Target)
		{
			Suppression->SetSuppressIntent(Enemy, Target);
		}
	}
}

void UCombatPressureSubsystem::TickRecycle(float Step)
{
	const TArray<FVector2D> Squad = GatherSquadPositions();
	if (Squad.IsEmpty())
	{
		return;
	}
	for (FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (!CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			continue;
		}
		const float Nearest = AmbushWatchRules::NearestDistance(Squad, CombatPressureLocal::To2D(Enemy->GetActorLocation()));
		Each.FarSeconds = Nearest > Config.RecycleDistanceCm ? Each.FarSeconds + Step : 0.f;
		const bool bEngaged = !Enemy->IsOnPatrol();
		if (Each.FarSeconds > 0.f && CombatPressureRules::ShouldRecycle(bEngaged, IsVisibleToSquad(CombatPressureLocal::FeetOf(*Enemy)), Nearest, Each.FarSeconds, Config))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] %s left far behind (%.0f cm for %.0f s): removed"), *Enemy->GetName(), Nearest, Each.FarSeconds);
			Enemy->Destroy();
		}
	}
}
