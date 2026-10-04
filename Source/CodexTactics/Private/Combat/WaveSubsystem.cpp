#include "Combat/WaveSubsystem.h"
#include "CodexTactics.h"
#include "Combat/FallbackWaveRules.h"
#include "Math/RandomStream.h"
#include "UI/FloatingTextSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Combat/SpawnLaneRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProgressionRules.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/GodotBalanceAsset.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/EnemySpawnPoint.h"
#include "Data/EnemyArchetypeAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "NavigationSystem.h"
#include "CollisionQueryParams.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

void UWaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UGameFlowSubsystem::StaticClass());
	// Godot main.gd _ready: the flank breach and the Susanin rescue fall on different waves of [1, 2, 3].
	TArray<int32> EventWaves = { 1, 2, 3 };
	for (int32 Index = EventWaves.Num() - 1; Index > 0; --Index)
	{
		EventWaves.Swap(Index, FMath::RandRange(0, Index));
	}
	SusaninRescueWave = EventWaves.Pop();
	DynamicBreachWave = EventWaves.Pop();
	// Headless checks (-ExecCmds, like UMissionSubsystem's menu skip) run without random wave events; the smokes that
	// test them pick the waves with SetRandomEventWaves.
	if (FString(FCommandLine::Get()).Contains(TEXT("-ExecCmds")))
	{
		SusaninRescueWave = -1;
		DynamicBreachWave = -1;
	}
}

void UWaveSubsystem::Deinitialize()
{
	ClearAllEnemies();
	Super::Deinitialize();
}

void UWaveSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UWaveSubsystem::HandleGameFlowChanged);
	}
}

bool UWaveSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UWaveSubsystem::Tick(float DeltaTime)
{
	if (!bWaveActive)
	{
		return;
	}
	// Godot freezes the enemy spawners during turn-based combat.
	if (IsTurnBased())
	{
		return;
	}
	// Godot _on_gorky17_combat_ended: the random events that fell during the fight run now.
	if (!PendingRandomEvents.IsEmpty())
	{
		TArray<TFunction<void()>> Pending = MoveTemp(PendingRandomEvents);
		PendingRandomEvents.Reset();
		for (const TFunction<void()>& Event : Pending)
		{
			Event();
		}
	}

	ProcessPendingSpawns(DeltaTime);
	CheckWaveCompletion();
}

TStatId UWaveSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWaveSubsystem, STATGROUP_Tickables);
}

void UWaveSubsystem::SetLevelConfig(ULevelConfigAsset* InConfig)
{
	LevelConfig = InConfig;
}

void UWaveSubsystem::StartWave(int32 WaveIndex)
{
	CurrentWaveIndex = WaveIndex;
	bWaveActive = true;
	SpawnTimer = 0.0f;

	PendingSpawns.Empty();
	AliveEnemies.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& E) { return !E.IsValid() || E->IsDying(); });
	CheckDynamicFlankSpawners(WaveIndex); // Godot _start_next_wave

	if (LevelConfig && LevelConfig->Config.Waves.IsValidIndex(WaveIndex - 1))
	{
		SpawnLevelWave(LevelConfig->Config.Waves[WaveIndex - 1]);
		OnWaveStarted.Broadcast(CurrentWaveIndex, TotalWaveEnemies);
		return;
	}
	// Godot _start_next_wave without a level wave: balance-driven counts, the whole wave at once.
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	FRandomStream Random(FMath::Rand());
	const FFallbackWaveCounts Counts = FallbackWaveRules::Compute(GameMode ? GameMode->GameBalanceConfig.LoadSynchronous() : nullptr,
		WaveIndex, Random);
	auto SpawnType = [this](EEnemyArchetype Type, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			SpawnEnemy(Type, FindFreeSpawnSpot(GetSpawnLocationForLane(FString(), Type)));
		}
	};
	SpawnType(EEnemyArchetype::FrostHound, Counts.Hounds);
	SpawnType(EEnemyArchetype::Spitter, Counts.Spitters);
	SpawnType(EEnemyArchetype::Brute, Counts.Brutes);
	TotalWaveEnemies = GetAliveEnemyCount();

	if (UGameMessageSubsystem* Msg = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Msg->PostMessage(FText::FromString(TEXT("Командир")), FText::FromString(FString::Printf(
			TEXT("Волна %d: Наступают враги (Всего: %d | 🐺 Гончие: %d, 🏹 Стрелки: %d, ❄️ Громилы: %d)!"),
			CurrentWaveIndex, Counts.Total(), Counts.Hounds, Counts.Spitters, Counts.Brutes)));
	}

	OnWaveStarted.Broadcast(CurrentWaveIndex, TotalWaveEnemies);
}

