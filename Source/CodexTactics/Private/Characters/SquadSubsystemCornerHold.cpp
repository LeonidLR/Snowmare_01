// USquadSubsystem: the two-corner hold at a wall (#57, user request 2026-11-07: "the squad pressed against a wall, a peek on the right sends
// the right man to that corner; then a peek on the left sends the left man there - but the right man leaves his position. Each of them must
// hold his own edge. The one left in the middle should scan from the left corner to the right"). Formation mode, wall leadership: a peek at
// the OTHER end of the same wall while the first corner man peeks = the second corner - its end man takes it (and, as any wall-leadership
// peek, the lead), the first corner man keeps his corner (IsFormationPeeker: the formation leaves him there), nobody else moves; the men
// between them sweep their look between the two corner looks (ApplyCornerHoldSweep). Behind Codex.Formation.TwoCornerHold.
// The game designer's answers 2026-11-08: (1) with both corners held the middle men walk back to the middle of the wall, spread evenly
// between the corner men (SpreadCornerHoldMiddle); (2) the second corner man stays the formation leader; (5) he goes down -> the man who led
// before him leads again and keeps his corner, only the fallen man's corner is freed (HandCornerHoldLeadOnFall). The sweep timing (3) and
// the slices (4) are unchanged.
// Pure rules: FormationCornerHold (tests CodexTactics.Formation.CornerHold.*).
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Both corners held").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Data/SquadWallTuning.h"
#include "Formation/FormationCornerHoldRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallLeadershipRules.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverTraceRules.h"

namespace SquadSubsystemCornerHoldLocal
{
	TAutoConsoleVariable<int32> CVarFormationTwoCornerHold(TEXT("Codex.Formation.TwoCornerHold"), 1,
		TEXT("#57 (user request 2026-11-07): 1 = the squad peeking round one end of a wall, a peek at the OTHER end sends that end's man there while the first corner man keeps his corner; the men between them walk to the middle of the wall (2026-11-08) and sweep their look between both corners; 0 = before #57 (the whole stack moves to the new end, the first corner man leaves his)"));

	/** The new peek's end lies on the corner man's wall: within this of his wall's face plane, cm. */
	constexpr float SameWallPlaneCm = 60.f;
	/** The held corner man is still at his corner: his cover slot within this of the end he peeks round, cm. */
	constexpr float AtCornerCm = 250.f;

	bool IsDown(const AOperativeCharacter* Operative)
	{
		return !Operative || !Operative->HealthComponent || !Operative->HealthComponent->IsAlive() || Operative->IsKnockedDown();
	}

	/** The corner man still holds the corner at Edge (FormationCornerHold::StillHolds: alive, up, in cover at that end, peeking). */
	bool HoldsCorner(const AOperativeCharacter& Holder, const FVector& Edge)
	{
		const bool bAlive = Holder.HealthComponent && Holder.HealthComponent->IsAlive();
		const bool bAtCorner = Holder.bInCover && FVector::Dist2D(Holder.GetCoverSlot().WallPoint, Edge) <= AtCornerCm;
		return FormationCornerHold::StillHolds(bAlive, Holder.IsKnockedDown(), bAtCorner, Holder.IsManualCornerPeek());
	}

	/** Where a corner man stands for the middle men's spread: his cover slot's stand point (in cover), else where he is. */
	FVector2D StandPoint(const AOperativeCharacter& Operative)
	{
		return FVector2D(Operative.bInCover ? Operative.GetCoverSlot().WorldLocation : Operative.GetActorLocation());
	}

