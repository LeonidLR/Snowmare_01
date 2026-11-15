#pragma once

// Shared helpers of the frontend smokes (FrontendSmoke, MainMenuSmoke, DialogueSmoke): they travel between the menu
// map and the mission level, so they run on the core ticker and look the game world up every step.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Core/GameplayPreloadSubsystem.h"
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

	/**
	 * 004 (2026-11-15): true while the game world prepares (UGameplayPreloadSubsystem: the paused world, the squad not spawned yet).
	 * A smoke that travels into a level checks it at the top of its step and returns (ticks again) BEFORE advancing its clocks, so its
	 * delays ("StageTime < 1.5") start when the level is playable. The preload's own 120 s timeout bounds the wait.
	 */
	inline bool IsGameWorldPreparing()
	{
		return UGameplayPreloadSubsystem::IsPreparing(FindGameWorld());
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
