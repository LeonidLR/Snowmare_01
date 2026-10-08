// Dev-only headless check: the aim offset in turn-based combat does not oscillate (user PIE video AnimOffset_Bug_01,
// 2026-10-08: with the 2D aim offset AO_Rifle_Aim wired, operatives jittered once they walked across grid cells; UE-only,
// no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.TurnBasedAimSmoke -Log Smoke-TurnBasedAim.log
// Turn-based fight with enemies around; each operative stands 2 s, walks 3-4 cells, stands 2 s. Every frame: AimYaw,
// AimPitch, AimOffsetAlpha and the barrel's yaw relative to the actor (the real pose, the AO included). Fails on a
// frame-to-frame AimYaw jump > 8 deg or more than 2 direction reversals per second (AimYaw or the barrel) - the jitter.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace TurnBasedAimSmoke
{
	/** One sampled signal: frame-to-frame jumps and direction reversals. */
	struct FSignal
	{
		bool bHas = false;
		float Last = 0.f;
		float LastDelta = 0.f;
		float MaxDelta = 0.f;
		int32 Reversals = 0;
		float Travel = 0.f;

		void Add(float Value, float Threshold = 0.5f)
		{
			if (bHas)
			{
				const float Delta = FRotator::NormalizeAxis(Value - Last);
				MaxDelta = FMath::Max(MaxDelta, FMath::Abs(Delta));
				Travel += FMath::Abs(Delta);
				if (FMath::Abs(Delta) >= Threshold)
				{
					if (LastDelta != 0.f && FMath::Sign(Delta) != FMath::Sign(LastDelta))
					{
						++Reversals;
					}
					LastDelta = Delta;
				}
			}
			Last = Value;
			bHas = true;
		}
	};

	struct FPhaseStats
	{
		FSignal AimYaw;
		FSignal AimPitch;
		FSignal Barrel;
		float Seconds = 0.f;
		float MaxAlpha = 0.f;
		float MaxAbsYaw = 0.f;
		int32 Frames = 0;
	};

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Time = 0.f;
		int32 Failures = 0;
		int32 UnitIndex = 0;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Units;
		FPhaseStats Stand;
		FPhaseStats Walk;
		FPhaseStats StandAfter;
		bool bMoveStarted = false;
		float LogTimer = 0.f;
		// Crouched corner case (AnimOffset_Bug_02): the leader crouched at the right-hand edge of a wall, hounds rushing in.
		FPhaseStats Corner;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		float SpawnTimer = 0.f;
		int32 Spawned = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Rushers;
		TWeakObjectPtr<AStaticMeshActor> Wall;
	};

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnBasedAimSmoke"));
		return false;
	}

	void Sample(FState& State, FPhaseStats& Stats, AOperativeCharacter& Op, float DeltaSeconds)
	{
		const UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		if (!Anim)
		{
			return;
		}
		Stats.Seconds += DeltaSeconds;
		++Stats.Frames;
		Stats.AimYaw.Add(Anim->AimYaw);
		Stats.AimPitch.Add(Anim->AimPitch);
		Stats.MaxAlpha = FMath::Max(Stats.MaxAlpha, Anim->AimOffsetAlpha);
		Stats.MaxAbsYaw = FMath::Max(Stats.MaxAbsYaw, FMath::Abs(Anim->AimYaw));
		float BarrelRel = 0.f;
		if (Op.WeaponMesh && Op.WeaponMesh->IsVisible())
		{
			const FVector Barrel = Op.WeaponMesh->GetComponentTransform().TransformVectorNoScale(Op.MuzzleOffset.GetSafeNormal());
			BarrelRel = FRotator::NormalizeAxis(static_cast<float>(Barrel.Rotation().Yaw) - static_cast<float>(Op.GetActorRotation().Yaw));
			const float Before = Stats.Barrel.Last;
			const bool bHad = Stats.Barrel.bHas;
			Stats.Barrel.Add(BarrelRel, 1.f);
			if (bHad && FMath::Abs(FRotator::NormalizeAxis(BarrelRel - Before)) > 20.f)
			{
				FString Clip;
				float MontageWeight = 0.f;
				float SlotWeight = 0.f;
				Anim->GetCoverPlayback(Clip, MontageWeight, SlotWeight);
				UE_LOG(LogCodexTactics, Display, TEXT("Trace %s: BARREL JUMP %.0f -> %.0f deg in one frame (clip %s w%.2f slot %.2f, in cover %d, aim %d, reloading %d, AimYaw %.1f alpha %.2f)"),
					*Op.DisplayName.ToString(), Before, BarrelRel, *Clip, MontageWeight, SlotWeight, Op.bInCover ? 1 : 0, Op.IsCornerAimActive() ? 1 : 0,
					Op.bIsReloading ? 1 : 0, Anim->AimYaw, Anim->AimOffsetAlpha);
			}
		}
		State.LogTimer -= DeltaSeconds;
		if (State.LogTimer <= 0.f)
		{
			State.LogTimer = 0.1f;
			UE_LOG(LogCodexTactics, Display, TEXT("Trace %s: AimYaw %6.1f (target %6.1f) pitch %5.1f alpha %.2f barrel %6.1f actor yaw %6.1f speed %3.0f twist-limit %.0f yawOn %d"),
				*Op.DisplayName.ToString(), Anim->AimYaw, Anim->AimYawTarget, Anim->AimPitch, Anim->AimOffsetAlpha, BarrelRel,
				Op.GetActorRotation().Yaw, Op.GetVelocity().Size2D(), Anim->AimYawClampDegrees, Anim->bAimOffsetYaw ? 1 : 0);
		}
	}

	void Report(FState& State, const FString& Who, const TCHAR* Name, const FPhaseStats& Stats)
	{
		const float Seconds = FMath::Max(Stats.Seconds, 0.01f);
		UE_LOG(LogCodexTactics, Display,
			TEXT("Smoke: %s %s - %.1f s, %d frames: AimYaw max step %.1f deg, %d reversals (%.1f/s), travel %.0f deg/s, max |yaw| %.0f; AimPitch max step %.1f, %d reversals; barrel max step %.1f deg, %d reversals (%.1f/s); alpha max %.2f"),
			*Who, Name, Stats.Seconds, Stats.Frames, Stats.AimYaw.MaxDelta, Stats.AimYaw.Reversals, Stats.AimYaw.Reversals / Seconds, Stats.AimYaw.Travel / Seconds, Stats.MaxAbsYaw,
			Stats.AimPitch.MaxDelta, Stats.AimPitch.Reversals, Stats.Barrel.MaxDelta, Stats.Barrel.Reversals, Stats.Barrel.Reversals / Seconds, Stats.MaxAlpha);
		Check(State, Stats.AimYaw.MaxDelta <= 8.f, FString::Printf(TEXT("%s %s: AimYaw changes smoothly (max %.1f deg per frame)"), *Who, Name, Stats.AimYaw.MaxDelta));
		Check(State, Stats.AimYaw.Reversals <= FMath::CeilToInt(2.f * Seconds), FString::Printf(TEXT("%s %s: AimYaw does not oscillate (%d reversals in %.1f s)"), *Who, Name, Stats.AimYaw.Reversals, Stats.Seconds));
		Check(State, Stats.AimYaw.Travel / Seconds <= 60.f, FString::Printf(TEXT("%s %s: the upper body does not sweep back and forth (%.0f deg/s of AimYaw travel)"), *Who, Name, Stats.AimYaw.Travel / Seconds));
		Check(State, Stats.Barrel.Reversals <= FMath::CeilToInt(2.f * Seconds) + 2, FString::Printf(TEXT("%s %s: the rifle does not jitter (%d barrel reversals in %.1f s)"), *Who, Name, Stats.Barrel.Reversals, Stats.Seconds));
	}

	bool Tick(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaSeconds)
	{
		DeltaSeconds = FMath::Min(DeltaSeconds, 0.1f);
		State.Time += DeltaSeconds;
		State.StageTime += DeltaSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 200.f)
		{
			if (World)
			{
				Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			}
			return World ? Finish(State) : false;
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
				It->Destroy();
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			const FVector F = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector R = FVector::CrossProduct(FVector::UpVector, F);
			const FVector Here = Leader->GetActorLocation();
			// Enemies around (front, both sides, one behind): targets switch sides as he walks.
			const FVector Spots[] = { Here + F * 900.f, Here + F * 600.f + R * 600.f, Here + F * 500.f - R * 700.f, Here - F * 700.f + R * 300.f };
			int32 Index = 0;
			for (const FVector& Spot : Spots)
			{
				if (AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(Index++ % 2 == 0 ? EEnemyArchetype::Brute : EEnemyArchetype::FrostHound,
					Spot + FVector(0.f, 0.f, 20.f)))
				{
					Enemy->GetHealthComponent()->SetMaxHealth(100000.f);
				}
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				Member->bTacticalCeaseFire = Member != Leader;
				State.Units.Add(Member);
			}
			// The crouched corner case first (real time): a 6 m x 3 m wall 4 m ahead, the right-hand corner slot.
			State.F = F;
			State.R = R;
			State.P = Here;
			State.GroundZ = Here.Z - Leader->GetSimpleCollisionHalfHeight();
			const FVector WallCentre = Here + F * 400.f;
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + 150.f), F.Rotation(), FVector(0.4f, 6.f, 3.f));
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f; // the turn-based enemies wait far off until the turn-based part
				It->SetActorTickEnabled(false);
				It->SetActorLocation(It->GetActorLocation() - F * 3000.f);
			}
			State.Stage = 10;
			State.StageTime = 0.f;
			return true;
		}
		case 10:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			FCoverSlot Corner;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f - State.R * 250.f + FVector(0.f, 0.f, 90.f), State.F, Corner);
			Check(State, bFound && Leader->OrderTakeCover(Corner, false) == EOperativeOrderResult::Accepted, TEXT("the leader takes the right-hand corner"));
			State.Stage = 11;
			State.StageTime = 0.f;
			return true;
		}
		case 11:
		{
			AOperativeCharacter* Leader = Squad->GetLeader();
			if ((!Leader->bInCover || State.StageTime < 2.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Leader->SetStance(EOperativeStance::Crouching);
			State.Stage = 12;
			State.StageTime = 0.f;
			return true;
		}
		case 12:
		{
			// Crouched corner hold, hounds rushing round the corner and along the open side (the video).
			AOperativeCharacter* Leader = Squad->GetLeader();
			State.SpawnTimer -= DeltaSeconds;
			if (State.Spawned < 8 && State.SpawnTimer <= 0.f && State.StageTime > 1.5f)
			{
				State.SpawnTimer = 0.9f;
				const int32 I = State.Spawned++;
				const FVector Where = I % 2 == 0 ? State.P + State.F * (1300.f + 60.f * I) - State.R * 380.f
					: State.P + State.F * (150.f + 40.f * I) - State.R * (1100.f + 50.f * I);
				if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
					FVector(Where.X, Where.Y, State.GroundZ + 100.f), (-State.F).Rotation()))
				{
					State.Rushers.Add(Hound);
				}
			}
			if (State.StageTime > 1.5f)
			{
				Sample(State, State.Corner, *Leader, DeltaSeconds);
			}
			if (State.StageTime < 13.f)
			{
				return true;
			}
			Check(State, Leader->GetStance() == EOperativeStance::Crouching, TEXT("crouched at the corner"));
			Report(State, Leader->DisplayName.ToString(), TEXT("crouched corner, hounds rushing"), State.Corner);
			for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Rushers)
			{
				if (Hound.IsValid())
				{
					Hound->Destroy();
				}
			}
			if (State.Wall.IsValid())
			{
				State.Wall->Destroy();
			}
			if (Leader->bInCover)
			{
				Leader->LeaveCover(TEXT("smoke: turn-based part"));
			}
			Leader->SetStance(EOperativeStance::Standing);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = false;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 1.f;
				It->SetActorTickEnabled(true);
				It->SetActorLocation(It->GetActorLocation() + State.F * 3000.f);
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			if (!TurnBased->IsActive())
			{
				return Finish(State);
			}
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Next operative: select, stand 2 s (sampled).
			if (State.UnitIndex >= State.Units.Num() || State.UnitIndex >= 2)
			{
				return Finish(State);
			}
			AOperativeCharacter* Op = State.Units[State.UnitIndex].Get();
			if (!Op || !TurnBased->SelectUnit(Op))
			{
				++State.UnitIndex;
				return true;
			}
			State.Stand = FPhaseStats();
			State.Walk = FPhaseStats();
			State.StandAfter = FPhaseStats();
			State.bMoveStarted = false;
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			AOperativeCharacter* Op = State.Units[State.UnitIndex].Get();
			if (!Op)
			{
				return Finish(State);
			}
			Sample(State, State.Stand, *Op, DeltaSeconds);
			if (State.StageTime < 2.f)
			{
				return true;
			}
			// The farthest reachable cell 3-4 cells away.
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Op);
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			FIntPoint Best = UnitState ? UnitState->GridPos : FIntPoint::ZeroValue;
			int32 BestScore = -1;
			if (UnitState && Grid)
			{
				for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, UnitState->AP))
				{
					const FIntPoint D = Entry.Key - UnitState->GridPos;
					const int32 Cells = FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y));
					const int32 Score = Cells <= 4 ? Cells * 10 + FMath::Min(FMath::Abs(D.X), FMath::Abs(D.Y)) : -1;
					if (Grid->IsCellWalkable(Entry.Key) && Score > BestScore)
					{
						BestScore = Score;
						Best = Entry.Key;
					}
				}
			}
			const bool bMoved = BestScore >= 30 && TurnBased->MoveActiveUnitTo(Best);
			Check(State, bMoved, FString::Printf(TEXT("%s walks %d cells"), *Op->DisplayName.ToString(), BestScore / 10));
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Op = State.Units[State.UnitIndex].Get();
			if (!Op)
			{
				return Finish(State);
			}
			const bool bMoving = TurnBased->IsUnitMoving() || Op->GetVelocity().SizeSquared2D() > 25.f;
			State.bMoveStarted |= bMoving;
			if (bMoving || (!State.bMoveStarted && State.StageTime < 2.f))
			{
				Sample(State, State.Walk, *Op, DeltaSeconds);
				if (State.StageTime < 20.f)
				{
					return true;
				}
			}
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			AOperativeCharacter* Op = State.Units[State.UnitIndex].Get();
			if (!Op)
			{
				return Finish(State);
			}
			Sample(State, State.StandAfter, *Op, DeltaSeconds);
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FString Who = Op->DisplayName.ToString();
			Report(State, Who, TEXT("standing"), State.Stand);
			Report(State, Who, TEXT("walking"), State.Walk);
			Report(State, Who, TEXT("standing after the walk"), State.StandAfter);
			++State.UnitIndex;
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaSeconds)
		{
			return Tick(WeakWorld, *State, DeltaSeconds);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnBasedAimSmoke"),
		TEXT("Dev check: the aim offset (AimYaw / AimPitch) does not oscillate in turn-based combat, standing and walking grid cells; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
