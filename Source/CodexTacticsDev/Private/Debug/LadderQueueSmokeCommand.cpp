// Dev-only check of the queue at a ladder (docs/design/LADDERS.md 7.1, orchestrator task 2026-10-23: several operatives ordered up - one on
// the ladder at a time, the others wait at its foot, in the order they came), on campaign level 0.1 with its test ladders:
//   Scripts/smoke_parallel.ps1 -Commands LadderQueueSmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
//  1. the whole squad (3 operatives) selected, a real click on the wall ladder's roof (a group order: each gets his formation spot up there);
//  2. every frame: never more than one of them on the ladder (on it = any climb state but None / Queue), the waiting ones stand within 3.5 m
//     of the ladder's foot and out of the climber's way (not on the hang line);
//  3. they climb in the order they joined the line (the first to arrive goes first), the ladder's lease and queue are empty at the end, and all
//     three stand on the roof.
// Godot: none (UE-only system).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Ladders/LadderSubsystem.h"

namespace LadderQueueSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ALadderActor> Ladder;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<int32> QueueOrder;
		TArray<int32> ClimbOrder;
		int32 MaxOnLadder = 0;
		float MaxQueueDistance = 0.f;
		float MinQueueOffHangLine = 1e6f;
		int32 QueueSamples = 0;
		float DoneAt = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("LadderQueueSmoke"));
		return false;
	}

	FString JoinOrder(const TArray<int32>& Order)
	{
		return FString::JoinBy(Order, TEXT(" "), [](int32 Index) { return FString::FromInt(Index); });
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 400.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			if (!Squad || !Squad->GetLeader() || State.StageTime < 3.5f || (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.StageTime < 15.f))
			{
				return true;
			}
			const ULadderSubsystem* Ladders = World->GetSubsystem<ULadderSubsystem>();
			for (ALadderActor* Ladder : Ladders ? Ladders->GetLadders() : TArray<ALadderActor*>())
			{
				if (Ladder->HasWall())
				{
					State.Ladder = Ladder;
				}
			}
			TArray<AOperativeCharacter*> Group = Squad->GetMembers();
			Check(State, State.Ladder.IsValid() && Group.Num() >= 3, FString::Printf(TEXT("the wall ladder and %d operatives"), Group.Num()));
			if (!State.Ladder.IsValid() || Group.Num() < 3)
			{
				return Finish(State, false);
			}
			Group.SetNum(3);
			for (AOperativeCharacter* Member : Group)
			{
				State.Members.Add(Member);
				Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			Squad->SetSelectedGroup(Group);
			// A real click on the roof, a metre beyond the ladder's head: the group order.
			ALadderActor* Ladder = State.Ladder.Get();
			const FVector Goal = Ladder->GetTopLinkWorld() - Ladder->GetOutward() * 150.f;
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			FHitResult Hit;
			const bool bHit = PC && World->LineTraceSingleByChannel(Hit, Goal + FVector(0.f, 0.f, 300.f), Goal - FVector(0.f, 0.f, 300.f), ECC_Visibility);
			if (bHit)
			{
				PC->HandleWorldHit(Hit);
			}
			Check(State, bHit && Squad->HasMultiSelection(), FString::Printf(TEXT("the 3 operatives selected (multi-selection %d), a click on the roof at %s"),
				Squad->HasMultiSelection() ? 1 : 0, *Hit.ImpactPoint.ToCompactString()));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			ALadderActor* Ladder = State.Ladder.Get();
			int32 OnLadder = 0;
			bool bAllUp = true;
			for (int32 Index = 0; Index < State.Members.Num(); ++Index)
			{
				const AOperativeCharacter* Member = State.Members[Index].Get();
				const ULadderClimbComponent* Climb = Member ? Member->LadderClimb.Get() : nullptr;
				if (!Member || !Climb)
				{
					Check(State, false, TEXT("an operative went missing"));
					return Finish(State, false);
				}
				const ELadderClimbState S = Climb->GetState();
				OnLadder += Climb->IsOnLadder() ? 1 : 0;
				if (S == ELadderClimbState::Queue && Climb->GetLadder() == Ladder && !State.QueueOrder.Contains(Index))
				{
					State.QueueOrder.Add(Index);
				}
				if (S == ELadderClimbState::ClimbUp && !State.ClimbOrder.Contains(Index))
				{
					State.ClimbOrder.Add(Index);
				}
				if (S == ELadderClimbState::Queue && Climb->GetLadder() == Ladder)
				{
					// Waiting: near the foot, but off the hang line (the climber's way up and the bottom exit).
					const FVector Feet = Member->GetActorLocation();
					State.MaxQueueDistance = FMath::Max(State.MaxQueueDistance, static_cast<float>(FVector::Dist2D(Feet, Ladder->GetBottomLinkWorld())));
					if (Member->GetVelocity().Size2D() < 10.f && State.StageTime > 1.f)
					{
						State.MinQueueOffHangLine = FMath::Min(State.MinQueueOffHangLine,
							static_cast<float>(FVector::Dist2D(Feet, Ladder->GetHangWorldLocation(0.f))));
						++State.QueueSamples;
					}
				}
				const float FeetOverTop = static_cast<float>(Member->GetActorLocation().Z) - Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Ladder->GetTopWorldZ();
				bAllUp &= S == ELadderClimbState::None && FMath::Abs(FeetOverTop) < 15.f && Member->GetVelocity().Size2D() < 10.f;
			}
			State.MaxOnLadder = FMath::Max(State.MaxOnLadder, OnLadder);
			if (State.StageTime > 240.f)
			{
				Check(State, false, FString::Printf(TEXT("all three up in time (climb order %s, queue order %s)"), *JoinOrder(State.ClimbOrder), *JoinOrder(State.QueueOrder)));
				return Finish(State, false);
			}
			if (!bAllUp)
			{
				State.DoneAt = -1.f;
				return true;
			}
			if (State.DoneAt < 0.f)
			{
				State.DoneAt = State.StageTime;
			}
			if (State.StageTime - State.DoneAt < 1.f)
			{
				return true;
			}
			Check(State, State.MaxOnLadder == 1, FString::Printf(TEXT("never more than one on the ladder (max %d at once)"), State.MaxOnLadder));
			Check(State, State.ClimbOrder.Num() == 3 && State.QueueOrder.Num() >= 1,
				FString::Printf(TEXT("all three climbed it (order %s), %d of them waited in the line (order %s)"), *JoinOrder(State.ClimbOrder), State.QueueOrder.Num(),
					*JoinOrder(State.QueueOrder)));
			// The ones that waited climb in the order they joined the line (after the ones that never had to wait).
			TArray<int32> WaitedClimbOrder = State.ClimbOrder;
			WaitedClimbOrder.RemoveAll([&State](int32 Index) { return !State.QueueOrder.Contains(Index); });
			Check(State, WaitedClimbOrder == State.QueueOrder, FString::Printf(TEXT("the waiting ones went in the order they came (climbed %s, joined %s)"),
				*JoinOrder(WaitedClimbOrder), *JoinOrder(State.QueueOrder)));
			Check(State, State.QueueSamples > 5 && State.MaxQueueDistance < 350.f && State.MinQueueOffHangLine > 60.f,
				FString::Printf(TEXT("the waiting ones stood at the foot (at most %.0f cm from the link point) and off the climber's line (at least %.0f cm from it, %d samples)"),
					State.MaxQueueDistance, State.MinQueueOffHangLine, State.QueueSamples));
			Check(State, !Ladder->GetLeaseHolder() && Ladder->GetQueueLength() == 0, TEXT("the ladder's lease and line are empty, all three stand on the roof"));
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LadderQueueSmoke"),
		TEXT("Dev check (2026-10-23, docs/design/LADDERS.md 7.1): on L_Demo01_B_FrozenFactory the 3 operatives ordered onto the wall ladder's roof together: "
			"one on the ladder at a time, the others wait at its foot off the climber's line, in the order they came; all three end on the roof."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
