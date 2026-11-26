// USquadSubsystem: the formation compresses in a confined space (the game designer's decision 2026-11-21, "the whole squad must enter" a
// ~5 m2 khrushchevka flat hall and the 2.5 m gable-vestibule corridors). The pure rule is FormationConfinedRules (Formation/FormationConfinedRules.h);
// this file probes the navmesh pocket round the leader's arrival and moves this frame's follower slots into it (after the formation's own slots,
// before the leader-corner rule). Godot reference: none (UE-only). In-game check: LevelGenBuildingsWalkSmoke's flat-entry squad legs.

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "Formation/FormationConfinedRules.h"
#include "AIController.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderClimbComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

namespace SquadSubsystemFormationConfinedLocal
{
	/** Speed below which the leader counts as standing (the pocket round where he stands), cm/s. */
	constexpr float ConfinedStandingSpeed = 15.f;
	/** The throat test needs a centre this much roomier than ThroatClearCm (FindDistanceToWall at the centre >= ThroatClearCm / this). */
	constexpr float ThroatCentreRatio = 0.7f;
	/** ... and samples the line from this far out of the centre to this short of the place, cm. */
	constexpr double ThroatStartCm = 40.0;
	constexpr double ThroatEndSkipCm = 10.0;
	/** The entry line's clearance samples stop this short of the place (a place may stand near a wall), cm. */
	constexpr double EntryEndSkipCm = 30.0;
	/** The pocket centre must project onto the navmesh within this box, cm. */
	const FVector ConfinedProjectExtent(50.f, 50.f, 250.f);

	TAutoConsoleVariable<int32> CVarConfinedCrowd(TEXT("Codex.Formation.ConfinedCrowd"), 0,
		TEXT("Designer decision 2026-11-21 (the whole squad enters a flat hall): 0 = while the formation is compressed in a confined pocket the followers' ")
		TEXT("crowd separation and obstacle avoidance are off (their 4 m query range round the standing leader and his men held them at the hall door - ")
		TEXT("velocity 0 with the move running; the capsules still collide); 1 = kept on (A/B)"));
	/** Entry order: the followers' navmesh ways to the inside entry point are found again this often, s. */
	constexpr float QueueRefreshSeconds = 0.1f;
	/** Entry order block detection: a man counts as away from his place beyond this, cm. */
	constexpr float BlockAwayCm = 50.f;
	/** The operatives' own crowd separation weight (OperativeAIController::OnPossess), restored when the rule ends. */
	constexpr float OperativeSeparationWeight = 2.f;

	int32 FormationDebugLevel()
	{
		static const IConsoleVariable* DebugVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Debug"));
		return DebugVar ? DebugVar->GetInt() : 0;
	}
}

