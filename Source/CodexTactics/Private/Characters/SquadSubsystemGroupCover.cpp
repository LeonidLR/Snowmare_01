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
#include "GameFlow/GameFlowSubsystem.h"
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
		Facts.bOnOwnOrder = IsOnOwnOrder(Member);
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
