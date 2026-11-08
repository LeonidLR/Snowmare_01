#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The sidearm switch (the weapons week, "the whole pistol", 2026-11-08): an operative whose primary weapon (the M16, the Engineer's D-1)
 * runs dry with an enemy close draws the pistol instead of standing through the reload, and goes back to the primary (and reloads it)
 * once no target is left for a while. The player's own controls stay as they are (X cycles the weapons; the weapon selector). An empty
 * primary with no reserve left already switched for good (AOperativeCharacter::AutoSwitchOnEmpty).
 * Data: Content/Data/Weapons/weapons_tuning.json root block
 *   "sidearm": {"enabled": true, "weapon": "pistol", "threat_range_m": 10, "draw_s": 0.35, "holster_after_s": 3}
 * UE-only (Godot had no sidearm switch). Tested in CodexTactics.Weapons.Sidearm.*.
 */
struct CODEXTACTICS_API FSidearmConfig
{
	bool bEnabled = true;
	/** The weapon drawn (the pistol). */
	FString WeaponId = TEXT("pistol");
	/** An enemy at most this far away is a threat worth the switch (m). */
	float ThreatRangeM = 10.f;
	/** The draw: no shot for this long after the switch (s; the equip clip plays meanwhile). */
	float DrawSeconds = 0.35f;
	/** Back to the primary after this long without a target (s). */
	float HolsterAfterSeconds = 3.f;
};

namespace SidearmRules
{
	/** Reads the "sidearm" block over the defaults; bad values are kept at the default with a line in OutErrors. */
	CODEXTACTICS_API FSidearmConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors = nullptr);

	/** The config in force (WeaponTuning::ApplyFile sets it). */
	CODEXTACTICS_API void Set(const FSidearmConfig& Config);
	CODEXTACTICS_API const FSidearmConfig& Get();

	/**
	 * Draw the sidearm now? Only while holding a primary (not the sidearm itself) whose clip is EMPTY (a tactical top-up never switches)
	 * and that still has reserve to reload later, the sidearm owned with rounds, and a live threat within ThreatRangeM (ThreatDistanceCm < 0
	 * = no threat). Never while raging (rage ignores the reload, Godot).
	 */
	CODEXTACTICS_API bool ShouldDrawSidearm(const FSidearmConfig& Config, bool bHoldingSidearm, bool bPrimaryClipEmpty, bool bPrimaryHasReserve,
		bool bSidearmUsable, float ThreatDistanceCm, bool bRaging);

	/** Back to the primary? The sidearm was drawn by the rule and either no target for HolsterAfterSeconds or the sidearm is dry. */
	CODEXTACTICS_API bool ShouldReturnToPrimary(const FSidearmConfig& Config, bool bDrawnByRule, float SecondsWithoutTarget, bool bSidearmDry);
}
