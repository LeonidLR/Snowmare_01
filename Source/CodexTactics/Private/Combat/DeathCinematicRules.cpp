#include "Combat/DeathCinematicRules.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarDeathCamSlowMo(TEXT("Codex.DeathCam.SlowMoScale"), -1.f,
		TEXT("Death cinematic: world time scale during the slow motion (<0 = default 0.3)."));
	TAutoConsoleVariable<float> CVarDeathCamSlowMoSeconds(TEXT("Codex.DeathCam.SlowMoRealSeconds"), -1.f,
		TEXT("Death cinematic: real seconds of slow motion (<0 = default 2; 0 = no slow motion)."));
	TAutoConsoleVariable<float> CVarDeathCamDistance(TEXT("Codex.DeathCam.FocusDistance"), -1.f,
		TEXT("Death cinematic: camera distance on the fallen operative, cm (<0 = default 1100)."));
	TAutoConsoleVariable<float> CVarDeathCamHold(TEXT("Codex.DeathCam.HoldAfterClipSeconds"), -1.f,
		TEXT("Death cinematic: hold after the death clip ends, s (<0 = default 0.4)."));
	TAutoConsoleVariable<float> CVarDeathCamMax(TEXT("Codex.DeathCam.MaxFocusRealSeconds"), -1.f,
		TEXT("Death cinematic: longest focus, real s (<0 = default 6)."));
	TAutoConsoleVariable<float> CVarDeathCamFade(TEXT("Codex.DeathCam.DefeatFadeSeconds"), -1.f,
		TEXT("Death cinematic: commander fade to black, real s (<0 = default 1.2)."));
	TAutoConsoleVariable<float> CVarDeathCamText(TEXT("Codex.DeathCam.DefeatTextSeconds"), -1.f,
		TEXT("Death cinematic: «THE SQUAD HAS FALLEN» on black, real s (<0 = default 2.5)."));

	void DeathCamOverride(float& Value, const TAutoConsoleVariable<float>& CVar)
	{
		const float Override = CVar.GetValueOnGameThread();
		if (Override >= 0.f)
		{
			Value = Override;
		}
	}
}

FDeathCinematicConfig DeathCinematicRules::GetConfig()
{
	FDeathCinematicConfig Config;
	DeathCamOverride(Config.SlowMoScale, CVarDeathCamSlowMo);
	DeathCamOverride(Config.SlowMoRealSeconds, CVarDeathCamSlowMoSeconds);
	DeathCamOverride(Config.FocusDistance, CVarDeathCamDistance);
	DeathCamOverride(Config.HoldAfterClipSeconds, CVarDeathCamHold);
	DeathCamOverride(Config.MaxFocusRealSeconds, CVarDeathCamMax);
	DeathCamOverride(Config.DefeatFadeSeconds, CVarDeathCamFade);
	DeathCamOverride(Config.DefeatTextSeconds, CVarDeathCamText);
	Config.SlowMoScale = FMath::Clamp(Config.SlowMoScale, 0.01f, 1.f);
	return Config;
}

float DeathCinematicRules::FocusDilation(const FDeathCinematicConfig& Config, const FDeathFocusTimes& Times, float BaseDilation)
{
	if (!Times.bSlowMo || Times.RealSeconds >= Config.SlowMoRealSeconds)
	{
		return BaseDilation;
	}
	return FMath::Min(BaseDilation, Config.SlowMoScale);
}

bool DeathCinematicRules::IsFocusFinished(const FDeathCinematicConfig& Config, const FDeathFocusTimes& Times)
{
	if (Times.RealSeconds >= Config.MaxFocusRealSeconds)
	{
		return true;
	}
	const bool bSlowMoDone = !Times.bSlowMo || Times.RealSeconds >= Config.SlowMoRealSeconds;
	const bool bClipDone = Times.GameSeconds >= Times.ClipSeconds + Config.HoldAfterClipSeconds;
	// The camera has to arrive on him before it may leave again.
	return bSlowMoDone && bClipDone && Times.RealSeconds >= Config.FocusBlendSeconds;
}

float DeathCinematicRules::FadeAlpha(const FDeathCinematicConfig& Config, EDeathCinematicPhase Phase, float PhaseRealSeconds)
{
	switch (Phase)
	{
	case EDeathCinematicPhase::DefeatFade:
		return Config.DefeatFadeSeconds <= 0.f ? 1.f : FMath::Clamp(PhaseRealSeconds / Config.DefeatFadeSeconds, 0.f, 1.f);
	case EDeathCinematicPhase::DefeatText:
		return 1.f;
	default:
		return 0.f;
	}
}

bool DeathCinematicRules::ShowsDefeatText(EDeathCinematicPhase Phase)
{
	return Phase == EDeathCinematicPhase::DefeatText;
}
