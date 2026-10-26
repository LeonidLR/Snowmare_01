// USquadSubsystem: the squad's corner peek in the formation wall layout (behind Codex.Formation.Sectors). User bug report 2026-10-26: "one
// operative hugging a wall: hovering its corner shows the peek silhouette and a click peeks round it; the squad hugging it in formation has
// neither" - the wall layout puts nobody in a cover slot, and the corner peek (hover and click) was the cover slot's only. Now the member
// nearest the hovered end takes the corner in cover and peeks (AOperativeCharacter::OrderCornerPeek, the single operative's order); the others
// form the v2.4 corner stack there (FormationWallEnd::PlanSquadPeek / StackSlots: K second, the rear man back along the wall) or hold.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad's corner peek").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallEndRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"

namespace SquadSubsystemPeekLocal
{
	/** The leader's move into the corner stack becomes the slide / the stack within this (his stand-up clip, LeaderOrderDecideMaxSeconds), s. */
	constexpr float PeekAwaitStackSeconds = 4.f;
	/** The peeker reaches his corner within this (a long wall walked crouched included), s. */
	constexpr float PeekReachMaxSeconds = 45.f;
}

bool USquadSubsystem::PlanFormationCornerPeek(const FVector& EdgeOnFace, AOperativeCharacter*& OutPeeker, FFormationSquadPeek& OutPlan) const
{
	OutPeeker = nullptr;
	AOperativeCharacter* LeaderRef = Leader.Get();
	if (!LeaderRef || !FormationRuntime::IsEnabled() || bIsSoloMode || bFollowersHolding || HasMultiSelection() || !IsFormationActive() || Followers.IsEmpty()
		|| Sectors.Formation != EFormationKind::Wall || !Sectors.bWallLayoutValid || LeaderRef->bInCover)
	{
		return false;
	}
	const TArray<FFormationSlot>& Start = Sectors.bSlide ? Sectors.FrameSlots : Sectors.WallSlots;
	if (Start.IsEmpty() || Start[0].Role != EFormationRole::Commander)
	{
		return false;
	}
	// Who stands where (the leader at K).
	TArray<FFormationSlot> Places;
	TArray<AOperativeCharacter*> Who;
	FFormationSlot Lead;
	Lead.Role = EFormationRole::Commander;
	Lead.Position = FVector2D(LeaderRef->GetActorLocation());
	Places.Add(Lead);
	Who.Add(LeaderRef);
	for (const FFollowerState& Follower : Followers)
	{
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (Role.IsSet() && Follower.Operative.IsValid())
		{
			FFormationSlot Member;
			Member.Role = Role.GetValue();
			Member.Position = FVector2D(Follower.Operative->GetActorLocation());
			Places.Add(Member);
			Who.Add(Follower.Operative.Get());
		}
	}
	// The same walls the slide reads for its corner stack (TryStartFormationSlide), so the plan and the move agree.
	const TArray<FFormationWall> NoWalls;
	const bool bWalls = FormationGuardRuntime::IsEnabled() && Sectors.bWorldWallsValid;
	OutPlan = FormationWallEnd::PlanSquadPeek(bWalls ? Sectors.WorldWalls : NoWalls, Start, Sectors.WallNormalYaw, FVector2D(EdgeOnFace), Places, FormationTuning::Get());
	for (int32 Index = 0; Index < Places.Num(); ++Index)
	{
		if (Places[Index].Role == OutPlan.Peeker)
		{
			OutPeeker = Who[Index];
			break;
		}
	}
	return OutPeeker != nullptr;
}

