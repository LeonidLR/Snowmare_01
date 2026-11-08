#include "Combat/Explosion.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/EncounterQueries.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/GrenadeRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/InteractableActor.h"
#include "Vehicles/VehicleSubsystem.h"

namespace Explosions
{
	FOnExplosionDetonated& OnDetonated()
	{
		static FOnExplosionDetonated Delegate;
		return Delegate;
	}

	FExplosionSpec GrenadeSpec(const FVector& Centre, float Damage, float RadiusCm, const FString& SourceName, AActor* Source)
	{
		FExplosionSpec Spec;
		Spec.Kind = GameplayEvents::ExplosionKinds::Grenade;
		Spec.Centre = Centre;
		Spec.RadiusCm = RadiusCm;
		Spec.EnemyDamage = Damage;
		Spec.EnemyFalloff.Model = EBlastFalloff::HalfAtEdge;
		Spec.SquadDamage = Damage;
		Spec.SquadFalloff.Model = EBlastFalloff::HalfAtEdge;
		Spec.SquadScale = GrenadeRules::SquadDamageScale;
		Spec.DamageType = EDamageType::Explosive;
		Spec.SourceName = SourceName;
		Spec.Source = Source;
		Spec.Targets = EBlastTargets::AllActorsInRadius;
		Spec.bChainBarrelsAndTraps = true;
		Spec.bSquadNoise = true;
		return Spec;
	}

	FExplosionSpec ChargeSpec(const FVector& Centre, float EnemyDamage, float SquadDamage, float RadiusCm, float ArmorPenetration, EDamageType DamageType,
		const FString& SourceName, AActor* Source)
	{
		FExplosionSpec Spec;
		Spec.Kind = GameplayEvents::GetExplosionKind(Source);
		Spec.Centre = Centre;
		Spec.RadiusCm = RadiusCm;
		Spec.EnemyDamage = EnemyDamage;
		Spec.EnemyFalloff.Model = EBlastFalloff::Linear;
		Spec.EnemyFalloff.Factor = DeployableRules::EnemyFalloff;
		Spec.SquadDamage = SquadDamage;
		Spec.SquadFalloff.Model = EBlastFalloff::Linear;
		Spec.SquadFalloff.Factor = DeployableRules::SquadFalloff;
		Spec.DamageType = DamageType;
		Spec.ArmorPenetration = ArmorPenetration;
		Spec.SourceName = SourceName;
		Spec.Source = Source;
		Spec.Targets = EBlastTargets::EnemiesAndSquad;
		Spec.bTrapEvent = true;
		return Spec;
	}

