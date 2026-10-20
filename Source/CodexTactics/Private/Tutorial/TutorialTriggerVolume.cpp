#include "Tutorial/TutorialTriggerVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Vehicles/VehiclePawn.h"

ATutorialTriggerVolume::ATutorialTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(FVector(400.f, 400.f, 150.f)); // 8 x 8 x 3 m: a lesson zone, resize per instance
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	Box->ShapeColor = FColor(255, 170, 40); // amber = tutorial (navigation language: amber = path)
	Box->SetHiddenInGame(true);
}

void ATutorialTriggerVolume::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddDynamic(this, &ATutorialTriggerVolume::HandleOverlap);
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->RegisterVolume(this); // 2026-10-20: a navigation target ("guide") may be this volume
	}
}

void ATutorialTriggerVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->UnregisterVolume(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ATutorialTriggerVolume::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryActivate(OtherActor);
}

bool ATutorialTriggerVolume::TryActivate(AActor* Actor)
{
	if (bFired && bFireOnce)
	{
		return false;
	}
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	// APC P1: "Activated By: Vehicle" - a vehicle with the squad aboard drove in; its driver (else any crew member) counts.
	if (ActivatedBy == ETutorialVolumeFilter::Vehicle)
	{
		const AVehiclePawn* Vehicle = Cast<AVehiclePawn>(Actor);
		Operative = Vehicle ? Vehicle->GetOccupant(EVehicleSeat::Driver) : nullptr;
		for (const FVehicleCrewMember& Member : Vehicle ? Vehicle->GetCrew() : TArray<FVehicleCrewMember>())
		{
			Operative = Operative ? Operative : Member.Operative.Get();
		}
	}
	else if (Cast<AVehiclePawn>(Actor))
	{
		return false;
	}
	if (!Operative || !Squad || !Squad->GetMembers().Contains(Operative))
	{
		return false;
	}
	if (ActivatedBy == ETutorialVolumeFilter::Leader && Squad->GetLeader() != Operative)
	{
		return false;
	}
	UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
	// Rev 5f: used up BEFORE the tutorial reacts, so a checkpoint written by the beat this volume starts saves it as fired.
	const bool bWasFired = bFired;
	bFired = true;
	if (!Tutorial || !Tutorial->NotifyVolumeEntered(this, Operative))
	{
		bFired = bWasFired;
		return false;
	}
	return true;
}

void ATutorialTriggerVolume::CheckOverlapping()
{
	TArray<AActor*> Inside;
	Box->GetOverlappingActors(Inside, ActivatedBy == ETutorialVolumeFilter::Vehicle ? AVehiclePawn::StaticClass() : AOperativeCharacter::StaticClass());
	for (AActor* Actor : Inside)
	{
		if (TryActivate(Actor) && bFireOnce)
		{
			return;
		}
	}
}
