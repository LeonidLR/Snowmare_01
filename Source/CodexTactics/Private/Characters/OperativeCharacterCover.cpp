// AOperativeCharacter: Tactical cover: orders, enter / leave, shimmy, cover fire mode, facing along the wall, cover
// events.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// UE-only (Sprint 12 tactical cover, TANDEM «SPRINT 12 DIRECTIVE»; no Godot reference). Pure rules: Tactics/CoverRules,
// CoverTraceRules, CoverDecisionRules, CoverFacingRules.

#include "Characters/OperativeCharacter.h"
#include "TimerManager.h"

#include "CodexTactics.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugCornerAim(TEXT("Codex.Debug.CornerAim"), 0,
		TEXT("1: the sustained corner aim's decision, its reason and inputs over the operatives in cover."));
}

EOperativeOrderResult AOperativeCharacter::OrderTakeCover(const FCoverSlot& Slot, bool bSprint)
{
	if (!Slot.IsValid())
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsKnockedDown())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		KnockdownComponent->BufferOrder([WeakThis, Slot, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				Self->OrderTakeCover(Slot, bSprint);
			}
		});
		return EOperativeOrderResult::Accepted;
	}
	if (bInCover && CoverTraceRules::IsSameWall(CoverSlot, Slot))
	{
		return OrderShimmyTo(Slot);
	}
	// Rev 5i: a tutorial confinement refuses cover outside its area before he leaves the cover he holds.
	if (!UTutorialSubsystem::CheckConfinedOrder(this, this, Slot.WorldLocation))
	{
		return EOperativeOrderResult::Refused;
	}
	if (bInCover)
	{
		LeaveCover(TEXT("another cover"));
	}
	PendingCoverSlot = Slot;
	bHasPendingCover = true;
	TGuardValue<bool> CoverOrder(bCoverMoveOrder, true);
	const EOperativeOrderResult Result = OrderMoveTo(Slot.WorldLocation, bSprint);
	if (Result != EOperativeOrderResult::Accepted)
	{
		bHasPendingCover = false;
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: to the %s cover at (%.0f, %.0f)%s"), *DisplayName.ToString(),
			Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"), Slot.WorldLocation.X, Slot.WorldLocation.Y,
			bSprint ? TEXT(" at a sprint") : TEXT(""));
	}
	return Result;
}

void AOperativeCharacter::SetPendingCover(const FCoverSlot& Slot)
{
	PendingCoverSlot = Slot;
	bHasPendingCover = Slot.IsValid();
}

