// AOperativeCharacter: wall-following round the cover object (game designer 2026-10-20: "the character moves along the wall of any
// building / object without leaving it") - the wrap order, steering along the perimeter path, the face held round each corner, the
// planned wrap of the pause. UE-only (no Godot reference). Pure rules: Tactics/CoverWrapRules (the perimeter path, the corner snap, the
// world plan).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverWrapRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "Tutorial/TutorialSubsystem.h"

namespace
{
	/** He steers at the path point this far ahead of where he is along it (a smooth line round the arcs), cm. */
	constexpr float CoverWrapLookAheadCm = 30.f;
	/** Within this of the goal he is there (EnterCover puts him on it), cm. */
	constexpr float CoverWrapArriveCm = 8.f;
	/** Slower than this on the way, after the first half second, counts as stuck, cm/s. */
	constexpr float CoverWrapStallSpeed = 15.f;
	/** Stuck this long: the wrap is given up where he stands (in cover there), s. */
	constexpr float CoverWrapStallGiveUpSeconds = 1.f;
}

FCoverWrapConfig AOperativeCharacter::GetCoverWrapConfig() const
{
	FCoverWrapConfig Config;
	Config.bEnabled = bCoverWrapEnabled;
	Config.MaxSurfaceDistanceCm = CoverWrapMaxSurfaceDistanceCm;
	Config.MinTravelCm = CoverWrapMinTravelCm;
	Config.MaxTravelCm = CoverWrapMaxTravelCm;
	Config.CornerSnapCm = CoverWrapCornerSnapCm;
	// The cover slot distance, but clear of the capsule by 10 cm (the steering hugs this line exactly).
	Config.OffsetCm = FMath::Max(FCoverTraceConfig().SlotOffsetCm, GetSimpleCollisionRadius() + 10.f);
	return Config;
}

ECoverWrapVerdict AOperativeCharacter::PlanCoverWrap(const FVector& Click, FCoverWrapPlan& OutPlan) const
{
	OutPlan = FCoverWrapPlan();
	if (!bInCover || !CoverSlot.IsValid())
	{
		return ECoverWrapVerdict::NotInCover;
	}
	const bool bCrouched = CurrentCoverHeight == ECoverHeight::LowCover || Stance == EOperativeStance::Crouching;
	const FCoverFacingConfig FacingConfig = GetCoverFacingConfig();
	const TArray<const AActor*> Ignored{ this };
	return CoverWrapWorld::PlanWrap(GetWorld(), CoverSlot, GetActorLocation(), Click, GetCoverWrapConfig(),
		[&FacingConfig, bCrouched](ECoverFacing Facing) { return CoverFacingRules::CornerStandOff(Facing, bCrouched, FacingConfig); }, OutPlan, Ignored);
}

