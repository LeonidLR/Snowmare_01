#include "Interactables/VaultNavigation.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavModifierComponent.h"

UNavArea_Vault::UNavArea_Vault()
{
	// Dearer than open ground: a way round of up to three times the length is preferred to climbing over.
	DefaultCost = 3.f;
	DrawColor = FColor(255, 200, 40);
}

UNavFilter_NoVault::UNavFilter_NoVault()
{
	FNavigationFilterArea& Area = Areas.AddDefaulted_GetRef();
	Area.AreaClass = UNavArea_Vault::StaticClass();
	Area.bIsExcluded = true;
}

namespace VaultNavigation
{
	const FName VaultTag(TEXT("Vault"));

	bool IsVaultable(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (const ABarricadeActor* Barricade = Cast<ABarricadeActor>(Actor))
		{
			return Barricade->bVaultable;
		}
		return Actor->ActorHasTag(VaultTag);
	}

	namespace
	{
		bool IsObstacleTop(const AActor* Actor)
		{
			return Actor && (Actor->IsA<ABarricadeActor>() || Actor->IsA<AInteractableActor>() || Actor->ActorHasTag(VaultTag));
		}
	}

	bool IsStandingOnObstacle(const ACharacter& Character)
	{
		const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		return Movement && Movement->IsMovingOnGround() && IsObstacleTop(Movement->CurrentFloor.HitResult.GetActor());
	}

	bool FindStepOffSpot(const ACharacter& Character, FVector& OutCentre)
	{
		UWorld* World = Character.GetWorld();
		const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
		if (!World || !Capsule)
		{
			return false;
		}
		const float Radius = Capsule->GetScaledCapsuleRadius();
		const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(StepOffObstacle), false, &Character);
		const FVector From = Character.GetActorLocation();
		const float StartYaw = Character.GetActorRotation().Yaw;
		for (const float Distance : { 120.f, 180.f, 250.f, 350.f })
		{
			for (int32 Step = 0; Step < 12; ++Step)
			{
				const FVector Probe = From + FRotator(0.f, StartYaw + Step * 30.f, 0.f).Vector() * Distance;
				FHitResult Ground;
				if (!World->LineTraceSingleByChannel(Ground, Probe + FVector(0.f, 0.f, 150.f), Probe - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
					|| IsObstacleTop(Ground.GetActor()) || Ground.GetActor() && Ground.GetActor()->IsA<ACharacter>())
				{
					continue;
				}
				FVector Feet = Ground.ImpactPoint;
				if (Nav)
				{
					FNavLocation OnNav;
					if (!Nav->ProjectPointToNavigation(Feet, OnNav, FVector(50.f, 50.f, 120.f)))
					{
						continue;
					}
					Feet.Z = FMath::Max(Feet.Z, OnNav.Location.Z);
				}
				const FVector Centre = Feet + FVector(0.f, 0.f, HalfHeight + 2.f);
				if (World->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
				{
					continue;
				}
				OutCentre = Centre;
				return true;
			}
		}
		return false;
	}

	void MakeVaultable(AActor* Actor)
	{
		if (!Actor || Actor->FindComponentByClass<UNavModifierComponent>())
		{
			return;
		}
		TArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			Primitive->SetCanEverAffectNavigation(false);
		}
		UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(Actor, TEXT("VaultNavModifier"));
		Modifier->SetAreaClass(UNavArea_Vault::StaticClass());
		Modifier->RegisterComponent();
	}
}
