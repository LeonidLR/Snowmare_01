// USquadSubsystem: the group cover order (game designer 2026-10-20) - who goes along with the leader's cover order, and the
// members without a slot who go prone behind the cover (arrival -> face the cover -> prone -> hold).
// UE-only (no Godot reference). Pure rules: Characters/SquadOrderRules::ShouldJoinGroupCover, Tactics/GroupCoverRules.
// The slots and the orders are issued by ACodexTacticsPlayerController::OrderGroupCover (CodexTacticsPlayerControllerCover.cpp).

#include "Characters/SquadSubsystem.h"

#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadOrderRules.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "NavigationSystem.h"
#include "Tactics/GroupCoverRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

TArray<AOperativeCharacter*> USquadSubsystem::GetGroupCoverMembers(const AOperativeCharacter* Direct) const
{
	TArray<AOperativeCharacter*> Result;
	const bool bMulti = HasMultiSelection();
	const bool bFormation = IsFormationActive();
	for (AOperativeCharacter* Member : GetMembers())
	{
		if (!Member)
		{
			continue;
		}
		SquadOrderRules::FGroupCoverJoinFacts Facts;
		Facts.bDirectOrder = Member == Direct;
		Facts.bAlive = Member->HealthComponent && Member->HealthComponent->IsAlive();
		Facts.bMultiSelection = bMulti;
		Facts.bSelected = IsGroupSelected(Member);
		Facts.bSoloMode = bIsSoloMode || bFollowersHolding; // a camera-zone perimeter hold keeps them too
		Facts.bFormationActive = bFormation;
		Facts.bFollower = Followers.ContainsByPredicate([Member](const FFollowerState& Follower) { return Follower.Operative.Get() == Member; });
		// One still waiting off the wall for the leader of the last group cover order (2026-10-23) goes along with the next one.
		Facts.bOnOwnOrder = !IsWaitingForGroupCoverLeader(Member) && IsOnOwnOrder(Member); // (any new order ends the wait)
		Facts.bUnavailable = VehicleQueries::IsMounted(Member) || VehicleQueries::IsBoarding(Member) || Member->IsTrafficHeld();
		if (SquadOrderRules::ShouldJoinGroupCover(Facts))
		{
			Result.Add(Member);
		}
	}
	return Result;
}

bool USquadSubsystem::OrderGroupCoverProne(AOperativeCharacter* Operative, const FVector& Spot, float FaceYaw, bool bSprint, bool bPlan, float Radius,
	FVector& OutDestination)
{
	OutDestination = Spot;
	if (!Operative)
	{
		return false;
	}
	FGroupCoverProneOrder Order;
	Order.Spot = Spot;
	Order.FaceYaw = FaceYaw;
	if (bPlan)
	{
		Operative->ClearPendingCover(); // the planned walk ends prone behind the cover, not in a slot planned earlier
		OutDestination = PlanMove(Operative, Spot, bSprint, Radius);
		Order.Spot = OutDestination;
		Order.Phase = ESquadGroupCoverPronePhase::Planned;
		GroupCoverProneOrders.Add(Operative, Order);
		return true;
	}
	if (Operative->OrderMoveTo(Spot, bSprint) != EOperativeOrderResult::Accepted)
	{
		GroupCoverProneOrders.Remove(Operative);
		return false;
	}
	Order.Serial = Operative->GetMoveOrderSerial();
	Order.Phase = ESquadGroupCoverPronePhase::Moving;
	GroupCoverProneOrders.Add(Operative, Order);
	UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: to the prone spot behind the cover at (%.0f, %.0f)%s"), *Operative->DisplayName.ToString(),
		Spot.X, Spot.Y, bSprint ? TEXT(" at a run") : TEXT(""));
	return true;
}

bool USquadSubsystem::HasGroupCoverProne(const AOperativeCharacter* Operative, bool bHoldingOnly) const
{
	const FGroupCoverProneOrder* Order = GroupCoverProneOrders.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
	return Order && (!bHoldingOnly || Order->Phase == ESquadGroupCoverPronePhase::Holding);
}

