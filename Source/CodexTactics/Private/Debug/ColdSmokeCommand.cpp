// Dev-only console command for headless cold checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ColdSmoke
// The commander stands in the open: cold must accumulate (Godot 0.88 %/s for fortitude 15). Then cold is forced
// to 75 % (Freezing tier -> speed x0.45) and to 100 % (frostbite collapse prone, weapon frozen, cannot shoot).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"

namespace ColdSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr int32 AccumulateSeconds = 5;

	struct FState
	{
		int32 Second = 0;
		float StartCold = 0.f;
		float NormalSpeed = 0.f;
		bool bAccumulateOk = false;
		bool bFreezingOk = false;
	};

	void Finish(bool bFrostbiteOk, const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke accumulate=%d freezing=%d frostbite=%d"),
			State.bAccumulateOk ? 1 : 0, State.bFreezingOk ? 1 : 0, bFrostbiteOk ? 1 : 0);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"),
			State.bAccumulateOk && State.bFreezingOk && bFrostbiteOk ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ColdSmoke"));
	}

	/** Returns true when the check finished. */
	bool Step(UWorld* World, FState& State)
	{
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		UColdSurvivalComponent* Cold = Leader ? Leader->ColdSurvival.Get() : nullptr;
		if (!Cold)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: leader or cold component missing"));
			FState Failed;
			Finish(false, Failed);
			return true;
		}
		++State.Second;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke t=%2ds cold=%5.2f tier=%d speedMul=%.2f maxSpeed=%.0f stance=%d frozen=%d nearHeat=%d"),
			State.Second, Leader->ColdLevel, static_cast<int32>(Cold->GetTier()), Leader->GetColdSpeedMultiplier(), Leader->GetMaxSpeed(),
			static_cast<int32>(Leader->GetStance()), Cold->IsWeaponFrozen() ? 1 : 0, Cold->IsNearHeatSource() ? 1 : 0);

		if (State.Second == AccumulateSeconds)
		{
			// Commander in the open: ~0.88 %/s (zone multipliers may scale it; it must only grow).
			const float Gained = Leader->ColdLevel - State.StartCold;
			State.bAccumulateOk = !Cold->IsNearHeatSource() && Gained > 0.5f * 0.88f * (AccumulateSeconds - 1);
			State.NormalSpeed = Leader->GetMaxSpeed();
			Leader->ColdLevel = 75.f;
		}
		else if (State.Second == AccumulateSeconds + 1)
		{
			State.bFreezingOk = Cold->GetTier() == EColdTier::Freezing
				&& FMath::IsNearlyEqual(Leader->GetColdSpeedMultiplier(), 0.45f)
				&& FMath::IsNearlyEqual(Leader->GetMaxSpeed(), State.NormalSpeed * 0.45f, 1.f)
				&& !Cold->IsWeaponFrozen();
			Leader->ColdLevel = 100.f;
		}
		else if (State.Second == AccumulateSeconds + 2)
		{
			const bool bFrostbiteOk = Cold->GetTier() == EColdTier::Frostbite && Cold->IsFrostbitten()
				&& Leader->GetStance() == EOperativeStance::Prone && Cold->IsWeaponFrozen() && !Leader->CanShoot();
			Finish(bFrostbiteOk, State);
			return true;
		}
		return false;
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: no squad leader"));
			FPlatformMisc::RequestExit(false, TEXT("ColdSmoke"));
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->StartCold = Leader->ColdLevel;

		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), 1.f, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ColdSmoke"),
		TEXT("Dev check: cold accumulation, Freezing speed tier, frostbite collapse and frozen weapon; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
