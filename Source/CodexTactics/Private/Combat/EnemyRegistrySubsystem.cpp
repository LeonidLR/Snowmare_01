#include "Combat/EnemyRegistrySubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Combat/EncounterQueries.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace EnemyRegistryCVars
{
	// A / B switch for measurements and bisecting: 0 = the old world scans (and the per-call shot-line ignore lists).
	static TAutoConsoleVariable<int32> CVarEnabled(TEXT("Codex.EnemyRegistry"), 1,
		TEXT("1: per-frame enemy queries read UEnemyRegistrySubsystem; 0: they scan the world as before (A / B checks)"));
}

void UEnemyRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UWorld* World = GetWorld())
	{
		SpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UEnemyRegistrySubsystem::HandleActorSpawned));
	}
}

void UEnemyRegistrySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
	}
	SpawnedHandle.Reset();
	Super::Deinitialize();
}

void UEnemyRegistrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Placed actors that carry the tag without being enemy characters (those register themselves in BeginPlay).
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (!It->IsA<AEnemyCharacter>() && It->ActorHasTag(CombatQueries::EnemyTag))
		{
			RegisterTaggedActor(*It);
		}
	}
}

void UEnemyRegistrySubsystem::RegisterEnemy(AEnemyCharacter* Enemy)
{
	if (Enemy)
	{
		Enemies.Add(Enemy);
	}
}

void UEnemyRegistrySubsystem::UnregisterEnemy(AEnemyCharacter* Enemy)
{
	if (Enemy)
	{
		Enemies.Remove(Enemy);
	}
	PruneStale();
}

void UEnemyRegistrySubsystem::RegisterTaggedActor(AActor* Actor)
{
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
	{
		RegisterEnemy(Enemy);
	}
	else if (Actor)
	{
		TaggedActors.Add(Actor);
	}
}

void UEnemyRegistrySubsystem::HandleActorSpawned(AActor* Actor)
{
	if (Actor && !Actor->IsA<AEnemyCharacter>() && Actor->ActorHasTag(CombatQueries::EnemyTag))
	{
		TaggedActors.Add(Actor);
	}
}

void UEnemyRegistrySubsystem::PruneStale()
{
	Enemies.RemoveAllBut([](const TWeakObjectPtr<AEnemyCharacter>& Entry) { return Entry.IsValid(); });
	TaggedActors.RemoveAllBut([](const TWeakObjectPtr<AActor>& Entry) { return Entry.IsValid(); });
}

bool UEnemyRegistrySubsystem::IsEnabled()
{
	return EnemyRegistryCVars::CVarEnabled.GetValueOnGameThread() != 0;
}

bool UEnemyRegistrySubsystem::IsServing(const UWorld* World)
{
	return IsEnabled() && World && World->HasBegunPlay() && World->GetSubsystem<UEnemyRegistrySubsystem>() != nullptr;
}

UEnemyRegistrySubsystem::FActorList UEnemyRegistrySubsystem::GetTaggedEnemies(const UWorld* World)
{
	FActorList Result;
	if (!World)
	{
		return Result;
	}
	const UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>();
	if (!Registry || !World->HasBegunPlay() || !IsEnabled())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(CombatQueries::EnemyTag))
			{
				Result.Add(*It);
			}
		}
		return Result;
	}
	for (const TWeakObjectPtr<AEnemyCharacter>& Entry : Registry->Enemies.GetItems())
	{
		AEnemyCharacter* Enemy = Entry.Get();
		if (IsValid(Enemy) && Enemy->ActorHasTag(CombatQueries::EnemyTag))
		{
			Result.Add(Enemy);
		}
	}
	for (const TWeakObjectPtr<AActor>& Entry : Registry->TaggedActors.GetItems())
	{
		AActor* Actor = Entry.Get();
		if (IsValid(Actor) && Actor->ActorHasTag(CombatQueries::EnemyTag))
		{
			Result.Add(Actor);
		}
	}
	return Result;
}

UEnemyRegistrySubsystem::FEnemyList UEnemyRegistrySubsystem::GetEnemyCharacters(const UWorld* World)
{
	FEnemyList Result;
	if (!World)
	{
		return Result;
	}
	const UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>();
	if (!Registry || !World->HasBegunPlay() || !IsEnabled())
	{
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			Result.Add(*It);
		}
		return Result;
	}
	for (const TWeakObjectPtr<AEnemyCharacter>& Entry : Registry->Enemies.GetItems())
	{
		AEnemyCharacter* Enemy = Entry.Get();
		if (IsValid(Enemy))
		{
			Result.Add(Enemy);
		}
	}
	return Result;
}