bool USquadSubsystem::GetGroupCoverProneSpot(const AOperativeCharacter* Operative, FVector& OutSpot) const
{
	const FGroupCoverProneOrder* Order = GroupCoverProneOrders.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
	if (Order)
	{
		OutSpot = Order->Spot;
	}
	return Order != nullptr;
}

void USquadSubsystem::DropGroupCoverProne(const AOperativeCharacter* Operative, const TCHAR* Why)
{
	if (Operative && GroupCoverProneOrders.Remove(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative))) > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: prone order ended (%s)"), *Operative->DisplayName.ToString(), Why);
	}
}

void USquadSubsystem::StartPlannedGroupCoverProne()
{
	for (auto It = GroupCoverProneOrders.CreateIterator(); It; ++It)
	{
		FGroupCoverProneOrder& Order = It->Value;
		if (Order.Phase != ESquadGroupCoverPronePhase::Planned)
		{
			continue;
		}
		AOperativeCharacter* Operative = It->Key.Get();
		const FPlannedOrder* Planned = PlannedOrders.Find(It->Key);
		// Re-planned elsewhere during the pause (another click for him): that plan is his order now.
		if (!Operative || !Planned || FVector::Dist2D(Planned->Destination, Order.Spot) > 10.f)
		{
			It.RemoveCurrent();
			continue;
		}
		Order.Serial = Operative->GetMoveOrderSerial();
		Order.Phase = ESquadGroupCoverPronePhase::Moving;
		Order.PhaseSeconds = 0.f;
	}
}

bool USquadSubsystem::OrderGroupCoverWait(AOperativeCharacter* Member, const FCoverSlot& Slot, AOperativeCharacter* CoverLeader, bool bHoldSlot,
	const TSharedRef<FGroupCoverSpares>& Spares)
{
	UWorld* World = GetWorld();
	if (!Member || !CoverLeader || !World || !Slot.IsValid())
	{
		return false;
	}
	FGroupCoverConfig Config;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector WaitPoint = FVector::ZeroVector;
	bool bFound = false;
	for (const float StandOff : { Config.WaitStandOffCm, Config.WaitStandOffCm * 0.7f })
	{
		Config.WaitStandOffCm = StandOff;
		const FVector Candidate = GroupCoverRules::ComputeWaitPoint(Slot.WorldLocation, Slot.WallNormal, Config);
		FNavLocation OnNav;
		if (!Nav || (Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(60.f, 60.f, 200.f)) && FVector::Dist2D(OnNav.Location, Candidate) <= 50.f))
		{
			WaitPoint = Nav ? OnNav.Location : Candidate;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		return false;
	}
	if (FVector::Dist2D(Member->GetActorLocation(), WaitPoint) > Config.WaitArriveCm)
	{
		if (Member->OrderMoveTo(WaitPoint, true) != EOperativeOrderResult::Accepted)
		{
			return false;
		}
	}
	else
	{
		Member->StopOperative();
	}
	Member->ClearPendingCover();
	FGroupCoverWaitOrder Order;
	Order.Leader = CoverLeader;
	Order.Slot = Slot;
	Order.WaitPoint = WaitPoint;
	Order.Serial = Member->GetMoveOrderSerial();
	Order.bHoldSlot = bHoldSlot;
	Order.Spares = Spares;
	GroupCoverWaitOrders.Add(Member, Order);
	UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: waits %.0f cm out from his slot until %s has taken cover"), *Member->DisplayName.ToString(),
		FVector::Dist2D(WaitPoint, Slot.WorldLocation), *CoverLeader->DisplayName.ToString());
	return true;
}

bool USquadSubsystem::IsWaitingForGroupCoverLeader(const AOperativeCharacter* Operative) const
{
	return Operative && GroupCoverWaitOrders.Contains(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
}

void USquadSubsystem::DropGroupCoverWait(const AOperativeCharacter* Operative, const TCHAR* Why)
{
	if (Operative && GroupCoverWaitOrders.Remove(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative))) > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: no longer waits for the leader (%s)"), *Operative->DisplayName.ToString(), Why);
	}
}

