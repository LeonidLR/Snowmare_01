#include "Core/SaveGameRules.h"

FString SaveGameRules::SanitizeSlotName(const FString& Raw)
{
	FString Slot = Raw.TrimStartAndEnd();
	if (Slot.IsEmpty())
	{
		return TEXT("Leonid_01");
	}
	for (const TCHAR* Invalid : { TEXT("\\"), TEXT("/"), TEXT(":"), TEXT("*"), TEXT("?"), TEXT("\""), TEXT("<"), TEXT(">"), TEXT("|") })
	{
		Slot.ReplaceInline(Invalid, TEXT("_"));
	}
	Slot.TrimStartAndEndInline();
	Slot.RemoveFromStart(TEXT("."));
	Slot.RemoveFromEnd(TEXT("."));
	return Slot.IsEmpty() ? FString(TEXT("Leonid_01")) : Slot;
}

FString SaveGameRules::GetSaveType(const FString& SlotName)
{
	const FString Lower = FText::FromString(SlotName).ToLower().ToString();
	if (Lower.Contains(TEXT("auto")))
	{
		return TEXT("autosave");
	}
	if (Lower.Contains(TEXT("quick")) || Lower.Contains(TEXT("быстр")) /* legacy Russian slot names ("Быстрое") */) // cyrillic-ok: legacy Russian data
	{
		return TEXT("quicksave");
	}
	return TEXT("manual");
}

FString SaveGameRules::SuggestNextSlotName(const TArray<FString>& ExistingSlots, const FString& Prefix)
{
	const FString Clean = Prefix.TrimStartAndEnd();
	int32 Max = 0;
	for (const FString& Slot : ExistingSlots)
	{
		if (Slot.StartsWith(Clean + TEXT("_")))
		{
			const FString Rest = Slot.RightChop(Clean.Len() + 1);
			if (!Rest.IsEmpty() && Rest.IsNumeric() && !Rest.Contains(TEXT(".")) && !Rest.Contains(TEXT("-")))
			{
				Max = FMath::Max(Max, FCString::Atoi(*Rest));
			}
		}
	}
	return FString::Printf(TEXT("%s_%02d"), *Clean, Max + 1);
}

FString SaveGameRules::GetStageName(const FQuestChainState& Quests, bool bGateOpen, bool bWaveActive, bool bPreparation,
	int32 WaveIndex, bool bSoloMode)
{
	FString Stage;
	if (bWaveActive)
	{
		Stage = FString::Printf(TEXT("Defence: Wave %d"), WaveIndex);
	}
	else if (bPreparation)
	{
		Stage = FString::Printf(TEXT("Defence preparation: Wave %d"), WaveIndex);
	}
	else if (bGateOpen)
	{
		Stage = TEXT("Inner yard (Gate open)");
	}
	else if (Quests.bIsGatePowered)
	{
		Stage = TEXT("Checkpoint (Blast gate powered)");
	}
	else if (Quests.bIsGeneratorRunning)
	{
		Stage = TEXT("Checkpoint (Generator running)");
	}
	else if (Quests.bHasFuelCanister)
	{
		Stage = TEXT("Checkpoint (Fuel secured)");
	}
	else if (Quests.bHasEmptyCanister)
	{
		Stage = TEXT("Checkpoint perimeter (Find diesel)");
	}
	else
	{
		Stage = TEXT("Checkpoint perimeter (Find canister)");
	}
	return bSoloMode ? Stage + TEXT(" [Solo]") : Stage;
}

FString SaveGameRules::GetSquadSummary(const TArray<TPair<float, float>>& HealthAndMax)
{
	if (HealthAndMax.IsEmpty())
	{
		return TEXT("Squad: 0 operatives");
	}
	int32 Alive = 0;
	float TotalPct = 0.f;
	for (const TPair<float, float>& Entry : HealthAndMax)
	{
		if (Entry.Key > 0.f)
		{
			++Alive;
			TotalPct += Entry.Key / FMath::Max(Entry.Value, 1.f) * 100.f;
		}
	}
	return FString::Printf(TEXT("Operatives: %d/%d | HP: %d%%"), Alive, HealthAndMax.Num(), FMath::RoundToInt(TotalPct / HealthAndMax.Num()));
}

FString SaveGameRules::GetAutosaveSlotName()
{
	return TEXT("autosave");
}

