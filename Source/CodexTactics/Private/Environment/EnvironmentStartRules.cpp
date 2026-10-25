#include "Environment/EnvironmentStartRules.h"

#include "Environment/WeatherSnowFxRules.h"

FWeatherRigPlan EnvironmentStartRules::PlanRig(const FLevelLightingInventory& Level, bool bBuildMissing)
{
	FWeatherRigPlan Plan;
	const bool bSun = Level.DirectionalLights > 0;
	Plan.bOwnSun = bBuildMissing && !bSun;
	Plan.bOwnSkyAtmosphere = bBuildMissing && !Level.bSkyAtmosphere;
	Plan.bOwnSkyLight = bBuildMissing && !Level.bSkyLight;
	Plan.bOwnHeightFog = bBuildMissing && !Level.bHeightFog;
	// As before the own rig existed: the moon is the level's second directional light, else the presenter's; same for the post-process.
	Plan.bOwnMoon = Level.DirectionalLights < 2;
	Plan.bOwnPostProcess = !Level.bUnboundPostProcess;
	const int32 Own = (Plan.bOwnSun ? 1 : 0) + (Plan.bOwnSkyAtmosphere ? 1 : 0) + (Plan.bOwnSkyLight ? 1 : 0) + (Plan.bOwnHeightFog ? 1 : 0);
	Plan.Mode = Own == 0 ? EWeatherRigMode::Adopted : (Own == 4 ? EWeatherRigMode::Own : EWeatherRigMode::Mixed);
	return Plan;
}

FString EnvironmentStartRules::RigModeToString(EWeatherRigMode Mode)
{
	switch (Mode)
	{
	case EWeatherRigMode::Own: return TEXT("own");
	case EWeatherRigMode::Mixed: return TEXT("mixed");
	default: return TEXT("adopted");
	}
}

float EnvironmentStartRules::GetSnowWarmupSeconds(const FWeatherSnowFxTuning& Tuning)
{
	const float Seconds = Tuning.StartWarmupSeconds > 0.f ? Tuning.StartWarmupSeconds : FMath::Max(Tuning.LifetimeMin, Tuning.LifetimeMax);
	return FMath::Clamp(Seconds, 0.f, MaxWarmupSeconds);
}

int32 EnvironmentStartRules::GetSnowWarmupTicks(float WarmupSeconds, float TickSeconds)
{
	if (WarmupSeconds <= 0.f)
	{
		return 0;
	}
	const float Tick = FMath::Max(TickSeconds, 1.f / 240.f);
	return FMath::Max(1, FMath::CeilToInt(WarmupSeconds / Tick - KINDA_SMALL_NUMBER));
}

bool EnvironmentStartRules::ShouldSnap(bool bFirstApplication, int32 SeenSnapSerial, int32 CurrentSnapSerial)
{
	return bFirstApplication || SeenSnapSerial != CurrentSnapSerial;
}

bool EnvironmentStartRules::IsExposureSnapActive(float SecondsSinceSnap, float WindowSeconds)
{
	return SecondsSinceSnap >= 0.f && SecondsSinceSnap < WindowSeconds;
}