bool USquadSubsystem::IsGroupCoverPointTaken(const FVector& Point, const AOperativeCharacter* Except) const
{
	const FGroupCoverConfig Config;
	for (TActorIterator<AOperativeCharacter> It(GetWorld()); It; ++It)
	{
		const AOperativeCharacter* Other = *It;
		if (Other == Except || !Other->HealthComponent || !Other->HealthComponent->IsAlive())
		{
			continue;
		}
		if ((Other->bInCover && FVector::Dist2D(Other->GetCoverSlot().WorldLocation, Point) < Config.OccupiedRadiusCm)
			|| (Other->HasPendingCover() && FVector::Dist2D(Other->GetPendingCoverSlot().WorldLocation, Point) < Config.OccupiedRadiusCm)
			|| FVector::Dist2D(Other->GetActorLocation(), Point) < Config.OccupiedRadiusCm)
		{
			return true;
		}
	}
	return false;
}

void USquadSubsystem::ReleaseGroupCoverWait(AOperativeCharacter& Member, const FGroupCoverWaitOrder& Order, const TCHAR* Why)
{
	FCoverSlot Slot = Order.Slot;
	FGroupCoverSpares* Spares = Order.Spares.Get();
	if (IsGroupCoverPointTaken(Slot.WorldLocation, &Member))
	{
		// His slot was taken while he waited: the nearest free spare slot of the same wall, else prone behind the cover - never
		// standing in the open, never past the wall's edge (the spares are the order's own probed slots).
		int32 Pick = INDEX_NONE;
		if (Spares)
		{
			TArray<FVector2D> Points;
			TArray<bool> Taken;
			for (int32 Index = 0; Index < Spares->Slots.Num(); ++Index)
			{
				Points.Add(FVector2D(Spares->Slots[Index].WorldLocation));
				Taken.Add((Spares->SlotUsed.IsValidIndex(Index) && Spares->SlotUsed[Index]) || IsGroupCoverPointTaken(Spares->Slots[Index].WorldLocation, &Member));
			}
			Pick = GroupCoverRules::PickFreeSlot(Points, Taken, FVector2D(Slot.WorldLocation));
		}
		if (Pick != INDEX_NONE)
		{
			Spares->SlotUsed[Pick] = true;
			Slot = Spares->Slots[Pick];
			UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: his slot was taken - the next free one, %.0f cm along"), *Member.DisplayName.ToString(),
				FVector::Dist2D(Slot.WorldLocation, Order.Slot.WorldLocation));
		}
		else
		{
			for (int32 Index = 0; Spares && Index < Spares->ProneSpots.Num(); ++Index)
			{
				FVector Destination;
				if (!Spares->ProneUsed[Index] && !IsGroupCoverPointTaken(Spares->ProneSpots[Index], &Member)
					&& OrderGroupCoverProne(&Member, Spares->ProneSpots[Index], Spares->FaceYaw, true, false, 0.f, Destination))
				{
					Spares->ProneUsed[Index] = true;
					UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: his slot was taken, no free slot left - prone behind the cover (%s)"),
						*Member.DisplayName.ToString(), Why);
					ReallocateWallGroupForMember(Member, TEXT("a follower's re-entry (the prone fallback)"));
					return;
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: his slot was taken, no free slot / prone spot - into it anyway"), *Member.DisplayName.ToString());
		}
	}
	const EOperativeOrderResult Result = Member.OrderTakeCover(Slot, true);
	if (Result == EOperativeOrderResult::Accepted)
	{
		Member.SetHoldGroupCoverSlot(Order.bHoldSlot);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: into his slot after the leader (%s)%s"), *Member.DisplayName.ToString(), Why,
		Result == EOperativeOrderResult::Accepted ? TEXT("") : TEXT(" - refused"));
	ReallocateWallGroupForMember(Member, TEXT("a follower's re-entry (into his slot after the leader)"));
}

