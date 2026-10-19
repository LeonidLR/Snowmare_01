// Dev-only check of the Demo 01 rev 5h corner-peek lesson (game designer 2026-10-15) on /Game/Maps/L_Demo01_A_FrozenLake:
//   Scripts/smoke_parallel.ps1 -Commands PeekMinesSmoke     (map from Scripts/test_map.json)
// The map's additive section AutoLayout_Demo01_5h put two hidden mines (SpotEventTag A3PeekMines) beyond the corner of the
// first high wall of the A3 cover field and the marker A3_peek at that corner. Nothing is saved.
//  0. the mines and the marker are found (rev 5i: FOUR lesson mines tagged A3PeekMines - the two 5h peek mines + the two field
//     mines moved into the green zone beyond the peek corner by section 5i - each with SpotRequiresStep A3_peek);
//     rev 5i: BEFORE the peek lesson an operative is put 3 m from every lesson mine and in front of every Cover_A3_* wall face:
//     no mine is spotted (proximity spotting waits for the A3_peek step; only a peek may reveal them);
//  1. rev 5j (game designer 2026-10-19, section 5j): the cover lesson A3_cover forced - three labelled role rings (Medic-Sapper behind
//     the slab, Commander at the middle low wall, Engineer behind the front low wall); the Commander in cover at the ENGINEER's wall
//     does not count; each in cover at his own ring completes it (the rings hide); then the A3_peek card. After 2 s in cover NO
//     mine is spotted (no spot before the peek - beyond the proximity spot range, out of view behind the slab);
//  2. a click on the slab's end (the real input: FindCornerPeekForHit + TryHandleCoverClick) - the MEDIC-SAPPER leans out: each
//     lesson mine is spotted only once it lies in his peek cone (the sweeping reach from his leaned-out eyes, line of sight), one
//     by one, the nearest first; every lesson mine spotted by his peek (GetPeekSpottedCount), TrapSpotted.Peek + TrapSpotted.A3PeekMines raised, the squad
//     holds in Solo (rev 5h trap hold), the stat traps_spotted.A3PeekMines = their number, the card A3_peek completed and the mine card A3_mine
//     (disarm / go round) next.
// Needs the map with section 5j (the role rings, the mines in the medic's cone).

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
#include "Interactables/TrapSpotRules.h"
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
		/** Rev 5i: points an operative is put at before the peek lesson (3 m from each mine, in front of each cover wall face). */
		TArray<FVector> ProbePoints;
		int32 ProbeIndex = 0;
		float NearestProbeCm = 1.0e9f;
		/** Rev 5j: the role rings of A3_cover (by role id) and the operative of each role. */
		TMap<FName, TWeakObjectPtr<ATutorialMarkerActor>> RoleRings;
		TMap<FName, TWeakObjectPtr<AOperativeCharacter>> RoleOps;
		/** When the medic's lean-out began (stage time), each mine's reveal time after it, the reveal distances in order. */
		float PeekStart = -1.f;
		TMap<TWeakObjectPtr<AProximityMineActor>, float> RevealTimes;
		TArray<float> RevealDistances;
	};

	/** Orders Operative into cover at the wall of RoleId's ring (the ring stands 55 cm in front of the wall's approach face). */
	bool TakeCoverAtRing(UWorld* World, AOperativeCharacter& Operative, const FState& State, const TCHAR* RoleId)
	{
		const TWeakObjectPtr<ATutorialMarkerActor>* Ring = State.RoleRings.Find(FName(RoleId));
		if (!Ring || !Ring->IsValid())
		{
			return false;
		}
		const FVector Face = (*Ring)->GetActorLocation() - State.Normal * 25.f + FVector(0.f, 0.f, 60.f);
		FCoverSlot Slot;
		return CoverTraceRules::FindCoverSlotAt(World, Face, -State.Normal, Slot) && Operative.OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted;
	}

	/** Rev 5i: the probe points - never closer than 2.5 m to any mine (the trigger radius is 1.6 m). */
	void BuildProbePoints(UWorld* World, FState& State)
	{
		auto FarFromMines = [&State](const FVector& Point)
		{
			float Nearest = 1.0e9f;
			for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
			{
				Nearest = Mine.IsValid() ? FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(Mine->GetActorLocation(), Point))) : Nearest;
			}
			return Nearest;
		};
		for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
		{
			if (!Mine.IsValid())
			{
				continue;
			}
			FVector Best = FVector::ZeroVector;
			float BestClear = -1.f;
			for (int32 Dir = 0; Dir < 8; ++Dir)
			{
				const FVector Point = Mine->GetActorLocation() + FRotator(0.f, Dir * 45.f, 0.f).Vector() * 300.f;
				float Clear = 1.0e9f;
				for (const TWeakObjectPtr<AProximityMineActor>& Other : State.Mines)
				{
					if (Other.IsValid() && Other != Mine)
					{
						Clear = FMath::Min(Clear, static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), Point)));
					}
				}
				if (Clear > BestClear)
				{
					BestClear = Clear;
					Best = Point;
				}
			}
			if (BestClear >= 250.f)
			{
				State.ProbePoints.Add(Best);
			}
		}
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (!It->GetActorNameOrLabel().StartsWith(TEXT("Cover_A3_")))
			{
				continue;
			}
			const FBox Box = It->GetComponentsBoundingBox();
			const FVector C = Box.GetCenter();
			const FVector E = Box.GetExtent();
			const FVector Normal = E.Y > E.X ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
			const float Thick = E.Y > E.X ? E.X : E.Y;
			for (const float Side : { -1.f, 1.f })
			{
				const FVector Point = FVector(C.X, C.Y, Box.Min.Z) + Normal * Side * (Thick + 80.f);
				if (FarFromMines(Point) >= 250.f)
				{
					State.ProbePoints.Add(Point);
				}
			}
		}
	}

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
				if (It->StepId == FName(TEXT("A3_cover")) && !It->RoleId.IsNone())
				{
					State.RoleRings.Add(It->RoleId, *It);
				}
			}
			const ATutorialMarkerActor* Marker = State.Marker.Get();
			Check(State, State.Mines.Num() == 4 && CountRevealed(State) == 0, FString::Printf(TEXT("rev 5i: four hidden lesson mines tagged A3PeekMines (%d)"), State.Mines.Num()));
			int32 Gated = 0;
			for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
			{
				Gated += Mine.IsValid() && Mine->SpotRequiresStep == FName(TEXT("A3_peek")) ? 1 : 0;
			}
			Check(State, Gated == State.Mines.Num(), FString::Printf(TEXT("rev 5i: every lesson mine waits for the A3_peek step (%d / %d)"), Gated, State.Mines.Num()));
			Check(State, Marker != nullptr, TEXT("the marker A3_peek at the peek corner"));
			if (State.Mines.Num() < 2 || !Marker)
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
			Check(State, Tutorial && Tutorial->IsTutorialActive() && Tutorial->GetStepState(TEXT("A3_peek")) == ETutorialStepState::Idle,
				TEXT("rev 5i: the tutorial runs, the peek lesson has not started"));
			BuildProbePoints(World, State);
			Check(State, State.ProbePoints.Num() >= State.Mines.Num() + 4, FString::Printf(TEXT("rev 5i: %d probe points (3 m from each mine, in front of each Cover_A3_* face)"),
				State.ProbePoints.Num()));
			State.Stage = 10;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5i: no proximity spot before the peek lesson - an operative is put at every probe point in turn.
		case 10:
		{
			AOperativeCharacter* Prober = Squad->GetMembers().Num() > 1 ? Squad->GetMembers()[1] : Leader;
			if (State.ProbeIndex > 0 && State.StageTime < 0.6f)
			{
				return true;
			}
			if (State.ProbeIndex > 0)
			{
				for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
				{
					State.NearestProbeCm = Mine.IsValid() ? FMath::Min(State.NearestProbeCm, static_cast<float>(FVector::Dist(Mine->GetActorLocation(), Prober->GetActorLocation())))
						: State.NearestProbeCm;
				}
			}
			if (CountRevealed(State) > 0 || State.ProbeIndex >= State.ProbePoints.Num())
			{
				Check(State, CountRevealed(State) == 0, FString::Printf(TEXT("rev 5i: no lesson mine spotted from %d probe points before the peek lesson (nearest %.1f m; spot range 4.5 / 6 m)"),
					State.ProbeIndex, State.NearestProbeCm / 100.f));
				Next(State);
				return true;
			}
			Prober->StopOperative();
			Prober->TeleportTo(SmokeUtils::FreeSpot(World, State.ProbePoints[State.ProbeIndex] + FVector(0.f, 0.f, 100.f), Prober), Prober->GetActorRotation(), false, true);
			++State.ProbeIndex;
			State.StageTime = 0.f;
			return true;
		}
		case 11:
		{
			// 2026-10-19 (game designer): the cover lesson is role-specific - each role in cover at HIS ring (step A3_cover, section 5j).
			Check(State, State.RoleRings.Num() == 3, FString::Printf(TEXT("rev 5j: three role rings of A3_cover (%d)"), State.RoleRings.Num()));
			if (State.RoleRings.Num() != 3 || !Tutorial || !Tutorial->IsTutorialActive())
			{
				return Finish(State, false);
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				for (const TPair<FName, TWeakObjectPtr<ATutorialMarkerActor>>& Ring : State.RoleRings)
				{
					if (Ring.Value.IsValid() && UTutorialSubsystem::GetRoleId(*Member) == Ring.Key)
					{
						State.RoleOps.Add(Ring.Key, Member);
					}
				}
			}
			Check(State, State.RoleOps.Num() == 3, TEXT("rev 5j: an operative for every ring (Medic-Sapper, Commander, Engineer)"));
			if (State.RoleOps.Num() != 3)
			{
				return Finish(State, false);
			}
			if (!Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			Check(State, Tutorial->TriggerStep(TEXT("A3_cover")) && Tutorial->GetCurrentBeat() == FName(TEXT("A3")), TEXT("tutorial: the A3_cover card forced (beat A3)"));
			// Each one 1.5 m in front of his ring.
			for (const TPair<FName, TWeakObjectPtr<AOperativeCharacter>>& Pair : State.RoleOps)
			{
				AOperativeCharacter* Member = Pair.Value.Get();
				const FVector Ring = State.RoleRings[Pair.Key]->GetActorLocation();
				Member->StopOperative();
				Member->TeleportTo(SmokeUtils::FreeSpot(World, Ring + State.Normal * 150.f + FVector(0.f, 0.f, 100.f), Member), (-State.Normal).Rotation(), false, true);
			}
			State.Stage = 12;
			State.StageTime = 0.f;
			return true;
		}
		case 12:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_cover")) == ETutorialStepState::Active, TEXT("the 'take cover one by one' card is up"));
			int32 Shown = 0;
			for (const TPair<FName, TWeakObjectPtr<ATutorialMarkerActor>>& Ring : State.RoleRings)
			{
				Shown += Ring.Value.IsValid() && Ring.Value->IsMarkerShown() && !Ring.Value->Label.IsEmpty() ? 1 : 0;
			}
			Check(State, Shown == 3, FString::Printf(TEXT("rev 5j: all three labelled role rings shown (%d)"), Shown));
			// The Commander first walks into the ENGINEER's cover (the wrong spot).
			Check(State, TakeCoverAtRing(World, *State.RoleOps[TEXT("Commander")].Get(), State, TEXT("Engineer")), TEXT("the Commander takes cover at the ENGINEER's wall"));
			State.Stage = 13;
			State.StageTime = 0.f;
			return true;
		}
		case 13:
		{
			AOperativeCharacter* Commander = State.RoleOps[TEXT("Commander")].Get();
			if ((!Commander->bInCover || Commander->bShimmying || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Commander->bInCover && Tutorial->GetStepProgress(TEXT("A3_cover")) == 0,
				FString::Printf(TEXT("rev 5j: the Commander in cover at someone else's spot does not count (%d / 3)"), Tutorial->GetStepProgress(TEXT("A3_cover"))));
			for (const TCHAR* Role : { TEXT("Commander"), TEXT("Engineer"), TEXT("MedicSapper") })
			{
				Check(State, TakeCoverAtRing(World, *State.RoleOps[Role].Get(), State, Role), FString::Printf(TEXT("%s takes cover at his own ring"), Role));
			}
			State.Stage = 14;
			State.StageTime = 0.f;
			return true;
		}
		case 14:
		{
			if (Tutorial->GetStepState(TEXT("A3_cover")) != ETutorialStepState::Completed && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_cover")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("rev 5j: each role in cover at his own ring - the card done (%.1f s)"), State.StageTime));
			int32 Hidden = 0;
			for (const TPair<FName, TWeakObjectPtr<ATutorialMarkerActor>>& Ring : State.RoleRings)
			{
				Hidden += Ring.Value.IsValid() && !Ring.Value->IsMarkerShown() ? 1 : 0;
			}
			Check(State, Hidden == 3, TEXT("rev 5j: the rings are gone"));
			Squad->SetLeader(State.RoleOps[TEXT("MedicSapper")].Get());
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (Tutorial->GetStepState(TEXT("A3_peek")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_peek")) == ETutorialStepState::Active, TEXT("then the peek card"));
			Next(State);
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Medic = State.RoleOps[TEXT("MedicSapper")].Get();
			Check(State, Medic->bInCover && Medic->IsInHighCover(), FString::Printf(TEXT("the Medic-Sapper is in HIGH cover behind the slab (%s)"), *Medic->GetActorLocation().ToString()));
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 2.f)
			{
				return true; // 2 s in cover
			}
			AOperativeCharacter* Medic = State.RoleOps[TEXT("MedicSapper")].Get();
			float Nearest = 1.0e9f;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
				{
					Nearest = Mine.IsValid() ? FMath::Min(Nearest, static_cast<float>(FVector::Dist(Mine->GetActorLocation(), Member->GetActorLocation()))) : Nearest;
				}
			}
			Check(State, CountRevealed(State) == 0 && Medic->GetPeekSpottedCount() == 0,
				FString::Printf(TEXT("no spot before the peek (nearest operative %.1f m from a mine; spot range 4.5 / 6 m)"), Nearest / 100.f));
			// The real input: a click on the slab's end on his face, 20 cm in from the end.
			AStaticMeshActor* Wall = State.Wall.Get();
			const FBox Box = Wall->GetComponentsBoundingBox();
			const FVector E = Box.GetExtent();
			const float HalfLong = FMath::Abs(State.Along.X) > 0.5f ? E.X : E.Y;
			const float HalfThick = FMath::Abs(State.Normal.X) > 0.5f ? E.X : E.Y;
			const FVector EdgePoint = FVector(Box.GetCenter().X, Box.GetCenter().Y, State.GroundZ + 150.f) + State.Normal * HalfThick + State.Along * (HalfLong - 20.f);
			const FHitResult EdgeHit(Wall, Wall->GetStaticMeshComponent(), EdgePoint, State.Normal);
			ECoverFacing Side = ECoverFacing::Left;
			float Edge = 0.f;
			Check(State, PC->FindCornerPeekForHit(Medic, EdgeHit, Side, Edge), TEXT("the slab's end at his ring is a corner peek target"));
			Check(State, PC->TryHandleCoverClick(EdgeHit, nullptr) && Medic->IsManualCornerPeek(), TEXT("a click on it orders the Medic-Sapper's corner peek"));
			Next(State);
			return true;
		}
		case 4:
		{
			// 2026-10-19: the mines are spotted PHYSICALLY - each only when it lies in his current peek cone (the trap search's sweeping
			// reach, line of sight from the leaned-out eyes), one by one.
			AOperativeCharacter* Medic = State.RoleOps[TEXT("MedicSapper")].Get();
			if (Medic->IsPeekingRoundCorner() && State.PeekStart < 0.f)
			{
				State.PeekStart = State.StageTime;
			}
			FVector Eye;
			FVector Direction;
			const bool bPeek = Medic->GetPeekSight(Eye, Direction);
			for (const TWeakObjectPtr<AProximityMineActor>& Mine : State.Mines)
			{
				if (!Mine.IsValid() || !Mine->IsRevealed() || State.RevealTimes.Contains(Mine))
				{
					continue;
				}
				const FVector Target = Mine->GetActorLocation() + FVector(0.f, 0.f, Medic->PeekSpotConfig.TargetHeightCm);
				const float Since = State.PeekStart >= 0.f ? State.StageTime - State.PeekStart : -1.f;
				// The smoke samples every 0.1 s: the reach may have grown up to one step since the scan that found it.
				const float Reach = TrapSpotRules::PeekSweepRange(Medic->PeekSpotConfig.RangeCm, Medic->PeekSpotConfig.SweepSeconds, Since + 0.25f);
				const bool bInCone = bPeek && TrapSpotRules::IsInPeekSightCone(Eye, Direction, Target, Reach, Medic->PeekSpotConfig.HalfAngleDeg);
				Check(State, bInCone, FString::Printf(TEXT("%s spotted %.1f s into the peek, inside his peek cone (%.1f m from the leaned-out eyes, reach %.1f m)"),
					*Mine->GetName(), Since, FVector::Dist2D(Eye, Target) / 100.f, Reach / 100.f));
				State.RevealTimes.Add(Mine, Since);
				State.RevealDistances.Add(static_cast<float>(FVector::Dist2D(Eye, Target)));
			}
			if (CountRevealed(State) < State.Mines.Num() && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, CountRevealed(State) == State.Mines.Num() && Medic->GetPeekSpottedCount() == State.Mines.Num(),
				FString::Printf(TEXT("every lesson mine spotted by the Medic-Sapper's peek (%d revealed, %d by the peek, %.1f s)"), CountRevealed(State),
					Medic->GetPeekSpottedCount(), State.StageTime));
			TSet<int32> Moments;
			for (const TPair<TWeakObjectPtr<AProximityMineActor>, float>& Pair : State.RevealTimes)
			{
				Moments.Add(FMath::RoundToInt(Pair.Value * 10.f));
			}
			bool bNearestFirst = true;
			for (int32 Index = 1; Index < State.RevealDistances.Num(); ++Index)
			{
				bNearestFirst &= State.RevealDistances[Index] + 50.f >= State.RevealDistances[Index - 1];
			}
			Check(State, Moments.Num() >= 3 && bNearestFirst, FString::Printf(TEXT("one by one as the cone covers them, the nearest first (%d moments)"), Moments.Num()));
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, State.Probe->CountEvent(TEXT("TrapSpotted.Peek")) == State.Mines.Num() && State.Probe->CountEvent(TEXT("TrapSpotted.A3PeekMines")) == State.Mines.Num(),
				FString::Printf(TEXT("TrapSpotted.Peek / TrapSpotted.A3PeekMines raised (%d / %d)"), State.Probe->CountEvent(TEXT("TrapSpotted.Peek")),
					State.Probe->CountEvent(TEXT("TrapSpotted.A3PeekMines"))));
			Check(State, Squad->IsSoloMode() && Squad->IsTrapSolo(), TEXT("the squad holds in Solo (rev 5h trap hold)"));
			Check(State, State.RoleOps[TEXT("MedicSapper")]->bInCover && State.RoleOps[TEXT("MedicSapper")]->IsManualCornerPeek(),
				TEXT("the peeking Medic-Sapper keeps his corner pose"));
			if (Tutorial && Tutorial->IsTutorialActive())
			{
				Check(State, FMath::IsNearlyEqual(Tutorial->GetConditionStat(TEXT("traps_spotted.A3PeekMines")), static_cast<float>(State.Mines.Num())),
					FString::Printf(TEXT("tutorial stat traps_spotted.A3PeekMines = %d"), State.Mines.Num()));
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
		TEXT("Dev check (Demo 01 rev 5h / 5i): the A3 peek corner - no proximity spot of the lesson mines before the peek lesson, the corner peek reveals them all, the card completes."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
