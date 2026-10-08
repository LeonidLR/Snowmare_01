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
