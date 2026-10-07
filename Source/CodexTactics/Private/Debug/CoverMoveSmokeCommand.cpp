// Dev-only headless check of moving into / out of cover (user-found bugs 2026-10-07; UE-only, no Godot reference):
//   Scripts/smoke.ps1 -Command CodexTactics.CoverMoveSmoke -Log Smoke-CoverMove.log
// A 6 m x 3 m wall is spawned 4 m ahead of the leader on L_MovementTest (nothing is saved).
//  1. Running into cover: the cover is entered before the slot, the enter clip blends in smoothly — sampled every frame:
//     the body (pelvis) never jumps back out from the wall (it popped 76 cm with the old hard stop), the enter clip's
//     weight ramps up without dropping, no Rifle2 stop clip between the run and the enter clip, the mesh offset returns
//     to zero.
//  2. Turn-based with the leader pressed against the wall: a grid walk away from it leaves the cover and walks — no cover
//     montage while moving (it slid across the grid in the cover pose), the anim speed follows the walk.
//  3. A grid walk onto a cover cell: the walk animation while moving, then the cover is entered on the cell.
// "CodexTactics.CoverMoveSmoke crouch" (user request 2026-10-07): the same with a 60 cm low cover and a crouch-walk into
// it â€” the crouched enter clip (crch_idle_fwd_to_cvr_crch_idle, starting 98 cm out), the crouched cover loop (cvr_crch_*).

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
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"

namespace CoverMoveSmoke
{
	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Op;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		bool bCrouch = false;
		// Run-in sampling.
		bool bEntered = false;
		float SinceEntry = 0.f;
		FVector LastPelvis = FVector::ZeroVector;
		bool bHavePelvis = false;
		float MaxBackJump = 0.f;
		float BackTravel = 0.f;
		float LastWeight = -1.f;
		float MaxWeightDrop = 0.f;
		float FirstWeight = -1.f;
		bool bSawStopClip = false;
		float EntryDistance = 0.f;
		float MaxOffset = 0.f;
		// Grid walks.
		int32 Samples = 0;
		int32 CoverPoseWhileMoving = 0;
		float MaxAnimSpeed = 0.f;
		bool bLeftCover = false;
		FString CoverClipSeen;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CoverMoveSmoke"));
		return false;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

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

	/** The cover pose on the FullBody slot: a cover montage (cvr_* / *_to_cvr_*) with weight. */
	bool PlaysCoverPose(const AOperativeCharacter& Op, FString& OutClip)
	{
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		if (const UOperativeAnimInstance* Anim = AnimOf(&Op))
		{
			Anim->GetCoverPlayback(OutClip, MontageWeight, SlotWeight);
		}
		return OutClip.Contains(TEXT("cvr")) && MontageWeight > 0.1f && SlotWeight > 0.1f;
	}

