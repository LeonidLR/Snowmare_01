#pragma once

#include "CoreMinimal.h"
#include "Audio/CodexAudioTypes.h"
#include "Environment/EnvironmentTypes.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * Pure evaluation rules for CodexTactics adaptive audio and voice lines.
 * Governed by docs/design/audio/EVENTS.md, Content/Data/Audio/music_cues.json,
 * and Content/Data/Audio/voice_lines.json.
 */
class CODEXTACTICS_API FCodexAudioRules
{
public:
	/**
	 * Resolves adaptive music state based on current world context.
	 * Hierarchy per EVENTS.md:
	 * 1. Frontend World -> Menu
	 * 2. Phase == GameOver -> Defeat
	 * 3. Phase == WaveCleared or PostCombat -> Victory
	 * 4. Phase == WaveCombat:
	 *    - Horde released / active -> CombatHeavy
	 *    - Else -> Combat
	 * 5. Outside WaveCombat:
	 *    - Squad sees enemy or combat pressure active -> Tension
	 *    - Weather == Blizzard -> ExploreCold
	 *    - Else -> ExploreCalm
	 */
	static ECodexMusicState EvaluateMusicState(
		bool bIsFrontend,
		ECodexGamePhase Phase,
		int32 AliveEnemies,
		bool bSquadSeesEnemy,
		bool bCombatPressureActive,
		bool bHordeActive,
		EWeatherState Weather
	);

	/** Converts music state to string identifier (e.g. "explore_calm"). */
	static FString MusicStateToString(ECodexMusicState State);

	/** Parses string identifier to music state enum. */
	static ECodexMusicState StringToMusicState(const FString& StateStr);

	/** Selects a cue ID for the specified music state, picking variant deterministically by index. */
	static FName SelectMusicCueForState(ECodexMusicState State, int32 VariantIndex = 0);

	/** Returns all available cue IDs for the given state. */
	static void GetAvailableMusicCuesForState(ECodexMusicState State, TArray<FName>& OutCues);

	/** Returns the full master list of registered music cue definitions. */
	static const TArray<FCodexMusicCueDef>& GetAllMusicCues();

	/** Finds cue definition by cue ID. */
	static bool FindMusicCueDef(FName CueId, FCodexMusicCueDef& OutDef);

	/**
	 * Maps gameplay or bus event to voice line ID from voice_lines.json.
	 * Supports raw event names, 'vo_' prefixed event names, and speaker context.
	 */
	static FName MapEventToVoiceLine(FName EventId, FName Speaker = NAME_None);

	/** Returns the full master list of registered voice line definitions. */
	static const TArray<FCodexVoiceLineDef>& GetAllVoiceLines();

	/** Finds voice line definition by line ID. */
	static bool FindVoiceLineDef(FName LineId, FCodexVoiceLineDef& OutDef);

	/** Checks whether a voice line can be played based on phrase and speaker cooldowns. */
	static bool CanPlayVoiceLine(
		FName LineId,
		float CurrentTime,
		const TMap<FName, float>& LastPlayedTimes,
		float CooldownOverride = -1.f
	);

	/** Constructs expected Unreal asset path for a cue or voice line. */
	static FString GetExpectedAssetPath(FName Id, bool bIsVoice = false);

	/**
	 * Checks if a sound asset package actually exists on disk without loading it.
	 * Prevents load warnings on missing assets.
	 */
	static bool DoesSoundAssetExist(const FString& AssetPath);

	/** Parses music cues from Content/Data/Audio/music_cues.json JSON string. */
	static bool LoadMusicCuesFromJson(const FString& JsonContent, TArray<FCodexMusicCueDef>& OutCues);

	/** Parses voice lines from Content/Data/Audio/voice_lines.json JSON string. */
	static bool LoadVoiceLinesFromJson(const FString& JsonContent, TArray<FCodexVoiceLineDef>& OutLines);
};
