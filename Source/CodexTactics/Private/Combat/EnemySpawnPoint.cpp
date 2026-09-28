#include "Combat/EnemySpawnPoint.h"
#include "Components/SceneComponent.h"

AEnemySpawnPoint::AEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* RootComp = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = RootComp;

	Tags.Add(FName(TEXT("EnemySpawnPoint")));
}
