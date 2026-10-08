// Dev-only headless check of the horde after a long real-time fight (user request 2026-10-06, UHordeSubsystem) on
// L_MovementTest (not saved):
//   Scripts/smoke.ps1 -Command "CodexTactics.HordeSmoke 4" -Log Smoke-Horde.log
// The trigger is shortened to the argument (default 4 s) through Codex.Horde.TriggerSeconds. A wave fight with one frozen
// far-off keeper (so the wave never clears) and an immortal squad holding fire:
//   the clock runs in real time, stops in the tactical pause; at the trigger 8 enemies (5 frost hounds, 3 frostbitten)
//   appear 25-45 m from the squad's centre, clustered, as wave members that know where the squad is; the HUD warning is
//   up; they close in on the squad; no second horde in the same fight (once per fight by default).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace HordeSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		float TriggerSeconds = 4.f;
		TWeakObjectPtr<AEnemyCharacter> Keeper;
		float CountedBeforePause = 0.f;
		float MeanDistanceAtSpawn = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		SetCVar(TEXT("Codex.Horde.TriggerSeconds"), -1.f);
		SetCVar(TEXT("Codex.Horde.Enabled"), -1.f);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HordeSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	float MeanDistanceToSquad(const UHordeSubsystem& Horde, const FVector& Centre)
	{
		float Sum = 0.f;
		int32 Count = 0;
		for (const TWeakObjectPtr<AEnemyCharacter>& Member : Horde.GetLastHorde())
		{
			if (const AEnemyCharacter* Enemy = Member.Get())
			{
				Sum += FVector::Dist2D(Enemy->GetActorLocation(), Centre);
				++Count;
			}
		}
		return Count > 0 ? Sum / Count : 0.f;
	}

	FVector SquadCentre(const USquadSubsystem& Squad)
	{
		FVector Sum = FVector::ZeroVector;
		const TArray<AOperativeCharacter*> Members = Squad.GetMembers();
		for (const AOperativeCharacter* Member : Members)
		{
			Sum += Member->GetActorLocation();
		}
		return Members.IsEmpty() ? Sum : Sum / Members.Num();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 60.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Flow || !Squad || !Horde || !Waves)
		{
			return State.Total < 5.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true; // the horde must live long enough to be measured
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// Before the fight starts: the subsystem reads the config when the wave fight begins.
			SetCVar(TEXT("Codex.Horde.TriggerSeconds"), State.TriggerSeconds);
			SetCVar(TEXT("Codex.Horde.Enabled"), 1.f);
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			// A keeper far behind, frozen and tough: the wave stays on (the horde is part of the same fight).
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.Keeper = Waves->SpawnEnemy(EEnemyArchetype::Brute, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 6000.f + FVector(0.f, 0.f, 50.f));
			if (AEnemyCharacter* Keeper = State.Keeper.Get())
			{
				Keeper->CustomTimeDilation = 0.f;
				Keeper->GetHealthComponent()->SetMaxHealth(1000000.f);
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("real-time wave fight"));
			Check(State, Horde->IsFightTracked() && Horde->GetConfig().bEnabled && FMath::IsNearlyEqual(Horde->GetConfig().TriggerSeconds, State.TriggerSeconds),
				FString::Printf(TEXT("horde clock started (trigger %.1f s, 240 s outside the smoke)"), Horde->GetConfig().TriggerSeconds));
			Next(State);
			return true;
		}
		case 1:
			// Real time counts ...
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Horde->GetCombatSeconds() > 0.9f && Horde->GetHordesReleased() == 0,
				FString::Printf(TEXT("the clock runs in real time (%.1f s counted)"), Horde->GetCombatSeconds()));
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
				TEXT("tactical pause on"));
			State.CountedBeforePause = Horde->GetCombatSeconds();
			Next(State);
			return true;
		case 2:
			// ... the tactical pause does not.
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Horde->GetCombatSeconds(), State.CountedBeforePause, 0.05f) && !Horde->IsCounting(),
				FString::Printf(TEXT("the clock stops in the tactical pause (%.2f -> %.2f s)"), State.CountedBeforePause, Horde->GetCombatSeconds()));
			Flow->ToggleTacticalPause();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("back in real time"));
			Next(State);
			return true;
		case 3:
		{
			if (Horde->GetHordesReleased() == 0 && State.StageTime < State.TriggerSeconds + 3.f)
			{
				return true;
			}
			Check(State, Horde->GetHordesReleased() == 1, FString::Printf(TEXT("the horde came at %.1f s of real-time fight"), Horde->GetCombatSeconds()));
			const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Horde->GetLastHorde();
			int32 Hounds = 0;
			int32 Frostbitten = 0;
			bool bAllHunters = Members.Num() > 0;
			bool bClustered = Members.Num() > 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Member : Members)
			{
				const AEnemyCharacter* Enemy = Member.Get();
				if (!Enemy)
				{
					bAllHunters = false;
					continue;
				}
				Hounds += Enemy->GetArchetype() == EEnemyArchetype::FrostHound ? 1 : 0;
				Frostbitten += Enemy->GetArchetype() == EEnemyArchetype::Frostbitten ? 1 : 0;
				bAllHunters &= Enemy->bKnowsSquadPosition && !Enemy->IsOnPatrol();
				bClustered &= FVector::Dist2D(Enemy->GetActorLocation(), Horde->GetLastSpawnPoint()) <= Horde->GetConfig().ClusterRadiusCm + 250.f;
			}
			Check(State, Members.Num() == 8 && Hounds == 5 && Frostbitten == 3,
				FString::Printf(TEXT("8 enemies: %d frost hounds, %d frostbitten"), Hounds, Frostbitten));
			const float Distance = FVector::Dist2D(Horde->GetLastSpawnPoint(), Horde->GetLastSquadCentre());
			Check(State, Distance >= 2500.f - 1.f && Distance <= 4500.f + 1.f,
				FString::Printf(TEXT("spawned %.1f m from the squad's centre (band 25-45 m, %s)"), Distance / 100.f,
					Horde->WasLastSpawnHidden() ? TEXT("out of sight") : TEXT("in sight: no hidden spot")));
			Check(State, bClustered, TEXT("clustered around the spawn point"));
			Check(State, bAllHunters, TEXT("they know where the squad is (no patrol / perception)"));
			Check(State, Waves->GetAliveEnemyCount() >= 9, FString::Printf(TEXT("they joined the wave (%d alive)"), Waves->GetAliveEnemyCount()));
			FVector WarningAt;
			int32 WarningCount = 0;
			float WarningLeft = 0.f;
			Check(State, Horde->GetActiveWarning(WarningAt, WarningCount, WarningLeft) && WarningCount == Members.Num(), TEXT("HUD warning \"HORDE!\" up"));
			State.MeanDistanceAtSpawn = MeanDistanceToSquad(*Horde, SquadCentre(*Squad));
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 6.f)
			{
				return true;
			}
			const float Mean = MeanDistanceToSquad(*Horde, SquadCentre(*Squad));
			Check(State, Mean < State.MeanDistanceAtSpawn - 300.f,
				FString::Printf(TEXT("the horde rushes the squad (mean distance %.1f -> %.1f m)"), State.MeanDistanceAtSpawn / 100.f, Mean / 100.f));
			Check(State, Horde->GetHordesReleased() == 1 && Horde->GetCombatSeconds() > State.TriggerSeconds + 3.f,
				FString::Printf(TEXT("once per fight: no second horde (%.1f s counted)"), Horde->GetCombatSeconds()));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		if (Args.Num() > 0 && FCString::Atof(*Args[0]) > 0.f)
		{
			State->TriggerSeconds = FCString::Atof(*Args[0]);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeSmoke"),
		TEXT("Dev check of the horde after a long real-time fight (arg: trigger seconds, default 4); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
