#include "Combat/WaveSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/EnemySpawnPoint.h"
#include "Data/EnemyArchetypeAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/GameMessageSubsystem.h"

void UWaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UGameFlowSubsystem::StaticClass());
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
	if (const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>(); Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		return;
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

	if (LevelConfig && LevelConfig->Config.Waves.IsValidIndex(WaveIndex - 1))
	{
		const FWaveDefinition& Def = LevelConfig->Config.Waves[WaveIndex - 1];
		MaxSimultaneousEnemies = Def.MaxSimultaneousEnemies;

		for (const FEnemySpawnEntry& Entry : Def.Spawns)
		{
			for (int32 i = 0; i < Entry.Count; ++i)
			{
				FEnemySpawnEntry Single = Entry;
				Single.Count = 1;
				PendingSpawns.Add(Single);
			}
		}
	}
	else
	{
		// Default wave setup matching Godot stage 01 progression
		MaxSimultaneousEnemies = 8;
		int32 HoundCount = 3 + (WaveIndex - 1) * 2;
		int32 SpitterCount = 1 + (WaveIndex - 1);
		int32 BruteCount = (WaveIndex >= 3) ? (WaveIndex - 2) : 0;

		for (int32 i = 0; i < HoundCount; ++i)
		{
			FEnemySpawnEntry E;
			E.EnemyType = EEnemyArchetype::FrostHound;
			E.Count = 1;
			E.SpawnDelaySec = 0.8f;
			PendingSpawns.Add(E);
		}
		for (int32 i = 0; i < SpitterCount; ++i)
		{
			FEnemySpawnEntry E;
			E.EnemyType = EEnemyArchetype::Spitter;
			E.Count = 1;
			E.SpawnDelaySec = 1.2f;
			PendingSpawns.Add(E);
		}
		for (int32 i = 0; i < BruteCount; ++i)
		{
			FEnemySpawnEntry E;
			E.EnemyType = EEnemyArchetype::Brute;
			E.Count = 1;
			E.SpawnDelaySec = 2.0f;
			PendingSpawns.Add(E);
		}
	}

	TotalWaveEnemies = PendingSpawns.Num() + AliveEnemies.Num();

	if (UGameMessageSubsystem* Msg = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Msg->PostMessage(
			FText::FromString(TEXT("Командир")),
			FText::FromString(FString::Printf(TEXT("Волна %d: Наступают враги (Всего: %d)! Занять оборону!"), CurrentWaveIndex, TotalWaveEnemies))
		);
	}

	OnWaveStarted.Broadcast(CurrentWaveIndex, TotalWaveEnemies);
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

		FVector Loc = GetSpawnLocationForLane(NextSpawn.SpawnLane);
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

	AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(), Location, Rotation, Params);
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
	AliveEnemies.RemoveAll([](const TWeakObjectPtr<AEnemyCharacter>& E) { return !E.IsValid() || E->IsDying(); });

	if (bWaveActive && PendingSpawns.Num() == 0 && AliveEnemies.Num() == 0)
	{
		bWaveActive = false;

		if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->NotifyWaveCleared();
		}

		if (UGameMessageSubsystem* Msg = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Msg->PostMessage(
				FText::FromString(TEXT("Командир")),
				FText::FromString(FString::Printf(TEXT("Волна %d успешно отбита! Всем перегруппироваться."), CurrentWaveIndex))
			);
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

FVector UWaveSubsystem::GetSpawnLocationForLane(const FString& Lane) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	TArray<FVector> CandidateLocations;
	for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
	{
		if (It->bIsActive && (Lane.IsEmpty() || Lane.Equals(TEXT("ANY"), ESearchCase::IgnoreCase) || It->SpawnLane.Equals(Lane, ESearchCase::IgnoreCase)))
		{
			CandidateLocations.Add(It->GetActorLocation());
		}
	}

	if (CandidateLocations.Num() > 0)
	{
		const int32 PickIndex = FMath::RandRange(0, CandidateLocations.Num() - 1);
		return CandidateLocations[PickIndex];
	}

	// Fallback location: courtyard beyond the south gate (Y = -2600)
	return FVector(FMath::RandRange(-600.f, 600.f), -2600.f, 100.f);
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
