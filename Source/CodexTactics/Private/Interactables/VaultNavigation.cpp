#include "Interactables/VaultNavigation.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Interactables/BarricadeActor.h"
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