void UWaveSubsystem::SpawnLevelWave(const FWaveDefinition& Def)
{
	const FWaveModifiers& Mods = Def.Modifiers;
	ColdDrainMultiplier = Mods.ColdDrainMult;

	TMap<EEnemyArchetype, int32> Counts;
	int32 Spawned = 0;
	for (const FEnemySpawnEntry& Entry : Def.Spawns)
	{
		for (int32 Index = 0; Index < Entry.Count; ++Index)
		{
			if (AEnemyCharacter* Enemy = SpawnEnemy(Entry.EnemyType, FindFreeSpawnSpot(GetSpawnLocationForLane(Entry.SpawnLane, Entry.EnemyType))))
			{
				Enemy->ApplySpawnEntry(Mods, Entry);
				Counts.FindOrAdd(Entry.EnemyType)++;
				++Spawned;
			}
		}
	}
	TotalWaveEnemies = GetAliveEnemyCount();

	if (UGameMessageSubsystem* Msg = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		const int32 Cutters = Counts.FindRef(EEnemyArchetype::Cutter);
		const FString CutterPart = Cutters > 0 ? FString::Printf(TEXT("🐺 Cutter: %d, "), Cutters) : FString();
		Msg->PostMessage(FText::FromString(TEXT("Командир")), FText::FromString(FString::Printf(
			TEXT("Волна %d: Наступают враги (Всего: %d | %s🐺 Гончие: %d, 🏹 Стрелки: %d, ❄️ Громилы: %d)!"),
			CurrentWaveIndex, Spawned, *CutterPart, Counts.FindRef(EEnemyArchetype::FrostHound),
			Counts.FindRef(EEnemyArchetype::Spitter), Counts.FindRef(EEnemyArchetype::Brute))));
	}
}

void UWaveSubsystem::ProcessPendingSpawns(float DeltaTime)
{
	AliveEnemies.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& E) { return !E.IsValid() || E->IsDying(); });

	if (PendingSpawns.Num() == 0 || AliveEnemies.Num() >= MaxSimultaneousEnemies)
	{
		return;
	}

	SpawnTimer -= DeltaTime;
	if (SpawnTimer <= 0.0f)
	{
		FEnemySpawnEntry NextSpawn = PendingSpawns[0];
		PendingSpawns.RemoveAt(0);

		FVector Loc = FindFreeSpawnSpot(GetSpawnLocationForLane(NextSpawn.SpawnLane, NextSpawn.EnemyType));
		SpawnEnemy(NextSpawn.EnemyType, Loc);

		SpawnTimer = FMath::Max(0.2f, NextSpawn.SpawnDelaySec);
	}
}

