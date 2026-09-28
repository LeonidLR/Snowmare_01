#include "Combat/EncounterQueries.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace CombatQueries
{
	const FName EnemyTag(TEXT("Enemy"));

	bool HasEnemiesWithin(const UWorld* World, const FVector& Center, float Radius)
	{
		if (!World)
		{
			return false;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(EnemyTag) && !It->IsHidden() && FVector::Dist2D(It->GetActorLocation(), Center) <= Radius)
			{
				return true;
			}
		}
		return false;
	}
}
