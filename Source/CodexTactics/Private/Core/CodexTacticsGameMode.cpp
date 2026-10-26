#include "Core/CodexTacticsGameMode.h"
#include "Core/LoadoutRules.h"
#include "Data/LevelJsonRules.h"
#include "GameFlow/CampaignCarry.h"
#include "GameFlow/CampaignSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Data/DialogueSequenceAsset.h"
#include "Characters/OperativeBalance.h"
#include "CodexTactics.h"
#include "Combat/SuppressionRules.h"
#include "Combat/WaveSubsystem.h"
#include "Core/LevelFlowRules.h"
#include "Core/SquadSpawnRules.h"
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
#include "Misc/PackageName.h"
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
#include "Interactables/CampfireActor.h"
#include "Interactables/PortableStoveActor.h"
#include "Combat/FirePuddleActor.h"
#include "Combat/SignalFlareActor.h"
#include "Data/TacticalHeatTuning.h"
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
	PortableStoveClass = APortableStoveActor::StaticClass();
	CampfireClass = ACampfireActor::StaticClass();
	SignalFlareClass = ASignalFlareActor::StaticClass();
	FirePuddleClass = AFirePuddleActor::StaticClass();
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
	// 2026-10-21: her single-slot optimized body (the fallback when her member Blueprint is missing).
	SquadRoster[2].BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Characters/Operatives/Optimized/Female_Soldier/SK_Female_Soldier_1slot.SK_Female_Soldier_1slot")));
	// User request 2026-10-11: her own Blueprint (a BP_Operative child the user dresses, e.g. a backpack). Missing -> BP_Operative + BodyMesh.
	SquadRoster[2].MemberBlueprint = TSoftClassPtr<AOperativeCharacter>(
		FSoftObjectPath(TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C")));
	SquadRoster[2].ExtraWeapons.Add(TSoftObjectPtr<UWeaponDataAsset>(FSoftObjectPath(TEXT("/Game/Data/Weapons/DA_Weapon_sniper_rifle.DA_Weapon_sniper_rifle"))));
	RecruitSusanin = { LOCTEXT("Susanin", "Ivan Susanin"), FLinearColor(0.95f, 0.28f, 0.72f), FVector::ZeroVector, 20.f, EOperativeRole::Recruit, 45.f, 70.f };
}

void ACodexTacticsGameMode::StartPlay()
{
	AITuning::ApplyFile(AITuning::GetDefaultPath()); // before any actor's BeginPlay reads a Codex.* tunable
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath()); // Wave Editor weapon power onto DA_Weapon_* (in memory)
	SquadROE::ApplyFile(SquadROE::GetDefaultPath()); // Commander Mode tactical ROE (Wave Editor "Squad tactics")
	EnemyPerception::ApplyFile(EnemyPerception::GetDefaultPath()); // patrol sight / hearing / smell, trap search
	TacticalHeatTuning::ApplyFile(TacticalHeatTuning::GetDefaultPath()); // Sprint 15 heat sources + their starting loadout
	SuppressionRules::ReloadConfig(); // suppressive fire tunables (Content/Data/Combat/suppression.json, 2026-10-26)
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

const ULevelConfigAsset* ACodexTacticsGameMode::PeekLevelConfig()
{
	if (!ActiveLevelConfig)
	{
		ActiveLevelConfig = LoadLevelConfig();
	}
	return ActiveLevelConfig;
}

void ACodexTacticsGameMode::ApplyLevelConfig()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ULevelConfigAsset* Level = LoadLevelConfig();
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

ULevelConfigAsset* ACodexTacticsGameMode::LoadLevelConfig()
{
	// The level JSON (Wave Editor) first, the imported asset as the fallback.
	ULevelConfigAsset* Level = nullptr;
	// -LevelJson=<file name or absolute path> replaces the map's level for a run (bot A / B batches on other waves).
	// Without it, Content/Data/LevelJson/<MapName>.json when the map has its own file, else LevelJsonFile.
	FString CommandLineJson;
	FParse::Value(FCommandLine::Get(), TEXT("LevelJson="), CommandLineJson);
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
	// 2026-10-22 b: a level reached through the campaign reads its own file - the -LevelJson= of the run was meant for the first map.
	const UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(this);
	if (Campaign && Campaign->GetCarryFor(GetWorld()) && !CommandLineJson.IsEmpty())
	{
		UE_LOG(LogCodexTactics, Log, TEXT("Level JSON: -LevelJson=%s ignored on the campaign arrival map %s"), *CommandLineJson, *MapName);
		CommandLineJson.Reset();
	}
	const FString JsonFile = LevelJsonRules::ResolveLevelJsonFile(MapName, LevelJsonFile, CommandLineJson);
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
	return Level;
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
	// 2026-10-22 b: the squad that left the previous campaign level (UCampaignSubsystem): its progression and kit, the fallen stay behind.
	const UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(this);
	const FCampaignSquadCarry* Carry = Campaign ? Campaign->GetCarryFor(World) : nullptr;
	if (TActorIterator<AOperativeCharacter>(World))
	{
		// The level places its own squad: the carry goes onto the placed operatives.
		for (TActorIterator<AOperativeCharacter> It(World); It && Carry; ++It)
		{
			if (const FCarriedOperative* Carried = It->bRecruited ? CampaignCarry::FindFor(*Carry, It->SquadRole, It->SquadIndex) : nullptr)
			{
				CampaignCarry::Apply(**It, *Carried);
			}
		}
		return;
	}

	const AActor* Start = UGameplayStatics::GetActorOfClass(World, APlayerStart::StaticClass());
	const FTransform StartTransform = Start ? Start->GetActorTransform() : FTransform::Identity;
	const FRotator Facing(0.f, StartTransform.Rotator().Yaw, 0.f);

	int32 Spawned = 0;
	for (int32 Index = 0; Index < SquadRoster.Num(); ++Index)
	{
		const FSquadMemberSpawn& Entry = SquadRoster[Index];
		if (Carry && !CampaignCarry::ShouldSpawn(*Carry, Entry.Role, Index))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Campaign: %s fell on %s - not spawned"), *Entry.DisplayName.ToString(), *Carry->FromMap);
			continue;
		}
		// Bug 2026-10-23: with a fallen mate the living close up on the roster's first spawn spots (the formation's slots), else a follower
		// spawned on the fallen one's spot walks across the start to his slot while the level fades in.
		const FVector Offset = SquadRoster[Spawned++].Offset;
		AOperativeCharacter* Operative = SpawnOperative(Entry, Index, StartTransform.GetLocation() + Facing.RotateVector(Offset), Facing);
		if (const FCarriedOperative* Carried = Operative && Carry ? CampaignCarry::FindFor(*Carry, Entry.Role, Index) : nullptr)
		{
			CampaignCarry::Apply(*Operative, *Carried);
			UE_LOG(LogCodexTactics, Display, TEXT("Campaign: %s arrives from %s - level %d, EXP %d, %d unspent points"), *Entry.DisplayName.ToString(),
				*Carry->FromMap, Operative->Level, Operative->CurrentExp, Operative->UnspentStatPoints);
		}
	}
}