	/** One grid-walk sample: no cover pose while the body moves, the anim speed follows it. */
	void SampleGridWalk(FState& State, UTurnBasedCombatSubsystem& TurnBased)
	{
		AOperativeCharacter* Op = State.Op.Get();
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		const float Tactical = TurnBased.GetTacticalMoveSpeed(Op);
		State.bLeftCover |= !Op->bInCover;
		if (Tactical > 60.f)
		{
			++State.Samples;
			State.MaxAnimSpeed = FMath::Max(State.MaxAnimSpeed, Anim ? Anim->Speed : 0.f);
			FString Clip;
			if (PlaysCoverPose(*Op, Clip) || Op->bInCover)
			{
				++State.CoverPoseWhileMoving;
				State.CoverClipSeen = Clip.IsEmpty() ? FString(TEXT("bInCover")) : Clip;
			}
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 90.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = State.Op.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
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
			// Keep one wave enemy, parked far behind and frozen (an empty wave would be cleared at once).
			AEnemyCharacter* Kept = nullptr;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				if (!Kept && !It->IsDying())
				{
					Kept = *It;
				}
				else
				{
					It->Destroy();
				}
			}
			Op = Squad->GetLeader();
			State.Op = Op;
			if (!Op || !Kept)
			{
				Check(State, false, TEXT("leader and a wave enemy"));
				return Finish(State, false);
			}
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation();
			State.GroundZ = State.P.Z - Op->GetSimpleCollisionHalfHeight();
			Kept->SetActorLocation(State.P - State.F * 4000.f);
			Kept->CustomTimeDilation = 0.f;
			// A frozen hound behind the wall gives the grid fight its enemy.
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				State.P + State.F * 900.f + FVector(0.f, 0.f, 20.f), (-State.F).Rotation()))
			{
				Hound->CustomTimeDilation = 0.f;
				Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			}
			// The pose is refreshed every frame even without rendering (the pelvis samples).
			Op->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				if (Member != Op)
				{
					Member->StopOperative();
					Member->TeleportTo(State.P - State.F * 300.f + State.R * (Index++ % 2 == 0 ? 300.f : -300.f), State.F.Rotation(), false, true);
				}
			}
			const FVector WallCentre = State.P + State.F * 600.f;
			const float WallHeight = State.bCrouch ? 60.f : 300.f; // crouch run: a 60 cm low cover
			State.Wall = SpawnBlock(World, FVector(WallCentre.X, WallCentre.Y, State.GroundZ + WallHeight * 0.5f), State.F.Rotation(),
				FVector(0.4f, 6.f, WallHeight / 100.f));
			Check(State, State.Wall.IsValid(), FString::Printf(TEXT("a %.1f m high wall spawned 6 m ahead"), WallHeight / 100.f));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			const FVector Click = State.bCrouch ? FVector(0.f, 0.f, 30.f) : FVector(0.f, 0.f, 90.f);
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 580.f + Click, State.F, State.Slot);
			const ECoverHeight Wanted = State.bCrouch ? ECoverHeight::LowCover : ECoverHeight::HighCover;
			Check(State, bFound && State.Slot.Height == Wanted, FString::Printf(TEXT("%s cover slot at the wall's middle"), State.bCrouch ? TEXT("low") : TEXT("high")));
			if (!bFound)
			{
				return Finish(State, false);
			}
			if (State.bCrouch)
			{
				Op->SetStance(EOperativeStance::Crouching); // crouch-walk into the low cover
			}
			Op->bIgnoreCoverThreatForTesting = true; // the run-in alone (no fire stance: a threat beyond the low cover would offset the mesh too)
			Check(State, Op->OrderTakeCover(State.Slot, !State.bCrouch) == EOperativeOrderResult::Accepted,
				State.bCrouch ? TEXT("crouch-walk into the low cover ordered (5 m)") : TEXT("sprint into cover ordered (5 m run)"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// Every frame: the run, the early entry, the blend.
			const UOperativeAnimInstance* Anim = AnimOf(Op);
			const FVector Normal = State.Slot.WallNormal.GetSafeNormal2D();
			const FVector Pelvis = Op->GetMesh()->GetBoneLocation(TEXT("pelvis"));
			if (!State.bEntered && Op->bInCover)
			{
				State.bEntered = true;
				State.EntryDistance = static_cast<float>(FVector::Dist2D(State.LastPelvis, State.Slot.WorldLocation));
			}
			if (State.bEntered)
			{
				State.SinceEntry += DeltaTime;
			}
			if (State.bHavePelvis && (State.bEntered || FVector::Dist2D(Op->GetActorLocation(), State.Slot.WorldLocation) < 300.f))
			{
				// Away from the wall = along its normal: the run goes towards it, the enter clip turns round in place and
				// backs towards it — the old hard stop popped the body 76 cm back out.
				const float Away = static_cast<float>(FVector::DotProduct(Pelvis - State.LastPelvis, Normal));
				State.MaxBackJump = FMath::Max(State.MaxBackJump, Away);
				State.BackTravel += FMath::Max(Away, 0.f);
			}
			State.LastPelvis = Pelvis;
			State.bHavePelvis = true;
			if (Anim && Anim->GetRifleLocoState() == ERifleLocoState::Stop && (State.bEntered || FVector::Dist2D(Op->GetActorLocation(), State.Slot.WorldLocation) < 250.f))
			{
				State.bSawStopClip = true;
			}
			if (State.bEntered && Anim)
			{
				const float Weight = Anim->GetCoverEnterBlendWeight();
				if (Weight >= 0.f)
				{
					if (State.FirstWeight < 0.f)
					{
						State.FirstWeight = Weight;
					}
					if (State.LastWeight >= 0.f)
					{
						State.MaxWeightDrop = FMath::Max(State.MaxWeightDrop, State.LastWeight - Weight);
					}
					State.LastWeight = Weight;
				}
				State.MaxOffset = FMath::Max(State.MaxOffset, static_cast<float>(Op->GetCoverEntryMeshOffset().Size2D()));
			}
			if ((!State.bEntered || State.SinceEntry < 1.5f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, State.bEntered && Op->bInCover, FString::Printf(TEXT("entered the cover from the run (%.1f s)"), State.StageTime));
			Check(State, State.EntryDistance >= 50.f, FString::Printf(TEXT("... entered before the slot, the enter clip overlapping the last steps (body %.0f cm out)"), State.EntryDistance));
			Check(State, State.BackTravel <= 25.f, FString::Printf(TEXT("... the body never pops back out from the wall (moved %.0f cm away in all, max %.1f cm in a frame)"),
				State.BackTravel, State.MaxBackJump));
			Check(State, State.FirstWeight >= 0.f && State.FirstWeight < 0.6f && State.MaxWeightDrop <= 0.02f,
				FString::Printf(TEXT("... the enter clip blends in smoothly (first weight %.2f, max drop %.2f)"), State.FirstWeight, State.MaxWeightDrop));
			Check(State, !State.bSawStopClip, TEXT("... no locomotion stop clip between the run and the enter clip"));
			Check(State, State.MaxOffset > 30.f && Op->GetCoverEntryMeshOffset().Size2D() < 1.f,
				FString::Printf(TEXT("... the mesh offset carried the body (max %.0f cm) and is gone after the blend"), State.MaxOffset));
			if (Anim)
			{
				bool bEnterClip = false;
				const TCHAR* EnterName = State.bCrouch ? TEXT("crch_idle_fwd_to_cvr_crch_idle") : TEXT("std_idle_fwd_to_cvr_std_idle");
				for (const FString& Clip : Anim->GetCoverClipLog())
				{
					bEnterClip |= Clip.Contains(EnterName);
				}
				Check(State, bEnterClip, FString::Printf(TEXT("... the pack's %s enter clip played"), EnterName));
			}
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			FString Clip;
			const bool bCoverPose = PlaysCoverPose(*Op, Clip);
			Check(State, Op->bInCover && bCoverPose, FString::Printf(TEXT("in cover, the cover loop plays (%s)"), *Clip));
			if (State.bCrouch)
			{
				Check(State, Clip.Contains(TEXT("cvr_crch_")) && Op->GetStance() == EOperativeStance::Crouching,
					FString::Printf(TEXT("... crouched: the crouched cover loop (%s)"), *Clip));
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started with the leader at the wall"));
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.5f || TurnBased->IsBusy())
			{
				return State.StageTime < 15.f ? true : Finish(State, false);
			}
			AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			Check(State, Unit == Op, TEXT("the leader (at the wall) has the turn"));
			if (Unit != Op || !UnitState)
			{
				return Finish(State, false);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: at the grid fight start the leader is %s cover"), Op->bInCover ? TEXT("in") : TEXT("out of"));
			// A cell 2-3 steps back from the wall (half the AP, the rest for the walk back to the wall).
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			FIntPoint Best(-999, -999);
			float BestScore = -1.e9f;
			for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, UnitState->AP))
			{
				// Crouched every step costs double (TurnBasedRules::MoveCostMultiplier): keep AP for the walk back.
				const int32 StepBudget = UnitState->AP / (Op->GetStance() == EOperativeStance::Crouching ? 2 : 1);
				if (Entry.Value < 2 || Entry.Value > StepBudget / 2 || Grid->GetOccupantType(Entry.Key) != EGorkyOccupantType::None)
				{
					continue;
				}
				const float Away = static_cast<float>(FVector::DotProduct(Grid->GridToWorld(Entry.Key) - State.Slot.WorldLocation, State.Slot.WallNormal));
				if (Away > BestScore)
				{
					BestScore = Away;
					Best = Entry.Key;
				}
			}
			Check(State, Best.X != -999 && TurnBased->MoveActiveUnitTo(Best), FString::Printf(TEXT("grid walk away from the wall ordered (%.0f cm out)"), BestScore));
			State.Samples = 0;
			State.CoverPoseWhileMoving = 0;
			State.MaxAnimSpeed = 0.f;
			State.bLeftCover = false;
			State.CoverClipSeen.Reset();
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			SampleGridWalk(State, *TurnBased);
			if ((TurnBased->IsUnitMoving() || State.StageTime < 0.3f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, State.bLeftCover && !Op->bInCover, TEXT("grid walk: he left the cover"));
			Check(State, State.Samples >= 5 && State.CoverPoseWhileMoving == 0,
				FString::Printf(TEXT("... no cover pose while walking (%d of %d moving frames, '%s')"), State.CoverPoseWhileMoving, State.Samples, *State.CoverClipSeen));
			Check(State, State.MaxAnimSpeed > 100.f, FString::Printf(TEXT("... the locomotion walks (anim speed up to %.0f)"), State.MaxAnimSpeed));
			// Back to the wall: the cover cell (the controller's turn-based cover confirm: pending slot + grid walk).
			const FIntPoint Cell = TurnBased->GetGrid()->WorldToGrid(State.Slot.WorldLocation);
			Op->SetPendingCover(State.Slot);
			Check(State, TurnBased->MoveActiveUnitTo(Cell), TEXT("grid walk onto the cover cell ordered"));
			State.Samples = 0;
			State.CoverPoseWhileMoving = 0;
			State.MaxAnimSpeed = 0.f;
			State.CoverClipSeen.Reset();
			State.Stage = 6;
			State.StageTime = 0.f;
			return true;
		}
		case 6:
		{
			SampleGridWalk(State, *TurnBased);
			if ((TurnBased->IsUnitMoving() || State.StageTime < 0.3f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, State.Samples >= 3 && State.CoverPoseWhileMoving == 0,
				FString::Printf(TEXT("walk onto the cover cell: walking, no cover pose on the way (%d of %d, '%s')"), State.CoverPoseWhileMoving, State.Samples, *State.CoverClipSeen));
			Check(State, State.MaxAnimSpeed > 100.f, FString::Printf(TEXT("... anim speed up to %.0f"), State.MaxAnimSpeed));
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		}
		case 7:
		{
			if (!Op->bInCover && State.StageTime < 3.f)
			{
				return true;
			}
			FString Clip;
			Check(State, Op->bInCover, FString::Printf(TEXT("... and enters the cover on the cell (%.1f s)"), State.StageTime));
			State.Stage = 8;
			State.StageTime = 0.f;
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			FString Clip;
			const bool bCoverPose = PlaysCoverPose(*Op, Clip);
			Check(State, Op->bInCover && bCoverPose, FString::Printf(TEXT("... the cover pose plays at the wall again (%s)"), *Clip));
			if (State.bCrouch)
			{
				Check(State, Clip.Contains(TEXT("cvr_crch_")), FString::Printf(TEXT("... crouched again (%s)"), *Clip));
			}
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
		State->bCrouch = Args.Contains(TEXT("crouch"));
		// Every frame (the run-in blend is sampled per frame).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverMoveSmoke"),
		TEXT("Dev check: running into cover blends smoothly; grid walks from / to cover walk (no cover pose sliding); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