	/** Point projected onto the wall line through LinePoint (NormalYaw = its outward normal). */
	FVector2D OnWallLine(const FVector2D& Point, const FVector2D& LinePoint, float NormalYaw)
	{
		const double Rad = FMath::DegreesToRadians(static_cast<double>(NormalYaw));
		const FVector2D Along(-FMath::Sin(Rad), FMath::Cos(Rad));
		return LinePoint + Along * FVector2D::DotProduct(Point - LinePoint, Along);
	}
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
	const ECoverFacing HeldSide = Sectors.Peek.Side;
	TMap<TWeakObjectPtr<AOperativeCharacter>, EFormationRole> Roles = Sectors.WallOrderRoles;
	// Decision 4 (#57) / the game designer's answer 2 (2026-11-08): today's wall-leadership rule - the man taking a corner takes the lead and
	// keeps it; the man who led until then is remembered (answer 5: he leads again if the new leader falls).
	const FormationCornerHold::FLeaderChain Chain = FormationCornerHold::AfterSecondCorner(Members.IndexOfByKey(OldLeader), Members.IndexOfByKey(Peeker));
	AOperativeCharacter* NewLeader = Members.IsValidIndex(Chain.Leader) ? Members[Chain.Leader].Get() : nullptr;
	AOperativeCharacter* PreviousLeader = Members.IsValidIndex(Chain.Previous) ? Members[Chain.Previous].Get() : nullptr;
	if (NewLeader != Peeker)
	{
		return false; // not a squad member: no second corner
	}
	// The first corner's man keeps it (the peek ends, its corner is held).
	EndFormationCornerPeek(TEXT("the second corner"), /*bKeepHeldCorner*/ true);
	Sectors.bSlide = false;
	Sectors.bSlideStack = false;
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
	Sectors.HeldCorner.Side = HeldSide;
	Sectors.HeldCorner.Leader = Peeker;
	Sectors.HeldCorner.PreviousLeader = PreviousLeader;
	Sectors.CornerSweepSeconds = 0.f;
	Sectors.CornerSweepPhase = 0.f;
	const EOperativeOrderResult Result = Peeker->OrderTakeCover(CornerSlot, false);
	if (Result == EOperativeOrderResult::Accepted)
	{
		Peeker->MarkCornerPeekArrival();
		// Answer 1 (2026-11-08): the squad bunched at the first corner - the middle men walk back to the middle of the wall between the two.
		SpreadCornerHoldMiddle(SquadSubsystemCornerHoldLocal::StandPoint(*Holder), FVector2D(CornerSlot.WorldLocation), Holder, Peeker);
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
	AOperativeCharacter* LeaderRef = Leader.Get();
	// Answer 5 (2026-11-08): the hold's leader (the second corner man) knocked down - the man who led before him leads again, his corner stays
	// (a dead leader comes through UnregisterOperative).
	if (LeaderRef && Held.Leader.Get() == LeaderRef && !Held.bLeaderFallSeen && IsDown(LeaderRef) && HandCornerHoldLeadOnFall(*LeaderRef))
	{
		return;
	}
	const TCHAR* Over = nullptr;
	if (!HoldsCorner(*Holder, Held.EdgeOnFace))
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
	Sectors.CornerSweepPhase = 0.f;
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
		Sectors.CornerSweepPhase = 0.f;
		return;
	}
	Sectors.CornerSweepSeconds += DeltaTime;
	// Answer 3 (2026-11-08, "6 s, option V"): slow out of combat, 6 s one way in combat / alert - the squad's own alert facts.
	FormationCornerHold::FSweepAlertFacts Alert;
	const FFormationConfig& Config = FormationTuning::Get();
	for (const FFormationThreat& Threat : GatherFormationThreats())
	{
		Alert.bThreatKnown |= Threat.AgeS >= 0.f && Threat.AgeS <= Config.BypassThreatMemoryS;
	}
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		AOperativeCharacter* Operative = Member.Get();
		AOperativeCharacter* Shooter = nullptr;
		FVector Flash;
		Alert.bUnderFire |= Operative && Operative->GetUnderFireThreat(Shooter, Flash);
		Alert.bSuppressed |= Operative && USuppressionSubsystem::GetTierOf(Operative) != ESuppressionTier::None;
	}
	const FSquadWallTuning& WallTuning = SquadWallTuning::Get();
	const float SweepSeconds = FormationCornerHold::SweepSecondsFor(Alert, WallTuning.CornerSweepCalmSeconds, WallTuning.CornerSweepSeconds);
	const bool bAlert = FormationCornerHold::IsSweepAlert(Alert);
	if (bAlert != Sectors.bCornerSweepAlert || !FMath::IsNearlyEqual(SweepSeconds, Sectors.CornerSweepNowSeconds))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the middle men's sweep %.0f s one way (%s: threat %d, under fire %d, suppressed %d)"), SweepSeconds,
			bAlert ? TEXT("combat / alert") : TEXT("calm"), Alert.bThreatKnown ? 1 : 0, Alert.bUnderFire ? 1 : 0, Alert.bSuppressed ? 1 : 0);
	}
	Sectors.bCornerSweepAlert = bAlert;
	Sectors.CornerSweepNowSeconds = SweepSeconds;
	Sectors.CornerSweepPhase += SweepSeconds > KINDA_SMALL_NUMBER ? DeltaTime / SweepSeconds : 0.f;
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
	const FVector EdgeA = Sectors.HeldCorner.EdgeOnFace;
	const FVector EdgeB = Sectors.Peek.EdgeOnFace;
	for (int32 Index = 0; Index < Middle.Num(); ++Index)
	{
		// His sweep ends: his look towards each corner along the wall (the corner men's own peek looks round the ends, behind the wall from
		// where he stands - never into the wall), the arc through the outward side.
		const FVector Here = Middle[Index].Value->GetActorLocation();
		const float YawA = FormationCornerHold::ClampOffOutward(static_cast<float>((EdgeA - Here).Rotation().Yaw), Sectors.WallNormalYaw);
		const float YawB = FormationCornerHold::ClampOffOutward(static_cast<float>((EdgeB - Here).Rotation().Yaw), Sectors.WallNormalYaw);
		const float Fraction = FormationCornerHold::SweepFraction(Sectors.CornerSweepPhase, SweepSeconds > KINDA_SMALL_NUMBER ? 1.f : 0.f, Index, Middle.Num());
		FrameLooks.Add(Middle[Index].Value, FormationCornerHold::SweepYaw(YawA, YawB, Sectors.WallNormalYaw, Fraction));
	}
	Sectors.CornerSweepMembers = Middle.Num();
}

