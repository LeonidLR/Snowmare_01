#pragma once

#include "CoreMinimal.h"

/**
 * Start-flicker trace (docs/systems/environment-weather.md section 19): timestamped one-line events of everything that can change the
 * picture in the first seconds of a level - the presenter's applications, the exposure mode, sky light recaptures, overlay scans / toggles,
 * Niagara activations, the lightning flash, the interactive-snow render target, the minimap captures, character snow pushes. Each line
 * starts "[EnvTrace] f<engine frame> t<game seconds>", so a rendered frame capture (CodexTactics.StartFlickerShots) can line its
 * luminance jumps up with them. One-off events log at Log, per-frame / per-apply events at Verbose
 * (-LogCmds="LogCodexEnvTrace Verbose"). UE-only, no Godot reference.
 */
CODEXTACTICS_API DECLARE_LOG_CATEGORY_EXTERN(LogCodexEnvTrace, Log, All);

namespace EnvironmentTrace
{
	/** The game time of the context's world (-1 without a world). */
	CODEXTACTICS_API double GetTime(const UObject* WorldContext);
}

/** UE_LOG into LogCodexEnvTrace with the engine frame and the world's game time in front. */
#define CODEX_ENV_TRACE(Verbosity, WorldContext, Format, ...) \
	UE_LOG(LogCodexEnvTrace, Verbosity, TEXT("[EnvTrace] f%llu t%.3f ") Format, static_cast<uint64>(GFrameCounter), EnvironmentTrace::GetTime(WorldContext), ##__VA_ARGS__)
