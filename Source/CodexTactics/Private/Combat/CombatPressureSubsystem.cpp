// The combat lab's endless enemy pressure: patrols and ambushers (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md).
// Since the designer's decisions of 2026-11-01 night: a bypassed ambush RUNS a concealed route on foot (no teleport), an ambush watched all along
// strikes after its 60-90 s hold limit, hiding spots hug the wall (0.8 m).
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
#include "Combat/TacticalSightSubsystem.h"
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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
	/** Routes evaluated per relocation search (the best hiding spots ahead by score). */
	constexpr int32 RelocateRoutes = 8;
	/** A runner within this of his leg's end turns onto the next leg, cm (the guard post's own stop radius is 80). */
	constexpr float RunLegAcceptCm = 150.f;
	/** ... within this of his final spot he has arrived, cm. */
	constexpr float RunArriveCm = 120.f;
	/** A following patrol re-aims at the squad this often, s; its post moves when the squad moved this far, cm. */
	constexpr float FollowStepSeconds = 2.f;
	constexpr float FollowRepostCm = 300.f;
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
#if !UE_BUILD_SHIPPING
	// Training Ground bot batches (Scripts/bot_run.ps1 -ReportDir): every run of a batch gets its own pressure seed.
	int32 SeedOverride = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("PressureSeed="), SeedOverride))
	{
		Config.Seed = SeedOverride;
	}
#endif
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

void UCombatPressureSubsystem::SetHoldLimitOverride(float MinSeconds, float MaxSeconds)
{
	HoldMinOverride = MinSeconds;
	HoldMaxOverride = MaxSeconds;
}

float UCombatPressureSubsystem::DrawHoldLimit(int32 Key) const
{
	FAmbushWatchParams Params = Config.Watch;
	if (HoldMaxOverride >= 0.f)
	{
		Params.HoldStrikeMinSeconds = FMath::Max(0.f, HoldMinOverride);
		Params.HoldStrikeMaxSeconds = HoldMaxOverride;
	}
	// Its own stream per ambush: the seeded event stream (kinds, types, spots) stays exactly as before.
	FRandomStream Draw(Config.Seed * 7919 + Key);
	return AmbushWatchRules::HoldLimitSeconds(Draw.FRand(), Params);
}