bool USquadSubsystem::SpreadCornerHoldMiddle(const FVector2D& CornerA, const FVector2D& CornerB, const AOperativeCharacter* HolderA, const AOperativeCharacter* HolderB)
{
	// The middle men: the wall order's members between the corner men who can walk (a knocked-down one keeps his place).
	TArray<FFollowerState*> Who;
	TArray<FVector2D> From;
	TArray<EFormationRole> Roles;
	for (FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		const EFormationRole* Role = Operative ? Sectors.WallOrderRoles.Find(Follower.Operative) : nullptr;
		if (!Operative || Operative == HolderA || Operative == HolderB || !Role || !CanTakeWallLead(Operative)
			|| !Sectors.WallSlots.ContainsByPredicate([Role](const FFormationSlot& Each) { return Each.Role == *Role; }))
		{
			continue;
		}
		Who.Add(&Follower);
		From.Add(FVector2D(Operative->GetActorLocation()));
		Roles.Add(*Role);
	}
	if (Who.IsEmpty())
	{
		return false;
	}
	// Their places stay on the wall layout's face-offset line (the line their stack slots stand on), the layout's spacing the minimum.
	const EFormationRole FirstRole = Roles[0];
	const FFormationSlot* LineSlot = Sectors.WallSlots.FindByPredicate([FirstRole](const FFormationSlot& Each) { return Each.Role == FirstRole; });
	const float MinSpacing = FormationTuning::Get().WallSpacing;
	const TArray<FVector2D> Places = FormationCornerHold::MiddlePlaces(CornerA, CornerB, LineSlot->Position, Sectors.WallNormalYaw, Who.Num(), MinSpacing);
	if (Places.Num() != Who.Num())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the wall between the corners (%.0f cm) is too short for %d middle men %.0f cm apart - they keep their places"),
			FVector2D::Distance(CornerA, CornerB), Who.Num(), MinSpacing);
		return false;
	}
	const TArray<int32> Order = FormationCornerHold::OrderAlong(From, CornerA, CornerB);
	const IConsoleVariable* AlongWallVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.ReformAlongWall"));
	const float AlongOffCm = !AlongWallVar || AlongWallVar->GetInt() != 0 ? SquadWallTuning::Get().ReformAlongWallOffCm : 0.f;
	FString Line;
	for (int32 Place = 0; Place < Order.Num(); ++Place)
	{
		const int32 Index = Order[Place];
		const EFormationRole Role = Roles[Index];
		FFormationSlot* Slot = Sectors.WallSlots.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; });
		FFollowerState& Follower = *Who[Index];
		if (!Slot)
		{
			continue;
		}
		const FVector2D Old = Slot->Position;
		Slot->Position = Places[Place];
		Follower.RepathTimeRemaining = 0.f;
		Follower.bParked = false;
		Follower.bReformTurned = false;
		// A walk along the same wall: he keeps his crouch and his look (the game designer's bug 2026-11-03), as into the stack.
		Follower.bReformAlongWall = FormationWallLeadership::ReformAlongWall(From[Index], Places[Place], Sectors.WallNormalYaw, AlongOffCm);
		Line += FString::Printf(TEXT(" %s (%s) (%.0f, %.0f) -> (%.0f, %.0f);"), *Follower.Operative->DisplayName.ToString(), FormationNames::ToString(Role), Old.X, Old.Y,
			Places[Place].X, Places[Place].Y);
	}
	++Sectors.CornerMiddleSpreadCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the middle men back to the middle of the wall between (%.0f, %.0f) and (%.0f, %.0f):%s"), CornerA.X, CornerA.Y,
		CornerB.X, CornerB.Y, *Line);
	return true;
}

