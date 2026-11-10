#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

/**
 * The CRITICAL wound (the game designer 2026-11-10, fix/injured-layer-visible; UE-only - Godot had none): below "critical_from" of his max
 * health (Content/Data/Movement/critical_wound.json, default 0.2) an operative goes DOWN by himself (prone) and moves only crawling - the
 * injured crawl of the injured locomotion layer (InjuredLocomotionRules: wounded + prone). He cannot stand up: stand / crouch orders are refused
 * with the feed line "<name> is critically wounded - heal him", no sprint, no ladder (stand-only), no vault (prone never vaults); he still
 * fires prone. Healing on the spot (a medkit, the medic) that brings him back to "critical_from" or above lets him stand again - he stays
 * prone until ordered up. No carrying of the wounded (the designer: too complex).
 * Hostile PMC fighters share the operative body: "apply_to_hostiles" (default false) decides whether they collapse too.
 * The character side: AOperativeCharacter::IsCriticallyWounded / UpdateCriticalWound (OperativeCharacterStance.cpp).
 * Tests CodexTactics.Characters.CriticalWound.*; smoke stage in CodexTactics.InjuredLocomotionSmoke.
 */
namespace CriticalWoundRules
{
	/** critical_wound.json. */
	struct FCriticalWoundConfig
	{
		/** "critical_from": the health share (0..1; a percent 1..100 is read as a percent) under which he is critical; <= 0 = off. */
		float CriticalFrom = 0.2f;
		/** "apply_to_hostiles": the hostile PMC fighters (TeamId != 0, the same body) go down too. */
		bool bApplyToHostiles = false;
	};

	/** What the character does this tick (UpdateCriticalWound). */
	enum class ECriticalWoundAction : uint8
	{
		/** Nothing changes. */
		None,
		/** He just became critical: down to prone now (stop, lie down in place) + the feed line. */
		GoDown,
		/** Critical, not prone, but busy (a ladder, a knockdown, a vault, a fall, a stance clip): wait, go down as soon as he is free. */
		WaitToGoDown,
		/** Healed back over the threshold: the stance lock is lifted (he stays prone until ordered up) + the feed line. */
		MayStand
	};

	/** A share (0..1) or a percent (1..100) -> a share; anything else (negative, NaN, > 100) -> 0 = off. */
	CODEXTACTICS_API float SanitizeShare(double Value);

	/** Critical: alive (Health > 0), MaxHealth > 0 and Health / MaxHealth < CriticalFrom (the rule on, CriticalFrom > 0). */
	CODEXTACTICS_API bool IsCritical(const FCriticalWoundConfig& Config, float Health, float MaxHealth);

	/** The rule applies to this body: the player's squad always, a hostile only with bApplyToHostiles. */
	CODEXTACTICS_API bool AppliesTo(const FCriticalWoundConfig& Config, bool bPlayerSide);

	/** A stance he may take: anything when not critical; only prone when critical. */
	CODEXTACTICS_API bool AllowsStance(bool bCritical, EOperativeStance Wanted);

	/** A sprint is allowed (critical: never). */
	CODEXTACTICS_API bool AllowsSprint(bool bCritical);

	/**
	 * The tick's decision: bDown = he is held down by the rule (the state the character keeps), bCritical = critical now, bProne = prone now,
	 * bBusy = he cannot lie down this tick (ladder, knockdown, vault, fall, stance clip, dead).
	 */
	CODEXTACTICS_API ECriticalWoundAction StepWound(bool bDown, bool bCritical, bool bProne, bool bBusy);

	/** Parses critical_wound.json; false (Out untouched) when the text is not a JSON object. Unknown keys are ignored. */
	CODEXTACTICS_API bool ParseConfig(const FString& JsonText, FCriticalWoundConfig& Out);

	/** Content/Data/Movement/critical_wound.json. */
	CODEXTACTICS_API FString GetWoundConfigPath();

	/** The config in force (critical_wound.json read once, the code defaults when it is missing). */
	CODEXTACTICS_API const FCriticalWoundConfig& GetWoundConfig();

	/** Replaces the config in force (tests / smokes). */
	CODEXTACTICS_API void SetWoundConfig(const FCriticalWoundConfig& Config);
}
