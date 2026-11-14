#include "Audio/CodexAudioRules.h"
#include "Misc/PackageName.h"
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
		auto AddCue = [](FName Id, ECodexMusicState State, FName Group, FName Variant, bool bLoop, float Duration)
		{
			FCodexMusicCueDef Def;
			Def.Id = Id;
			Def.State = State;
			Def.CueGroup = Group;
			Def.Variant = Variant;
			Def.bLoop = bLoop;
			Def.TargetDurationSec = Duration;
			Def.AssetPath = GetExpectedAssetPath(Id, false);
			MasterCues.Add(Def);
		};

		// 22 Cues defined in Content/Data/Audio/music_cues.json
		AddCue(TEXT("explore_calm_v1"), ECodexMusicState::ExploreCalm, TEXT("explore_calm"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("explore_calm_v2"), ECodexMusicState::ExploreCalm, TEXT("explore_calm"), TEXT("v2"), true, 30.f);

		AddCue(TEXT("explore_cold_v1"), ECodexMusicState::ExploreCold, TEXT("explore_cold"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("explore_cold_v2"), ECodexMusicState::ExploreCold, TEXT("explore_cold"), TEXT("v2"), true, 30.f);
		AddCue(TEXT("explore_cold_v3"), ECodexMusicState::ExploreCold, TEXT("explore_cold"), TEXT("v3"), true, 30.f);
		AddCue(TEXT("explore_cold_v4"), ECodexMusicState::ExploreCold, TEXT("explore_cold"), TEXT("v4"), true, 30.f);
		AddCue(TEXT("explore_cold_v5"), ECodexMusicState::ExploreCold, TEXT("explore_cold"), TEXT("v5"), true, 30.f);

		AddCue(TEXT("tension_v1"), ECodexMusicState::Tension, TEXT("tension"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("tension_v2"), ECodexMusicState::Tension, TEXT("tension"), TEXT("v2"), true, 30.f);

		AddCue(TEXT("combat_v1"), ECodexMusicState::Combat, TEXT("combat"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("combat_v2"), ECodexMusicState::Combat, TEXT("combat"), TEXT("v2"), true, 30.f);
		AddCue(TEXT("combat_v3"), ECodexMusicState::Combat, TEXT("combat"), TEXT("v3"), true, 30.f);
		AddCue(TEXT("combat_v4"), ECodexMusicState::Combat, TEXT("combat"), TEXT("v4"), true, 30.f);
		AddCue(TEXT("combat_v5"), ECodexMusicState::Combat, TEXT("combat"), TEXT("v5"), true, 30.f);

		AddCue(TEXT("combat_heavy_v1"), ECodexMusicState::CombatHeavy, TEXT("combat_heavy"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("combat_heavy_v2"), ECodexMusicState::CombatHeavy, TEXT("combat_heavy"), TEXT("v2"), true, 30.f);

		AddCue(TEXT("victory_sting_v1"), ECodexMusicState::Victory, TEXT("victory_sting"), TEXT("v1"), false, 8.f);
		AddCue(TEXT("victory_sting_v2"), ECodexMusicState::Victory, TEXT("victory_sting"), TEXT("v2"), false, 8.f);

		AddCue(TEXT("defeat_sting_v1"), ECodexMusicState::Defeat, TEXT("defeat_sting"), TEXT("v1"), false, 8.f);
		AddCue(TEXT("defeat_sting_v2"), ECodexMusicState::Defeat, TEXT("defeat_sting"), TEXT("v2"), false, 8.f);

		AddCue(TEXT("menu_v1"), ECodexMusicState::Menu, TEXT("menu"), TEXT("v1"), true, 30.f);
		AddCue(TEXT("menu_v2"), ECodexMusicState::Menu, TEXT("menu"), TEXT("v2"), true, 30.f);
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
		auto AddLine = [](FName Id, FName Speaker, FName Category, const FString& Text, float Priority, float Cooldown)
		{
			FCodexVoiceLineDef Def;
			Def.Id = Id;
			Def.Speaker = Speaker;
			Def.Category = Category;
			Def.Text = Text;
			Def.Priority = Priority;
			Def.CooldownSeconds = Cooldown;
			Def.AssetPath = GetExpectedAssetPath(Id, true);
			MasterLines.Add(Def);
		};

		// 49 Voice lines defined in Content/Data/Audio/voice_lines.json & EVENTS.md
		// Commander (11)
		AddLine(TEXT("cmd_contact"), TEXT("commander"), TEXT("combat_alert"), TEXT("Contact!"), 2.0f, 8.0f);
		AddLine(TEXT("cmd_ambush"), TEXT("commander"), TEXT("combat_alert"), TEXT("Ambush! Hostiles on the level - eliminate them!"), 1.0f, 15.0f);
		AddLine(TEXT("cmd_wave_advancing"), TEXT("commander"), TEXT("combat_wave"), TEXT("Wave advancing! Form defensive perimeter!"), 1.0f, 15.0f);
		AddLine(TEXT("cmd_autonomy_on"), TEXT("commander"), TEXT("squad_order"), TEXT("Free engagement. Hold your sectors."), 2.0f, 6.0f);
		AddLine(TEXT("cmd_autonomy_off"), TEXT("commander"), TEXT("squad_order"), TEXT("Hold fire. Maintain position."), 2.0f, 6.0f);
		AddLine(TEXT("cmd_orders_received"), TEXT("commander"), TEXT("squad_order"), TEXT("Order acknowledged."), 3.0f, 5.0f);
		AddLine(TEXT("cmd_suppressive_fire"), TEXT("commander"), TEXT("combat_order"), TEXT("Suppress that corridor! Keep their heads down!"), 2.0f, 8.0f);
		AddLine(TEXT("cmd_generator_starts"), TEXT("commander"), TEXT("quest_complete"), TEXT("Primary turbine online. Defence grid active."), 2.0f, 15.0f);
		AddLine(TEXT("cmd_gate_exit_open"), TEXT("commander"), TEXT("quest_complete"), TEXT("Sector blast gate unlocked. Proceed to extraction."), 1.0f, 20.0f);
		AddLine(TEXT("cmd_solo_leash"), TEXT("commander"), TEXT("squad_order"), TEXT("Operative out of squad formation. Regroup immediately."), 2.0f, 12.0f);
		AddLine(TEXT("cmd_panic_flee"), TEXT("commander"), TEXT("combat_panic"), TEXT("Morale broken! Fall back to secondary cover!"), 1.0f, 10.0f);

		// Engineer (16)
		AddLine(TEXT("eng_generator_restored"), TEXT("engineer"), TEXT("quest_complete"), TEXT("Generator restarted! Circuit holding!"), 2.0f, 15.0f);
		AddLine(TEXT("eng_fortifications_dismantled"), TEXT("engineer"), TEXT("post_wave"), TEXT("Barrier salvaged. Scrap secured for the team."), 3.0f, 15.0f);
		AddLine(TEXT("eng_generator_dry"), TEXT("engineer"), TEXT("quest_inspect"), TEXT("Fuel cell dry. Need a diesel canister from the motor pool."), 2.0f, 10.0f);
		AddLine(TEXT("eng_draining_diesel"), TEXT("engineer"), TEXT("quest_progress"), TEXT("Siphoning fuel from the APC tank... almost there."), 2.0f, 15.0f);
		AddLine(TEXT("eng_found_canister"), TEXT("engineer"), TEXT("loot_pickup"), TEXT("Found an intact fuel canister. Heading to the generator."), 2.0f, 15.0f);
		AddLine(TEXT("eng_corner_peek"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Peeking the corner. Lane is hot."), 2.0f, 12.0f);
		AddLine(TEXT("eng_hugging_wall"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Covered by the wall. Eyes on the breach."), 2.0f, 12.0f);
		AddLine(TEXT("eng_behind_cover"), TEXT("engineer"), TEXT("combat_cover"), TEXT("In low cover. Watching the lane."), 2.0f, 12.0f);
		AddLine(TEXT("eng_take_cover"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Taking cover!"), 2.0f, 8.0f);
		AddLine(TEXT("eng_dash_other_cover"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Shifting positions!"), 2.0f, 10.0f);
		AddLine(TEXT("eng_along_wall_corner"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Moving along the perimeter."), 2.0f, 12.0f);
		AddLine(TEXT("eng_guard_on"), TEXT("engineer"), TEXT("squad_order"), TEXT("Guard stance set. Sector locked down."), 2.0f, 6.0f);
		AddLine(TEXT("eng_guard_off"), TEXT("engineer"), TEXT("squad_order"), TEXT("Guard stance released. Moving on."), 2.0f, 6.0f);
		AddLine(TEXT("eng_changing_mags"), TEXT("engineer"), TEXT("combat_callout"), TEXT("Changing mags! Cover me!"), 2.0f, 8.0f);
		AddLine(TEXT("eng_cover_group_stack"), TEXT("engineer"), TEXT("combat_cover"), TEXT("Stacking behind the lead operative. Ready on entry."), 2.0f, 12.0f);
		AddLine(TEXT("eng_rage_shout"), TEXT("engineer"), TEXT("combat_rage"), TEXT("Burn it all down! No retreat!"), 1.0f, 10.0f);

		// Medic / Sapper (13)
		AddLine(TEXT("med_tripwire"), TEXT("medic_sapper"), TEXT("hazard_alert"), TEXT("Trap detected! Hold up - disarming."), 1.0f, 10.0f);
		AddLine(TEXT("med_apc_tank_needs_canister"), TEXT("medic_sapper"), TEXT("quest_inspect"), TEXT("Tank has fuel, but we need an empty canister to siphon it."), 2.0f, 15.0f);
		AddLine(TEXT("med_reloading"), TEXT("medic_sapper"), TEXT("combat_callout"), TEXT("Reloading sidearm!"), 2.0f, 8.0f);
		AddLine(TEXT("med_watch_back"), TEXT("medic_sapper"), TEXT("combat_callout"), TEXT("Check your six!"), 2.0f, 10.0f);
		AddLine(TEXT("med_grenade_out"), TEXT("medic_sapper"), TEXT("combat_callout"), TEXT("Frag out! Heads down!"), 2.0f, 6.0f);
		AddLine(TEXT("med_grenade_away"), TEXT("medic_sapper"), TEXT("combat_callout"), TEXT("Tossing smoke! Disengaging!"), 1.0f, 8.0f);
		AddLine(TEXT("med_in_cover"), TEXT("medic_sapper"), TEXT("combat_cover"), TEXT("Secured behind the barricade."), 2.0f, 12.0f);
		AddLine(TEXT("med_barricade_cover"), TEXT("medic_sapper"), TEXT("combat_cover"), TEXT("Holding behind the sandbags."), 2.0f, 12.0f);
		AddLine(TEXT("med_patched_up"), TEXT("medic_sapper"), TEXT("medical"), TEXT("Tourniquet secured. You're stable - keep moving."), 2.0f, 10.0f);
		AddLine(TEXT("med_thawed"), TEXT("medic_sapper"), TEXT("survival"), TEXT("Core temperature stabilized. Hypothermia halted."), 2.0f, 10.0f);
		AddLine(TEXT("med_critically_down"), TEXT("medic_sapper"), TEXT("medical"), TEXT("Operative down! Need immediate triage!"), 1.0f, 8.0f);
		AddLine(TEXT("med_looted_crate"), TEXT("medic_sapper"), TEXT("loot_pickup"), TEXT("Supply crate cracked. Bandages and rations inside."), 3.0f, 15.0f);
		AddLine(TEXT("med_panic_grip"), TEXT("medic_sapper"), TEXT("combat_panic"), TEXT("Can't stop the shaking... weapons slipping!"), 1.0f, 10.0f);

		// PMC Op 1 (3)
		AddLine(TEXT("pmc1_behind_cover"), TEXT("pmc_op_1"), TEXT("pmc_cover"), TEXT("Hostile taking cover! Suppress!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc1_hugging_wall"), TEXT("pmc_op_1"), TEXT("pmc_cover"), TEXT("Target hugged to the east wall!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc1_reloading"), TEXT("pmc_op_1"), TEXT("pmc_callout"), TEXT("He's dry! Rush him!"), 2.0f, 8.0f);

		// PMC Op 2 (3)
		AddLine(TEXT("pmc2_behind_cover"), TEXT("pmc_op_2"), TEXT("pmc_cover"), TEXT("Target ducked behind the crates!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc2_hugging_wall"), TEXT("pmc_op_2"), TEXT("pmc_cover"), TEXT("Flanking along the concrete barrier!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc2_changing_mags"), TEXT("pmc_op_2"), TEXT("pmc_callout"), TEXT("Enemy reloading! Push up!"), 2.0f, 8.0f);

		// PMC Op 3 (3)
		AddLine(TEXT("pmc3_at_corner"), TEXT("pmc_op_3"), TEXT("pmc_cover"), TEXT("Hostile spotted at the corner!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc3_in_cover_sector"), TEXT("pmc_op_3"), TEXT("pmc_cover"), TEXT("Suppressing their vantage point!"), 2.0f, 12.0f);
		AddLine(TEXT("pmc3_grenade_out"), TEXT("pmc_op_3"), TEXT("pmc_callout"), TEXT("Incoming ordnance! Scatter!"), 2.0f, 6.0f);
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
		return FString::Printf(TEXT("/Game/Audio/VO/%s"), *Id.ToString());
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

		// Lookup default priority / cooldown from master rules table if available
		FCodexVoiceLineDef MasterDef;
		if (FindVoiceLineDef(Def.Id, MasterDef))
		{
			Def.Priority = MasterDef.Priority;
			Def.CooldownSeconds = MasterDef.CooldownSeconds;
		}
		else
		{
			Def.Priority = 2.0f;
			Def.CooldownSeconds = 8.0f;
		}
		Def.AssetPath = GetExpectedAssetPath(Def.Id, true);

		OutLines.Add(Def);
	}

	return OutLines.Num() > 0;
}