bool USquadSubsystem::OrderFormationCornerPeek(AOperativeCharacter* Peeker, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace)
{
	AOperativeCharacter* LeaderRef = Leader.Get();
	AOperativeCharacter* Planned = nullptr;
	FFormationSquadPeek Plan;
	if (!LeaderRef || !Peeker || !CornerSlot.IsValid() || !PlanFormationCornerPeek(EdgeOnFace, Planned, Plan) || Planned != Peeker)
	{
		return false;
	}
	const bool bCrouch = Sectors.bWallOrder ? (Sectors.bSlide ? Sectors.bSlideFromOrder : Sectors.bWallOrderCrouch) : false;
	const TArray<FFormationSlot> Start = Sectors.bSlide ? Sectors.FrameSlots : Sectors.WallSlots;
	EndFormationCornerPeek(TEXT("a new corner peek"));
	const bool bLeaderPeeks = Peeker == LeaderRef;
	if (bLeaderPeeks && !Plan.bStack && !Sectors.bWallOrder)
	{
		// The automatic layout becomes the squad's wall order while he peeks (his walk to the corner is no "next order" of the squad's).
		HoldWallLayoutAt(*LeaderRef, Sectors.WallNormalYaw, false, TOptional<bool>());
	}
	// The peeker to the corner: his own cover order (in cover at that wall already: the peek from where he is).
	const bool bInPlace = Peeker->bInCover && CoverTraceRules::IsSameWall(Peeker->GetCoverSlot(), CornerSlot);
	if (!bInPlace)
	{
		const EOperativeOrderResult Result = Peeker->OrderTakeCover(CornerSlot, false);
		if (Result != EOperativeOrderResult::Accepted)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] squad corner peek: %s cannot take the corner (%d)"), *Peeker->DisplayName.ToString(), static_cast<int32>(Result));
			return false;
		}
	}
	if (bLeaderPeeks)
	{
		// His walk to the corner is the squad's order (the stack round his corner; not read as a slide / a new order).
		if (Plan.bStack)
		{
			Sectors.bSlide = false;
			Sectors.bSlideStack = false;
			HoldCornerStack(*LeaderRef, Start, Plan.End, bCrouch);
		}
		Sectors.WallOrderSerial = LeaderRef->GetMoveOrderSerial();
		Sectors.LastLeaderSerial = Sectors.WallOrderSerial;
		Sectors.bLastLeaderSerialValid = true;
		Sectors.bLeaderOrderPending = false;
	}
	else if (Plan.bStack)
	{
		// The leader's move into the stack (the ground click at the wall's end would give the same): once it runs, TryStartFormationSlide makes
		// it the wall slide into the corner stack (or the stack in place) with the peeker's place at the edge.
		LeaderRef->OrderMoveTo(FVector(Plan.StackDestination.X, Plan.StackDestination.Y, LeaderRef->GetActorLocation().Z), false);
	}
	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	Peek = FFormationSectorState::FSquadPeek();
	Peek.Peeker = Peeker;
	Peek.Leader = LeaderRef;
	Peek.EdgeOnFace = EdgeOnFace;
	Peek.Side = CoverTraceRules::AlongWallDistance(CornerSlot, EdgeOnFace) >= 0.f ? ECoverFacing::Right : ECoverFacing::Left;
	Peek.LeaderSerial = LeaderRef->GetMoveOrderSerial();
	Peek.bPending = true;
	Peek.bAwaitStack = Plan.bStack && !bLeaderPeeks;
	FString Line;
	for (const FFormationSlot& Slot : Plan.StackSlots)
	{
		Line += FString::Printf(TEXT(" %s (%.0f, %.0f);"), FormationNames::ToString(Slot.Role), Slot.Position.X, Slot.Position.Y);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] squad corner peek at (%.0f, %.0f): %s (%s%s) takes the corner%s%s"), EdgeOnFace.X, EdgeOnFace.Y,
		*Peeker->DisplayName.ToString(), FormationNames::ToString(Plan.Peeker), bLeaderPeeks ? TEXT(", the leader") : TEXT(""),
		bInPlace ? TEXT(" where he is") : TEXT(""), Plan.bStack ? *FString::Printf(TEXT(", the others stack behind him:%s"), *Line) : TEXT(", the others hold"));
	return true;
}