float UCombatPressureSubsystem::GetHoldLimit(const AEnemyCharacter* Enemy) const
{
	for (const FManaged& Each : Managed)
	{
		if (Each.Enemy.Get() != Enemy || Each.Kind != EPressureSpawnKind::Ambush)
		{
			continue;
		}
		if (Each.Plan > 0)
		{
			FPressurePlanInfo Info;
			return GetPlanInfo(Each.Plan, Info) ? Info.HoldLimitSeconds : 0.f;
		}
		const float* Limit = GroupHoldLimit.Find(Each.Group);
		return Limit ? *Limit : 0.f;
	}
	return 0.f;
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
	const auto HasGroup = [this](int32 Group) { return Managed.ContainsByPredicate([Group](const FManaged& Each) { return Each.Group == Group; }); };
	for (auto It = GroupBypassSeconds.CreateIterator(); It; ++It)
	{
		if (!HasGroup(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = GroupNoRouteSearches.CreateIterator(); It; ++It)
	{
		if (!HasGroup(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = FollowerGroups.CreateIterator(); It; ++It)
	{
		if (!HasGroup(*It))
		{
			It.RemoveCurrent();
		}
	}
	Runs.RemoveAll([&HasGroup](const FRun& Run) { return !HasGroup(Run.Group); });
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
	GroupNoRouteSearches.Reset();
	FollowerGroups.Reset();
	Runs.Reset();
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

bool UCombatPressureSubsystem::IsPointInSquadSight(const FVector& Target) const
{
	const UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	if (!Squad)
	{
		return false;
	}
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!CombatPressureLocal::IsLiving(Member))
		{
			continue;
		}
		// His real eyes (the stance height over his capsule: a man on a tower / roof looks from up there), any direction.
		const FVector Eye = UTacticalSightSubsystem::EyePoint(*Member);
		// Its range grows with his height level (one vision model, 2026-11-02: UTacticalSightSubsystem::GetSightRangeCm).
		if (FVector::Dist(Eye, Target) > (Sight ? Sight->GetSightRangeCm(*Member) : UTacticalSightSubsystem::MaxSightCm))
		{
			continue;
		}
		if (Sight ? Sight->HasClearSight(Eye, Target)
			: !World->LineTraceTestByObjectType(Eye, Target, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CombatPressureRoute), false)))
		{
			return true;
		}
	}
	return false;
}

bool UCombatPressureSubsystem::IsSpottedBySquad(const AEnemyCharacter& Enemy) const
{
	const UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	if (!Squad || !Sight)
	{
		return Squad && IsVisibleToSquad(CombatPressureLocal::FeetOf(Enemy));
	}
	const FVector Profile = UTacticalSightSubsystem::ProfilePoint(Enemy);
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (CombatPressureLocal::IsLiving(Member) && Sight->CanOperativeSee(*Member, Profile))
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
			Box.HeightCm = static_cast<float>(Local.GetSize().Z * Scale.Z);
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

TArray<UCombatPressureSubsystem::FHideChoice> UCombatPressureSubsystem::RankHideSpots(float MinDistanceCm, float MaxDistanceCm, float MinAheadCos,
	const TArray<FVector2D>& Avoid, float AvoidCm, TArray<float>* OutScores)
{
	TArray<FHideChoice> Out;
	const TArray<FVector2D> Squad = GatherSquadPositions();
	if (!GetWorld() || Squad.IsEmpty())
	{
		return Out;
	}
	const TArray<FPressureBox>& Boxes = GetObstacleBoxes();
	const TArray<FAmbushSpot>& Spots = GetHideSpots();
	const TArray<FAmbushWatcher> Watchers = GatherWatchers();
	const FVector Feet = LeaderFeet();
	const FVector2D Leader2D = CombatPressureLocal::To2D(Feet);
	const FVector2D Heading2D = CombatPressureLocal::To2D(SquadHeading()).GetSafeNormal();
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
		if (Distance < MinDistanceCm || Distance > MaxDistanceCm
			|| CombatPressureRules::CheckSpawnPoint(Spot.Position, Boxes, Clearance, Squad, MinDistanceCm, Bounds) != ESpawnPointVerdict::Ok
			|| Avoid.ContainsByPredicate([&Spot, AvoidCm](const FVector2D& Each) { return FVector2D::Distance(Each, Spot.Position) < AvoidCm; }))
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
			continue;
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
		Candidate.Score = CombatPressureRules::ScoreSpot(Input);
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	for (const FCandidate& Each : Candidates)
	{
		Out.Add(Each.Choice);
		if (OutScores)
		{
			OutScores->Add(Each.Score);
		}
	}
	return Out;
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
		// Hugging a wall (decision 3, the 0.8 m spots) a mate's spot could lie on the wall's far side: only spots with a clear walk from the group's point.
		if (const TOptional<FVector> OnNav = ProjectFeet(Point, static_cast<float>(Feet.Z));
			OnNav.IsSet() && CombatPressureRules::Clearance(CombatPressureLocal::To2D(OnNav.GetValue()), Obstacles) >= Clearance
			&& (!bConcealed || !IsVisibleToSquad(OnNav.GetValue()))
			&& !GetWorld()->LineTraceTestByObjectType(Feet + FVector(0.f, 0.f, 60.f), OnNav.GetValue() + FVector(0.f, 0.f, 60.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(CombatPressureGroupSpot), false)))
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
	// Decision 2 (2026-11-01 night): watched all along, it strikes anyway after its own 60-90 s (a plan keeps one limit for all its parties).
	if (Tags.Plan == 0)
	{
		GroupHoldLimit.Add(Group, DrawHoldLimit(Group));
	}
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
	Info.HoldLimitSeconds = DrawHoldLimit(100000 + Info.Id);
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
		if (Each.Kind == EPressureSpawnKind::Ambush && Each.Plan == 0 && Each.AmbushState != EPressureAmbushState::Sprung
			&& Each.AmbushState != EPressureAmbushState::Relocating)
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
		float Held = 0.f;
		for (FManaged& Each : Managed)
		{
			if (Each.Group != Group)
			{
				continue;
			}
			const EPressureAmbushState Was = Each.AmbushState;
			Each.AmbushState = Decision == EAmbushDecision::Hold ? EPressureAmbushState::Holding : EPressureAmbushState::Hidden;
			Each.HoldSeconds += Decision == EAmbushDecision::Hold ? Step : 0.f;
			Held = FMath::Max(Held, Each.HoldSeconds);
			if (Was != Each.AmbushState && Each.AmbushState == EPressureAmbushState::Holding && Each.Enemy.Get() == First)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d holds: the squad is in range but looks its way"), Group);
			}
		}
		// Decision 2 (2026-11-01 night): a squad that watches every side still gets attacked - after the group's own hold limit.
		const float* Limit = GroupHoldLimit.Find(Group);
		if (Decision == EAmbushDecision::Hold && Limit && AmbushWatchRules::HeldTooLong(Held, *Limit))
		{
			++SprungByHold;
			UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d held %.1f s while watched (its limit %.1f s): it strikes anyway"), Group, Held, *Limit);
			Spring(Group, TEXT("held too long, watched all along"));
			continue;
		}
		// Answer 3 / decision 1 (2026-11-01): bypassed, it runs a concealed route to a spot ahead of the squad (no route: it stays / follows).
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
	TickRuns(Step);
	FollowTimer += Step;
	if (FollowTimer >= CombatPressureLocal::FollowStepSeconds)
	{
		FollowTimer = 0.f;
		TickFollowers();
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
	Tick.HoldSeconds = Info.HoldSeconds;
	Tick.HoldLimitSeconds = Info.HoldLimitSeconds;
	// A runner of a party spotted on its relocation route: the whole plan springs (the flank block, then the assault).
	Tick.bAnyEngaged |= Info.bSpottedOnRoute;
	bool bRunning = false;
	for (const int32 Group : Info.PartyGroups)
	{
		bRunning |= IsGroupRunning(Group);
	}
	if (bRunning && !Tick.bAnyEngaged)
	{
		return; // its parties are on their way to the new layout (decision 1): no phase change until they hide again
	}

	// Bypassed while waiting (answer 3; the hybrid of decision 1): the plan is laid out afresh round the squad, its parties RUN there by concealed
	// routes - all four or none; no concealed routes for long: it dissolves into single groups (each one searches / follows on its own).
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
			const bool bLayout = PlanLayout(Fresh);
			TArray<FRouteChoice> PartyRoutes;
			if (bLayout)
			{
				const FVector2D Heading2D = CombatPressureLocal::To2D(SquadHeading()).GetSafeNormal();
				for (int32 Slot = 0; Slot < 4 && Slot < Info.PartyGroups.Num(); ++Slot)
				{
					FRouteChoice& Route = PartyRoutes.AddDefaulted_GetRef();
					const FVector2D To = CombatPressureLocal::To2D(Fresh.PartySpots[Slot]);
					const float Distance = static_cast<float>(FVector2D::Distance(Leader2D, To));
					Route.Hide.Feet = Fresh.PartySpots[Slot];
					Route.Hide.Kind = Fresh.PartyKinds[Slot];
					Route.Hide.AheadCos = Distance > 1.f ? static_cast<float>(FVector2D::DotProduct(Heading2D, (To - Leader2D) / Distance)) : 1.f;
					Route.RoutesTried = 1;
					// A party already gone (killed) needs no route.
					if (!Managed.ContainsByPredicate([Group = Info.PartyGroups[Slot]](const FManaged& Each) { return Each.Group == Group; }))
					{
						Route.Candidate.bReachable = true;
						continue;
					}
					EvaluateRoute(Info.PartyGroups[Slot], Fresh.PartySpots[Slot], !IsVisibleToSquad(Fresh.PartySpots[Slot]), 0.f, Route.Candidate, Route.Path);
				}
			}
			const bool bAllConcealed = bLayout && !PartyRoutes.ContainsByPredicate([this](const FRouteChoice& Route)
			{
				return !AmbushRelocateRules::IsConcealedRoute(Route.Candidate, Config.Relocate);
			});
			if (bAllConcealed)
			{
				Plan.NoRouteSearches = 0;
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d bypassed: re-laid round the squad (anchor %s -> %s), its parties run there by concealed routes"),
					Info.Id, *Info.Anchor.ToString(), *Fresh.Anchor.ToString());
				for (int32 Slot = 0; Slot < PartyRoutes.Num(); ++Slot)
				{
					if (!PartyRoutes[Slot].Path.IsEmpty())
					{
						StartRun(Info.PartyGroups[Slot], Info.Id, PartyRoutes[Slot]);
					}
				}
				Info.Anchor = Fresh.Anchor;
				Info.HeadingYawDeg = Fresh.HeadingYawDeg;
				Info.PartySpots = Fresh.PartySpots;
				Info.PartyKinds = Fresh.PartyKinds;
				++Info.Relocations;
			}
			else if (bLayout && AmbushRelocateRules::NoRouteOutcome(++Plan.NoRouteSearches, Config.Relocate) == EAmbushNoRouteOutcome::Stay)
			{
				++NoRouteSearches;
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d bypassed: no concealed route for every party (search %d) - it stays put, no teleport"),
					Info.Id, Plan.NoRouteSearches);
			}
			else
			{
				// No layout round the squad here (or no concealed routes for long): the parties go on as single groups (each searches on its own).
				NoRouteSearches += bLayout ? 1 : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d bypassed, %s: its parties go on as single groups"), Info.Id,
					bLayout ? TEXT("no concealed routes") : TEXT("no layout round the squad"));
				Info.bDissolved = true;
				for (FManaged& Each : Managed)
				{
					if (Each.Plan == Info.Id)
					{
						Each.Plan = 0;
						Each.Party = EAmbushParty::Single;
						if (!GroupHoldLimit.Contains(Each.Group))
						{
							GroupHoldLimit.Add(Each.Group, DrawHoldLimit(Each.Group)); // a single group now: its own hold limit
						}
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
			// A flank side unwatched (the watch rule) or - decision 2 - watched all round for its whole hold limit (it strikes anyway).
			const bool bByHold = !Tick.bAnyEngaged && Tick.FlanksUnwatched == 0;
			SprungByWatch += !Tick.bAnyEngaged && !bByHold ? 1 : 0;
			SprungByHold += bByHold ? 1 : 0;
			if (bByHold)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] coordinated plan %d held %.1f s watched all round (its limit %.1f s): it strikes anyway"), Info.Id,
					Info.HoldSeconds, Info.HoldLimitSeconds);
			}
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
		else if (Each.AmbushState == EPressureAmbushState::Relocating)
		{
			continue; // still on its way to its new spot (it strikes from wherever it is at the assault)
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
	// Decision 1 (2026-11-01 night): a CONCEALED ROUTE on foot to a spot ahead - never a teleport.
	int32 Tried = 0;
	const TOptional<FRouteChoice> Route = FindRelocationRoute(Group, Tried);
	if (!Route.IsSet())
	{
		++NoRouteSearches;
		int32& Fails = GroupNoRouteSearches.FindOrAdd(Group);
		++Fails;
		const EAmbushNoRouteOutcome Outcome = AmbushRelocateRules::NoRouteOutcome(Fails, Config.Relocate);
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d bypassed (%.0f cm away, %.0f deg off the squad's way): no concealed route ahead (%d routes tried, search %d) - %s"),
			Group, Nearest, Angle, Tried, Fails, Outcome == EAmbushNoRouteOutcome::Patrol ? TEXT("it follows the squad as a patrol") : TEXT("it stays put"));
		if (Outcome == EAmbushNoRouteOutcome::Patrol)
		{
			ConvertToFollower(Group, TEXT("no concealed route"));
		}
		return;
	}
	GroupNoRouteSearches.Remove(Group);
	StartRun(Group, 0, Route.GetValue());
}

