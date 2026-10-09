#include "Core/CodexTacticsGameMode.h"
#include "Data/LevelJsonRules.h"
#include "Characters/EnemyCharacter.h"
#include "Data/DialogueSequenceAsset.h"
#include "Characters/OperativeBalance.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Core/LevelFlowRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Data/AITuning.h"
#include "Data/SquadROE.h"
#include "Data/EnemyPerception.h"
#include "Data/WeaponTuning.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Core/CodexTacticsGameState.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TurretActor.h"
#include "Interactables/VaultNavigation.h"
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
	// Enemy art per type (Scripts/Editor/setup_enemy_animation.py); Godot resources/enemies/anims/*.tres picks the same clips.
	EnemyClasses.Add(EEnemyArchetype::FrostHound,
		TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C"))));
	EnemyClasses.Add(EEnemyArchetype::Brute,
		TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C"))));
	EnemyClasses.Add(EEnemyArchetype::Frostbitten,
		TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C"))));
	EnemyClasses.Add(EEnemyArchetype::Cutter,
		TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter.BP_Enemy_Cutter_C"))));
	// Biochemical_Monster_1 (Scripts/Editor/setup_marksman_animation.py); missing -> the C++ AMarksmanEnemyCharacter.
	EnemyClasses.Add(EEnemyArchetype::Marksman,
		TSoftClassPtr<AEnemyCharacter>(FSoftObjectPath(TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C"))));
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
		{ LOCTEXT("Commander", "Commander"), FLinearColor::FromSRGBColor(FColor(0x20, 0x80, 0xEC)), FVector(0.f, 0.f, 0.f), 15.f, EOperativeRole::Commander, 25.f, 90.f },
		{ LOCTEXT("Engineer", "Engineer"), FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F)), FVector(-280.f, -260.f, 0.f), 25.f, EOperativeRole::Engineer, 30.f, 75.f },
		{ LOCTEXT("Medic", "Medic-Sapper"), FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33)), FVector(-280.f, 260.f, 0.f), 20.f, EOperativeRole::MedicSapper, 35.f, 85.f } };
	// User request 2026-10-09: the Medic-Sapper is the Female Soldier (same ABP_Operative) and also carries the sniper rifle.
	SquadRoster[2].BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Female_Soldier/Mesh/SK_Female_soldier.SK_Female_soldier")));
	SquadRoster[2].ExtraWeapons.Add(TSoftObjectPtr<UWeaponDataAsset>(FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_sniper_rifle.DA_Weapon_sniper_rifle"))));
	RecruitSusanin = { LOCTEXT("Susanin", "Ivan Susanin"), FLinearColor(0.95f, 0.28f, 0.72f), FVector::ZeroVector, 20.f, EOperativeRole::Recruit, 45.f, 70.f };
}

void ACodexTacticsGameMode::StartPlay()
{
	AITuning::ApplyFile(AITuning::GetDefaultPath()); // before any actor's BeginPlay reads a Codex.* tunable
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath()); // Wave Editor weapon power onto DA_Weapon_* (in memory)
	SquadROE::ApplyFile(SquadROE::GetDefaultPath()); // Commander Mode tactical ROE (Wave Editor "Squad tactics")
	EnemyPerception::ApplyFile(EnemyPerception::GetDefaultPath()); // patrol sight / hearing / smell, trap search
	ApplyLevelConfig();
	Super::StartPlay();
	SpawnSquad();
	// Level objects tagged "Vault" (Godot "vault" group): the squad paths across and vaults them (barricades do it themselves).
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(VaultNavigation::VaultTag))
		{
			VaultNavigation::MakeVaultable(*It);
		}
	}
}

void ACodexTacticsGameMode::ApplyLevelConfig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// The level JSON (Wave Editor) first, the imported asset as the fallback.
	ULevelConfigAsset* Level = nullptr;
	// -LevelJson=<file name or absolute path> replaces the map's level for a run (bot A / B batches on other waves).
	FString JsonFile = LevelJsonFile;
	FParse::Value(FCommandLine::Get(), TEXT("LevelJson="), JsonFile);
	if (!JsonFile.IsEmpty())
	{
		FLevelCombatConfig Parsed;
		FString Error;
		if (LevelJsonRules::LoadLevel(JsonFile, Parsed, Error))
		{
			Level = NewObject<ULevelConfigAsset>(this, TEXT("LevelFromJson"));
			Level->Config = MoveTemp(Parsed);
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Level JSON %s: %s — using %s"), *JsonFile, *Error, *LevelConfig.ToString());
		}
	}
	if (!Level && !LevelConfig.IsNull())
	{
		Level = LevelConfig.LoadSynchronous();
	}
	ActiveLevelConfig = Level;
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
		SpawnOperative(Entry, Index, StartTransform.GetLocation() + Facing.RotateVector(Entry.Offset), Facing);
	}
}

AOperativeCharacter* ACodexTacticsGameMode::SpawnOperative(const FSquadMemberSpawn& Entry, int32 SquadIndex, const FVector& Location,
	const FRotator& Facing, bool bRecruited)
{
	UWorld* World = GetWorld();
	TSubclassOf<AOperativeCharacter> SpawnClass = OperativeBlueprint.IsNull() ? nullptr : OperativeBlueprint.LoadSynchronous();
	if (!SpawnClass)
	{
		SpawnClass = OperativeClass;
	}
	if (!World || !SpawnClass)
	{
		return nullptr;
	}
	{
		AOperativeCharacter* Operative = World->SpawnActorDeferred<AOperativeCharacter>(SpawnClass,
			FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Operative)
		{
			return nullptr;
		}
		Operative->bRecruited = bRecruited;
		Operative->SquadIndex = SquadIndex;
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
		WeaponTuning::ApplyGrenades(*Operative);
		if (!Entry.BodyMesh.IsNull())
		{
			Operative->ApplyBodyMeshOverride(Entry.BodyMesh.LoadSynchronous());
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
		for (const TSoftObjectPtr<UWeaponDataAsset>& Extra : Entry.ExtraWeapons)
		{
			Operative->AddArsenalWeapon(Extra.LoadSynchronous()); // reserve = the weapon's DefaultReserveAmmo (weapons_tuning.json)
		}
		Operative->ApplyBodyColor();
		return Operative;
	}
}

#undef LOCTEXT_NAMESPACE
