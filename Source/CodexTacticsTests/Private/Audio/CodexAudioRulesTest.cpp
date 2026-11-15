// CodexTactics.Audio.* - Tests for CodexTactics adaptive audio and voice line rules.
// Governed by docs/design/audio/EVENTS.md, Content/Data/Audio/music_cues.json, and Content/Data/Audio/voice_lines.json.

#include "Misc/AutomationTest.h"
#include "Audio/CodexAudioRules.h"
#include "Audio/CodexAudioTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

#define AUDIO_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Audio." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

AUDIO_TEST(FAudioMusicStateTransitionsTest, "MusicStateTransitions")
bool FAudioMusicStateTransitionsTest::RunTest(const FString&)
{
	// 1. Frontend map takes highest precedence -> Menu
	TestEqual(
		TEXT("Frontend map evaluates to Menu"),
		FCodexAudioRules::EvaluateMusicState(true, ECodexGamePhase::Exploration, 0, false, false, false, EWeatherState::Clear),
		ECodexMusicState::Menu
	);

	// 2. GameOver -> Defeat
	TestEqual(
		TEXT("Game over evaluates to Defeat"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::GameOver, 5, false, false, false, EWeatherState::Clear),
		ECodexMusicState::Defeat
	);

	// 3. WaveCleared / PostCombat -> Victory
	TestEqual(
		TEXT("Wave cleared evaluates to Victory"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::WaveCleared, 0, false, false, false, EWeatherState::Clear),
		ECodexMusicState::Victory
	);
	TestEqual(
		TEXT("PostCombat evaluates to Victory"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::PostCombat, 0, false, false, false, EWeatherState::Clear),
		ECodexMusicState::Victory
	);

	// 4. WaveCombat -> Combat (or CombatHeavy if horde active)
	TestEqual(
		TEXT("WaveCombat evaluates to Combat"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::WaveCombat, 8, true, true, false, EWeatherState::Clear),
		ECodexMusicState::Combat
	);
	TestEqual(
		TEXT("WaveCombat with active horde evaluates to CombatHeavy"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::WaveCombat, 15, true, true, true, EWeatherState::Clear),
		ECodexMusicState::CombatHeavy
	);

	// 5. Outside combat with enemy sight / pressure -> Tension
	TestEqual(
		TEXT("Enemies spotted during exploration evaluates to Tension"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::Exploration, 3, true, false, false, EWeatherState::Clear),
		ECodexMusicState::Tension
	);
	TestEqual(
		TEXT("Combat pressure active evaluates to Tension"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::Preparation, 2, false, true, false, EWeatherState::Clear),
		ECodexMusicState::Tension
	);

	// 6. Outside combat + Blizzard -> ExploreCold
	TestEqual(
		TEXT("Blizzard exploration evaluates to ExploreCold"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::Exploration, 0, false, false, false, EWeatherState::Blizzard),
		ECodexMusicState::ExploreCold
	);

	// 7. Outside combat + Calm weather -> ExploreCalm
	TestEqual(
		TEXT("Clear weather exploration evaluates to ExploreCalm"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::Exploration, 0, false, false, false, EWeatherState::Clear),
		ECodexMusicState::ExploreCalm
	);
	TestEqual(
		TEXT("Overcast weather exploration evaluates to ExploreCalm"),
		FCodexAudioRules::EvaluateMusicState(false, ECodexGamePhase::Exploration, 0, false, false, false, EWeatherState::Overcast),
		ECodexMusicState::ExploreCalm
	);

	return true;
}

