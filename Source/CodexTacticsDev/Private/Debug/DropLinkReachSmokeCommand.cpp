// Dev-only headless sweep of a map's one-way drops (user bug 2026-11-03, the roof-hole soft-lock on L_Gen_Depot_1; docs/systems/
// stances-and-movement.md "Falls and drops", "The soft-lock"): once the drop links are generated, every drop link (UDropLinkSubsystem) and every
// one-way link of a NavLinkProxy (the level generator's hole links) must land where the main navmesh is reachable (FDropLinkReach: the landing
// poly reaches the player start). Registered on the Lake (L_Demo01_A_FrozenLake, ~90 drop links); DropSoftlockSmoke runs the same sweep on
// L_Gen_Depot_1. PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands DropLinkReachSmoke -ExtraArgs "-Multiprocess"
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/DropReachSweep.h"
#include "Engine/World.h"
#include "Falls/DropLinkSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

namespace DropLinkReachSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		bool bDone = false;
	};

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World || State.bDone)
		{
			return false;
		}
		State.Time += StepSeconds;
		const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		const bool bReady = Links && Links->HasGenerated();
		if (!bReady && State.Time < 120.f)
		{
			return true; // the navmesh / the links are not there yet
		}
		const DropReachSweep::FResult Result = DropReachSweep::Run(*World);
		const bool bOk = bReady && Result.bBuilt && Result.Bad == 0 && Result.DropLinks + Result.HoleLinks > 0;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s%s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *Result.Line, bReady ? TEXT("") : TEXT(" (the drop links were never generated)"));
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("DropLinkReachSmoke"));
		return false;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DropLinkReachSmoke"),
		TEXT("Dev check (user bug 2026-11-03, the roof-hole soft-lock): every drop link and one-way hole link of this map lands where the main navmesh is ")
		TEXT("reachable. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