void USquadSubsystem::ApplyFormationConfined(const AOperativeCharacter& LeaderRef, bool bLeaderMoving, float DeltaTime)
{
	using namespace SquadSubsystemFormationConfinedLocal;
	const FFormationConfinedTuning& Tuning = FormationConfinedTuning::Get();
	const auto Off = [this](const TCHAR* Why) { EndFormationConfined(Why, /*bKeepPocket*/ true); };
	// The player's wall order and its slide place every man themselves; a box selection walks own orders.
	if (!FormationConfinedRules::IsEnabled(Tuning) || Sectors.bWallOrder || Sectors.bSlide || HasMultiSelection() || Sectors.FrameSlots.IsEmpty())
	{
		Confined.bPocketValid = false;
		Off(TEXT("not in force"));
		return;
	}
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		Off(TEXT("no navigation"));
		return;
	}

	// The pocket centre: his destination as he arrives (within arrive_m, on his level), else where he stands; walking elsewhere - none.
	const FVector Feet = LeaderRef.GetFloorReferenceLocation();
	const FVector Destination = LeaderRef.GetLastMoveDestination();
	TOptional<FVector> Wanted;
	if (!Destination.IsZero() && FVector::Dist2D(Destination, Feet) <= Tuning.ArriveCm && FMath::Abs(Destination.Z - Feet.Z) <= Tuning.LevelDzCm)
	{
		Wanted = Destination;
	}
	else if (!bLeaderMoving && LeaderRef.GetVelocity().Size2D() < ConfinedStandingSpeed)
	{
		Wanted = Feet;
	}
	if (!Wanted.IsSet())
	{
		Confined.bPocketValid = false;
		Off(TEXT("the leader walks on"));
		return;
	}

	// Probe again only when the wanted centre moved (or refresh_s passed): the pocket and its candidate grid stay put while he settles.
	Confined.AgeSeconds += DeltaTime;
	bool bFindEntry = false;
	const bool bNewPocket = !Confined.bPocketValid || FVector2D::Distance(Confined.Pocket.Center, FVector2D(Wanted.GetValue())) > Tuning.RecenterCm;
	if (bNewPocket || Confined.AgeSeconds >= Tuning.RefreshSeconds)
	{
		Confined.bPocketValid = false;
		Confined.AgeSeconds = 0.f;
		FNavLocation Start;
		if (!Nav->ProjectPointToNavigation(Wanted.GetValue(), Start, ConfinedProjectExtent))
		{
			Off(TEXT("the centre is off the navmesh"));
			return;
		}
		Confined.Pocket.Center = FVector2D(Start.Location);
		Confined.RoomCache.Reset();
		if (bNewPocket)
		{
			Confined.HeldPlaces.Reset();
			Confined.QueueRanks.Reset();
			Confined.ToEntryCm.Reset();
			Confined.ToEntryAge = TNumericLimits<float>::Max();
			Confined.StillSeconds.Reset();
			Confined.SwapCooldown.Reset();
			// The door the squad walks in through: toward where the leader is now (kept on a refresh of the same pocket).
			Confined.EntryFrom.Reset();
			Confined.EntryInside.Reset();
			bFindEntry = true;
			if (FVector2D::Distance(FVector2D(Feet), Confined.Pocket.Center) > Tuning.WayMinCm)
			{
				Confined.EntryFrom = FVector2D(Feet);
			}
		}
		Confined.CenterZ = Start.Location.Z;
		Confined.Pocket.Free.Reset();
		for (int32 Ray = 0; Ray < Tuning.ProbeRays; ++Ray)
		{
			const FVector2D Dir = FormationConfinedRules::RayDir(Ray, Tuning.ProbeRays);
			const FVector End = Start.Location + FVector(Dir * Tuning.ProbeCm, 0.0);
			FVector Hit;
			const bool bHit = UNavigationSystemV1::NavigationRaycast(World, Start.Location, End, Hit);
			Confined.Pocket.Free.Add(bHit ? static_cast<float>(FVector::Dist2D(Start.Location, Hit)) : Tuning.ProbeCm);
		}
		Confined.bPocketValid = true;
		++Confined.ProbeStamp;
		if (FormationDebugLevel() >= 1)
		{
			FString Rays;
			for (const float Free : Confined.Pocket.Free)
			{
				Rays += FString::Printf(TEXT(" %.0f"), Free);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined probe at (%.0f, %.0f): median %.0f cm (confined <= %.0f) -> %s; free%s"),
				Confined.Pocket.Center.X, Confined.Pocket.Center.Y, FormationConfinedRules::Median(Confined.Pocket.Free), Tuning.ConfinedMedianCm,
				FormationConfinedRules::IsConfined(Confined.Pocket, Tuning) ? TEXT("confined") : TEXT("open"), *Rays);
		}
	}

	// The navmesh line tests (Lead review 2026-11-22: a place round a jamb corner behind the door is no place; round 2: none beyond a throat).
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
	const double Z = Confined.CenterZ;
	FFormationConfinedLineTests Lines;
	Lines.InRoom = [this, Recast, Z, &Tuning](const FVector2D& Place)
	{
		if (!Recast || Tuning.ThroatClearCm <= 0.f)
		{
			return true;
		}
		const FIntPoint Key(FMath::RoundToInt32(Place.X), FMath::RoundToInt32(Place.Y));
		if (const bool* Known = Confined.RoomCache.Find(Key))
		{
			return *Known;
		}
		const FVector2D From = Confined.Pocket.Center;
		// A roomy centre only (a one-body-wide hall is narrow everywhere - no throat to tell).
		bool bIn = Recast->FindDistanceToWall(FVector(From, Z), nullptr, Tuning.ThroatClearCm / ThroatCentreRatio + 1.f) < Tuning.ThroatClearCm / ThroatCentreRatio;
		const double Length = FVector2D::Distance(From, Place);
		for (double At = ThroatStartCm; !bIn && At < Length - ThroatEndSkipCm; At += FMath::Max(Tuning.JambSampleCm, 5.f))
		{
			if (Recast->FindDistanceToWall(FVector(From + (Place - From) * (At / Length), Z), nullptr, Tuning.ThroatClearCm + 1.f) < Tuning.ThroatClearCm)
			{
				Confined.RoomCache.Add(Key, false);
				return false;
			}
		}
		Confined.RoomCache.Add(Key, true);
		return true;
	};

	// Entry order (depth queue): the squad's navmesh way in - the leader's path to the centre, else the farthest follower's - gives the inside
	// entry point (just inside the doorway) and the door-side point entry_back_cm back along it, fixed for the pocket. (Why Lead review round 2's
	// entry point inside the door raised the slow legs: it was taken from the leader's walk, so it moved mid-walk - into the door throat, where
	// the jamb test saw no place at all, the filter was dropped and the places were dealt again as the men reached the door, one of them right
	// at the door's inner mouth: the 78-80 cm blocker. B_y diagnostics 2026-11-22.)
	const bool bQueue = FormationConfinedRules::IsQueueEnabled(Tuning);
	if (bFindEntry && bQueue)
	{
		TArray<FVector> Starts{ Feet };
		const AOperativeCharacter* Farthest = nullptr;
		for (const FFollowerState& Follower : Followers)
		{
			const AOperativeCharacter* Operative = Follower.Operative.Get();
			if (Operative && (!Farthest || FVector2D::DistSquared(FVector2D(Operative->GetActorLocation()), Confined.Pocket.Center)
				> FVector2D::DistSquared(FVector2D(Farthest->GetActorLocation()), Confined.Pocket.Center)))
			{
				Farthest = Operative;
			}
		}
		if (Farthest)
		{
			Starts.Add(Farthest->GetFloorReferenceLocation());
		}
		const ANavigationData* NavData = Nav->GetNavDataForProps(LeaderRef.GetNavAgentPropertiesRef(), Feet);
		for (const FVector& From : Starts)
		{
			if (!NavData || Confined.EntryInside.IsSet())
			{
				break;
			}
			FNavLocation FromNav;
			if (!Nav->ProjectPointToNavigation(From, FromNav, ConfinedProjectExtent, NavData))
			{
				continue;
			}
			FPathFindingQuery Query(&LeaderRef, *NavData, FromNav.Location, FVector(Confined.Pocket.Center, Confined.CenterZ));
			const FPathFindingResult Path = Nav->FindPathSync(LeaderRef.GetNavAgentPropertiesRef(), Query);
			if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
			{
				continue;
			}
			TArray<FVector2D> Way;
			for (const FNavPathPoint& Point : Path.Path->GetPathPoints())
			{
				Way.Add(FVector2D(Point.Location));
			}
			if (const TOptional<FFormationConfinedEntry> Entry = FormationConfinedRules::EntryOnPath(Confined.Pocket, Way, Tuning, Lines.InRoom); Entry.IsSet())
			{
				Confined.EntryInside = Entry->Inside;
				Confined.EntryFrom = Entry->Outside;
			}
		}
		if (FormationDebugLevel() >= 1)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined entry: %s"), Confined.EntryInside.IsSet()
				? *FString::Printf(TEXT("inside (%.0f, %.0f), door side (%.0f, %.0f)"), Confined.EntryInside->X, Confined.EntryInside->Y,
					Confined.EntryFrom->X, Confined.EntryFrom->Y)
				: TEXT("none on the way in (the cost rule)"));
		}
	}
	if (!bQueue)
	{
		Confined.EntryInside.Reset();
	}

	// No way-in entry: the entry point is where the leader first walks within the pocket radius - set once per pocket.
	const float Radius = FormationConfinedRules::PocketRadius(Confined.Pocket, Tuning);
	const double LeaderToCenter = FVector2D::Distance(FVector2D(Feet), Confined.Pocket.Center);
	if (!Confined.EntryInside.IsSet() && Confined.EntryFrom.IsSet() && FVector2D::Distance(Confined.EntryFrom.GetValue(), Confined.Pocket.Center) > Radius
		&& LeaderToCenter <= Radius && LeaderToCenter > Tuning.WayMinCm)
	{
		Confined.EntryFrom = FVector2D(Feet);
	}

	// This frame's follower slots with their men.
	TArray<FFormationConfinedSlot> Slots;
	TArray<int32> SlotIndices;
	TArray<TWeakObjectPtr<AOperativeCharacter>> SlotMembers;
	for (const FFollowerState& Follower : Followers)
	{
		const AOperativeCharacter* Operative = Follower.Operative.Get();
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (!Operative || !Role.IsSet())
		{
			continue;
		}
		const int32 Index = Sectors.FrameSlots.IndexOfByPredicate([&Role](const FFormationSlot& Slot) { return Slot.Role == Role.GetValue(); });
		if (Index == INDEX_NONE || SlotIndices.Contains(Index))
		{
			continue;
		}
		FFormationConfinedSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Desired = Sectors.FrameSlots[Index].Position;
		Slot.Occupant = FVector2D(Operative->GetActorLocation());
		if (const FVector2D* Held = Tuning.bHoldPlaces ? Confined.HeldPlaces.Find(Follower.Operative) : nullptr)
		{
			Slot.Held = *Held;
		}
		SlotIndices.Add(Index);
		SlotMembers.Add(Follower.Operative);
	}
	if (Slots.IsEmpty())
	{
		Off(TEXT("no follower slots"));
		return;
	}
	// Entry order (depth queue): each man's navmesh way to the inside entry point (refreshed every QueueRefreshSeconds), his last rank, and
	// the block detection - standing away from his place with a team-mate on his way (they swap places, then a cooldown).
	FFormationConfinedQueue Queue;
	const bool bQueued = bQueue && Confined.EntryInside.IsSet();
	if (bQueued)
	{
		Queue.EntryInside = Confined.EntryInside.GetValue();
		Confined.ToEntryAge += DeltaTime;
		const bool bRefreshWays = Confined.ToEntryAge >= QueueRefreshSeconds;
		if (bRefreshWays)
		{
			Confined.ToEntryAge = 0.f;
		}
		const ANavigationData* NavData = Nav->GetNavDataForProps(LeaderRef.GetNavAgentPropertiesRef(), Feet);
		const FVector Inside(Queue.EntryInside, Confined.CenterZ);
		for (int32 Each = 0; Each < Slots.Num(); ++Each)
		{
			AOperativeCharacter* Operative = SlotMembers[Each].Get();
			if (!Operative)
			{
				continue;
			}
			if (bRefreshWays || !Confined.ToEntryCm.Contains(SlotMembers[Each]))
			{
				float Way = static_cast<float>(FVector2D::Distance(FVector2D(Operative->GetActorLocation()), Queue.EntryInside));
				FNavLocation FromNav;
				if (NavData && Nav->ProjectPointToNavigation(Operative->GetFloorReferenceLocation(), FromNav, ConfinedProjectExtent, NavData))
				{
					FPathFindingQuery Query(Operative, *NavData, FromNav.Location, Inside);
					const FPathFindingResult Path = Nav->FindPathSync(Operative->GetNavAgentPropertiesRef(), Query);
					if (Path.IsSuccessful() && Path.Path.IsValid() && !Path.Path->IsPartial())
					{
						Way = static_cast<float>(Path.Path->GetLength());
					}
				}
				Confined.ToEntryCm.Add(SlotMembers[Each], Way);
			}
			Slots[Each].ToEntryCm = Confined.ToEntryCm.FindRef(SlotMembers[Each]);
			if (const int32* Rank = Confined.QueueRanks.Find(SlotMembers[Each]))
			{
				Slots[Each].PrevRank = *Rank;
			}
			// Block detection on his place from last frame.
			float& Cooldown = Confined.SwapCooldown.FindOrAdd(SlotMembers[Each]);
			Cooldown = FMath::Max(0.f, Cooldown - DeltaTime);
			float& Still = Confined.StillSeconds.FindOrAdd(SlotMembers[Each]);
			const FVector2D* Place = Confined.HeldPlaces.Find(SlotMembers[Each]);
			const FVector2D Here(Operative->GetActorLocation());
			if (!Place || FVector2D::Distance(Here, *Place) <= BlockAwayCm || Operative->GetVelocity().Size2D() >= Tuning.BlockSpeedCmS)
			{
				Still = 0.f;
				continue;
			}
			Still += DeltaTime;
			if (Still < Tuning.BlockSeconds || Cooldown > 0.f)
			{
				continue;
			}
			for (int32 Other = 0; Other < Slots.Num(); ++Other)
			{
				const AOperativeCharacter* Mate = SlotMembers[Other].Get();
				if (Other == Each || !Mate || !Slots[Other].Held.IsSet() || !Slots[Each].Held.IsSet() || Confined.SwapCooldown.FindRef(SlotMembers[Other]) > 0.f
					|| !FormationConfinedRules::IsBlockedBy(Here, *Place, FVector2D(Mate->GetActorLocation()), Tuning))
				{
					continue;
				}
				Slots[Each].BlockedBy = Other;
				Still = 0.f;
				Cooldown = Tuning.BlockCooldownSeconds;
				Confined.SwapCooldown.Add(SlotMembers[Other], Tuning.BlockCooldownSeconds);
				UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined queue: %s stands blocked at (%.0f, %.0f) by %s at (%.0f, %.0f) on his way to (%.0f, %.0f) - they swap places"),
					*Operative->DisplayName.ToString(), Here.X, Here.Y, *Mate->DisplayName.ToString(), Mate->GetActorLocation().X, Mate->GetActorLocation().Y,
					Place->X, Place->Y);
				break;
			}
		}
	}
	const FVector2D LeaderAt(Feet);
	// The entry test is cached per pocket and entry point (its clearance samples are the costly part); a follower's own straight line is one raycast.
	// Entry order: with an inside entry point the jamb line test runs from it (just past the throat: the clearance samples see the room, not
	// the jambs - from the door side every line through a narrow door cut its jamb and nothing was seen); the entry line stays on the door side.
	const TOptional<FVector2D> LineFrom = Confined.EntryInside.IsSet() ? Confined.EntryInside : Confined.EntryFrom;
	if (!LineFrom.IsSet() || !Confined.EntryCacheFrom.Equals(LineFrom.GetValue(), 1.0) || Confined.EntryCacheStamp != Confined.ProbeStamp)
	{
		Confined.EntryCache.Reset();
		Confined.EntryCacheFrom = LineFrom.Get(FVector2D::ZeroVector);
		Confined.EntryCacheStamp = Confined.ProbeStamp;
	}
	Lines.Straight = [World, Z](const FVector2D& From, const FVector2D& To)
	{
		FVector Hit;
		return !UNavigationSystemV1::NavigationRaycast(World, FVector(From, Z), FVector(To, Z), Hit);
	};
	Lines.FromEntry = [this, World, Recast, Z, &Tuning](const FVector2D& Place)
	{
		const FIntPoint Key(FMath::RoundToInt32(Place.X), FMath::RoundToInt32(Place.Y));
		if (const bool* Known = Confined.EntryCache.Find(Key))
		{
			return *Known;
		}
		const FVector2D From = Confined.EntryInside.IsSet() ? Confined.EntryInside.GetValue() : Confined.EntryFrom.Get(Confined.Pocket.Center);
		FVector Hit;
		bool bClear = !UNavigationSystemV1::NavigationRaycast(World, FVector(From, Z), FVector(Place, Z), Hit);
		const double Length = FVector2D::Distance(From, Place);
		if (bClear && Recast && Tuning.JambClearCm > 0.f && Length > EntryEndSkipCm)
		{
			// Every sample but the place's own end keeps the clearance from the navmesh edge: a line that cuts a jamb corner comes within it.
			const int32 Steps = FMath::Max(1, FMath::CeilToInt32((Length - EntryEndSkipCm) / FMath::Max(Tuning.JambSampleCm, 5.f)));
			for (int32 Step = 0; Step <= Steps && bClear; ++Step)
			{
				const FVector2D At = From + (Place - From) * ((Length - EntryEndSkipCm) * Step / Steps / Length);
				bClear = Recast->FindDistanceToWall(FVector(At, Z), nullptr, Tuning.JambClearCm + 1.f) >= Tuning.JambClearCm;
			}
		}
		Confined.EntryCache.Add(Key, bClear);
		return bClear;
	};
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Confined.Pocket, Slots, LeaderAt, Confined.EntryFrom, Tuning, &Lines,
		bQueued ? &Queue : nullptr);
	if (!Result.bConfined)
	{
		Off(TEXT("open ground"));
		return;
	}
	Confined.HeldPlaces.Reset();
	Confined.QueueRanks.Reset();
	FString QueueText;
	for (int32 Each = 0; Each < SlotIndices.Num(); ++Each)
	{
		Sectors.FrameSlots[SlotIndices[Each]].Position = Result.Places[Each];
		if (Result.Replaced[Each])
		{
			Confined.HeldPlaces.Add(SlotMembers[Each], Result.Places[Each]);
		}
		if (Result.bQueued && Result.Rank[Each] != INDEX_NONE)
		{
			Confined.QueueRanks.Add(SlotMembers[Each], Result.Rank[Each]);
			const AOperativeCharacter* Operative = SlotMembers[Each].Get();
			QueueText += FString::Printf(TEXT(" %s #%d%s (%.0f, %.0f) depth %.0f;"), Operative ? *Operative->DisplayName.ToString() : TEXT("?"), Result.Rank[Each] + 1,
				Result.Locked[Each] ? TEXT(" in") : TEXT(""), Result.Places[Each].X, Result.Places[Each].Y, FVector2D::Distance(Result.Places[Each], Queue.EntryInside));
		}
	}
	// The queue's order and its places, once per change (a man through the door, a re-deal).
	if (Result.bQueued && QueueText != Confined.LastLoggedQueue)
	{
		Confined.LastLoggedQueue = QueueText;
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined queue from (%.0f, %.0f):%s"), Queue.EntryInside.X, Queue.EntryInside.Y, *QueueText);
	}
	if (CVarConfinedCrowd.GetValueOnGameThread() == 0)
	{
		for (const FFollowerState& Follower : Followers)
		{
			AOperativeCharacter* Operative = Follower.Operative.Get();
			if (Operative && SectorRoleOf(Follower).IsSet() && !Confined.NoSeparation.Contains(Follower.Operative))
			{
				SetConfinedCrowdSeparation(*Operative, false);
				Confined.NoSeparation.Add(Follower.Operative);
			}
		}
	}
	if (!Confined.bActive || Confined.ReplacedCount != Result.ReplacedCount)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined: %d of %d follower slot(s) moved into the pocket at (%.0f, %.0f) (median %.0f cm, %s)"),
			Result.ReplacedCount, Slots.Num(), Confined.Pocket.Center.X, Confined.Pocket.Center.Y, FormationConfinedRules::Median(Confined.Pocket.Free),
			FormationNames::ToString(Sectors.Formation));
	}
	// Codex.Formation.Debug 2: the line tests' effect and the places given, once per change of places.
	if (FormationDebugLevel() >= 2)
	{
		FString Places;
		for (int32 Each = 0; Each < SlotIndices.Num(); ++Each)
		{
			Places += FString::Printf(TEXT(" (%.0f, %.0f)%s"), Result.Places[Each].X, Result.Places[Each].Y, Result.Replaced[Each] ? TEXT("") : TEXT(" kept"));
		}
		if (Places != Confined.LastLoggedPlaces)
		{
			Confined.LastLoggedPlaces = Places;
			int32 Seen = 0;
			int32 InRoom = 0;
			const TArray<FVector2D> All = FormationConfinedRules::Candidates(Confined.Pocket, Tuning);
			for (const FVector2D& Each : All)
			{
				Seen += Lines.FromEntry(Each) ? 1 : 0;
				InRoom += Lines.InRoom(Each) ? 1 : 0;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined places:%s - candidates %d, seen from the door %d (entry %s), in the room %d; centre wall distance %.0f cm"),
				*Places, All.Num(), Seen, Confined.EntryFrom.IsSet() ? *FString::Printf(TEXT("(%.0f, %.0f)"), Confined.EntryFrom->X, Confined.EntryFrom->Y) : TEXT("-"),
				InRoom, Recast ? Recast->FindDistanceToWall(FVector(Confined.Pocket.Center, Z), nullptr, 500.f) : -1.0);
		}
	}
	// The leader makes room (2026-11-22, the walk smoke's STUCK hall legs): a follower by the door found the standing leader first on his way
	// with no side to step round him - the leader steps straight away from the inside entry point (once per player order, at most
	// leader_room_max_cm, on the navmesh); the pocket follows him and the queue is dealt again round his new spot.
	const bool bRoomRequest = Confined.bLeaderRoomRequest;
	Confined.bLeaderRoomRequest = false;
	AOperativeCharacter* const LeaderMutable = GetLeader();
	// A ladder pauses the path and owns the resume goal, including its queue and exit. It is not an idle leader to move aside.
	const ULadderClimbComponent* LeaderLadder = LeaderRef.FindComponentByClass<ULadderClimbComponent>();
	if (bRoomRequest && Result.bQueued && LeaderMutable == &LeaderRef && !bLeaderMoving && LeaderRef.GetVelocity().Size2D() < ConfinedStandingSpeed
		&& !LeaderRef.HasOwnMoveOrder() && !(LeaderLadder && LeaderLadder->IsBusy()) && Confined.LeaderRoomSerial.Get(LeaderRef.GetMoveOrderSerial() + 1u) != LeaderRef.GetMoveOrderSerial())
	{
		const FVector2D EntryIn = Queue.EntryInside;
		const FVector2D Away = (LeaderAt - EntryIn).GetSafeNormal();
		const float ProbeCm = Tuning.LeaderRoomMaxCm + Tuning.WallMarginCm + 1.f;
		FVector Hit;
		const float FreeAhead = UNavigationSystemV1::NavigationRaycast(World, Feet, Feet + FVector(Away * ProbeCm, 0.0), Hit)
			? static_cast<float>(FVector::Dist2D(Feet, Hit)) : ProbeCm;
		const TOptional<FVector2D> Step = FormationConfinedRules::LeaderRoomStep(LeaderAt, EntryIn, FreeAhead, Tuning);
		FNavLocation OnNav;
		if (Step.IsSet() && Nav->ProjectPointToNavigation(FVector(Step.GetValue(), Feet.Z), OnNav, ConfinedProjectExtent))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined: the leader makes room - %s at (%.0f, %.0f), %.0f cm from the door's inside point (%.0f, %.0f), steps to (%.0f, %.0f)"),
				*LeaderRef.DisplayName.ToString(), LeaderAt.X, LeaderAt.Y, FVector2D::Distance(LeaderAt, EntryIn), EntryIn.X, EntryIn.Y, OnNav.Location.X, OnNav.Location.Y);
			LeaderMutable->OrderMoveTo(OnNav.Location, /*bSprint*/ false);
		}
		Confined.LeaderRoomSerial = LeaderRef.GetMoveOrderSerial(); // tried for this order (stepped or not): never again until his next one
	}
	Confined.bActive = true;
	Confined.ReplacedCount = Result.ReplacedCount;
}

