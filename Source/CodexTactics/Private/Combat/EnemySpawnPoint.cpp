#include "Combat/EnemySpawnPoint.h"
#include "Components/SceneComponent.h"

AEnemySpawnPoint::AEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootComp;

	Tags.Add(FName(TEXT("EnemySpawnPoint")));
}

bool AEnemySpawnPoint::Accepts(EEnemyArchetype Type) const
{
	switch (AllowedEnemyType)
	{
	case EEnemySpawnFilter::Hound: return Type == EEnemyArchetype::FrostHound;
	case EEnemySpawnFilter::Spitter: return Type == EEnemyArchetype::Spitter;
	case EEnemySpawnFilter::Brute: return Type == EEnemyArchetype::Brute;
	case EEnemySpawnFilter::Cutter: return Type == EEnemyArchetype::Cutter;
	default: return true;
	}
}
