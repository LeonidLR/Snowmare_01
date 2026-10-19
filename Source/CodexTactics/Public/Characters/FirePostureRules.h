#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "FirePostureRules.generated.h"

/**
 * Rules of engagement of the operatives' automatic fire (user request 2026-10-06, UE-only). Direct player orders
 * (Ctrl / plain click on an enemy, blind fire at a silhouette, planned shots, grenades, abilities) are obeyed in every
 * posture; the posture only gates fire the operatives open on their own (visible enemies, Commander Mode targets,
 * autonomous grenades).
 */
UENUM(BlueprintType)
enum class ESquadFirePosture : uint8
{
	/** "Passive": never open fire on their own, not even when attacked. */
	Passive,
	/**
	 * "Defensive": no fire on enemies that have not spotted the squad; it engages an enemy that has detected the squad (hunts it)
	 * and, once attacked (self, or any squad member — tunable), fights back at any enemy for that fight (user decision 2026-10-19).
	 */
	Defensive,
	/** "Aggressive": open fire on any enemy in sight at once (the behaviour before postures existed). */
	Aggressive
};

/** Who a posture order (keys , . / or the action bar buttons) changes (user decision 2026-10-06). */
enum class EPostureOrderScope : uint8
{
	/** The selected operative(s): the box-selected group, else the leader the player controls (personal override). */
	Selected,
	/** The whole squad (Alt + , . /): the squad posture, every override cleared. */
	Squad
};

/** Posture tuning (console: Codex.Posture.DefensiveSquadWide, Codex.Posture.AggressiveExplorationFire). */
struct CODEXTACTICS_API FFirePostureConfig
{
	/** Defensive: an attack on any squad member provokes every defensive operative (false: only the one attacked). */
	bool bDefensiveSquadWideProvocation = false;
	/**
	 * Aggressive: still in exploration, an operative who sees an enemy in weapon range opens fire where contact starts the
	 * fight (an ambush level, or an enemy placed in the map on a «button» level - LevelEncounterRules::ShouldStartFightOnContact).
	 */
	bool bAggressiveOpensFireInExploration = true;
};

/** Pure posture rules, tested in CodexTactics.Squad.Posture.*; applied by AOperativeCharacter / USquadSubsystem. */
namespace FirePostureRules
{
	/** The default squad posture: Aggressive reproduces the auto-fire of the fights before postures existed. */
	constexpr ESquadFirePosture DefaultPosture = ESquadFirePosture::Aggressive;

	/** The posture in force for one operative: its override when it has one, else the squad's. */
	CODEXTACTICS_API ESquadFirePosture Resolve(ESquadFirePosture SquadPosture, bool bHasOverride, ESquadFirePosture Override);

	/**
	 * May the operative open fire on its own now? Passive never; Defensive once provoked (attacked itself, or the squad
	 * with bDefensiveSquadWideProvocation); Aggressive always.
	 */
	CODEXTACTICS_API bool MayAutoFire(ESquadFirePosture Posture, bool bSelfAttacked, bool bSquadAttacked, const FFirePostureConfig& Config);

	/**
	 * Per target (user decision 2026-10-19): may the operative open fire on THIS enemy on its own? Passive never; Defensive at an
	 * enemy that has detected the squad (bTargetHasDetectedSquad: it hunts the squad - AEnemyCharacter::IsHuntingSquad) or once
	 * provoked (MayAutoFire); Aggressive at any enemy in sight.
	 */
	CODEXTACTICS_API bool MayAutoFireAt(ESquadFirePosture Posture, bool bSelfAttacked, bool bSquadAttacked, bool bTargetHasDetectedSquad,
		const FFirePostureConfig& Config);

	/** Does the posture allow any automatic target at all (Defensive: only the detecting enemies until provoked)? Passive: no. */
	CODEXTACTICS_API bool AllowsAutoTargets(ESquadFirePosture Posture);

	/** May it fire at a target now: a direct player order always, otherwise MayAutoFire. */
	CODEXTACTICS_API bool MayFire(ESquadFirePosture Posture, bool bDirectOrder, bool bSelfAttacked, bool bSquadAttacked,
		const FFirePostureConfig& Config);

	/**
	 * Aggressive auto-fire in exploration: allowed only where opening fire can start the fight (bContactStartsFight: an ambush
	 * level, or a level with enemies placed in the map - contact with one of them starts the fight on a «button» level too, fix
	 * 2026-10-19: the Demo 01 lake is a «button» level with placed patrols and the Aggressive squad never fired), still
	 * exploring, fight not yet unlocked — never on a classic wave map without placed enemies, where exploration stays peaceful.
	 * The encounter system still decides per enemy (ULevelEncounterSubsystem::NotifyHostileContact).
	 */
	CODEXTACTICS_API bool MayAutoFireInExploration(ESquadFirePosture Posture, bool bContactStartsFight, ECodexGamePhase Phase, bool bCombatUnlocked,
		const FFirePostureConfig& Config);

	/** Provocations end with the fight: a wave cleared, after the combat, back in exploration or game over. */
	CODEXTACTICS_API bool ClearsProvocation(ECodexGamePhase NewPhase);

	/**
	 * Scope of a posture order: Alt held -> the whole squad; otherwise the selected operative(s) (NumSelected = the
	 * box-selected group, or 1 for the controlled leader). Nobody selected (no leader) falls back to the squad.
	 */
	CODEXTACTICS_API EPostureOrderScope GetOrderScope(bool bSquadWideModifier, int32 NumSelected);

	/** The posture all of Postures share (HUD of a selection); false when they differ or the list is empty. */
	CODEXTACTICS_API bool GetCommonPosture(const TArray<ESquadFirePosture>& Postures, ESquadFirePosture& OutCommon);

	/** Next posture of the cycle Passive -> Defensive -> Aggressive -> Passive. */
	CODEXTACTICS_API ESquadFirePosture Next(ESquadFirePosture Posture);

	/** Data name "passive" / "defensive" / "aggressive" (case-insensitive; the tutorial "set_posture" action). False when unknown. */
	CODEXTACTICS_API bool ParseName(const FString& Name, ESquadFirePosture& OutPosture);

	/** "PASSIVE" / "DEFENSIVE" / "AGGRESSIVE". */
	CODEXTACTICS_API FString GetLabel(ESquadFirePosture Posture);
	/** Short action bar label "PAS" / "DEF" / "AGG". */
	CODEXTACTICS_API FString GetShortLabel(ESquadFirePosture Posture);
	/** One-letter indicator of the per-operative marker / squad slot: "P" / "D" / "A". */
	CODEXTACTICS_API FString GetLetter(ESquadFirePosture Posture);
	/** The direct-select hotkey shown on the HUD: «,» / «.» / «/». */
	CODEXTACTICS_API FString GetKeyHint(ESquadFirePosture Posture);
}
