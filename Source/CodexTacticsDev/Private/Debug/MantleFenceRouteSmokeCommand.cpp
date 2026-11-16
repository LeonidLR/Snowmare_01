// Dev-only console command: a Solo move order across a low (135 cm) fence takes the mantle over it on a generated map (designer report
// 2026-11-16, batch 2 item 9: "on the generated maps operatives never vault or mantle - at a low fence they run around it";
// docs/port/MANTLING_BATCH2_REPORT.md):
//   Scripts/smoke_parallel.ps1 -Commands MantleFenceRouteSmoke -ExtraArgs -Multiprocess        (L_Gen_Depot_1)
//   Scripts/smoke.ps1 -Command CodexTactics.MantleFenceRouteSmoke -Map /Game/Maps/Generated/<map>  (any generated map)
// Checks: the map got its mantle links (none dropped by the links.max_total cap); up to MaxFences 135 cm fences whose way round is longer than
// the mantle area's entering cost by links.entering_cost_cm: the fence carries a link; a ground click beyond it (the controller's
// HandleWorldHit) makes the Solo leader mantle over it (high_fence_over) and arrive within the time limit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/MantleSmokeSites.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace MantleFenceRouteSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** He stands this far off the fence's face; the goal lies this far past its far face. */
	constexpr float StandOffCm = 150.f;
	constexpr float GoalOffCm = 200.f;
	/** Fences tried (one that qualifies is enough). */
	constexpr int32 MaxFences = 6;
	/** A fence order is over after this long, s. */
	constexpr float ArriveLimitSeconds = 20.f;
	constexpr float ArriveRadiusCm = 120.f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		SmokeUtils::FPreloadGate PreloadGate;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Tried = 0;
		int32 Passed = 0;
		int32 MantlesBefore = 0;
		bool bMantledHere = false;
		MantleSmokeSites::FSite Site;
		FVector Goal = FVector::ZeroVector;
		TSet<const AActor*> Used;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MantleFenceRouteSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	FVector FeetOf(const AOperativeCharacter& Leader)
	{
		return Leader.GetActorLocation() - FVector(0.f, 0.f, Leader.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->PreloadGate.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return true;
		case SmokeUtils::EPreloadGate::Failed:
			Check(*State, false, State->PreloadGate.FailReason);
			return Finish(*State, false);
		default:
			break;
		}
		State->Time += StepSeconds;
		State->StageTime += StepSeconds;
		if (State->Time > 500.f)
		{
			Check(*State, false, FString::Printf(TEXT("time out in stage %d"), State->Stage));
			return Finish(*State, false);
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation > 0.f)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = State->Leader.Get();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FMantleConfig& Config = MantleRules::GetConfig();
		switch (State->Stage)
		{
		case 0:
		{
			if (!Squad || !Squad->GetLeader() || !Mantle || !PC || !Nav || State->StageTime < 3.f || (!Mantle->HasGenerated() && State->StageTime < 240.f))
			{
				return true;
			}
			State->Leader = Squad->GetLeader();
			Squad->EnterSoloMode();
			Squad->SetFollowersHolding(true);
			State->Leader->bTacticalCeaseFire = true;
			int32 FenceLinks = 0;
			for (const FMantleLink& Link : Mantle->GetLinks())
			{
				FenceLinks += Link.ClassId == FName(TEXT("high_fence_over")) ? 1 : 0;
			}
			Check(*State, Mantle->HasGenerated() && !Mantle->WasLinkCapReached() && FenceLinks > 0, FString::Printf(
				TEXT("the map's mantle links: %d (%d over fences), the cap (links.max_total %.0f) %s"), Mantle->GetLinks().Num(), FenceLinks, Config.LinkMaxTotal,
				Mantle->WasLinkCapReached() ? TEXT("REACHED - links dropped") : TEXT("not reached")));
			Next(*State, 1);
			return true;
		}
		case 1: // a 135 cm fence whose way round is long enough for the link to pay
		{
			if (State->Tried >= MaxFences || State->Passed > 0)
			{
				Check(*State, State->Passed > 0, FString::Printf(TEXT("a Solo ground click across a 135 cm fence mantles over it (%d of %d fences tried)"), State->Passed,
					State->Tried));
				return Finish(*State, State->Tried > 0);
			}
			State->Site = MantleSmokeSites::FindSite(World, Leader, TEXT("fence"), 135.f, StandOffCm, Leader->GetActorLocation(), State->Used, true, GoalOffCm);
			if (!State->Site.bFound)
			{
				Check(*State, State->Tried > 0, FString::Printf(TEXT("a 135 cm fence with free ground on both sides (%d tried)"), State->Tried));
				return Finish(*State, false);
			}
			State->Used.Add(State->Site.Actor.Get());
			const FMantleSite& Probe = State->Site.Probe;
			State->Goal = Probe.FacePoint + Probe.Dir * (Probe.Probe.DepthCm + GoalOffCm);
			State->Goal.Z = State->Site.Feet.Z;
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(State->Goal, OnNav, FVector(60.f, 60.f, 100.f)))
			{
				return true;
			}
			State->Goal = OnNav.Location;
			const UNavigationPath* Round = Nav->FindPathToLocationSynchronously(World, State->Site.Feet, State->Goal, Leader, UNavFilter_OperativeNoMantle::StaticClass());
			const float Direct = static_cast<float>(FVector::Dist2D(State->Site.Feet, State->Goal));
			const float RoundCm = Round && Round->IsValid() && !Round->IsPartial() ? static_cast<float>(Round->GetPathLength()) : TNumericLimits<float>::Max();
			int32 Links = 0;
			for (const FMantleLink& Link : Mantle->GetLinks())
			{
				Links += Link.Obstacle.Get() == State->Site.Actor.Get() && !Link.bDown ? 1 : 0;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[MantleFenceRouteSmoke] %s: %d links over it, the way round %.0f cm vs %.0f straight (entering cost %.0f)"),
				*GetNameSafe(State->Site.Actor.Get()), Links, RoundCm >= TNumericLimits<float>::Max() ? -1.f : RoundCm, Direct, Config.LinkEnteringCostCm);
			if (RoundCm < Direct + Config.LinkEnteringCostCm + 100.f)
			{
				return true; // the way round is short: the path rightly walks round - another fence
			}
			++State->Tried;
			Check(*State, Links > 0, FString::Printf(TEXT("%s (135 cm fence, way round %.0f cm): it carries mantle links (%d)"), *GetNameSafe(State->Site.Actor.Get()),
				RoundCm >= TNumericLimits<float>::Max() ? -1.f : RoundCm, Links));
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			Leader->TeleportTo(State->Site.Feet + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.f), State->Site.Dir.Rotation(), false, true);
			Next(*State, 2);
			return true;
		}
		case 2: // settled: the ground click beyond the fence
		{
			if (State->StageTime < 1.f)
			{
				return true;
			}
			State->MantlesBefore = Leader->GetMantleCount();
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, State->Goal + FVector(0.f, 0.f, 150.f), State->Goal - FVector(0.f, 0.f, 150.f), ECC_Visibility))
			{
				Next(*State, 1);
				return true;
			}
			PC->HandleWorldHit(Hit);
			Next(*State, 3);
			return true;
		}
		case 3:
		{
			const FVector Feet = FeetOf(*Leader);
			const bool bThere = FVector::Dist2D(Feet, State->Goal) <= ArriveRadiusCm && !Leader->IsMantling();
			if (!bThere && State->StageTime < ArriveLimitSeconds)
			{
				return true;
			}
			const bool bMantled = Leader->GetMantleCount() > State->MantlesBefore;
			Check(*State, bThere && bMantled, FString::Printf(TEXT("%s: the click beyond it - %s, %s in %.1f s (%.0f cm off the goal)"),
				*GetNameSafe(State->Site.Actor.Get()), bMantled ? *FString::Printf(TEXT("mantled over it (%s)"), *Leader->GetLastMantleClass().ToString()) : TEXT("NO mantle"),
				bThere ? TEXT("arrived") : TEXT("NOT arrived"), State->StageTime, FVector::Dist2D(Feet, State->Goal)));
			State->Passed += bThere && bMantled ? 1 : 0;
			Next(*State, 1);
			return true;
		}
		default:
			return Finish(*State, false);
		}
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
		TEXT("CodexTactics.MantleFenceRouteSmoke"),
		TEXT("Dev check (batch 2 item 9, any generated map): the map's mantle links are complete (no cap), a Solo ground click across a 135 cm fence whose way ")
		TEXT("round is long mantles over it and arrives; PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
