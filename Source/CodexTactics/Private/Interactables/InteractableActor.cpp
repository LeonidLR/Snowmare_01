#include "Interactables/InteractableActor.h"
#include "Characters/OperativeCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interactables/HeatSourceComponent.h"
#include "Quests/QuestSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Stand-off from the box surface when approaching, cm. */
	constexpr float ApproachStandOff = 60.f;
}

AInteractableActor::AInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Box->SetCanEverAffectNavigation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	HeatSource = CreateDefaultSubobject<UHeatSourceComponent>(TEXT("HeatSource"));
	HeatSource->SetupAttachment(Box);
}

void AInteractableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Placeholder visual fills the collision box (engine cube is 100 cm).
	Mesh->SetRelativeScale3D(Box->GetUnscaledBoxExtent() / 50.f);
}

void AInteractableActor::BeginPlay()
{
	Super::BeginPlay();

	if (ObjectType == EInteractableType::Generator)
	{
		if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
		{
			Quests->OnGeneratorStarted.AddDynamic(this, &AInteractableActor::HandleGeneratorStarted);
		}
	}
}

void AInteractableActor::Interact(AOperativeCharacter* User)
{
	if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
	{
		Quests->InteractWith(ObjectType, this);
	}
}

float AInteractableActor::GetDistanceTo(const FVector& Location) const
{
	FVector Closest;
	const float Distance = Box->GetClosestPointOnCollision(Location, Closest);
	return Distance >= 0.f ? Distance : FVector::Dist(Location, GetActorLocation());
}

FVector AInteractableActor::GetApproachPoint(const FVector& FromLocation) const
{
	FVector Closest;
	if (Box->GetClosestPointOnCollision(FromLocation, Closest) < 0.f)
	{
		Closest = GetActorLocation();
	}
	const FVector Outward = (FromLocation - Closest).GetSafeNormal2D();
	return Closest + Outward * ApproachStandOff;
}

void AInteractableActor::HandleGeneratorStarted()
{
	HeatSource->SetHeatActive(true);
}