bool UCombatPressureSubsystem::IsGroupRunning(int32 Group) const
{
	return Runs.ContainsByPredicate([Group](const FRun& Run) { return Run.Group == Group; });
}

bool UCombatPressureSubsystem::EvaluateRoute(int32 Group, const FVector& Dest, bool bDestinationConcealed, float Score, FAmbushRouteCandidate& OutRoute,
	TArray<FVector>& OutPath) const
{
	OutRoute = FAmbushRouteCandidate();
	OutRoute.Score = Score;
	OutRoute.bDestinationConcealed = bDestinationConcealed;
	OutPath.Reset();
	const AEnemyCharacter* First = nullptr;
	for (const FManaged& Each : Managed)
	{
		if (Each.Group == Group && Each.AmbushState != EPressureAmbushState::Sprung && CombatPressureLocal::IsLivingEnemy(Each.Enemy.Get()))
		{
			First = Each.Enemy.Get();
			break;
		}
	}
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!First || !Nav)
	{
		return false;
	}
	// Its own navmesh path (its nav agent and filter: a mutant never paths through a ladder).
	const FVector Start = CombatPressureLocal::FeetOf(*First);
	UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Start + FVector(0.f, 0.f, 50.f), Dest + FVector(0.f, 0.f, 50.f), const_cast<AEnemyCharacter*>(First));
	if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
	{
		return true;
	}
	OutRoute.bReachable = true;
	TArray<FVector2D> Points;
	for (const FVector& Point : Path->PathPoints)
	{
		OutPath.Add(Point);
		Points.Add(CombatPressureLocal::To2D(Point));
	}
	OutRoute.PathCm = AmbushRelocateRules::PathLength(Points);
	if (OutRoute.PathCm > Config.Relocate.MaxRouteCm)
	{
		return true;
	}
	// The runner's body along the route: his profile point's height over his feet at every sample (the path's Z between its corners).
	const float Lift = static_cast<float>(UTacticalSightSubsystem::ProfilePoint(*First).Z - Start.Z);
	const TArray<FVector2D> Samples = AmbushRelocateRules::SamplePath(Points, Config.Relocate.RouteSampleCm);
	for (const FVector2D& Sample : Samples)
	{
		// The sample's ground height: on the path segment nearest to it.
		double Z = OutPath[0].Z;
		double Best = MAX_dbl;
		for (int32 Corner = 1; Corner < OutPath.Num(); ++Corner)
		{
			const FVector2D A = Points[Corner - 1];
			const FVector2D AB = Points[Corner] - A;
			const double Length2 = AB.SizeSquared();
			const double T = Length2 > 1.0 ? FMath::Clamp(FVector2D::DotProduct(Sample - A, AB) / Length2, 0.0, 1.0) : 0.0;
			const double Distance = FVector2D::Distance(A + AB * T, Sample);
			if (Distance < Best)
			{
				Best = Distance;
				Z = FMath::Lerp(OutPath[Corner - 1].Z, OutPath[Corner].Z, T);
			}
		}
		if (bRouteBlockedOverride || IsPointInSquadSight(FVector(Sample.X, Sample.Y, Z + Lift)))
		{
			OutRoute.SeenSamples = 1; // one seen sample is enough to refuse the route
			break;
		}
	}
	return true;
}

