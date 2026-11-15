#pragma once

// UE-only (no Godot reference): frame-pacing gate of the enemy-appear hitch probe (004, 2026-11-15). Pure logic; the limits are
// console variables (Codex.HitchProbe.*) so a designer can tighten / loosen them without a rebuild.

#include "CoreMinimal.h"

/** Limits of the hitch probe (CodexTactics.HitchProbeSmoke). */
struct CODEXTACTICS_API FHitchGateLimits
{
	/** Hard fail: any measured frame, single spawn or same-frame spawn sum above this (Codex.HitchProbe.HardLimitMs). */
	double HardLimitMs = 250.0;
	/** Spawn-window gate: the slowest frame within SpawnWindowFrames of an enemy spawn (Codex.HitchProbe.SpawnMaxFrameMs). */
	double SpawnMaxFrameMs = 50.0;
	/** A frame slower than this counts as a visible stutter (two 60 Hz frames; Codex.HitchProbe.SlowFrameMs). Reported. */
	double SlowFrameMs = 33.0;
	/** Frames after a spawn frame that still belong to its window (first animation / render use; Codex.HitchProbe.SpawnWindowFrames). */
	int32 SpawnWindowFrames = 10;
	/** Slow frames allowed in all spawn windows together; negative = report only (Codex.HitchProbe.MaxSlowSpawnFrames). */
	int32 MaxSlowSpawnFrames = -1;
	/**
	 * Scripted-wave window gate (the whole roster spawned in one call, 34 enemies on L_Gen_Depot_1: 30-85 ms measured 2026-11-15);
	 * negative = report only until the wave spawn is budgeted over frames (Codex.HitchProbe.WaveMaxFrameMs).
	 */
	double WaveMaxFrameMs = -1.0;
};

/** One measured game-thread frame. */
struct CODEXTACTICS_API FHitchFrameSample
{
	uint64 Frame = 0;
	double Ms = 0.0;
};

/** Summary of the frames in a window. */
struct CODEXTACTICS_API FHitchWindowStats
{
	int32 Frames = 0;
	double MaxMs = 0.0;
	uint64 MaxFrame = 0;
	int32 SlowFrames = 0;
};

namespace HitchGateRules
{
	/** The limits from the Codex.HitchProbe.* console variables (defaults = FHitchGateLimits{}). */
	CODEXTACTICS_API FHitchGateLimits FromConsole();

	/** Is Frame inside [SpawnFrame, SpawnFrame + WindowFrames] of any of SpawnFrames? */
	CODEXTACTICS_API bool IsInSpawnWindow(uint64 Frame, TConstArrayView<uint64> SpawnFrames, int32 WindowFrames);

	/** Frames inside the spawn windows (all frames when bAllFrames): count, slowest, slower than SlowFrameMs. */
	CODEXTACTICS_API FHitchWindowStats Summarize(TConstArrayView<FHitchFrameSample> Samples, TConstArrayView<uint64> SpawnFrames,
		const FHitchGateLimits& Limits, bool bAllFrames = false);

	/** The stricter spawn gate: at least one frame measured, the slowest <= SpawnMaxFrameMs, the slow-frame budget kept (when set). */
	CODEXTACTICS_API bool PassesSpawnGate(const FHitchWindowStats& Stats, const FHitchGateLimits& Limits);

	/** The scripted-wave gate: always passes while WaveMaxFrameMs is negative (report only); else a measured window <= WaveMaxFrameMs. */
	CODEXTACTICS_API bool PassesWaveGate(const FHitchWindowStats& Stats, const FHitchGateLimits& Limits);
}
