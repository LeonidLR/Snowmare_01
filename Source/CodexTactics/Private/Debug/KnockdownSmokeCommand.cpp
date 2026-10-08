// Dev-only console command for a headless knockdown check on L_MovementTest (Sprint 14, TANDEM request #12):
//   Scripts/smoke.ps1 -Command CodexTactics.KnockdownSmoke
//   rendered: UnrealEditor.exe ... -game -ExecCmds="CodexTactics.KnockdownSmoke shot" (Saved/Screenshots/WindowsEditor/Knockdown_*.png)
// Real time: a Frost Brute's blow knocks the leader down on his back — the FullBody slot plays AM_Knocked_Back (no left-hand
// IK / aim offset, capsule free, no walking), a move order is buffered, the recovery bar fills, stops in the tactical pause
// and under the dialogue AI pause, AM_Revive_Back plays, control returns and the buffered order runs; no re-knock right
// away. Enemies: a >= 40 HP hit floors a Frostbitten, a Brute shrugs off a plain heavy hit (poise) but not a crit, the
// Frostbitten killed while down plays Death_*. Turn-based: getting up costs 2 AP; with 1 AP the turn is skipped and he
// stays down; back in real time the timer gets him up. Last, a blast behind a squad mate throws him on his face and his
// death while down plays AM_Death_Front.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/WorldAIPauseSubsystem.h"
#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Misc/Paths.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace KnockdownSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		bool bShotTaken = false;
		bool bFinalShot = false;
		float Fraction = 0.f;
		int32 APBefore = 0;
		FVector LeaderStart = FVector::ZeroVector;
		FVector BufferedGoal = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<AOperativeCharacter> TurnUnit;
		TWeakObjectPtr<AOperativeCharacter> Mate;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TWeakObjectPtr<AEnemyCharacter> Frostbitten;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("KnockdownSmoke"));
		return false;
	}

	void Shot(FState& State, const TCHAR* Name)
	{
		if (State.bShots)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("Knockdown_%s.png"), Name), /*bShowUI*/ true, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot Knockdown_%s.png"), Name);
		}
	}

	UKnockdownComponent* KnockdownOf(const AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UKnockdownComponent>() : nullptr;
	}

	/** The knockdown montage really plays on the unit's full-body slot and its clip name contains Clip. */
	bool PlaysClip(const ACharacter* Unit, const TCHAR* Clip, float& OutWeight)
	{
		const UKnockdownComponent* Knockdown = KnockdownOf(Unit);
		UAnimInstance* Anim = Unit && Unit->GetMesh() ? Unit->GetMesh()->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = Knockdown ? Knockdown->GetPlayingMontage() : nullptr;
		const UAnimSequenceBase* Asset = Knockdown ? Knockdown->GetPlayingClip() : nullptr;
		OutWeight = Anim && Knockdown ? Anim->GetSlotMontageGlobalWeight(Knockdown->GetSlotName()) : 0.f;
		// Active instance (a held last frame is no longer "playing" but still owns the slot).
		const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
		return Instance && !Instance->IsStopped() && Asset && Asset->GetName().Contains(Clip);
	}

	void CheckClip(FState& State, const ACharacter* Unit, const TCHAR* Clip, const TCHAR* What)
	{
		float Weight = 0.f;
		const bool bPlays = PlaysClip(Unit, Clip, Weight);
		const UKnockdownComponent* Knockdown = KnockdownOf(Unit);
		Check(State, bPlays && Weight > 0.5f, FString::Printf(TEXT("%s: %s on slot %s (asset %s, slot weight %.2f)"), What, Clip,
			Knockdown ? *Knockdown->GetSlotName().ToString() : TEXT("-"),
			Knockdown && Knockdown->GetPlayingClip() ? *Knockdown->GetPlayingClip()->GetName() : TEXT("none"), Weight));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
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
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		AOperativeCharacter* Leader = State.Leader.Get();
		UKnockdownComponent* LeaderKnock = KnockdownOf(Leader);
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; State.bShotTaken = false; };
		auto Timeout = [&State](float Seconds, const TCHAR* What)
		{
			if (State.StageTime > Seconds)
			{
				Check(State, false, FString::Printf(TEXT("%s (stage %d timed out)"), What, State.Stage));
				return true;
			}
			return false;
		};
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
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true; // small enemies would rush it
				}
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true; // nobody shoots the test enemies
			}
			State.Leader = Squad->GetLeader();
			Leader = State.Leader.Get();
			Check(State, Leader && KnockdownOf(Leader) && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("real-time fight, the leader has a knockdown component"));
			if (!Leader)
			{
				return Finish(State, false);
			}
			State.LeaderStart = Leader->GetActorLocation();
			// A frozen hound far behind keeps the wave alive (no "wave cleared" time stop when the test enemies go).
			if (AEnemyCharacter* Keeper = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 3500.f + FVector(0.f, 0.f, 30.f)))
			{
				Keeper->CustomTimeDilation = 0.f;
			}
			State.Brute = Waves->SpawnEnemy(EEnemyArchetype::Brute, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 160.f + FVector(0.f, 0.f, 30.f),
				(-Leader->GetActorForwardVector()).Rotation());
			Check(State, State.Brute.IsValid() && KnockdownOf(State.Brute.Get()) && KnockdownOf(State.Brute.Get())->bHeavyPoise, TEXT("brute spawned in front of the leader (heavy poise)"));
			Next();
			return true;
		}
		case 1:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			AEnemyCharacter* Brute = State.Brute.Get();
			Leader->ForcedDodgeRollForTesting = 0.f; // no dodge
			Brute->AttackTarget(Leader);
			Brute->Destroy();
			Check(State, LeaderKnock->IsDown() && LeaderKnock->GetPhase() == EKnockdownPhase::Falling,
				FString::Printf(TEXT("brute blow knocks the leader down (cause %d)"), static_cast<int32>(LeaderKnock->GetState().Cause)));
			Check(State, LeaderKnock->GetDirection() == EKnockdownDirection::Back, TEXT("blow from the front: falls on his back"));
			Check(State, Leader->HealthComponent->HasStatusEffect(EStatusEffect::Knockdown), TEXT("status effect Knockdown reported by the health component"));
			Check(State, Leader->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore
				&& Leader->GetCharacterMovement()->MovementMode == MOVE_None, TEXT("capsule frees pawns, no walking"));
			State.BufferedGoal = State.LeaderStart + Leader->GetActorRightVector() * 400.f;
			Check(State, Leader->OrderMoveTo(State.BufferedGoal, false) == EOperativeOrderResult::Accepted && LeaderKnock->HasBufferedOrder(),
				TEXT("move order while down is buffered"));
			Check(State, !Leader->CanShoot(), TEXT("cannot shoot while down"));
			Next();
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.35f)
			{
				return true;
			}
			CheckClip(State, Leader, TEXT("Knocked_Back"), TEXT("fall clip"));
			const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance());
			Check(State, Anim && Anim->bKnockedDown && Anim->LeftHandIKAlpha < 0.05f && Anim->AimOffsetAlpha < 0.05f,
				FString::Printf(TEXT("anim knocked down, left-hand IK %.2f, aim offset %.2f"), Anim ? Anim->LeftHandIKAlpha : -1.f, Anim ? Anim->AimOffsetAlpha : -1.f));
			Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) < 60.f, TEXT("he stays where he fell"));
			Shot(State, TEXT("1_Fall"));
			Next();
			return true;
		}
		case 3:
			if (LeaderKnock->GetPhase() != EKnockdownPhase::Downed)
			{
				return !Timeout(3.f, TEXT("never landed")) || Finish(State, false);
			}
			CheckClip(State, Leader, TEXT("Knocked_Back"), TEXT("downed: the fall's last frame held"));
			State.Fraction = LeaderKnock->GetRecoveryFraction();
			Next();
			return true;
		case 4:
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			Check(State, LeaderKnock->GetRecoveryFraction() > State.Fraction + 0.2f,
				FString::Printf(TEXT("recovery bar fills (%.2f -> %.2f)"), State.Fraction, LeaderKnock->GetRecoveryFraction()));
			Shot(State, TEXT("2_Downed"));
			Flow->ToggleTacticalPause();
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tactical pause"));
			State.Fraction = LeaderKnock->GetRecoveryFraction();
			Next();
			return true;
		case 5:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(LeaderKnock->GetRecoveryFraction(), State.Fraction, 1e-4f) && LeaderKnock->GetPhase() == EKnockdownPhase::Downed,
				FString::Printf(TEXT("bar frozen in the tactical pause (%.3f)"), LeaderKnock->GetRecoveryFraction()));
			Flow->ToggleTacticalPause();
			World->GetSubsystem<UWorldAIPauseSubsystem>()->AddPauseBlocker(TEXT("KnockdownSmoke"));
			State.Fraction = LeaderKnock->GetRecoveryFraction();
			Next();
			return true;
		case 6:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(LeaderKnock->GetRecoveryFraction(), State.Fraction, 1e-4f),
				FString::Printf(TEXT("bar frozen while the dialogue pause holds the world (%.3f)"), LeaderKnock->GetRecoveryFraction()));
			World->GetSubsystem<UWorldAIPauseSubsystem>()->RemovePauseBlocker(TEXT("KnockdownSmoke"));
			Next();
			return true;
		case 7:
			if (LeaderKnock->GetPhase() != EKnockdownPhase::GettingUp)
			{
				return !Timeout(3.f, TEXT("never started to get up")) || Finish(State, false);
			}
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			CheckClip(State, Leader, TEXT("Revive_Back"), TEXT("get-up clip"));
			Check(State, FMath::IsNearlyEqual(LeaderKnock->GetRecoveryFraction(), 1.f), TEXT("bar full while getting up"));
			Shot(State, TEXT("3_GetUp"));
			Next();
			return true;
		case 8:
			if (LeaderKnock->IsDown())
			{
				return !Timeout(3.f, TEXT("never got up")) || Finish(State, false);
			}
			Check(State, Leader->GetCharacterMovement()->MovementMode == MOVE_Walking
				&& Leader->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block, TEXT("up: walking, capsule blocks pawns again"));
			Check(State, !LeaderKnock->HasBufferedOrder() && FVector::Dist2D(Leader->GetLastMoveDestination(), State.BufferedGoal) < 100.f,
				TEXT("the buffered move order ran after the get-up"));
			Check(State, LeaderKnock->GetRecoveryCount() == 1, TEXT("one recovery"));
			Check(State, !LeaderKnock->TryKnockDown(EKnockdownCause::Pounce, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 100.f),
				TEXT("no second knockdown right after getting up (re-knock immunity)"));
			// Enemies.
			State.Frostbitten = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 350.f + FVector(0.f, 0.f, 30.f),
				(-Leader->GetActorForwardVector()).Rotation());
			State.Brute = Waves->SpawnEnemy(EEnemyArchetype::Brute, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 900.f + FVector(0.f, 0.f, 30.f));
			Next();
			return true;
		case 9:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			AEnemyCharacter* Frost = State.Frostbitten.Get();
			AEnemyCharacter* Brute = State.Brute.Get();
			if (!Frost || !Brute)
			{
				Check(State, false, TEXT("enemies spawned"));
				return Finish(State, false);
			}
			Frost->GetHealthComponent()->SetMaxHealth(1000.f, true); // a 70 HP hit must not kill it
			Brute->GetHealthComponent()->SetMaxHealth(2000.f, true);
			FDamageSpec Heavy;
			Heavy.Amount = 70.f;
			Heavy.ArmorPenetration = 1.f;
			Heavy.AttackerSource = TEXT("Smoke");
			Frost->GetHealthComponent()->TakeDamage(Heavy);
			Check(State, Frost->IsKnockedDown() && KnockdownOf(Frost)->GetState().Cause == EKnockdownCause::HeavyHit, TEXT("Frostbitten: a >= 40 HP hit knocks it down"));
			FDamageSpec Plain;
			Plain.Amount = 30.f;
			Plain.DamageType = EDamageType::Energy; // x2 on the brute: 60 HP
			Plain.ArmorPenetration = 1.f;
			Plain.AttackerSource = TEXT("Smoke");
			Brute->GetHealthComponent()->TakeDamage(Plain);
			Check(State, !Brute->IsKnockedDown(), TEXT("Brute: a plain heavy hit does not floor it (poise)"));
			Plain.bIsCritical = true;
			Brute->GetHealthComponent()->TakeDamage(Plain);
			Check(State, Brute->IsKnockedDown(), TEXT("Brute: a critical heavy hit does"));
			Next();
			return true;
		}
		case 10:
		{
			AEnemyCharacter* Frost = State.Frostbitten.Get();
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			if (!State.bShotTaken)
			{
				State.bShotTaken = true;
				CheckClip(State, Frost, TEXT("Knocked_"), TEXT("Frostbitten fall clip on its one-shot slot"));
				CheckClip(State, State.Brute.Get(), TEXT("Knocked_"), TEXT("Brute fall clip"));
			}
			if (KnockdownOf(Frost)->GetPhase() != EKnockdownPhase::Downed)
			{
				return !Timeout(3.f, TEXT("Frostbitten never lay down")) || Finish(State, false);
			}
			Frost->GetHealthComponent()->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
			Next();
			return true;
		}
		case 11:
		{
			if (State.StageTime < (State.bShots ? 1.4f : 0.3f))
			{
				return true;
			}
			Shot(State, TEXT("4_EnemyDeathBack"));
			AEnemyCharacter* Frost = State.Frostbitten.Get();
			const UKnockdownComponent* Knock = KnockdownOf(Frost);
			Check(State, Knock && Knock->DiedWhileDown(), TEXT("Frostbitten died while down"));
			CheckClip(State, Frost, TEXT("Death_"), TEXT("Frostbitten death from the ground"));
			if (AEnemyCharacter* Brute = State.Brute.Get())
			{
				Brute->Destroy();
			}
			Next();
			return true;
		}
		case 12:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			if (!TurnBased->IsActive())
			{
				// Turn-based (after the screenshot frame of the enemy death).
				State.Frostbitten = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 450.f + FVector(0.f, 0.f, 30.f),
					(-Leader->GetActorForwardVector()).Rotation());
				const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
				Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
				State.StageTime = 0.f;
				return true;
			}
			if (State.StageTime < 0.5f || TurnBased->GetPhase() != ETurnPhase::Squad || TurnBased->IsBusy())
			{
				return !Timeout(5.f, TEXT("no squad turn")) || Finish(State, false);
			}
			State.TurnUnit = TurnBased->GetActiveUnit();
			const FTurnUnitState* Unit = TurnBased->GetUnitState(State.TurnUnit.Get());
			State.APBefore = Unit ? Unit->AP : 0;
			Check(State, KnockdownOf(State.TurnUnit.Get()) && KnockdownOf(State.TurnUnit.Get())->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion),
				FString::Printf(TEXT("turn-based: the active operative is knocked down (AP %d)"), State.APBefore));
			Check(State, !TurnBased->MoveActiveUnitTo(Unit ? Unit->GridPos + FIntPoint(1, 0) : FIntPoint::ZeroValue), TEXT("no grid move while down"));
			Next();
			return true;
		}
		case 13:
		{
			const UKnockdownComponent* Knock = KnockdownOf(State.TurnUnit.Get());
			if (Knock->GetPhase() != EKnockdownPhase::GettingUp)
			{
				return !Timeout(4.f, TEXT("turn-based get-up never started")) || Finish(State, false);
			}
			const FTurnUnitState* Unit = TurnBased->GetUnitState(State.TurnUnit.Get());
			Check(State, Unit && Unit->AP == State.APBefore - 2, FString::Printf(TEXT("getting up cost 2 AP (%d -> %d)"), State.APBefore, Unit ? Unit->AP : -1));
			Next();
			return true;
		}
		case 14:
		{
			UKnockdownComponent* Knock = KnockdownOf(State.TurnUnit.Get());
			if (Knock->IsDown())
			{
				return !Timeout(4.f, TEXT("turn-based get-up never ended")) || Finish(State, false);
			}
			if (TurnBased->GetActiveUnit() != State.TurnUnit.Get() || TurnBased->IsBusy())
			{
				Check(State, false, TEXT("the same operative is still active after his get-up"));
				return Finish(State, false);
			}
			const_cast<FTurnUnitState*>(TurnBased->GetUnitState(State.TurnUnit.Get()))->AP = 1;
			Check(State, Knock->ForceKnockDown(EKnockdownDirection::Front, EKnockdownCause::Explosion), TEXT("knocked down again with 1 AP left"));
			Next();
			return true;
		}
		case 15:
		{
			const UKnockdownComponent* Knock = KnockdownOf(State.TurnUnit.Get());
			if (TurnBased->GetActiveUnit() == State.TurnUnit.Get() && TurnBased->GetPhase() == ETurnPhase::Squad)
			{
				return !Timeout(4.f, TEXT("1 AP: the turn was never skipped")) || Finish(State, false);
			}
			Check(State, Knock->GetPhase() == EKnockdownPhase::Downed, TEXT("1 AP: turn skipped, he stays down"));
			Flow->ExitTurnBasedToRealTime();
			Next();
			return true;
		}
		case 16:
		{
			const UKnockdownComponent* Knock = KnockdownOf(State.TurnUnit.Get());
			if (Knock->IsDown())
			{
				return !Timeout(5.f, TEXT("back in real time he never got up")) || Finish(State, false);
			}
			Check(State, Flow->GetCombatMode() != ECodexCombatMode::TurnBased, TEXT("back in real time the downed timer got him up"));
			// A blast behind a squad mate: on his face, then killed while down.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != State.TurnUnit.Get() && Member->HealthComponent->IsAlive() && !Member->IsKnockedDown())
				{
					State.Mate = Member;
					break;
				}
			}
			AOperativeCharacter* Mate = State.Mate.Get();
			if (!Mate)
			{
				Check(State, false, TEXT("a squad mate for the blast"));
				return Finish(State, false);
			}
			const int32 Fallen = UKnockdownComponent::NotifyExplosion(World, Mate->GetActorLocation() - Mate->GetActorForwardVector() * 150.f);
			const UKnockdownComponent* Knock2 = KnockdownOf(Mate);
			Check(State, Fallen >= 1 && Knock2->IsDown() && Knock2->GetDirection() == EKnockdownDirection::Front
				&& Knock2->GetState().Cause == EKnockdownCause::Explosion, TEXT("blast 1.5 m behind: knocked down on his face"));
			Next();
			return true;
		}
		case 17:
		{
			AOperativeCharacter* Mate = State.Mate.Get();
			if (KnockdownOf(Mate)->GetPhase() != EKnockdownPhase::Downed)
			{
				return !Timeout(3.f, TEXT("squad mate never lay down")) || Finish(State, false);
			}
			CheckClip(State, Mate, TEXT("Knocked_Front"), TEXT("face-down fall held"));
			Mate->HealthComponent->ApplyDirectHealthLoss(100000.f, TEXT("Smoke"));
			Next();
			return true;
		}
		case 18:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (!State.bShotTaken)
			{
				State.bShotTaken = true;
				AOperativeCharacter* Mate = State.Mate.Get();
				Check(State, KnockdownOf(Mate)->DiedWhileDown(), TEXT("squad mate died while down"));
				CheckClip(State, Mate, TEXT("Death_Front"), TEXT("death from the ground"));
			}
			if (!State.bShots)
			{
				return Finish(State, true);
			}
			if (State.StageTime >= 1.4f && !State.bFinalShot)
			{
				State.bFinalShot = true;
				Shot(State, TEXT("5_DeathFront"));
			}
			return State.StageTime < 2.f ? true : Finish(State, true);
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
		State->bShots = Args.Contains(TEXT("shot"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.KnockdownSmoke"),
		TEXT("Dev check (Sprint 14): knockdown fall / downed bar / pause freeze / get-up / buffered order, enemy poise and death "
			"while down, turn-based 2 AP get-up and skipped turn; 'shot' saves Knockdown_*.png. Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
