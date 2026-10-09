// Dev-only console command for a headless check of the death cinematic, the commander-only defeat rule and the
// operative hit reactions on L_MovementTest (user requests 2026-10-08):
//   Scripts/smoke.ps1 -Command CodexTactics.DeathCamSmoke
// Real time: a hit on a walking operative plays the hit clip on the upper-body slot (legs keep walking), a second hit
// right away is throttled. The Engineer dies: no mission failure, the camera focuses on him, time slows (~0.3x), input is
// off, his death clip really plays on the FullBody slot, then the camera returns to the leader, time and input resume,
// he lies as a corpse (held clip, no pawn collision, no movement, out of the squad, remains to search). Turn-based: a grid
// hit plays the reaction too, the Medic-Sapper dies -> out of the turn order, the fight goes on. The Commander dies ->
// focus, fade to black, «THE SQUAD HAS FALLEN», then the mission-failed screen (GameOver).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/LootCrateActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "UI/DeathCinematicOverlayWidget.h"

namespace DeathCamSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 HitsBefore = 0;
		float LowestDilation = 1.f;
		bool bSawInputOff = false;
		bool bSawFocus = false;
		bool bSawFade = false;
		bool bSawText = false;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<AOperativeCharacter> Medic;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DeathCamSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	/** The death clip owns the body: FullBody slot weight and the montage asset name. */
	float DeathSlotWeight(const AOperativeCharacter* Operative, FString& OutClip)
	{
		UOperativeAnimInstance* Anim = AnimOf(Operative);
		OutClip = TEXT("none");
		if (!Anim)
		{
			return 0.f;
		}
		for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
		{
			if (Instance && Instance->Montage && Instance->IsActive() && Instance->Montage->IsValidSlot(Anim->FullBodySlot))
			{
				OutClip = Instance->Montage->SlotAnimTracks.IsEmpty() || Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty()
					|| !Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()
					? Instance->Montage->GetName() : Instance->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()->GetName();
			}
		}
		return Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot);
	}

	void CheckCorpse(FState& State, const AOperativeCharacter* Dead, const TCHAR* Who, USquadSubsystem* Squad)
	{
		FString Clip;
		const float Weight = Dead ? DeathSlotWeight(Dead, Clip) : 0.f;
		Check(State, Dead && Weight > 0.9f, FString::Printf(TEXT("%s lies as a corpse: FullBody weight %.2f (%s held)"), Who, Weight, *Clip));
		Check(State, Dead && Dead->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
			&& Dead->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore
			&& Dead->GetCharacterMovement()->MovementMode == MOVE_None, FString::Printf(TEXT("%s: no pawn / click collision, no movement"), Who));
		Check(State, Dead && !Squad->GetMembers().Contains(Dead) && Dead->IsKilledInAction(), FString::Printf(TEXT("%s out of the squad, killed in action"), Who));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 120.f)
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
		UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(World, 0));
		const float Dilation = World->GetWorldSettings()->GetEffectiveTimeDilation();
		AOperativeCharacter* Commander = State.Commander.Get();
		AOperativeCharacter* Engineer = State.Engineer.Get();
		AOperativeCharacter* Medic = State.Medic.Get();
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto Wait = [&State](float Seconds) { return State.StageTime < Seconds; };
		switch (State.Stage)
		{
		case 0:
		{
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				(Member->SquadRole == EOperativeRole::Commander ? State.Commander : Member->SquadRole == EOperativeRole::Engineer ? State.Engineer
					: State.Medic) = Member;
			}
			Commander = State.Commander.Get();
			Engineer = State.Engineer.Get();
			Medic = State.Medic.Get();
			Check(State, Commander && Engineer && Medic && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("real-time fight, commander / engineer / medic"));
			if (!Commander || !Engineer || !Medic)
			{
				return Finish(State, false);
			}
			Squad->SetLeader(Commander);
			if (AEnemyCharacter* Keeper = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Commander->GetActorLocation() - Commander->GetActorForwardVector() * 3500.f + FVector(0.f, 0.f, 30.f)))
			{
				Keeper->CustomTimeDilation = 0.f; // keeps the wave alive
			}
			Squad->SetLeader(Engineer); // he walks on his own order (the commander leads again before his death)
			Engineer->OrderMoveTo(Engineer->GetActorLocation() + Engineer->GetActorRightVector() * 600.f, false);
			Next();
			return true;
		}
		case 1:
		{
			if (Wait(0.8f))
			{
				return true;
			}
			// Real-time hit reaction on a walking operative (upper body, legs keep walking).
			UOperativeAnimInstance* Anim = AnimOf(Engineer);
			State.HitsBefore = Anim ? Anim->GetHitReactionsPlayed() : 0;
			Engineer->TakeHit(12.f, TEXT("Smoke"), false, true);
			Engineer->TakeHit(12.f, TEXT("Smoke"), false, true); // same burst: throttled
			Check(State, Anim && Anim->GetHitReactionsPlayed() == State.HitsBefore + 1 && Anim->GetLastHitReactionClip(),
				FString::Printf(TEXT("hit reaction started once for a burst of two (clip %s, slot %s)"),
					Anim && Anim->GetLastHitReactionClip() ? *Anim->GetLastHitReactionClip()->GetName() : TEXT("none"),
					Anim ? *Anim->GetLastHitReactionSlot().ToString() : TEXT("-")));
			Next();
			return true;
		}
		case 2:
		{
			if (Wait(0.2f))
			{
				return true;
			}
			UOperativeAnimInstance* Anim = AnimOf(Engineer);
			const float Upper = Anim ? Anim->GetSlotMontageGlobalWeight(Anim->UpperBodySlot) : 0.f;
			const float Full = Anim ? Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot) : 1.f;
			Check(State, Anim && Anim->GetLastHitReactionSlot() == Anim->UpperBodySlot && Upper > 0.3f && Full < 0.3f,
				FString::Printf(TEXT("real time: hit clip on the upper-body slot (weight %.2f, FullBody %.2f)"), Upper, Full));
			Check(State, Engineer->GetVelocity().Size2D() > 50.f, FString::Printf(TEXT("legs keep walking (%.0f cm/s)"), Engineer->GetVelocity().Size2D()));
			Next();
			return true;
		}
		case 3:
		{
			if (Wait(1.f))
			{
				return true;
			}
			// The Engineer dies in real time.
			Squad->SetLeader(Commander);
			Engineer->StopOperative();
			Engineer->TakeHit(100000.f, TEXT("Smoke"), false, true);
			Check(State, !Mission->IsMissionFailed() && Flow->GetPhase() != ECodexGamePhase::GameOver, TEXT("the engineer's death does not fail the mission"));
			Check(State, DeathCam->IsActive() && DeathCam->GetFocusVictim() == Engineer && !DeathCam->IsDefeatPending(), TEXT("death cinematic on the engineer"));
			Check(State, Squad->GetLeader() == Commander, TEXT("the commander stays the leader"));
			State.LowestDilation = 1.f;
			Next();
			return true;
		}
		case 4:
		{
			State.LowestDilation = FMath::Min(State.LowestDilation, Dilation);
			State.bSawInputOff |= PC && !PC->InputEnabled();
			if (Wait(0.9f))
			{
				return true;
			}
			Check(State, State.LowestDilation < 0.35f && State.LowestDilation > 0.25f, FString::Printf(TEXT("slow motion %.2f"), State.LowestDilation));
			Check(State, State.bSawInputOff, TEXT("player input off during the focus"));
			Check(State, Camera && Camera->GetFollowTarget() == Engineer, TEXT("camera follows the fallen engineer"));
			FString Clip;
			const float Weight = DeathSlotWeight(Engineer, Clip);
			const UKnockdownComponent* Knockdown = Engineer->KnockdownComponent;
			Check(State, Weight > 0.9f && Knockdown && Knockdown->PlayedDeathFall(),
				FString::Printf(TEXT("death clip plays on the FullBody slot (weight %.2f, %s)"), Weight, *Clip));
			Next();
			return true;
		}
		case 5:
			State.LowestDilation = FMath::Min(State.LowestDilation, Dilation);
			if (DeathCam->IsActive())
			{
				return State.StageTime < 10.f ? true : (Check(State, false, TEXT("focus never ended")), Finish(State, false));
			}
			Check(State, State.StageTime > 1.f, FString::Printf(TEXT("the focus lasted %.1f s real after the first second"), State.StageTime));
			Next();
			return true;
		case 6:
		{
			if (Wait(1.2f))
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Dilation, 1.f, 0.01f), FString::Printf(TEXT("time resumed (%.2f)"), Dilation));
			Check(State, PC && PC->InputEnabled(), TEXT("player input back"));
			Check(State, Camera && Camera->GetFollowTarget() == Commander, TEXT("camera back on the leader"));
			CheckCorpse(State, Engineer, TEXT("engineer"), Squad);
			bool bRemains = false;
			for (TActorIterator<ALootCrateActor> It(World); It; ++It)
			{
				bRemains |= It->ActorHasTag(TEXT("CorpseLoot")) && It->CrateName.ToString().Contains(Engineer->DisplayName.ToString());
			}
			Check(State, bRemains, TEXT("his remains can be searched"));
			// Turn-based next: an enemy 9 m out makes the fight.
			if (AEnemyCharacter* Frost = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				Commander->GetActorLocation() + Commander->GetActorForwardVector() * 900.f + FVector(0.f, 0.f, 30.f)))
			{
				Frost->GetHealthComponent()->SetMaxHealth(5000.f, true);
			}
			Next();
			return true;
		}
		case 7:
			if (Wait(0.3f))
			{
				return true;
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			Next();
			return true;
		case 8:
		{
			if (Wait(0.6f) || TurnBased->IsBusy())
			{
				return State.StageTime < 8.f ? true : (Check(State, false, TEXT("turn-based stays busy")), Finish(State, false));
			}
			Check(State, TurnBased->GetUnitState(Engineer) == nullptr, TEXT("the dead engineer is not in the turn order / grid"));
			// Grid hit on the medic (the enemy turn's ApplySquadHit path: TakeHit with bypass).
			UOperativeAnimInstance* Anim = AnimOf(Medic);
			State.HitsBefore = Anim ? Anim->GetHitReactionsPlayed() : 0;
			Medic->TakeHit(15.f, TEXT("Frostbitten"), false, true);
			Check(State, Anim && Anim->GetHitReactionsPlayed() == State.HitsBefore + 1, TEXT("turn-based: the grid hit plays a reaction"));
			Next();
			return true;
		}
		case 9:
		{
			if (Wait(0.2f))
			{
				return true;
			}
			UOperativeAnimInstance* Anim = AnimOf(Medic);
			const float Upper = Anim ? Anim->GetSlotMontageGlobalWeight(Anim->GetLastHitReactionSlot()) : 0.f;
			Check(State, Upper > 0.3f, FString::Printf(TEXT("turn-based: reaction montage on %s (weight %.2f)"),
				Anim ? *Anim->GetLastHitReactionSlot().ToString() : TEXT("-"), Upper));
			Medic->TakeHit(100000.f, TEXT("Frostbitten"), false, true);
			Check(State, !Mission->IsMissionFailed() && TurnBased->IsActive() && TurnBased->GetUnitState(Medic) == nullptr,
				TEXT("turn-based: the medic's death drops him from the turn order, the fight goes on"));
			Check(State, DeathCam->IsActive() && DeathCam->GetFocusVictim() == Medic, TEXT("turn-based: death cinematic on the medic"));
			Next();
			return true;
		}
		case 10:
			if (DeathCam->IsActive())
			{
				return State.StageTime < 10.f ? true : (Check(State, false, TEXT("turn-based focus never ended")), Finish(State, false));
			}
			Next();
			return true;
		case 11:
			if (Wait(1.f))
			{
				return true;
			}
			CheckCorpse(State, Medic, TEXT("medic"), Squad);
			Check(State, TurnBased->IsActive() && Squad->GetMembers().Num() >= 1, TEXT("turn-based fight goes on with the commander"));
			Flow->ExitTurnBasedToRealTime();
			Next();
			return true;
		case 12:
			if (Wait(0.6f))
			{
				return true;
			}
			// The commander dies: focus, fade, «THE SQUAD HAS FALLEN», mission failed.
			Commander->TakeHit(100000.f, TEXT("Smoke"), false, true);
			Check(State, DeathCam->IsDefeatPending() && !Mission->IsMissionFailed(), TEXT("commander killed: defeat pending, no instant failure screen"));
			Next();
			return true;
		case 13:
		{
			State.bSawFocus |= DeathCam->GetPhase() == EDeathCinematicPhase::Focus && DeathCam->GetFocusVictim() == Commander;
			const UDeathCinematicOverlayWidget* Overlay = DeathCam->GetOverlay();
			State.bSawFade |= DeathCam->GetPhase() == EDeathCinematicPhase::DefeatFade && Overlay && Overlay->GetShownAlpha() > 0.2f;
			State.bSawText |= DeathCam->GetPhase() == EDeathCinematicPhase::DefeatText && Overlay
				&& Overlay->GetShownText().ToString() == MissionRules::GetSquadFallenText().ToString() && Overlay->GetShownAlpha() > 0.99f;
			if (!Mission->IsMissionFailed())
			{
				return State.StageTime < 15.f ? true : (Check(State, false, TEXT("mission never failed")), Finish(State, false));
			}
			Check(State, State.bSawFocus, TEXT("focus on the fallen commander first"));
			Check(State, State.bSawFade, TEXT("fade to black"));
			Check(State, State.bSawText, TEXT("«THE SQUAD HAS FALLEN» on black"));
			Check(State, Mission->IsMissionFailed() && Flow->GetPhase() == ECodexGamePhase::GameOver, TEXT("then the mission-failed screen (GameOver)"));
			Check(State, Mission->GetFailureReason().ToString().Contains(Commander->DisplayName.ToString()), TEXT("reason names the commander"));
			Check(State, DeathCam->GetOverlay() == nullptr, TEXT("defeat overlay removed for the failed screen"));
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
		TEXT("CodexTactics.DeathCamSmoke"),
		TEXT("Dev check (2026-10-08): hit reactions (real time / turn-based), commander-only defeat, death cinematic (focus, slow "
			"motion, death clip, return), corpses, fade + THE SQUAD HAS FALLEN + mission failed; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
