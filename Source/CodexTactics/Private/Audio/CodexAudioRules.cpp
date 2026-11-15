#include "Audio/CodexAudioRules.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ECodexMusicState FCodexAudioRules::EvaluateMusicState(
	bool bIsFrontend,
	ECodexGamePhase Phase,
	int32 AliveEnemies,
	bool bSquadSeesEnemy,
	bool bCombatPressureActive,
	bool bHordeActive,
	EWeatherState Weather)
{
	// 1. Menu state: frontend level always takes precedence
	if (bIsFrontend)
	{
		return ECodexMusicState::Menu;
	}

	// 2. Defeat state: match lost
	if (Phase == ECodexGamePhase::GameOver)
	{
		return ECodexMusicState::Defeat;
	}

	// 3. Victory state: wave cleared or all waves completed
	if (Phase == ECodexGamePhase::WaveCleared || Phase == ECodexGamePhase::PostCombat)
	{
		return ECodexMusicState::Victory;
	}

	// 4. Combat states: wave fight in progress
	if (Phase == ECodexGamePhase::WaveCombat)
	{
		if (bHordeActive)
		{
			return ECodexMusicState::CombatHeavy;
		}
		return ECodexMusicState::Combat;
	}

	// 5. Outside wave combat (Exploration, Preparation, Cutscene):
	// Check for active threats near the squad
	if (AliveEnemies > 0 && (bSquadSeesEnemy || bCombatPressureActive))
	{
		return ECodexMusicState::Tension;
	}

	// Check weather environment
	if (Weather == EWeatherState::Blizzard)
	{
		return ECodexMusicState::ExploreCold;
	}

	// Default baseline exploration
	return ECodexMusicState::ExploreCalm;
}

FString FCodexAudioRules::MusicStateToString(ECodexMusicState State)
{
	switch (State)
	{
	case ECodexMusicState::Menu:         return TEXT("menu");
	case ECodexMusicState::ExploreCalm:  return TEXT("explore_calm");
	case ECodexMusicState::ExploreCold:  return TEXT("explore_cold");
	case ECodexMusicState::Tension:      return TEXT("tension");
	case ECodexMusicState::Combat:       return TEXT("combat");
	case ECodexMusicState::CombatHeavy:  return TEXT("combat_heavy");
	case ECodexMusicState::Victory:      return TEXT("victory");
	case ECodexMusicState::Defeat:       return TEXT("defeat");
	default:                             return TEXT("none");
	}
}

ECodexMusicState FCodexAudioRules::StringToMusicState(const FString& StateStr)
{
	if (StateStr.Equals(TEXT("menu"), ESearchCase::IgnoreCase))         return ECodexMusicState::Menu;
	if (StateStr.Equals(TEXT("explore_calm"), ESearchCase::IgnoreCase)) return ECodexMusicState::ExploreCalm;
	if (StateStr.Equals(TEXT("explore_cold"), ESearchCase::IgnoreCase)) return ECodexMusicState::ExploreCold;
	if (StateStr.Equals(TEXT("tension"), ESearchCase::IgnoreCase))      return ECodexMusicState::Tension;
	if (StateStr.Equals(TEXT("combat"), ESearchCase::IgnoreCase))       return ECodexMusicState::Combat;
	if (StateStr.Equals(TEXT("combat_heavy"), ESearchCase::IgnoreCase)) return ECodexMusicState::CombatHeavy;
	if (StateStr.Equals(TEXT("victory"), ESearchCase::IgnoreCase))      return ECodexMusicState::Victory;
	if (StateStr.Equals(TEXT("defeat"), ESearchCase::IgnoreCase))       return ECodexMusicState::Defeat;
	return ECodexMusicState::None;
}

const TArray<FCodexMusicCueDef>& FCodexAudioRules::GetAllMusicCues()
{
	static TArray<FCodexMusicCueDef> MasterCues;
	if (MasterCues.Num() == 0)
	{
		const FString MusicPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Audio/music_cues.json"));
		FString JsonContent;
		if (FFileHelper::LoadFileToString(JsonContent, *MusicPath))
		{
			LoadMusicCuesFromJson(JsonContent, MasterCues);
		}
	}
	return MasterCues;
}

void FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState State, TArray<FName>& OutCues)
{
	OutCues.Reset();
	const TArray<FCodexMusicCueDef>& Cues = GetAllMusicCues();
	for (const FCodexMusicCueDef& Def : Cues)
	{
		if (Def.State == State)
		{
			OutCues.Add(Def.Id);
		}
	}
}

