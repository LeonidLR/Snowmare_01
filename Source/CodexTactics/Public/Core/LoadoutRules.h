#pragma once

#include "CoreMinimal.h"
#include "Core/MissionSessionSubsystem.h"
#include "Data/WaveConfigTypes.h"

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
	/** Level mode, overridden by the start: «Начать игру» collects, «Начать бой» turns collecting into the starting set. */
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
}
