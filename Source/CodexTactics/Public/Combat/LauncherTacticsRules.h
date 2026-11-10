#pragma once

#include "CoreMinimal.h"

/**
 * When a shooter fires the RPG-7 instead of his rifle (the game designer's answers on the RPG-7, 2026-11-09, item 7: "the AI must actually use
 * the RPG, including as suppression"; docs/systems/weapons.md "The RPG-7: who fires it and when"; new - no Godot reference). ONE rule for our
 * squad's operatives and the hostile PMC fighters (both are AOperativeCharacter: the weapon choice runs in the shared fire loop,
 * AOperativeCharacter::UpdateLauncherChoice in OperativeCharacterLauncherAI.cpp) and the PMC commander's RpgStrike order.
 *
 * Every target the shooter could fire at is scored 0..: the best of its reasons (a cluster of enemies round it, cover the rifle cannot beat,
 * a heavy target - a vehicle / turret / mortar crew -, a group to suppress, and the single exposed man, worth nothing by default: the rifle),
 * after the vetoes (no rockets, inside the minimum range, past the reach, a mate near the impact, the last rockets kept for a heavy target).
 * A score at or above the threshold draws the launcher. The weights live in Content/Data/Combat/launcher_tactics.json ("params", flat keys
 * named on each field; the struct defaults mirror the file - test CodexTactics.Weapons.RpgAI.TuningFileMirrorsDefaults).
 * Pure: the world view comes in as FLauncherTargetView. Tested in CodexTactics.Weapons.RpgAI.*.
 */

/** Why a target is worth a rocket (the highest-scoring reason). */
enum class ELauncherUse : uint8
{
	/** Not worth a rocket (the rifle). */
	None,
	/** Two or more enemies inside the blast round the aim point. */
	Cluster,
	/** The target hides behind cover / a barricade: the rifle cannot hit him (well), the blast reaches behind it. */
	Cover,
	/** A vehicle, a turret or a mortar crewman. */
	Heavy,
	/** A group that fires at us / closes in: one rocket makes it duck (the blast's suppression, SuppressionRules "launcher.*"). */
	Suppress,
	/** A single exposed man (weight_single_exposed, 0 by default: the rifle). */
	Single
};

/** Why a target may not get a rocket at all. */
enum class ELauncherVeto : uint8
{
	None,
	/** No rocket in the tube or in reserve. */
	NoRockets,
	/** Inside the minimum range (the shooter would be in his own blast; LauncherRules::MinimumRangeCm). */
	TooClose,
	/** Past the weapon's reach. */
	OutOfRange,
	/** A mate of the shooter stands within friendly_clear_m of the impact point. */
	FriendlyNear,
	/** The rocket's line hits something well short of the target (outside the blast round him): it would burst on a nearer wall. */
	NoLine,
	/** Only keep_rockets_for_heavy rockets left and the target is not a heavy one. */
	SavedForHeavy
};

/**
 * Every tunable of the launcher choice (launcher_tactics.json "params"; distances in cm in the code, the keys say their unit). Counts are
 * floats like every key of the flat file.
 */
struct CODEXTACTICS_API FLauncherTacticsConfig
{
	/** "enabled": 0 = the AI never draws the launcher on its own (a launcher in hands still fires; console Codex.Combat.LauncherAI 0). */
	float Enabled = 1.f;
	/** "threshold": a target scoring this much draws the launcher / is fired at. */
	float Threshold = 0.5f;
	/** "ordered_threshold": the PMC commander's RpgStrike order fires at a target scoring this much (a looser rule: it was ordered). */
	float OrderedThreshold = 0.3f;

	/** "cluster.radius_m": enemies within this of the aim point count as one cluster (the rocket's 4 m blast). */
	float ClusterRadiusCm = 400.f;
	/** "cluster.min": the smallest cluster (the target included). */
	float ClusterMin = 2.f;
	/** "cluster.full": the cluster factor is 1 from this count (min -> (min - 1) / (full - 1)). */
	float ClusterFull = 3.f;
	/** "weight.cluster" */
	float WeightCluster = 1.2f;
	/** "weight.cover": the target in cover / behind a barricade (the rifle cannot hit him well). */
	float WeightCover = 0.8f;
	/** "weight.heavy": a vehicle / turret / mortar crewman. */
	float WeightHeavy = 1.5f;
	/** "weight.suppress": a group of suppress.group_min within suppress.group_radius_m that fires at us / closes in. */
	float WeightSuppress = 0.7f;
	/** "weight.single_exposed": a single man in the open (0: the rifle). */
	float WeightSingleExposed = 0.f;
	/** "suppress.group_radius_m": the group round the aim point a rocket suppresses (SuppressionRules launcher.radius_cm, 9 m). */
	float SuppressGroupRadiusCm = 900.f;
	/** "suppress.group_min": the smallest group worth a suppressing rocket (the target included). */
	float SuppressGroupMin = 2.f;

	/** "friendly_clear_m": no rocket when a mate of the shooter stands this close to the impact point. */
	float FriendlyClearCm = 600.f;
	/** "keep_rockets_for_heavy": the last N rockets are kept for a heavy target (0 = none kept; no enemy vehicles yet). */
	float KeepRocketsForHeavy = 0.f;

