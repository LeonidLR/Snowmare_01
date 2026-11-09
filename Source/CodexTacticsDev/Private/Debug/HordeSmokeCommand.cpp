// Dev-only headless check of the horde's spawn and rush (UHordeSubsystem) on L_MovementTest (not saved):
//   Scripts/smoke.ps1 -Command "CodexTactics.HordeSmoke 3" -Log Smoke-Horde.log
// 2026-11-09 (game designer: only the firefight streak summons the horde - the 240 s real-time-fight clock is gone): the horde comes through
// the firefight path (UHordeSubsystem::RequestFirefightHorde, what UFirefightSubsystem calls at the level's threshold) with the warning lead
// shortened to the argument (default 3 s) through Codex.Horde.FirefightWarningSeconds. A wave fight with one frozen far-off keeper (so the
// fight never clears) and an immortal squad holding fire:
//   no horde comes by itself however long the fight runs (no Codex.Horde.TriggerSeconds any more); the request posts the HQ warning, the
//   countdown runs in real time and stops in the tactical pause; at its end 8 enemies (5 frost hounds, 3 frostbitten) appear 25-45 m from
//   the squad's centre, clustered, as fight members that know where the squad is; the HUD warning is up; they close in on the squad; no
//   second horde without a second request.
// 2026-11-02: the world's proximity mines are removed first - the smoke measures the horde's arrival and rush, and mines placed on the
// user's L_MovementTest killed horde members on the way (a flaky "they joined the wave" / "rushes the squad").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Combat/HordeSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace HordeSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		float LeadSeconds = 3.f;
		TWeakObjectPtr<AEnemyCharacter> Keeper;
		float LeftBeforePause = 0.f;
		float MeanDistanceAtSpawn = 0.f;
		int32 FeedMark = 0;
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
		SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), -1.f);
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

	/** Feed lines posted since the mark, joined. */
	FString FeedSince(UWorld* World, int32 Mark)
	{
		FString Out;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (int32 Index = FMath::Max(Mark, 0); Index < Messages->GetHistory().Num(); ++Index)
			{
				Out += Messages->GetHistory()[Index].Text.ToString() + TEXT(" | ");
			}
		}
		return Out;
	}

	int32 FeedCount(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages ? Messages->GetHistory().Num() : 0;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 90.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>();
		UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Flow || !Squad || !Horde || !Intro || !Waves)
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
			SetCVar(TEXT("Codex.Horde.FirefightWarningSeconds"), State.LeadSeconds);
			SetCVar(TEXT("Codex.Horde.Enabled"), 1.f);
			SmokeUtils::StartWaveFight(Flow); // sudden horde start, its free pause released
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			int32 MinesRemoved = 0;
			for (TActorIterator<AProximityMineActor> It(World); It; ++It)
			{
				It->Destroy();
				++MinesRemoved;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %d proximity mine(s) removed (they must not thin out the horde)"), MinesRemoved);
			// A keeper far behind, frozen and tough: the fight stays on (the horde joins the same fight).
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.Keeper = Waves->SpawnEnemy(EEnemyArchetype::Brute, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 6000.f + FVector(0.f, 0.f, 50.f));
			if (AEnemyCharacter* Keeper = State.Keeper.Get())
			{
				Keeper->CustomTimeDilation = 0.f;
				Keeper->GetHealthComponent()->SetMaxHealth(1000000.f);
			}
			Horde->RefreshConfig();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("real-time wave fight"));
			Check(State, Horde->GetConfig().bEnabled && FMath::IsNearlyEqual(Horde->GetConfig().Firefight.WarningLeadSeconds, State.LeadSeconds),
				FString::Printf(TEXT("horde on, warning lead %.1f s (10 s outside the smoke)"), Horde->GetConfig().Firefight.WarningLeadSeconds));
			Check(State, IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Horde.TriggerSeconds")) == nullptr
				&& IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Horde.RepeatSeconds")) == nullptr,
				TEXT("no real-time-fight clock any more (Codex.Horde.TriggerSeconds / RepeatSeconds gone, game designer 2026-11-09)"));
			Next(State);
			return true;
		}
		case 1:
		{
			// The fight runs in real time: no horde comes by itself.
			if (State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Horde->GetHordesReleased() == 0 && !Horde->IsFirefightHordePending() && Horde->GetLastHorde().IsEmpty(),
				FString::Printf(TEXT("%.0f s of real-time fight: no horde by itself (only the firefight streak summons it)"), State.StageTime));
			// The firefight path: what UFirefightSubsystem calls at the level's threshold.
			State.FeedMark = FeedCount(World);
			Check(State, Horde->RequestFirefightHorde() && Horde->IsFirefightHordePending(), TEXT("the firefight horde requested"));
			Check(State, FeedSince(World, State.FeedMark).Contains(TEXT("HORDE is closing")), TEXT("... the HQ warning at once"));
			Check(State, !Horde->RequestFirefightHorde(), TEXT("... a second request while one is on its way: refused"));
			Next(State);
			return true;
		}
		case 2:
			// The countdown runs in real time ...
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Horde->IsCountdownRunning() && Horde->GetFirefightHordeSecondsLeft() < State.LeadSeconds - 0.5f && Horde->GetHordesReleased() == 0,
				FString::Printf(TEXT("the warning counts down in real time (%.1f s left)"), Horde->GetFirefightHordeSecondsLeft()));
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
				TEXT("tactical pause on"));
			State.LeftBeforePause = Horde->GetFirefightHordeSecondsLeft();
			Next(State);
			return true;
		case 3:
			// ... the tactical pause holds it.
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Horde->GetFirefightHordeSecondsLeft(), State.LeftBeforePause, 0.05f) && !Horde->IsCountdownRunning(),
				FString::Printf(TEXT("the countdown stops in the tactical pause (%.2f -> %.2f s left)"), State.LeftBeforePause, Horde->GetFirefightHordeSecondsLeft()));
			Flow->ToggleTacticalPause();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("back in real time"));
			Next(State);
			return true;
		case 4:
		{
			if (Horde->GetHordesReleased() == 0 && State.StageTime < State.LeadSeconds + 4.f)
			{
				return true;
			}
			Check(State, Horde->GetHordesReleased() == 1 && Horde->GetFirefightHordesReleased() == 1 && !Horde->IsFirefightHordePending(),
				FString::Printf(TEXT("the horde came %.1f s after the pause"), State.StageTime));
			if (Intro->IsIntroPending())
			{
				Intro->DismissIntro(); // the horde intro (checked by HordeFirefightsSmoke) - here the rush is measured in real time
			}
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
			Check(State, Waves->GetAliveEnemyCount() >= 9, FString::Printf(TEXT("they joined the fight (%d alive)"), Waves->GetAliveEnemyCount()));
			FVector WarningAt;
			int32 WarningCount = 0;
			float WarningLeft = 0.f;
			Check(State, Horde->GetActiveWarning(WarningAt, WarningCount, WarningLeft) && WarningCount == Members.Num(), TEXT("HUD warning \"HORDE!\" up"));
			State.MeanDistanceAtSpawn = MeanDistanceToSquad(*Horde, SquadCentre(*Squad));
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 6.f)
			{
				return true;
			}
			const float Mean = MeanDistanceToSquad(*Horde, SquadCentre(*Squad));
			Check(State, Mean < State.MeanDistanceAtSpawn - 300.f,
				FString::Printf(TEXT("the horde rushes the squad (mean distance %.1f -> %.1f m)"), State.MeanDistanceAtSpawn / 100.f, Mean / 100.f));
			Check(State, Horde->GetHordesReleased() == 1 && !Horde->IsFirefightHordePending(),
				FString::Printf(TEXT("no second horde without a second request (%d released)"), Horde->GetHordesReleased()));
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
			State->LeadSeconds = FCString::Atof(*Args[0]);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeSmoke"),
		TEXT("Dev check of the horde's spawn and rush through the firefight path (arg: warning lead seconds, default 3); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
