// Dev-only headless check of the blast gate tied to the generator during the wave fight (user request 2026-10-11) on
// L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GateGeneratorSmoke -Log Smoke-GateGenerator.log
// The quest chain is completed directly (canister, APC, generator, terminal): the terminal only unlocks the paced servos
// (HQ «grid is overloaded» line, no «swing open»). The wave fight starts suddenly (SmokeUtils::StartWaveFight); each wave
// is parked out of the way so nothing ends it early. The creep is sped up (Codex.Gate.ProgressPerSecond 0.05):
//  1. wave 1: the door creeps (progress > 0, below the wave's 30 % cap, collision closed, «OPENING SLOWLY»);
//  2. the generator breaks down: progress pauses (not reversed), «GENERATOR OFFLINE», HQ «generator is down»;
//  3. the engineer repairs it (ExecuteAction, 2.5 s): HQ «Power is back», the creep resumes;
//  4. each wave holds at its cap (30 / 60 / 90 %), never full, the leaves' collision and the navmesh block stay
//     (no complete path beyond the gate); the wave is killed, the victory panel's button starts the next one;
//  5. wave 3: the generator breaks again before the kill — the final wave cleared without power does NOT open the door
//     (HQ «last wave»); repaired: «the blast door is opening», the leaves slide open, collision off, a complete nav path
//     beyond the gate and the leader walks through.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Components/BoxComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/GateActor.h"
#include "Interactables/InteractableActor.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Quests/QuestSubsystem.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace GateGeneratorSmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr float WarmupSeconds = 3.f;
	constexpr float StageTimeout = 30.f;
	/** Sped-up creep for the check (the data default is 0.004 / s). */
	constexpr float SmokeProgressPerSecond = 0.05f;
	/** Past the gate, in L_MovementTest design coordinates (SmokeUtils::LevelPoint). */
	const FVector DesignBeyondGate(0.f, -2600.f, 100.f);

	enum class EStage : uint8
	{
		Setup,
		Creeping,
		Broken,
		Repairing,
		WaveHold,
		WaveNext,
		FinalNoPower,
		Opening,
		Walk
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		float ProgressAtBreakdown = 0.f;
		int32 FeedMark = 0;
		bool bHoldChecked = false;
		bool bWalkOrdered = false;
		bool bMovedDuringRepair = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete, const TCHAR* Why)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s"), Why);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GateGeneratorSmoke"));
		return false;
	}

	void SetGateCVars(float Rate)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Gate.ProgressPerSecond")))
		{
			Variable->Set(Rate, ECVF_SetByConsole);
		}
	}

	AInteractableActor* FindQuestObject(UWorld* World, EInteractableType Type)
	{
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == Type)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** A feed line containing Needle was posted since the mark. */
	bool FeedHas(UWorld* World, int32 Mark, const TCHAR* Needle)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		const TArray<FGameMessage>& History = Messages ? Messages->GetHistory() : TArray<FGameMessage>();
		for (int32 Index = FMath::Max(0, Mark); Index < History.Num(); ++Index)
		{
			if (History[Index].Text.ToString().Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}

	int32 FeedCount(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		return Messages ? Messages->GetHistory().Num() : 0;
	}

	/** The wave's enemies are moved out of the way and frozen: the wave must not end on its own. */
	int32 ParkEnemies(UWorld* World)
	{
		int32 Parked = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->CustomTimeDilation != 0.f && !It->IsDying())
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
				++Parked;
			}
		}
		return Parked;
	}

	void KillAll(UWorld* World)
	{
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
			Enemy->CustomTimeDilation = 1.f; // parked ones die normally
			FDamageSpec Spec;
			Spec.Amount = 100000.f;
			Spec.AttackerSource = TEXT("Commander");
			Enemy->GetHealthComponent()->TakeDamage(Spec);
		}
	}

	/** A complete navmesh path from the leader to beyond the gate exists. */
	bool PathBeyondGate(UWorld* World, const AOperativeCharacter* Leader)
	{
		const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Leader->GetActorLocation(),
			SmokeUtils::LevelPoint(World, DesignBeyondGate));
		return Path && Path->IsValid() && !Path->IsPartial();
	}

	bool LeavesBlock(const AGateActor* Gate)
	{
		return Gate->LeftDoor->GetCollisionEnabled() != ECollisionEnabled::NoCollision
			&& Gate->RightDoor->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < WarmupSeconds)
		{
			return World != nullptr;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		UWaveVictorySubsystem* Victory = World->GetSubsystem<UWaveVictorySubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AGateActor* Gate = AGateActor::FindLevelGate(World);
		AInteractableActor* Generator = FindQuestObject(World, EInteractableType::Generator);
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Quests || !Victory || !Squad || !Gate || !Generator || !Leader)
		{
			Check(State, false, TEXT("flow, quests, victory, squad, gate, generator, leader"));
			return Finish(State, false, TEXT("missing pieces"));
		}
		if (State.StageTime > StageTimeout)
		{
			Check(State, false, FString::Printf(TEXT("stage %d timed out: phase %s wave %d, gate \"%s\" (%.3f)"), static_cast<int32>(State.Stage),
				*UEnum::GetValueAsString(Flow->GetPhase()), Flow->GetWaveIndex(), *Gate->GetStatusText(), Gate->GetOpenProgress()));
			return Finish(State, false, TEXT("timeout"));
		}
		auto Next = [&State](EStage Stage) { State.Stage = Stage; State.StageTime = 0.f; State.bHoldChecked = false; };
		const int32 Total = Flow->GetConfig().TotalWaves;
		const float Progress = Gate->GetOpenProgress();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			SetGateCVars(SmokeProgressPerSecond);
			Check(State, Gate->UsesGeneratorPacing() && Total == 3, FString::Printf(TEXT("level data: generator_waves, %d waves"), Total));
			State.FeedMark = FeedCount(World);
			Quests->InteractWith(EInteractableType::Canister, FindQuestObject(World, EInteractableType::Canister));
			Quests->InteractWith(EInteractableType::Vehicle, nullptr);
			Quests->InteractWith(EInteractableType::Generator, Generator);
			Quests->InteractWith(EInteractableType::GateTerminal, nullptr);
			Check(State, Quests->IsGeneratorRunning() && Quests->IsGatePowered() && Gate->IsProgressUnlocked() && !Gate->IsOpen() && !Gate->IsOpening(),
				TEXT("terminal powered: the paced servos unlocked, the gate stays shut"));
			Check(State, !FeedHas(World, State.FeedMark, TEXT("swing open")) && FeedHas(World, State.FeedMark, TEXT("barely move")),
				TEXT("terminal line: «the leaves barely move», no «swing open»"));
			Check(State, Quests->GetObjective().ToString().Contains(TEXT("blast door")), FString::Printf(TEXT("objective \"%s\""), *Quests->GetObjective().ToString()));
			SmokeUtils::StartWaveFight(Flow);
			const int32 Parked = ParkEnemies(World);
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetWaveIndex() == 1 && Parked > 0,
				FString::Printf(TEXT("wave 1 came suddenly (%d enemies parked)"), Parked));
			Next(EStage::Creeping);
			return true;
		}
		case EStage::Creeping:
			ParkEnemies(World); // late spawns
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, FeedHas(World, State.FeedMark, TEXT("overloaded")), TEXT("HQ: «the power grid is overloaded»"));
			Check(State, Progress > 0.02f && Progress < 0.3f && Gate->GetProgressStatus() == EGateProgressStatus::Advancing,
				FString::Printf(TEXT("wave 1: the door creeps (%.3f, \"%s\")"), Progress, *Gate->GetStatusText()));
			Check(State, LeavesBlock(Gate) && !PathBeyondGate(World, Leader), TEXT("creeping: collision closed, no nav path beyond the gate"));
			State.FeedMark = FeedCount(World);
			Generator->TakeGeneratorDamage(100000.f);
			State.ProgressAtBreakdown = Gate->GetOpenProgress();
			Check(State, Generator->bGeneratorBroken, TEXT("the generator broke down"));
			Next(EStage::Broken);
			return true;
		case EStage::Broken:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Progress, State.ProgressAtBreakdown, 1e-4f),
				FString::Printf(TEXT("generator down 2 s: progress paused (%.4f -> %.4f), not reversed"), State.ProgressAtBreakdown, Progress));
			Check(State, Gate->GetProgressStatus() == EGateProgressStatus::NoPower && Gate->GetStatusText().Contains(TEXT("GENERATOR OFFLINE")),
				FString::Printf(TEXT("HUD line \"%s\""), *Gate->GetStatusText()));
			Check(State, FeedHas(World, State.FeedMark, TEXT("generator is down")), TEXT("HQ: «the generator is down and the blast door has stopped»"));
			State.FeedMark = FeedCount(World);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					Generator->ExecuteAction(Member); // the engineer's repair (2.5 s)
				}
			}
			Next(EStage::Repairing);
			return true;
		case EStage::Repairing:
			if (Generator->bGeneratorBroken)
			{
				State.bMovedDuringRepair |= !FMath::IsNearlyEqual(Progress, State.ProgressAtBreakdown, 1e-4f);
				return true;
			}
			if (!State.bHoldChecked)
			{
				State.bHoldChecked = true;
				return true; // one step for the gate to notice
			}
			Check(State, !State.bMovedDuringRepair, FString::Printf(TEXT("still paused during the repair (%.1fs)"), State.StageTime));
			Check(State, FeedHas(World, State.FeedMark, TEXT("Power is back")), TEXT("engineer repaired it: HQ «Power is back»"));
			Next(EStage::WaveHold);
			return true;
		case EStage::WaveHold:
		{
			ParkEnemies(World);
			const int32 Wave = Flow->GetWaveIndex();
			const float Cap = 0.3f * Wave;
			if (Progress < Cap - 1e-3f)
			{
				if (Progress >= 1.f || Gate->IsOpening() || Gate->IsOpen())
				{
					Check(State, false, FString::Printf(TEXT("wave %d: the door opened mid-wave (%.3f)"), Wave, Progress));
					return Finish(State, false, TEXT("opened too early"));
				}
				return true;
			}
			if (!State.bHoldChecked)
			{
				State.bHoldChecked = true;
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true; // held a second at the cap
			}
			Check(State, FMath::IsNearlyEqual(Progress, Cap, 1e-3f) && Gate->GetProgressStatus() == EGateProgressStatus::HoldingForWave,
				FString::Printf(TEXT("wave %d: held at its cap %.2f (%.4f, \"%s\")"), Wave, Cap, Progress, *Gate->GetStatusText()));
			Check(State, !Gate->IsOpen() && !Gate->IsOpening() && LeavesBlock(Gate) && !PathBeyondGate(World, Leader),
				FString::Printf(TEXT("wave %d: door impassable (collision closed, no nav path beyond)"), Wave));
			if (Wave >= Total)
			{
				// The final wave cleared without power must not open it.
				State.FeedMark = FeedCount(World);
				Generator->TakeGeneratorDamage(100000.f);
				KillAll(World);
				Next(EStage::FinalNoPower);
				return true;
			}
			KillAll(World);
			Next(EStage::WaveNext);
			return true;
		}
		case EStage::WaveNext:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				KillAll(World); // late spawns
				return true;
			}
			if (Flow->GetPhase() != ECodexGamePhase::WaveCleared || State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Gate->IsOpening() && !Gate->IsOpen() && Gate->GetOpenProgress() < 1.f,
				FString::Printf(TEXT("wave %d cleared (not the last): the door stays shut (%.3f)"), Flow->GetWaveIndex(), Gate->GetOpenProgress()));
			Victory->ContinueAfterWave();
			SmokeUtils::ReleaseHordeIntroPause(Flow);
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, FString::Printf(TEXT("wave %d started"), Flow->GetWaveIndex()));
			ParkEnemies(World);
			Next(EStage::WaveHold);
			return true;
		case EStage::FinalNoPower:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				KillAll(World);
				return true;
			}
			if (Flow->GetPhase() != ECodexGamePhase::WaveCleared || State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Gate->IsFinalWaveCleared() && !Gate->IsOpening() && !Gate->IsOpen() && LeavesBlock(Gate)
				&& Gate->GetProgressStatus() == EGateProgressStatus::NoPower,
				FString::Printf(TEXT("last wave cleared, generator down: the door waits (%.3f, \"%s\")"), Gate->GetOpenProgress(), *Gate->GetStatusText()));
			Check(State, FeedHas(World, State.FeedMark, TEXT("last wave")), TEXT("HQ: «That was the last wave, but the blast door has no power»"));
			State.FeedMark = FeedCount(World);
			Generator->RepairGenerator();
			Next(EStage::Opening);
			return true;
		case EStage::Opening:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			if (!State.bHoldChecked)
			{
				State.bHoldChecked = true;
				Check(State, (Gate->IsOpening() || Gate->IsOpen()) && Gate->GetOpenProgress() >= 1.f,
					FString::Printf(TEXT("generator back after the last wave: the door opens (\"%s\")"), *Gate->GetStatusText()));
				Check(State, FeedHas(World, State.FeedMark, TEXT("blast door is opening")), TEXT("HQ: «the blast door is opening! Get the squad inside»"));
				Victory->ContinueAfterWave(); // the victory panel's button: post-combat (the world runs again)
				return true;
			}
			if (!Gate->IsOpen())
			{
				return true;
			}
			if (!PathBeyondGate(World, Leader))
			{
				return true; // the navmesh rebuilds around the opened leaves
			}
			Check(State, !LeavesBlock(Gate) && Gate->GetProgressStatus() == EGateProgressStatus::Open,
				FString::Printf(TEXT("door fully open after the last wave: collision off, nav path beyond (%.1fs)"), State.StageTime));
			Next(EStage::Walk);
			return true;
		case EStage::Walk:
		{
			const FVector Beyond = SmokeUtils::LevelPoint(World, DesignBeyondGate);
			if (!State.bWalkOrdered)
			{
				Leader->OrderMoveTo(Beyond, true);
				State.bWalkOrdered = true;
				return true;
			}
			if (FVector::Dist2D(Leader->GetActorLocation(), Beyond) < 80.f)
			{
				Check(State, true, FString::Printf(TEXT("leader walked through the blast door (%.1fs)"), State.StageTime));
				return Finish(State, true, TEXT("blast door paced by the generator and the waves"));
			}
			return true;
		}
		default:
			return Finish(State, false, TEXT("unknown stage"));
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
		TEXT("CodexTactics.GateGeneratorSmoke"),
		TEXT("Dev check: the blast gate creeps open only with the generator up during the waves, pauses on a breakdown, opens after the last wave; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
