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
#include "Navigation/CrowdFollowingComponent.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace SquadSubsystemFormationConfinedLocal
{
	/** Speed below which the leader counts as standing (the pocket round where he stands), cm/s. */
	constexpr float ConfinedStandingSpeed = 15.f;
	/** The entry line's clearance samples stop this short of the place (a place may stand near a wall), cm. */
	constexpr double EntryEndSkipCm = 30.0;
	/** The pocket centre must project onto the navmesh within this box, cm. */
	const FVector ConfinedProjectExtent(50.f, 50.f, 250.f);

	TAutoConsoleVariable<int32> CVarConfinedCrowd(TEXT("Codex.Formation.ConfinedCrowd"), 0,
		TEXT("Designer decision 2026-11-21 (the whole squad enters a flat hall): 0 = while the formation is compressed in a confined pocket the followers' ")
		TEXT("crowd separation and obstacle avoidance are off (their 4 m query range round the standing leader and his men held them at the hall door - ")
		TEXT("velocity 0 with the move running; the capsules still collide); 1 = kept on (A/B)"));
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
		if (bNewPocket)
		{
			// The door the squad walks in through: toward where the leader is now (kept on a refresh of the same pocket).
			Confined.EntryFrom.Reset();
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

	// The entry point: where the leader first walks within the pocket radius (just inside the door) - set once per pocket.
	const float Radius = FormationConfinedRules::PocketRadius(Confined.Pocket, Tuning);
	const double LeaderToCenter = FVector2D::Distance(FVector2D(Feet), Confined.Pocket.Center);
	if (Confined.EntryFrom.IsSet() && FVector2D::Distance(Confined.EntryFrom.GetValue(), Confined.Pocket.Center) > Radius
		&& LeaderToCenter <= Radius && LeaderToCenter > Tuning.WayMinCm)
	{
		Confined.EntryFrom = FVector2D(Feet);
	}

	// This frame's follower slots with their men.
	TArray<FFormationConfinedSlot> Slots;
	TArray<int32> SlotIndices;
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
		SlotIndices.Add(Index);
	}
	if (Slots.IsEmpty())
	{
		Off(TEXT("no follower slots"));
		return;
	}
	const FVector2D LeaderAt(Feet);
	// Lead review 2026-11-22: the navmesh line tests (a place round a jamb corner behind the door is no place). The entry test is cached per
	// pocket and entry point (its clearance samples are the costly part); a follower's own straight line is one raycast.
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
	const double Z = Confined.CenterZ;
	if (!Confined.EntryFrom.IsSet() || !Confined.EntryCacheFrom.Equals(Confined.EntryFrom.GetValue(), 1.0) || Confined.EntryCacheStamp != Confined.ProbeStamp)
	{
		Confined.EntryCache.Reset();
		Confined.EntryCacheFrom = Confined.EntryFrom.Get(FVector2D::ZeroVector);
		Confined.EntryCacheStamp = Confined.ProbeStamp;
	}
	FFormationConfinedLineTests Lines;
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
		const FVector2D From = Confined.EntryFrom.Get(Confined.Pocket.Center);
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
	const FFormationConfinedResult Result = FormationConfinedRules::CompressSlots(Confined.Pocket, Slots, LeaderAt, Confined.EntryFrom, Tuning, &Lines);
	if (!Result.bConfined)
	{
		Off(TEXT("open ground"));
		return;
	}
	for (int32 Each = 0; Each < SlotIndices.Num(); ++Each)
	{
		Sectors.FrameSlots[SlotIndices[Each]].Position = Result.Places[Each];
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
