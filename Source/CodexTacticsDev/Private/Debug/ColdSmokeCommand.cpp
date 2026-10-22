// Dev-only console command for headless cold checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.ColdSmoke
// The commander stands in the open: cold must accumulate (Godot 0.88 %/s for fortitude 15). Then cold is forced
// to 75 % (Freezing tier -> speed x0.45) and to 100 % (frostbite collapse prone, weapon frozen, cannot shoot).
// Before that (user decision 2026-10-01, not in Godot): a sprint at 40 % cold warms him up; at 65 % he cannot sprint.
// Environment phase 2 (first, instant): Blizzard at night must raise his open-air cold rate over Clear at midday (the multipliers come
// from UEnvironmentSubsystem through GatherEnvironment); the environment state is restored afterwards.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/FrostVignetteWidget.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Data/EnvironmentTuning.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
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
		bool bSprintOk = false;
		float SprintStartCold = 0.f;
		bool bFreezingOk = false;
		bool bVignetteHiddenWhenWarm = false;
		bool bEnvironmentOk = false;
	};

	/** 1 = the HUD frost vignette is up, 0 = hidden, -1 = no HUD. */
	int32 VignetteShown(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		if (!Hud || !Hud->GetFrostVignette())
		{
			return -1;
		}
		Hud->UpdateFrostVignette(); // DrawHUD does not run headless
		return Hud->GetFrostVignette()->IsShown() ? 1 : 0;
	}

	/**
	 * Environment phase 2: the leader's open-air cold rate (1 s from 0 %, standing, no heat / preparation / sprint) at Clear midday
	 * and at Blizzard night, read through GatherEnvironment; the night storm must be clearly colder and the multipliers must match the
	 * tuning. Restores the environment state.
	 */
	bool CheckEnvironmentCold(UWorld* World, UColdSurvivalComponent* Cold)
	{
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		if (!Environment || !Environment->IsInitialised())
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke environment cold: no initialised UEnvironmentSubsystem"));
			return false;
		}
		if (Environment->IsLocked() && !EnvironmentTuning::Get().bColdOnLockedLevels)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke environment cold: the level is locked (cold multipliers off); run on L_MovementTest"));
			return false;
		}
		const FEnvironmentState Saved = Environment->GetState();
		auto Sample = [Cold](float& OutWeather, float& OutPhase)
		{
			FColdEnvironment Open = Cold->GatherEnvironment();
			Open.bWarm = false;
			Open.bPreparation = false;
			Open.bSprinting = false;
			OutWeather = Open.WeatherMultiplier;
			OutPhase = Open.PhaseMultiplier;
			return ColdRules::StepCold(Cold->Config, 0.f, 1.f, Open, EOperativeStance::Standing, Cold->Fortitude);
		};

		float ClearWeather = 0.f, ClearPhase = 0.f, StormWeather = 0.f, StormPhase = 0.f;
		Environment->SetHour(12.f);
		Environment->ForceWeather(EWeatherState::Clear, true);
		const float ClearMidday = Sample(ClearWeather, ClearPhase);
		Environment->SetHour(23.f);
		Environment->ForceWeather(EWeatherState::Blizzard, true);
		const float BlizzardNight = Sample(StormWeather, StormPhase);
		Environment->SetState(Saved);

		const FEnvironmentConfig& Tuning = EnvironmentTuning::Get();
		const bool bMultipliersOk = FMath::IsNearlyEqual(ClearWeather, EnvironmentRules::GetColdWeatherMultiplier(Tuning, EWeatherState::Clear), 0.001f)
			&& FMath::IsNearlyEqual(ClearPhase, EnvironmentRules::GetColdPhaseMultiplier(Tuning, ETimeOfDayPhase::Midday), 0.001f)
			&& FMath::IsNearlyEqual(StormWeather, EnvironmentRules::GetColdWeatherMultiplier(Tuning, EWeatherState::Blizzard), 0.001f)
			&& FMath::IsNearlyEqual(StormPhase, EnvironmentRules::GetColdPhaseMultiplier(Tuning, ETimeOfDayPhase::Night), 0.001f);
		const bool bRises = ClearMidday > 0.f && BlizzardNight > ClearMidday * 1.5f;
		UE_LOG(LogCodexTactics, Display,
			TEXT("Smoke environment cold: Clear midday %.3f %%/s (weather %.2f phase %.2f), Blizzard night %.3f %%/s (weather %.2f phase %.2f), x%.2f"),
			ClearMidday, ClearWeather, ClearPhase, BlizzardNight, StormWeather, StormPhase, ClearMidday > 0.f ? BlizzardNight / ClearMidday : 0.f);
		return bMultipliersOk && bRises;
	}

	void Finish(bool bFrostbiteOk, const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke accumulate=%d sprint=%d freezing=%d frostbite=%d environment=%d"),
			State.bAccumulateOk ? 1 : 0, State.bSprintOk ? 1 : 0, State.bFreezingOk ? 1 : 0, bFrostbiteOk ? 1 : 0, State.bEnvironmentOk ? 1 : 0);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"),
			State.bAccumulateOk && State.bSprintOk && State.bFreezingOk && bFrostbiteOk && State.bEnvironmentOk ? TEXT("PASS") : TEXT("FAIL"));
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
			State.bVignetteHiddenWhenWarm = VignetteShown(World) == 0;
			// Sprint warm-up: 40 % cold, a sprint order 12 m ahead.
			Leader->ColdLevel = 40.f;
			State.SprintStartCold = 40.f;
			Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1200.f, true);
		}
		else if (State.Second == AccumulateSeconds + 3)
		{
			// The 12 m run takes ~1.7 s: by now he may have arrived; the cold must have gone down instead of up.
			const bool bWarmed = Leader->ColdLevel < State.SprintStartCold - 1.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke sprint warm-up: sprinting %d, cold %.2f -> %.2f"),
				Leader->IsSprinting() ? 1 : 0, State.SprintStartCold, Leader->ColdLevel);
			Leader->StopOperative();
			Leader->ColdLevel = 65.f; // above max_cold_to_sprint (60)
			Leader->OrderMoveTo(Leader->GetActorLocation() - Leader->GetActorForwardVector() * 600.f, true);
			State.bSprintOk = bWarmed;
		}
		else if (State.Second == AccumulateSeconds + 4)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke chilled sprint order: sprinting %d (cold %.2f)"), Leader->IsSprinting() ? 1 : 0, Leader->ColdLevel);
			State.bSprintOk &= !Leader->IsSprinting() && Leader->ColdLevel > 65.f;
			Leader->StopOperative();
			Leader->ColdLevel = 75.f;
		}
		else if (State.Second == AccumulateSeconds + 5)
		{
			State.bFreezingOk = Cold->GetTier() == EColdTier::Freezing
				&& FMath::IsNearlyEqual(Leader->GetColdSpeedMultiplier(), 0.45f)
				&& FMath::IsNearlyEqual(Leader->GetMaxSpeed(), State.NormalSpeed * 0.45f, 1.f)
				&& !Cold->IsWeaponFrozen();
			Leader->ColdLevel = 100.f;
		}
		else if (State.Second == AccumulateSeconds + 6)
		{
			// Godot UI/FrostOverlay: the frost vignette follows the coldest operative (hidden up to 35 %).
			const bool bVignetteOk = State.bVignetteHiddenWhenWarm && VignetteShown(World) == 1;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke frost vignette: hidden when warm %d, shown at 100 %% %d"),
				State.bVignetteHiddenWhenWarm ? 1 : 0, VignetteShown(World));
			const bool bFrostbiteOk = Cold->GetTier() == EColdTier::Frostbite && Cold->IsFrostbitten()
				&& Leader->GetStance() == EOperativeStance::Prone && Cold->IsWeaponFrozen() && !Leader->CanShoot() && bVignetteOk;
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
		if (UColdSurvivalComponent* Cold = Leader->ColdSurvival.Get())
		{
			State->bEnvironmentOk = CheckEnvironmentCold(World, Cold);
		}
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
