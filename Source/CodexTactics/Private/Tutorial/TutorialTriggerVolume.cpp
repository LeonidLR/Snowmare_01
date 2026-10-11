#include "Tutorial/TutorialTriggerVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Tutorial/TutorialSubsystem.h"

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
	if (!Operative || !Squad || !Squad->GetMembers().Contains(Operative))
	{
		return false;
	}
	if (ActivatedBy == ETutorialVolumeFilter::Leader && Squad->GetLeader() != Operative)
	{
		return false;
	}
	UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
	if (!Tutorial || !Tutorial->NotifyVolumeEntered(this, Operative))
	{
		return false;
	}
	bFired = true;
	return true;
}

void ATutorialTriggerVolume::CheckOverlapping()
{
	TArray<AActor*> Inside;
	Box->GetOverlappingActors(Inside, AOperativeCharacter::StaticClass());
	for (AActor* Actor : Inside)
	{
		if (TryActivate(Actor) && bFireOnce)
		{
			return;
		}
	}
}