void AOperativeCharacter::EnterCover(const FCoverSlot& Slot)
{
	if (!Slot.IsValid() || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	const bool bWasInCover = bInCover;
	const bool bSameWall = bWasInCover && CoverTraceRules::IsSameWall(CoverSlot, Slot);
	const bool bWasAutoSnap = bCoverAutoSnap; // the automatic walk to the threat-side corner is no player shimmy
	StopOperative();
	CoverSlot = Slot;
	bInCover = true;
	CurrentCoverHeight = Slot.Height;
	bShimmying = false;
	ShimmyDirection = 0.f;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	LeanTimer = 0.f;
	BlindFireTimer = 0.f;
	bHasPendingCover = false;
	bOpenShotReturnPending = false; // a cover entry ends any open shot
	bCornerAimActive = false;
	bQuietCoverEntry = bQuietCoverEntryRequest; // the return after an open shot: no Cover_Enter clip
	bCoverEntryFromRun = bCoverEntryFromRunRequest;
	bCoverLeftToMove = false;
	// A fresh wall: walk to the threat-side edge on the next update if it is close (a player's shimmy is never undone).
	bCoverSnapPending = !bSameWall;
	bCoverAutoSnap = false;
	if (!bSameWall)
	{
		bHasCoverThreat = false;
		CoverThreatSeenTime = -1.0e9;
		CoverShotTarget.Reset();
		CoverFacing = PredictCoverFacing(Slot);
		if (!bQuietCoverEntry)
		{
			bManualCornerPeek = false; // another wall: a manual peek belongs to the old one
			bManualCornerPeekEventPending = false;
		}
	}
	// Back to the wall, on the slot (the feet keep their ground height), facing along the wall towards the threat side.
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	SetActorLocation(FVector(Slot.WorldLocation.X, Slot.WorldLocation.Y, Feet.Z + GetSimpleCollisionHalfHeight()), true);
	CoverThreatTimer = 0.f;
	UpdateCoverFacing(/*bAllowSnap*/ false);
	if (!bSameWall)
	{
		// The actor turns its back to the wall at once (user report 2026-10-09: pops on entry). With an enter clip the clip's
		// first frame faces the wall - the pose he arrives in (the approach turns him to the wall, UpdateCombatFacing) - and the
		// anim blends it in fast when the turn is large (GetCoverEntryTurnDeg). Without one the body's old yaw is drawn back on
		// the mesh and eased out (UpdateCoverEntryBlend).
		const float OldYaw = GetActorRotation().Yaw;
		SetActorRotation(FRotator(0.f, GetCoverFacingYaw(), 0.f));
		CoverEntryTurnDeg = FMath::Abs(FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw));
		const UOperativeAnimInstance* EntryAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		const auto HasClip = [](const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) { return Clips.ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; }); };
		const bool bEnterClip = EntryAnim && EntryAnim->bUseNativeCoverClips && (HasClip(EntryAnim->CoverStandEnter) || HasClip(EntryAnim->CoverCrouchEnter));
		CoverEntryYawOffset = FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw);
		CoverEntryYawTime = bQuietCoverEntry || bEnterClip ? -1.f : 0.f;
		bBodyYawFree = false;
		// With an enter clip and a big turn the clip comes in at once (CoverEnterTurnedBlendSeconds), one anim update later:
		// until it plays, the old pose keeps its old yaw on the mesh (no frame of the run pose turned round).
		bCoverEntryAwaitClip = bEnterClip && !bQuietCoverEntry && CoverEntryTurnDeg > 90.f;
		bCoverEntryClipSeen = false;
		CoverEntryAwaitSeconds = 0.f;
	}
	ClearIdleFacing();
	if (!bSameWall)
	{
		// High cover: standing (the wall covers him); low cover: crouched. Prone stays prone (hidden by the 60 cm rule).
		const EOperativeStance Default = CoverRules::DefaultStanceFor(Slot.Height);
		if (Stance != EOperativeStance::Prone && Stance != Default && !(ColdSurvival && ColdSurvival->IsFrostbitten()))
		{
			SetStance(Default);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: %s %s cover at (%.0f, %.0f), facing %s, corners L %d R %d"), *DisplayName.ToString(),
		bSameWall ? TEXT("shimmied along the") : TEXT("entered the"), Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"),
		Slot.WorldLocation.X, Slot.WorldLocation.Y, CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"),
		Slot.bLeftEdgeExposed ? 1 : 0, Slot.bRightEdgeExposed ? 1 : 0);
	if (!bWasInCover)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, Slot.Height == ECoverHeight::HighCover ? TEXT("🧱 AT THE WALL") : TEXT("🧱 BEHIND COVER"),
			FLinearColor(0.5f, 0.8f, 1.f));
		if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(DisplayName, FText::FromString(Slot.Height == ECoverHeight::HighCover
				? TEXT("🧱 Hugging the wall, holding the corner!") : TEXT("🧱 Behind cover!")));
		}
		ReceiveCoverChanged(true, CurrentCoverHeight);
	}
	// Demo 01 tutorial: the entry counts once he has settled at the slot (UpdateCoverEnteredEvent: a run-in enters ~1 m out
	// while the enter clip glides him in). The return after an open shot is no new entry; a player's side-step is a shimmy.
	if (!bQuietCoverEntry)
	{
		if (bSameWall)
		{
			if (!bWasAutoSnap)
			{
				RaiseCoverEvent(GameplayEvents::CoverShimmy, Slot);
			}
		}
		else
		{
			bCoverEnteredEventPending = true;
			CoverEnteredPendingSeconds = 0.f;
		}
	}
}

