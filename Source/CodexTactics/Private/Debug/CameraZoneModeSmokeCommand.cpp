// Dev-only console command for a headless camera-zone check across the combat modes on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CameraZoneModeSmoke
// User request 2026-10-08: the bunker zone camera works in REAL-TIME combat, steps aside in the TACTICAL PAUSE and in
// TURN-BASED combat (the normal / turn-based camera) and comes back with real time while the leader is still inside; the
// death cinematic takes priority over it and hands it back. Fight with the leader in the zone: RT (zone camera) -> pause
// (pawn) -> RT (zone) -> turn-based (pawn) -> RT (zone) -> a squad mate dies (pawn during the focus) -> zone again.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace CameraZoneModeSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ACameraZoneVolume> Zone;
		TWeakObjectPtr<AOperativeCharacter> Mate;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CameraZoneModeSmoke"));
		return false;
	}

	/** The player's view is the zone's fixed camera. */
	bool OnZoneCamera(UWorld* World, const ACameraZoneVolume* Zone)
	{
		const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC && Zone && Zone->TargetCamera && PC->GetViewTarget() == Zone->TargetCamera && Zone->IsZoneCameraShown();
	}

	bool OnPawn(UWorld* World)
	{
		const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC && PC->GetPawn() && PC->GetViewTarget() == PC->GetPawn();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>();
		ACameraZoneVolume* Zone = State.Zone.Get();
		AOperativeCharacter* Leader = Squad->GetLeader();
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto Wait = [&State](float Seconds) { return State.StageTime < Seconds; };
		switch (State.Stage)
		{
		case 0:
		{
			TActorIterator<ACameraZoneVolume> It(World);
			State.Zone = It ? *It : nullptr;
			Zone = State.Zone.Get();
			Check(State, Zone && Zone->TargetCamera && Leader, TEXT("bunker camera zone with its camera, leader present"));
			if (!Zone || !Zone->TargetCamera || !Leader)
			{
				return Finish(State, false);
			}
			Check(State, Zone->bActiveInRealTime && !Zone->bActiveInTurnBased && !Zone->bActiveInTacticalPause,
				TEXT("defaults: real time on, turn-based / tactical pause off"));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> Enemy(World); Enemy; ++Enemy)
			{
				Enemy->Destroy();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member != Leader && !State.Mate.IsValid())
				{
					State.Mate = Member;
				}
			}
			const FVector Inside = SmokeUtils::FreeSpot(World, Zone->GetActorLocation(), Leader);
			Leader->StopOperative();
			Leader->TeleportTo(Inside, Leader->GetActorRotation(), false, true);
			// One enemy far off keeps the wave alive (frozen), one at 9 m makes the turn-based fight.
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			if (AEnemyCharacter* Keeper = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Inside + FVector(3500.f, 0.f, 30.f)))
			{
				Keeper->CustomTimeDilation = 0.f;
			}
			Check(State, Zone->ContainsLocation(Leader->GetActorLocation()) && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("real-time fight, the leader inside the zone"));
			Next();
			return true;
		}
		case 1:
			if (Wait(0.6f))
			{
				return true;
			}
			Check(State, Zone->IsZoneActive() && OnZoneCamera(World, Zone), TEXT("real-time combat: zone camera shown"));
			Flow->ToggleTacticalPause();
			Next();
			return true;
		case 2:
			if (Wait(0.4f))
			{
				return true;
			}
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause && OnPawn(World) && Zone->IsZoneActive(),
				TEXT("tactical pause: back on the gameplay camera (zone still entered)"));
			Flow->ToggleTacticalPause();
			Next();
			return true;
		case 3:
			if (Wait(0.4f))
			{
				return true;
			}
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime && OnZoneCamera(World, Zone), TEXT("pause released: zone camera again"));
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 900.f + FVector(0.f, 0.f, 30.f)))
			{
				Hound->GetHealthComponent()->SetMaxHealth(5000.f, true);
			}
			Next();
			return true;
		case 4:
			if (Wait(0.3f))
			{
				return true;
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			Next();
			return true;
		case 5:
			if (Wait(0.5f))
			{
				return true;
			}
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TurnBased && OnPawn(World), TEXT("turn-based: the turn-based camera, not the zone camera"));
			Flow->ExitTurnBasedToRealTime();
			Next();
			return true;
		case 6:
			if (Wait(0.6f))
			{
				return true;
			}
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("back in real time"));
			Check(State, Zone->ContainsLocation(Leader->GetActorLocation()) ? OnZoneCamera(World, Zone) : OnPawn(World),
				FString::Printf(TEXT("real time re-evaluated: leader inside %d -> zone camera %d"), Zone->ContainsLocation(Leader->GetActorLocation()) ? 1 : 0,
					OnZoneCamera(World, Zone) ? 1 : 0));
			if (!Zone->ContainsLocation(Leader->GetActorLocation()))
			{
				Leader->TeleportTo(SmokeUtils::FreeSpot(World, Zone->GetActorLocation(), Leader), Leader->GetActorRotation(), false, true);
			}
			Next();
			return true;
		case 7:
		{
			if (Wait(0.5f))
			{
				return true;
			}
			Check(State, OnZoneCamera(World, Zone), TEXT("zone camera before the death"));
			AOperativeCharacter* Mate = State.Mate.Get();
			if (Mate)
			{
				Mate->TakeHit(100000.f, TEXT("Smoke"), false, true);
			}
			Next();
			return true;
		}
		case 8:
			if (Wait(0.3f))
			{
				return true;
			}
			Check(State, DeathCam->IsFocusActive() && OnPawn(World) && !Zone->IsZoneCameraShown(),
				TEXT("death cinematic takes priority: the zone camera steps aside"));
			Next();
			return true;
		case 9:
			if (DeathCam->IsActive())
			{
				return State.StageTime < 10.f ? true : (Check(State, false, TEXT("death cinematic never ended")), Finish(State, false));
			}
			Next();
			return true;
		case 10:
			if (Wait(0.5f))
			{
				return true;
			}
			Check(State, OnZoneCamera(World, Zone), TEXT("after the death cinematic: zone camera again (leader still inside)"));
			return Finish(State, true);
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CameraZoneModeSmoke"),
		TEXT("Dev check (2026-10-08): the bunker zone camera in real time, not in the tactical pause / turn-based, back in real "
			"time, death cinematic priority; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
