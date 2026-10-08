#include "Interactables/GateActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Quests/QuestSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Godot gate.gd stops within 0.05 m of the target. */
	constexpr float ArrivalTolerance = 5.f;
}

AGateActor::AGateActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakeDoor = [this](const TCHAR* BoxName, const TCHAR* MeshName, TObjectPtr<UStaticMeshComponent>& OutMesh)
	{
		UBoxComponent* Door = CreateDefaultSubobject<UBoxComponent>(BoxName);
		Door->SetupAttachment(RootComponent);
		Door->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Door->SetCanEverAffectNavigation(true);
		OutMesh = CreateDefaultSubobject<UStaticMeshComponent>(MeshName);
		OutMesh->SetupAttachment(Door);
		OutMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (CubeMesh.Succeeded())
		{
			OutMesh->SetStaticMesh(CubeMesh.Object);
		}
		return Door;
	};
	LeftDoor = MakeDoor(TEXT("LeftDoor"), TEXT("LeftDoorMesh"), LeftDoorMesh);
	RightDoor = MakeDoor(TEXT("RightDoor"), TEXT("RightDoorMesh"), RightDoorMesh);
}

void AGateActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const FVector Extent = DoorSize * 0.5f;
	for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
	{
		Door->SetBoxExtent(Extent);
	}
	LeftDoor->SetRelativeLocation(FVector(0.f, -Extent.Y, Extent.Z));
	RightDoor->SetRelativeLocation(FVector(0.f, Extent.Y, Extent.Z));
	LeftDoorMesh->SetRelativeScale3D(DoorSize / 100.f);
	RightDoorMesh->SetRelativeScale3D(DoorSize / 100.f);
}

void AGateActor::BeginPlay()
{
	Super::BeginPlay();
	if (UQuestSubsystem* Quests = GetWorld()->GetSubsystem<UQuestSubsystem>())
	{
		Quests->OnGateOpened.AddDynamic(this, &AGateActor::HandleGateOpened);
	}
}

void AGateActor::OpenGate()
{
	if (bOpening || bOpen)
	{
		return;
	}
	bOpening = true;
	LeftTargetY = LeftDoor->GetRelativeLocation().Y - OpenDistance;
	RightTargetY = RightDoor->GetRelativeLocation().Y + OpenDistance;
	SetActorTickEnabled(true);
}

void AGateActor::RestoreOpen(bool bInOpen)
{
	const float HalfWidth = DoorSize.Y * 0.5f;
	const float Offset = bInOpen ? OpenDistance : 0.f;
	FVector Left = LeftDoor->GetRelativeLocation();
	FVector Right = RightDoor->GetRelativeLocation();
	Left.Y = -HalfWidth - Offset;
	Right.Y = HalfWidth + Offset;
	LeftDoor->SetRelativeLocation(Left);
	RightDoor->SetRelativeLocation(Right);
	LeftTargetY = Left.Y;
	RightTargetY = Right.Y;
	bOpening = false;
	bOpen = bInOpen;
	SetActorTickEnabled(false);
	for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
	{
		Door->SetCollisionEnabled(bInOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Door->SetCanEverAffectNavigation(!bInOpen);
	}
}

void AGateActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOpening)
	{
		return;
	}

	const float Step = OpenSpeed * DeltaSeconds;
	auto Slide = [Step](UBoxComponent* Door, float TargetY)
	{
		FVector Location = Door->GetRelativeLocation();
		Location.Y = FMath::FInterpConstantTo(Location.Y, TargetY, 1.f, Step);
		Door->SetRelativeLocation(Location);
	};
	Slide(LeftDoor, LeftTargetY);
	Slide(RightDoor, RightTargetY);

	if (FMath::Abs(LeftDoor->GetRelativeLocation().Y - LeftTargetY) < ArrivalTolerance)
	{
		bOpening = false;
		bOpen = true;
		SetActorTickEnabled(false);
		// Architect spec: once open, the leaves stop blocking the passage (also refreshes the NavMesh).
		for (UBoxComponent* Door : { LeftDoor.Get(), RightDoor.Get() })
		{
			Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Door->SetCanEverAffectNavigation(false);
		}
	}
}

void AGateActor::HandleGateOpened()
{
	OpenGate();
}
