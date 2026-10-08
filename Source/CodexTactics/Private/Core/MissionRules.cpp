#include "Core/MissionRules.h"

#define LOCTEXT_NAMESPACE "MissionRules"

FText MissionRules::GetStartObjective()
{
	return LOCTEXT("Start", "Explore the checkpoint and find a way to open the blast gates");
}

FText MissionRules::GetModeObjective(EMissionStartMode Mode)
{
	switch (Mode)
	{
	case EMissionStartMode::Game:
		return GetStartObjective();
	default:
		return FText::GetEmpty();
	}
}

FText MissionRules::GetModeRadio(EMissionStartMode Mode)
{
	switch (Mode)
	{
	case EMissionStartMode::Game:
		return LOCTEXT("GameRadio", "We're at the main gate of the quarantine checkpoint. Freak frost in the middle of summer... We need to power the gate and get inside.");
	default:
		return FText::GetEmpty();
	}
}

EMissionStartMode MissionRules::GetAutoStartMode(bool bQuickRestart, EMissionStartMode LastMode, bool bSkipMenu)
{
	if (bQuickRestart && LastMode != EMissionStartMode::None)
	{
		return LastMode;
	}
	return bSkipMenu ? EMissionStartMode::Game : EMissionStartMode::None;
}

bool MissionRules::GetPhaseObjective(ECodexGamePhase Phase, int32 WaveIndex, float PreparationSeconds, bool bAfterCombat, FText& OutObjective)
{
	switch (Phase)
	{
	case ECodexGamePhase::Preparation:
		OutObjective = WaveIndex <= 1
			? LOCTEXT("FirstPreparation", "DEFENSE PREPARATION: place turrets, barricades and mines ([Space] for orders)")
			: FText::Format(LOCTEXT("RestPreparation", "PREPARATION: {0} sec until wave {1}. Fortify the defenses!"),
				FMath::TruncToInt(PreparationSeconds), WaveIndex);
		return true;
	case ECodexGamePhase::PostCombat:
		OutObjective = LOCTEXT("Victory", "LINE SECURED: The squad survived! Regrouping...");
		return true;
	case ECodexGamePhase::Exploration:
		if (bAfterCombat)
		{
			OutObjective = GetAfterVictoryObjective();
			return true;
		}
		return false;
	default:
		return false;
	}
}

FText MissionRules::GetPreparationRadio()
{
	return LOCTEXT("PrepRadio", "We're in the checkpoint courtyard! Squad, hold your positions and begin engineering the defensive line!");
}

FText MissionRules::GetWaveRestRadio(float PreparationSeconds)
{
	return FText::Format(LOCTEXT("RestRadio", "Prepare for the next wave! You have {0} seconds to regroup."),
		FMath::TruncToInt(PreparationSeconds));
}

FText MissionRules::GetVictoryRadio()
{
	return LOCTEXT("VictoryRadio", "Outstanding work, squad! The line is fully secure. You may continue exploring.");
}

FText MissionRules::GetAfterVictoryObjective()
{
	return LOCTEXT("Explore", "LINE SECURED: Explore the checkpoint grounds");
}

FText MissionRules::GetWaveObjective(int32 WaveIndex, int32 EnemyCount)
{
	return FText::Format(LOCTEXT("Wave", "DEFENSE: Repel wave {0}! Enemies: {1}"), WaveIndex, EnemyCount);
}

FText MissionRules::GetFailureReason(const FText& OperativeName, float ColdLevel)
{
	return ColdLevel >= FrozenDeathColdLevel
		? FText::Format(LOCTEXT("Frozen", "Operative {0} died of critical hypothermia!"), OperativeName)
		: FText::Format(LOCTEXT("Wounds", "Operative {0} died of battle wounds!"), OperativeName);
}

FText MissionRules::GetFailureRadio(const FText& OperativeName)
{
	return FText::Format(LOCTEXT("Radio", "Warning! Contact with {0} lost. Mission failed."), OperativeName);
}

#undef LOCTEXT_NAMESPACE
