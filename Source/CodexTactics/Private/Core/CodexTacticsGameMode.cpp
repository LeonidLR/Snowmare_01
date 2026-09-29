#include "Core/CodexTacticsGameMode.h"
#include "Data/DialogueSequenceAsset.h"
#include "Characters/OperativeBalance.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Core/LevelFlowRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Data/WeaponDataAsset.h"
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
#include "Interactables/TurretActor.h"
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
	TurretClass = ATurretActor::StaticClass();
	OperativeClass = AOperativeCharacter::StaticClass();
	OperativeBlueprint = TSoftClassPtr<AOperativeCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Operatives/BP_Operative.BP_Operative_C")));
	TurnBasedBalance = TSoftObjectPtr<UGodotBalanceAsset>(FSoftObjectPath(TEXT("/Game/Data/Balance/DA_Balance.DA_Balance")));
	GameBalanceConfig = TSoftObjectPtr<UGodotBalanceAsset>(FSoftObjectPath(TEXT("/Game/Data/Balance/DA_GameBalanceConfig.DA_GameBalanceConfig")));
	LevelConfig = TSoftObjectPtr<ULevelConfigAsset>(FSoftObjectPath(TEXT("/Game/Data/Levels/DA_Level_level_01_outpost.DA_Level_level_01_outpost")));
	for (const TCHAR* Id : { TEXT("m16"), TEXT("pistol"), TEXT("grenade"), TEXT("knife") })
	{
		StartingArsenal.Add(TSoftObjectPtr<UWeaponDataAsset>(FSoftObjectPath(FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), Id, Id))));
	}
	DialogueMissionStart = TSoftObjectPtr<UDialogueSequenceAsset>(FSoftObjectPath(TEXT("/Game/Data/Dialogues/DA_DialogueIntro.DA_DialogueIntro")));
	DialoguePreparationStarted = TSoftObjectPtr<UDialogueSequenceAsset>(FSoftObjectPath(TEXT("/Game/Data/Dialogues/DA_DialoguePrep.DA_DialoguePrep")));
	DialogueWaveRest = TSoftObjectPtr<UDialogueSequenceAsset>(FSoftObjectPath(TEXT("/Game/Data/Dialogues/DA_DialogueWaveRest.DA_DialogueWaveRest")));
	DialogueVictory = TSoftObjectPtr<UDialogueSequenceAsset>(FSoftObjectPath(TEXT("/Game/Data/Dialogues/DA_DialogueVictory.DA_DialogueVictory")));

	// Godot squad: Commander leads (Blue), Engineer (Orange), Medic-sapper (Green) in triangle formation.
	SquadRoster = {
		{ LOCTEXT("Commander", "Командир"), FLinearColor::FromSRGBColor(FColor(0x20, 0x80, 0xEC)), FVector(0.f, 0.f, 0.f), 15.f, EOperativeRole::Commander, 25.f, 90.f },
		{ LOCTEXT("Engineer", "Инженер"), FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F)), FVector(-280.f, -260.f, 0.f), 25.f, EOperativeRole::Engineer, 30.f, 75.f },
		{ LOCTEXT("Medic", "Медик-сапёр"), FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33)), FVector(-280.f, 260.f, 0.f), 20.f, EOperativeRole::MedicSapper, 35.f, 85.f } };
}

void ACodexTacticsGameMode::StartPlay()
{
	ApplyLevelConfig();
	Super::StartPlay();
	SpawnSquad();
}

void ACodexTacticsGameMode::ApplyLevelConfig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ULevelConfigAsset* Level = LevelConfig.IsNull() ? nullptr : LevelConfig.LoadSynchronous();
	if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
	{
		Waves->SetLevelConfig(Level);
	}
	if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->SetConfig(LevelFlowRules::ApplyLevel(Flow->GetConfig(), Level ? &Level->Config : nullptr,
			GameBalanceConfig.IsNull() ? nullptr : GameBalanceConfig.LoadSynchronous()));
		UE_LOG(LogCodexTactics, Log, TEXT("Level config %s: %d waves, preparation %.0f s, rest %.0f s"), *GetNameSafe(Level),
			Flow->GetConfig().TotalWaves, Flow->GetConfig().PreparationDuration, Flow->GetConfig().WaveRestDuration);
	}
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
		Operative->Accuracy = Entry.Accuracy;
		// Godot apply_balance_config: health, speeds, matches... from game_balance_config.tres (before BeginPlay).
		if (const UGodotBalanceAsset* Config = GameBalanceConfig.LoadSynchronous())
		{
			OperativeBalance::Apply(*Config, *Operative);
		}
		UGameplayStatics::FinishSpawningActor(Operative, FTransform(Facing, Location));
		TArray<UWeaponDataAsset*> Arsenal;
		for (const TSoftObjectPtr<UWeaponDataAsset>& Weapon : StartingArsenal)
		{
			if (UWeaponDataAsset* Loaded = Weapon.LoadSynchronous())
			{
				Arsenal.Add(Loaded);
			}
		}
		Operative->InitArsenal(Arsenal, StartingReserveAmmo); // full clips, M16 in hands
		Operative->ApplyBodyColor();
	}
}

#undef LOCTEXT_NAMESPACE
