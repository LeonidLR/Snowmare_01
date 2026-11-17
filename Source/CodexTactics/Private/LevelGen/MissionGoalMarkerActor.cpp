#include "LevelGen/MissionGoalMarkerActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LevelGen/MissionGoalsRules.h"
#include "Tutorial/TutorialSubsystem.h"

AMissionGoalMarkerActor::AMissionGoalMarkerActor()
{
	bScreenEdgePointer = false; // the tutorial overlay never sees this marker (not registered)
	BeaconHeightCm = 500.f;     // taller than the tutorial's: seen over yard walls and crates
	// Reuse the world-text pattern of InteriorExitArrowActor; the component supplies its engine font/material.
	GoalText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("GoalText"));
	GoalText->SetupAttachment(GetRootComponent());
	GoalText->SetHorizontalAlignment(EHTA_Center);
	GoalText->SetVerticalAlignment(EVRTA_TextCenter);
	GoalText->SetTextRenderColor(FColor::Orange);
	GoalText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGoalNumber(1, FMissionGoalsFile());
}

void AMissionGoalMarkerActor::BeginPlay()
{
	Super::BeginPlay(); // the marker's shape and hologram; it registers with the tutorial ...
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->UnregisterMarker(this); // ... which must not drive it: UMissionGoalsSubsystem shows / hides it
	}
	SetMarkerShown(bGoalShown); // the parent's BeginPlay hid it
}

void AMissionGoalMarkerActor::SetGoalNumber(int32 Number, const FMissionGoalsFile& Objectives)
{
	Label = FText::Format(NSLOCTEXT("MissionGoals", "GoalNumber", "GOAL {0}"), FText::AsNumber(Number));
	GoalText->SetText(Label);
	GoalText->SetRelativeLocation(FVector(0.f, 0.f, Objectives.LabelHeightCm));
	GoalText->SetWorldSize(Objectives.LabelWorldSizeCm);
}

void AMissionGoalMarkerActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->PlayerCameraManager)
	{
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - GoalText->GetComponentLocation();
		if (!ToCamera.IsNearlyZero())
		{
			GoalText->SetWorldRotation(ToCamera.Rotation());
		}
	}
}

void AMissionGoalMarkerActor::SetGoalShown(bool bShow)
{
	bGoalShown = bShow;
	GoalText->SetVisibility(bShow);
	if (HasActorBegunPlay())
	{
		SetMarkerShown(bShow);
	}
}
