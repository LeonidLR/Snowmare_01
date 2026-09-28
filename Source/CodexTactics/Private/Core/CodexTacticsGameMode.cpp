#include "Core/CodexTacticsGameMode.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Core/CodexTacticsGameState.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/ColdSurvivalComponent.h"

#define LOCTEXT_NAMESPACE "CodexTacticsGameMode"

ACodexTacticsGameMode::ACodexTacticsGameMode()
{
	GameStateClass = ACodexTacticsGameState::StaticClass();
	PlayerControllerClass = ACodexTacticsPlayerController::StaticClass();
	DefaultPawnClass = ATacticalCameraPawn::StaticClass();
	OperativeClass = AOperativeCharacter::StaticClass();

	// Godot squad: Commander leads (Blue), Engineer (Orange), Medic-sapper (Green) in triangle formation.
	SquadRoster = {
		{ LOCTEXT("Commander", "Командир"), FLinearColor::FromSRGBColor(FColor(0x20, 0x80, 0xEC)), FVector(0.f, 0.f, 0.f), 15.f },
		{ LOCTEXT("Engineer", "Инженер"), FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F)), FVector(-280.f, -260.f, 0.f), 25.f },
		{ LOCTEXT("Medic", "Медик-сапёр"), FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33)), FVector(-280.f, 260.f, 0.f), 20.f } };
}

void ACodexTacticsGameMode::StartPlay()
{
	Super::StartPlay();
	SpawnSquad();
}

void ACodexTacticsGameMode::SpawnSquad()
{
	UWorld* World = GetWorld();
	if (!World || !OperativeClass)
	{
		return;
	}
	if (TActorIterator<AOperativeCharacter>(World))
	{
		return; // the level places its own squad
	}

	const AActor* Start = UGameplayStatics::GetActorOfClass(World, APlayerStart::StaticClass());
	const FTransform StartTransform = Start ? Start->GetActorTransform() : FTransform::Identity;
	const FRotator Facing(0.f, StartTransform.Rotator().Yaw, 0.f);

	for (int32 Index = 0; Index < SquadRoster.Num(); ++Index)
	{
		const FSquadMemberSpawn& Entry = SquadRoster[Index];
		const FVector Location = StartTransform.GetLocation() + Facing.RotateVector(Entry.Offset);
		AOperativeCharacter* Operative = World->SpawnActorDeferred<AOperativeCharacter>(OperativeClass,
			FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Operative)
		{
			continue;
		}
		Operative->SquadIndex = Index;
		Operative->DisplayName = Entry.DisplayName;
		Operative->BodyColor = Entry.Color;
		Operative->ColdSurvival->Fortitude = Entry.Fortitude;
		UGameplayStatics::FinishSpawningActor(Operative, FTransform(Facing, Location));
		Operative->ApplyBodyColor();
	}
}

#undef LOCTEXT_NAMESPACE