TOptional<UCombatPressureSubsystem::FRouteChoice> UCombatPressureSubsystem::FindRelocationRoute(int32 Group, int32& OutTried)
{
	OutTried = 0;
	const FManaged* First = Managed.FindByPredicate([Group](const FManaged& Each)
	{
		return Each.Group == Group && Each.AmbushState != EPressureAmbushState::Sprung && CombatPressureLocal::IsLivingEnemy(Each.Enemy.Get());
	});
	if (!First)
	{
		return {};
	}
	const FVector2D Here = CombatPressureLocal::To2D(First->Enemy->GetActorLocation());
	TArray<float> Scores;
	const TArray<FHideChoice> Spots = RankHideSpots(Config.AmbushMinDistanceCm, Config.AmbushMaxDistanceCm, Config.Relocate.MinAheadCos, { Here },
		CombatPressureLocal::RelocateAvoidCm, &Scores);
	TArray<FAmbushRouteCandidate> Candidates;
	TArray<TArray<FVector>> Paths;
	for (int32 Index = 0; Index < Spots.Num() && Index < CombatPressureLocal::RelocateRoutes; ++Index)
	{
		FAmbushRouteCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		TArray<FVector>& Path = Paths.AddDefaulted_GetRef();
		EvaluateRoute(Group, Spots[Index].Feet, AmbushRelocateRules::IsGoodDestination(Spots[Index].AheadCos, true, Config.Relocate), Scores[Index], Candidate, Path);
	}
	OutTried = Candidates.Num();
	const int32 Best = AmbushRelocateRules::PickRoute(Candidates, Config.Relocate);
	if (Best == INDEX_NONE)
	{
		int32 Unreachable = 0;
		int32 TooLong = 0;
		int32 Seen = 0;
		for (const FAmbushRouteCandidate& Each : Candidates)
		{
			Unreachable += Each.bReachable ? 0 : 1;
			TooLong += Each.bReachable && Each.PathCm > Config.Relocate.MaxRouteCm ? 1 : 0;
			Seen += Each.bReachable && Each.PathCm <= Config.Relocate.MaxRouteCm && (Each.SeenSamples > 0 || !Each.bDestinationConcealed) ? 1 : 0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] group %d: %d hiding spots ahead, %d routes tried - %d unreachable, %d longer than %.0f m, %d in an operative's sight"),
			Group, Spots.Num(), Candidates.Num(), Unreachable, TooLong, Config.Relocate.MaxRouteCm / 100.f, Seen);
		return {};
	}
	FRouteChoice Choice;
	Choice.Hide = Spots[Best];
	Choice.Path = Paths[Best];
	Choice.Candidate = Candidates[Best];
	Choice.RoutesTried = Candidates.Num();
	return Choice;
}

