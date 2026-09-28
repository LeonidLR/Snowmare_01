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
#include "Interactables/BarricadeActor.h"
#include "Interactables/ProximityMineActor.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "CodexTacticsGameMode"

ACodexTacticsGameMode::ACodexTacticsGameMode()
{
	GameStateClass = ACodexTacticsGameState::StaticClass();
	PlayerControllerClass = ACodexTacticsPlayerController::StaticClass();
	DefaultPawnClass = ATacticalCameraPawn::StaticClass();
	HUDClass = ACodexTacticsHUD::StaticClass();
	BarricadeClass = ABarricadeActor::StaticClass();
	MineClass = AProximityMineActor::StaticClass();
	OperativeClass = AOperativeCharacter::StaticClass();
	OperativeBlueprint = TSoftClassPtr<AOperativeCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Operatives/BP_Operative.BP_Operative_C")));

	// Godot squad: Commander leads (Blue), Engineer (Orange), Medic-sapper (Green) in triangle formation.
	SquadRoster = {
		{ LOCTEXT("Commander", "Командир"), FLinearColor::FromSRGBColor(FColor(0x20, 0x80, 0xEC)), FVector(0.f, 0.f, 0.f), 15.f, EOperativeRole::Commander, 25.f },
		{ LOCTEXT("Engineer", "Инженер"), FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F)), FVector(-280.f, -260.f, 0.f), 25.f, EOperativeRole::Engineer, 30.f },
		{ LOCTEXT("Medic", "Медик-сапёр"), FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33)), FVector(-280.f, 260.f, 0.f), 20.f, EOperativeRole::MedicSapper, 35.f } };
}

void ACodexTacticsGameMode::StartPlay()
{
	Super::StartPlay();
	SpawnSquad();
}

void ACodexTacticsGameMode::SpawnSquad()
{
	UWorld* World = GetWorld();
	TSubclassOf<AOperativeCharacter> SpawnClass = OperativeBlueprint.IsNull() ? nullptr : OperativeBlueprint.LoadSynchronous();
	if (!SpawnClass)
	{
		SpawnClass = OperativeClass;
	}
	if (!World || !SpawnClass)
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
		AOperativeCharacter* Operative = World->SpawnActorDeferred<AOperativeCharacter>(SpawnClass,
			FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Operative)
		{
			continue;
		}
		Operative->SquadIndex = Index;
		Operative->DisplayName = Entry.DisplayName;
		Operative->BodyColor = Entry.Color;
		Operative->ColdSurvival->Fortitude = Entry.Fortitude;
		Operative->SquadRole = Entry.Role;
		Operative->Luck = Entry.Luck;
		UGameplayStatics::FinishSpawningActor(Operative, FTransform(Facing, Location));
		Operative->ApplyBodyColor();
	}
}

#undef LOCTEXT_NAMESPACE
