#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the death cinematic (user request 2026-10-08; UE-only, no Godot reference — Godot failed the mission at
 * once). When an operative dies the camera focuses on him, time slows down, his death clip plays out, then the camera
 * returns and time resumes; the commander's death then fades to black, shows «THE SQUAD HAS FALLEN» and opens the
 * mission-failed screen. Tested in CodexTactics.Combat.DeathCinematic.*; console overrides Codex.DeathCam.*.
 */
struct CODEXTACTICS_API FDeathCinematicConfig
{
	/** World time scale during the slow motion (never above the current flow dilation: no stacking with the pause). */
	float SlowMoScale = 0.3f;
	/** Real seconds of slow motion at the start of a focus. */
	float SlowMoRealSeconds = 2.f;
	/** Camera glide onto the fallen operative, real s. */
	float FocusBlendSeconds = 0.5f;
	/** Camera distance while focused, cm (<= 0 keeps the current zoom). */
	float FocusDistance = 1100.f;
	/** The focus holds this long after the death clip has finished (game time), s. */
	float HoldAfterClipSeconds = 0.4f;
	/** Death clip length assumed when none is playing, s. */
	float FallbackClipSeconds = 1.f;
	/** A focus never lasts longer than this, real s. */
	float MaxFocusRealSeconds = 6.f;
	/** Camera glide back to the fight, real s. */
	float ReturnBlendSeconds = 0.6f;
	/** Commander: fade to black, real s. */
	float DefeatFadeSeconds = 1.2f;
	/** Commander: «THE SQUAD HAS FALLEN» on black before the mission-failed screen, real s. */
	float DefeatTextSeconds = 2.5f;
};

enum class EDeathCinematicPhase : uint8
{
	Idle,
	/** Camera on the fallen operative (slow motion first). */
	Focus,
	/** Commander only: fading to black. */
	DefeatFade,
	/** Commander only: the defeat line on black. */
	DefeatText,
	/** The mission-failed screen is up. */
	Done
};

/** One running focus (real / game seconds since it began). */
struct FDeathFocusTimes
{
	float RealSeconds = 0.f;
	float GameSeconds = 0.f;
	/** Length of the death clip being shown (play-rate adjusted), s. */
	float ClipSeconds = 1.f;
	/** This focus slows time down (false for a death queued behind another one: no second slow motion). */
	bool bSlowMo = true;
};

namespace DeathCinematicRules
{
	/** Config with the Codex.DeathCam.* console overrides applied. */
	CODEXTACTICS_API FDeathCinematicConfig GetConfig();

	/**
	 * World time dilation wanted during a focus. BaseDilation = what the game flow wants now (1 real time, ~0.02 tactical
	 * pause, 0 game over): the slow motion only ever lowers it, never stacks below it.
	 */
	CODEXTACTICS_API float FocusDilation(const FDeathCinematicConfig& Config, const FDeathFocusTimes& Times, float BaseDilation);

	/** The focus is over: the slow motion has run, the death clip has played (+ hold), or the real-time cap is hit. */
	CODEXTACTICS_API bool IsFocusFinished(const FDeathCinematicConfig& Config, const FDeathFocusTimes& Times);

	/** Black overlay alpha (0..1) for a phase and the real seconds spent in it. */
	CODEXTACTICS_API float FadeAlpha(const FDeathCinematicConfig& Config, EDeathCinematicPhase Phase, float PhaseRealSeconds);

	/** The defeat line is drawn (on black). */
	CODEXTACTICS_API bool ShowsDefeatText(EDeathCinematicPhase Phase);
}