	/** "draw_s": the switch cost - no shot for this long after the rule draws the launcher (the reload of an empty tube comes on top). */
	float DrawSeconds = 0.6f;
	/** "return_after_s": with no rocket target (and the tube loaded / not reloading) this long, back to the rifle. */
	float ReturnAfterSeconds = 2.f;
	/** "redraw_cooldown_s": after going back to the rifle, no new draw for this long (no flapping between the two). */
	float RedrawCooldownSeconds = 3.f;
	/** "decision_interval_s": the rule re-scores the targets this often while the rifle is in hands. */
	float DecisionIntervalSeconds = 0.25f;
	/** "player_choice_hold_s": the player's own weapon pick (X / the selector) holds this long before the rule may switch again. */
	float PlayerChoiceHoldSeconds = 45.f;
	/** "strike_timeout_s": an ordered strike (RpgStrike) that could not fire within this is dropped. */
	float StrikeTimeoutSeconds = 6.f;
};

/** One target as the rule sees it (the world part fills it: AOperativeCharacter::BuildLauncherTargetView). */
struct CODEXTACTICS_API FLauncherTargetView
{
	/** Shooter -> aim point, cm. */
	float DistanceCm = 0.f;
	/** The launcher's minimum range (LauncherRules::MinimumRangeCm) and reach (Range.MaxRangeCm), cm. */
	float MinRangeCm = 1000.f;
	float MaxRangeCm = 15000.f;
	/** Rockets in the tube + in reserve. */
	int32 RocketsLeft = 0;
	/** Live enemies within cluster.radius_m of the aim point, the target included. */
	int32 ClusterCount = 1;
	/** The target is behind cover / a barricade (the rifle's line is blocked or partial, or he holds a cover slot). */
	bool bInCover = false;
	/** A vehicle / turret / mortar crewman. */
	bool bHeavy = false;
	/** Live enemies within suppress.group_radius_m of the aim point, the target included. */
	int32 SuppressGroup = 1;
	/** The group is a threat now: it fires at the shooter's side or closes in on it. */
	bool bGroupThreat = false;
	/** The shooter's mates within friendly_clear_m of the aim point. */
	int32 FriendliesNear = 0;
	/** The line from the muzzle to the target is blocked outside the blast round him (the rocket would burst short). */
	bool bLineBlockedShort = false;
};

/** The rule's verdict on one target. */
struct CODEXTACTICS_API FLauncherChoice
{
	float Score = 0.f;
	ELauncherUse Reason = ELauncherUse::None;
	ELauncherVeto Veto = ELauncherVeto::None;
	/** No veto and Score >= the threshold asked for. */
	bool bUse = false;
};

namespace LauncherTacticsRules
{
	/** The first veto that applies (None: a rocket may go): no rockets, too close, out of range, a mate near the impact, no line, kept for heavy. */
	CODEXTACTICS_API ELauncherVeto Veto(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View);

	/** The cluster factor of Count enemies: 0 below cluster.min, (Count - 1) / (full - 1) up to 1. */
	CODEXTACTICS_API float ClusterFactor(const FLauncherTacticsConfig& Config, int32 Count);

	/** The best reason and its score (no vetoes). */
	CODEXTACTICS_API FLauncherChoice ScoreReasons(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View);

	/** ScoreReasons + Veto; bUse at Score >= Threshold with no veto. */
	CODEXTACTICS_API FLauncherChoice Evaluate(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View, float Threshold);

	/** The index of the best usable choice (bUse, the highest score; ties: the first), INDEX_NONE when none is usable. */
	CODEXTACTICS_API int32 PickBest(const TArray<FLauncherChoice>& Choices);

	/** How many of Points lie within RadiusCm of Centre (3D). */
	CODEXTACTICS_API int32 CountWithin(const TArray<FVector>& Points, const FVector& Centre, float RadiusCm);

	/**
	 * The player's weapon pick still holds: picked SecondsSincePick ago (< 0 = never) and hold_s not over. While it holds the rule never
	 * switches away from his pick (the designer: the player's manual choice always wins).
	 */
	CODEXTACTICS_API bool IsPlayerPickHeld(const FLauncherTacticsConfig& Config, float SecondsSincePick);

	CODEXTACTICS_API const TCHAR* UseName(ELauncherUse Use);
	CODEXTACTICS_API const TCHAR* VetoName(ELauncherVeto Veto);

	// --- Tuning file ---

	/** Content/Data/Combat/launcher_tactics.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** Every "params" key, in the struct's order. */
	CODEXTACTICS_API TArray<FString> KnownKeys();
	/** The struct's value of Key (metres for the *_m keys); false when unknown. */
	CODEXTACTICS_API bool GetValue(const FLauncherTacticsConfig& Config, const FString& Key, float& OutValue);
	/** Applies the "params" object of a launcher_tactics.json text over InOut; unknown keys go to OutUnknown. False: not JSON / no params. */
	CODEXTACTICS_API bool ApplyJson(const FString& JsonText, FLauncherTacticsConfig& InOut, TArray<FString>* OutUnknown = nullptr);
	/** The values in force (the file on the first call; the defaults when it is missing). */
	CODEXTACTICS_API const FLauncherTacticsConfig& GetConfig();
	CODEXTACTICS_API void SetConfig(const FLauncherTacticsConfig& Config);
	/** Re-reads the file into GetConfig; false (values kept) when missing / unreadable. */
	CODEXTACTICS_API bool ReloadConfig();
}
