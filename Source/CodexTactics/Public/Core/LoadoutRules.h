#pragma once

#include "CoreMinimal.h"
#include "Core/MissionSessionSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/GameFlowTypes.h"

/** How the squad's engineering supply is set when the preparation begins. */
enum class ELoadoutMode : uint8
{
	/** Keep what the squad collected, gathered on the role specialists. */
	ExploreAndCollect,
	/** At least the starting set: commander 1 turret, engineer 2 barricades, medic-sapper 2 mines. */
	StartingUnique,
	/** The level's preset tier (or its custom numbers). */
	EditorPreset
};

/** One operative's supply the loadout touches. */
struct CODEXTACTICS_API FLoadoutSupply
{
	int32 Turrets = 0;
	int32 Barricades = 0;
	int32 Mines = 0;
	/** Commander only in EditorPreset; -1 = unchanged. */
	int32 Medkits = -1;
};

/**
 * Pure combat loadout rules.
 * Godot reference: Scenes/movements/main.gd _apply_stage_exploration_resources (without the --bot-loadout CLI override).
 */
namespace LoadoutRules
{
	/** Level mode, overridden by the start: "Start game" collects, "Start combat" turns collecting into the starting set. */
	CODEXTACTICS_API ELoadoutMode ResolveMode(const FString& LevelMode, EMissionStartMode StartMode);

	/**
	 * Applies the mode to the three role specialists (totals over all three). Godot quirk kept: collecting gives the
	 * specialist the squad total while the others keep theirs (only the turrets of the engineer / medic are cleared).
	 * OutM16Reserve / OutPistolReserve: EditorPreset reserves, -1 otherwise.
	 */
	CODEXTACTICS_API void Apply(ELoadoutMode Mode, const FSquadLoadout& Loadout, FLoadoutSupply& Commander, FLoadoutSupply& Engineer,
		FLoadoutSupply& Medic, int32& OutM16Reserve, int32& OutPistolReserve);

	/**
	 * Grenades each operative starts with (UE-only, Wave Editor): EDITOR_PRESET tiers MINIMAL 1 / STANDARD 2 / MAXIMAL 4,
	 * CUSTOM the level's grenades_count; the other modes the level's grenades_count when set. -1: keep the operatives' own.
	 * Clamped to MaxCarried.
	 */
	CODEXTACTICS_API int32 GrenadesFor(ELoadoutMode Mode, const FSquadLoadout& Loadout, int32 MaxCarried);

	/**
	 * Heat packs a starting operative spawns with (UE-only): the level's "heat_packs_count" when >= 0, else the role kit of
	 * tactical_heat.json; both clamped to [0, MaxStack].
	 */
	CODEXTACTICS_API int32 StartingHeatPacks(int32 RoleKit, int32 LevelOverride, int32 MaxStack);

	/**
	 * The stage loadout (UMissionSubsystem::ApplyStageLoadout when the preparation / a sudden first wave begins) is applied only to a
	 * squad that did NOT arrive through the campaign: a carried squad keeps the kit it brought (2026-11-08: the Depot's squad_loadout
	 * "grenades_count" must not replace the grenades carried from the lake). UE-only.
	 */
	CODEXTACTICS_API bool ShouldApplyStageLoadout(bool bCampaignArrival);

	/**
	 * Whether a game-flow phase change runs the stage loadout (Training Ground pass 1, 2026-11-09): at most once per level, at the first of
	 * the cutscene end / sudden first wave (bCutsceneEnded) or the first change into Preparation / WaveCombat (ambush / skirmish / firefight
	 * levels without a cutscene end). Before, a mis-indented call ran it on EVERY phase change (grenades reset at each fight start / end).
	 */
	CODEXTACTICS_API bool ShouldRunStageLoadout(bool bAlreadyApplied, bool bCutsceneEnded, ECodexGamePhase NewPhase);

	/** "Kit wins" (level JSON "kit"): the current value when the level's kit sets the field, else the stage value (Stage < 0 = keep the current one). */
	CODEXTACTICS_API int32 StageOrKit(int32 Stage, int32 Current, bool bKitSets);
}
