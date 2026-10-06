#include "AI/WorldAIPauseSubsystem.h"

#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterRules.h"
#include "UI/DialogueSubsystem.h"

bool UWorldAIPauseSubsystem::IsWorldAIPaused() const
{
	const UWorld* World = GetWorld();
	const UDialogueSubsystem* Dialogue = World ? World->GetSubsystem<UDialogueSubsystem>() : nullptr;
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return LevelEncounterRules::IsWorldAIPaused(Dialogue && Dialogue->IsDialogueOpen(), Flow ? Flow->GetPhase() : ECodexGamePhase::Exploration,
		GetNumBlockers());
}

bool UWorldAIPauseSubsystem::IsPausedIn(const UWorld* World)
{
	const UWorldAIPauseSubsystem* Gate = World ? World->GetSubsystem<UWorldAIPauseSubsystem>() : nullptr;
	return Gate && Gate->IsWorldAIPaused();
}

void UWorldAIPauseSubsystem::AddPauseBlocker(FName Reason)
{
	++Blockers.FindOrAdd(Reason);
}

void UWorldAIPauseSubsystem::RemovePauseBlocker(FName Reason)
{
	if (int32* Count = Blockers.Find(Reason))
	{
		if (--*Count <= 0)
		{
			Blockers.Remove(Reason);
		}
	}
}

int32 UWorldAIPauseSubsystem::GetNumBlockers() const
{
	int32 Total = 0;
	for (const TPair<FName, int32>& Entry : Blockers)
	{
		Total += Entry.Value;
	}
	return Total;
}