EOperativeOrderResult AOperativeCharacter::OrderCoverWrap(const FCoverWrapPlan& Plan)
{
	if (!bInCover || !Plan.IsValid())
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsRaging() || IsPanicking() || IsKnockedDown())
	{
		return EOperativeOrderResult::Refused;
	}
	if (!UTutorialSubsystem::CheckConfinedOrder(this, this, Plan.GoalSlot.WorldLocation))
	{
		return EOperativeOrderResult::Refused;
	}
	// Along his own face only: the plain shimmy (its clips, its rules).
	if (Plan.Path.CornerCount() == 0)
	{
		CancelCoverWrap(TEXT("a shimmy along the same face"));
		return OrderShimmyTo(Plan.GoalSlot);
	}
	if (Stance == EOperativeStance::Prone)
	{
		// As the shimmy (rev 2026-10-19): no prone wrap - he gets up first (low cover crouched, high cover standing), then goes round.
		const EOperativeStance Up = CurrentCoverHeight == ECoverHeight::LowCover ? EOperativeStance::Crouching : EOperativeStance::Standing;
		const float Rise = GetStanceChangeDelay(EOperativeStance::Prone, Up);
		SetStance(Up);
		if (Stance == EOperativeStance::Prone)
		{
			return EOperativeOrderResult::Refused;
		}
		if (Rise > 0.05f && GetWorld())
		{
			TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
			GetWorld()->GetTimerManager().SetTimer(PendingMoveTimer, FTimerDelegate::CreateLambda([WeakThis, Plan]()
			{
				AOperativeCharacter* Self = WeakThis.Get();
				if (Self && Self->bInCover && !Self->IsKnockedDown() && Self->GetStance() != EOperativeStance::Prone)
				{
					Self->OrderCoverWrap(Plan);
				}
			}), Rise, false);
			return EOperativeOrderResult::Accepted;
		}
	}
	if (bCornerAimActive)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("wrap round the object ordered"));
	}
	bManualCornerPeek = false; // another order: the manual corner peek ends
	bManualCornerPeekEventPending = false;
	CoverWrapPlan = Plan;
	bCoverWrapActive = true; // first: the stop below must not end it (HandleMoveFinished ignores move ends during a wrap)
	bCoverWrapPlanned = false;
	if (AController* OwnController = GetController())
	{
		OwnController->StopMovement(); // he steers himself along the path (no navmesh path: it would keep the navmesh's distance off the wall)
	}
	CoverWrapSlotIndex = 0;
	CoverWrapStallSeconds = 0.f;
	CoverWrapElapsedSeconds = 0.f;
	CoverWrapProgressCm = 0.f;
	CoverWrapNext = 1;
	CoverWrapPointDistance.Reset();
	float Along = 0.f;
	for (int32 Index = 0; Index < Plan.Path.Points.Num(); ++Index)
	{
		Along += Index > 0 ? static_cast<float>(FVector2D::Distance(Plan.Path.Points[Index - 1], Plan.Path.Points[Index])) : 0.f;
		CoverWrapPointDistance.Add(Along);
	}
	PendingCoverSlot = Plan.GoalSlot;
	bHasPendingCover = true;
	bHasMoveOrder = true;
	// The same side-step all the way: round a corner (convex or concave) his right stays his right along the next face.
	ShimmyDirection = static_cast<float>(Plan.Path.Direction);
	CoverFacing = Plan.Path.Direction > 0 ? ECoverFacing::Right : ECoverFacing::Left;
	bShimmying = true;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	bCoverSnapPending = false;
	bCoverAutoSnap = false;
	bAtCoverCorner = false;
	bSprinting = false;
	ApplyMovementParams(OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Crouching, false, IsWounded(), bCarrying) * ColdSpeedMultiplier);
	SetCoverWrapFace(Plan.WaypointSlot[0]);
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: round the object - %.0f cm along its wall to the %s, %d corner(s) (%d outer, %d inner), ends %s facing %s"),
		*DisplayName.ToString(), Plan.Path.LengthCm, Plan.Path.Direction > 0 ? TEXT("right") : TEXT("left"), Plan.Path.CornerCount(), Plan.Path.ConvexCorners,
		Plan.Path.ConcaveCorners, Plan.bGoalAtCorner ? TEXT("at the corner") : TEXT("on the face"), Plan.GoalFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"));
	return EOperativeOrderResult::Accepted;
}

void AOperativeCharacter::SetPendingCoverWrap(const FCoverWrapPlan& Plan)
{
	bCoverWrapPlanned = bHasPendingCover && Plan.IsValid();
	if (bCoverWrapPlanned)
	{
		CoverWrapPlan = Plan;
	}
}

EOperativeOrderResult AOperativeCharacter::StartPlannedCoverWrap()
{
	const FCoverWrapPlan Plan = CoverWrapPlan;
	bCoverWrapPlanned = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: the planned way round the object starts"), *DisplayName.ToString());
	return OrderCoverWrap(Plan);
}

void AOperativeCharacter::SetCoverWrapFace(int32 SlotIndex)
{
	if (SlotIndex == CoverWrapSlotIndex || !CoverWrapPlan.Slots.IsValidIndex(SlotIndex))
	{
		return;
	}
	CoverWrapSlotIndex = SlotIndex;
	// In cover on the next face from here on: the actor turns its back to it (UpdateCombatFacing eases the yaw round while he walks the arc).
	CoverSlot = CoverWrapPlan.Slots[SlotIndex];
	CurrentCoverHeight = CoverSlot.Height;
	bAtCoverCorner = false;
	++CoverWrapCornersTurned;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: round the corner onto face %d of %d"), *DisplayName.ToString(), SlotIndex + 1, CoverWrapPlan.Slots.Num());
}

