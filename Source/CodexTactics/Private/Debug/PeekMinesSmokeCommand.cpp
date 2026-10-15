// Dev-only check of the Demo 01 rev 5h corner-peek lesson (game designer 2026-10-15) on /Game/Maps/L_Demo01_A_FrozenLake:
//   Scripts/smoke_parallel.ps1 -Commands PeekMinesSmoke     (map from Scripts/test_map.json)
// The map's additive section AutoLayout_Demo01_5h put two hidden mines (SpotEventTag A3PeekMines) beyond the corner of the
// first high wall of the A3 cover field and the marker A3_peek at that corner. Nothing is saved.
//  0. the mines and the marker are found; the tutorial card A3_peek forced (beat A3: cover unlocked);
//  1. the leader takes cover at that wall next to the marker, the others wait 3 m behind him: after 2 s in cover NO mine is
//     spotted (no spot before the peek - they lie beyond the proximity spot range, out of view behind the wall);
//  2. a click on the wall's end (the real input: FindCornerPeekForHit + TryHandleCoverClick) - he leans out round the corner:
//     both mines spotted by his peek (GetPeekSpottedCount), TrapSpotted.Peek + TrapSpotted.A3PeekMines raised, the squad holds
//     in Solo (rev 5h trap hold), the stat traps_spotted.A3PeekMines = 2, the card A3_peek completed and the mine card A3_mine
//     (disarm / go round) next.
// Must not assume a squad body (any leader peeks).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "NavigationSystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace PeekMinesSmoke
{
	constexpr float StepSeconds = 0.1f;
	const FName PeekTag(TEXT("A3PeekMines"));

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<TWeakObjectPtr<AProximityMineActor>> Mines;
		TWeakObjectPtr<ATutorialMarkerActor> Marker;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		/** The wall's face normal on the squad's side and its long axis towards the marker's end. */
		FVector Normal = FVector::ForwardVector;
		FVector Along = FVector::RightVector;
		float GroundZ = 0.f;
		TStrongObjectPtr<UEventBusProbe> Probe;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("PeekMinesSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	int32 CountRevealed(const FState& State)
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
		{
			Count += Mine.IsValid() && Mine->IsRevealed() ? 1 : 0;
		}
		return Count;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		if (!Squad || !PC || !Leader || State.Time > 200.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / controller / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if (!(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f))) && State.StageTime < 120.f)
			{
				return true;
			}
			for (TActorIterator<AProximityMineActor> It(World); It; ++It)
			{
				if (It->SpotEventTag == PeekTag)
				{
					State.Mines.Add(*It);
				}
			}
			for (TActorIterator<ATutorialMarkerActor> It(World); It; ++It)
			{
				if (It->StepId == FName(TEXT("A3_peek")))
				{
					State.Marker = *It;
				}
			}
			const ATutorialMarkerActor* Marker = State.Marker.Get();
			Check(State, State.Mines.Num() == 2 && CountRevealed(State) == 0, FString::Printf(TEXT("two hidden mines tagged A3PeekMines (%d)"), State.Mines.Num()));
			Check(State, Marker != nullptr, TEXT("the marker A3_peek at the peek corner"));
			if (State.Mines.Num() != 2 || !Marker)
			{
				return Finish(State, false);
			}
			// The high wall whose end the marker stands at: the nearest static mesh taller than 1.8 m.
			float Best = 600.f;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				const FBox Box = It->GetComponentsBoundingBox();
				const FVector Extent = Box.GetExtent();
				if (Extent.Z * 2.f < 180.f || Extent.X > 1000.f || Extent.Y > 1000.f)
				{
					continue;
				}
				const float Distance = FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(FVector(Marker->GetActorLocation().X, Marker->GetActorLocation().Y, Box.GetCenter().Z)));
				if (Distance < Best)
				{
					Best = Distance;
					State.Wall = *It;
				}
			}
			AStaticMeshActor* Wall = State.Wall.Get();
			Check(State, Wall != nullptr, FString::Printf(TEXT("the high wall at the marker (%s, %.0f cm)"), Wall ? *Wall->GetName() : TEXT("-"), Best));
			if (!Wall)
			{
				return Finish(State, false);
			}
			const FBox Box = Wall->GetComponentsBoundingBox();
			const FVector C = Box.GetCenter();
			const FVector E = Box.GetExtent();
			const bool bAlongY = E.Y > E.X;
			const FVector ToMarker = Marker->GetActorLocation() - C;
			State.Normal = bAlongY ? FVector(FMath::Sign(ToMarker.X), 0.f, 0.f) : FVector(0.f, FMath::Sign(ToMarker.Y), 0.f);
			State.Along = bAlongY ? FVector(0.f, FMath::Sign(ToMarker.Y), 0.f) : FVector(FMath::Sign(ToMarker.X), 0.f, 0.f);
			State.GroundZ = Box.Min.Z;
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			if (Tutorial && Tutorial->IsTutorialActive())
			{
				Check(State, Tutorial->TriggerStep(TEXT("A3_peek")) && Tutorial->GetCurrentBeat() == FName(TEXT("A3")), TEXT("tutorial: the A3_peek card forced (beat A3)"));
			}
			// The leader 1.5 m in front of the wall at the marker, the others 3 m behind him.
			const FVector Spot = FVector(Marker->GetActorLocation().X, Marker->GetActorLocation().Y, State.GroundZ) + State.Normal * 150.f;
			int32 Index = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				const FVector Point = Member == Leader ? Spot : Spot + State.Normal * 300.f + State.Along * (Index++ == 0 ? -150.f : -350.f);
				Member->TeleportTo(SmokeUtils::FreeSpot(World, Point + FVector(0.f, 0.f, 100.f), Member), State.Normal.Rotation() + FRotator(0.f, 180.f, 0.f), false, true);
			}
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			FCoverSlot Slot;
			const FVector Face = FVector(State.Marker->GetActorLocation().X, State.Marker->GetActorLocation().Y, State.GroundZ + 90.f) + State.Normal * 30.f;
			const bool bFound = CoverTraceRules::FindCoverSlotAt(World, Face, -State.Normal, Slot);
			Check(State, bFound && Slot.Height == ECoverHeight::HighCover, TEXT("a high cover slot at the marker"));
			Check(State, bFound && Leader->OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted, TEXT("the leader takes it"));
			Next(State);
			return true;
		}
		case 2:
		{
			if (!Leader->bInCover && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Leader->bInCover, FString::Printf(TEXT("the leader is in cover at the wall (%.1f s)"), State.StageTime));
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.f)
			{
				return true; // 2 s in cover
			}
			float Nearest = 1.0e9f;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
				{
					Nearest = Mine.IsValid() ? FMath::Min(Nearest, static_cast<float>(FVector::Dist(Mine->GetActorLocation(), Member->GetActorLocation()))) : Nearest;
				}
			}
			Check(State, CountRevealed(State) == 0 && Leader->GetPeekSpottedCount() == 0,
				FString::Printf(TEXT("no spot before the peek (nearest operative %.1f m from a mine; spot range 4.5 / 6 m)"), Nearest / 100.f));
			// The real input: a click on the wall's end on his face, 20 cm in from the end.
			AStaticMeshActor* Wall = State.Wall.Get();
			const FBox Box = Wall->GetComponentsBoundingBox();
			const FVector E = Box.GetExtent();
			const float HalfLong = FMath::Abs(State.Along.X) > 0.5f ? E.X : E.Y;
			const float HalfThick = FMath::Abs(State.Normal.X) > 0.5f ? E.X : E.Y;
			const FVector EdgePoint = FVector(Box.GetCenter().X, Box.GetCenter().Y, State.GroundZ + 150.f) + State.Normal * HalfThick + State.Along * (HalfLong - 20.f);
			const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(), EdgePoint, State.Normal);
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			Check(State, PC->FindCornerPeekForHit(Leader, EdgeHit, Side, Edge), TEXT("the wall's end at the marker is a corner peek target"));
			Check(State, PC->TryHandleCoverClick(EdgeHit, nullptr) && Leader->IsManualCornerPeek(), TEXT("a click on it orders the corner peek"));
			Next(State);
			return true;
		}
		case 4:
		{
			if (CountRevealed(State) < 2 && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Leader->IsPeekingRoundCorner() || CountRevealed(State) == 2, TEXT("he leans out round the corner"));
			Check(State, CountRevealed(State) == 2 && Leader->GetPeekSpottedCount() == 2,
				FString::Printf(TEXT("both mines spotted by his peek (%d revealed, %d by the peek, %.1f s)"), CountRevealed(State), Leader->GetPeekSpottedCount(), State.StageTime));
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, State.Probe->CountEvent(TEXT("TrapSpotted.Peek")) == 2 && State.Probe->CountEvent(TEXT("TrapSpotted.A3PeekMines")) == 2,
				FString::Printf(TEXT("TrapSpotted.Peek / TrapSpotted.A3PeekMines raised (%d / %d)"), State.Probe->CountEvent(TEXT("TrapSpotted.Peek")),
					State.Probe->CountEvent(TEXT("TrapSpotted.A3PeekMines"))));
			Check(State, Squad->IsSoloMode() && Squad->IsTrapSolo(), TEXT("the squad holds in Solo (rev 5h trap hold)"));
			Check(State, Leader->bInCover && Leader->IsManualCornerPeek(), TEXT("the peeking leader keeps his corner pose"));
			if (Tutorial && Tutorial->IsTutorialActive())
			{
				Check(State, FMath::IsNearlyEqual(Tutorial->GetConditionStat(TEXT("traps_spotted.A3PeekMines")), 2.f), TEXT("tutorial stat traps_spotted.A3PeekMines = 2"));
				Check(State, Tutorial->GetStepState(TEXT("A3_peek")) == ETutorialStepState::Completed, TEXT("the card 'Peek around the corner and check what's ahead' completed"));
				const ETutorialStepState MineCard = Tutorial->GetStepState(TEXT("A3_mine"));
				Check(State, MineCard == ETutorialStepState::Queued || MineCard == ETutorialStepState::Active || MineCard == ETutorialStepState::Completed,
					FString::Printf(TEXT("the mine card (disarm / go round) follows (state %d)"), static_cast<int32>(MineCard)));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PeekMinesSmoke"),
		TEXT("Dev check (Demo 01 rev 5h): the A3 peek corner - no spot from cover, the corner peek reveals both mines beyond it, the card completes."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