	bool IsWallBetween(UWorld* World, const FVector& Centre, const AActor* Body, const AActor* Source)
	{
		if (!World || !Body)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ExplosionWall), false);
		Params.AddIgnoredActor(Body);
		if (Source)
		{
			Params.AddIgnoredActor(Source);
		}
		const FVector From = Centre + FVector(0.f, 0.f, 50.f);
		const FVector To = Body->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
		{
			return false;
		}
		// Bodies (characters) between are not walls.
		return !Cast<ACharacter>(Hit.GetActor());
	}

	bool IsRoofAbove(UWorld* World, const AActor* Body)
	{
		if (!World || !Body)
		{
			return false;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ExplosionRoof), false);
		Params.AddIgnoredActor(Body);
		const FVector From = Body->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, From, From + FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params) && !Cast<ACharacter>(Hit.GetActor());
	}

	namespace ExplosionLocal
	{
		/** The cover multiplier for one body (1 when the kind does not use the cover rule). */
		float CoverFor(UWorld* World, const FExplosionSpec& Spec, bool bOcclusion, const AActor* Body)
		{
			if (!bOcclusion || !Body)
			{
				return 1.f;
			}
			const FBlastOcclusionConfig& Config = ExplosionRules::Get().Occlusion;
			const bool bWall = !Spec.bPlunging && IsWallBetween(World, Spec.Centre, Body, Spec.Source);
			const bool bRoof = Spec.bPlunging && IsRoofAbove(World, Body);
			return ExplosionRules::CoverMultiplier(Spec.bPlunging, bWall, bRoof, Config);
		}

		void Damage(UHealthComponent& Health, const FExplosionSpec& Spec, float Amount, bool bPlainKinetic)
		{
			FDamageSpec Hit;
			Hit.Amount = Amount;
			Hit.AttackerSource = Spec.SourceName;
			if (!bPlainKinetic)
			{
				Hit.DamageType = Spec.DamageType;
				Hit.ArmorPenetration = Spec.ArmorPenetration;
				Hit.PenetrationClass = Spec.PenetrationClass;
				Hit.StatusEffect = Spec.Status;
				Hit.StatusDuration = Spec.StatusDuration;
				Hit.StatusTickDamage = Spec.StatusTickDamage;
			}
			Health.TakeDamage(Hit);
		}
	}

	FExplosionResult Detonate(UWorld* World, const FExplosionSpec& Spec)
	{
		FExplosionResult Result;
		Result.Kind = Spec.Kind;
		Result.Centre = Spec.Centre;
		Result.RadiusCm = Spec.RadiusCm;
		if (!World)
		{
			return Result;
		}
		const FVector Centre = Spec.Centre;
		const bool bOcclusion = Spec.OcclusionOverride.IsSet() ? Spec.OcclusionOverride.GetValue() : ExplosionRules::Get().UsesOcclusion(Spec.Kind);
		TOptional<AEnemyCharacter::FScopedTrapBlast> TrapBlast;
		if (Spec.bTrapEvent)
		{
			TrapBlast.Emplace(); // a placed charge: patrols search, no ambush fight (user amendment 2026-10-06)
		}
		auto Record = [&Result](AActor* Actor, float Distance, float Damage, float Cover)
		{
			FExplosionVictim& Victim = Result.Victims.AddDefaulted_GetRef();
			Victim.Actor = Actor;
			Victim.DistanceCm = Distance;
			Victim.Damage = Damage;
			Victim.Cover = Cover;
		};
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (Spec.Targets == EBlastTargets::AllActorsInRadius)
		{
			// The grenade's order (Godot grenade.gd _apply_area_effect): vehicles, the noise, then every actor in reach.
			UVehicleSubsystem::ApplyBlastToVehicles(World, Centre, Spec.RadiusCm, [&Spec](float HullDistance)
			{
				return Spec.EnemyDamage * ExplosionRules::Falloff(Spec.EnemyFalloff, HullDistance, Spec.RadiusCm);
			}, Spec.TriggeredByVehicle, Spec.SourceName);
			if (Spec.bSquadNoise)
			{
				AEnemyCharacter::NotifySquadNoise(World, Centre, ESquadNoise::Explosion);
			}
			TArray<AActor*> InRadius;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It != Spec.Source && FVector::Dist(Centre, It->GetActorLocation()) <= Spec.RadiusCm)
				{
					InRadius.Add(*It);
				}
			}
			const FText SourceText = FText::FromString(Spec.SourceName);
			for (AActor* Candidate : InRadius)
			{
				if (!IsValid(Candidate))
				{
					continue;
				}
				if (Spec.bChainBarrelsAndTraps)
				{
					if (ABarrelActor* Barrel = Cast<ABarrelActor>(Candidate))
					{
						Barrel->Explode(SourceText);
					}
					else if (AInteractableActor* Object = Cast<AInteractableActor>(Candidate); Object && Object->bTrapped)
					{
						Object->DetonateTrap(true, SourceText);
					}
				}
				UHealthComponent* Health = IsValid(Candidate) ? Candidate->FindComponentByClass<UHealthComponent>() : nullptr;
				if (!Health || !Health->IsAlive())
				{
					continue;
				}
				const float Distance = static_cast<float>(FVector::Dist(Centre, Candidate->GetActorLocation()));
				const float Cover = ExplosionLocal::CoverFor(World, Spec, bOcclusion, Candidate);
				const float Applied = ExplosionRules::VictimDamage(Spec.EnemyDamage, Spec.EnemyFalloff, Distance, Spec.RadiusCm, 1.f, Cover);
				if (Applied <= 0.f)
				{
					Record(Candidate, Distance, 0.f, Cover); // fully covered (a roof against a plunging round); the grenade never gets here (>= x0.5)
					continue;
				}
				if (Candidate->ActorHasTag(TEXT("Enemy")))
				{
					ExplosionLocal::Damage(*Health, Spec, Applied, false);
					Record(Candidate, Distance, Applied, Cover);
				}
				else if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Candidate); Operative && Squad && Squad->GetMembers().Contains(Operative))
				{
					const float SquadHit = ExplosionRules::VictimDamage(Spec.SquadDamage, Spec.SquadFalloff, Distance, Spec.RadiusCm, Spec.SquadScale, Cover);
					Operative->TakeHit(SquadHit, Spec.SourceName, false, true); // Godot bypass_avoidance
					if (Spec.OnSquadHit)
					{
						Spec.OnSquadHit(*Operative, SquadHit);
					}
					Record(Candidate, Distance, SquadHit, Cover);
				}
				else
				{
					ExplosionLocal::Damage(*Health, Spec, Applied, Spec.bOtherBodiesKinetic);
					Record(Candidate, Distance, Applied, Cover);
				}
			}
		}
		else
		{
			// The placed charges' order (AInteractableActor::ApplyBlast): enemies, the squad, vehicles.
			for (AActor* Candidate : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
			{
				if (!Candidate || Candidate == Spec.Source || !Candidate->ActorHasTag(CombatQueries::EnemyTag))
				{
					continue;
				}
				UHealthComponent* Health = Candidate->FindComponentByClass<UHealthComponent>();
				const float Distance = static_cast<float>(FVector::Dist(Centre, Candidate->GetActorLocation()));
				if (!Health || !Health->IsAlive() || Distance > Spec.RadiusCm)
				{
					continue;
				}
				const float Cover = ExplosionLocal::CoverFor(World, Spec, bOcclusion, Candidate);
				const float Applied = ExplosionRules::VictimDamage(Spec.EnemyDamage, Spec.EnemyFalloff, Distance, Spec.RadiusCm, 1.f, Cover);
				if (Applied > 0.f)
				{
					ExplosionLocal::Damage(*Health, Spec, Applied, false);
					Record(Candidate, Distance, Applied, Cover);
				}
			}
			if (Squad)
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					UHealthComponent* Health = Member ? Member->HealthComponent.Get() : nullptr;
					if (!Health || !Health->IsAlive())
					{
						continue;
					}
					const float Distance = static_cast<float>(FVector::Dist(Centre, Member->GetActorLocation()));
					const float Cover = Distance <= Spec.RadiusCm ? ExplosionLocal::CoverFor(World, Spec, bOcclusion, Member) : 1.f;
					const float SquadHit = ExplosionRules::VictimDamage(Spec.SquadDamage, Spec.SquadFalloff, Distance, Spec.RadiusCm, Spec.SquadScale, Cover);
					if (SquadHit > 0.f)
					{
						Member->TakeHit(SquadHit, Spec.SourceName, false, true); // Godot take_damage(..., bypass_avoidance)
						if (Spec.OnSquadHit)
						{
							Spec.OnSquadHit(*Member, SquadHit);
						}
						Record(Member, Distance, SquadHit, Cover);
					}
				}
			}
			UVehicleSubsystem::ApplyBlastToVehicles(World, Centre, Spec.RadiusCm, [&Spec](float HullDistance)
			{
				return Spec.EnemyDamage * ExplosionRules::Falloff(Spec.EnemyFalloff, HullDistance, Spec.RadiusCm);
			}, Spec.TriggeredByVehicle, Spec.SourceName);
		}
		if (Spec.bKnockdown)
		{
			Result.Knockdowns = UKnockdownComponent::NotifyExplosion(World, Centre); // Sprint 14: everyone closer than 2.5 m
		}
		if (Spec.bTrapEvent)
		{
			AEnemyCharacter::AlertPatrolsNearTrap(World, Centre); // patrols within the trap alert radius go searching
		}
		if (Spec.bRaiseEvent)
		{
			GameplayEvents::RaiseExplosion(Spec.Source ? static_cast<const UObject*>(Spec.Source) : World, Spec.Kind, Centre, Spec.RadiusCm, Spec.Source);
		}
		OnDetonated().Broadcast(World, Result);
		return Result;
	}
}