ESaveBlockReason SaveGameRules::GetSaveBlockReason(ECodexGamePhase Phase)
{
	switch (Phase)
	{
	case ECodexGamePhase::WaveCombat:
		return ESaveBlockReason::Combat;
	case ECodexGamePhase::Cutscene:
		return ESaveBlockReason::Cutscene;
	case ECodexGamePhase::GameOver:
		return ESaveBlockReason::GameOver;
	default:
		return ESaveBlockReason::None;
	}
}

FText SaveGameRules::GetSaveBlockText(ESaveBlockReason Reason)
{
	switch (Reason)
	{
	case ESaveBlockReason::Combat:
		return NSLOCTEXT("SaveGameRules", "BlockedCombat", "Saving is disabled during combat");
	case ESaveBlockReason::Cutscene:
		return NSLOCTEXT("SaveGameRules", "BlockedCutscene", "Saving is disabled while the battle begins");
	case ESaveBlockReason::GameOver:
		return NSLOCTEXT("SaveGameRules", "BlockedGameOver", "Saving is disabled after the mission failed");
	default:
		return FText::GetEmpty();
	}
}

EAutosaveMoment SaveGameRules::GetAutosaveMoment(ECodexGamePhase OldPhase, ECodexGamePhase NewPhase)
{
	if (NewPhase == ECodexGamePhase::WaveCombat && OldPhase != ECodexGamePhase::WaveCombat && OldPhase != ECodexGamePhase::GameOver)
	{
		return EAutosaveMoment::BeforeCombat;
	}
	if (NewPhase == ECodexGamePhase::WaveCleared && OldPhase == ECodexGamePhase::WaveCombat)
	{
		return EAutosaveMoment::AfterCombat;
	}
	return EAutosaveMoment::None;
}

FSaveLoadFlow SaveGameRules::ResolveLoadFlow(int32 Version, ECodexGamePhase SavedPhase, bool bCombatUnlocked, bool bPreparationActive,
	bool bWaveActive, int32 WaveIndex, int32 TotalWaves, bool bAmbushFight, bool bAmbushSingleFight)
{
	FSaveLoadFlow Flow;
	const int32 Wave = FMath::Max(1, WaveIndex);
	auto Preparation = [&Flow](int32 InWave)
	{
		Flow.Phase = ECodexGamePhase::Preparation;
		Flow.WaveIndex = FMath::Max(1, InWave);
		Flow.bCombatUnlocked = true;
	};
	auto Exploration = [&Flow](bool bUnlocked, int32 InWave)
	{
		Flow.Phase = ECodexGamePhase::Exploration;
		Flow.WaveIndex = FMath::Max(0, InWave);
		Flow.bCombatUnlocked = bUnlocked;
	};
	if (Version < 2)
	{
		// Godot / version 1: the flow resumes in the saved wave's preparation.
		if (bCombatUnlocked && (bPreparationActive || bWaveActive))
		{
			Preparation(Wave);
		}
		else
		{
			Exploration(bCombatUnlocked, WaveIndex);
		}
		return Flow;
	}
	switch (SavedPhase)
	{
	case ECodexGamePhase::Preparation:
		Preparation(Wave);
		break;
	case ECodexGamePhase::Cutscene:
		Preparation(1);
		break;
	case ECodexGamePhase::WaveCombat:
		if (bAmbushFight)
		{
			Exploration(false, 0); // before the ambush: the patrols are back on their routes
		}
		else
		{
			Preparation(Wave);
		}
		break;
	case ECodexGamePhase::WaveCleared:
		if (Wave >= FMath::Max(1, TotalWaves) || (bAmbushFight && bAmbushSingleFight))
		{
			Exploration(false, Wave); // the battle is won (FinishPostCombat locks the combat zone again)
		}
		else
		{
			Preparation(Wave + 1);
		}
		break;
	case ECodexGamePhase::PostCombat:
		Exploration(false, Wave);
		break;
	default:
		Exploration(bCombatUnlocked, WaveIndex);
		break;
	}
	return Flow;
}

FString SaveGameRules::PhaseToString(ECodexGamePhase Phase)
{
	const UEnum* Enum = StaticEnum<ECodexGamePhase>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Phase)) : FString();
}

bool SaveGameRules::ParsePhase(const FString& Text, ECodexGamePhase& OutPhase)
{
	const UEnum* Enum = StaticEnum<ECodexGamePhase>();
	const int64 Value = Enum && !Text.IsEmpty() ? Enum->GetValueByNameString(Text) : INDEX_NONE;
	if (Value == INDEX_NONE)
	{
		return false;
	}
	OutPhase = static_cast<ECodexGamePhase>(Value);
	return true;
}
