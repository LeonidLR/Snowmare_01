#include "UI/FormationPlacementRules.h"

void FFormationPlacement::Begin(int32 InSlot, int32 InAnchorSquad, const TArray<FFormationCustomGhost>& InGhosts)
{
	bActive = true;
	Slot = InSlot;
	AnchorSquad = InAnchorSquad;
	Ghosts = InGhosts;
	Selected.Reset();
	PressPoint.Reset();
}

void FFormationPlacement::End()
{
	bActive = false;
	Selected.Reset();
	PressPoint.Reset();
	Ghosts.Reset();
}

const FFormationCustomGhost* FFormationPlacement::FindGhost(int32 Squad) const
{
	return Ghosts.FindByPredicate([Squad](const FFormationCustomGhost& Ghost) { return Ghost.Squad == Squad; });
}

float FFormationPlacement::DefaultYaw() const
{
	const FFormationCustomGhost* Anchor = FindGhost(AnchorSquad);
	return Anchor ? Anchor->Yaw : 0.f;
}

EFormationPlacementResult FFormationPlacement::Select(int32 Squad)
{
	if (!bActive)
	{
		return EFormationPlacementResult::None;
	}
	Selected = Squad;
	PressPoint.Reset();
	return EFormationPlacementResult::Selected;
}

TOptional<int32> FFormationPlacement::GhostAt(const FVector2D& Ground, double Radius) const
{
	TOptional<int32> Best;
	double BestDistance = Radius;
	for (const FFormationCustomGhost& Ghost : Ghosts)
	{
		const double Distance = FVector2D::Distance(Ghost.Position, Ground);
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Ghost.Squad;
		}
	}
	return Best;
}

EFormationPlacementResult FFormationPlacement::Press(const FVector2D& Ground)
{
	if (!bActive)
	{
		return EFormationPlacementResult::None;
	}
	if (!Selected.IsSet())
	{
		return EFormationPlacementResult::NoOperative;
	}
	PressPoint = Ground;
	return EFormationPlacementResult::Pressed;
}

TOptional<float> FFormationPlacement::PreviewLook(const FVector2D& Cursor) const
{
	if (!bActive || !PressPoint.IsSet())
	{
		return TOptional<float>();
	}
	// The anchor's own plain click keeps the heading he has; anyone else's takes the anchor's (the squad's heading).
	return FormationCustomRules::DragLook(PressPoint.GetValue(), Cursor, DefaultYaw());
}

EFormationPlacementResult FFormationPlacement::Release(const FVector2D& Cursor, const FFormationConfig& Config)
{
	if (!bActive || !PressPoint.IsSet() || !Selected.IsSet())
	{
		PressPoint.Reset();
		return EFormationPlacementResult::None;
	}
	const FVector2D Spot = PressPoint.GetValue();
	PressPoint.Reset();
	const int32 Squad = Selected.GetValue();
	const EFormationCustomPlace Allowed = FormationCustomRules::CanPlace(Ghosts, AnchorSquad, Squad, Spot, Config);
	if (Allowed != EFormationCustomPlace::Ok)
	{
		return Allowed == EFormationCustomPlace::TooFar ? EFormationPlacementResult::TooFar : EFormationPlacementResult::TooClose;
	}
	const float Look = FormationCustomRules::DragLook(Spot, Cursor, DefaultYaw());
	FFormationCustomGhost* Own = Ghosts.FindByPredicate([Squad](const FFormationCustomGhost& Ghost) { return Ghost.Squad == Squad; });
	if (!Own)
	{
		Own = &Ghosts.AddDefaulted_GetRef();
		Own->Squad = Squad;
	}
	Own->Position = Spot;
	Own->Yaw = Look;
	return EFormationPlacementResult::Placed;
}

float FormationPlacementRules::PoseOffRefDeg(TConstArrayView<FTransform> Pose, TConstArrayView<FTransform> RefPose)
{
	double Worst = 0.0;
	const int32 Count = FMath::Min(Pose.Num(), RefPose.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Worst = FMath::Max(Worst, Pose[Index].GetRotation().GetNormalized().AngularDistance(RefPose[Index].GetRotation().GetNormalized()));
	}
	return static_cast<float>(FMath::RadiansToDegrees(Worst));
}

bool FormationPlacementRules::IsPosed(float OffRefDeg)
{
	return OffRefDeg >= PosedMinDeg;
}

FormationPlacementRules::EBTap FormationPlacementRules::ResolveBTap(bool bPlacementActive)
{
	return bPlacementActive ? EBTap::ConfirmPlacement : EBTap::SoloToggle;
}
