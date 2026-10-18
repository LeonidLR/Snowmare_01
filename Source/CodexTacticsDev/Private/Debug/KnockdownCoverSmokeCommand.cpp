// Dev-only headless check of the return to cover after a knockdown (user decision 2026-10-18, every level;
// KnockdownRules::DecideCoverReturn, AOperativeCharacter::ReturnToCoverAfterKnockdown) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands KnockdownCoverSmoke
// The leader takes cover at a 3 m wall spawned 3 m ahead (nothing saved, every enemy of the map removed first):
//  A. a BLAST knocks him down, nobody close: once up he walks back into the same cover slot;
//  B. two hounds stand next to him (frozen) and a POUNCE knocks him down: he stays out of the cover (the player re-orders);
//  C. back in cover (re-ordered), the hounds still 2 m off, a BLAST: he stays out (enemies around him);
//  D. hounds gone, back in cover, a BLAST and a move order while he lies (buffered): the order wins - he walks there.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace KnockdownCoverSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FCoverSlot Slot;
		FVector SlotLocation = FVector::ZeroVector;
		FVector MoveGoal = FVector::ZeroVector;
		bool bSawDown = false;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Hounds;
		TWeakObjectPtr<AStaticMeshActor> Wall;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Hounds)
		{
			if (Hound.IsValid())
			{
				Hound->Destroy();
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("KnockdownCoverSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bSawDown = false;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
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

	/** In cover at the remembered slot, standing still. */
	bool IsBackAtSlot(const FState& State, const AOperativeCharacter& Op)
	{
		return Op.bInCover && !Op.IsKnockedDown() && FVector::Dist2D(Op.GetCoverSlot().WorldLocation, State.SlotLocation) < 60.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Op = Squad ? Squad->GetLeader() : nullptr;
		if (!Op || !Op->KnockdownComponent)
		{
			Check(State, false, TEXT("leader with a knockdown component"));
			return Finish(State);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		State.bSawDown |= Op->IsKnockedDown();
		UKnockdownComponent* Knock = Op->KnockdownComponent;
		auto OrderCover = [&State, Op]()
		{
			return Op->OrderTakeCover(State.Slot, false) == EOperativeOrderResult::Accepted;
		};
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Squad->SetFollowersHolding(true);
			State.F = Op->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Op->GetActorLocation() - FVector(0.f, 0.f, Op->GetSimpleCollisionHalfHeight());
			State.Wall = SpawnWall(World, State.P + State.F * 300.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.4f, 4.f, 3.f));
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 280.f + FVector(0.f, 0.f, 90.f), State.F, State.Slot);
			Check(State, bFound && OrderCover(), TEXT("the leader takes cover at the wall"));
			State.SlotLocation = State.Slot.WorldLocation;
			Next(State, 2);
			return true;
		}
		case 2:
			if ((!Op->bInCover || State.StageTime < 1.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Op->bInCover, TEXT("in cover"));
			State.SlotLocation = Op->GetCoverSlot().WorldLocation;
			// A. A blast floors him, nobody around.
			Check(State, Knock->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion) && !Op->bInCover,
				TEXT("A: a blast knocks him down and out of the cover"));
			Next(State, 3);
			return true;
		case 3:
			if ((!State.bSawDown || Op->IsKnockedDown() || !IsBackAtSlot(State, *Op)) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Op->GetLastCoverReturn() == EKnockdownCoverReturn::Return && IsBackAtSlot(State, *Op),
				FString::Printf(TEXT("A: up again he walks back into the same cover slot (%s, %.1f s, %.0f cm from it)"),
					KnockdownRules::CoverReturnName(Op->GetLastCoverReturn()), State.StageTime,
					FVector::Dist2D(Op->GetCoverSlot().WorldLocation, State.SlotLocation)));
			Next(State, 4);
			return true;
		case 4:
		{
			// B. Two hounds next to him (frozen: only their presence counts), a pounce floors him.
			if (State.StageTime < 2.f)
			{
				return true; // past the re-knock immunity
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			for (const float Side : { -1.f, 1.f })
			{
				const FVector Where = SmokeUtils::FreeSpot(World, Op->GetActorLocation() - State.F * 150.f + State.R * (Side * 150.f), Op);
				if (AEnemyCharacter* Hound = Waves ? Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Where, State.F.Rotation()) : nullptr)
				{
					Hound->CustomTimeDilation = 0.f;
					Hound->SetActorTickEnabled(false);
					State.Hounds.Add(Hound);
				}
			}
			Check(State, State.Hounds.Num() == 2, TEXT("B: two hounds next to him"));
			Check(State, Knock->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Pounce), TEXT("B: a pounce knocks him down"));
			Next(State, 5);
			return true;
		}
		case 5:
			if ((!State.bSawDown || Op->IsKnockedDown() || State.StageTime < 4.f) && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Op->GetLastCoverReturn() == EKnockdownCoverReturn::NotABlast && !Op->bInCover && !Op->IsMoving(),
				FString::Printf(TEXT("B: a pounce with hounds on him - he stays out of the cover (%s, in cover %d)"),
					KnockdownRules::CoverReturnName(Op->GetLastCoverReturn()), Op->bInCover ? 1 : 0));
			Check(State, OrderCover(), TEXT("C: the player re-orders the cover"));
			Next(State, 6);
			return true;
		case 6:
			if ((!IsBackAtSlot(State, *Op) || State.StageTime < 2.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, IsBackAtSlot(State, *Op), TEXT("C: back in cover"));
			Check(State, Knock->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion), TEXT("C: a blast with the hounds 2 m off"));
			Next(State, 7);
			return true;
		case 7:
			if ((!State.bSawDown || Op->IsKnockedDown() || State.StageTime < 4.f) && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Op->GetLastCoverReturn() == EKnockdownCoverReturn::EnemiesClose && !Op->bInCover,
				FString::Printf(TEXT("C: a blast but enemies around him - he stays out (%s)"), KnockdownRules::CoverReturnName(Op->GetLastCoverReturn())));
			for (const TWeakObjectPtr<AEnemyCharacter>& Hound : State.Hounds)
			{
				if (Hound.IsValid())
				{
					Hound->Destroy();
				}
			}
			State.Hounds.Reset();
			Check(State, OrderCover(), TEXT("D: the player re-orders the cover"));
			Next(State, 8);
			return true;
		case 8:
			if ((!IsBackAtSlot(State, *Op) || State.StageTime < 2.f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, IsBackAtSlot(State, *Op), TEXT("D: back in cover"));
			// D. A blast, then an order while he lies: the order wins.
			Check(State, Knock->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion), TEXT("D: a blast knocks him down"));
			State.MoveGoal = SmokeUtils::FreeSpot(World, State.P - State.F * 400.f + State.R * 200.f, Op);
			Op->OrderMoveTo(State.MoveGoal, false);
			Check(State, Knock->HasBufferedOrder(), TEXT("D: the move order given while he lies is buffered"));
			Next(State, 9);
			return true;
		case 9:
		{
			const float Left = FVector::Dist2D(Op->GetActorLocation(), State.MoveGoal);
			if ((!State.bSawDown || Op->IsKnockedDown() || (Left > 150.f && State.StageTime < 9.f)))
			{
				if (State.StageTime < 9.f)
				{
					return true;
				}
			}
			Check(State, Op->GetLastCoverReturn() == EKnockdownCoverReturn::OrderGiven && !Op->bInCover && Left <= 150.f,
				FString::Printf(TEXT("D: the order given while he lay wins - he walks there, not to the cover (%s, %.0f cm left)"),
					KnockdownRules::CoverReturnName(Op->GetLastCoverReturn()), Left));
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.KnockdownCoverSmoke"),
		TEXT("Dev check (2026-10-18): back to cover after a blast knockdown, out of it after a pounce / with enemies around, a buffered order wins; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
