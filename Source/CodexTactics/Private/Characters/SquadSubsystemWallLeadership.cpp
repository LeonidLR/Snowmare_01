// USquadSubsystem: wall leadership (the game designer's decisions 2026-11-01; behind Codex.Formation.Sectors + Codex.Formation.WallLeadership).
// The squad pressed to a wall, the player orders a peek round one of its corners: the member NEAREST that corner peeks and BECOMES THE
// FORMATION LEADER (selection, portrait frame and camera follow switch to him - SetLeader; the Commander keeps his own role / perks, only
// the formation leadership moves); all the others line up along the near face BEHIND him (a stack from the corner along the wall, in the
// order they stood, nobody passing him or anybody else on the way); a member who cannot take it is skipped. Leaving the wall keeps the new
// leader and the order (the places by travel from the stack). Before: the leader stayed, the stack's point man peeked and a man could step out past the corner.
// Pure rules: FormationWallLeadership (tests CodexTactics.Formation.WallLeadership.*).
// Godot reference: none (UE-only; docs/design/FORMATION_SECTORS.md "Wall leadership", docs/systems/squad-and-orders.md "Wall leadership").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallLeadershipRules.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace SquadSubsystemWallLeadershipLocal
{
	TAutoConsoleVariable<int32> CVarFormationWallLeadership(TEXT("Codex.Formation.WallLeadership"), 1,
		TEXT("Game designer 2026-11-01: 1 = the squad's corner peek makes the member nearest that corner the formation leader, the others stack behind him along the near face; 0 = the v2.4 squad peek (the corner stack's point man peeks, the leader stays) - A/B"));

	/** Already in cover at that wall within this of the corner slot: the peek from where he is (no walk), cm. */
	constexpr float InPlaceCm = 120.f;
}

bool USquadSubsystem::IsWallLeadershipEnabled()
{
	return SquadSubsystemWallLeadershipLocal::CVarFormationWallLeadership.GetValueOnGameThread() != 0;
}

bool USquadSubsystem::IsWallLeadershipPeeker(const AOperativeCharacter* Operative) const
{
	return Operative && IsWallLeadershipEnabled() && Sectors.Peek.bLeadership && Sectors.Peek.Peeker.Get() == Operative && Leader.Get() == Operative;
}

bool USquadSubsystem::CanTakeWallLead(const AOperativeCharacter* Operative) const
{
	return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKnockedDown() && !Operative->IsClimbingLadder()
		&& !Operative->IsPanicking() && !Operative->bGuarding
		&& Members.ContainsByPredicate([Operative](const TWeakObjectPtr<AOperativeCharacter>& Each) { return Each.Get() == Operative; });
}

bool USquadSubsystem::PlanWallLeadership(const FVector& EdgeOnFace, FWallLeadershipPlan& OutPlan, TArray<AOperativeCharacter*>& OutWho) const
{
	OutPlan = FWallLeadershipPlan();
	OutWho.Reset();
	AOperativeCharacter* LeaderRef = Leader.Get();
	// The leader in cover is the single operative's peek - unless it is the corner of this squad's own wall-leadership peek.
	if (!LeaderRef || !FormationRuntime::IsEnabled() || bIsSoloMode || bFollowersHolding || HasMultiSelection() || !IsFormationActive() || Followers.IsEmpty()
		|| Sectors.Formation != EFormationKind::Wall || !Sectors.bWallLayoutValid || (LeaderRef->bInCover && !IsWallLeadershipPeeker(LeaderRef)))
	{
		return false;
	}
	const TArray<FFormationSlot>& Start = Sectors.bSlide ? Sectors.FrameSlots : Sectors.WallSlots;
	const FFormationSlot* KSlot = Start.FindByPredicate([](const FFormationSlot& Each) { return Each.Role == EFormationRole::Commander; });
	if (!KSlot)
	{
		return false;
	}
	TArray<FWallLeadershipMember> Candidates;
	FWallLeadershipMember Lead;
	Lead.Role = EFormationRole::Commander;
	Lead.Position = FVector2D(LeaderRef->GetActorLocation());
	Lead.bAble = CanTakeWallLead(LeaderRef);
	Candidates.Add(Lead);
	OutWho.Add(LeaderRef);
	for (const FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (Operative && Role.IsSet())
		{
			FWallLeadershipMember Member;
			Member.Role = Role.GetValue();
			Member.Position = FVector2D(Operative->GetActorLocation());
			Member.bAble = CanTakeWallLead(Operative);
			Candidates.Add(Member);
			OutWho.Add(Operative);
		}
	}
	// The same walls the slide's corner stack reads (TryStartFormationSlide), so the plan and the squad's other moves agree.
	const TArray<FFormationWall> NoWalls;
	const bool bWalls = FormationGuardRuntime::IsEnabled() && Sectors.bWorldWallsValid;
	OutPlan = FormationWallLeadership::Plan(bWalls ? Sectors.WorldWalls : NoWalls, KSlot->Position, Sectors.WallNormalYaw, FVector2D(EdgeOnFace), Candidates,
		FormationTuning::Get());
	return OutWho.IsValidIndex(OutPlan.Leader);
}

