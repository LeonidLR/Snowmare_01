#include "Combat/CombatGroundQuery.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

namespace CombatGround
{
	FVector FeetOf(const AActor& Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(&Actor);
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		return Capsule ? Actor.GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight()) : Actor.GetActorLocation();
	}
}