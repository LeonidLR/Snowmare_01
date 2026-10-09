// Dev-only console command (user request 2026-10-09): the Medic-Sapper is the Female Soldier and carries the sniper rifle.
//   Scripts/smoke.ps1 -Command CodexTactics.SniperSmoke
// 1. body: SK_Female_soldier with ABP_Operative (others keep their mesh), feet on the capsule bottom, hand_r weapon socket;
//    arsenal: only she has the sniper rifle, right after the M16 in the X cycle; switch: M16 model, clip 5 / reserve 15.
// 2. standing idle with the rifle (Passive posture, enemy in sight): no shot, the stand aim idle loop.
// 3. order (priority target) while standing: she kneels by herself (AS_Knee_Aim_Start), then fires (AS_Knee_Aim_Fire),
//    the bolt follows (AS_Knee_Aim_Bullet_Reload); every shot kneeling / prone and still.
// 4. order while walking: she stops, then fires; Aggressive auto fire while walking under a move order: no shot, no stop;
//    arrived (standing still) she kneels by herself and fires.
// 5. prone: fires prone (stays prone, AS_Prone_Aim_Fire); last round -> magazine reload clip, clip refilled from the reserve.
// 6. Ctrl + click a barrel while standing: waits for the kneel, then the barrel explodes.
// 7. turn-based: standing the attack costs kneel 1 + shot 3 AP; with 3 AP it is not offered / refused; with the AP she kneels
//    (stance on the grid too) and fires.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/CoreDelegates.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
#include "TimerManager.h"