AEnemyCharacter* UWaveSubsystem::SpawnEnemy(EEnemyArchetype Archetype, const FVector& Location, const FRotator& Rotation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// The type's Blueprint (mesh + AnimBP) from the game mode, else the C++ class with the placeholder body.
	UClass* EnemyClass = Archetype == EEnemyArchetype::Marksman ? AMarksmanEnemyCharacter::StaticClass() : AEnemyCharacter::StaticClass();
	if (const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>())
	{
		if (const TSoftClassPtr<AEnemyCharacter>* Soft = GameMode->EnemyClasses.Find(Archetype))
		{
			if (UClass* Loaded = Soft->LoadSynchronous())
			{
				EnemyClass = Loaded;
			}
		}
	}
	AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(EnemyClass, Location, Rotation, Params);
	if (Enemy)
	{
		Enemy->InitializeArchetype(Archetype);
		Enemy->OnEnemyDiedNative.AddUObject(this, &UWaveSubsystem::HandleEnemyDied);
		AliveEnemies.Add(Enemy);

		OnEnemySpawned.Broadcast(Enemy, Archetype);
		OnEnemySpawnedNative.Broadcast(Enemy, Archetype);
	}

	return Enemy;
}

void UWaveSubsystem::HandleEnemyDied(AEnemyCharacter* Enemy)
{
	AliveEnemies.Remove(Enemy);
	CheckWaveCompletion();
}

void UWaveSubsystem::CheckWaveCompletion()
{
	// Godot main.gd _process checks the wave only outside turn-based combat (is_wave_active and not
	// is_gorky17_combat_active): the last kill on the grid first ends the turn-based fight, the wave clears afterwards.
	if (const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>(); Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		return;
	}
	AliveEnemies.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& E) { return !E.IsValid() || E->IsDying(); });

	if (bWaveActive && PendingSpawns.Num() == 0 && AliveEnemies.Num() == 0)
	{
		bWaveActive = false;

		if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->NotifyWaveCleared();
		}

		// The wave-cleared radio line (Godot _on_wave_cleared) is posted by UWaveVictorySubsystem.

		// Godot main.gd _on_wave_cleared: every squad member gets exp_reward_wave_complete (before the profile opens).
		const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
		const int32 WaveExp = ProgressionRules::WaveClearReward(GameMode ? GameMode->GameBalanceConfig.LoadSynchronous() : nullptr);
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->AddExp(WaveExp);
			}
		}

		OnWaveCleared.Broadcast(CurrentWaveIndex);
	}
}

void UWaveSubsystem::ClearAllEnemies()
{
	PendingSpawns.Empty();
	for (TWeakObjectPtr<AEnemyCharacter>& WeakEnemy : AliveEnemies)
	{
		if (WeakEnemy.IsValid())
		{
			WeakEnemy->Destroy();
		}
	}
	AliveEnemies.Empty();
}

int32 UWaveSubsystem::GetAliveEnemyCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakEnemy : AliveEnemies)
	{
		if (WeakEnemy.IsValid() && !WeakEnemy->IsDying())
		{
			++Count;
		}
	}
	return Count;
}

FVector UWaveSubsystem::GetSpawnLocationForLane(const FString& Lane, EEnemyArchetype Type) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	// Godot main.gd _get_enemy_spawn_pos: dynamic points are skipped; a point matches when its allowed type accepts
	// Type and the lanes match (SpawnLaneRules: either name contains the other, case-insensitive, or they are aliases —
	// Sprint 05-C; "ANY" matches every point). Without
	// a match any non-dynamic point is used, then any point; the hard-coded yard is the last resort.
	const bool bAnyLane = Lane.IsEmpty() || Lane.Equals(TEXT("ANY"), ESearchCase::IgnoreCase);
	TArray<FVector> CandidateLocations;
	TArray<FVector> StaticLocations;
	TArray<FVector> AnyLocations;
	for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
	{
		if (!It->bIsActive)
		{
			continue;
		}
		AnyLocations.Add(It->GetActorLocation());
		if (It->bIsDynamic)
		{
			continue;
		}
		StaticLocations.Add(It->GetActorLocation());
		const bool bLaneOk = bAnyLane || SpawnLaneRules::LanesMatch(It->SpawnLane, Lane); // + NORTH_GATE <-> «Северные ворота» aliases
		if (bLaneOk && It->Accepts(Type))
		{
			CandidateLocations.Add(It->GetActorLocation());
		}
	}
	if (CandidateLocations.Num() == 0)
	{
		CandidateLocations = StaticLocations.Num() > 0 ? MoveTemp(StaticLocations) : MoveTemp(AnyLocations);
	}

	if (CandidateLocations.Num() > 0)
	{
		const int32 PickIndex = FMath::RandRange(0, CandidateLocations.Num() - 1);
		return CandidateLocations[PickIndex];
	}

	// Fallback location: courtyard beyond the south gate (Y = -2600)
	return FVector(FMath::RandRange(-600.f, 600.f), -2600.f, 100.f);
}