void UCombatPressureSubsystem::StartRun(int32 Group, int32 Plan, const FRouteChoice& Route)
{
	FRun Run;
	Run.Group = Group;
	Run.Plan = Plan;
	for (FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Group == Group && Each.AmbushState != EPressureAmbushState::Sprung && CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			Run.Members.Add(Enemy);
			Each.AmbushState = EPressureAmbushState::Relocating;
		}
	}
	if (Run.Members.IsEmpty() || Route.Path.Num() < 2)
	{
		return;
	}
	// Each runner: the route's corners, then his own spot round the new hiding spot (the group's spacing there).
	TArray<FVector> Finals = GroupSpots(Route.Hide.Feet, Run.Members.Num(), true);
	if (Finals.Num() < Run.Members.Num())
	{
		Finals = GroupSpots(Route.Hide.Feet, Run.Members.Num(), false);
	}
	float Speed = MAX_flt;
	for (int32 Index = 0; Index < Run.Members.Num(); ++Index)
	{
		AEnemyCharacter* Enemy = Run.Members[Index].Get();
		TArray<FVector>& Legs = Run.Legs.AddDefaulted_GetRef();
		for (int32 Corner = 1; Corner + 1 < Route.Path.Num(); ++Corner)
		{
			Legs.Add(Route.Path[Corner]);
		}
		Legs.Add(Finals[FMath::Min(Index, Finals.Num() - 1)]);
		Run.Leg.Add(0);
		Run.LastFeet.Add(CombatPressureLocal::FeetOf(*Enemy));
		// The group runs together at its slowest member's pace.
		Speed = FMath::Min(Speed, Enemy->GetBaseWalkSpeed() * Config.Relocate.RunSpeedScale);
	}
	Run.SpeedCmS = FMath::Max(150.f, Speed);
	Run.LimitSeconds = AmbushRelocateRules::RunLimitSeconds(Route.Candidate.PathCm, Run.SpeedCmS);
	for (int32 Index = 0; Index < Run.Members.Num(); ++Index)
	{
		AEnemyCharacter* Enemy = Run.Members[Index].Get();
		const FVector Leg = Run.Legs[Index][0];
		Enemy->MoveGuardPost(Leg + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()), CombatPressureLocal::YawOf(Leg - CombatPressureLocal::FeetOf(*Enemy)), Run.SpeedCmS);
	}
	FPressureRelocationRecord Record;
	Record.Group = Group;
	Record.Plan = Plan;
	Record.From = Route.Path[0];
	Record.To = Route.Hide.Feet;
	Record.Kind = Route.Hide.Kind;
	Record.AheadCos = Route.Hide.AheadCos;
	Record.bSeenAtMove = IsGroupSeen(Group);
	Record.bDestinationConcealed = !IsVisibleToSquad(Route.Hide.Feet);
	Record.Clock = Clock;
	Record.Members = Run.Members.Num();
	Record.PathCm = Route.Candidate.PathCm;
	Record.Samples = AmbushRelocateRules::SamplePath([&Route]()
	{
		TArray<FVector2D> Points;
		for (const FVector& Point : Route.Path)
		{
			Points.Add(CombatPressureLocal::To2D(Point));
		}
		return Points;
	}(), Config.Relocate.RouteSampleCm).Num();
	Record.SeenSamples = Route.Candidate.SeenSamples;
	Record.RoutesTried = Route.RoutesTried;
	Record.Path = Route.Path;
	Run.Record = Relocations.Add(Record);
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d%s runs a concealed route on foot: %s -> %s (%s spot, %.0f m, %d sight samples clear, %d routes tried, ahead %.2f, %d members at %.0f cm/s)"),
		Group, Plan > 0 ? *FString::Printf(TEXT(" (plan %d)"), Plan) : TEXT(""), *Record.From.ToCompactString(), *Record.To.ToCompactString(),
		AmbushSpotRules::KindName(Record.Kind), Record.PathCm / 100.f, Record.Samples, Record.RoutesTried, Record.AheadCos, Record.Members, Run.SpeedCmS);
	Runs.Add(MoveTemp(Run));
}