namespace SniperSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<ABarrelActor> Barrel;
		int32 Shots = 0;
		int32 IllegalShots = 0;
		int32 ShotsAtStageStart = 0;
		FString LastShotStance;
		int32 KneelsBefore = 0;
		int32 StopsBefore = 0;
		bool bSawMovingUnderAutoFire = false;
		int32 ReserveBefore = 0;
		int32 KneelClipsBefore = 0;
		int32 KneelFireClipsBefore = 0;
		FDelegateHandle FiredHandle;
		// Per-frame traces (OnEndFrame): the mesh's world yaw (pops), the FullBody slot weight in the sniper sequence (legs locked).
		FDelegateHandle FrameHandle;
		bool bTraceYaw = false;
		float LastMeshYaw = 0.f;
		bool bHasLastMeshYaw = false;
		float MaxMeshYawJump = 0.f;
		/** Cover entry: when she entered (world time) and the FullBody slot's lowest weight 0.1-0.4 s later. */
		double CoverEnteredAt = -1.0;
		float EntrySlotMin = 1.f;
		bool bTraceSlot = false;
		float MinSlotWeight = 1.f;
		float ShotYaw = 0.f;
		float MaxYawDrift = 0.f;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FCoverSlot Corner;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FCoreDelegates::OnEndFrame.Remove(State.FrameHandle);
		if (AOperativeCharacter* Medic = State.Medic.Get())
		{
			Medic->OnWeaponFiredNative.Remove(State.FiredHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SniperSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke stage %d"), State.Stage);
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->GetMesh() ? Cast<UOperativeAnimInstance>(Operative->GetMesh()->GetAnimInstance()) : nullptr;
	}

	bool ClipLogged(const AOperativeCharacter* Operative, const TCHAR* Name)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		return Anim && Anim->GetSniperClipLog().Contains(FString(Name));
	}

	int32 ClipCount(const AOperativeCharacter* Operative, const TCHAR* Name)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		int32 Count = 0;
		for (const FString& Clip : Anim ? Anim->GetSniperClipLog() : TArray<FString>())
		{
			Count += Clip == Name ? 1 : 0;
		}
		return Count;
	}

	FString ClipLog(const AOperativeCharacter* Operative)
	{
		const UOperativeAnimInstance* Anim = AnimOf(Operative);
		return Anim ? FString::Join(Anim->GetSniperClipLog(), TEXT(", ")) : FString(TEXT("-"));
	}

	/** Nobody else shoots; the target stays put with plenty of health; stray wave enemies go. */
	void KeepScene(UWorld* World, FState& State, bool bMedicMayFire)
	{
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			Member->bTacticalCeaseFire = Member != State.Medic.Get() || !bMedicMayFire;
		}
		const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		if (TurnBased && TurnBased->IsActive())
		{
			return;
		}
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (*It != State.Enemy.Get())
			{
				It->Destroy();
			}
		}
		if (AEnemyCharacter* Enemy = State.Enemy.Get())
		{
			Enemy->GetCharacterMovement()->DisableMovement();
			Enemy->GetHealthComponent()->Heal(100000.f);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d (clips: %s)"), State.Stage, *ClipLog(State.Medic.Get()));
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Medic = State.Medic.Get();
		if (State.Stage > 0 && (!Medic || !State.Enemy.IsValid()))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: medic or target gone"));
			return Finish(State, false);
		}
		if (State.Stage > 0)
		{
			KeepScene(World, State, State.Stage >= 3);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const USkeletalMesh* Body = Member->GetMesh()->GetSkeletalMeshAsset();
				const bool bFemale = Body && Body->GetName() == TEXT("SK_Female_soldier");
				const bool bHasSniper = Member->AvailableWeapons.ContainsByPredicate([](const UWeaponDataAsset* W) { return W && W->IsSniperRifle(); });
				if (Member->SquadRole == EOperativeRole::MedicSapper)
				{
					State.Medic = Member;
					Check(State, bFemale, FString::Printf(TEXT("Medic-Sapper body = SK_Female_soldier (%s)"), Body ? *Body->GetName() : TEXT("none")));
					Check(State, bHasSniper, TEXT("Medic-Sapper carries the sniper rifle"));
				}
				else
				{
					Check(State, !bFemale && !bHasSniper, FString::Printf(TEXT("%s keeps his body and has no sniper rifle"), *Member->DisplayName.ToString()));
				}
			}
			Medic = State.Medic.Get();
			if (!Medic)
			{
				return Finish(State, false);
			}
			const UClass* AnimClass = Medic->GetMesh()->GetAnimClass();
			Check(State, AnimClass && AnimClass->GetName().Contains(TEXT("ABP_Operative")) && AnimOf(Medic),
				FString::Printf(TEXT("same AnimBP (%s)"), AnimClass ? *AnimClass->GetName() : TEXT("none")));
			const float FeetZ = Medic->GetActorLocation().Z - Medic->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(State, FMath::Abs(Medic->GetMesh()->GetComponentLocation().Z - FeetZ) < 5.f,
				FString::Printf(TEXT("feet on the ground: mesh origin %.1f cm from the capsule bottom"), Medic->GetMesh()->GetComponentLocation().Z - FeetZ));
			Check(State, Medic->GetMesh()->DoesSocketExist(Medic->WeaponSocket) && Medic->WeaponMesh->GetAttachSocketName() == Medic->WeaponSocket,
				TEXT("rifle on the female hand_r"));
			const UStaticMesh* M16Model = Medic->WeaponMesh->GetStaticMesh();
			const int32 SniperIndex = Medic->AvailableWeapons.IndexOfByPredicate([](const UWeaponDataAsset* W) { return W && W->IsSniperRifle(); });
			Check(State, Medic->CurrentWeapon && Medic->CurrentWeapon->WeaponId == TEXT("m16") && SniperIndex == 1,
				TEXT("M16 in hands, the sniper rifle next in the X cycle"));
			const UWeaponDataAsset* Sniper = SniperIndex != INDEX_NONE ? Medic->AvailableWeapons[SniperIndex].Get() : nullptr;
			Check(State, Sniper && Medic->SwitchToWeaponById(Sniper->WeaponId) && Medic->IsSniperWeaponEquipped(), TEXT("switched to the sniper rifle"));
			Check(State, Medic->CurrentClip == 5 && Medic->ReserveAmmo == 15 && Sniper && Sniper->FireRate >= 2.f && Sniper->BaseDamage >= 60.f,
				FString::Printf(TEXT("sniper [%d/%d], damage %.0f, %.1f s per shot (weapons_tuning.json)"), Medic->CurrentClip, Medic->ReserveAmmo,
					Sniper ? Sniper->BaseDamage : 0.f, Sniper ? Sniper->FireRate : 0.f));
			Check(State, Medic->WeaponMesh->GetStaticMesh() == M16Model && M16Model, TEXT("stand-in model: the M16 (HandMesh)"));
			State.FiredHandle = Medic->OnWeaponFiredNative.AddLambda([StateRef](AOperativeCharacter* Shooter, AActor*, bool)
			{
				FState& S = *StateRef;
				++S.Shots;
				const bool bMoving = Shooter->GetVelocity().Size2D() > 20.f;
				S.LastShotStance = AOperativeCharacter::GetStanceDisplayName(Shooter->GetStance()).ToString();
				if (Shooter->GetStance() == EOperativeStance::Standing || bMoving)
				{
					++S.IllegalShots;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot %d: %s, speed %.0f"), S.Shots, *S.LastShotStance, Shooter->GetVelocity().Size2D());
			});
			// The fight: one target 11 m ahead, frozen and unkillable; nobody else fires.
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetLeader(Medic);
			const FVector From = Medic->GetActorLocation();
			const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorForwardVector() * 1100.f);
			State.Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, Spot + FVector(0.f, 0.f, 20.f));
			if (AEnemyCharacter* Enemy = State.Enemy.Get())
			{
				Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
			}
			Check(State, State.Enemy.IsValid(), TEXT("target spawned 11 m out"));
			Next(State);
			return true;
		}
		case 1: // Standing idle with the rifle: allowed, no shot (Passive posture).
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, State.Shots == 0 && Medic->GetStance() == EOperativeStance::Standing, TEXT("standing idle with the sniper rifle: no shot"));
			Check(State, AnimOf(Medic) && AnimOf(Medic)->bSniperPose && ClipLogged(Medic, TEXT("AS_Stand_Aim_Idle")),
				FString::Printf(TEXT("sniper stand idle plays (clips: %s)"), *ClipLog(Medic)));
			State.KneelsBefore = Medic->GetSniperKneels();
			Medic->FaceAimAt(State.Enemy->GetActorLocation());
			Medic->SetManualPriorityTarget(State.Enemy.Get());
			Next(State);
			return true;
		case 2: // Allow the fire: the order while standing -> she kneels, then fires.
			Next(State);
			return true;
		case 3:
			if (State.Shots < 1 && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.Shots >= 1, FString::Printf(TEXT("ordered shot fired (%.1f s)"), State.StageTime));
			Check(State, Medic->GetSniperKneels() == State.KneelsBefore + 1 && Medic->GetStance() == EOperativeStance::Crouching,
				TEXT("she knelt by herself before the shot"));
			Check(State, ClipLogged(Medic, TEXT("AS_Knee_Aim_Start")) && ClipLogged(Medic, TEXT("AS_Knee_Aim_Fire")),
				FString::Printf(TEXT("kneel clip then the kneeling shot (clips: %s)"), *ClipLog(Medic)));
			Next(State);
			return true;
		case 4: // The bolt after the shot.
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, ClipLogged(Medic, TEXT("AS_Knee_Aim_Bullet_Reload")), FString::Printf(TEXT("bolt worked after the shot (clips: %s)"), *ClipLog(Medic)));
			// Order while walking: stand up, walk off, then the priority target again.
			Medic->SetManualPriorityTarget(nullptr);
			Medic->bTacticalCeaseFire = true;
			Medic->SetStance(EOperativeStance::Standing);
			Next(State);
			return true;
		case 5:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			{
				const FVector From = Medic->GetActorLocation();
				const FVector Goal = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorRightVector() * 700.f);
				Check(State, Medic->OrderMoveTo(Goal, false) == EOperativeOrderResult::Accepted, TEXT("walk order"));
			}
			Next(State);
			return true;
		case 6:
			if (State.StageTime < 0.8f)
			{
				return true;
			}
			Check(State, Medic->IsMoving(), TEXT("she walks"));
			State.StopsBefore = Medic->GetSniperStops();
			State.ShotsAtStageStart = State.Shots;
			Medic->SetManualPriorityTarget(State.Enemy.Get());
			Next(State);
			return true;
		case 7:
			if (State.Shots == State.ShotsAtStageStart && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Medic->GetSniperStops() == State.StopsBefore + 1, TEXT("ordered while walking: she stopped first"));
			Check(State, State.Shots > State.ShotsAtStageStart && State.IllegalShots == 0,
				FString::Printf(TEXT("then she fired (stance %s), no shot standing / moving so far"), *State.LastShotStance));
			// Aggressive auto fire while walking under a move order: no shot, no stop.
			Medic->SetManualPriorityTarget(nullptr);
			Medic->SetStance(EOperativeStance::Standing);
			Next(State);
			return true;
		case 8:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			{
				const FVector From = Medic->GetActorLocation();
				const FVector Goal = SmokeUtils::ClearPoint(World, From, From - Medic->GetActorRightVector() * 600.f);
				Medic->OrderMoveTo(Goal, false);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive); // only once she walks: standing still she would kneel and fire
			State.ShotsAtStageStart = State.Shots;
			State.StopsBefore = Medic->GetSniperStops();
			State.KneelsBefore = Medic->GetSniperKneels();
			State.bSawMovingUnderAutoFire = false;
			Next(State);
			return true;
		case 9:
			State.bSawMovingUnderAutoFire |= Medic->IsMoving() && Medic->GetStance() == EOperativeStance::Standing;
			if (Medic->IsMoving())
			{
				if (State.Shots != State.ShotsAtStageStart || Medic->GetSniperStops() != State.StopsBefore)
				{
					Check(State, false, TEXT("auto fire while walking: no shot and no stop"));
				}
				if (State.StageTime < 12.f)
				{
					return true;
				}
			}
			Check(State, State.bSawMovingUnderAutoFire && Medic->GetSniperStops() == State.StopsBefore,
				TEXT("Aggressive posture while walking: no stop, no shot"));
			State.ShotsAtStageStart = State.Shots;
			Next(State);
			return true;
		case 10: // Arrived: she kneels by herself and the auto fire shoots.
			if (State.Shots == State.ShotsAtStageStart && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.Shots > State.ShotsAtStageStart && Medic->GetSniperKneels() > State.KneelsBefore && State.IllegalShots == 0,
				TEXT("arrived: auto fire kneels by herself, then fires"));
			// Prone: the last round -> the magazine reload.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Medic->SetStance(EOperativeStance::Prone);
			Medic->CurrentClip = 1;
			State.ReserveBefore = Medic->ReserveAmmo;
			Next(State);
			return true;
		case 11:
			if (State.StageTime < 3.5f)
			{
				return true;
			}
			State.ShotsAtStageStart = State.Shots;
			Medic->SetManualPriorityTarget(State.Enemy.Get());
			Next(State);
			return true;
		case 12:
			if (State.Shots == State.ShotsAtStageStart && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.Shots > State.ShotsAtStageStart && Medic->GetStance() == EOperativeStance::Prone && State.LastShotStance == TEXT("PRONE"),
				TEXT("prone shot, she stays prone"));
			Check(State, ClipLogged(Medic, TEXT("AS_Prone_Aim_Fire")), TEXT("prone fire clip"));
			Next(State);
			return true;
		case 13:
			if (Medic->bIsReloading && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, ClipLogged(Medic, TEXT("AS_Prone_Aim_Magazine_Reload")), FString::Printf(TEXT("prone magazine reload clip (clips: %s)"), *ClipLog(Medic)));
			Check(State, Medic->CurrentClip == 5 && Medic->ReserveAmmo == State.ReserveBefore - 5,
				FString::Printf(TEXT("magazine refilled [%d/%d]"), Medic->CurrentClip, Medic->ReserveAmmo));
			// Ctrl + click a barrel while standing: the shot waits for the kneel.
			Medic->SetManualPriorityTarget(nullptr);
			Medic->SetStance(EOperativeStance::Standing);
			Next(State);
			return true;
		case 14:
			if (State.StageTime < 3.f)
			{
				return true;
			}
			{
				const FVector From = Medic->GetActorLocation();
				const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Medic->GetActorForwardVector() * 800.f);
				State.Barrel = World->SpawnActor<ABarrelActor>(Spot + FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator);
				Medic->bTacticalCeaseFire = false;
				const bool bFiredAtOnce = State.Barrel.IsValid() && Medic->ShootAtObject(State.Barrel.Get());
				Check(State, State.Barrel.IsValid() && !bFiredAtOnce && Medic->GetSniperPendingObject() == State.Barrel.Get(),
					TEXT("Ctrl + click standing: no shot yet, the barrel shot waits for the kneel"));
			}
			State.ShotsAtStageStart = State.Shots;
			Next(State);
			return true;
		case 15:
			if (State.Shots == State.ShotsAtStageStart && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, State.Shots > State.ShotsAtStageStart && Medic->GetStance() == EOperativeStance::Crouching && !Medic->GetSniperPendingObject(),
				TEXT("knelt, then the barrel shot"));
			Check(State, State.IllegalShots == 0, FString::Printf(TEXT("real time: %d shots, none standing / moving"), State.Shots));
			// Turn-based.
			Medic->SetStance(EOperativeStance::Standing);
			Next(State);
			return true;
		case 16:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			if (!TurnBased->IsActive())
			{
				return Finish(State, false);
			}
			TurnBased->SelectUnit(Medic);
			FTurnUnitState* Unit = const_cast<FTurnUnitState*>(TurnBased->GetUnitState(Medic));
			Check(State, TurnBased->GetActiveUnit() == Medic && Unit && Unit->Stance == EOperativeStance::Standing, TEXT("medic active, standing"));
			if (!Unit)
			{
				return Finish(State, false);
			}
			Check(State, TurnBased->GetActiveAttackCost() == 4, FString::Printf(TEXT("standing sniper attack = kneel 1 + shot 3 = %d AP"), TurnBased->GetActiveAttackCost()));
			Unit->AP = 3;
			TurnBased->EnterAttackMode();
			Check(State, !TurnBased->IsAttackMode(), TEXT("3 AP standing: the sniper attack is not offered"));
			const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
			const FTurnAttackResult Refused = EnemyState ? TurnBased->AttackCell(EnemyState->GridPos) : FTurnAttackResult();
			Check(State, !Refused.bSuccess && Refused.Reason == TEXT("not_enough_ap") && Unit->Stance == EOperativeStance::Standing && Unit->AP == 3,
				FString::Printf(TEXT("3 AP standing: the shot is refused (%s), no AP spent"), *Refused.Reason));
			Unit->AP = 8;
			// Into a fire lane if needed (walking keeps her standing).
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			if (EnemyState && Grid && !(TurnBasedRules::IsTargetInPattern(Medic->CurrentWeapon, EnemyState->GridPos - Unit->GridPos)
				&& GorkyLineOfSight::HasLineOfSight(Unit->GridPos, EnemyState->GridPos, *Grid)))
			{
				for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(Unit->GridPos, Unit->AP - 4))
				{
					if (Entry.Key != Unit->GridPos && Grid->IsCellWalkable(Entry.Key)
						&& TurnBasedRules::IsTargetInPattern(Medic->CurrentWeapon, EnemyState->GridPos - Entry.Key)
						&& GorkyLineOfSight::HasLineOfSight(Entry.Key, EnemyState->GridPos, *Grid))
					{
						Check(State, TurnBased->MoveActiveUnitTo(Entry.Key), TEXT("walks into a fire lane"));
						break;
					}
				}
			}
			Next(State);
			return true;
		}
		case 17:
		{
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Medic);
			const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
			if (!Unit || !EnemyState)
			{
				return Finish(State, false);
			}
			const int32 ApBefore = Unit->AP;
			State.KneelClipsBefore = ClipCount(Medic, TEXT("AS_Knee_Aim_Start"));
			State.KneelFireClipsBefore = ClipCount(Medic, TEXT("AS_Knee_Aim_Fire"));
			const EOperativeStance StanceBefore = Unit->Stance;
			TurnBased->bGuaranteeAllHits = true;
			const FTurnAttackResult Attack = TurnBased->AttackCell(EnemyState->GridPos);
			// User report 2026-10-09: after a grid shot she snapped back to the old facing. A parked follower carries an idle
			// facing from real time; the grid must win (UpdateCombatFacing clears it while turn-based is on).
			State.ShotYaw = Medic->GetActorRotation().Yaw;
			State.MaxYawDrift = 0.f;
			Medic->SetIdleFacingYaw(State.ShotYaw - 90.f);
			Check(State, StanceBefore == EOperativeStance::Standing && Attack.bSuccess && Attack.bHit,
				FString::Printf(TEXT("turn-based sniper shot from standing (reason %s)"), *Attack.Reason));
			Check(State, Unit->Stance == EOperativeStance::Crouching && Medic->GetStance() == EOperativeStance::Crouching && Unit->AP == ApBefore - 4,
				FString::Printf(TEXT("she knelt for it on the grid: AP %d -> %d"), ApBefore, Unit->AP));
			Next(State);
			return true;
		}
		case 18:
			State.MaxYawDrift = FMath::Max(State.MaxYawDrift, FMath::Abs(FRotator::NormalizeAxis(Medic->GetActorRotation().Yaw - State.ShotYaw)));
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, State.MaxYawDrift < 5.f, FString::Printf(TEXT("turn-based: she keeps facing the target after the shot (max drift %.1f deg)"), State.MaxYawDrift));
			Check(State, ClipCount(Medic, TEXT("AS_Knee_Aim_Start")) > State.KneelClipsBefore && ClipCount(Medic, TEXT("AS_Knee_Aim_Fire")) > State.KneelFireClipsBefore,
				FString::Printf(TEXT("turn-based: kneel clip then the shot clip (clips: %s)"), *ClipLog(Medic)));
			Flow->ExitTurnBasedToRealTime();
			Next(State);
			return true;
		case 19: // Cover entry (user report 2026-10-09: two pops on the way in): a 6 x 3 m wall 4 m ahead, run to its corner.
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Medic->SetManualPriorityTarget(nullptr);
			Medic->SetStance(EOperativeStance::Standing);
			State.F = Medic->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Medic->GetActorLocation();
			const float GroundZ = State.P.Z - Medic->GetSimpleCollisionHalfHeight();
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				const FVector Centre = State.P + State.F * 400.f;
				const FTransform Xf(State.F.Rotation(), FVector(Centre.X, Centre.Y, GroundZ + 150.f), FVector(0.4f, 6.f, 3.f));
				if (AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Xf, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
					Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
					Block->FinishSpawning(Xf);
				}
			}
			// The target beyond the wall, round its right-hand corner.
			// (a fresh frozen Frostbitten as in PhoneShots snipercover; the turn-based brute goes)
			if (AEnemyCharacter* Beyond = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				State.P + State.F * 1500.f - State.R * 650.f + FVector(0.f, 0.f, 60.f)))
			{
				Beyond->GetHealthComponent()->SetMaxHealth(100000.f);
				Beyond->CustomTimeDilation = 0.f;
				State.Enemy->Destroy();
				State.Enemy = Beyond;
			}
			Next(State);
			return true;
		}
		case 20:
		{
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			const bool bCorner = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f), State.F, State.Corner);
			Check(State, bCorner && State.Corner.Height == ECoverHeight::HighCover, TEXT("the wall's corner is high cover"));
			if (!bCorner)
			{
				return Finish(State, false);
			}
			State.bHasLastMeshYaw = false;
			State.MaxMeshYawJump = 0.f;
			State.bTraceYaw = true;
			Check(State, Medic->OrderTakeCover(State.Corner, true) == EOperativeOrderResult::Accepted, TEXT("cover order (run)"));
			Next(State);
			return true;
		}
		case 21:
			if (!Medic->bInCover && State.StageTime < 8.f)
			{
				return true;
			}
			Next(State);
			return true;
		case 22: // 1.5 s settled at the wall
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.bTraceYaw = false;
			Check(State, Medic->bInCover, TEXT("she is in cover"));
			// The approach turns her to the wall (no body yaw jump on the way in); entering, the actor turns its back to the wall
			// at once, so the enter clip (first frame = the arrival pose) must cover the FullBody slot fast (no turned-round run pose).
			Check(State, State.MaxMeshYawJump < 25.f, FString::Printf(TEXT("run to the wall: max body yaw step %.1f deg per frame"), State.MaxMeshYawJump));
			Check(State, State.EntrySlotMin > 0.9f && Medic->GetCoverEntryTurnDeg() > 90.f, FString::Printf(TEXT("cover entry: turned %.0f deg, enter clip covers the body within 0.1 s (slot min %.2f in 0.1-0.4 s)"),
				Medic->GetCoverEntryTurnDeg(), State.EntrySlotMin));
			State.ShotsAtStageStart = State.Shots;
			State.KneelsBefore = Medic->GetSniperKneels();
			Medic->SetManualPriorityTarget(State.Enemy.Get());
			Next(State);
			return true;
		case 23:
			if (State.Shots == State.ShotsAtStageStart && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Medic->GetSniperKneels() > State.KneelsBefore && Medic->GetStance() == EOperativeStance::Crouching && Medic->bInCover,
				TEXT("ordered at the wall: she crouches in cover first"));
			Check(State, State.Shots > State.ShotsAtStageStart && State.IllegalShots == 0, TEXT("then fires from the crouched corner, never standing"));
			Check(State, State.MinSlotWeight > 0.97f, FString::Printf(TEXT("sniper sequence: FullBody slot never dipped (min %.2f) - the legs stay in the kneel / prone"), State.MinSlotWeight));
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->FrameHandle = FCoreDelegates::OnEndFrame.AddLambda([State]()
		{
			const AOperativeCharacter* Medic = State->Medic.Get();
			if (!Medic)
			{
				return;
			}
			if (State->bTraceYaw && Medic->bInCover)
			{
				const UOperativeAnimInstance* EntryAnim = AnimOf(Medic);
				const double Now = Medic->GetWorld()->GetTimeSeconds();
				if (State->CoverEnteredAt < 0.0)
				{
					State->CoverEnteredAt = Now;
				}
				if (EntryAnim && Now - State->CoverEnteredAt >= 0.1 && Now - State->CoverEnteredAt <= 0.4)
				{
					State->EntrySlotMin = FMath::Min(State->EntrySlotMin, EntryAnim->GetSlotMontageGlobalWeight(EntryAnim->FullBodySlot));
				}
			}
			if (State->bTraceYaw && !Medic->bInCover)
			{
				const float Yaw = Medic->GetMesh()->GetComponentRotation().Yaw;
				if (State->bHasLastMeshYaw)
				{
					State->MaxMeshYawJump = FMath::Max(State->MaxMeshYawJump, FMath::Abs(FRotator::NormalizeAxis(Yaw - State->LastMeshYaw)));
				}
				State->LastMeshYaw = Yaw;
				State->bHasLastMeshYaw = true;
			}
			// From her first shot through bolt / reload / next shot (stages 3-4 kneeling, 12-13 prone): no slot dip.
			const UOperativeAnimInstance* Anim = AnimOf(Medic);
			const bool bSequence = (State->Stage == 4 || State->Stage == 13) && Anim && Anim->bSniperPose && !Anim->IsPlayingStanceTransition();
			if (bSequence)
			{
				State->MinSlotWeight = FMath::Min(State->MinSlotWeight, Anim->GetSlotMontageGlobalWeight(Anim->FullBodySlot));
			}
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SniperSmoke"),
		TEXT("Dev check: Female Soldier Medic-Sapper + sniper rifle (kneel / prone only, stop / kneel before the shot, bolt, reload, turn-based AP); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
