#include "Core/CodexTacticsGameMode.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Core/CodexTacticsGameState.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "CodexTacticsGameMode"

ACodexTacticsGameMode::ACodexTacticsGameMode()
{
	GameStateClass = ACodexTacticsGameState::StaticClass();
	PlayerControllerClass = ACodexTacticsPlayerController::StaticClass();
	DefaultPawnClass = ATacticalCameraPawn::StaticClass();
	OperativeClass = AOperativeCharacter::StaticClass();

	// Godot squad: Commander leads, Engineer and Medic-sapper follow in the triangle formation.
	// Colors follow the Godot battle stats palette (#4db8ff, #ff704d, #5cd65c).
	SquadRoster = {
		{ LOCTEXT("Commander", "Командир"), FLinearColor::FromSRGBColor(FColor(0x4D, 0xB8, 0xFF)), FVector(0.f, 0.f, 0.f) },
		{ LOCTEXT("Engineer", "Инженер"), FLinearColor::FromSRGBColor(FColor(0xFF, 0x70, 0x4D)), FVector(-280.f, -260.f, 0.f) },
		{ LOCTEXT("Medic", "Медик-сапёр"), FLinearColor::FromSRGBColor(FColor(0x5C, 0xD6, 0x5C)), FVector(-280.f, 260.f, 0.f) } };
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
		UGameplayStatics::FinishSpawningActor(Operative, FTransform(Facing, Location));
	}
}

#undef LOCTEXT_NAMESPACE