bool USquadSubsystem::HandCornerHoldLeadOnFall(AOperativeCharacter& Fallen)
{
	using namespace SquadSubsystemCornerHoldLocal;
	FFormationSectorState::FHeldCorner& Held = Sectors.HeldCorner;
	AOperativeCharacter* Holder = Held.Holder.Get();
	AOperativeCharacter* Previous = Held.PreviousLeader.Get();
	FormationCornerHold::FLeaderFallFacts Facts;
	Facts.bEnabled = IsTwoCornerHoldEnabled();
	Facts.bFallenLeadsTheHold = Holder && Leader.Get() == &Fallen && Held.Leader.Get() == &Fallen && IsDown(&Fallen);
	Facts.bPreviousKnown = Previous && Members.Contains(Previous);
	Facts.bPreviousIsFallen = Previous == &Fallen;
	Facts.bPreviousAble = CanTakeWallLead(Previous);
	Facts.bPreviousHoldsOtherCorner = Previous && Previous == Holder && HoldsCorner(*Holder, Held.EdgeOnFace);
	const FormationCornerHold::FLeaderAfterFall After = FormationCornerHold::DecideLeaderAfterFall(Facts);
	if (After.Pick == FormationCornerHold::ELeaderAfterFall::NotTheHold)
	{
		return false;
	}
	if (After.Pick == FormationCornerHold::ELeaderAfterFall::Fallback)
	{
		Held.bLeaderFallSeen = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the leader %s is down, the leader before him (%s) cannot lead - the existing leader-pick rule"),
			*Fallen.DisplayName.ToString(), Previous ? *Previous->DisplayName.ToString() : TEXT("-"));
		return false;
	}
	if (!After.bKeepOtherCorner)
	{
		// He leads, but holds no corner (out of it): an ordinary leader change - the wall order goes on round him, the hold ends.
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the leader %s is down - %s (the leader before him) leads again"), *Fallen.DisplayName.ToString(),
			*Previous->DisplayName.ToString());
		if (!SetLeader(Previous, true) || Leader.Get() != Previous)
		{
			return false;
		}
		++Sectors.CornerLeadHandBackCount;
		return true;
	}
	// The fallen man's corner is free (his squad peek is over); the other corner man keeps his and leads from it - the squad's one corner peek,
	// as after a single wall-leadership peek (a later peek at the free end is a second corner again). The middle men keep their places.
	const FVector Edge = Held.EdgeOnFace;
	const ECoverFacing Side = Held.Side;
	const float HeldSeconds = Held.Seconds;
	const EFormationRole* HolderRole = Sectors.WallOrderRoles.Find(Holder);
	const TOptional<EFormationRole> GivenRole = HolderRole ? TOptional<EFormationRole>(*HolderRole) : TOptional<EFormationRole>();
	Sectors.Peek = FFormationSectorState::FSquadPeek();
	Held = FFormationSectorState::FHeldCorner();
	Sectors.CornerSweepSeconds = 0.f;
	Sectors.CornerSweepPhase = 0.f;
	Sectors.CornerSweepMembers = 0;
	{
		TGuardValue<bool> Switching(bWallLeadershipSwitch, true);
		if (!SetLeader(Holder, true) || Leader.Get() != Holder)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: %s cannot take the lead back"), *Holder->DisplayName.ToString());
			return false;
		}
	}
	// The wall order re-keyed: he is K again (K's place is his corner in the stack); the fallen man (still in the squad: knocked down) takes his
	// place in the order - where he lies, at the wall (he gets up there; nobody takes the free corner without a new order).
	Sectors.WallOrderRoles.Remove(Holder);
	if (GivenRole.IsSet() && Members.Contains(&Fallen))
	{
		const EFormationRole Role = GivenRole.GetValue();
		Sectors.WallOrderRoles.Add(&Fallen, Role);
		const FFormationSlot* KSlot = Sectors.WallSlots.FindByPredicate([](const FFormationSlot& Each) { return Each.Role == EFormationRole::Commander; });
		FFormationSlot* Slot = Sectors.WallSlots.FindByPredicate([Role](const FFormationSlot& Each) { return Each.Role == Role; });
		if (Slot && KSlot)
		{
			Slot->Position = OnWallLine(FVector2D(Fallen.GetActorLocation()), KSlot->Position, Sectors.WallNormalYaw);
		}
	}
	Sectors.bWallOrder = true;
	Sectors.WallOrderLeader = Holder;
	Sectors.WallOrderSerial = Holder->GetMoveOrderSerial();
	Sectors.LastLeaderSerial = Sectors.WallOrderSerial;
	Sectors.bLastLeaderSerialValid = true;
	Sectors.bLeaderOrderPending = false;
	FFormationSectorState::FSquadPeek& Peek = Sectors.Peek;
	Peek.Peeker = Holder;
	Peek.Leader = Holder;
	Peek.EdgeOnFace = Edge;
	Peek.Side = Side;
	Peek.LeaderSerial = Sectors.WallOrderSerial;
	Peek.bPending = false; // he is peeking already
	Peek.bAwaitStack = false;
	Peek.StackEnd = Sectors.CornerStackEnd;
	Peek.bLeadership = true;
	Peek.Line.Add(Holder);
	Peek.LineAble.Add(true);
	++Sectors.CornerLeadHandBackCount;
	UE_LOG(LogCodexTactics, Display, TEXT("[Formation] two-corner hold: the leader %s is %s after %.1f s - %s (the leader before him) leads again and keeps his corner at (%.0f, %.0f); %s's corner is free, the middle men keep their places"),
		*Fallen.DisplayName.ToString(), Fallen.HealthComponent && Fallen.HealthComponent->IsAlive() ? TEXT("knocked down") : TEXT("dead"), HeldSeconds,
		*Holder->DisplayName.ToString(), Edge.X, Edge.Y, *Fallen.DisplayName.ToString());
	return true;
}