void AOperativeCharacter::UpdateCoverEnteredEvent(float DeltaTime)
{
	if (!bCoverEnteredEventPending || !bInCover)
	{
		return;
	}
	CoverEnteredPendingSeconds += DeltaTime;
	const bool bOnSlot = FVector::DistSquared2D(GetActorLocation(), CoverSlot.WorldLocation) <= FMath::Square(25.f);
	// Settled = the run-in glide / enter clip blend is over (CoverEntryBlendTime), no walk to the corner pending or running.
	const bool bBusy = bShimmying || bCoverSnapPending || CoverEntryBlendTime >= 0.f || !bOnSlot || GetVelocity().SizeSquared2D() > FMath::Square(20.f);
	if (bBusy && CoverEnteredPendingSeconds < 3.f)
	{
		return;
	}
	bCoverEnteredEventPending = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: settled in the cover after %.2f s (%.0f cm off the slot, shimmying %d, speed %.0f) -> CoverEntered"),
		*DisplayName.ToString(), CoverEnteredPendingSeconds, FVector::Dist2D(GetActorLocation(), CoverSlot.WorldLocation), bShimmying ? 1 : 0,
		GetVelocity().Size2D());
	const FName Kind = CoverSlot.Height == ECoverHeight::HighCover ? FName(TEXT("High")) : FName(TEXT("Low"));
	RaiseCoverEvent(GameplayEvents::CoverEntered, CoverSlot, Kind);
	RaiseCoverEvent(FName(*(GameplayEvents::CoverEntered.ToString() + TEXT(".") + Kind.ToString())), CoverSlot, Kind);
}

void AOperativeCharacter::RaiseCoverEvent(FName EventName, const FCoverSlot& Slot, FName Kind)
{
	FGameplayEventContext Context;
	Context.Subject = this;
	Context.Target = Slot.WallActor.Get();
	Context.Kind = Kind;
	Context.Location = Slot.WorldLocation;
	if (!Context.Target)
	{
		// A wall without an actor (landscape / BSP): the slot's wall point names the place.
		Context.TargetId = FName(*FString::Printf(TEXT("Wall_%d_%d"), FMath::RoundToInt(Slot.WallPoint.X / 50.f), FMath::RoundToInt(Slot.WallPoint.Y / 50.f)));
	}
	GameplayEvents::RaiseWithContext(this, EventName, Context);
}

