// The combat lab's endless enemy pressure: patrols and ambushers (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md).
// UE-only, no Godot reference. Pure rules: CombatPressureRules / AmbushWatchRules (CodexTactics.Combat.Pressure.*, CodexTactics.AI.AmbushWatch.*).

#include "Combat/CombatPressureSubsystem.h"

#include "AI/PatrolRouteActor.h"
#include "AI/PerceptionRules.h"
#include "Algo/Reverse.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
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

	FVector2D To2D(const FVector& V) { return FVector2D(V.X, V.Y); }

	bool IsLiving(const AOperativeCharacter* Member)
	{
		return Member && Member->HealthComponent && Member->HealthComponent->IsAlive();
	}

	bool IsLivingEnemy(const AEnemyCharacter* Enemy)
	{
		return Enemy && !Enemy->IsDying() && !Enemy->IsActorBeingDestroyed();
	}

	/** Perception muted to nothing (a hidden ambusher waits for the watch rule, not for its own eyes / ears / nose). */
	FEnemyPerceptionParams Muted(const FEnemyPerceptionParams& Params)
	{
		FEnemyPerceptionParams Out = PerceptionRules::Scaled(Params, 0.f);
		Out.SmellDetectCm = 0.f;
		return Out;
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
	Stream.Initialize(Config.Seed);
	Clock = 0.f;
	DueAt = Config.FirstEventSeconds;
	if (IsActive())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] on: every %.0f s (first at %.0f s), groups %d-%d, max alive %d, patrols %.0f %%, seed %d, wall clearance %.0f cm"),
			Config.IntervalSeconds, Config.FirstEventSeconds, Config.GroupMin, Config.GroupMax, Config.MaxAlive, Config.PatrolShare * 100.f, Config.Seed,
			Config.WallClearanceCm);
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
			const float Body = static_cast<float>(Member->GetActorRotation().Yaw);
			Out.Add({ Here, Body });
			// His formation sector look (FORMATION_SECTORS.md): where his eyes go while the body turns there.
			if (const TOptional<float> Sector = Member->GetFormationLookYaw(); Sector.IsSet() && AmbushWatchRules::AngleOffDeg(Body, Sector.GetValue()) > 5.f)
			{
				Out.Add({ Here, Sector.GetValue() });
			}
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
	const float HeadingYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X)));
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

TArray<FVector> UCombatPressureSubsystem::GroupSpots(const FVector& Feet, int32 Count, bool bConcealed) const
{
	TArray<FVector> Out = { Feet };
	const TArray<FVector2D> Squad = GatherSquadPositions();
	const FVector2D Center = CombatPressureLocal::To2D(Feet);
	for (int32 Index = 1; Out.Num() < Count && Index < 12; ++Index)
	{
		const FVector2D Point = Center + CombatPressureRules::GroupOffset(Index, CombatPressureLocal::GroupSpacingCm);
		if (CombatPressureRules::CheckSpawnPoint(Point, Obstacles, Config.WallClearanceCm, Squad, 0.f, Bounds) != ESpawnPointVerdict::Ok)
		{
			continue;
		}
		if (const TOptional<FVector> OnNav = ProjectFeet(Point, static_cast<float>(Feet.Z));
			OnNav.IsSet() && CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Obstacles) >= Config.WallClearanceCm
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
	int32 Group, bool bPlacedByCaller)
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
	Entry.Group = Group;
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
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] spawned %s %s (group %d) at %s: clearance %.0f cm, squad %.0f cm, concealed %d, watched %d"),
		Kind == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambusher"), *UEnum::GetValueAsString(Type), Group, *Feet.ToCompactString(),
		Record.ClearanceCm, Record.SquadDistanceCm, Record.bConcealed ? 1 : 0, Record.bWatched ? 1 : 0);
	return Enemy;
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnPatrolGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types)
{
	return SpawnPatrolGroup(Feet, Types, true);
}

