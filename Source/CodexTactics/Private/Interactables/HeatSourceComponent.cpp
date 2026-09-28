#include "Interactables/HeatSourceComponent.h"

namespace
{
	TArray<TWeakObjectPtr<UHeatSourceComponent>>& Registry()
	{
		static TArray<TWeakObjectPtr<UHeatSourceComponent>> Sources;
		return Sources;
	}
}

UHeatSourceComponent::UHeatSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHeatSourceComponent::OnRegister()
{
	Super::OnRegister();
	Registry().AddUnique(this);
}

void UHeatSourceComponent::OnUnregister()
{
	Registry().Remove(this);
	Super::OnUnregister();
}

bool UHeatSourceComponent::IsLocationWarm(const FVector& Location) const
{
	return bHeatActive && FVector::Dist(GetComponentLocation(), Location) <= Radius + Tolerance;
}

const TArray<TWeakObjectPtr<UHeatSourceComponent>>& UHeatSourceComponent::GetAllSources()
{
	TArray<TWeakObjectPtr<UHeatSourceComponent>>& Sources = Registry();
	Sources.RemoveAll([](const TWeakObjectPtr<UHeatSourceComponent>& Source) { return !Source.IsValid(); });
	return Sources;
}
