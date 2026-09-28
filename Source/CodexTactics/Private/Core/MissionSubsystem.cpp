#include "Core/MissionSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Core/MissionRules.h"
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
	LastPhase = Phase;
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	FText PhaseObjective;
	if (Flow && MissionRules::GetPhaseObjective(Phase, Flow->GetWaveIndex(), Flow->GetPreparationTimeRemaining(), bCombatFinished, PhaseObjective))
	{
		SetObjective(PhaseObjective);
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

void UMissionSubsystem::RestartMission()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Mission restart"));
	UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
}

#undef LOCTEXT_NAMESPACE