void AOperativeCharacter::LeaveCover(const FString& Reason)
{
	if (!bInCover)
	{
		return;
	}
	if (bCoverEnteredEventPending)
	{
		bCoverEnteredEventPending = false; // left before he settled: no entry, no leave
	}
	else if (!bQuietCoverLeaveRequest)
	{
		RaiseCoverEvent(GameplayEvents::CoverLeft, CoverSlot, CurrentCoverHeight == ECoverHeight::HighCover ? FName(TEXT("High")) : FName(TEXT("Low")));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: left the cover (%s)"), *DisplayName.ToString(), *Reason);
	if (!bQuietCoverLeaveRequest)
	{
		bManualCornerPeek = false; // a move / another cover ends the manual corner peek (the open-shot step-off comes back to it)
		bManualCornerPeekEventPending = false;
	}
	bInCover = false;
	CurrentCoverHeight = ECoverHeight::None;
	bShimmying = false;
	ShimmyDirection = 0.f;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	bCoverHoldFire = false;
	LeanTimer = 0.f;
	BlindFireTimer = 0.f;
	bAtCoverCorner = false;
	bHasCoverThreat = false;
	CoverThreatSeenTime = -1.0e9;
	bCoverAutoSnap = false;
	bCoverSnapPending = false;
	bCornerAimActive = false; // the sustained corner aim ends with the cover
	bCoverEntryFromRun = false;
	CoverShotTarget.Reset();
	CoverSlot = FCoverSlot();
	bQuietCoverEntry = false;
	ReceiveCoverChanged(false, ECoverHeight::None);
}

EOperativeOrderResult AOperativeCharacter::OrderShimmyTo(const FCoverSlot& Target)
{
	if (!bInCover || !Target.IsValid() || !CoverTraceRules::IsSameWall(CoverSlot, Target))
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsRaging() || IsPanicking())
	{
		return EOperativeOrderResult::Refused;
	}
	// From where the body IS: while a shimmy runs CoverSlot is still the slot it started from (a reversing order whose
	// target is on the old side of that slot used to get the old direction: right-moving body, left clip, no turn).
	const float Along = CoverTraceRules::AlongWallDistanceFrom(CoverSlot, GetActorLocation(), Target.WorldLocation);
	if (!bCornerPeekOrdering && bManualCornerPeek)
	{
		bManualCornerPeek = false; // a shimmy is another order: the manual corner peek ends
		bManualCornerPeekEventPending = false;
	}
	if (FMath::Abs(Along) < 10.f)
	{
		return EOperativeOrderResult::Accepted; // already there
	}
	if (Stance == EOperativeStance::Prone)
	{
		// Rev 2026-10-19 (user bug: a prone operative in cover, ordered to the corner, crawled BACKWARDS at twice the crawl speed - the
		// shimmy ran at the crouch pace with the body held facing off the wall): no prone shimmy - he gets up first (low cover: crouched,
		// high cover: standing), then side-steps. Frostbitten (cannot leave prone): refused.
		const EOperativeStance Up = CurrentCoverHeight == ECoverHeight::LowCover ? EOperativeStance::Crouching : EOperativeStance::Standing;
		const float Rise = GetStanceChangeDelay(EOperativeStance::Prone, Up);
		SetStance(Up);
		if (Stance == EOperativeStance::Prone)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: shimmy refused - cannot get up from prone"), *DisplayName.ToString());
			return EOperativeOrderResult::Refused;
		}
		if (Rise > 0.05f && GetWorld())
		{
			// The side-step starts once he is up (the stance clip; StopOperative / another order clears the timer).
			const bool bForPeek = bCornerPeekOrdering;
			TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
			GetWorld()->GetTimerManager().SetTimer(PendingMoveTimer, FTimerDelegate::CreateLambda([WeakThis, Target, bForPeek]()
			{
				AOperativeCharacter* Self = WeakThis.Get();
				if (Self && Self->bInCover && !Self->IsKnockedDown() && Self->GetStance() != EOperativeStance::Prone)
				{
					TGuardValue<bool> Ordering(Self->bCornerPeekOrdering, bForPeek);
					Self->OrderShimmyTo(Target);
				}
			}), Rise, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: gets up from prone first (%.2f s), then side-steps"), *DisplayName.ToString(), Rise);
			return EOperativeOrderResult::Accepted;
		}
	}
	if (bCornerAimActive)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("shimmy ordered"));
	}
	PendingCoverSlot = Target;
	PendingCoverSlot.WallNormal = CoverSlot.WallNormal; // one wall, one facing
	bHasPendingCover = true;
	ShimmyDirection = Along > 0.f ? 1.f : -1.f;
	// No threat known: face forward in the direction of the move (and idle facing that side afterwards).
	CoverFacing = CoverFacingRules::FacingForShimmy(CoverFacing, ShimmyDirection, bHasCoverThreat);
	bShimmying = true;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	bCoverSnapPending = false;
	bCoverAutoSnap = false; // TrySnapToCoverCorner sets it after this call
	// Side-step at the crouch-walk pace along the wall, facing the threat side (UpdateCombatFacing holds the facing):
	// forward towards it, backwards away from it (CoverFacingRules::IsShimmyForward).
	TGuardValue<bool> CoverOrder(bCoverMoveOrder, true);
	bSprinting = false;
	ApplyMovementParams(OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Crouching, false, IsWounded(), bCarrying) * ColdSpeedMultiplier);
	const EOperativeOrderResult Result = RequestMove(Target.WorldLocation);
	if (Result != EOperativeOrderResult::Accepted)
	{
		bHasPendingCover = false;
		bShimmying = false;
		ShimmyDirection = 0.f;
		ApplyMovementParams();
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: shimmies %.0f cm to the %s (%s, facing %s)"), *DisplayName.ToString(), FMath::Abs(Along),
			Along > 0.f ? TEXT("right") : TEXT("left"), IsShimmyForward() ? TEXT("forward") : TEXT("backwards"),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"));
	}
	return Result;
}

void AOperativeCharacter::SetCoverFireMode(ECoverFireMode Mode)
{
	CoverFireMode = Mode == ECoverFireMode::Normal ? ECoverFireMode::CornerLean : Mode;
}

ECoverFireMode AOperativeCharacter::ToggleCoverFireMode()
{
	SetCoverFireMode(CoverFireMode == ECoverFireMode::BlindFire ? ECoverFireMode::CornerLean : ECoverFireMode::BlindFire);
	UFloatingTextSubsystem::SpawnAboveOperative(this, CoverFireMode == ECoverFireMode::BlindFire ? TEXT("🙈 BLIND FIRE (-40%)") : TEXT("👁️ CORNER FIRE"),
		FLinearColor(0.9f, 0.85f, 0.4f));
	return CoverFireMode;
}

