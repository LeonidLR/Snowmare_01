#include "Environment/EnvironmentTrace.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogCodexEnvTrace);

double EnvironmentTrace::GetTime(const UObject* WorldContext)
{
	const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetTimeSeconds() : -1.0;
}
