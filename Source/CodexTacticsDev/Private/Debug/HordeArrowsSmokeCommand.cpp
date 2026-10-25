// Dev-only check of the red horde-direction edge arrows (game designer 2026-10-25, UHordeArrowsWidget / HordeArrowRules) on
// L_Demo01_A_FrozenLake (wave 1 = frostbitten from NORTH_ICE + hounds from EAST_GULLY):
//   Scripts/smoke_parallel.ps1 -Commands CodexTactics.HordeArrowsSmoke
// 1. no wave: the HUD widget exists and reports no approach direction;
// 2. wave 1 spawned (the squad 30 m from the spawn points): the widget reports one approach direction per lane (two), each bearing
//    within 40 degrees of the bearing from the leader to that lane's spawn points, counts add up to the wave, the arrows drawn
//    (off-screen groups) never outnumber the groups and carry a distance label;
// 3. the north group dies: one direction left, the east one; 4. everything dead: none.
// Argument "shot" (needs a rendered run, not -nullrhi): saves the HUD with the arrows to Saved/Screenshots/HordeArrows/horde_arrows.png
// after step 2.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/HordeArrowRules.h"
#include "UI/HordeArrowsWidget.h"
#include "UnrealClient.h"

namespace HordeArrowsSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotRequested = false;
		int32 WaveEnemies = 0;
		float NorthBearing = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HordeArrowsSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Bearings (degrees) from Origin to the lane's spawn points. */
	TArray<float> LaneBearings(UWorld* World, const FVector& Origin, const TCHAR* Lane)
	{
		TArray<float> Bearings;
		for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
		{
			if (It->SpawnLane == Lane)
			{
				const FVector Delta = It->GetActorLocation() - Origin;
				Bearings.Add(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
			}
		}
		return Bearings;
	}

	bool NearAny(const TArray<float>& Bearings, float Bearing, float ToleranceDeg)
	{
		for (const float Other : Bearings)
		{
			if (FMath::Abs(HordeArrowRules::AngleDeltaDeg(Bearing, Other)) <= ToleranceDeg)
			{
				return true;
			}
		}
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Total += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 120.f)
		{
			if (World)
			{
				Check(State, false, FString::Printf(TEXT("finished in time (stage %d)"), State.Stage));
			}
			return World ? Finish(State, false) : false;
		}
		if (State.Total < 2.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		UHordeArrowsWidget* Arrows = Hud ? Hud->GetHordeArrows() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Waves || !Leader || !Arrows)
		{
			Check(State, false, FString::Printf(TEXT("world ready (flow %d, waves %d, leader %d, arrows widget %d)"), Flow != nullptr, Waves != nullptr, Leader != nullptr,
				Arrows != nullptr));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			Arrows->Refresh(true);
			Check(State, Arrows->GetApproaches().IsEmpty() && Arrows->GetArrowCount() == 0, TEXT("no wave: no red arrow"));
			SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true; // the placement settles
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			if (UHordeIntroSubsystem* Intro = World->GetSubsystem<UHordeIntroSubsystem>())
			{
				Intro->DismissIntro();
			}
			Flow->CancelHordeIntro();
			if (Flow->IsFreePause())
			{
				Flow->ToggleTacticalPause();
			}
			Next(State);
			return true;
		}
		case 2:
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat || Waves->GetAliveEnemyCount() < 6 || Waves->GetRemainingSpawnCount() > 0)
			{
				if (State.StageTime > 20.f)
				{
					Check(State, false, FString::Printf(TEXT("wave 1 out (phase %d, alive %d)"), static_cast<int32>(Flow->GetPhase()), Waves->GetAliveEnemyCount()));
					return Finish(State, false);
				}
				return true;
			}
			State.WaveEnemies = Waves->GetAliveEnemyCount();
			Arrows->Refresh(true);
			const TArray<UHordeArrowsWidget::FApproach>& Approaches = Arrows->GetApproaches();
			Check(State, Approaches.Num() == 2, FString::Printf(TEXT("two approach directions (NORTH_ICE, EAST_GULLY): %d"), Approaches.Num()));
			const FVector Origin = Leader->GetActorLocation();
			const TArray<float> North = LaneBearings(World, Origin, TEXT("NORTH_ICE"));
			const TArray<float> East = LaneBearings(World, Origin, TEXT("EAST_GULLY"));
			Check(State, !North.IsEmpty() && !East.IsEmpty(), FString::Printf(TEXT("the level has the lanes' spawn points (%d / %d)"), North.Num(), East.Num()));
			int32 Total = 0;
			bool bNorth = false;
			bool bEast = false;
			for (const UHordeArrowsWidget::FApproach& Approach : Approaches)
			{
				Total += Approach.Group.Count;
				UE_LOG(LogCodexTactics, Display, TEXT("[HordeArrows] group bearing %.0f, %d enemies, nearest %.0f m, arrow %d (%s)"), Approach.Group.BearingDeg, Approach.Group.Count,
					Approach.Group.NearestCm / 100.f, Approach.bArrow ? 1 : 0, *Approach.Distance.ToString());
				if (NearAny(North, Approach.Group.BearingDeg, 40.f) && !NearAny(East, Approach.Group.BearingDeg, 40.f))
				{
					bNorth = true;
					State.NorthBearing = Approach.Group.BearingDeg;
				}
				else if (NearAny(East, Approach.Group.BearingDeg, 40.f) && !NearAny(North, Approach.Group.BearingDeg, 40.f))
				{
					bEast = true;
				}
				Check(State, !Approach.bArrow || (!Approach.Distance.IsEmpty() && Approach.Distance.ToString().EndsWith(TEXT(" m"))), TEXT("a drawn arrow carries its distance"));
			}
			Check(State, bNorth && bEast, TEXT("one group towards each lane (bearings within 40 deg of the lane's spawn points)"));
			Check(State, Total == State.WaveEnemies, FString::Printf(TEXT("the groups hold the whole wave (%d / %d)"), Total, State.WaveEnemies));
			Check(State, Arrows->GetArrowCount() <= Approaches.Num() && Arrows->GetArrowCount() >= 1, FString::Printf(TEXT("red arrows drawn: %d"), Arrows->GetArrowCount()));
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.bShot)
			{
				if (!State.bShotRequested)
				{
					const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/HordeArrows"));
					FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Dir);
					FScreenshotRequest::RequestScreenshot(Dir / TEXT("horde_arrows.png"), /*bShowUI*/ true, false);
					State.bShotRequested = true;
					UE_LOG(LogCodexTactics, Display, TEXT("[HordeArrows] screenshot requested: %s"), *(Dir / TEXT("horde_arrows.png")));
				}
				if (State.StageTime < 2.f)
				{
					return true;
				}
			}
			// The north group dies.
			for (AEnemyCharacter* Enemy : Waves->GetAliveEnemies())
			{
				const FVector Delta = Enemy->GetActorLocation() - Leader->GetActorLocation();
				const float Bearing = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
				if (FMath::Abs(HordeArrowRules::AngleDeltaDeg(Bearing, State.NorthBearing)) < 40.f && Enemy->GetHealthComponent())
				{
					Enemy->GetHealthComponent()->ApplyDirectHealthLoss(1.0e6f, TEXT("HordeArrowsSmoke"));
				}
			}
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Arrows->Refresh(true);
			Check(State, Arrows->GetApproaches().Num() == 1, FString::Printf(TEXT("the north group is dead: one direction left (%d)"), Arrows->GetApproaches().Num()));
			Waves->ClearAllEnemies();
			Next(State);
			return true;
		}
		case 5:
		{
			Arrows->Refresh(true);
			Check(State, Arrows->GetApproaches().IsEmpty() && Arrows->GetArrowCount() == 0, TEXT("everything dead: no red arrow"));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HordeArrowsSmoke"),
		TEXT("Dev check of the red horde-direction edge arrows (lake wave 1: two lanes, two arrows; dead = none); 'shot' saves a screenshot; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