bool AOperativeCharacter::CanFireFromCover() const
{
	if (!bInCover)
	{
		return true;
	}
	if (CurrentCoverHeight == ECoverHeight::LowCover)
	{
		return true; // over the top
	}
	return CoverSlot.HasExposedEdge();
}

FVector AOperativeCharacter::GetCoverFireOrigin() const
{
	const FVector Muzzle = GetMuzzleLocation();
	if (!bInCover || (CurrentCoverHeight != ECoverHeight::HighCover && !(bCornerAimActive && IsManualCornerPeekSpot())))
	{
		return Muzzle; // low cover: over the top (a manual low-cover corner peek fires round its end, rev 5e)
	}
	return CornerFireOriginFor(CoverSlot, CoverFacing, Muzzle);
}

FVector AOperativeCharacter::CornerFireOriginFor(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Muzzle)
{
	// Round the facing's corner (or the only exposed one).
	const ECoverFacing Side = Slot.IsEdgeExposed(Facing) ? Facing : (Slot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right);
	const float EdgeDistance = Side == ECoverFacing::Left ? Slot.LeftEdgeDistanceCm : Slot.RightEdgeDistanceCm;
	return CoverRules::CornerMuzzle(Slot, Side, Muzzle, FMath::Max(CoverRules::GetConfig().CornerPeekOffsetCm, EdgeDistance + 20.f));
}

bool AOperativeCharacter::IsHiddenInCoverFrom(const FVector& ObserverLocation) const
{
	if (!bInCover)
	{
		return false;
	}
	const bool bInArc = CoverRules::IsInFrontalArc(CoverSlot.WallNormal, CoverSlot.WorldLocation, ObserverLocation, CoverRules::GetConfig().FrontalArcDeg);
	return CoverRules::HiddenFromObserver(CurrentCoverHeight, bIsCornerLeaning, bInArc);
}

void AOperativeCharacter::BeginCoverShot()
{
	CoverLastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FCoverCombatConfig& Config = CoverRules::GetConfig();
	if (CoverFireMode == ECoverFireMode::BlindFire)
	{
		bIsBlindFiring = true;
		bIsCornerLeaning = false;
		BlindFireTimer = Config.BlindFireHoldSeconds;
	}
	else
	{
		bIsCornerLeaning = true;
		bIsBlindFiring = false;
		LeanTimer = Config.LeanHoldSeconds;
		BeginCornerAim();
	}
}

