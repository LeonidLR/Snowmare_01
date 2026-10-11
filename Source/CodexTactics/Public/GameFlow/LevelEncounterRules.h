#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowTypes.h"

/**
 * Per-level encounter rules (user request 2026-10-06, UE-only): how the fight starts (button vs ambush) and when the
 * world AI is held (dialogue / cutscene). Pure, tested in CodexTactics.Combat.AmbushStartsCombat and
 * CodexTactics.AI.Patrol.DialoguePausesPerception; ULevelEncounterSubsystem / UWorldAIPauseSubsystem apply them.
 */
namespace LevelEncounterRules
{
	/** "auto" / "ambush" / "button" (any case; empty = auto). False for anything else. */
	CODEXTACTICS_API bool ParseCombatStart(const FString& Text, ECombatStartMode& OutMode);
	CODEXTACTICS_API FString CombatStartName(ECombatStartMode Mode);

	/**
	 * The fight starts by ambush: Ambush, or Auto on a map with patrols or with any enemy placed in the map (user bug
	 * 2026-10-11: L_PatrolTest's marksman and hound without routes attacked a squad that stayed in exploration).
	 */
	CODEXTACTICS_API bool IsAmbushStart(ECombatStartMode Mode, bool bLevelHasPatrols, bool bLevelHasPlacedEnemies = false);

	/**
	 * A hostile contact (the squad attacked / hurt an enemy, or an enemy detected the squad) starts the fight now: ambush
	 * level, still exploring and the combat not yet unlocked (once per mission — no double start).
	 */
	CODEXTACTICS_API bool ShouldStartAmbush(bool bAmbushLevel, ECodexGamePhase Phase, bool bCombatUnlocked);

	/**
	 * A hostile contact starts the (ambush) fight now: on an ambush level, or - on any level, even a "button" one - when
	 * the contact involves an enemy placed in the map (it attacked / spotted the squad, or the squad hit it): the level's
	 * own enemies never fight a squad left in exploration (user bug 2026-10-11). Still exploring, not yet unlocked.
	 */
	CODEXTACTICS_API bool ShouldStartFightOnContact(bool bAmbushLevel, bool bContactWithLevelEnemy, ECodexGamePhase Phase, bool bCombatUnlocked);

	/** Enemies neither perceive nor act: a blocking dialogue window, the pre-combat cutscene, or any registered blocker. */
	CODEXTACTICS_API bool IsWorldAIPaused(bool bDialogueOpen, ECodexGamePhase Phase, int32 NumBlockers);

	/** Patrol search seconds: the level's value when > 0, else the global one. */
	CODEXTACTICS_API float ResolveSearchSeconds(float LevelSeconds, float GlobalSeconds);
}