FVector UWaveSubsystem::FindFreeSpawnSpot(const FVector& Point) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!World)
	{
		return Point;
	}
	// The biggest enemy capsule (brute) with a little margin; centre 1 m above the ground like the spawn points.
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(55.f, 95.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySpawnSpot), false);
	auto TrySpot = [&](const FVector& Candidate, FVector& Out) -> bool
	{
		FVector Ground = Candidate;
		if (Nav)
		{
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(120.f, 120.f, 400.f)))
			{
				return false;
			}
			Ground = OnNav.Location;
		}
		const FVector Centre = Ground + FVector(0.f, 0.f, 100.f);
		if (World->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			return false;
		}
		Out = Centre;
		return true;
	};
	FVector Spot;
	if (TrySpot(Point, Spot))
	{
		return Spot;
	}
	const float StartAngle = FMath::FRandRange(0.f, 360.f);
	// Up to ~130 free spots per point (the Wave Editor allows 100 of a type per wave).
	const float Radii[] = { 150.f, 300.f, 450.f, 600.f, 750.f, 900.f };
	for (const float Radius : Radii)
	{
		const int32 Samples = FMath::RoundToInt(Radius / 150.f) * 6;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const FVector Offset = FVector(Radius, 0.f, 0.f).RotateAngleAxis(StartAngle + Index * 360.f / Samples, FVector::UpVector);
			if (TrySpot(Point + Offset, Spot))
			{
				return Spot;
			}
		}
	}
	FNavLocation OnNav;
	const bool bOnNav = Nav && Nav->ProjectPointToNavigation(Point, OnNav, FVector(600.f, 600.f, 600.f));
	if (!bOnNav && !WarnedSpawnPoints.Contains(Point))
	{
		WarnedSpawnPoints.Add(Point);
		UE_LOG(LogCodexTactics, Warning, TEXT("[Wave] spawn point %s is off the navmesh (> 6 m): enemies spawned there cannot walk"), *Point.ToCompactString());
	}
	return bOnNav ? OnNav.Location + FVector(0.f, 0.f, 100.f) : Point;
}

void UWaveSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (Phase == ECodexGamePhase::WaveCombat)
	{
		UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const int32 FlowWave = Flow ? Flow->GetWaveIndex() : 1;
		if (!bWaveActive || CurrentWaveIndex != FlowWave)
		{
			StartWave(FlowWave);
		}
	}
}

bool UWaveSubsystem::IsTurnBased() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
}

void UWaveSubsystem::RunOrDeferRandomEvent(TFunction<void()> Event)
{
	if (IsTurnBased())
	{
		PendingRandomEvents.Add(MoveTemp(Event));
		return;
	}
	Event();
}

void UWaveSubsystem::CheckDynamicFlankSpawners(int32 WaveNum)
{
	UWorld* World = GetWorld();
	if (!World || WaveNum != DynamicBreachWave || bDynamicBreachTriggered)
	{
		return;
	}
	for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
	{
		if (!It->bIsDynamic || FMath::FRand() > It->ActivationChance)
		{
			continue;
		}
		bDynamicBreachTriggered = true;
		TWeakObjectPtr<AEnemySpawnPoint> WeakPoint(*It);
		TWeakObjectPtr<UWaveSubsystem> WeakThis(this);
		auto ExecuteBreach = [WeakThis, WeakPoint]()
		{
			UWaveSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->bWaveActive || !WeakPoint.IsValid())
			{
				return;
			}
			Self->RunOrDeferRandomEvent([WeakThis, WeakPoint]()
			{
				if (WeakThis.IsValid() && WeakThis->bWaveActive && WeakPoint.IsValid())
				{
					WeakThis->TriggerBreach(WeakPoint.Get());
				}
			});
		};
		const float Delay = bInstantRandomEvents ? 0.f : FMath::FRandRange(4.f, 7.f);
		if (Delay > 0.f)
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(ExecuteBreach), Delay, false);
		}
		else
		{
			ExecuteBreach();
		}
		break;
	}
}

