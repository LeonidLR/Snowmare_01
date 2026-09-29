#include "Quests/DialogueTriggerVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/BoxComponent.h"
#include "Data/DialogueSequenceAsset.h"
#include "Engine/World.h"
#include "UI/DialogueSubsystem.h"

ADialogueTriggerVolume::ADialogueTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	// Godot BoxShape3D_v1mfj: 4.3 x 2.3 x 2.6 m.
	Box->SetBoxExtent(FVector(217.f, 131.f, 116.f));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
}

void ADialogueTriggerVolume::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddDynamic(this, &ADialogueTriggerVolume::HandleOverlap);
}

void ADialogueTriggerVolume::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryTrigger(OtherActor);
}

bool ADialogueTriggerVolume::TryTrigger(AActor* Actor)
{
	if (bHasTriggered && bTriggerOnce)
	{
		return false;
	}
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	if (bSquadOnly && !(Operative && Squad && Squad->GetMembers().Contains(Operative)))
	{
		return false;
	}
	const UDialogueSequenceAsset* Sequence = Dialogue.LoadSynchronous();
	if (!Sequence)
	{
		return false;
	}
	bHasTriggered = true;
	if (UDialogueSubsystem* Dialogues = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogues->PlayInFeed(Sequence); // Godot play_dialogue: line by line in the feed
	}
	return true;
}
