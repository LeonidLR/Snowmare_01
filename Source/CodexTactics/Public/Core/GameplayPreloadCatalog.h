#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class ACodexTacticsGameMode;
class FJsonObject;

/** One asset to prepare before gameplay; required paths must be reported separately from optional fallbacks. */
struct CODEXTACTICS_API FGameplayPreloadPath
{
	FSoftObjectPath Path;
	bool bRequired = false;
};

/** Collects paths only; never loads assets or changes the active level. */
namespace GameplayPreloadCatalog
{
	/** Runtime-configured class, body, weapon, PMC and ladder paths. Level and weapon tuning must already be applied. */
	CODEXTACTICS_API void GatherInitial(const ACodexTacticsGameMode& Mode, TArray<FGameplayPreloadPath>& Out);

	/** Weapon animation JSON paths and existing retarget copies. Native default clips are not included yet. */
	CODEXTACTICS_API void GatherAnimations(TArray<FGameplayPreloadPath>& Out);

	/** Enumerates one weapon block using the runtime retarget rule; PackageExists receives long package names. */
	CODEXTACTICS_API void GatherAnimationBlock(const FJsonObject& Block, TFunctionRef<bool(const FString&)> PackageExists,
		TArray<FGameplayPreloadPath>& Out);
}
