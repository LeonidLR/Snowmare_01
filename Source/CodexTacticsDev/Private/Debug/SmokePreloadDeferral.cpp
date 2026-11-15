// Holds the headless smoke commands (-ExecCmds="CodexTactics.<Name>") until the world preload is done (004, 2026-11-15). UE-only.
//
// The async world preload (UGameplayPreloadSubsystem) holds the squad spawn, the animation binding and the operatives' ticks for a few
// seconds after the map loads, while -ExecCmds run on the first engine frame and most smokes count their start-up delays ("Time < 3")
// on the core ticker. Rather than gating each of the ~200 smokes, the dev module keeps the queued CodexTactics.* commands (and everything
// queued after them, so the order of smoke_parallel -PreCmds is kept) in its own list while the game world prepares, and puts them back
// at the front of GEngine->DeferredCommands once it is released (Ready, Failed or Disabled). Smokes that must observe the preparation
// themselves (HitchProbeSmoke) are exempt; smokes with their own gate (SmokeUtils::FPreloadGate) pass it at once.
// Only in a -game process (not the editor: a command typed in PIE runs immediately; such smokes carry FPreloadGate where needed).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "CodexTactics.h"
#include "Core/GameplayPreloadSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"

namespace SmokePreloadDeferral
{
	TArray<FString> Held;
	double HeldSince = 0.0;

	bool IsExempt(const FString& Command)
	{
		// HitchProbeSmoke checks that the preparation holds the clocks and positions: it has to start during it.
		return Command.Contains(TEXT("HitchProbeSmoke"));
	}

	void OnBeginFrame()
	{
		if (!GEngine || GIsEditor)
		{
			return;
		}
		const UWorld* World = GWorld;
		if (UGameplayPreloadSubsystem::IsPreparing(World))
		{
			const int32 First = GEngine->DeferredCommands.IndexOfByPredicate([](const FString& Command)
			{
				return Command.StartsWith(TEXT("CodexTactics."), ESearchCase::IgnoreCase) && !IsExempt(Command);
			});
			if (First != INDEX_NONE)
			{
				if (Held.IsEmpty())
				{
					HeldSince = FPlatformTime::Seconds();
				}
				for (int32 Index = First; Index < GEngine->DeferredCommands.Num(); ++Index)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: '%s' waits for the world preload"), *GEngine->DeferredCommands[Index]);
					Held.Add(GEngine->DeferredCommands[Index]);
				}
				GEngine->DeferredCommands.SetNum(First);
			}
			return;
		}
		if (!Held.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: world preload %s after %.2f s; running %d held command(s)"),
				*UGameplayPreloadSubsystem::StateName(UGameplayPreloadSubsystem::GetState(World)), FPlatformTime::Seconds() - HeldSince, Held.Num());
			GEngine->DeferredCommands.Insert(Held, 0);
			Held.Reset();
		}
	}

	/** Binds for the module's lifetime (removed again when the DLL unloads). */
	struct FRegistration
	{
		FDelegateHandle Handle;
		FRegistration() { Handle = FCoreDelegates::OnBeginFrame.AddStatic(&OnBeginFrame); }
		~FRegistration() { FCoreDelegates::OnBeginFrame.Remove(Handle); }
	};
	static FRegistration Registration;
}
#endif // !UE_BUILD_SHIPPING
