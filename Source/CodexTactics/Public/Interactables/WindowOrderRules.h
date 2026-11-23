// Window order rules (designer decision 2026-11-23, Lead relay): who may path / climb through a WHOLE (shut) window, per mode and fight.
// UE-only (no Godot reference). Tuning: Content/Data/Interactables/windows.json.
#pragma once

#include "CoreMinimal.h"

/**
 * The window switches (windows.json). The three designer rules:
 *  1. Solo with the whole squad box-selected, in a fight: the order opens every window on the way (no getting stuck; the noise no longer
 *     matters) - "solo_group_combat_windows", "solo_group_whole_squad_only".
 *  2. Formation: the followers go through a window only once it is broken (the leader broke it on his window order); the formation never
 *     takes a whole window on its own - "formation_follow_broken_only", "formation_run_in_fight_windows" (0: a run in a fight opens none).
 *  3. Solo out of a fight: a window only on the explicit window order (the silhouette click; loud) - nothing here opens one.
 * "solo_run_in_fight_windows": the 2026-11-22 rule kept for Solo - a run order (double click) in a fight opens the windows.
 */
struct CODEXTACTICS_API FWindowOrderTuning
{
	bool bSoloGroupCombatWindows = true;
	bool bSoloGroupWholeSquadOnly = true;
	bool bSoloRunInFightWindows = true;
	bool bFormationRunInFightWindows = false;
	bool bFormationFollowBrokenOnly = true;
};

namespace WindowOrderRules
{
	/** The current tuning (windows.json, read once; Reload re-reads). */
	CODEXTACTICS_API const FWindowOrderTuning& Get();
	CODEXTACTICS_API void Reload();
	/** windows.json text -> tuning (missing keys keep the defaults). */
	CODEXTACTICS_API FWindowOrderTuning Parse(const FString& JsonText);
	CODEXTACTICS_API FString FilePath();

	/**
	 * Whether a plain move order (no window click) opens the whole windows for its paths: bSolo = Solo mode, SelectedCount / SquadCount the
	 * box selection and the squad, bInFight = a wave / firefight on or enemies hunting the squad, bSprint = a run order (double click).
	 */
	CODEXTACTICS_API bool MoveOpensWindows(bool bSolo, int32 SelectedCount, int32 SquadCount, bool bInFight, bool bSprint, const FWindowOrderTuning& Tuning);

	/**
	 * Whether this man's path may take a WHOLE window now: bWindowOrder = the squad's window flag (a window click or MoveOpensWindows),
	 * bFormationFollower = a formation follower (not Solo, not the formation leader). Rule 2: a follower never does - he follows through it
	 * once the leader broke it (a broken window is an ordinary vault for everyone).
	 */
	CODEXTACTICS_API bool MayTakeWholeWindow(bool bWindowOrder, bool bFormationFollower, const FWindowOrderTuning& Tuning);
}
