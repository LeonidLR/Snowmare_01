// Dev-only headless check of the group cover leader priority (game designer play-test 2026-10-23) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands GroupCoverPrioritySmoke
// "When I order 'take cover at the wall' while in formation, the soldier running to the wall can stumble or be delayed and the two others
// reach the wall first. Then he doesn't take cover at all and just stands, or goes past the edge of the wall because his mates already
// took his place. Give him PRIORITY: the other two wait until he has taken cover, then they take cover after him."
// Own empty ground (SmokeUtils::FindClearArena), an 8 m long, 3 m high wall spawned 6 m ahead; the order goes through the controller's cover
// confirm (ShowCoverPreview + ConfirmCoverPreview):
//  1. group mode, the leader delayed (CustomTimeDilation 0.25 = he runs at a quarter of the speed): the two others reach their wait
//     points ~1.6 m out from the wall first and WAIT there (not in cover, never on his slot) while he is still on his way;
//  2. the leader takes cover on the clicked slot; then the two others take their slots: all three in cover on the same wall, the leader
//     on the clicked slot, nobody past the wall's ends, no overlap;
//  3. the leader's order cancelled on his way (StopOperative): the others wait the grace (4 s), then take their slots anyway.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/GroupCoverRules.h"

namespace GroupCoverPrioritySmoke
{
	constexpr float StepSeconds = 0.1f;
	constexpr float WallDistanceCm = 600.f;
	constexpr float WallLengthCm = 800.f;

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
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FCoverSlot Slot;
		bool bSawWaiting = false;
		bool bFollowerOnLeaderSlot = false;
		bool bFollowerInCoverFirst = false;
		float MinWaitDepth = 1.0e9f;
		float LeaderInCoverAt = -1.f;
		float CancelledAt = -1.f;
		float ReleasedAt = -1.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		FPlatformMisc::RequestExit(false, TEXT("GroupCoverPrioritySmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	void SpawnWall(UWorld* World, FState& State)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector Centre = State.P + State.F * (WallDistanceCm + 20.f);
		const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.GroundZ + 150.f), FVector(0.4f, WallLengthCm / 100.f, 3.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
		}
		State.Wall = Block;
	}

	/** Everyone out of cover, standing: the leader at the origin, the others 1.5 m ahead of him to the sides (nearer the wall). */
	void ResetSquad(USquadSubsystem* Squad, FState& State)
	{
		const FVector Offsets[] = { FVector::ZeroVector, State.F * 150.f - State.R * 200.f, State.F * 150.f + State.R * 200.f };
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->CustomTimeDilation = 1.f;
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			Each->TeleportTo(State.P + Offsets[FMath::Min(Index, 2)] + FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
		}
		Squad->SetLeader(Member(State, 0));
	}