AOperativeCharacter* ACodexTacticsGameMode::SpawnOperative(const FSquadMemberSpawn& Entry, int32 SquadIndex, const FVector& Location,
	const FRotator& Facing, bool bRecruited)
{
	UWorld* World = GetWorld();
	bool bApplyBodyMesh = true;
	const TSubclassOf<AOperativeCharacter> SpawnClass = ResolveMemberClass(Entry, bApplyBodyMesh);
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
		{
			// Sprint 15: heat packs, firewood, flares, the stove with a full canister (tactical_heat.json "starting_loadout").
			const FTacticalHeatConfig& Heat = TacticalHeatTuning::Get();
			const FTacticalHeatLoadout Loadout = TacticalHeatRules::GetLoadout(Heat, static_cast<int32>(Entry.Role));
			// The level's "heat_packs_count" replaces the role kit for the starting squad (Demo 01: 1 per operative; more come from the A1 hut crate).
			const int32 LevelHeatPacks = !bRecruited && ActiveLevelConfig ? ActiveLevelConfig->Config.SquadLoadout.HeatPacksCount : -1;
			Operative->HeatPacksCount = LoadoutRules::StartingHeatPacks(Loadout.HeatPacks, LevelHeatPacks, Heat.HeatPackMaxStack);
			Operative->FirewoodCount = Loadout.Firewood;
			Operative->FlaresCount = FMath::Min(Loadout.Flares, Heat.FlareMaxCarried);
			Operative->PortableStovesCount = FMath::Min(Loadout.Stoves, Heat.StoveMaxCarried);
			Operative->StoveGasSeconds = Operative->PortableStovesCount > 0 ? Heat.StoveGasCapacitySeconds : 0.f;
		}
		if (bApplyBodyMesh && !Entry.BodyMesh.IsNull())
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

TSubclassOf<AOperativeCharacter> ACodexTacticsGameMode::ResolveMemberClass(const FSquadMemberSpawn& Entry, bool& bOutApplyBodyMesh) const
{
	TSubclassOf<AOperativeCharacter> DefaultClass = OperativeBlueprint.IsNull() ? nullptr : OperativeBlueprint.LoadSynchronous();
	if (!DefaultClass)
	{
		DefaultClass = OperativeClass;
	}
	const bool bMemberSet = !Entry.MemberBlueprint.IsNull();
	UClass* MemberClass = nullptr;
	// Checked on disk first: a member Blueprint the user has not created yet falls back without a load warning.
	if (bMemberSet && FPackageName::DoesPackageExist(Entry.MemberBlueprint.ToSoftObjectPath().GetLongPackageName()))
	{
		MemberClass = Entry.MemberBlueprint.LoadSynchronous();
	}
	const SquadSpawnRules::FMemberClassChoice Choice = SquadSpawnRules::ChooseMemberClass(bMemberSet, MemberClass, DefaultClass);
	if (Choice.bMemberBlueprintMissing)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("%s: member Blueprint %s not found - %s with its body mesh"), *Entry.DisplayName.ToString(),
			*Entry.MemberBlueprint.ToString(), *GetNameSafe(Choice.Class));
	}
	else if (bMemberSet)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: member Blueprint %s"), *Entry.DisplayName.ToString(), *GetNameSafe(Choice.Class));
	}
	bOutApplyBodyMesh = Choice.bApplyBodyMesh;
	return Choice.Class;
}

#undef LOCTEXT_NAMESPACE