void USquadSubsystem::ReallocateWallGroupForMember(AOperativeCharacter& Member, const TCHAR* Why)
{
	// Rules v2.2: only a member of the wall group in force (a box selection's group cover has none).
	if (WallGroup.bActive && WallGroup.Members.Contains(&Member))
	{
		++WallGroup.Reentries;
		ReallocateWallGroup(WallGroup.Leader.Get(), { &Member }, Why);
	}
}

void USquadSubsystem::UpdateGroupCoverWait(float DeltaTime)
{
	if (GroupCoverWaitOrders.IsEmpty())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const bool bGrid = TurnBased && TurnBased->IsActive();
	const FGroupCoverConfig Config;
	TArray<TPair<TWeakObjectPtr<AOperativeCharacter>, FGroupCoverWaitOrder>> Released;
	for (auto It = GroupCoverWaitOrders.CreateIterator(); It; ++It)
	{
		AOperativeCharacter* Member = It->Key.Get();
		FGroupCoverWaitOrder& Order = It->Value;
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
		{
			It.RemoveCurrent();
			continue;
		}
		if (Member->IsKnockedDown())
		{
			Order.Serial = 0; // the buffered walk replays at the get-up (a new serial): taken as this one
		}
		else if (Order.Serial == 0)
		{
			Order.Serial = Member->GetMoveOrderSerial();
		}
		const TCHAR* DropWhy = bGrid ? TEXT("turn-based: the grid owns the positions")
			: Order.Serial != 0 && Member->GetMoveOrderSerial() != Order.Serial ? TEXT("a new order") : nullptr;
		if (DropWhy)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: no longer waits for the leader (%s)"), *Member->DisplayName.ToString(), DropWhy);
			It.RemoveCurrent();
			continue;
		}
		Order.WaitSeconds += DeltaTime;
		const AOperativeCharacter* CoverLeader = Order.Leader.Get();
		FGroupCoverWaitFacts Facts;
		Facts.bLeaderInCover = CoverLeader && CoverLeader->bInCover;
		Facts.bLeaderGoingToCover = CoverLeader && CoverLeader->HasPendingCover();
		Facts.bLeaderDown = !CoverLeader || !CoverLeader->HealthComponent || !CoverLeader->HealthComponent->IsAlive() || CoverLeader->IsKnockedDown();
		Order.LeaderIdleSeconds = (Facts.bLeaderGoingToCover && !Facts.bLeaderDown) ? 0.f : Order.LeaderIdleSeconds + DeltaTime;
		Facts.LeaderIdleSeconds = Order.LeaderIdleSeconds;
		Facts.WaitSeconds = Order.WaitSeconds;
		if (!Member->HasOwnMoveOrder() && FVector::Dist2D(Member->GetActorLocation(), Order.WaitPoint) <= Config.WaitArriveCm * 3.f)
		{
			Member->SetIdleFacingYaw((-Order.Slot.WallNormal.GetSafeNormal2D()).Rotation().Yaw); // waits facing the cover
		}
		if (GroupCoverRules::ShouldReleaseToSlot(Facts, Config))
		{
			Released.Emplace(It->Key, Order);
			It.RemoveCurrent();
		}
	}
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FGroupCoverWaitOrder>& Each : Released)
	{
		if (AOperativeCharacter* Member = Each.Key.Get())
		{
			const AOperativeCharacter* CoverLeader = Each.Value.Leader.Get();
			ReleaseGroupCoverWait(*Member, Each.Value, CoverLeader && CoverLeader->bInCover ? TEXT("the leader is in cover")
				: Each.Value.WaitSeconds >= Config.MaxWaitSeconds ? TEXT("waited too long") : TEXT("the leader's cover order ended"));
		}
	}
}

