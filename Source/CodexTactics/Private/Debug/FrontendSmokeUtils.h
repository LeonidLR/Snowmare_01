#pragma once

// Shared helpers of the frontend smokes (FrontendSmoke, MainMenuSmoke, DialogueSmoke): they travel between the menu
// map and the mission level, so they run on the core ticker and look the game world up every step.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"

namespace FrontendSmokeUtils
{
	inline UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** Opens the frontend map (title screen) unless the world already is it; true when it is. */
	inline bool EnsureFrontend(UWorld* World)
	{
		if (UCodexFrontendSubsystem::IsFrontendWorld(World))
		{
			return true;
		}
		const FString Map = UCodexFrontendSettings::Get().FrontendLevel.ToSoftObjectPath().GetLongPackageName();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: opening the frontend map %s"), *Map);
		UGameplayStatics::OpenLevel(World, FName(*Map));
		return false;
	}
}

#endif // !UE_BUILD_SHIPPING