void AOperativeCharacter::UpdateCover(float DeltaTime)
{
	RecentIncomingDamage = CoverDecisionRules::DecayRecentDamage(FCoverDecisionConfig(), RecentIncomingDamage, DeltaTime);
	RecentRangedDamage = CoverDecisionRules::DecayRecentDamage(FCoverDecisionConfig(), RecentRangedDamage, DeltaTime);
	// Grid walks (turn-based) and planned walks end without HandleMoveFinished: enter the pending slot on arrival.
	if (bHasPendingCover && !bHasMoveOrder && GetVelocity().SizeSquared2D() < 4.f
		&& FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) <= 120.f)
	{
		const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
		if (!TurnBased || TurnBased->GetTacticalMoveSpeed(this) < 0.f)
		{
			bHasPendingCover = false;
			EnterCover(PendingCoverSlot);
		}
	}
	UpdateOpenShotReturn();
	if (!bInCover)
	{
		return;
	}
	UpdateCoverEnteredEvent(DeltaTime);
	UpdateCornerAim(DeltaTime);
	// Corner hold (user decision 2026-10-07): at the exposed edge he steps out into the fire stance at once - also right
	// after a reload - and holds it; only in a fight / with a known threat (2026-10-11: exploring he hugs the wall).
	if (!bCornerAimActive && !bIsReloading && WantsCornerHold() && !(bCornerAutoDuck && GetWorld() && GetWorld()->GetTimeSeconds() < CornerAimDuckUntil))
	{
		BeginCornerAim();
	}
	// Manual corner peek (rev 5c): the pose is reached - CornerPeek once per order (the tutorial / telemetry).
	if (bManualCornerPeekEventPending && bCornerAimActive && IsManualCornerPeekSpot())
	{
		bManualCornerPeekEventPending = false;
		++ManualCornerPeekCount;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: corner peek - leaning out at the %s corner, holding the aim"), *DisplayName.ToString(),
			ManualCornerPeekSide == ECoverFacing::Left ? TEXT("left") : TEXT("right"));
		RaiseCoverEvent(GameplayEvents::CornerPeek, CoverSlot, ManualCornerPeekSide == ECoverFacing::Left ? FName(TEXT("Left")) : FName(TEXT("Right")));
	}
	// Rev 5h: leaning out round the corner he sees what lies beyond it - hidden traps there are spotted.
	UpdatePeekTrapSpotting(DeltaTime);
	if (CVarDebugCornerAim.GetValueOnGameThread() > 0 && GetWorld())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		const FString Head = bCornerAimActive ? FString(IsCornerHoldSpot() ? TEXT("CORNER HOLD") : TEXT("CORNER AIM")) : Now < CornerAimDuckUntil
			? FString::Printf(TEXT("DUCKED (%s, %.1fs)"), CoverDecisionRules::CornerAimDecisionName(LastCornerAimBreak), CornerAimDuckUntil - Now)
			: FString(TEXT("in cover"));
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 120.f), Head + TEXT("\n") + CornerAimLabel, nullptr,
			bCornerAimActive ? FColor::Green : FColor::Orange, 0.f, true, 1.f);
	}
	if (bIsCornerLeaning && !bCornerAimActive)
	{
		LeanTimer -= DeltaTime;
		if (LeanTimer <= 0.f)
		{
			bIsCornerLeaning = false; // back behind the corner
		}
	}
	if (bIsBlindFiring)
	{
		BlindFireTimer -= DeltaTime;
		if (BlindFireTimer <= 0.f)
		{
			bIsBlindFiring = false;
		}
	}
	if (bShimmying && !bHasMoveOrder && GetVelocity().SizeSquared2D() < 4.f)
	{
		bShimmying = false;
		ShimmyDirection = 0.f;
		bCoverAutoSnap = false;
	}
	// Facing along the wall towards the last known threat (user design rule 2026-10-06).
	CoverThreatTimer -= DeltaTime;
	if (CoverThreatTimer <= 0.f)
	{
		CoverThreatTimer = FCoverFacingConfig().ThreatUpdateSeconds;
		UpdateCoverFacing(/*bAllowSnap*/ true);
	}
	if (bCoverSnapPending && !bShimmying && !bHasMoveOrder)
	{
		TrySnapToCoverCorner();
	}
}

float AOperativeCharacter::GetCoverFacingYaw() const
{
	return CoverFacingRules::FacingYaw(CoverSlot, CoverFacing);
}

bool AOperativeCharacter::IsShimmyForward() const
{
	return CoverFacingRules::IsShimmyForward(CoverFacing, ShimmyDirection, bHasCoverThreat);
}

bool AOperativeCharacter::GatherCoverThreat(FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World || bIgnoreCoverThreatForTesting)
	{
		return false;
	}
	const FCoverFacingConfig Config;
	const FVector Here = GetActorLocation();
	TArray<FCoverThreatCandidate> Candidates;
	auto IsAlive = [](const AActor* Actor)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
		return IsValid(Actor) && (!Enemy || (!Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive()));
	};
	auto Add = [&Candidates, &Here](const FVector& Location, ECoverThreatSource Source)
	{
		FCoverThreatCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Location = Location;
		Candidate.Source = Source;
		Candidate.DistanceCm = static_cast<float>(FVector::Dist2D(Here, Location));
	};
	// The enemy he is ordered to / does shoot at (while it is in sight; out of sight its silhouette counts as heard). A quick
	// fire order (Ctrl + click on a point, 2026-10-19): its point.
	if (bQuickFireActive)
	{
		Add(QuickFirePoint, ECoverThreatSource::PriorityTarget);
	}
	else
	{
		for (const AActor* Priority : { CoverShotTarget.Get(), ManualPriorityTarget.Get(), CurrentCombatTarget.Get() })
		{
			if (Priority && IsAlive(Priority) && !Priority->IsHidden())
			{
				Add(Priority->GetActorLocation(), ECoverThreatSource::PriorityTarget);
				break;
			}
		}
	}
	// Enemies seen now (the tactical sight hides the unseen ones in a fight) and heard / remembered silhouettes.
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (IsAlive(Enemy) && !Enemy->IsHidden() && FVector::Dist2D(Here, Enemy->GetActorLocation()) <= Config.MaxThreatDistanceCm)
		{
			Add(Enemy->GetActorLocation(), ECoverThreatSource::Visible);
		}
	}
	for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
	{
		if (!It->IsHidden() && FVector::Dist2D(Here, It->GetActorLocation()) <= Config.MaxThreatDistanceCm)
		{
			Add(It->GetActorLocation(), ECoverThreatSource::Heard);
		}
	}
	const int32 Best = CoverFacingRules::PickThreat(Candidates);
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutLocation = Candidates[Best].Location;
	return true;
}

