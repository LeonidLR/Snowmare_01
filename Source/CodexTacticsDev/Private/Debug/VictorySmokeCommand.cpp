// Dev-only console command for a headless wave-victory check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.VictorySmoke
// Godot main.gd register_enemy_kill / _on_wave_cleared / _on_next_wave_pressed / _start_post_combat_sequence /
// _auto_recover_all_deployables: kill statistics; between waves NO victory panel / next-wave button since 2026-10-25 (game
// designer: "the waves must break through on their own" - the rest starts by itself, the "wave cleared" feed line still comes; cut
// short here like the bot does, HordeIntroSmoke waits one out); after the last wave the squad stays where it fought, the commander
// leads and the deployables are back in the supply.

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
		/** Waves whose rest (no panel) was checked. */
		int32 RestsChecked = 0;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Stations;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> EndSpots;
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
		const TCHAR* Sources[] = { TEXT("Commander"), TEXT("Turret"), TEXT("Mine") };
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
			Spec.AttackerSource = Index < 3 ? Sources[Index] : TEXT("Commander");
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
		{
			// Godot _apply_stage_exploration_resources at the end of the cutscene («Начать игру»: what was collected,
			// gathered on the specialists, capped 2 / 4 / 5).
			int32 Totals[3] = { 0, 0, 0 };
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				// A collected supply spread over the squad: 3 turrets, 2 barricades, 3 mines.
				Member->TurretsCount = Member->SquadRole == EOperativeRole::Recruit ? 0 : 1;
				Member->BarricadesCount = Member->SquadRole == EOperativeRole::Engineer ? 2 : 0;
				Member->MinesCount = Member->SquadRole == EOperativeRole::MedicSapper ? 3 : 0;
				if (Member->SquadRole != EOperativeRole::Recruit)
				{
					Totals[0] += Member->TurretsCount;
					Totals[1] += Member->BarricadesCount;
					Totals[2] += Member->MinesCount;
				}
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					Check(State, Member->TurretsCount == FMath::Min(Totals[0], 2), FString::Printf(TEXT("loadout: commander turrets %d of %d"), Member->TurretsCount, Totals[0]));
				}
				else if (Member->SquadRole == EOperativeRole::Engineer)
				{
					Check(State, Member->BarricadesCount == FMath::Min(Totals[1], 4), FString::Printf(TEXT("loadout: engineer barricades %d of %d"), Member->BarricadesCount, Totals[1]));
				}
				else if (Member->SquadRole == EOperativeRole::MedicSapper)
				{
					Check(State, Member->MinesCount == FMath::Min(Totals[2], 5), FString::Printf(TEXT("loadout: medic mines %d of %d"), Member->MinesCount, Totals[2]));
				}
			}
		}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.Stations.Add(Member, Member->GetActorLocation());
			}
			Check(State, Victory->GetPrepStation(Squad->GetMembers()[1]).Equals(Squad->GetMembers()[1]->GetActorLocation(), 1.f),
				TEXT("preparation spot recorded when wave 1 spawns (sudden horde start)"));
			Next();
			return true;
		case 1:
		{
			// Each wave: start, kill everything, the panel appears; the button goes on. A horde wave comes suddenly in the
			// horde intro (user decisions 2026-10-11): dismissed here, the fight stays in real time.
			// The last wave (game designer 2026-10-22): no panel, no button - its end goes straight to post-combat, so the squad is
			// scattered and a turret left BEFORE its enemies die; the squad must STAY where the fight ended (no teleport back to the
			// preparation spots, the followers not walked to the formation).
			auto PrepareLastWave = [&State, World, Squad]()
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Member->TeleportTo(Member->GetActorLocation() + FVector(300.f, 0.f, 0.f), Member->GetActorRotation(), false, true);
				}
				State.EndSpots.Reset();
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					State.EndSpots.Add(Member, Member->GetActorLocation());
				}
				Squad->SetLeader(Squad->GetMembers()[2]);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				World->SpawnActor<ATurretActor>(SmokeUtils::LevelPoint(World, FVector(1500.f, -1500.f, 60.f)), FRotator::ZeroRotator, Params);
				State.TurretsBefore = 0;
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					State.TurretsBefore += Member->TurretsCount;
				}
			};
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat && (Flow->IsAwaitingHordeIntro() || Flow->IsFreePause()))
			{
				Check(State, Flow->GetPauseCharges() == Flow->GetConfig().TacticalPauseMaxCharges,
					FString::Printf(TEXT("wave %d: sudden start, charges %d/%d"), Flow->GetWaveIndex(),
						Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges));
				SmokeUtils::ReleaseHordeIntroPause(Flow);
				State.Wave = Flow->GetWaveIndex();
				if (State.Wave >= Total)
				{
					PrepareLastWave();
				}
				KillAll(World);
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::Preparation)
			{
				if (Flow->IsRestBetweenWaves())
				{
					// 2026-10-25: a cleared wave goes straight into the rest before the next one - no panel, no button.
					const int32 Cleared = Flow->GetWaveIndex() - 1;
					Check(State, !Panel->IsShown() && Flow->GetWaveIndex() == State.Wave + 1,
						FString::Printf(TEXT("wave %d cleared: the rest before wave %d, no victory panel / \"Start the next wave\""), Cleared,
							Flow->GetWaveIndex()));
					Check(State, Flow->GetPreparationTimeRemaining() > 0.f && Flow->GetPreparationTimeRemaining() <= Flow->GetConfig().WaveRestDuration,
						FString::Printf(TEXT("the rest counts down by itself (%.1f s left)"), Flow->GetPreparationTimeRemaining()));
					if (Cleared == 1)
					{
						// The statistics the panel used to show are still kept (saved, the bot's report).
						const FSquadKillStats& Stats = Victory->GetKillStats();
						Check(State, Stats.Commander.TurretKills == 1 && Stats.Medic.MineKills == 1 && Stats.GetTotal() >= 3
							&& Stats.Commander.Total == Stats.GetTotal() - 1,
							FString::Printf(TEXT("kill stats: total %d, turret %d, mine %d"), Stats.GetTotal(), Stats.Commander.TurretKills, Stats.Medic.MineKills));
					}
					++State.RestsChecked;
				}
				Flow->FinishPreparation();
				State.Wave = Flow->GetWaveIndex();
				if (State.Wave >= Total)
				{
					PrepareLastWave();
				}
				KillAll(World);
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::PostCombat && State.Wave >= Total)
			{
				Check(State, !Panel->IsShown(), TEXT("last wave: no victory panel, no \"End combat and continue exploring\" button - straight to post-combat"));
				// Nobody was teleported back to his preparation spot when the fight ended; from here on nobody moves.
				float MaxJump = 0.f;
				for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Spot : State.EndSpots)
				{
					MaxJump = FMath::Max(MaxJump, Spot.Key.IsValid() ? FVector::Dist2D(Spot.Value, Spot.Key->GetActorLocation()) : 99999.f);
				}
				Check(State, MaxJump < 200.f, FString::Printf(TEXT("the fight's end teleports nobody (max %.0f cm from where they stood)"), MaxJump));
				Check(State, State.RestsChecked == Total - 1, FString::Printf(TEXT("%d rests between %d waves, none with a panel"), State.RestsChecked, Total));
				State.EndSpots.Reset();
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					State.EndSpots.Add(Member, Member->GetActorLocation());
				}
				Next();
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				KillAll(World); // late spawns
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Check(State, false, FString::Printf(TEXT("wave %d stopped on the victory panel - since 2026-10-25 the next wave comes by itself"),
					State.Wave));
				return Finish(State, false);
			}
			return true;
		}
		case 2:
		{
			Check(State, Flow->GetPhase() == ECodexGamePhase::PostCombat || Flow->GetPhase() == ECodexGamePhase::Exploration,
				TEXT("after the last wave: post-combat"));
			if (State.StageTime < 3.f)
			{
				return true; // give the formation time to (wrongly) walk them
			}
			bool bStayed = true;
			float MaxMoved = 0.f;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector* Spot = State.EndSpots.Find(const_cast<AOperativeCharacter*>(Member));
				const float Moved = Spot ? FVector::Dist2D(*Spot, Member->GetActorLocation()) : 99999.f;
				MaxMoved = FMath::Max(MaxMoved, Moved);
				bStayed &= Moved < 50.f;
			}
			Check(State, bStayed, FString::Printf(TEXT("the squad stays where the fight ended, 3 s later (max %.0f cm moved; no teleport to the prep spots)"), MaxMoved));
			Check(State, Squad->IsPostFightHoldActive(), TEXT("the followers hold their spots until the leader moves"));
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
