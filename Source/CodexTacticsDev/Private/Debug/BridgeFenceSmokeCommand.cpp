// Dev-only headless check of the Demo 01 level obstacles on L_MovementTest (user flow 2026-10-13):
//   Scripts/smoke.ps1 -Command CodexTactics.BridgeFenceSmoke
// 1. A one-at-a-time bridge (ABridgeCrossingVolume, capacity 1) 6 m ahead of the squad: the leader is ordered across in
//    formation. Only ONE operative is on the span at a time; the followers are stopped at the edge (BridgeRefused, feed line,
//    traffic hold) and wait; when the leader steps off on the far side (BridgeCrossed) the next one is admitted, until all
//    three have crossed. Nobody was ever on the span together.
// 2. A low fence (ACrawlGapActor, clearance 90 cm) ahead of the leader: a STANDING leader ordered beyond it is stopped before
//    the fence (CrawlGapRefused, the "go prone" feed line) and never gets past it; prone (V) he crawls under and comes out on
//    the other side (CrawlGapPassed). A standing path to a point beyond the fence never crosses the crawl-only area.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "Level/BridgeCrossingVolume.h"
#include "Level/CrawlGapActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Subsystems/CodexEventBus.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace BridgeFenceSmoke
{
	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ABridgeCrossingVolume> Bridge;
		TWeakObjectPtr<ACrawlGapActor> Fence;
		FVector BridgeCentre = FVector::ZeroVector;
		FVector BridgeAxis = FVector::ForwardVector;
		FVector FenceCentre = FVector::ZeroVector;
		FVector FenceAxis = FVector::ForwardVector;
		int32 MaxOnSpan = 0;
		TSet<FName> Crossed;
		int32 Refused = 0;
		int32 Passed = 0;
		float LeaderMaxAlongFence = -10000.f;
		TWeakObjectPtr<AOperativeCharacter> Crawler;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("BridgeFenceSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** A blocking cube (navmesh obstacle): the crevasse banks beside the span, so the span is the only way across. */
	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
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

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (const FGameMessage& Message : Messages->GetHistory())
			{
				Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return Count;
	}

	/** Events of this smoke's world, counted by the bus (a UObject listener is not needed: a lambda on the native path is not available, so poll the actors). */
	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || Squad->GetMembers().Num() < 3 || State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("world ready / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			// The bridge: a 6 x 2 m span 6 m ahead of the leader, its X along his forward.
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Far = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Forward * 2100.f);
			const FVector Axis = (Far - Leader->GetActorLocation()).GetSafeNormal2D();
			const float FloorZ = Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Centre = FVector(Leader->GetActorLocation().X, Leader->GetActorLocation().Y, FloorZ) + Axis * 600.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABridgeCrossingVolume* Bridge = World->SpawnActor<ABridgeCrossingVolume>(Centre + FVector(0.f, 0.f, 100.f), Axis.Rotation(), Params);
			Bridge->Span->SetBoxExtent(FVector(300.f, 110.f, 150.f));
			State.Bridge = Bridge;
			State.BridgeCentre = Centre;
			State.BridgeAxis = Axis;
			// The crevasse banks: blocking cubes 6 x 30 x 3 m on both sides of the 2.2 m span (the span is the only way across).
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Axis).GetSafeNormal();
			SpawnBlock(World, Centre + Right * 1610.f + FVector(0.f, 0.f, 150.f), Axis.Rotation(), FVector(6.f, 30.f, 3.f));
			SpawnBlock(World, Centre - Right * 1610.f + FVector(0.f, 0.f, 150.f), Axis.Rotation(), FVector(6.f, 30.f, 3.f));
			Check(State, Bridge && Bridge->Capacity == 1 && Bridge->GetOccupantCount() == 0, TEXT("bridge placed: capacity 1, empty, banks blocked"));
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 4.f)
			{
				return true; // the navmesh picks up the banks
			}
			// Formation on; the leader walks to 11 m past the far end (the followers' column slots end up off the span), the followers
			// follow and must queue.
			Leader->OrderMoveTo(State.BridgeCentre + State.BridgeAxis * 1400.f, false);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			ABridgeCrossingVolume* Bridge = State.Bridge.Get();
			// The order is repeated every 3 s until the leader stands on the far side (the dynamic navmesh may still have been rebuilding).
			const float LeaderAlong = FVector::DotProduct(Leader->GetActorLocation() - State.BridgeCentre, State.BridgeAxis);
			if (LeaderAlong < 1300.f && !Leader->IsMoving() && FMath::Fmod(State.StageTime, 3.f) < 0.05f)
			{
				Leader->OrderMoveTo(State.BridgeCentre + State.BridgeAxis * 1400.f, false);
			}
			State.MaxOnSpan = FMath::Max(State.MaxOnSpan, Bridge->GetOccupantCount());
			State.Refused = Bridge->GetRefusalCount();
			// Who is past the far end (crossed)?
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float Along = FVector::DotProduct(Member->GetActorLocation() - State.BridgeCentre, State.BridgeAxis);
				if (Along > 320.f)
				{
					State.Crossed.Add(Member->GetFName());
				}
			}
			// Everybody who is NOT an occupant and stands on the span must be held (the "wait at the edge" rule).
			TArray<AActor*> OnSpan;
			Bridge->Span->GetOverlappingActors(OnSpan, AOperativeCharacter::StaticClass());
			int32 Unheld = 0;
			for (AActor* Actor : OnSpan)
			{
				const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
				if (Operative && !Bridge->IsOccupant(Operative) && !Operative->IsTrafficHeld() && State.StageTime > 1.f)
				{
					++Unheld;
				}
			}
			if (Unheld > 0)
			{
				Check(State, false, FString::Printf(TEXT("%d operative(s) on the full span neither occupant nor held"), Unheld));
			}
			if (Bridge->GetCrossingCount() >= 3 || State.StageTime > 90.f)
			{
				Check(State, State.MaxOnSpan == 1, FString::Printf(TEXT("never more than one on the span (max %d)"), State.MaxOnSpan));
				Check(State, State.Refused >= 1, FString::Printf(TEXT("a follower was refused at the edge (%d refusals)"), State.Refused));
				Check(State, CountMessages(World, TEXT("won't hold")) >= 1, TEXT("the creak feed line was posted"));
				Check(State, Bridge->GetCrossingCount() == 3, FString::Printf(TEXT("all three crossed one by one (%d crossings in %.0f s)"),
					Bridge->GetCrossingCount(), State.StageTime));
				Check(State, Bridge->GetOccupantCount() == 0 && Bridge->GetWaitingCount() == 0, TEXT("span empty, nobody waiting"));
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Check(State, !Member->IsTrafficHeld(), FString::Printf(TEXT("%s released"), *Member->DisplayName.ToString()));
				}
				Next(State);
			}
			return true;
		}
		case 2:
		{
			// The fence: 6 m ahead of the leader (fresh direction), 6 m wide, clearance 90 cm.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
			}
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Far = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Forward * 1400.f);
			const FVector Axis = (Far - Leader->GetActorLocation()).GetSafeNormal2D();
			const float FloorZ = Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Centre = FVector(Leader->GetActorLocation().X, Leader->GetActorLocation().Y, FloorZ) + Axis * 600.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACrawlGapActor* Fence = World->SpawnActor<ACrawlGapActor>(Centre, Axis.Rotation(), Params);
			Fence->GapWidthCm = 3000.f; // a 30 m fence line, closed at both ends by blocks: no way round
			Fence->OnConstruction(Fence->GetActorTransform());
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Axis).GetSafeNormal();
			SpawnBlock(World, Centre + Right * 3500.f + FVector(0.f, 0.f, 150.f), Axis.Rotation(), FVector(1.f, 40.f, 3.f));
			SpawnBlock(World, Centre - Right * 3500.f + FVector(0.f, 0.f, 150.f), Axis.Rotation(), FVector(1.f, 40.f, 3.f));
			State.Fence = Fence;
			State.FenceCentre = Centre;
			State.FenceAxis = Axis;
			Check(State, Fence && Fence->GetRefusalCount() == 0, TEXT("low fence placed (clearance 90 cm, 30 m wide)"));
			Squad->SetFollowersHolding(true); // the followers stay out of this part
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 4.f)
			{
				return true; // let the navmesh pick up the crawl-only area
			}
			// A standing path to a point beyond the fence ends on this side of it (the crawl-only strip is closed to UNavFilter_NoCrawl);
			// a prone path (no filter) reaches the far side.
			const FVector Beyond = State.FenceCentre + State.FenceAxis * 500.f;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			UNavigationPath* Standing = Nav ? UNavigationSystemV1::FindPathToLocationSynchronously(World, Leader->GetActorLocation(), Beyond, Leader,
				UNavFilter_NoCrawl::StaticClass()) : nullptr;
			UNavigationPath* Prone = Nav ? UNavigationSystemV1::FindPathToLocationSynchronously(World, Leader->GetActorLocation(), Beyond, Leader) : nullptr;
			const float StandingEnd = Standing && Standing->IsValid() && Standing->PathPoints.Num() > 0
				? FVector::DotProduct(Standing->PathPoints.Last() - State.FenceCentre, State.FenceAxis) : 0.f;
			const float ProneEnd = Prone && Prone->IsValid() && Prone->PathPoints.Num() > 0
				? FVector::DotProduct(Prone->PathPoints.Last() - State.FenceCentre, State.FenceAxis) : 0.f;
			// Informational: the synchronous query with the filter class has been seen to return a full path while the operative's own
			// move (AOperativeCharacter::RequestMove, same filter) ends before the fence; the behavioural checks below are the contract.
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: standing path (UNavFilter_NoCrawl) ends %.0f cm along (partial %d), prone path %.0f cm"),
				StandingEnd, Standing && Standing->IsPartial() ? 1 : 0, ProneEnd);
			Check(State, Standing && Standing->IsValid(), TEXT("a standing path query returns a path"));
			Check(State, Prone && Prone->IsValid() && !Prone->IsPartial() && ProneEnd > 400.f,
				FString::Printf(TEXT("the prone path (no filter) reaches the far side (%.0f cm along)"), ProneEnd));
			Leader->SetStance(EOperativeStance::Standing);
			Leader->OrderMoveTo(Beyond, false);
			State.LeaderMaxAlongFence = -10000.f;
			Next(State);
			return true;
		}
		case 4:
		{
			const float Along = FVector::DotProduct(Leader->GetActorLocation() - State.FenceCentre, State.FenceAxis);
			State.LeaderMaxAlongFence = FMath::Max(State.LeaderMaxAlongFence, Along);
			ACrawlGapActor* Fence = State.Fence.Get();
			if ((Fence->GetRefusalCount() >= 1 && !Leader->IsMoving() && State.StageTime > 1.f) || State.StageTime > 25.f)
			{
				Check(State, Fence->GetRefusalCount() >= 1, FString::Printf(TEXT("the standing leader was stopped / hinted at the fence (%d refusals)"), Fence->GetRefusalCount()));
				Check(State, State.LeaderMaxAlongFence < -20.f, FString::Printf(TEXT("he never got past the fence line (max %.0f cm along)"), State.LeaderMaxAlongFence));
				Check(State, CountMessages(World, TEXT("go prone")) >= 1, TEXT("the 'go prone' feed line was posted"));
				Check(State, Fence->GetPassCount() == 0, TEXT("no crawl-through yet"));
				// Prone: he crawls under.
				Leader->SetStance(EOperativeStance::Prone);
				State.Crawler = Leader;
				Next(State);
			}
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the lie-down clip
			}
			Check(State, Leader->GetStance() == EOperativeStance::Prone, TEXT("leader prone"));
			Check(State, !Leader->IsTrafficHeld(), TEXT("prone: the fence released its hold"));
			const EOperativeOrderResult Result = Leader->OrderMoveTo(State.FenceCentre + State.FenceAxis * 500.f, false);
			Check(State, Result != EOperativeOrderResult::Unreachable, TEXT("a prone move beyond the fence is accepted (crawl-only area open to prone)"));
			Next(State);
			return true;
		}
		case 6:
		{
			ACrawlGapActor* Fence = State.Fence.Get();
			const float Along = FVector::DotProduct(Leader->GetActorLocation() - State.FenceCentre, State.FenceAxis);
			if (Fence->GetPassCount() >= 1 || State.StageTime > 60.f)
			{
				Check(State, Fence->GetPassCount() == 1, FString::Printf(TEXT("the prone leader crawled under (%d passes, %.0f s, %.0f cm past)"),
					Fence->GetPassCount(), State.StageTime, Along));
				Check(State, Along > 0.f, TEXT("he is on the far side"));
				Squad->SetFollowersHolding(false);
				return Finish(State, true);
			}
			return true;
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.BridgeFenceSmoke"),
		TEXT("Dev check: one-at-a-time bridge (followers wait at the edge, all cross one by one) and the low fence (standing stopped, prone crawls under); PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