ECoverFacing AOperativeCharacter::PredictCoverFacing(const FCoverSlot& Slot) const
{
	// Corner hold (user decision 2026-10-07): a slot at the one exposed edge of a high wall faces that edge - he steps out
	// there at once (CornerHoldSmoke: re-entering after an open shot he faced the closed side for 5 s).
	if (bCornerHoldAtEdge && Slot.Height == ECoverHeight::HighCover && Slot.bLeftEdgeExposed != Slot.bRightEdgeExposed)
	{
		const ECoverFacing Edge = Slot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right;
		if (CoverFacingRules::IsAtCorner(Slot, Edge, FCoverFacingConfig()))
		{
			return Edge;
		}
	}
	FVector Threat;
	if (GatherCoverThreat(Threat))
	{
		return CoverFacingRules::ThreatAlongWall(Slot, Threat) < 0.f ? ECoverFacing::Left : ECoverFacing::Right;
	}
	return CoverFacingRules::DefaultSide(Slot, CoverFacing);
}

void AOperativeCharacter::UpdateCoverFacing(bool bAllowSnap)
{
	if (!bInCover)
	{
		return;
	}
	const FCoverFacingConfig Config;
	FVector Threat;
	if (GatherCoverThreat(Threat))
	{
		bHasCoverThreat = true; // remembered: the facing keeps the last known direction when nobody is in sight
		CoverThreatLocation = Threat;
		CoverThreatSeenTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; // ... the fire-ready pose only while it is fresh
	}
	const ECoverFacing Old = CoverFacing;
	// No threat known: the facing stays (nearest edge from PredictCoverFacing on entry, then the last movement direction).
	CoverFacing = bHasCoverThreat ? CoverFacingRules::ResolveThreatSide(CoverSlot, CoverThreatLocation, CoverFacing, Config.ThreatSideHysteresisCm)
		: CoverFacing;
	// Engaged (leaned out, or a cover shot within CoverEngageHoldSeconds): no flip-flop of the fire stance between the sides.
	{
		const UWorld* FacingWorld = GetWorld();
		// Firing (a shot within CoverEngageHoldSeconds) keeps the side - also at the corner hold, where the stance is held
		// between the shots; a threat that stays on the other side without a shot for that long turns him (the hold ends).
		const bool bEngaged = (bCornerAimActive && bCornerAutoDuck)
			|| (FacingWorld && FacingWorld->GetTimeSeconds() - CoverLastShotTime <= CoverEngageHoldSeconds);
		CoverFacing = CoverFacingRules::KeepEngagedFacing(CoverSlot, Old, CoverFacing, bEngaged && !bShimmying);
	}
	// Corner hold (user decision 2026-10-07: no side flips at that corner): at the one exposed edge of a high wall he faces
	// that edge whatever the threat side - the closed side has nothing to fire round (CornerHoldSmoke 2026-10-07: back at
	// the wall after an open shot he turned to the closed side for a threat there and never leaned out again).
	if (bCornerHoldAtEdge && !bShimmying && CoverSlot.Height == ECoverHeight::HighCover && CoverSlot.bLeftEdgeExposed != CoverSlot.bRightEdgeExposed)
	{
		const ECoverFacing Edge = CoverSlot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right;
		if (CoverFacingRules::IsAtCorner(CoverSlot, Edge, Config))
		{
			CoverFacing = Edge;
		}
	}
	if (bManualCornerPeek)
	{
		CoverFacing = ManualCornerPeekSide; // the ordered corner, whatever the threat side (until another order)
	}
	const bool bWasAtCorner = bAtCoverCorner;
	bAtCoverCorner = CoverFacingRules::IsAtCorner(CoverSlot, CoverFacing, Config);
	if (bAtCoverCorner && !bWasAtCorner && bInCover && !bShimmying)
	{
		RaiseCoverEvent(GameplayEvents::CornerReached, CoverSlot, CoverFacing == ECoverFacing::Left ? FName(TEXT("Left")) : FName(TEXT("Right")));
	}
	if (CoverFacing != Old)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: turns along the wall to the %s (threat %s)"), *DisplayName.ToString(),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), bHasCoverThreat ? TEXT("known") : TEXT("none, nearest edge"));
		bCoverSnapPending |= bAllowSnap && !bShimmying && !bManualCornerPeek;
	}
}