bool USquadSubsystem::OrderWallLeadershipPeek(AOperativeCharacter* Peeker, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace)
{
	using namespace SquadSubsystemWallLeadershipLocal;
	AOperativeCharacter* OldLeader = Leader.Get();
	FWallLeadershipPlan Plan;
	TArray<AOperativeCharacter*> Who;
	if (!OldLeader || !Peeker || !CornerSlot.IsValid() || !PlanWallLeadership(EdgeOnFace, Plan, Who) || Who[Plan.Leader] != Peeker)
	{
		return false;
	}
	const bool bCrouch = Sectors.bWallOrder ? (Sectors.bSlide ? Sectors.bSlideFromOrder : Sectors.bWallOrderCrouch) : false;
	const AOperativeCharacter* OldPeeker = Sectors.Peek.Peeker.Get();
	EndFormationCornerPeek(TEXT("a new corner peek"));
	// A running slide was the old leader's move: the stack replaces it.
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;

	// Decisions 2 / 5: the peeker leads - a normal leader change (selection, portrait, camera). Decision 6 needs nothing more: leaving the
	// wall the places are dealt by travel from the stack (contract 10.6), so the man behind him stays behind him and the leader is kept.
	const bool bSwitch = Peeker != OldLeader;
	if (bSwitch)
	{
		TGuardValue<bool> Switching(bWallLeadershipSwitch, true);
		if (!SetLeader(Peeker, true) || Leader.Get() != Peeker)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: %s cannot take the lead"), *Peeker->DisplayName.ToString());
			return false;
		}
		++WallLeadershipSwitchCount;
	}
	// The wall order re-keyed: the new leader K, the old leader the new leader's old role, the rest unchanged (held by HoldCornerStack).
	Sectors.WallOrderRoles.Reset();
	for (int32 Index = 0; Index < Who.Num(); ++Index)
	{
		if (Index != Plan.Leader && Who[Index])
		{
			Sectors.WallOrderRoles.Add(Who[Index], Plan.Roles[Index]);
		}
	}
	Sectors.bWallOrder = true;
	// Everybody behind him walks into the stack: out of his cover / peek corner / prone spot / own cover walk first (the old peeker - the
	// old leader at the other corner - held his corner by the followers-in-cover rule while the new leader takes cover).
	for (FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		if (!Operative)
		{
			continue;
		}
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
		const bool bInCover = Operative->bInCover;
		const bool bPendingCover = Operative->HasPendingCover();
		const bool bProne = HasGroupCoverProne(Operative);
		if (bInCover || bPendingCover || bProne)
		{
			if (bPendingCover && !bInCover)
			{
				Operative->ClearPendingCover();
				Operative->StopOperative();
			}
			LeaveCoverForSquad(Follower, *Operative, *Peeker, TEXT("wall leadership: the stack behind the new leader"));
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: %s leaves his %s for his place behind %s"), *Operative->DisplayName.ToString(),
				bInCover ? (Operative == OldPeeker ? TEXT("peek corner") : TEXT("cover")) : bProne ? TEXT("prone spot") : TEXT("walk to a cover slot"),
				*Peeker->DisplayName.ToString());
		}
	}
	// The new leader to the corner: his own cover order (already in cover at that corner: the peek from where he is).
	const bool bInPlace = Peeker->bInCover && CoverTraceRules::IsSameWall(Peeker->GetCoverSlot(), CornerSlot)
		&& FVector::Dist2D(Peeker->GetCoverSlot().WorldLocation, CornerSlot.WorldLocation) <= InPlaceCm;
	if (!bInPlace)
	{
		const EOperativeOrderResult Result = Peeker->OrderTakeCover(CornerSlot, false);
		if (Result != EOperativeOrderResult::Accepted)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: %s cannot take the corner (%d)"), *Peeker->DisplayName.ToString(), static_cast<int32>(Result));
			// He leads anyway (the switch is done): the stack still forms behind where he stands.
		}
		else
		{
			// His walk to the corner runs along the wall: its cover entry turns straight into the peek (no wall-facing enter clip).
			Peeker->MarkCornerPeekArrival();
		}
	}
	HoldCornerStack(*Peeker, Plan.StackSlots, Plan.End, bCrouch);
	Sectors.WallOrderSerial = Peeker->GetMoveOrderSerial();
	Sectors.LastLeaderSerial = Sectors.WallOrderSerial;
	Sectors.bLastLeaderSerialValid = true;
	Sectors.bLeaderOrderPending = false;

	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	Peek = FFormationSectorState::FSquadPeek();
	Peek.Peeker = Peeker;
	Peek.Leader = Peeker;
	Peek.EdgeOnFace = EdgeOnFace;
	Peek.Side = CoverTraceRules::AlongWallDistance(CornerSlot, EdgeOnFace) >= 0.f ? ECoverFacing::Right : ECoverFacing::Left;
	Peek.LeaderSerial = Peeker->GetMoveOrderSerial();
	Peek.bPending = true;
	Peek.bAwaitStack = false;
	Peek.StackEnd = Plan.End;
	Peek.bLeadership = true;
	Peek.Line.Add(Peeker);
	Peek.LineAble.Add(true);
	FString Line;
	for (const int32 Index : Plan.Order)
	{
		Peek.Line.Add(Who[Index]);
		Peek.LineAble.Add(CanTakeWallLead(Who[Index]));
		Line += FString::Printf(TEXT(" %s%s;"), *Who[Index]->DisplayName.ToString(), Peek.LineAble.Last() ? TEXT("") : TEXT(" (cannot walk)"));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: the corner peek at (%.0f, %.0f) - %s takes the corner and the lead%s (the corner %s at (%.0f, %.0f)); behind him:%s"),
		EdgeOnFace.X, EdgeOnFace.Y, *Peeker->DisplayName.ToString(), bSwitch ? *FString::Printf(TEXT(" from %s"), *OldLeader->DisplayName.ToString()) : TEXT(" (he leads already)"),
		Plan.bWorldEnd ? TEXT("of the world walls") : TEXT("at the peek edge"), Plan.End.End.X, Plan.End.End.Y, *Line);
	return true;
}

