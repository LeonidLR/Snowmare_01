// USquadSubsystem: the two-corner hold at a wall (#57, user request 2026-11-07: "the squad pressed against a wall, a peek on the right sends
// the right man to that corner; then a peek on the left sends the left man there - but the right man leaves his position. Each of them must
// hold his own edge. The one left in the middle should scan from the left corner to the right"). Formation mode, wall leadership: a peek at
// the OTHER end of the same wall while the first corner man peeks = the second corner - its end man takes it (and, as any wall-leadership
// peek, the lead), the first corner man keeps his corner (IsFormationPeeker: the formation leaves him there), nobody else moves; the men
// between them sweep their look between the two corner looks (ApplyCornerHoldSweep). Behind Codex.Formation.TwoCornerHold.
// Pure rules: FormationCornerHold (tests CodexTactics.Formation.CornerHold.*).
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Both corners held").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Data/SquadWallTuning.h"
#include "Formation/FormationCornerHoldRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace SquadSubsystemCornerHoldLocal
{
	TAutoConsoleVariable<int32> CVarFormationTwoCornerHold(TEXT("Codex.Formation.TwoCornerHold"), 1,
		TEXT("#57 (user request 2026-11-07): 1 = the squad peeking round one end of a wall, a peek at the OTHER end sends that end's man there while the first corner man keeps his corner; the men between them sweep their look between both corners; 0 = before #57 (the whole stack moves to the new end, the first corner man leaves his)"));

	/** The new peek's end lies on the corner man's wall: within this of his wall's face plane, cm. */
	constexpr float SameWallPlaneCm = 60.f;
	/** The held corner man is still at his corner: his cover slot within this of the end he peeks round, cm. */
	constexpr float AtCornerCm = 250.f;
}

bool USquadSubsystem::IsTwoCornerHoldEnabled()
{
	return SquadSubsystemCornerHoldLocal::CVarFormationTwoCornerHold.GetValueOnGameThread() != 0;
}

FormationCornerHold::EPeekOrder USquadSubsystem::ClassifyCornerHoldPeek(const FVector& EdgeOnFace, AOperativeCharacter** OutHolder) const
{
	using namespace SquadSubsystemCornerHoldLocal;
	if (OutHolder)
	{
		*OutHolder = nullptr;
	}
	const FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	AOperativeCharacter* Peeker = Peek.Peeker.Get();
	AOperativeCharacter* Holder = Sectors.HeldCorner.Holder.Get();
	FormationCornerHold::FPeekOrderFacts Facts;
	Facts.bEnabled = IsTwoCornerHoldEnabled() && IsWallLeadershipEnabled();
	Facts.bPeekerAtCorner = Peeker && Peek.bLeadership && !Peek.bPending && Peeker->bInCover && Peeker->IsManualCornerPeek();
	if (Peeker && Peeker->bInCover)
	{
		const FCoverSlot& Slot = Peeker->GetCoverSlot();
		Facts.bSameWall = FMath::Abs(FVector::DotProduct(FVector(EdgeOnFace - Slot.WallPoint) * FVector(1.f, 1.f, 0.f), Slot.WallNormal.GetSafeNormal2D())) <= SameWallPlaneCm;
	}
	Facts.ToPeekEdgeCm = FVector::Dist2D(EdgeOnFace, Peek.EdgeOnFace);
	Facts.bHeldCorner = Holder != nullptr;
	Facts.ToHeldEdgeCm = Holder ? FVector::Dist2D(EdgeOnFace, Sectors.HeldCorner.EdgeOnFace) : 0.0;
	const FormationCornerHold::EPeekOrder Kind = FormationCornerHold::DecidePeekOrder(Facts);
	if (OutHolder && Kind == FormationCornerHold::EPeekOrder::AlreadyHeld)
	{
		*OutHolder = Holder && Facts.ToHeldEdgeCm < Facts.ToPeekEdgeCm ? Holder : Peeker;
	}
	return Kind;
}