void UCombatPressureSubsystem::TickRuns(float Step)
{
	for (int32 RunIndex = Runs.Num() - 1; RunIndex >= 0; --RunIndex)
	{
		FRun& Run = Runs[RunIndex];
		Run.Elapsed += Step;
		FPressureRelocationRecord* Record = Relocations.IsValidIndex(Run.Record) ? &Relocations[Run.Record] : nullptr;
		bool bSpotted = false;
		bool bAllArrived = true;
		int32 Running = 0;
		for (int32 Index = 0; Index < Run.Members.Num(); ++Index)
		{
			AEnemyCharacter* Enemy = Run.Members[Index].Get();
			FManaged* Entry = Enemy ? Managed.FindByPredicate([Enemy](const FManaged& Each) { return Each.Enemy.Get() == Enemy; }) : nullptr;
			if (!Entry || !CombatPressureLocal::IsLivingEnemy(Enemy) || Entry->AmbushState != EPressureAmbushState::Relocating)
			{
				continue; // gone, or struck already (a plan's flank block)
			}
			++Running;
			const FVector Feet = CombatPressureLocal::FeetOf(*Enemy);
			if (Record)
			{
				Record->MaxStepCm = FMath::Max(Record->MaxStepCm, static_cast<float>(FVector::Dist2D(Feet, Run.LastFeet[Index])));
			}
			Run.LastFeet[Index] = Feet;
			// Shot at / perceived otherwise (it left its post) or seen by an operative: spotted on its way.
			bSpotted |= !Enemy->IsOnPatrol() || IsSpottedBySquad(*Enemy);
			TArray<FVector>& Legs = Run.Legs[Index];
			int32& Leg = Run.Leg[Index];
			while (Leg + 1 < Legs.Num() && FVector::Dist2D(Feet, Legs[Leg]) <= CombatPressureLocal::RunLegAcceptCm)
			{
				++Leg;
				Enemy->MoveGuardPost(Legs[Leg] + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()), CombatPressureLocal::YawOf(Legs[Leg] - Feet), Run.SpeedCmS);
			}
			bAllArrived &= Leg + 1 == Legs.Num() && FVector::Dist2D(Feet, Legs.Last()) <= CombatPressureLocal::RunArriveCm;
		}
		if (Running == 0)
		{
			// Every runner struck already (a plan's flank block / assault) or is gone: the run is over.
			if (Record && Record->Outcome == EAmbushRunStep::Running)
			{
				Record->Outcome = EAmbushRunStep::Spotted;
				Record->EndClock = Clock;
			}
			Runs.RemoveAt(RunIndex);
			continue;
		}
		const EAmbushRunStep Outcome = AmbushRelocateRules::StepRun(bSpotted, bAllArrived, Run.Elapsed, Run.LimitSeconds);
		if (Outcome == EAmbushRunStep::Running)
		{
			continue;
		}
		if (Record)
		{
			Record->Outcome = Outcome;
			Record->EndClock = Clock;
		}
		const FVector Face = LeaderFeet();
		for (FManaged& Each : Managed)
		{
			AEnemyCharacter* Enemy = Each.Enemy.Get();
			if (Each.Group != Run.Group || Each.AmbushState != EPressureAmbushState::Relocating || !CombatPressureLocal::IsLivingEnemy(Enemy))
			{
				continue;
			}
			Each.AmbushState = EPressureAmbushState::Hidden;
			if (Outcome != EAmbushRunStep::Spotted)
			{
				// It hides where it is now (its final spot, or where the time ran out): the post stays there, facing the squad.
				const FVector Feet = CombatPressureLocal::FeetOf(*Enemy);
				Enemy->MoveGuardPost(Enemy->GetActorLocation(), CombatPressureLocal::YawOf(Face - Feet));
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d's run %s after %.1f s%s"), Run.Group, AmbushRelocateRules::RunStepName(Outcome), Run.Elapsed,
			Record ? *FString::Printf(TEXT(" (largest step %.0f cm)"), Record->MaxStepCm) : TEXT(""));
		if (Outcome == EAmbushRunStep::Spotted)
		{
			// Decision 1: spotted on the way, it attacks at once (a plan springs: the flank block, then the assault).
			if (Run.Plan > 0)
			{
				if (FPlan* Owner = Plans.FindByPredicate([&Run](const FPlan& Each) { return Each.Info.Id == Run.Plan; }))
				{
					Owner->Info.bSpottedOnRoute = true;
				}
			}
			Spring(Run.Group, TEXT("spotted on its relocation route"));
		}
		Runs.RemoveAt(RunIndex);
	}
}

