// Dev-only headless check of the game designer's play-test bug 2026-10-23 (lake, after the horde): "After the fight ended, one of the
// operatives died, one froze, and now even pressing B he does not return to the formation. Solo mode does not switch off, and he
// doesn't come back." On L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands RegroupAfterLossesSmoke
// Root causes: (a) the final fight ends in PostCombat, which lasts until the level ends - and the formation ran in Exploration only, so
// after the horde no follower ever walked again (B toggled Solo on / off without a visible effect); (b) the post-fight hold
// (2026-10-22) ended only when the leader moved, not on Regroup; (c) a formation slot off the navmesh (here the fallen Engineer's
// remains crate, at the lake a wall / the generator) was unreachable for good - the follower stood while the leader stood.
//  1. a real-time wave fight, Solo on with the Engineer leading; the Engineer (the leader) is killed: the leadership passes to a
//     living operative (the Commander), Solo stays on; the Medic-Sapper freezes (frostbite: prone, cannot get up);
//  2. the fight's end -> PostCombat, the Commander leads, the post-fight hold is armed; B is not refused (no order lock, no
//     tutorial lock);
//  3. A/B with the old rule (Codex.Squad.PostCombatFormation 0): B ends Solo but the frozen Medic does not come back (the bug);
//  4. the fix: Solo on, B again -> Solo off, the hold ends, the frostbitten Medic crawls towards the leader (slowly, still prone);
//  5. warmed by a heat pack (frostbite cleared), Solo on, sent 9 m away, B -> she walks back into the formation;
//  6. the leader walks 10 m: she follows (the formation runs in PostCombat).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Data/TacticalHeatTuning.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Formation/FormationRuntimeRules.h"
#include "Kismet/GameplayStatics.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tutorial/TutorialSubsystem.h"

