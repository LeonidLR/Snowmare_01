// Dev-only console command for a headless wave-victory check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.VictorySmoke
// Godot main.gd register_enemy_kill / _on_wave_cleared / _on_next_wave_pressed / _start_post_combat_sequence /
// _auto_recover_all_deployables: kill statistics, the victory panel between waves, the next-wave button, and after the
// last wave the squad back at its preparation spots, the commander leading and the deployables back in the supply.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/TurretActor.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/VictoryPanelWidget.h"

namespace VictorySmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Wave = 0;
		int32 TurretsBefore = 0;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Stations;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("VictorySmoke"));
		return false;
	}

	/** Kills every enemy; the first three by the commander, a turret and a mine. */
	void KillAll(UWorld* World)
	{
		const TCHAR* Sources[] = { TEXT("Командир"), TEXT("Турель"), TEXT("Мина") };
		int32 Index = 0;
		TArray<AEnemyCharacter*> Enemies;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (!It->IsDying())
			{
				Enemies.Add(*It);
			}
		}
		for (AEnemyCharacter* Enemy : Enemies)
		{
			FDamageSpec Spec;
			Spec.Amount = 100000.f;
			Spec.AttackerSource = Index < 3 ? Sources[Index] : TEXT("Командир");
			Enemy->GetHealthComponent()->TakeDamage(Spec);
			++Index;
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		UVictoryPanelWidget* Panel = Hud ? Hud->GetVictoryPanel() : nullptr;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Panel || !Flow || !Victory || !Squad || State.Time > 60.f)
		{
			Check(State, false, FString::Printf(TEXT("panel / subsystems / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		const int32 Total = Flow->GetConfig().TotalWaves;
		switch (State.Stage)
		{
		case 0:
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.Stations.Add(Member, Member->GetActorLocation());
			}
			Check(State, Victory->GetPrepStation(Squad->GetMembers()[1]).Equals(Squad->GetMembers()[1]->GetActorLocation(), 1.f),
				TEXT("preparation spot recorded when the cutscene ends"));
			Next();
			return true;
		case 1:
			// Each wave: start, kill everything, the panel appears; the button goes on.
			if (Flow->GetPhase() == ECodexGamePhase::Preparation)
			{
				Flow->FinishPreparation();
				State.Wave = Flow->GetWaveIndex();
				KillAll(World);
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				KillAll(World); // late spawns
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				if (State.StageTime < 0.3f)
				{
					return true;
				}
				const FString Text = Panel->GetShownText();
				Check(State, Panel->IsShown(), FString::Printf(TEXT("wave %d: panel shown"), State.Wave));
				if (State.Wave == 1)
				{
					const FSquadKillStats& Stats = Victory->GetKillStats();
					Check(State, Text.Contains(TEXT("ВОЛНА 1 ОТРАЖЕНА")) && Text.Contains(FString::Printf(TEXT("Запустить следующую волну (2/%d)"), Total)),
						TEXT("title and «next wave (2/N)»"));
					Check(State, Stats.Commander.TurretKills == 1 && Stats.Medic.MineKills == 1 && Stats.GetTotal() >= 3
						&& Stats.Commander.Total == Stats.GetTotal() - 1,
						FString::Printf(TEXT("kill stats: total %d, turret %d, mine %d"), Stats.GetTotal(), Stats.Commander.TurretKills, Stats.Medic.MineKills));
					Check(State, Text.Contains(FString::Printf(TEXT("ВСЕГО УНИЧТОЖЕНО: %d"), Stats.GetTotal())), TEXT("stats card text"));
				}
				if (State.Wave >= Total)
				{
					Check(State, Text.Contains(TEXT("ПОЛНАЯ ПОБЕДА")) && Text.Contains(TEXT("Завершить бой")), TEXT("last wave: full victory"));
					// Scatter the squad and leave a turret: the post-combat sequence brings them back.
					for (AOperativeCharacter* Member : Squad->GetMembers())
					{
						Member->TeleportTo(Member->GetActorLocation() + FVector(300.f, 0.f, 0.f), Member->GetActorRotation(), false, true);
					}
					Squad->SetLeader(Squad->GetMembers()[2]);
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					World->SpawnActor<ATurretActor>(SmokeUtils::LevelPoint(World, FVector(1500.f, -1500.f, 60.f)), FRotator::ZeroRotator, Params);
					for (const AOperativeCharacter* Member : Squad->GetMembers())
					{
						State.TurretsBefore += Member->TurretsCount;
					}
					Panel->PressNext();
					Next();
					return true;
				}
				Panel->PressNext();
				Check(State, Flow->GetPhase() == ECodexGamePhase::Preparation && Flow->GetWaveIndex() == State.Wave + 1 && !Panel->IsShown(),
					FString::Printf(TEXT("next wave: rest before wave %d, panel hidden"), Flow->GetWaveIndex()));
				return true;
			}
			return true;
		case 2:
		{
			Check(State, Flow->GetPhase() == ECodexGamePhase::PostCombat || Flow->GetPhase() == ECodexGamePhase::Exploration,
				TEXT("after the last wave: post-combat"));
			bool bBack = true;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector* Station = State.Stations.Find(const_cast<AOperativeCharacter*>(Member));
				bBack &= Station && FVector::Dist2D(*Station, Member->GetActorLocation()) < 50.f;
			}
			Check(State, bBack, TEXT("squad back at the preparation spots"));
			Check(State, Squad->GetLeader() && Squad->GetLeader()->SquadRole == EOperativeRole::Commander, TEXT("the commander leads"));
			int32 Enemies = 0;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				Enemies += It->IsActorBeingDestroyed() ? 0 : 1;
			}
			int32 Turrets = 0;
			for (TActorIterator<ATurretActor> It(World); It; ++It)
			{
				Turrets += It->IsActorBeingDestroyed() || !It->bDeployable ? 0 : 1;
			}
			int32 TurretsAfter = 0;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				TurretsAfter += Member->TurretsCount;
			}
			Check(State, Enemies == 0, TEXT("no enemies left"));
			Check(State, Turrets == 0 && TurretsAfter > State.TurretsBefore,
				FString::Printf(TEXT("turrets recovered into the supply (%d -> %d)"), State.TurretsBefore, TurretsAfter));
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VictorySmoke"),
		TEXT("Dev check: kill stats, victory panel, next wave, post-combat return and recovery; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