AUDIO_TEST(FAudioMusicCueSelectionTest, "MusicCueSelection")
bool FAudioMusicCueSelectionTest::RunTest(const FString&)
{
	// Check cue count per state
	TArray<FName> Cues;

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::ExploreCalm, Cues);
	TestEqual(TEXT("ExploreCalm has 2 cues"), Cues.Num(), 2);
	TestEqual(TEXT("ExploreCalm variant 0"), FCodexAudioRules::SelectMusicCueForState(ECodexMusicState::ExploreCalm, 0), FName(TEXT("explore_calm_v1")));
	TestEqual(TEXT("ExploreCalm variant 1"), FCodexAudioRules::SelectMusicCueForState(ECodexMusicState::ExploreCalm, 1), FName(TEXT("explore_calm_v2")));
	TestEqual(TEXT("ExploreCalm wraps index 2"), FCodexAudioRules::SelectMusicCueForState(ECodexMusicState::ExploreCalm, 2), FName(TEXT("explore_calm_v1")));

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::ExploreCold, Cues);
	TestEqual(TEXT("ExploreCold has 5 cues"), Cues.Num(), 5);

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::Tension, Cues);
	TestEqual(TEXT("Tension has 2 cues"), Cues.Num(), 2);

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::Combat, Cues);
	TestEqual(TEXT("Combat has 5 cues"), Cues.Num(), 5);

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::CombatHeavy, Cues);
	TestEqual(TEXT("CombatHeavy has 2 cues"), Cues.Num(), 2);

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::Victory, Cues);
	TestEqual(TEXT("Victory has 2 cues"), Cues.Num(), 2);
	TestEqual(TEXT("Victory sting v1"), FCodexAudioRules::SelectMusicCueForState(ECodexMusicState::Victory, 0), FName(TEXT("victory_sting_v1")));

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::Defeat, Cues);
	TestEqual(TEXT("Defeat has 2 cues"), Cues.Num(), 2);
	TestEqual(TEXT("Defeat sting v1"), FCodexAudioRules::SelectMusicCueForState(ECodexMusicState::Defeat, 0), FName(TEXT("defeat_sting_v1")));

	FCodexAudioRules::GetAvailableMusicCuesForState(ECodexMusicState::Menu, Cues);
	TestEqual(TEXT("Menu has 2 cues"), Cues.Num(), 2);

	// Total master cues must be 22
	TestEqual(TEXT("Total master cues is 22"), FCodexAudioRules::GetAllMusicCues().Num(), 22);

	return true;
}

AUDIO_TEST(FAudioVoiceLineMappingTest, "VoiceLineMapping")
bool FAudioVoiceLineMappingTest::RunTest(const FString&)
{
	// Direct line ID
	TestEqual(
		TEXT("Direct line cmd_contact"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("cmd_contact")),
		FName(TEXT("cmd_contact"))
	);

	// vo_ prefix stripping
	TestEqual(
		TEXT("vo_cmd_contact stripped to cmd_contact"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("vo_cmd_contact")),
		FName(TEXT("cmd_contact"))
	);
	TestEqual(
		TEXT("vo_med_tripwire stripped to med_tripwire"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("vo_med_tripwire")),
		FName(TEXT("med_tripwire"))
	);

	// Generic gameplay event mappings
	TestEqual(
		TEXT("OnSoldierDowned maps to med_critically_down"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("OnSoldierDowned")),
		FName(TEXT("med_critically_down"))
	);
	TestEqual(
		TEXT("OnMineSpotted maps to med_tripwire"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("OnMineSpotted")),
		FName(TEXT("med_tripwire"))
	);
	TestEqual(
		TEXT("OnRageStarted maps to eng_rage_shout"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("OnRageStarted")),
		FName(TEXT("eng_rage_shout"))
	);
	TestEqual(
		TEXT("OnGeneratorStarted maps to cmd_generator_starts"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("OnGeneratorStarted")),
		FName(TEXT("cmd_generator_starts"))
	);
	TestEqual(
		TEXT("OnHordeReleased maps to cmd_wave_advancing"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("OnHordeReleased")),
		FName(TEXT("cmd_wave_advancing"))
	);

	// Speaker-dependent reload mapping
	TestEqual(
		TEXT("Reloading for medic_sapper maps to med_reloading"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("Reloading"), TEXT("medic_sapper")),
		FName(TEXT("med_reloading"))
	);
	TestEqual(
		TEXT("Reloading for pmc_op_1 maps to pmc1_reloading"),
		FCodexAudioRules::MapEventToVoiceLine(TEXT("Reloading"), TEXT("pmc_op_1")),
		FName(TEXT("pmc1_reloading"))
	);

	// Total voice lines must be 49
	TestEqual(TEXT("Total master voice lines is 49"), FCodexAudioRules::GetAllVoiceLines().Num(), 49);

	return true;
}