TArray<AEnemyCharacter*> UCombatPressureSubsystem::SpawnAmbushGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards)
{
	return SpawnAmbushGroup(Feet, Types, FaceTowards, true);
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
		AEnemyCharacter* Enemy = SpawnOne(Types[Index], Spots[Index], Ahead, EPressureSpawnKind::Patrol, Group, bPlacedByCaller);
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
	bool bPlacedByCaller)
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
		AEnemyCharacter* Enemy = SpawnOne(Types[Index], Spots[Index], FaceTowards, EPressureSpawnKind::Ambush, Group, bPlacedByCaller);
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
	const int32 Room = bIgnoreCap ? Config.GroupMax : FMath::Max(0, Config.MaxAlive - GetAliveCount());
	const int32 Size = Count > 0 ? FMath::Min(Count, FMath::Max(Room, bIgnoreCap ? Count : 0))
		: CombatPressureRules::GroupSize(Stream.FRand(), Config.GroupMin, Config.GroupMax, bIgnoreCap ? 0 : GetAliveCount(), bIgnoreCap ? Config.GroupMax : Config.MaxAlive);
	if (Size <= 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no room for an event (%d of %d alive)"), GetAliveCount(), Config.MaxAlive);
		return Out;
	}
	EPressureSpawnKind Picked = Kind.IsSet() ? Kind.GetValue() : CombatPressureRules::PickKind(Stream.FRand(), Config.PatrolShare);
	TArray<EEnemyArchetype> Types;
	for (int32 Index = 0; Index < Size; ++Index)
	{
		Types.Add(CombatPressureRules::PickType(Config.TypeWeights, Stream.FRand()));
	}
	TOptional<FVector> Spot = FindSpot(Picked);
	if (!Spot.IsSet() && Picked == EPressureSpawnKind::Ambush)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no concealed ambush spot near the squad: a patrol instead"));
		Picked = EPressureSpawnKind::Patrol;
		Spot = FindSpot(Picked);
	}
	if (!Spot.IsSet())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] no valid spawn spot this time"));
		return Out;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] event at %.0f s: %s of %d"), Clock, Picked == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambush"), Size);
	Out = Picked == EPressureSpawnKind::Patrol ? SpawnPatrolGroup(Spot.GetValue(), Types, false)
		: SpawnAmbushGroup(Spot.GetValue(), Types, LeaderFeet(), false);
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

void UCombatPressureSubsystem::Spring(int32 Group, const TCHAR* Why)
{
	const TArray<FVector2D> Squad = GatherSquadPositions();
	const USquadSubsystem* SquadSystem = GetWorld()->GetSubsystem<USquadSubsystem>();
	for (FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Group != Group || Each.AmbushState == EPressureAmbushState::Sprung || !Enemy)
		{
			continue;
		}
		Each.AmbushState = EPressureAmbushState::Sprung;
		Enemy->bOverridePerception = false; // its own senses again (the archetype's data)
		// Strike at the nearest operative: Patrol -> Engage, which starts the fight / joins the skirmish (NotifyHostileContact).
		FVector Target = Enemy->GetActorLocation();
		float Best = MAX_flt;
		for (const AOperativeCharacter* Member : SquadSystem ? SquadSystem->GetMembers() : TArray<AOperativeCharacter*>())
		{
			const float Distance = CombatPressureLocal::IsLiving(Member) ? static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Enemy->GetActorLocation())) : MAX_flt;
			if (Distance < Best)
			{
				Best = Distance;
				Target = Member->GetActorLocation();
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambusher %s strikes (%s; nearest operative %.0f cm)"), *Enemy->GetName(), Why, Best);
		if (Enemy->IsOnPatrol())
		{
			Enemy->BreakPatrol(EPatrolAlertCause::Sight, Target);
		}
	}
}

void UCombatPressureSubsystem::TickAmbushers(float Step)
{
	const TArray<FVector2D> Squad = GatherSquadPositions();
	const TArray<FAmbushWatcher> Watchers = GatherWatchers();
	TSet<int32> Groups;
	for (const FManaged& Each : Managed)
	{
		if (Each.Kind == EPressureSpawnKind::Ambush && Each.AmbushState != EPressureAmbushState::Sprung)
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
		if (Each.FarSeconds > 0.f && CombatPressureRules::ShouldRecycle(bEngaged, IsVisibleToSquad(Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight())),
			Nearest, Each.FarSeconds, Config))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] %s left far behind (%.0f cm for %.0f s): removed"), *Enemy->GetName(), Nearest, Each.FarSeconds);
			Enemy->Destroy();
		}
	}
}