FName FCodexAudioRules::SelectMusicCueForState(ECodexMusicState State, int32 VariantIndex)
{
	TArray<FName> Available;
	GetAvailableMusicCuesForState(State, Available);
	if (Available.Num() == 0)
	{
		return NAME_None;
	}

	const int32 SafeIndex = FMath::Abs(VariantIndex) % Available.Num();
	return Available[SafeIndex];
}

bool FCodexAudioRules::FindMusicCueDef(FName CueId, FCodexMusicCueDef& OutDef)
{
	const TArray<FCodexMusicCueDef>& Cues = GetAllMusicCues();
	for (const FCodexMusicCueDef& Def : Cues)
	{
		if (Def.Id == CueId)
		{
			OutDef = Def;
			return true;
		}
	}
	return false;
}

const TArray<FCodexVoiceLineDef>& FCodexAudioRules::GetAllVoiceLines()
{
	static TArray<FCodexVoiceLineDef> MasterLines;
	if (MasterLines.Num() == 0)
	{
		const FString VoicePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Audio/voice_lines.json"));
		FString JsonContent;
		if (FFileHelper::LoadFileToString(JsonContent, *VoicePath))
		{
			LoadVoiceLinesFromJson(JsonContent, MasterLines);
		}
	}
	return MasterLines;
}

bool FCodexAudioRules::FindVoiceLineDef(FName LineId, FCodexVoiceLineDef& OutDef)
{
	const TArray<FCodexVoiceLineDef>& Lines = GetAllVoiceLines();
	for (const FCodexVoiceLineDef& Def : Lines)
	{
		if (Def.Id == LineId)
		{
			OutDef = Def;
			return true;
		}
	}
	return false;
}

