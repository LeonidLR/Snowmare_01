#include "Core/SaveGameRules.h"
#include "Dom/JsonObject.h"

namespace
{
	FString SaveRulesString(const FJsonObject& Data, const TCHAR* Field, const FString& Fallback)
	{
		FString Value;
		return Data.TryGetStringField(Field, Value) ? Value : Fallback;
	}
}

ESaveBlockReason SaveGameRules::GetSaveBlockReason(ECodexGamePhase Phase, bool bDialogueOpen)
{
	const ESaveBlockReason ByPhase = GetSaveBlockReason(Phase);
	return ByPhase != ESaveBlockReason::None ? ByPhase : (bDialogueOpen ? ESaveBlockReason::Dialogue : ESaveBlockReason::None);
}

bool SaveGameRules::ParseSlotInfo(const FJsonObject& Data, const FString& SlotName, FSaveSlotInfo& OutInfo)
{
	if (!Data.HasField(TEXT("version")) && !Data.HasField(TEXT("squad")) && !Data.HasField(TEXT("game_state")) && !Data.HasField(TEXT("world_state")))
	{
		return false; // not a save file
	}
	const FString Slot = SanitizeSlotName(SlotName);
	OutInfo = FSaveSlotInfo();
	OutInfo.SlotName = SaveRulesString(Data, TEXT("slot_name"), Slot);
	OutInfo.Title = SaveRulesString(Data, TEXT("title"), Slot);
	double Timestamp = 0.0;
	Data.TryGetNumberField(TEXT("timestamp"), Timestamp);
	OutInfo.Timestamp = static_cast<int64>(Timestamp);
	Data.TryGetStringField(TEXT("datetime_str"), OutInfo.DateTime);
	OutInfo.SaveType = SaveRulesString(Data, TEXT("save_type"), GetSaveType(Slot));
	OutInfo.Author = SaveRulesString(Data, TEXT("author"), TEXT("Commander"));
	OutInfo.StageName = SaveRulesString(Data, TEXT("stage_name"), TEXT("Unknown stage"));
	Data.TryGetStringField(TEXT("squad_summary"), OutInfo.SquadSummary);
	const TSharedPtr<FJsonObject>* GameState = nullptr;
	double Wave = 1.0;
	if (Data.TryGetObjectField(TEXT("game_state"), GameState) && GameState && GameState->IsValid())
	{
		(*GameState)->TryGetNumberField(TEXT("current_wave_index"), Wave);
	}
	OutInfo.Wave = static_cast<int32>(Wave);
	const TArray<TSharedPtr<FJsonValue>>* SquadList = nullptr;
	OutInfo.SquadCount = Data.TryGetArrayField(TEXT("squad"), SquadList) ? SquadList->Num() : 0;
	double Version = 1.0;
	Data.TryGetNumberField(TEXT("version"), Version);
	OutInfo.Version = static_cast<int32>(Version);
	Data.TryGetStringField(TEXT("map"), OutInfo.MapName);
	return true;
}

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
	if (Lower.StartsWith(GetCheckpointSlotName()))
	{
		return TEXT("checkpoint");
	}
	if (Lower.StartsWith(TEXT("auto")))
	{
		return TEXT("autosave"); // a legacy automatic save (rev 5f: nothing writes one any more)
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

FString SaveGameRules::GetCheckpointSlotName()
{
	return TEXT("checkpoint");
}

FString SaveGameRules::GetQuicksaveSlotName()
{
	return TEXT("quicksave");
}

bool SaveGameRules::IsListedSaveType(const FString& SaveType)
{
	return SaveType != TEXT("autosave");
}

FString SaveGameRules::GetSaveTypeLabel(const FString& SaveType)
{
	if (SaveType == TEXT("checkpoint"))
	{
		return TEXT("CHECKPOINT");
	}
	if (SaveType == TEXT("quicksave"))
	{
		return TEXT("QUICKSAVE");
	}
	return SaveType == TEXT("autosave") ? TEXT("OLD AUTOSAVE") : TEXT("SAVE");
}

int32 SaveGameRules::PickQuickLoadIndex(const TArray<FSaveSlotInfo>& Slots)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FString& Type = Slots[Index].SaveType;
		if ((Type == TEXT("quicksave") || Type == TEXT("checkpoint")) && (Best == INDEX_NONE || Slots[Index].Timestamp > Slots[Best].Timestamp))
		{
			Best = Index;
		}
	}
	return Best;
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
	case ESaveBlockReason::Dialogue:
		return NSLOCTEXT("SaveGameRules", "BlockedDialogue", "Saving is disabled during a conversation");
	default:
		return FText::GetEmpty();
	}
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
