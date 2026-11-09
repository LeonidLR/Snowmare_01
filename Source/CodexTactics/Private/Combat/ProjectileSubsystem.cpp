#include "Combat/ProjectileSubsystem.h"

#include "Engine/World.h"

AProjectileActor* UProjectileSubsystem::Fire(const FProjectileLaunch& Launch)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AProjectileActor* Round = nullptr;
	while (Pool.Num() > 0 && !Round)
	{
		Round = Pool.Pop();
		if (!IsValid(Round))
		{
			Round = nullptr;
		}
	}
	if (!Round)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Round = World->SpawnActor<AProjectileActor>(AProjectileActor::StaticClass(), Launch.From, Launch.Velocity.Rotation(), SpawnParams);
		if (!Round)
		{
			return nullptr;
		}
	}
	Active.Add(Round);
	Round->Launch(Launch, GetWorldGravityZ());
	return Round;
}

bool UProjectileSubsystem::FireAt(FProjectileLaunch Launch, const FVector& Target, AProjectileActor** OutRound)
{
	if (OutRound)
	{
		*OutRound = nullptr;
	}
	const float GravityZ = ProjectileRules::GravityFor(Launch.Projectile, GetWorldGravityZ());
	FVector Velocity;
	if (!ProjectileRules::SolveLaunch(Launch.From, Target, Launch.Projectile.SpeedCmS, GravityZ, Launch.Projectile.Trajectory, Velocity))
	{
		return false;
	}
	Launch.Velocity = Velocity;
	AProjectileActor* Round = Fire(Launch);
	if (OutRound)
	{
		*OutRound = Round;
	}
	return Round != nullptr;
}

void UProjectileSubsystem::Release(AProjectileActor* Round, const FProjectileImpact& Impact)
{
	if (Round)
	{
		Active.Remove(Round);
		Pool.AddUnique(Round);
	}
	OnProjectileFinished.Broadcast(Impact);
}

float UProjectileSubsystem::GetWorldGravityZ() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGravityZ() : -980.f;
}

void UProjectileSubsystem::Deinitialize()
{
	Pool.Reset();
	Active.Reset();
	OnProjectileFinished.Clear();
	Super::Deinitialize();
}