void USquadSubsystem::UpdateGroupCoverProne(float DeltaTime)
{
	if (GroupCoverProneOrders.IsEmpty())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bGrid = TurnBased && TurnBased->IsActive();
	const bool bPause = Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const FGroupCoverConfig Config;
	for (auto It = GroupCoverProneOrders.CreateIterator(); It; ++It)
	{
		AOperativeCharacter* Operative = It->Key.Get();
		FGroupCoverProneOrder& Order = It->Value;
		if (!Operative || !Operative->HealthComponent || !Operative->HealthComponent->IsAlive())
		{
			It.RemoveCurrent();
			continue;
		}
		Order.PhaseSeconds += DeltaTime;
		const TCHAR* DropWhy = nullptr;
		const float ToSpot = FVector::Dist2D(Operative->GetActorLocation(), Order.Spot);
		if (bGrid)
		{
			DropWhy = TEXT("turn-based: the grid owns the positions");
		}
		else if (Order.Phase == ESquadGroupCoverPronePhase::Planned)
		{
			if (!bPause && !PlannedOrders.Contains(It->Key))
			{
				DropWhy = TEXT("the plan was dropped");
			}
		}
		else if (Order.Phase == ESquadGroupCoverPronePhase::Moving)
		{
			if (Operative->IsKnockedDown())
			{
				Order.Serial = 0; // the buffered order replays at the get-up (a new serial): taken as this one
			}
			else if (Order.Serial == 0)
			{
				Order.Serial = Operative->GetMoveOrderSerial();
			}
			else if (Operative->GetMoveOrderSerial() != Order.Serial)
			{
				DropWhy = TEXT("a new order");
			}
			else if (!Operative->HasOwnMoveOrder())
			{
				if (!Order.bRetried && ToSpot > Config.ProneSettleCm && ToSpot <= Config.ProneRetryCm)
				{
					// Stopped short (a body in the way, the avoidance): one walk onto the spot itself.
					Order.bRetried = true;
					if (Operative->OrderMoveTo(Order.Spot, false) == EOperativeOrderResult::Accepted)
					{
						Order.Serial = Operative->GetMoveOrderSerial();
					}
				}
				else if (ToSpot > Config.ProneArriveCm)
				{
					DropWhy = TEXT("did not reach the spot");
				}
				else
				{
					Operative->SetIdleFacingYaw(Order.FaceYaw);
					Order.Phase = ESquadGroupCoverPronePhase::Facing;
					Order.PhaseSeconds = 0.f;
				}
			}
		}
		else if (Operative->GetMoveOrderSerial() != Order.Serial)
		{
			DropWhy = TEXT("a new order");
		}
		else if (Order.Phase == ESquadGroupCoverPronePhase::Facing)
		{
			Operative->SetIdleFacingYaw(Order.FaceYaw);
			const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(Operative->GetActorRotation().Yaw, Order.FaceYaw));
			const bool bStill = Operative->GetVelocity().SizeSquared2D() < FMath::Square(20.f); // lies down in place, no dive
			if ((Off <= Config.ProneFaceToleranceDeg && bStill) || Order.PhaseSeconds >= Config.ProneFaceMaxSeconds)
			{
				if (Operative->GetStance() != EOperativeStance::Prone)
				{
					Operative->DropProne();
				}
				Order.Phase = ESquadGroupCoverPronePhase::Holding;
				Order.PhaseSeconds = 0.f;
				UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: prone behind the cover (%.0f cm off the spot, facing %.0f deg off)"),
					*Operative->DisplayName.ToString(), ToSpot, Off);
				ReallocateWallGroupForMember(*Operative, TEXT("a follower's re-entry (prone behind the cover)"));
			}
		}
		else // Holding
		{
			if (Order.PhaseSeconds > 1.5f && Operative->GetStance() != EOperativeStance::Prone && Operative->GetMoveHoldSeconds() <= 0.f)
			{
				DropWhy = TEXT("stance changed");
			}
			else if (ToSpot > Config.ProneArriveCm + 100.f)
			{
				DropWhy = TEXT("moved away");
			}
		}
		if (DropWhy)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[GroupCover] %s: prone order ended (%s)"), *Operative->DisplayName.ToString(), DropWhy);
			It.RemoveCurrent();
		}
	}
}
