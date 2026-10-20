#include "Combat/EncounterQueries.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"

namespace CombatQueries
{
	const FName EnemyTag(TEXT("Enemy"));

	bool HasEnemiesWithin(const UWorld* World, const FVector& Center, float Radius)
	{
		if (!World)
		{
			return false;
		}
		for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
		{
			if (!Enemy->ActorHasTag(EnemyTag) || FVector::Dist2D(Enemy->GetActorLocation(), Center) > Radius)
			{
				continue;
			}
			// A live enemy counts even while the squad's sight hides it (TacticalSightSubsystem::SetEnemyHidden, d380d83):
			// presence is gameplay, not rendering (turn-based entry, encounter radius). Other tagged actors keep the old rule.
			if (const AEnemyCharacter* Character = Cast<AEnemyCharacter>(Enemy))
			{
				const UHealthComponent* Health = Character->GetHealthComponent();
				if (!Health || Health->IsAlive())
				{
					return true;
				}
			}
			else if (!Enemy->IsHidden())
			{
				return true;
			}
		}
		return false;
	}

	bool IsSquadOnFlatGround(TArray<float> GroundSamples, const TArray<float>& SquadFeet, float Tolerance, float& OutGround)
	{
		if (GroundSamples.IsEmpty())
		{
			OutGround = SquadFeet.IsEmpty() ? 0.f : SquadFeet[0];
			return true;
		}
		GroundSamples.Sort();
		OutGround = GroundSamples[GroundSamples.Num() / 2];
		for (const float Feet : SquadFeet)
		{
			if (FMath::Abs(Feet - OutGround) > Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	TArray<float> SampleGroundHeights(const UWorld* World, const FVector& Center, float HalfExtent, int32 Count)
	{
		TArray<float> Heights;
		if (!World || Count < 1)
		{
			return Heights;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnGroundSample), false);
		for (TActorIterator<ACharacter> It(const_cast<UWorld*>(World)); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		const float Step = Count > 1 ? 2.f * HalfExtent / (Count - 1) : 0.f;
		for (int32 X = 0; X < Count; ++X)
		{
			for (int32 Y = 0; Y < Count; ++Y)
			{
				const FVector Point(Center.X - HalfExtent + X * Step, Center.Y - HalfExtent + Y * Step, Center.Z);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 600.f), Point - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params))
				{
					Heights.Add(Hit.ImpactPoint.Z);
				}
			}
		}
		return Heights;
	}
}
