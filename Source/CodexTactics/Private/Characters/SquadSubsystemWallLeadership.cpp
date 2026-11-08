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
#include "Data/SquadWallTuning.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace SquadSubsystemWallLeadershipLocal
{
	TAutoConsoleVariable<int32> CVarFormationWallLeadership(TEXT("Codex.Formation.WallLeadership"), 1,
		TEXT("Game designer 2026-11-01: 1 = the squad's corner peek makes the member nearest that corner the formation leader, the others stack behind him along the near face; 0 = the v2.4 squad peek (the corner stack's point man peeks, the leader stays) - A/B"));

	TAutoConsoleVariable<int32> CVarFormationReformAlongWall(TEXT("Codex.Formation.ReformAlongWall"), 1,
		TEXT("The game designer's bug 2026-11-03 (a corner peek at the far end of the same long wall: all three stood up and faced their walk, the rear open): 1 = a member walking into the wall-leadership stack ALONG THE SAME WALL keeps his crouch and his look (squad_walls.json reform_along_wall_off_m); 0 = item 2's stand-up / face-the-walk for every far member (A/B)"));

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

bool USquadSubsystem::IsLeaderCoverPeekPlannable(const AOperativeCharacter& LeaderRef, bool bAllowLeaderRunPeek) const
{
	return IsWallLeadershipPeeker(&LeaderRef)
		|| (bAllowLeaderRunPeek && LeaderRef.bInCover && LeaderRef.IsManualCornerPeek() && LeaderRef.IsCoverEntryPeekStance());
}

bool USquadSubsystem::PlanWallLeadership(const FVector& EdgeOnFace, FWallLeadershipPlan& OutPlan, TArray<AOperativeCharacter*>& OutWho, bool bAllowLeaderRunPeek) const
{
	OutPlan = FWallLeadershipPlan();
	OutWho.Reset();
	AOperativeCharacter* LeaderRef = Leader.Get();
	// The leader in cover is the single operative's peek - unless it is the corner of this squad's own wall-leadership peek.
	if (!LeaderRef || !FormationRuntime::IsEnabled() || bIsSoloMode || bFollowersHolding || HasMultiSelection() || !IsFormationActive() || Followers.IsEmpty()
		|| Sectors.Formation != EFormationKind::Wall || !Sectors.bWallLayoutValid || (LeaderRef->bInCover && !IsLeaderCoverPeekPlannable(*LeaderRef, bAllowLeaderRunPeek)))
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
	// #57 two-corner hold: the other end of the wall while a corner is held - its corner man(s) keep their corners (not candidates); an end
	// held already - its own man (nobody swaps).
	AOperativeCharacter* EndHolder = nullptr;
	const FormationCornerHold::EPeekOrder HoldKind = ClassifyCornerHoldPeek(EdgeOnFace, &EndHolder);
	for (int32 Index = 0; Index < OutWho.Num(); ++Index)
	{
		const bool bCornerMan = OutWho[Index] == Sectors.Peek.Peeker.Get() || OutWho[Index] == Sectors.HeldCorner.Holder.Get();
		if ((HoldKind == FormationCornerHold::EPeekOrder::SecondCorner && bCornerMan) || (HoldKind == FormationCornerHold::EPeekOrder::AlreadyHeld && OutWho[Index] != EndHolder))
		{
			Candidates[Index].bAble = false;
		}
	}
	OutPlan = FormationWallLeadership::Plan(bWalls ? Sectors.WorldWalls : NoWalls, KSlot->Position, Sectors.WallNormalYaw, FVector2D(EdgeOnFace), Candidates,
		FormationTuning::Get());
	return OutWho.IsValidIndex(OutPlan.Leader);
}

bool USquadSubsystem::OrderWallLeadershipPeek(AOperativeCharacter* Peeker, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace, bool bRunPeek)
{
	using namespace SquadSubsystemWallLeadershipLocal;
	// #57 two-corner hold: the other end of the wall while a corner man peeks = the second corner (he keeps his); an end held already = nothing.
	{
		AOperativeCharacter* EndHolder = nullptr;
		const FormationCornerHold::EPeekOrder HoldKind = ClassifyCornerHoldPeek(EdgeOnFace, &EndHolder);
		if (HoldKind == FormationCornerHold::EPeekOrder::AlreadyHeld)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the peek at (%.0f, %.0f) - %s holds that corner already, nobody swaps"), EdgeOnFace.X, EdgeOnFace.Y,
				EndHolder ? *EndHolder->DisplayName.ToString() : TEXT("-"));
			return true;
		}
		if (HoldKind == FormationCornerHold::EPeekOrder::SecondCorner && OrderSecondCornerPeek(Peeker, CornerSlot, EdgeOnFace))
		{
			return true;
		}
	}
	AOperativeCharacter* OldLeader = Leader.Get();
	FWallLeadershipPlan Plan;
	TArray<AOperativeCharacter*> Who;
	if (!OldLeader || !Peeker || !CornerSlot.IsValid() || !PlanWallLeadership(EdgeOnFace, Plan, Who, bRunPeek) || Who[Plan.Leader] != Peeker)
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
		// The run-and-peek carry (#55 fix 2026-11-08): his walk into the corner goes straight into the peek stance (armed before the walk).
		const EOperativeOrderResult Result = bRunPeek ? Peeker->OrderTakeCoverForPeek(CornerSlot, EdgeOnFace, false) : Peeker->OrderTakeCover(CornerSlot, false);
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
	// Item 2 (game designer 2026-11-02: the stack ended right but took far too long - the others crept along crouched): a crouched member far
	// from his place in the stack stands up for the walk (squad_walls.json reform_stand_m); he crouches again at his place (SettleFormationWallPose).
	// The game designer's bug 2026-11-03 (L_Gen_Depot_1, the right-end peek after the left-end one at the same 31 m wall: all three stood up,
	// faced their walk and walked along the wall, the rear wide open): a walk ALONG THE SAME WALL keeps his crouch and his look (the
	// rear-looker walks backwards / sideways as before item 2); the stand-up and the face-the-walk rule only for a walk that leaves the wall.
	const FSquadWallTuning& WallTuning = SquadWallTuning::Get();
	const float StandCm = WallTuning.ReformStandCm;
	const float AlongOffCm = CVarFormationReformAlongWall.GetValueOnGameThread() != 0 ? WallTuning.ReformAlongWallOffCm : 0.f;
	for (int32 Index = 0; Index < Who.Num(); ++Index)
	{
		AOperativeCharacter* Member = Who[Index];
		const FFormationSlot* Place = Plan.Roles.IsValidIndex(Index)
			? Plan.StackSlots.FindByPredicate([&Plan, Index](const FFormationSlot& Each) { return Each.Role == Plan.Roles[Index]; }) : nullptr;
		FFollowerState* State = Member ? Followers.FindByPredicate([Member](const FFollowerState& Each) { return Each.Operative.Get() == Member; }) : nullptr;
		if (State)
		{
			State->bReformAlongWall = false;
			State->bReformTurned = false;
		}
		if (Index == Plan.Leader || !Member || !Place || !CanTakeWallLead(Member) || Member->bInCover)
		{
			continue;
		}
		const double Distance = FVector2D::Distance(FVector2D(Member->GetActorLocation()), Place->Position);
		const bool bAlongWall = FormationWallLeadership::ReformAlongWall(FVector2D(Member->GetActorLocation()), Place->Position, Sectors.WallNormalYaw, AlongOffCm);
		if (State)
		{
			State->bReformAlongWall = bAlongWall;
		}
		if (bAlongWall && bCrouch && Distance > StandCm && Member->GetStance() == EOperativeStance::Standing)
		{
			// The old peeker out of his (high) corner cover stands: the crouched wall order's walk along the wall is crouched too.
			Member->SetStance(EOperativeStance::Crouching);
			Member->MarkStanceFromSquad();
		}
		if (bAlongWall && Distance > StandCm)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: %s walks along the wall into his place in the stack (%.0f cm) - %s, keeps his look"),
				*Member->DisplayName.ToString(), Distance, Member->GetStance() == EOperativeStance::Crouching ? TEXT("crouched") : TEXT("in his stance"));
		}
		if (FormationWallLeadership::ReformStands(Distance, Member->GetStance() == EOperativeStance::Crouching, StandCm, bAlongWall))
		{
			Member->SetFormationWallPose(false);
			Member->SetStance(EOperativeStance::Standing);
			Member->MarkStanceFromSquad(); // the squad's stance: he crouches again at his place
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall leadership: %s stands up for the walk to his place in the stack (%.0f cm, off the wall)"), *Member->DisplayName.ToString(),
				Distance);
		}
	}
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