void UCombatPressureSubsystem::ConvertToFollower(int32 Group, const TCHAR* Why)
{
	int32 Members = 0;
	for (FManaged& Each : Managed)
	{
		AEnemyCharacter* Enemy = Each.Enemy.Get();
		if (Each.Group != Group || Each.AmbushState == EPressureAmbushState::Sprung || !CombatPressureLocal::IsLivingEnemy(Enemy))
		{
			continue;
		}
		// No longer an ambush: a patrol on the squad's trail with its own senses (it engages when it perceives the squad).
		Each.Kind = EPressureSpawnKind::Patrol;
		Each.AmbushState = EPressureAmbushState::Hidden;
		Enemy->bOverridePerception = false;
		Enemy->GuardLookAroundDeg = PatrolRouteRules::GuardLookAroundDeg;
		++Members;
	}
	if (Members == 0)
	{
		return;
	}
	FollowerGroups.Add(Group);
	GroupBypassSeconds.Remove(Group);
	GroupNoRouteSearches.Remove(Group);
	++FollowerConversions;
	UE_LOG(LogCodexTactics, Display, TEXT("[Pressure] ambush group %d (%d members) becomes a patrol following the squad (%s)"), Group, Members, Why);
	TickFollowers();
}

void UCombatPressureSubsystem::TickFollowers()
{
	if (FollowerGroups.IsEmpty())
	{
		return;
	}
	const FVector Leader = LeaderFeet();
	for (const int32 Group : FollowerGroups)
	{
		int32 Index = 0;
		for (const FManaged& Each : Managed)
		{
			AEnemyCharacter* Enemy = Each.Enemy.Get();
			if (Each.Group != Group || !CombatPressureLocal::IsLivingEnemy(Enemy) || !Enemy->IsGuardingPost())
			{
				continue;
			}
			// On the squad's trail: its leader's feet, the group spread round them; walking (a patrol's pace, a little brisk), on foot.
			const FVector2D Want = CombatPressureLocal::To2D(Leader) + CombatPressureRules::GroupOffset(Index++, CombatPressureLocal::GroupSpacingCm);
			const TOptional<FVector> OnNav = ProjectFeet(Want, static_cast<float>(Leader.Z));
			const FVector Goal = OnNav.Get(Leader);
			if (FVector::Dist2D(Goal, Enemy->GetGuardPostLocation()) >= CombatPressureLocal::FollowRepostCm)
			{
				const FVector Feet = CombatPressureLocal::FeetOf(*Enemy);
				Enemy->MoveGuardPost(Goal + FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()), CombatPressureLocal::YawOf(Goal - Feet), Enemy->PatrolWalkSpeed * 1.25f);
			}
		}
	}
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