FName FCodexAudioRules::MapEventToVoiceLine(FName EventId, FName Speaker)
{
	if (EventId.IsNone())
	{
		return NAME_None;
	}

	const FString EventStr = EventId.ToString();

	// Direct match with master line IDs
	FCodexVoiceLineDef DirectDef;
	if (FindVoiceLineDef(EventId, DirectDef))
	{
		return EventId;
	}

	// If event has "vo_" prefix (e.g. "vo_cmd_contact"), strip it and test
	if (EventStr.StartsWith(TEXT("vo_")))
	{
		const FName StrippedName(*EventStr.Mid(3));
		if (FindVoiceLineDef(StrippedName, DirectDef))
		{
			return StrippedName;
		}
	}

	// Generic gameplay event mappings
	if (EventStr.Equals(TEXT("OnSoldierDowned"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("SoldierDowned"), ESearchCase::IgnoreCase))
	{
		return TEXT("med_critically_down");
	}

	if (EventStr.Equals(TEXT("OnMineSpotted"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("MineSpotted"), ESearchCase::IgnoreCase))
	{
		return TEXT("med_tripwire");
	}

	if (EventStr.Equals(TEXT("OnRageStarted"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("RageStarted"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_rage_shout");
	}

	if (EventStr.Equals(TEXT("OnGeneratorStarted"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("GeneratorStarted"), ESearchCase::IgnoreCase))
	{
		return TEXT("cmd_generator_starts");
	}

	if (EventStr.Equals(TEXT("OnGeneratorRestored"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("GeneratorRestored"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_generator_restored");
	}

	if (EventStr.Equals(TEXT("OnGeneratorDry"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("GeneratorDry"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_generator_dry");
	}

	if (EventStr.Equals(TEXT("OnSoldierPanicked"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("SoldierPanicked"), ESearchCase::IgnoreCase))
	{
		return TEXT("cmd_panic_flee");
	}

	if (EventStr.Equals(TEXT("OnSoldierCalmed"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("SoldierCalmed"), ESearchCase::IgnoreCase))
	{
		return TEXT("med_patched_up");
	}

	if (EventStr.Equals(TEXT("OnHordeReleased"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("HordeReleased"), ESearchCase::IgnoreCase))
	{
		return TEXT("cmd_wave_advancing");
	}

	if (EventStr.Equals(TEXT("OnAmbushStarted"), ESearchCase::IgnoreCase) ||
		EventStr.Equals(TEXT("AmbushStarted"), ESearchCase::IgnoreCase))
	{
		return TEXT("cmd_ambush");
	}

	if (EventStr.Equals(TEXT("TakeCover"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_take_cover");
	}

	if (EventStr.Equals(TEXT("DashCover"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_dash_other_cover");
	}

	if (EventStr.Equals(TEXT("GuardOn"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_guard_on");
	}

	if (EventStr.Equals(TEXT("GuardOff"), ESearchCase::IgnoreCase))
	{
		return TEXT("eng_guard_off");
	}

	if (EventStr.Equals(TEXT("Reloading"), ESearchCase::IgnoreCase))
	{
		if (Speaker == TEXT("medic_sapper")) return TEXT("med_reloading");
		if (Speaker == TEXT("pmc_op_1"))     return TEXT("pmc1_reloading");
		if (Speaker == TEXT("pmc_op_2"))     return TEXT("pmc2_changing_mags");
		return TEXT("eng_changing_mags");
	}

	return NAME_None;
}

bool FCodexAudioRules::CanPlayVoiceLine(
	FName LineId,
	float CurrentTime,
	const TMap<FName, float>& LastPlayedTimes,
	float CooldownOverride)
{
	FCodexVoiceLineDef Def;
	if (!FindVoiceLineDef(LineId, Def))
	{
		return false;
	}

	const float RequiredCooldown = (CooldownOverride >= 0.f) ? CooldownOverride : Def.CooldownSeconds;
	if (const float* LastTime = LastPlayedTimes.Find(LineId))
	{
		if (CurrentTime - (*LastTime) < RequiredCooldown)
		{
			return false;
		}
	}

	return true;
}

FString FCodexAudioRules::GetExpectedAssetPath(FName Id, bool bIsVoice)
{
	if (bIsVoice)
	{
		FCodexVoiceLineDef Def;
		if (FindVoiceLineDef(Id, Def) && !Def.Speaker.IsNone())
		{
			return FString::Printf(TEXT("/Game/Audio/Voice/%s/SW_%s"), *Def.Speaker.ToString(), *Id.ToString());
		}
		return FString::Printf(TEXT("/Game/Audio/Voice/common/SW_%s"), *Id.ToString());
	}
	return FString::Printf(TEXT("/Game/Audio/Music/%s"), *Id.ToString());
}

bool FCodexAudioRules::DoesSoundAssetExist(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return false;
	}
	return FPackageName::DoesPackageExist(AssetPath);
}

bool FCodexAudioRules::LoadMusicCuesFromJson(const FString& JsonContent, TArray<FCodexMusicCueDef>& OutCues)
{
	OutCues.Reset();
	if (JsonContent.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* CuesArray = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("cues"), CuesArray) || !CuesArray)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *CuesArray)
	{
		TSharedPtr<FJsonObject> CueObj = Value->AsObject();
		if (!CueObj.IsValid())
		{
			continue;
		}

		FCodexMusicCueDef Def;
		Def.Id = FName(*CueObj->GetStringField(TEXT("id")));
		Def.CueGroup = FName(*CueObj->GetStringField(TEXT("cue_group")));
		Def.Variant = FName(*CueObj->GetStringField(TEXT("variant")));
		Def.State = StringToMusicState(CueObj->GetStringField(TEXT("state")));
		Def.bLoop = CueObj->GetBoolField(TEXT("loop"));
		Def.TargetDurationSec = static_cast<float>(CueObj->GetNumberField(TEXT("target_duration_sec")));
		Def.AssetPath = GetExpectedAssetPath(Def.Id, false);

		OutCues.Add(Def);
	}

	return OutCues.Num() > 0;
}

bool FCodexAudioRules::LoadVoiceLinesFromJson(const FString& JsonContent, TArray<FCodexVoiceLineDef>& OutLines)
{
	OutLines.Reset();
	if (JsonContent.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* LinesArray = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("lines"), LinesArray) || !LinesArray)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *LinesArray)
	{
		TSharedPtr<FJsonObject> LineObj = Value->AsObject();
		if (!LineObj.IsValid())
		{
			continue;
		}

		FCodexVoiceLineDef Def;
		Def.Id = FName(*LineObj->GetStringField(TEXT("id")));
		Def.Speaker = FName(*LineObj->GetStringField(TEXT("speaker")));
		Def.Category = FName(*LineObj->GetStringField(TEXT("category")));
		Def.Text = LineObj->GetStringField(TEXT("text"));

		double PriorityVal = 2.0;
		if (LineObj->TryGetNumberField(TEXT("priority"), PriorityVal))
		{
			Def.Priority = static_cast<float>(PriorityVal);
		}
		else
		{
			Def.Priority = 2.0f;
		}

		double CooldownVal = 8.0;
		if (LineObj->TryGetNumberField(TEXT("cooldown_seconds"), CooldownVal))
		{
			Def.CooldownSeconds = static_cast<float>(CooldownVal);
		}
		else
		{
			Def.CooldownSeconds = 8.0f;
		}

		Def.AssetPath = FString::Printf(TEXT("/Game/Audio/Voice/%s/SW_%s"), *Def.Speaker.ToString(), *Def.Id.ToString());

		OutLines.Add(Def);
	}

	return OutLines.Num() > 0;
}