bool AOperativeCharacter::IsCoverThreatActive() const
{
	const UWorld* World = GetWorld();
	return bInCover && bHasCoverThreat && World && World->GetTimeSeconds() - CoverThreatSeenTime <= CoverFireReadyHoldSeconds;
}

bool AOperativeCharacter::IsCoverFireReady() const
{
	const UWorld* World = GetWorld();
	if (!bInCover || !World || bIsReloading)
	{
		return false;
	}
	if (bCornerAimActive)
	{
		return true; // sustained corner aim: the fire stance between the shots
	}
	const double Now = World->GetTimeSeconds();
	if (Now < CornerAimDuckUntil || Now - CornerAimEndTime < CoverFireReadyHoldSeconds)
	{
		return false; // ducked back / just relaxed: the plain cover pose
	}
	// User decision 2026-10-07: the fire-ready corner pose only for a threat behind the wall / round the corner (the same
	// rule as the corner shot); a threat out in front keeps the plain cover idle (it gets an open shot).
	// A low cover is fired over anywhere along it (the crouched fire stance pops the rifle over the top): no corner needed.
	const bool bFiringSpot = CoverFacingRules::IsFiringSpot(CurrentCoverHeight, bAtCoverCorner);
	return CoverFacingRules::IsFireReady(bFiringSpot, bShimmying, bHasCoverThreat, static_cast<float>(Now - CoverThreatSeenTime),
		CoverFireReadyHoldSeconds) && IsCornerShotTarget(CoverThreatLocation);
}

void AOperativeCharacter::LeaveCoverToMove(const FString& Reason)
{
	if (!bInCover)
	{
		return;
	}
	LeaveCover(Reason);
	bCoverLeftToMove = true; // cleared by the next cover entry
}

FCoverDecisionConfig AOperativeCharacter::GetCoverDecisionConfig() const
{
	FCoverDecisionConfig Config;
	Config.AimNoTargetGraceSeconds = CornerAimNoTargetGraceSeconds;
	Config.AimReentryDelaySeconds = CornerAimReentryDelaySeconds;
	return Config;
}

FCoverFacingConfig AOperativeCharacter::GetCoverFacingConfig() const
{
	FCoverFacingConfig Config;
	Config.StandStandOffPackLCm = CoverStandOffStandPackLCm;
	Config.StandStandOffPackRCm = CoverStandOffStandPackRCm;
	Config.CrouchStandOffPackLCm = CoverStandOffCrouchPackLCm;
	Config.CrouchStandOffPackRCm = CoverStandOffCrouchPackRCm;
	return Config;
}

void AOperativeCharacter::PlayCoverShot(AActor* Target, bool bHit)
{
	if (!bInCover)
	{
		return;
	}
	CoverShotTarget = Target;
	UpdateCoverFacing(/*bAllowSnap*/ false);
	if (const UWorld* World = GetWorld())
	{
		bHasCoverThreat = true; // a shot is a threat contact (keeps / enters the fire-ready pose)
		CoverThreatSeenTime = World->GetTimeSeconds();
	}
	SetActorRotation(FRotator(0.f, GetCoverFacingYaw(), 0.f)); // the grid shot resolves at once
	BeginCoverShot();
	(bIsBlindFiring ? CoverBlindShots : CoverLeanShots) += 1;
	OnWeaponFiredNative.Broadcast(this, Target, bHit); // the AnimInstance plays the cover fire clip
}
