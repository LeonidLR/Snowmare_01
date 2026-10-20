#pragma once

#include "CoreMinimal.h"

/** What an idle enemy does when its idle-break timer runs out (EnemyAnimRules::ChooseIdleBreak). */
enum class EEnemyIdleBreak : uint8
{
	/** Nothing to play (no clips): keeps its idle loop. */
	None,
	/** Another idle clip as a one-shot fidget (idle variety). */
	Fidget,
	/** The calm idle activity (the Military Zombie's devour / feeding). */
	Activity
};

/**
 * Pure rules of the enemy animation extras (game designer 2026-10-20, Military Zombie; UE-only, no Godot reference): per-instance
 * clip variants picked deterministically from a spawn seed, idle breaks (fidgets / a calm activity), and the stand-still lock of
 * the alert roar / the spawn entrance. Used by UEnemyAnimInstance and AEnemyCharacter; unit tests CodexTactics.Characters.EnemyAnim.*.
 */
namespace EnemyAnimRules
{
	/**
	 * Seed of one enemy's visual / clip variants: the same actor name at the same spawn point (10 cm grid) gives the same
	 * look in every run (waves spawn in a fixed order), two enemies side by side differ. Override >= 0 wins (a designer pins one).
	 */
	CODEXTACTICS_API uint32 MakeVariantSeed(const FString& ActorName, const FVector& SpawnLocation, int32 Override = -1);

	/**
	 * Index 0..Count-1 for Seed and a per-use Salt (a different salt per list, so the walk variant does not follow the material
	 * set); -1 when Count <= 0. Deterministic and spread over the whole range.
	 */
	CODEXTACTICS_API int32 PickVariantIndex(uint32 Seed, uint32 Salt, int32 Count);

	/**
	 * A calm idle the anim instance may break with a fidget / activity: standing (not moving), alive, not attacking / hit /
	 * knocked down, no one-shot of its own playing, not in the turn-based fight.
	 */
	CODEXTACTICS_API bool IsCalmIdle(bool bMoving, bool bDead, bool bKnockedDown, bool bOneShotPlaying, bool bTurnBased);

	/**
	 * Which break to play: the activity (devour) only while it does not hunt the squad, with ActivityChance (Roll 0..1);
	 * otherwise a fidget when there are fidget clips; the activity also stands in when there are no fidgets and it is calm.
	 */
	CODEXTACTICS_API EEnemyIdleBreak ChooseIdleBreak(float Roll, float ActivityChance, int32 NumFidgets, int32 NumActivities, bool bHunting);

	/**
	 * Index of a fidget clip that is not the idle loop it plays now (Current; -1 = none of them): Roll 0..1 over the others;
	 * the only clip when there is one; -1 without clips.
	 */
	CODEXTACTICS_API int32 PickFidget(float Roll, int32 NumFidgets, int32 Current);

	/** Seconds the enemy stands still for a clip of ClipSeconds at PlayRate, capped at MaxSeconds (0 = no cap); 0 without a clip. */
	CODEXTACTICS_API float PerformanceLockSeconds(float ClipSeconds, float PlayRate, float MaxSeconds);

	/** Seconds of calm idle before the next break: Min..Max by Roll (0..1); a swapped range is ordered; never below 0.5 s. */
	CODEXTACTICS_API float NextIdleBreakDelay(float Roll, float MinSeconds, float MaxSeconds);
}
