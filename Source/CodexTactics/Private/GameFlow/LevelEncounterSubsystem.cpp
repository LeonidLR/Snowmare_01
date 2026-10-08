#include "GameFlow/LevelEncounterSubsystem.h"

#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/EnemyPerception.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterRules.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "LevelEncounterSubsystem"

void ULevelEncounterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Level-placed patrols (their properties are loaded before BeginPlay; runtime spawns do not count).
	bLevelHasPatrols = TActorIterator<APatrolRouteActor>(&InWorld) ? true : false;
	for (TActorIterator<AEnemyCharacter> It(&InWorld); It && !bLevelHasPatrols; ++It)
	{
		bLevelHasPatrols = It->AssignedPatrolRoute != nullptr || It->GetEscortLeader() != nullptr;
	}
	FString CommandLineMode;
	ECombatStartMode Parsed = ECombatStartMode::Auto;
	if (FParse::Value(FCommandLine::Get(), TEXT("CombatStart="), CommandLineMode) && LevelEncounterRules::ParseCombatStart(CommandLineMode, Parsed))
	{
		SetCombatStartOverride(Parsed);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Encounter] patrols on the map: %s, combat start %s"), bLevelHasPatrols ? TEXT("yes") : TEXT("no"),
		*LevelEncounterRules::CombatStartName(GetCombatStartMode()));
}

const FLevelCombatConfig* ULevelEncounterSubsystem::GetLevelConfig() const
{
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	return Level ? &Level->Config : nullptr;
}

ECombatStartMode ULevelEncounterSubsystem::GetCombatStartMode() const
{
	if (bHasOverride)
	{
		return ModeOverride;
	}
	const FLevelCombatConfig* Level = GetLevelConfig();
	return Level ? Level->CombatStart : ECombatStartMode::Auto;
}

bool ULevelEncounterSubsystem::IsAmbushCombatStart() const
{
	return LevelEncounterRules::IsAmbushStart(GetCombatStartMode(), bLevelHasPatrols);
}

bool ULevelEncounterSubsystem::NotifyHostileContact(EAmbushTrigger Trigger, AActor* Source)
{
	UWorld* World = GetWorld();
	UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Flow || !LevelEncounterRules::ShouldStartAmbush(IsAmbushCombatStart(), Flow->GetPhase(), Flow->IsCombatUnlocked()))
	{
		return false;
	}
	if (Flow->StartAmbushCombat() != EGameFlowResult::Ok)
	{
		return false;
	}
	++AmbushStarts;
	LastTrigger = Trigger;
	UE_LOG(LogCodexTactics, Display, TEXT("[Encounter] ambush fight started (%s by %s)"), *UEnum::GetValueAsString(Trigger), *GetNameSafe(Source));
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("Commander", "Commander"), Trigger == EAmbushTrigger::PatrolDetection
			? LOCTEXT("Detected", "⚠️ We've been spotted! Engage!")
			: LOCTEXT("Opened", "⚔️ Open fire! Combat has begun!"));
	}
	OnAmbushCombatStarted.Broadcast(Trigger);
	return true;
}

bool ULevelEncounterSubsystem::NotifyHostileContactIn(UWorld* World, EAmbushTrigger Trigger, AActor* Source)
{
	ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	return Encounter && Encounter->NotifyHostileContact(Trigger, Source);
}

void ULevelEncounterSubsystem::SetCombatStartOverride(ECombatStartMode Mode)
{
	bHasOverride = true;
	ModeOverride = Mode;
}

float ULevelEncounterSubsystem::GetPatrolSearchSeconds() const
{
	if (SearchSecondsOverride > 0.f)
	{
		return SearchSecondsOverride;
	}
	if (const float Tuned = EnemyPerception::GetTuning().SearchSeconds; Tuned >= 0.f)
	{
		return Tuned; // Codex.Patrol.SearchSeconds (Jev AI coach) wins over the level for its experiments
	}
	const FLevelCombatConfig* Level = GetLevelConfig();
	return LevelEncounterRules::ResolveSearchSeconds(Level ? Level->PatrolSearchSeconds : -1.f, EnemyPerception::GetSearch().DurationSeconds);
}

float ULevelEncounterSubsystem::GetPatrolSearchSecondsIn(const UWorld* World)
{
	const ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
	return Encounter ? Encounter->GetPatrolSearchSeconds() : EnemyPerception::GetSearch().DurationSeconds;
}

namespace LevelEncounterConsole
{
	static void CombatStartCommand(const TArray<FString>& Args, UWorld* World)
	{
		ULevelEncounterSubsystem* Encounter = World ? World->GetSubsystem<ULevelEncounterSubsystem>() : nullptr;
		if (!Encounter)
		{
			return;
		}
		ECombatStartMode Mode = ECombatStartMode::Auto;
		if (Args.Num() > 0)
		{
			if (Args[0].Equals(TEXT("level"), ESearchCase::IgnoreCase))
			{
				Encounter->ClearCombatStartOverride();
			}
			else if (LevelEncounterRules::ParseCombatStart(Args[0], Mode))
			{
				Encounter->SetCombatStartOverride(Mode);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Encounter] combat start %s (patrols on the map: %s) -> %s"),
			*LevelEncounterRules::CombatStartName(Encounter->GetCombatStartMode()), Encounter->LevelHasPatrols() ? TEXT("yes") : TEXT("no"),
			Encounter->IsAmbushCombatStart() ? TEXT("ambush") : TEXT("button"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.CombatStart"),
		TEXT("Shows / sets how this level's fight starts: auto | ambush | button | level (back to the level JSON)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CombatStartCommand));
}

#undef LOCTEXT_NAMESPACE