void AOperativeCharacter::UpdateCoverWrap(float DeltaTime)
{
	if (!bCoverWrapActive)
	{
		return;
	}
	if (!bInCover)
	{
		CancelCoverWrap(TEXT("out of cover"));
		return;
	}
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (IsPanicking() || IsRaging() || IsKnockedDown() || (TurnBased && TurnBased->IsActive()))
	{
		GiveUpCoverWrapHere(TEXT("interrupted"));
		return;
	}
	const FCoverWrapPath& Path = CoverWrapPlan.Path;
	const FVector Here = GetActorLocation();
	// How far along the path he is (never back), from the segment he is on.
	const float Along = CoverWrapRules::ProgressAlongPolyline(Path.Points, FVector2D(Here), FMath::Max(CoverWrapNext - 1, 0), 3);
	CoverWrapProgressCm = FMath::Max(CoverWrapProgressCm, Along);
	// The faces of the waypoints passed (the next face from a corner's arc start / mitre point on).
	while (CoverWrapNext < CoverWrapPointDistance.Num() && CoverWrapPointDistance[CoverWrapNext] <= CoverWrapProgressCm + 1.f)
	{
		SetCoverWrapFace(CoverWrapPlan.WaypointSlot[CoverWrapNext]);
		++CoverWrapNext;
	}
	const float ToGoal = Path.LengthCm - CoverWrapProgressCm;
	if (ToGoal <= CoverWrapArriveCm || FVector::Dist2D(Here, CoverWrapPlan.Waypoints.Last()) <= CoverWrapArriveCm)
	{
		// At the goal: in cover there (EnterCover: the plan's facing, the face's slot).
		bHasMoveOrder = false;
		bHasPendingCover = false;
		SetCoverWrapFace(CoverWrapPlan.WaypointSlot.Last());
		EnterCover(CoverWrapPlan.GoalSlot);
		return;
	}
	// Steer at the path point a little ahead (the arcs come out round), slowing over the last 40 cm.
	const FVector2D Target = CoverWrapRules::PointAtDistance(Path.Points, CoverWrapProgressCm + CoverWrapLookAheadCm);
	const FVector Direction = FVector(Target.X - Here.X, Target.Y - Here.Y, 0.f).GetSafeNormal();
	AddMovementInput(Direction, FMath::Clamp(ToGoal / 40.f, 0.3f, 1.f));
	bHasMoveOrder = true;
	bShimmying = true;
	CoverWrapElapsedSeconds += DeltaTime;
	if (CoverWrapElapsedSeconds > 0.5f && GetVelocity().Size2D() < CoverWrapStallSpeed)
	{
		CoverWrapStallSeconds += DeltaTime;
		if (CoverWrapStallSeconds >= CoverWrapStallGiveUpSeconds)
		{
			GiveUpCoverWrapHere(TEXT("stuck on the way (a body or an obstacle in the way)"));
		}
	}
	else
	{
		CoverWrapStallSeconds = 0.f;
	}
}

void AOperativeCharacter::GiveUpCoverWrapHere(const TCHAR* Why)
{
	// In cover where he stands, at the face he holds now (traced again there), else out of cover.
	CancelCoverWrap(Why);
	bHasPendingCover = false;
	bShimmying = false;
	ShimmyDirection = 0.f;
	ApplyMovementParams();
	const FVector Here = GetActorLocation();
	const FVector Normal = CoverSlot.WallNormal.GetSafeNormal2D();
	const FVector Face = Here - Normal * FCoverTraceConfig().SlotOffsetCm;
	FCoverSlot HereSlot;
	if (CoverTraceRules::FindCoverSlotAt(GetWorld(), FVector(Face.X, Face.Y, CoverSlot.WallPoint.Z), -Normal, HereSlot) && CoverWrapWorld::IsSameObject(HereSlot, CoverSlot))
	{
		HereSlot.WorldLocation = FVector(Here.X, Here.Y, HereSlot.WorldLocation.Z); // where he stands (no jump)
		EnterCover(HereSlot);
	}
	else
	{
		LeaveCover(TEXT("the way round the object is blocked"));
	}
}

bool AOperativeCharacter::ContinueCoverWrapOnArrival()
{
	// He steers himself along the wrap's path: a path following's end (the stop of an earlier move) is no arrival.
	return bCoverWrapActive && bInCover;
}

void AOperativeCharacter::CancelCoverWrap(const TCHAR* Why)
{
	if (!bCoverWrapActive && !bCoverWrapPlanned)
	{
		return;
	}
	if (bCoverWrapActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: the way round the object ends (%s)"), *DisplayName.ToString(), Why);
		bHasMoveOrder = false; // his own steering stops (a following order sets its own move)
	}
	bCoverWrapActive = false;
	bCoverWrapPlanned = false;
}