void USquadSubsystem::SetConfinedCrowdSeparation(AOperativeCharacter& Operative, bool bSeparation)
{
	const AAIController* AI = Cast<AAIController>(Operative.GetController());
	if (UCrowdFollowingComponent* Crowd = AI ? Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent()) : nullptr)
	{
		Crowd->SetCrowdSeparation(bSeparation, false);
		if (bSeparation)
		{
			Crowd->SetCrowdSeparationWeight(SquadSubsystemFormationConfinedLocal::OperativeSeparationWeight, false);
		}
		Crowd->SetCrowdObstacleAvoidance(bSeparation, true); // one agent update for both
	}
}

void USquadSubsystem::EndFormationConfined(const TCHAR* Why, bool bKeepPocket)
{
	for (const TWeakObjectPtr<AOperativeCharacter>& Each : Confined.NoSeparation)
	{
		if (AOperativeCharacter* Operative = Each.Get())
		{
			SetConfinedCrowdSeparation(*Operative, true);
		}
	}
	Confined.NoSeparation.Reset();
	Confined.HeldPlaces.Reset();
	Confined.QueueRanks.Reset();
	Confined.StillSeconds.Reset();
	Confined.LastLoggedQueue.Reset();
	if (Confined.bActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] confined: off (%s)"), Why);
	}
	Confined.bActive = false;
	Confined.ReplacedCount = 0;
	if (!bKeepPocket)
	{
		Confined.bPocketValid = false;
	}
}
