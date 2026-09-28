#include "Core/MissionSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Data/DialogueSequenceAsset.h"
#include "UI/DialogueSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Quests/QuestSubsystem.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "MissionSubsystem"

void UMissionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Objective = MissionRules::GetStartObjective();
	StartMode = EMissionStartMode::None;
	if (UQuestSubsystem* Quests = InWorld.GetSubsystem<UQuestSubsystem>())
	{
		Quests->OnObjectiveChanged.AddDynamic(this, &UMissionSubsystem::HandleQuestObjectiveChanged);
	}
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UMissionSubsystem::HandleGameFlowChanged);
		LastPhase = Flow->GetPhase();
	}
	if (UWaveSubsystem* Waves = InWorld.GetSubsystem<UWaveSubsystem>())
	{
		Waves->OnWaveStarted.AddDynamic(this, &UMissionSubsystem::HandleWaveStarted);
	}

	// Godot _ready: Ctrl + X repeats the last mode, otherwise the start menu.
	UMissionSessionSubsystem* Session = InWorld.GetGameInstance() ? InWorld.GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	const TCHAR* CommandLine = FCommandLine::Get();
	const bool bSkipMenu = !FParse::Param(CommandLine, TEXT("ForceMainMenu"))
		&& (FParse::Param(CommandLine, TEXT("NoMainMenu")) || FString(CommandLine).Contains(TEXT("-ExecCmds")));
	const EMissionStartMode AutoMode = MissionRules::GetAutoStartMode(Session && Session->bQuickRestart,
		Session ? Session->LastMode : EMissionStartMode::None, bSkipMenu);
	bHeadlessStart = bSkipMenu && !(Session && Session->bQuickRestart);
	if (Session)
	{
		Session->bQuickRestart = false;
	}
	if (AutoMode == EMissionStartMode::None)
	{
		OpenMainMenu();
	}
	else
	{
		StartMission(AutoMode);
	}
}

void UMissionSubsystem::OpenMainMenu()
{
	bMainMenuOpen = true;
	StartMode = EMissionStartMode::None;
	UGameplayStatics::SetGamePaused(GetWorld(), true);
	OnMainMenuChanged.Broadcast(true);
}

void UMissionSubsystem::StartMission(EMissionStartMode Mode)
{
	if (Mode == EMissionStartMode::None)
	{
		return;
	}
	StartMode = Mode;
	if (UMissionSessionSubsystem* Session = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>() : nullptr)
	{
		Session->LastMode = Mode;
	}
	if (bMainMenuOpen)
	{
		bMainMenuOpen = false;
		UGameplayStatics::SetGamePaused(GetWorld(), false);
		OnMainMenuChanged.Broadcast(false);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Mission start mode: %s"), *UEnum::GetValueAsString(Mode));
	if (Mode == EMissionStartMode::Combat)
	{
		StartCombatMode();
		return;
	}
	SetObjective(MissionRules::GetModeObjective(Mode));
	// Godot: the intro briefing in the bottom dialogue window, the radio line only without it.
	UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	const UDialogueSequenceAsset* Intro = bHeadlessStart ? nullptr : LoadDialogue(&ACodexTacticsGameMode::DialogueMissionStart);
	if (Intro && Dialogue)
	{
		Dialogue->StartDialogue(Intro);
	}
	else
	{
		PostRadio(LOCTEXT("Commander", "Командир"), MissionRules::GetModeRadio(Mode));
	}
}

const UDialogueSequenceAsset* UMissionSubsystem::LoadDialogue(TSoftObjectPtr<UDialogueSequenceAsset> ACodexTacticsGameMode::* Member) const
{
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	return GameMode ? (GameMode->*Member).LoadSynchronous() : nullptr;
}

void UMissionSubsystem::PostRadio(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

void UMissionSubsystem::HandleVictoryDialogueFinished()
{
	SetObjective(MissionRules::GetAfterVictoryObjective());
	PostRadio(LOCTEXT("HQ", "ШТАБ"), MissionRules::GetVictoryRadio());
}

void UMissionSubsystem::StartCombatMode()
{
	UWorld* World = GetWorld();
	if (UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>())
	{
		Quests->CompleteChainForCombat();
	}
	// Godot _on_start_combat_pressed: squad behind the gate, full health, no cold.
	const AActor* CombatStart = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CombatStartTag))
		{
			CombatStart = *It;
			break;
		}
	}
	if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		// Godot offsets: commander (0, -18), engineer (-2.8, -20.5), medic (2.8, -20.5) -> 2.5 m behind, 2.8 m aside.
		const FVector Offsets[] = { FVector::ZeroVector, FVector(-250.f, -280.f, 0.f), FVector(-250.f, 280.f, 0.f) };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			if (CombatStart)
			{
				const FRotator Facing(0.f, CombatStart->GetActorRotation().Yaw, 0.f);
				Member->StopOperative();
				Member->TeleportTo(CombatStart->GetActorLocation() + Facing.RotateVector(Offsets[Index % 3]) + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight()),
					Facing);
			}
			Member->HealthComponent->Heal(Member->HealthComponent->GetMaxHealth());
			Member->ColdLevel = 0.f; // the cold component picks the tier up on its next step
		}
	}
	if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerCombatZone();
	}
}