void UWaveSubsystem::SpawnBreachPack(const AEnemySpawnPoint& Point)
{
	if (UGameMessageSubsystem* Msg = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Msg->PostMessage(FText::FromString(TEXT("ШТАБ")), FText::FromString(FString::Printf(
			TEXT("💥 ПРОРЫВ ВО ФЛАНГЕ! Враги пробили переборку на рубеже «%s»!"), *Point.SpawnLane)));
	}
	// Godot's match spawns hounds for any other type (FROSTBITTEN included).
	const EEnemyArchetype Type = Point.BreachEnemyType == EEnemyArchetype::Spitter || Point.BreachEnemyType == EEnemyArchetype::Brute
		|| Point.BreachEnemyType == EEnemyArchetype::Cutter ? Point.BreachEnemyType : EEnemyArchetype::FrostHound;
	for (int32 Index = 0; Index < Point.EnemyCount; ++Index)
	{
		SpawnEnemy(Type, FindFreeSpawnSpot(Point.GetActorLocation()));
	}
}

void UWaveSubsystem::TriggerBreach(AEnemySpawnPoint* Point, float CameraDelay)
{
	UWorld* World = GetWorld();
	if (!World || !Point || IsTurnBased())
	{
		return;
	}
	SpawnBreachPack(*Point);

	TWeakObjectPtr<UWaveSubsystem> WeakThis(this);
	TWeakObjectPtr<AEnemySpawnPoint> WeakPoint(Point);
	auto FocusCamera = [WeakThis, WeakPoint]()
	{
		UWaveSubsystem* Self = WeakThis.Get();
		if (!Self || !WeakPoint.IsValid() || Self->IsTurnBased())
		{
			return;
		}
		UWorld* PointWorld = Self->GetWorld();
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(PointWorld, 0));
		if (Camera)
		{
			Camera->SetFollowTarget(WeakPoint.Get());
		}
		if (UGameMessageSubsystem* Msg = PointWorld->GetSubsystem<UGameMessageSubsystem>())
		{
			Msg->PostMessage(FText::FromString(TEXT("ОТРЯД")), FText::FromString(TEXT("💥 Чёрт подери, откуда они взялись? Приготовиться к отражению атаки!")));
		}
		if (const USquadSubsystem* Squad = PointWorld->GetSubsystem<USquadSubsystem>(); Squad && Squad->GetLeader())
		{
			UFloatingTextSubsystem::SpawnAboveOperative(Squad->GetLeader(), TEXT("💥 ЧЁРТ ПОДЕРИ, ОТКУДА ОНИ?!"), FLinearColor(1.f, 0.3f, 0.3f));
		}
		// Back to the squad 1.8 s after the breach was shown.
		TWeakObjectPtr<ATacticalCameraPawn> WeakCamera(Camera);
		FTimerHandle ReturnHandle;
		PointWorld->GetTimerManager().SetTimer(ReturnHandle, FTimerDelegate::CreateLambda([WeakThis, WeakCamera]()
		{
			const USquadSubsystem* Squad = WeakThis.IsValid() ? WeakThis->GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
			if (WeakCamera.IsValid() && Squad && Squad->GetLeader())
			{
				WeakCamera->SetFollowTarget(Squad->GetLeader());
			}
		}), 1.8f, false);
	};
	if (CameraDelay <= 0.f)
	{
		FocusCamera();
	}
	else
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(FocusCamera), CameraDelay, false);
	}
}
