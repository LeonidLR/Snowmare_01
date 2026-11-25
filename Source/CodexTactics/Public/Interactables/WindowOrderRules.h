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
	/** Centimetres past the face on the operative's own side (the walk-up before the climb). */
	float ApproachPastCm = 80.f;
	/** Centimetres past the face on the far side. The order's landing. */
	float LandingPastCm = 120.f;
	/** This move is the climb when its goal is within this of the landing (the leader's target is the landing). */
	float ApproachMatchCm = 200.f;
	/** He has reached the near face when the walk stops within this of the approach point. */
	float ApproachArriveCm = 150.f;
};

/**
 * Which side of a window the operative is on, and the two feet points of a climb-in.
 * Distances are centimetres. Z is copied from the operative location passed to ChooseSide.
 */
struct CODEXTACTICS_API FWindowSideChoice
{
	/** Unit horizontal axis through the opening: the shorter of the actor's forward and right. */
	FVector Through = FVector::ForwardVector;
	/** +1 when the operative stands on the +Through side of the centre. */
	float OperativeSide = 1.f;
	/** Feet on his side, ApproachPastCm past the face. */
	FVector Approach = FVector::ZeroVector;
	/** Feet on the other side, LandingPastCm past the face. */
	FVector Landing = FVector::ZeroVector;
	/** Yaw facing from the approach toward the landing. */
	float Yaw = 0.f;
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

	/**
	 * The side he is on. Through is the shorter horizontal axis (actor forward when SizeX < SizeY, otherwise actor right),
	 * so a long mesh forward does not pick the side. Approach is on his side of the centre; Landing is the other side.
	 */
	CODEXTACTICS_API FWindowSideChoice ChooseSide(const FVector& OperativeLocation, const FVector& Center, const FVector& ActorForward,
		const FVector& ActorRight, float SizeX, float SizeY, float ApproachPastCm, float LandingPastCm);
}