void USquadSubsystem::UpdateFormationCornerPeek(AOperativeCharacter& LeaderRef, float DeltaTime)
{
	using namespace SquadSubsystemPeekLocal;
	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	AOperativeCharacter* Peeker = Peek.Peeker.Get();
	if (!Peeker)
	{
		if (Peek.Leader.IsValid())
		{
			EndFormationCornerPeek(TEXT("the peeker is gone"));
		}
		return;
	}
	Peek.Seconds += DeltaTime;
	const uint32 Serial = LeaderRef.GetMoveOrderSerial();
	// The leader's move into the stack became the squad's slide / the stack (TryStartFormationSlide gave him that move): the peek goes on with it.
	if (Peek.bAwaitStack && (Sectors.bCornerStack || (Sectors.bSlide && Sectors.bSlideStack)))
	{
		Peek.bAwaitStack = false;
		if (Serial == (Sectors.bSlide ? Sectors.SlideSerial : Sectors.WallOrderSerial))
		{
			Peek.LeaderSerial = Serial;
		}
	}
	const TCHAR* Over = nullptr;
	if (Peek.Leader.Get() != &LeaderRef)
	{
		Over = TEXT("the leader changed");
	}
	else if (Serial != Peek.LeaderSerial)
	{
		Over = TEXT("the leader's next order");
	}
	else if (Peek.bAwaitStack && Peek.Seconds > PeekAwaitStackSeconds)
	{
		Over = TEXT("the corner stack never started");
	}
	else if (!Peek.bAwaitStack && !Sectors.bWallOrder && !Sectors.bSlide && Sectors.Formation != EFormationKind::Wall)
	{
		Over = TEXT("the wall layout is over");
	}
	else if (Peek.bPending && Peek.Seconds > PeekReachMaxSeconds)
	{
		Over = TEXT("he did not reach the corner");
	}
	else if (Peek.bPending && !Peeker->bInCover && !Peeker->HasPendingCover())
	{
		Over = TEXT("his way to the corner ended");
	}
	else if (!Peek.bPending && !Peeker->bInCover)
	{
		Over = TEXT("he left the corner");
	}
	if (Over)
	{
		EndFormationCornerPeek(Over);
		return;
	}
	if (!Peek.bPending || !Peeker->IsInPeekableCover() || Peeker->bShimmying || Peeker->HasPendingCover())
	{
		return;
	}
	// In cover at the corner: the single operative's peek order round that end (the edge from his own slot; its stand point is where he is).
	const FCoverSlot& Slot = Peeker->GetCoverSlot();
	ECoverFacing Side = Peek.Side;
	float EdgeAlong = CoverTraceRules::AlongWallDistance(Slot, Peek.EdgeOnFace);
	ECoverFacing FoundSide = Side;
	float FoundAlong = EdgeAlong;
	if (CoverFacingRules::FindPeekEdge(Slot, Peek.EdgeOnFace, nullptr, FoundSide, FoundAlong))
	{
		Side = FoundSide;
		EdgeAlong = FoundAlong;
	}
	Peek.bPending = false;
	const EOperativeOrderResult Result = Peeker->OrderCornerPeek(Side, EdgeAlong);
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] squad corner peek: %s at the corner after %.1f s - the peek round the %s end (edge %.0f cm along) -> %d"),
		*Peeker->DisplayName.ToString(), Peek.Seconds, Side == ECoverFacing::Left ? TEXT("left") : TEXT("right"), EdgeAlong, static_cast<int32>(Result));
	if (Result != EOperativeOrderResult::Accepted)
	{
		EndFormationCornerPeek(TEXT("the peek order was refused"));
		return;
	}
	++Sectors.PeekCount;
}

void USquadSubsystem::EndFormationCornerPeek(const TCHAR* Why)
{
	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	if (const AOperativeCharacter* Peeker = Peek.Peeker.Get())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] squad corner peek over after %.1f s (%s%s): %s"), Peek.Seconds, *Peeker->DisplayName.ToString(),
			Peek.bPending ? TEXT(", the peek still due") : TEXT(""), Why);
	}
	Peek = FFormationSectorState::FSquadPeek();
}
