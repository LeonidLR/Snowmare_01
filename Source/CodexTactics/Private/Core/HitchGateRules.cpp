#include "Core/HitchGateRules.h"

#include "HAL/IConsoleManager.h"

namespace HitchGateRulesLocal
{
	const FHitchGateLimits Defaults;
	TAutoConsoleVariable<float> CVarHardLimitMs(TEXT("Codex.HitchProbe.HardLimitMs"), static_cast<float>(Defaults.HardLimitMs),
		TEXT("HitchProbeSmoke hard fail (ms): any measured frame, single spawn or same-frame spawn sum above it."));
	TAutoConsoleVariable<float> CVarSpawnMaxFrameMs(TEXT("Codex.HitchProbe.SpawnMaxFrameMs"), static_cast<float>(Defaults.SpawnMaxFrameMs),
		TEXT("HitchProbeSmoke spawn gate (ms): the slowest game-thread frame within SpawnWindowFrames of an enemy spawn."));
	TAutoConsoleVariable<float> CVarSlowFrameMs(TEXT("Codex.HitchProbe.SlowFrameMs"), static_cast<float>(Defaults.SlowFrameMs),
		TEXT("HitchProbeSmoke: a frame slower than this (ms) counts as a visible stutter (reported)."));
	TAutoConsoleVariable<int32> CVarSpawnWindowFrames(TEXT("Codex.HitchProbe.SpawnWindowFrames"), Defaults.SpawnWindowFrames,
		TEXT("HitchProbeSmoke: frames after a spawn frame that still belong to its window."));
	TAutoConsoleVariable<float> CVarWaveMaxFrameMs(TEXT("Codex.HitchProbe.WaveMaxFrameMs"), static_cast<float>(Defaults.WaveMaxFrameMs),
		TEXT("HitchProbeSmoke scripted-wave gate (ms): the slowest frame of the whole-roster wave spawn window; negative = report only."));
	TAutoConsoleVariable<int32> CVarMaxSlowSpawnFrames(TEXT("Codex.HitchProbe.MaxSlowSpawnFrames"), Defaults.MaxSlowSpawnFrames,
		TEXT("HitchProbeSmoke: slow frames allowed in all spawn windows together; negative = report only."));
}

FHitchGateLimits HitchGateRules::FromConsole()
{
	using namespace HitchGateRulesLocal;
	FHitchGateLimits Limits;
	Limits.HardLimitMs = CVarHardLimitMs.GetValueOnAnyThread();
	Limits.SpawnMaxFrameMs = CVarSpawnMaxFrameMs.GetValueOnAnyThread();
	Limits.SlowFrameMs = CVarSlowFrameMs.GetValueOnAnyThread();
	Limits.SpawnWindowFrames = FMath::Max(0, CVarSpawnWindowFrames.GetValueOnAnyThread());
	Limits.MaxSlowSpawnFrames = CVarMaxSlowSpawnFrames.GetValueOnAnyThread();
	Limits.WaveMaxFrameMs = CVarWaveMaxFrameMs.GetValueOnAnyThread();
	return Limits;
}

bool HitchGateRules::IsInSpawnWindow(uint64 Frame, TConstArrayView<uint64> SpawnFrames, int32 WindowFrames)
{
	const uint64 Window = static_cast<uint64>(FMath::Max(0, WindowFrames));
	for (const uint64 Spawn : SpawnFrames)
	{
		if (Frame >= Spawn && Frame - Spawn <= Window)
		{
			return true;
		}
	}
	return false;
}

FHitchWindowStats HitchGateRules::Summarize(TConstArrayView<FHitchFrameSample> Samples, TConstArrayView<uint64> SpawnFrames,
	const FHitchGateLimits& Limits, bool bAllFrames)
{
	FHitchWindowStats Stats;
	for (const FHitchFrameSample& Sample : Samples)
	{
		if (!bAllFrames && !IsInSpawnWindow(Sample.Frame, SpawnFrames, Limits.SpawnWindowFrames))
		{
			continue;
		}
		++Stats.Frames;
		if (Sample.Ms > Stats.MaxMs)
		{
			Stats.MaxMs = Sample.Ms;
			Stats.MaxFrame = Sample.Frame;
		}
		Stats.SlowFrames += Sample.Ms > Limits.SlowFrameMs ? 1 : 0;
	}
	return Stats;
}

bool HitchGateRules::PassesSpawnGate(const FHitchWindowStats& Stats, const FHitchGateLimits& Limits)
{
	return Stats.Frames > 0 && Stats.MaxMs <= Limits.SpawnMaxFrameMs
		&& (Limits.MaxSlowSpawnFrames < 0 || Stats.SlowFrames <= Limits.MaxSlowSpawnFrames);
}

bool HitchGateRules::PassesWaveGate(const FHitchWindowStats& Stats, const FHitchGateLimits& Limits)
{
	return Limits.WaveMaxFrameMs < 0.0 || (Stats.Frames > 0 && Stats.MaxMs <= Limits.WaveMaxFrameMs);
}