bool USquadSubsystem::OrderSecondCornerPeek(AOperativeCharacter* Peeker, const FCoverSlot& CornerSlot, const FVector& EdgeOnFace)
{
	AOperativeCharacter* OldLeader = Leader.Get();
	AOperativeCharacter* Holder = Sectors.Peek.Peeker.Get();
	if (!OldLeader || !Peeker || !Holder || Peeker == Holder || !CornerSlot.IsValid() || !CanTakeWallLead(Peeker))
	{
		return false;
	}
	const EFormationRole* PeekerRole = Sectors.WallOrderRoles.Find(Peeker);
	if (Peeker != OldLeader && !PeekerRole)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: %s has no place in the wall order - an ordinary corner peek"), *Peeker->DisplayName.ToString());
		return false;
	}
	const EFormationRole GivenRole = PeekerRole ? *PeekerRole : EFormationRole::Commander;
	const FVector HeldEdge = Sectors.Peek.EdgeOnFace;
	TMap<TWeakObjectPtr<AOperativeCharacter>, EFormationRole> Roles = Sectors.WallOrderRoles;
	// The first corner's man keeps it (the peek ends, its corner is held).
	EndFormationCornerPeek(TEXT("the second corner"), /*bKeepHeldCorner*/ true);
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;
	// Decision 4 (#57): today's wall-leadership rule - the man taking a corner takes the lead.
	if (Peeker != OldLeader)
	{
		TGuardValue<bool> Switching(bWallLeadershipSwitch, true);
		if (!SetLeader(Peeker, true) || Leader.Get() != Peeker)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: %s cannot take the lead"), *Peeker->DisplayName.ToString());
			return false;
		}
		++WallLeadershipSwitchCount;
	}
	// The wall order re-keyed, nobody moves: the new leader K, the old leader the new leader's old place (he is the held corner man, out of
	// the formation's drive while he holds it), every other man his place.
	Roles.Remove(Peeker);
	if (OldLeader != Peeker)
	{
		Roles.Add(OldLeader, GivenRole);
	}
	Sectors.WallOrderRoles = Roles;
	Sectors.bWallOrder = true;
	Sectors.HeldCorner = FFormationSectorState::FHeldCorner();
	Sectors.HeldCorner.Holder = Holder;
	Sectors.HeldCorner.EdgeOnFace = HeldEdge;
	Sectors.HeldCorner.Leader = Peeker;
	Sectors.CornerSweepSeconds = 0.f;
	const EOperativeOrderResult Result = Peeker->OrderTakeCover(CornerSlot, false);
	if (Result == EOperativeOrderResult::Accepted)
	{
		Peeker->MarkCornerPeekArrival();
	}
	Sectors.WallOrderLeader = Peeker;
	Sectors.WallOrderSerial = Peeker->GetMoveOrderSerial();
	Sectors.LastLeaderSerial = Sectors.WallOrderSerial;
	Sectors.bLastLeaderSerialValid = true;
	Sectors.bLeaderOrderPending = false;
	Sectors.HeldCorner.LeaderSerial = Sectors.WallOrderSerial;
	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	Peek = FFormationSectorState::FSquadPeek();
	Peek.Peeker = Peeker;
	Peek.Leader = Peeker;
	Peek.EdgeOnFace = EdgeOnFace;
	Peek.Side = CoverTraceRules::AlongWallDistance(CornerSlot, EdgeOnFace) >= 0.f ? ECoverFacing::Right : ECoverFacing::Left;
	Peek.LeaderSerial = Sectors.WallOrderSerial;
	Peek.bPending = true;
	Peek.bAwaitStack = false;
	Peek.StackEnd = Sectors.CornerStackEnd;
	Peek.bLeadership = true;
	Peek.Line.Add(Peeker); // no line behind him: the others keep their places
	Peek.LineAble.Add(true);
	++Sectors.SecondCornerCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the corner peek at (%.0f, %.0f), the other end of the wall - %s takes it%s (%d); %s keeps his corner at (%.0f, %.0f), the others stay"),
		EdgeOnFace.X, EdgeOnFace.Y, *Peeker->DisplayName.ToString(), Peeker != OldLeader ? *FString::Printf(TEXT(" and the lead from %s"), *OldLeader->DisplayName.ToString()) : TEXT(""),
		static_cast<int32>(Result), *Holder->DisplayName.ToString(), HeldEdge.X, HeldEdge.Y);
	return true;
}

void USquadSubsystem::UpdateFormationCornerHold(float DeltaTime)
{
	using namespace SquadSubsystemCornerHoldLocal;
	FFormationSectorState::FHeldCorner& Held = Sectors.HeldCorner;
	if (!Held.Holder.IsValid())
	{
		if (Held.Leader.IsValid())
		{
			EndCornerHold(TEXT("the corner man is gone"));
		}
		return;
	}
	AOperativeCharacter* Holder = Held.Holder.Get();
	Held.Seconds += DeltaTime;
	const bool bAlive = Holder->HealthComponent && Holder->HealthComponent->IsAlive();
	const bool bAtCorner = Holder->bInCover && FVector::Dist2D(Holder->GetCoverSlot().WallPoint, Held.EdgeOnFace) <= AtCornerCm;
	AOperativeCharacter* LeaderRef = Leader.Get();
	const TCHAR* Over = nullptr;
	if (!FormationCornerHold::StillHolds(bAlive, Holder->IsKnockedDown(), bAtCorner, Holder->IsManualCornerPeek()))
	{
		Over = TEXT("the corner man is down / left his corner (it stays free until a new order)");
	}
	else if (!LeaderRef || LeaderRef != Held.Leader.Get())
	{
		Over = TEXT("the leader changed");
	}
	else if (bIsSoloMode || bFollowersHolding || HasMultiSelection() || !FormationRuntime::IsEnabled())
	{
		Over = TEXT("the squad left the formation");
	}
	else if (!Sectors.bWallOrder)
	{
		Over = TEXT("the wall order is over");
	}
	else if (Sectors.Peek.Peeker.IsValid())
	{
		Held.LeaderSerial = LeaderRef->GetMoveOrderSerial(); // the squad peek tracks the leader's next order itself
	}
	else if (LeaderRef->GetMoveOrderSerial() != Held.LeaderSerial)
	{
		Over = TEXT("the leader's next order");
	}
	if (Over)
	{
		EndCornerHold(Over);
	}
}