AUDIO_TEST(FAudioVoiceLineCooldownTest, "VoiceLineCooldown")
bool FAudioVoiceLineCooldownTest::RunTest(const FString&)
{
	TMap<FName, float> PlayedTimes;
	const FName LineId = TEXT("cmd_contact"); // 8s cooldown

	// Initial play: allowed
	TestTrue(TEXT("Initially allowed"), FCodexAudioRules::CanPlayVoiceLine(LineId, 10.0f, PlayedTimes));

	// Record play at 10.0s
	PlayedTimes.Add(LineId, 10.0f);

	// Play at 15.0s (delta 5.0s < 8.0s cooldown): rejected
	TestFalse(TEXT("Rejected during cooldown window"), FCodexAudioRules::CanPlayVoiceLine(LineId, 15.0f, PlayedTimes));

	// Play at 19.0s (delta 9.0s >= 8.0s cooldown): allowed
	TestTrue(TEXT("Allowed after cooldown elapsed"), FCodexAudioRules::CanPlayVoiceLine(LineId, 19.0f, PlayedTimes));

	return true;
}

AUDIO_TEST(FAudioJsonDataValidationTest, "JsonDataValidation")
bool FAudioJsonDataValidationTest::RunTest(const FString&)
{
	const FString ProjectDir = FPaths::ProjectContentDir();

	// 1. Validate music_cues.json
	const FString MusicPath = FPaths::Combine(ProjectDir, TEXT("Data/Audio/music_cues.json"));
	FString MusicJson;
	if (FFileHelper::LoadFileToString(MusicJson, *MusicPath))
	{
		TArray<FCodexMusicCueDef> ParsedCues;
		const bool bOk = FCodexAudioRules::LoadMusicCuesFromJson(MusicJson, ParsedCues);
		TestTrue(TEXT("Loaded music_cues.json successfully"), bOk);
		TestEqual(TEXT("Parsed 22 cues from music_cues.json"), ParsedCues.Num(), 22);
	}
	else
	{
		AddWarning(FString::Printf(TEXT("Could not find music_cues.json at path %s (skipped in non-content build)"), *MusicPath));
	}

	// 2. Validate voice_lines.json
	const FString VoicePath = FPaths::Combine(ProjectDir, TEXT("Data/Audio/voice_lines.json"));
	FString VoiceJson;
	if (FFileHelper::LoadFileToString(VoiceJson, *VoicePath))
	{
		TArray<FCodexVoiceLineDef> ParsedLines;
		const bool bOk = FCodexAudioRules::LoadVoiceLinesFromJson(VoiceJson, ParsedLines);
		TestTrue(TEXT("Loaded voice_lines.json successfully"), bOk);
		TestEqual(TEXT("Parsed 49 lines from voice_lines.json"), ParsedLines.Num(), 49);
	}
	else
	{
		AddWarning(FString::Printf(TEXT("Could not find voice_lines.json at path %s (skipped in non-content build)"), *VoicePath));
	}

	return true;
}

AUDIO_TEST(FAudioMissingAssetSilentNoOpTest, "MissingAssetSilentNoOp")
bool FAudioMissingAssetSilentNoOpTest::RunTest(const FString&)
{
	const FString FakePath = TEXT("/Game/Audio/Music/non_existent_fake_cue");
	TestFalse(TEXT("Non-existent asset does not exist on disk"), FCodexAudioRules::DoesSoundAssetExist(FakePath));

	const FString ExpectedMusicPath = FCodexAudioRules::GetExpectedAssetPath(TEXT("explore_calm_v1"), false);
	TestEqual(TEXT("Expected music path format"), ExpectedMusicPath, TEXT("/Game/Audio/Music/explore_calm/SW_explore_calm_v1"));

	const FString ExpectedVoicePath = FCodexAudioRules::GetExpectedAssetPath(TEXT("cmd_contact"), true);
	TestEqual(TEXT("Expected voice path format"), ExpectedVoicePath, TEXT("/Game/Audio/Voice/commander/SW_cmd_contact"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