namespace RegroupAfterLossesSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		FVector MedicStart = FVector::ZeroVector;
		float StartGap = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetPostCombatFormation(int32 Value);
	void SetSlotFallback(int32 Value);

	/**
	 * Formation sectors (2026-10-25): an "old rule" reproduction of a pre-sectors bug runs on the old formation too (its slots, speeds and
	 * facing; Codex.Formation.Sectors 0) - bOld saves the console's value and sets 0, false restores it.
	 */
	void UseOldFormationRuleRegroup(bool bOld)
	{
		static int32 Saved = -1;
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Sectors"));
		if (!CVar)
		{
			return;
		}
		if (bOld && Saved < 0)
		{
			Saved = CVar->GetInt();
			CVar->Set(0, ECVF_SetByConsole);
		}
		else if (!bOld && Saved >= 0)
		{
			CVar->Set(Saved, ECVF_SetByConsole);
			Saved = -1;
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		SetPostCombatFormation(1);
		SetSlotFallback(1);
		UseOldFormationRuleRegroup(false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RegroupAfterLossesSmoke"));
		return false;
	}

	void SetPostCombatFormation(int32 Value)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.PostCombatFormation"))->Set(Value);
	}

	void SetSlotFallback(int32 Value)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.SlotFallback"))->Set(Value);
	}

	void Kill(AActor* Victim, UHealthComponent* Health)
	{
		if (Victim && Health && Health->IsAlive())
		{
			FDamageSpec Spec;
			Spec.Amount = 100000.f;
			Spec.AttackerSource = TEXT("smoke");
			Health->TakeDamage(Spec);
		}
	}

	/** Puts Who on a reachable navmesh spot Distance cm behind the leader (away from his facing). */
	void SendBehind(UWorld* World, AOperativeCharacter* Who, const AOperativeCharacter* Leader, float Distance)
	{
		const FVector From = Leader->GetActorLocation();
		const FVector Spot = SmokeUtils::ClearPoint(World, From, From - Leader->GetActorForwardVector() * Distance);
		Who->StopOperative();
		Who->TeleportTo(FVector(Spot.X, Spot.Y, Who->GetActorLocation().Z), Who->GetActorRotation(), false, true);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
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
		const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Flow || !Squad || !PC)
		{
			Check(State, false, TEXT("subsystems / player controller"));
			return Finish(State, false);
		}
		AOperativeCharacter* Commander = State.Commander.Get();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		AOperativeCharacter* Medic = State.Medic.Get();
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto Gap = [&]() { return Commander && Medic ? FVector::Dist2D(Commander->GetActorLocation(), Medic->GetActorLocation()) : 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			// One wave only: its end is the final fight's end (PostCombat), as the lake's horde.
			FGameFlowConfig Config = Flow->GetConfig();
			Config.TotalWaves = 1;
			Flow->SetConfig(Config);
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->CustomTimeDilation = 0.f; // the wave stays alive until the smoke ends it
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				(Member->SquadRole == EOperativeRole::Commander ? State.Commander : Member->SquadRole == EOperativeRole::Engineer ? State.Engineer
					: State.Medic) = Member;
			}
			Commander = State.Commander.Get();
			Engineer = State.Engineer.Get();
			Medic = State.Medic.Get();
			Check(State, Commander && Engineer && Medic && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				TEXT("[fight] a real-time wave fight; commander / engineer / medic"));
			if (!Commander || !Engineer || !Medic)
			{
				return Finish(State, false);
			}
			Commander->HealthComponent->SetMaxHealth(5000.f, true);
			Medic->HealthComponent->SetMaxHealth(5000.f, true);
			// Solo with the Engineer leading; the Medic holds 8 m behind him.
			Squad->SetLeader(Engineer);
			Squad->EnterSoloMode();
			SendBehind(World, Medic, Engineer, 800.f);
			Next();
			return true;
		}
		case 1:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// The leader falls: the leadership passes to a living operative, Solo stays on.
			Kill(Engineer, Engineer->HealthComponent);
			Check(State, Engineer->IsKilledInAction() && !Squad->GetMembers().Contains(Engineer) && Squad->GetMembers().Num() == 2,
				TEXT("[KIA] the Engineer (the leader) is killed in action and leaves the squad"));
			Check(State, Squad->GetLeader() && Squad->GetLeader() != Engineer && Squad->GetLeader()->HealthComponent->IsAlive(),
				FString::Printf(TEXT("[KIA] the leadership passes to a living operative (%s)"),
					Squad->GetLeader() ? *Squad->GetLeader()->DisplayName.ToString() : TEXT("none")));
			Check(State, Squad->IsSoloMode(), TEXT("[KIA] Solo stays on"));
			// The Medic freezes: frostbite (100 %) - she collapses prone and cannot get up.
			Medic->ColdSurvival->AddColdShock(100.f, 100.f);
			Medic->ColdSurvival->StepCold(0.1f);
			Next();
			return true;
		case 2:
			if (DeathCam && DeathCam->IsActive() && State.StageTime < 15.f)
			{
				return true; // the death cinematic of the Engineer
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Medic->ColdSurvival->IsFrostbitten() && Medic->GetStance() == EOperativeStance::Prone,
				FString::Printf(TEXT("[frozen] the Medic is frostbitten (cold %.0f, stance %d)"), Medic->ColdLevel, static_cast<int32>(Medic->GetStance())));
			// The fight ends.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 1.f;
				Kill(*It, It->GetHealthComponent());
			}
			Next();
			return true;
		case 3:
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					Kill(*It, It->GetHealthComponent()); // late spawns
				}
				return true;
			}
			if (Flow->GetPhase() == ECodexGamePhase::WaveCleared)
			{
				Flow->AdvanceAfterWave();
				return true;
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::PostCombat, TEXT("[after the fight] PostCombat"));
			Check(State, Squad->GetLeader() == Commander, TEXT("[after the fight] the Commander leads"));
			Check(State, Squad->IsSoloMode() && Squad->IsPostFightHoldActive(), TEXT("[after the fight] Solo still on, the post-fight hold armed"));
			Check(State, Squad->IsFormationActive(), TEXT("[after the fight] the formation runs in PostCombat (was: off until the level ended)"));
			Check(State, !PC->IsRealTimeOrderLocked() && !UTutorialSubsystem::CheckCommandLocked(PC, PlayerCommands::Regroup),
				TEXT("[after the fight] B is not refused (no order lock, no tutorial lock)"));
			// The Medic 9 m behind the leader.
			SendBehind(World, Medic, Commander, 900.f);
			Next();
			return true;
		case 4:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// A/B: the old rule reproduces the bug.
			SetPostCombatFormation(0);
			UseOldFormationRuleRegroup(true); // the old rules' reproductions (to the slot fallback off) run on the old formation
			State.MedicStart = Medic->GetActorLocation();
			Squad->ToggleSoloMode(); // B: Regroup
			Check(State, !Squad->IsSoloMode(), TEXT("[old rule] B: Solo off"));
			Next();
			return true;
		case 5:
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, FVector::Dist2D(State.MedicStart, Medic->GetActorLocation()) < 30.f && Squad->IsPostFightHoldActive(),
				FString::Printf(TEXT("[old rule] the bug reproduces: after B the Medic stays %.0f cm away, the hold stays (moved %.0f cm)"),
					Gap(), FVector::Dist2D(State.MedicStart, Medic->GetActorLocation())));
			// The fix of the formation after the fight: Solo on again, then B. The slot fallback still off (the old rule): her slot lies
			// on the navmesh hole of the Engineer's remains crate (he fell there) - unreachable, she stays.
			SetPostCombatFormation(1);
			SetSlotFallback(0);
			Squad->ToggleSoloMode();
			Check(State, Squad->IsSoloMode(), TEXT("[fix] B: Solo on"));
			Next();
			return true;
		case 6:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			State.StartGap = Gap();
			Squad->ToggleSoloMode();
			Check(State, !Squad->IsSoloMode() && !Squad->IsPostFightHoldActive(), TEXT("[fix] B: Solo off and the post-fight hold ends"));
			Next();
			return true;
		case 7:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			float RemainsToLeader = -1.f;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("CorpseLoot")))
				{
					RemainsToLeader = FVector::Dist2D(It->GetActorLocation(), Commander->GetActorLocation());
				}
			}
			Check(State, State.StartGap - Gap() < 30.f, FString::Printf(TEXT("[slot fallback off] the second cause reproduces: her slot is off the navmesh "
				"(the Engineer's remains %.0f cm from the leader), she stays (%.0f -> %.0f cm)"), RemainsToLeader, State.StartGap, Gap()));
			SetSlotFallback(1);
			UseOldFormationRuleRegroup(false);
			State.StartGap = Gap();
			Next();
			return true;
		}
		case 8:
			if (State.StageTime < 8.f && State.StartGap - Gap() < 120.f)
			{
				return true;
			}
			// Formation sectors: with two left she is the pair's partner behind the leader's right shoulder (FORMATION_SECTORS.md "Pair"), not on
			// the old left slot - the gap to the leader closes a little slower on the way there (100 cm then).
			Check(State, State.StartGap - Gap() >= (FormationRuntime::IsEnabled() ? 100.f : 120.f) && Medic->ColdSurvival->IsFrostbitten()
				&& Medic->GetStance() == EOperativeStance::Prone,
				FString::Printf(TEXT("[fix] the frostbitten Medic crawls back to the leader (%.0f -> %.0f cm in %.1f s, still prone)"),
					State.StartGap, Gap(), State.StageTime));
			// Warmed: a heat pack clears the frostbite (she gets up to a crouch).
			Medic->ColdSurvival->ApplyHeatPack(TacticalHeatTuning::Get());
			Medic->ColdLevel = 0.f;
			Medic->ColdSurvival->StepCold(0.1f);
			Check(State, !Medic->ColdSurvival->IsFrostbitten(), TEXT("[warmed] heat pack: frostbite cleared"));
			Squad->ToggleSoloMode();
			Check(State, Squad->IsSoloMode(), TEXT("[warmed] B: Solo on"));
			Next();
			return true;
		case 9:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			SendBehind(World, Medic, Commander, 900.f);
			State.StartGap = Gap();
			Squad->ToggleSoloMode();
			Check(State, !Squad->IsSoloMode(), TEXT("[warmed] B: Solo off (Regroup)"));
			Next();
			return true;
		case 10:
			if (State.StageTime < 12.f && Gap() > 450.f)
			{
				return true;
			}
			Check(State, Gap() <= 450.f, FString::Printf(TEXT("[warmed] the Medic walks back into the formation (%.0f -> %.0f cm in %.1f s)"),
				State.StartGap, Gap(), State.StageTime));
			// The leader walks 10 m: the formation follows him after the fight.
			Commander->OrderMoveTo(SmokeUtils::ClearPoint(World, Commander->GetActorLocation(),
				Commander->GetActorLocation() + Commander->GetActorForwardVector() * 1000.f), false);
			Next();
			return true;
		case 11:
			if (State.StageTime < 2.f || ((Commander->IsMoving() || Gap() > 450.f) && State.StageTime < 15.f))
			{
				return true;
			}
			Check(State, !Commander->IsMoving() && Gap() <= 450.f,
				FString::Printf(TEXT("[follow] the leader walked 10 m after the fight: the Medic follows (%.0f cm off)"), Gap()));
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
		TEXT("CodexTactics.RegroupAfterLossesSmoke"),
		TEXT("Dev check (bug 2026-10-23): after the final fight with one operative KIA and one frozen, B ends Solo and the follower regroups; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