void USquadSubsystem::EndCornerHold(const TCHAR* Why)
{
	FFormationSectorState::FHeldCorner& Held = Sectors.HeldCorner;
	if (Held.Holder.IsValid() || Held.Leader.IsValid())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold over after %.1f s (%s at (%.0f, %.0f)): %s"), Held.Seconds,
			Held.Holder.IsValid() ? *Held.Holder->DisplayName.ToString() : TEXT("-"), Held.EdgeOnFace.X, Held.EdgeOnFace.Y, Why);
	}
	Held = FFormationSectorState::FHeldCorner();
	Sectors.CornerSweepSeconds = 0.f;
	Sectors.CornerSweepMembers = 0;
}

void USquadSubsystem::ApplyCornerHoldSweep(float DeltaTime)
{
	Sectors.CornerSweepMembers = 0;
	AOperativeCharacter* Holder = Sectors.HeldCorner.Holder.Get();
	AOperativeCharacter* Peeker = Sectors.Peek.Peeker.Get();
	FVector EyeA;
	FVector DirA;
	FVector EyeB;
	FVector DirB;
	// Both corner men leaning out round their corners: the sweep runs between their two looks.
	if (!Holder || !Peeker || Sectors.Peek.bPending || !Sectors.bActiveThisFrame || !Holder->GetPeekSight(EyeA, DirA) || !Peeker->GetPeekSight(EyeB, DirB))
	{
		Sectors.CornerSweepSeconds = 0.f;
		return;
	}
	Sectors.CornerSweepSeconds += DeltaTime;
	const FVector2D A(Holder->GetActorLocation());
	const FVector2D AB = (FVector2D(Peeker->GetActorLocation()) - A).GetSafeNormal();
	TArray<TPair<double, AOperativeCharacter*>> Middle;
	for (const FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		// A real threat (his combat target) breaks his sweep into the engage; an order of his own, too. Both give it back after.
		if (!Operative || Operative == Holder || Operative == Peeker || !Operative->HealthComponent || !Operative->HealthComponent->IsAlive() || Operative->IsKnockedDown()
			|| Operative->GetCurrentCombatTarget() || IsOnOwnOrder(Operative))
		{
			continue;
		}
		Middle.Add(TPair<double, AOperativeCharacter*>(FVector2D::DotProduct(FVector2D(Operative->GetActorLocation()) - A, AB), Operative));
	}
	Middle.Sort([](const TPair<double, AOperativeCharacter*>& X, const TPair<double, AOperativeCharacter*>& Y) { return X.Key < Y.Key; });
	const float SweepSeconds = SquadWallTuning::Get().CornerSweepSeconds;
	const FVector EdgeA = Sectors.HeldCorner.EdgeOnFace;
	const FVector EdgeB = Sectors.Peek.EdgeOnFace;
	for (int32 Index = 0; Index < Middle.Num(); ++Index)
	{
		// His sweep ends: his look towards each corner along the wall (the corner men's own peek looks round the ends, behind the wall from
		// where he stands - never into the wall), the arc through the outward side.
		const FVector Here = Middle[Index].Value->GetActorLocation();
		const float YawA = FormationCornerHold::ClampOffOutward(static_cast<float>((EdgeA - Here).Rotation().Yaw), Sectors.WallNormalYaw);
		const float YawB = FormationCornerHold::ClampOffOutward(static_cast<float>((EdgeB - Here).Rotation().Yaw), Sectors.WallNormalYaw);
		const float Fraction = FormationCornerHold::SweepFraction(Sectors.CornerSweepSeconds, SweepSeconds, Index, Middle.Num());
		FrameLooks.Add(Middle[Index].Value, FormationCornerHold::SweepYaw(YawA, YawB, Sectors.WallNormalYaw, Fraction));
	}
	Sectors.CornerSweepMembers = Middle.Num();
}