void UMissionSubsystem::SetObjective(const FText& NewObjective)
{
	if (!Objective.EqualTo(NewObjective))
	{
		Objective = NewObjective;
		UE_LOG(LogCodexTactics, Log, TEXT("Objective: %s"), *Objective.ToString());
		OnObjectiveChanged.Broadcast(Objective);
	}
}

void UMissionSubsystem::HandleQuestObjectiveChanged(const FText& QuestObjective)
{
	SetObjective(QuestObjective);
}

void UMissionSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (Phase == LastPhase)
	{
		return;
	}
	bCombatFinished |= LastPhase == ECodexGamePhase::PostCombat;
	const bool bCutsceneEnded = LastPhase == ECodexGamePhase::Cutscene && Phase == ECodexGamePhase::Preparation;
	LastPhase = Phase;
	if (bCutsceneEnded)
	{
		// Godot _end_cutscene_and_start_pause: every operative starts the preparation warm and at full health.
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				Member->HealthComponent->Heal(Member->HealthComponent->GetMaxHealth());
			}
		}
	}
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	FText PhaseObjective;
	if (Flow && MissionRules::GetPhaseObjective(Phase, Flow->GetWaveIndex(), Flow->GetPreparationTimeRemaining(), bCombatFinished, PhaseObjective))
	{
		SetObjective(PhaseObjective);
	}
	// Godot play_dialogue: story lines in the message feed (radio fallbacks without the assets).
	UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>();
	if (Phase == ECodexGamePhase::Preparation && Flow)
	{
		const bool bFirst = Flow->GetWaveIndex() <= 1;
		const UDialogueSequenceAsset* Lines = LoadDialogue(bFirst ? &ACodexTacticsGameMode::DialoguePreparationStarted : &ACodexTacticsGameMode::DialogueWaveRest);
		if (Lines && Dialogue)
		{
			Dialogue->PlayInFeed(Lines);
		}
		else if (bFirst)
		{
			PostRadio(LOCTEXT("Commander", "Командир"), MissionRules::GetPreparationRadio());
		}
		else
		{
			PostRadio(LOCTEXT("HQ", "ШТАБ"), MissionRules::GetWaveRestRadio(Flow->GetPreparationTimeRemaining()));
		}
	}
	else if (Phase == ECodexGamePhase::PostCombat)
	{
		const UDialogueSequenceAsset* Lines = LoadDialogue(&ACodexTacticsGameMode::DialogueVictory);
		if (Lines && Dialogue)
		{
			Dialogue->PlayInFeed(Lines, FSimpleDelegate::CreateUObject(this, &UMissionSubsystem::HandleVictoryDialogueFinished));
		}
		else
		{
			HandleVictoryDialogueFinished();
		}
	}
}

void UMissionSubsystem::HandleWaveStarted(int32 WaveIndex, int32 TotalEnemies)
{
	SetObjective(MissionRules::GetWaveObjective(WaveIndex, TotalEnemies));
}

void UMissionSubsystem::TriggerMissionFailed(AOperativeCharacter* FallenOperative)
{
	if (bMissionFailed)
	{
		return;
	}
	bMissionFailed = true;
	const FText Name = FallenOperative ? FallenOperative->DisplayName : LOCTEXT("Soldier", "Боец");
	FailureReason = MissionRules::GetFailureReason(Name, FallenOperative ? FallenOperative->ColdLevel : 0.f);
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQ", "ШТАБ"), MissionRules::GetFailureRadio(Name));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Mission failed: %s"), *FailureReason.ToString());
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerGameOver(); // GameOver stops world time
	}
	OnMissionFailed.Broadcast(FailureReason);
}

void UMissionSubsystem::RestartMission(bool bQuick)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (UMissionSessionSubsystem* Session = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>() : nullptr)
	{
		Session->bQuickRestart = bQuick;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Mission restart%s"), bQuick ? TEXT(" (quick)") : TEXT(""));
	UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
}

#undef LOCTEXT_NAMESPACE
