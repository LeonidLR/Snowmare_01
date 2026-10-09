// Dev-only console command for a headless Gorky 17 turn-based check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedSmoke
// 1. a wave starts, one brute 6 m ahead; Space-hold equivalent enters turn-based: squad and enemy on the grid, overlay;
// 2. stance change costs 1 AP; 3. the squad passes the turn: the enemy walks up, bites (attack clip), steps back,
// round 2 starts;
// 4. an operative moves into a fire lane and shoots the (weakened) enemy -> victory -> tactical pause.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/MeshComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/GorkyLineOfSight.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/TurnBasedHudWidget.h"

namespace TurnBasedSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		TWeakObjectPtr<AEnemyCharacter> FarEnemy;
		FIntPoint EnemyCellBefore = FIntPoint::ZeroValue;
		float SquadHealthBefore = 0.f;
		/** The biting enemy played its attack clip (Godot play_tactical_attack). */
		bool bAttackClipSeen = false;
		/** User request 2026-10-09: the grid shot is shown after the turn (TurnAttackTimeline). */
		bool bShotShown = false;
		float ShotBodyLagDeg = 999.f;
		float ShotWaitSeconds = 0.f;
		float TurnAwayDeg = 0.f;
		bool bDamageBeforeShot = false;
		float ShotTrauma = 0.f;
		float EnemyHealthAtOrder = 0.f;
		FDelegateHandle ShotHandle;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** First mesh of the actor that has materials (the character mesh may be empty on placeholder enemies). */
	const UMeshComponent* VisibleMesh(const AActor* Actor)
	{
		TArray<UMeshComponent*> Meshes;
		if (Actor)
		{
			Actor->GetComponents<UMeshComponent>(Meshes);
		}
		for (const UMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetNumMaterials() > 0)
			{
				return Mesh;
			}
		}
		return nullptr;
	}

	float SquadHealth(UWorld* World)
	{
		float Total = 0.f;
		for (const AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			Total += Member->HealthComponent->GetCurrentHealth();
		}
		return Total;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // keep the fight to our own brute
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			// 3 cells out: the brute (5 AP, a 3 AP blow — EnemyTurnRules) walks 2 and still hits on its first turn.
			State.Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 450.f + FVector(0.f, 0.f, 20.f));
			// A hound 20 m behind stays outside the fight (stasis look).
			State.FarEnemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Leader->GetActorLocation() - Leader->GetActorForwardVector() * 2000.f + FVector(0.f, 0.f, 20.f));
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			{
				const UMeshComponent* FarMesh = VisibleMesh(State.FarEnemy.Get());
				const UMeshComponent* GridMesh = VisibleMesh(State.Enemy.Get());
				Check(State, TurnBased->StasisMaterial && FarMesh && FarMesh->GetMaterial(0) == TurnBased->StasisMaterial
					&& GridMesh && GridMesh->GetMaterial(0) != TurnBased->StasisMaterial, TEXT("the enemy outside the fight is in stasis, the grid enemy is not"));
			}
			Check(State, TurnBased->GetSquadCount() == 3 && TurnBased->GetEnemyCount() == 1,
				FString::Printf(TEXT("squad %d / enemies %d on the grid"), TurnBased->GetSquadCount(), TurnBased->GetEnemyCount()));
			const FTurnUnitState* Active = TurnBased->GetUnitState(TurnBased->GetActiveUnit());
			Check(State, Active && Active->AP == 8, TEXT("active operative has 8 AP"));
			if (UTurnBasedHudWidget* Hud = CreateWidget<UTurnBasedHudWidget>(UGameplayStatics::GetPlayerController(World, 0), UTurnBasedHudWidget::StaticClass()))
			{
				Check(State, Hud->GetPhaseText().ToString() == TEXT("⚔️ SQUAD TURN") && Hud->GetApText().ToString() == TEXT("AP: 8/8"),
					TEXT("turn-based panel: squad phase, AP 8/8"));
			}
			Check(State, TurnBased->SetActiveUnitStance(EOperativeStance::Crouching) && Active->AP == 7, TEXT("stance change costs 1 AP"));
			State.EnemyCellBefore = TurnBased->GetUnitState(State.Enemy.Get())->GridPos;
			State.SquadHealthBefore = SquadHealth(World);
			TurnBased->PassSquadTurn();
			Check(State, TurnBased->GetPhase() != ETurnPhase::Squad, TEXT("squad turn passed"));
			Next(State);
			return true;
		}
		case 1: // Enemy phase runs (walk, bite with its attack clip, step back), then round 2.
			if (const AEnemyCharacter* Biter = State.Enemy.Get())
			{
				if (const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Biter->GetMesh()->GetAnimInstance()))
				{
					State.bAttackClipSeen |= Anim->bIsAttacking && Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) > 0.5f;
				}
			}
			// Sprint 14: the brute's blow knocks the bitten operative down; on his turn he gets up first (2 AP).
			if (TurnBased->GetRound() < 2 || TurnBased->GetPhase() != ETurnPhase::Squad || TurnBased->IsUnitMoving()
				|| TurnBased->IsActiveUnitKnockedDown())
			{
				return true;
			}
			Check(State, State.bAttackClipSeen, TEXT("the enemy played its attack clip for the bite"));
			{
				const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
				Check(State, EnemyState && EnemyState->GridPos != State.EnemyCellBefore, TEXT("the enemy moved on its turn"));
				Check(State, SquadHealth(World) < State.SquadHealthBefore, FString::Printf(TEXT("the enemy bit an operative (%.0f -> %.0f)"),
					State.SquadHealthBefore, SquadHealth(World)));
			}
			Next(State);
			return true;
		case 2: // Walk into a fire lane of the enemy.
		{
			const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
			const AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			if (!EnemyState || !UnitState || !Grid)
			{
				return Finish(State, false);
			}
			const FIntPoint Target = EnemyState->GridPos;
			bool bInLane = TurnBasedRules::IsTargetInPattern(Unit->CurrentWeapon, Target - UnitState->GridPos)
				&& GorkyLineOfSight::HasLineOfSight(UnitState->GridPos, Target, *Grid);
			if (!bInLane)
			{
				for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, UnitState->AP - 3))
				{
					if (Entry.Key != UnitState->GridPos && Grid->IsCellWalkable(Entry.Key)
						&& TurnBasedRules::IsTargetInPattern(Unit->CurrentWeapon, Target - Entry.Key)
						&& GorkyLineOfSight::HasLineOfSight(Entry.Key, Target, *Grid))
					{
						Check(State, TurnBased->MoveActiveUnitTo(Entry.Key), TEXT("operative walks into a fire lane"));
						break;
					}
				}
			}
			Next(State);
			return true;
		}
		case 3:
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			{
				const FTurnUnitState* EnemyState = TurnBased->GetUnitState(State.Enemy.Get());
				State.Enemy->GetHealthComponent()->ApplyDirectHealthLoss(State.Enemy->GetHealthComponent()->GetCurrentHealth() - 1.f, TEXT("Smoke")); // 1 HP: the brute's own armor cuts grid damage (Godot take_damage)
				TurnBased->bGuaranteeAllHits = true;
				// A rifleman turned ~100 deg away from the target: the shot must wait for the body's turn.
				AOperativeCharacter* Shooter = TurnBased->GetActiveUnit();
				const float ToTarget = (State.Enemy->GetActorLocation() - Shooter->GetActorLocation()).Rotation().Yaw;
				Shooter->SetActorRotation(FRotator(0.f, ToTarget + 100.f, 0.f));
				State.TurnAwayDeg = 100.f;
				State.EnemyHealthAtOrder = State.Enemy->GetHealthComponent()->GetCurrentHealth();
				FState* StatePtr = &State;
				State.ShotHandle = TurnBased->OnGridShotFired.AddLambda([StatePtr, World](AOperativeCharacter* By, AActor* At)
				{
					StatePtr->bShotShown = true;
					const APlayerController* LambdaPC = UGameplayStatics::GetPlayerController(World, 0);
					const ATacticalCameraPawn* LambdaCamera = LambdaPC ? Cast<ATacticalCameraPawn>(LambdaPC->GetPawn()) : nullptr;
					StatePtr->ShotTrauma = LambdaCamera ? LambdaCamera->GetShakeTrauma() : 0.f;
					StatePtr->ShotBodyLagDeg = By ? FMath::Abs(By->GetBodyYawLagDeg()) : 999.f;
					StatePtr->ShotWaitSeconds = static_cast<float>(World->GetTimeSeconds()) - StatePtr->ShotWaitSeconds;
					if (const AEnemyCharacter* Enemy = StatePtr->Enemy.Get())
					{
						StatePtr->bDamageBeforeShot = Enemy->GetHealthComponent()->GetCurrentHealth() < StatePtr->EnemyHealthAtOrder;
					}
				});
				State.ShotWaitSeconds = static_cast<float>(World->GetTimeSeconds());
				const FTurnAttackResult Attack = TurnBased->AttackCell(EnemyState->GridPos);
				Check(State, Attack.bSuccess && Attack.bHit && Attack.Damage > 0, FString::Printf(TEXT("shot hits for %d (reason %s)"), Attack.Damage, *Attack.Reason));
				Check(State, Attack.bPending && TurnBased->IsShotPending() && TurnBased->IsActive() && State.Enemy->GetHealthComponent()->GetCurrentHealth() >= State.EnemyHealthAtOrder,
					TEXT("ordered while turned away: the shot waits for the turn (no damage at the click)"));
			}
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		case 4:
			if (!State.bShotShown && State.StageTime < 4.f)
			{
				return true;
			}
			TurnBased->OnGridShotFired.Remove(State.ShotHandle);
			Check(State, State.bShotShown && State.ShotBodyLagDeg <= 5.f && !State.bDamageBeforeShot && State.ShotWaitSeconds >= 0.15f,
				FString::Printf(TEXT("the shot is shown after the %.0f deg turn: body %.1f deg off at the shot, %.2f s after the order, damage only after it"),
					State.TurnAwayDeg, State.ShotBodyLagDeg, State.ShotWaitSeconds));
			{
				const APlayerController* ShotPC = UGameplayStatics::GetPlayerController(World, 0);
				const ATacticalCameraPawn* ShotCamera = ShotPC ? Cast<ATacticalCameraPawn>(ShotPC->GetPawn()) : nullptr;
				Check(State, ShotCamera && State.ShotTrauma > 0.3f, TEXT("the shot shakes the camera at the shot (Godot trigger_weapon_shake)"));
				Check(State, !TurnBased->IsActive(), TEXT("last enemy down -> combat over"));
				Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("victory returns to the tactical pause"));
				const UMeshComponent* FarMesh = VisibleMesh(State.FarEnemy.Get());
				Check(State, TurnBased->GetStasisMeshCount() == 0 && FarMesh && FarMesh->GetMaterial(0) != TurnBased->StasisMaterial,
					TEXT("stasis look restored after the fight"));
			}
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
		TEXT("CodexTactics.TurnBasedSmoke"),
		TEXT("Dev check: Gorky 17 turn-based combat start, stance AP, enemy turn, move + shot, victory; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
