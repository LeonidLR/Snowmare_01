#include "Camera/BuildingExitMarker.h"
#include "Components/ArrowComponent.h"

ABuildingExitMarker::ABuildingExitMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowColor = FColor(255, 170, 40);
	Arrow->ArrowSize = 2.f;
	Arrow->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	SetActorHiddenInGame(true);
}

FVector ABuildingExitMarker::GetOutwardDirection() const
{
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	return Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
}
