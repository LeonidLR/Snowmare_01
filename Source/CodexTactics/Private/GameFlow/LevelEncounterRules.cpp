#include "GameFlow/LevelEncounterRules.h"

bool LevelEncounterRules::ParseCombatStart(const FString& Text, ECombatStartMode& OutMode)
{
	const FString Name = Text.TrimStartAndEnd();
	if (Name.IsEmpty() || Name.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
	{
		OutMode = ECombatStartMode::Auto;
		return true;
	}
	if (Name.Equals(TEXT("ambush"), ESearchCase::IgnoreCase))
	{
		OutMode = ECombatStartMode::Ambush;
		return true;
	}
	if (Name.Equals(TEXT("button"), ESearchCase::IgnoreCase))
	{
		OutMode = ECombatStartMode::Button;
		return true;
	}
	return false;
}

FString LevelEncounterRules::CombatStartName(ECombatStartMode Mode)
{
	switch (Mode)
	{
	case ECombatStartMode::Ambush:
		return TEXT("ambush");
	case ECombatStartMode::Button:
		return TEXT("button");
	default:
		return TEXT("auto");
	}
}

bool LevelEncounterRules::IsAmbushStart(ECombatStartMode Mode, bool bLevelHasPatrols)
{
	return Mode == ECombatStartMode::Ambush || (Mode == ECombatStartMode::Auto && bLevelHasPatrols);
}

bool LevelEncounterRules::ShouldStartAmbush(bool bAmbushLevel, ECodexGamePhase Phase, bool bCombatUnlocked)
{
	return bAmbushLevel && Phase == ECodexGamePhase::Exploration && !bCombatUnlocked;
}

bool LevelEncounterRules::IsWorldAIPaused(bool bDialogueOpen, ECodexGamePhase Phase, int32 NumBlockers)
{
	return bDialogueOpen || Phase == ECodexGamePhase::Cutscene || NumBlockers > 0;
}

float LevelEncounterRules::ResolveSearchSeconds(float LevelSeconds, float GlobalSeconds)
{
	return LevelSeconds > 0.f ? LevelSeconds : FMath::Max(GlobalSeconds, 0.f);
}