	/** Off the wall's near face towards the squad's side, cm. */
	float Depth(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct((State.P + State.F * WallDistanceCm) - Point, State.F));
	}

	/** Along the wall from its middle, cm. */
	float Along(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - (State.P + State.F * WallDistanceCm), State.R));
	}

	/** While the leader is not in cover yet: the others wait off the wall, never in cover, never on his slot. */
	void TrackWaiting(FState& State, const USquadSubsystem* Squad)
	{
		AOperativeCharacter* A = Member(State, 0);
		for (int32 Index = 1; Index < State.Members.Num(); ++Index)
		{
			const AOperativeCharacter* Each = Member(State, Index);
			if (!Each || !A || A->bInCover)
			{
				continue;
			}
			State.bFollowerInCoverFirst |= Each->bInCover;
			State.bFollowerOnLeaderSlot |= FVector::Dist2D(Each->GetActorLocation(), State.Slot.WorldLocation) < 60.f;
			if (Squad->IsWaitingForGroupCoverLeader(Each) && !Each->HasOwnMoveOrder())
			{
				State.bSawWaiting = true;
				State.MinWaitDepth = FMath::Min(State.MinWaitDepth, Depth(State, Each->GetActorLocation()));
			}
		}
	}

	bool AllSettled(const FState& State)
	{
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			if (!Member(State, Index) || !Member(State, Index)->bInCover)
			{
				return false;
			}
		}
		return true;
	}

	void CheckSettled(FState& State, const TCHAR* Tag)
	{
		const AOperativeCharacter* Three[] = { Member(State, 0), Member(State, 1), Member(State, 2) };
		bool bSameWall = true;
		float MaxAlong = 0.f;
		float MinGap = 1.0e9f;
		for (int32 I = 0; I < 3; ++I)
		{
			bSameWall &= Three[I] && Three[I]->GetCoverSlot().WallActor.Get() == State.Wall.Get();
			MaxAlong = FMath::Max(MaxAlong, FMath::Abs(Along(State, Three[I]->GetActorLocation())));
			for (int32 J = I + 1; J < 3; ++J)
			{
				MinGap = FMath::Min(MinGap, static_cast<float>(FVector::Dist2D(Three[I]->GetActorLocation(), Three[J]->GetActorLocation())));
			}
		}
		Check(State, bSameWall, FString::Printf(TEXT("%s all three in cover on the same wall"), Tag));
		Check(State, MaxAlong <= WallLengthCm * 0.5f - 20.f, FString::Printf(TEXT("%s nobody past the wall's ends (farthest %.0f cm from its middle, ends at %.0f)"),
			Tag, MaxAlong, WallLengthCm * 0.5f));
		Check(State, MinGap >= 80.f, FString::Printf(TEXT("%s no overlap (closest two %.0f cm apart)"), Tag, MinGap));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			if (World)
			{
				Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			}
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* A = Member(State, 0);
		AOperativeCharacter* B = Member(State, 1);
		AOperativeCharacter* C = Member(State, 2);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-1000.f, -700.f), FVector2D(900.f, 700.f)), Ignore);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			State.GroundZ = Arena.Origin.Z;
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			ResetSquad(Squad, State);
			SpawnWall(World, State);
			Check(State, State.Wall.IsValid(), TEXT("an 8 m long high wall 6 m ahead"));
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			const FVector Face = State.P + State.F * WallDistanceCm;
			if (!CoverTraceRules::FindCoverSlotAt(World, FVector(Face.X, Face.Y, State.GroundZ + 90.f), State.F, State.Slot)
				|| State.Slot.Height != ECoverHeight::HighCover)
			{
				Check(State, false, TEXT("a high cover slot in the middle of the wall"));
				return Finish(State, false);
			}
			Check(State, Squad->GetGroupCoverMembers(A).Num() == 2, TEXT("[1] group mode: the two followers go along"));
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			A->CustomTimeDilation = 0.25f; // he "stumbles": the two others reach the wall first
			Check(State, PC->GetLastGroupCoverSlotCount() == 3 && Squad->IsWaitingForGroupCoverLeader(B) && Squad->IsWaitingForGroupCoverLeader(C)
				&& A->HasPendingCover(), FString::Printf(TEXT("[1] a group cover order: 3 slots, the leader runs to his, the others to their wait points (%d)"),
					PC->GetLastGroupCoverSlotCount()));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			TrackWaiting(State, Squad);
			if (!A->bInCover && State.StageTime < 30.f)
			{
				return true;
			}
			State.LeaderInCoverAt = State.StageTime;
			Check(State, A->bInCover, FString::Printf(TEXT("[1] the delayed leader takes his cover (%.1f s)"), State.StageTime));
			Check(State, State.bSawWaiting && State.MinWaitDepth >= 120.f, FString::Printf(TEXT("[1] the others reached their wait points first and waited off the wall (nearest %.0f cm)"),
				State.MinWaitDepth));
			Check(State, !State.bFollowerInCoverFirst && !State.bFollowerOnLeaderSlot, TEXT("[1] nobody took cover before him, nobody stood on his slot"));
			const float LeaderOff = FVector::Dist2D(A->GetActorLocation(), State.Slot.WorldLocation);
			Check(State, LeaderOff < 40.f, FString::Printf(TEXT("[1] the leader on the clicked slot (%.0f cm off)"), LeaderOff));
			A->CustomTimeDilation = 1.f;
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (!AllSettled(State) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, AllSettled(State), FString::Printf(TEXT("[2] after him the others take their slots (%.1f s)"), State.StageTime));
			if (!AllSettled(State))
			{
				return Finish(State, false);
			}
			CheckSettled(State, TEXT("[2]"));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, AllSettled(State) && FVector::Dist2D(A->GetActorLocation(), State.Slot.WorldLocation) < 40.f, TEXT("[2] 1.5 s later all still there"));
			// 3: the leader's order cancelled on his way.
			ResetSquad(Squad, State);
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			PC->ShowCoverPreview(A, State.Slot);
			PC->ConfirmCoverPreview();
			A->CustomTimeDilation = 0.25f;
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			A->StopOperative();
			A->ClearPendingCover();
			A->CustomTimeDilation = 1.f;
			State.CancelledAt = State.Time;
			Check(State, !A->bInCover && !A->HasPendingCover() && Squad->IsWaitingForGroupCoverLeader(B) && Squad->IsWaitingForGroupCoverLeader(C),
				TEXT("[3] the leader's cover order cancelled on his way; the others still wait"));
			Next(State, 7);
			return true;
		}
		case 7:
		{
			const bool bWaiting = Squad->IsWaitingForGroupCoverLeader(B) || Squad->IsWaitingForGroupCoverLeader(C);
			if (bWaiting && State.StageTime < 10.f)
			{
				return true;
			}
			const FGroupCoverConfig Config;
			State.ReleasedAt = State.StageTime;
			Check(State, !bWaiting && State.ReleasedAt >= Config.LeaderFailGraceSeconds - 0.3f && State.ReleasedAt <= Config.LeaderFailGraceSeconds + 1.f,
				FString::Printf(TEXT("[3] released after the grace (%.1f s, grace %.1f s)"), State.ReleasedAt, Config.LeaderFailGraceSeconds));
			Next(State, 8);
			return true;
		}
		case 8:
		{
			const bool bBoth = B->bInCover && C->bInCover;
			if (!bBoth && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, bBoth && !A->bInCover, FString::Printf(TEXT("[3] the two others take their slots anyway (%d %d), the leader stays out"), B->bInCover, C->bInCover));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GroupCoverPrioritySmoke"),
		TEXT("Dev check (game designer 2026-10-23): group cover leader priority - the others wait off the wall until the delayed leader is in cover, then "
			"take their slots; a cancelled leader order releases them after the grace; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