FVector2D USquadSubsystem::HoldBehindInWallLine(const AOperativeCharacter& Operative, const FVector2D& Target) const
{
	const FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	if (!Peek.bLeadership || !Sectors.bCornerStack || !Sectors.bWallOrder || Peek.Line.Num() != Peek.LineAble.Num())
	{
		return Target;
	}
	const int32 Position = Peek.Line.IndexOfByPredicate([&Operative](const TWeakObjectPtr<AOperativeCharacter>& Each) { return Each.Get() == &Operative; });
	const int32 AheadAt = Position > 0 ? FormationWallLeadership::AheadInLine(Peek.LineAble, Position) : INDEX_NONE;
	const AOperativeCharacter* Ahead = Peek.Line.IsValidIndex(AheadAt) ? Peek.Line[AheadAt].Get() : nullptr;
	if (!Ahead)
	{
		return Target;
	}
	const auto RoleOf = [this](const AOperativeCharacter* Member) -> TOptional<EFormationRole>
	{
		if (Member == Leader.Get())
		{
			return EFormationRole::Commander;
		}
		const EFormationRole* Role = Sectors.WallOrderRoles.Find(const_cast<AOperativeCharacter*>(Member));
		return Role ? TOptional<EFormationRole>(*Role) : TOptional<EFormationRole>();
	};
	const TOptional<EFormationRole> Mine = RoleOf(&Operative);
	const TOptional<EFormationRole> Theirs = RoleOf(Ahead);
	const FFormationSlot* MyPlace = Mine.IsSet() ? Sectors.WallSlots.FindByPredicate([&Mine](const FFormationSlot& Each) { return Each.Role == Mine.GetValue(); }) : nullptr;
	const FFormationSlot* AheadPlace = Theirs.IsSet() ? Sectors.WallSlots.FindByPredicate([&Theirs](const FFormationSlot& Each) { return Each.Role == Theirs.GetValue(); }) : nullptr;
	if (!MyPlace || !AheadPlace)
	{
		return Target;
	}
	return FormationWallLeadership::HoldBehind(Target, MyPlace->Position, AheadPlace->Position, FVector2D(Ahead->GetActorLocation()), Sectors.CornerStackEnd.Along);
}
