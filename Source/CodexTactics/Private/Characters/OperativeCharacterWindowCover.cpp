// AOperativeCharacter: fire through a window from cover (G016). The opening tolerances are WindowBreakage's.
// Passage through the pane stays in WindowOrderRules; this file never writes bWindowPathAllowed.
#include "Characters/OperativeCharacter.h"

#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeMovementRules.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Interactables/VaultNavigation.h"
#include "Interactables/WindowBreakage.h"
#include "Interactables/WindowOrderRules.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/WindowCoverRules.h"

namespace
{
	void ReadStorey(TConstArrayView<FName> Tags, FString& Building, int32& Floor)
	{
		FString ParsedBuilding;
		int32 ParsedFloor = INDEX_NONE;
		RoofCutawayRules::ParseStoreyTags(Tags, ParsedBuilding, ParsedFloor);
		if (Building.IsEmpty() && !ParsedBuilding.IsEmpty())
		{
			Building = ParsedBuilding;
		}
		if (Floor == INDEX_NONE && ParsedFloor != INDEX_NONE)
		{
			Floor = ParsedFloor;
		}
	}

	int32 FloorOf(const AActor* Actor, const UPrimitiveComponent* Wall, FString& OutBuilding)
	{
		int32 Floor = INDEX_NONE;
		if (Actor)
		{
			ReadStorey(Actor->Tags, OutBuilding, Floor);
		}
		if (Wall)
		{
			ReadStorey(Wall->ComponentTags, OutBuilding, Floor);
			if (const AActor* Owner = Wall->GetOwner())
			{
				ReadStorey(Owner->Tags, OutBuilding, Floor);
			}
		}
		return Floor;
	}
	float SlotFeetZ(const AOperativeCharacter& Op)
	{
		return static_cast<float>(Op.GetActorLocation().Z) - Op.GetSimpleCollisionHalfHeight();
	}
}

float AOperativeCharacter::GetWindowCoverSillTopCm() const
{
	return WindowCoverRules::EffectiveSillTopCm(WindowCover.SillTopCm);
}

FCoverSlot AOperativeCharacter::GetEffectiveCoverSlotForCorner() const
{
	FCoverSlot Effective = CoverSlot;
	if (WindowCover.bLeftEdgeExposed && (!Effective.bLeftEdgeExposed || WindowCover.LeftEdgeDistanceCm < Effective.LeftEdgeDistanceCm))
	{
		Effective.bLeftEdgeExposed = true;
		Effective.LeftEdgeDistanceCm = WindowCover.LeftEdgeDistanceCm;
	}
	if (WindowCover.bRightEdgeExposed && (!Effective.bRightEdgeExposed || WindowCover.RightEdgeDistanceCm < Effective.RightEdgeDistanceCm))
	{
		Effective.bRightEdgeExposed = true;
		Effective.RightEdgeDistanceCm = WindowCover.RightEdgeDistanceCm;
	}
	return Effective;
}

bool AOperativeCharacter::HasAnyExposedCoverEdge() const
{
	return CoverSlot.HasExposedEdge() || WindowCover.bLeftEdgeExposed || WindowCover.bRightEdgeExposed;
}

bool AOperativeCharacter::IsExposedCoverEdge(ECoverFacing Facing) const
{
	return CoverSlot.IsEdgeExposed(Facing)
		|| (Facing == ECoverFacing::Left ? WindowCover.bLeftEdgeExposed : WindowCover.bRightEdgeExposed);
}

float AOperativeCharacter::GetExposedCoverEdgeDistanceCm(ECoverFacing Facing) const
{
	const float WallDist = CoverSlot.IsEdgeExposed(Facing)
		? (Facing == ECoverFacing::Left ? CoverSlot.LeftEdgeDistanceCm : CoverSlot.RightEdgeDistanceCm)
		: -1.f;
	const bool bWin = Facing == ECoverFacing::Left ? WindowCover.bLeftEdgeExposed : WindowCover.bRightEdgeExposed;
	const float WinDist = bWin ? (Facing == ECoverFacing::Left ? WindowCover.LeftEdgeDistanceCm : WindowCover.RightEdgeDistanceCm) : -1.f;
	if (WallDist < 0.f)
	{
		return WinDist;
	}
	if (WinDist < 0.f)
	{
		return WallDist;
	}
	return FMath::Min(WallDist, WinDist);
}

bool AOperativeCharacter::GetActiveWindowForShot(const FVector& AimAt, WindowCoverRules::FWindowCoverBinding& OutBinding) const
{
	if (!bInCover)
	{
		return false;
	}
	const FVector From = GetCoverFireOrigin();
	const FVector Delta = AimAt - From;
	if (Delta.IsNearlyZero())
	{
		return false;
	}
	if (IsWindowCoverConfirmed() && WindowCover.Opening.IsValid && FMath::LineBoxIntersection(WindowCover.Opening, From, AimAt, Delta))
	{
		OutBinding = WindowCover;
		return true;
	}
	if (bAtCoverCorner && !CoverSlot.IsEdgeExposed(CoverFacing))
	{
		const bool bLeft = CoverFacing == ECoverFacing::Left;
		const bool bWinEdge = bLeft ? WindowCover.bLeftEdgeExposed : WindowCover.bRightEdgeExposed;
		const FBox& EdgeOpening = bLeft ? WindowCover.LeftEdgeOpening : WindowCover.RightEdgeOpening;
		AActor* EdgeSill = bLeft ? WindowCover.LeftEdgeSill.Get() : WindowCover.RightEdgeSill.Get();
		if (bWinEdge && EdgeSill && EdgeOpening.IsValid && FMath::LineBoxIntersection(EdgeOpening, From, AimAt, Delta))
		{
			OutBinding = WindowCover;
			OutBinding.Sill = EdgeSill;
			OutBinding.Opening = EdgeOpening;
			OutBinding.Glass = bLeft ? WindowCover.LeftEdgeGlass : WindowCover.RightEdgeGlass;
			OutBinding.GlassRounds = bLeft ? WindowCover.LeftEdgeGlassRounds : WindowCover.RightEdgeGlassRounds;
			OutBinding.bConfirmed = true;
			return true;
		}
	}
	return false;
}

bool AOperativeCharacter::IsWindowCoverShot(const FVector& AimAt) const
{
	WindowCoverRules::FWindowCoverBinding Binding;
	return GetActiveWindowForShot(AimAt, Binding);
}

bool AOperativeCharacter::TryGetWindowCoverAimPoint(FVector& OutAim) const
{
	if (bHasCoverThreat)
	{
		OutAim = CoverThreatLocation;
		return true;
	}
	if (const AActor* Shot = CoverShotTarget.Get())
	{
		OutAim = Shot->GetActorLocation();
		return true;
	}
	if (const AActor* Combat = GetCurrentCombatTarget())
	{
		OutAim = Combat->GetActorLocation();
		return true;
	}
	return false;
}

bool AOperativeCharacter::HasWindowCoverAim() const
{
	FVector Aim;
	return IsWindowCoverConfirmed() && !bShimmying && !bAtCoverCorner && !IsCoverShotBlind()
		&& TryGetWindowCoverAimPoint(Aim) && IsWindowCoverShot(Aim);
}

bool AOperativeCharacter::IsCoverBodyAligned() const
{
	const float Tolerance = FCoverFacingConfig().FacingToleranceDeg;
	if (HasWindowCoverAim())
	{
		return FMath::Abs(FRotator::NormalizeAxis(GetActorRotation().Yaw - GetCoverFacingYaw())) <= Tolerance;
	}
	return CoverFacingRules::IsFacingAligned(GetActorRotation().Yaw, CoverSlot, CoverFacing, Tolerance);
}

bool AOperativeCharacter::IsWindowCoverStopped() const
{
	return !bShimmying && !bHasMoveOrder && GetVelocity().SizeSquared2D() <= FMath::Square(4.f);
}

void AOperativeCharacter::ClearWindowCover()
{
	if (bWindowCoverForcedCrouch && HealthComponent && HealthComponent->IsAlive() && CurrentCoverHeight == ECoverHeight::HighCover
		&& Stance == EOperativeStance::Crouching && !(ColdSurvival && ColdSurvival->IsFrostbitten()))
	{
		SetStance(EOperativeStance::Standing);
	}
	if (WindowCover.bConfirmed || WindowCover.bHasInterval)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: left the window, back to the wall"), *DisplayName.ToString());
	}
	bWindowCoverForcedCrouch = false;
	bWindowGlassShot = false;
	WindowCover = WindowCoverRules::FWindowCoverBinding();
	WindowCoverSearchedAt = FVector(FLT_MAX);
	WindowCoverLastPlace = WindowCoverRules::EWindowIntervalPlace::Outside;
	WindowCoverSegment = FVector(FLT_MAX);
	WindowCoverWall.Reset();
}

void AOperativeCharacter::ApplyWindowCoverStance()
{
	const float AlongFromOrigin = WindowCover.AlongAxis.IsNearlyZero()
		? 0.f
		: static_cast<float>(FVector::DotProduct(GetActorLocation() - WindowCover.SpanOrigin, WindowCover.AlongAxis));
	const bool bShimmyWindowCrouch = bShimmying && CurrentCoverHeight == ECoverHeight::HighCover
		&& WindowCoverRules::WantsPathCrouch(AlongFromOrigin, WindowCover.WallSpans);
	const bool bWant = WindowCoverRules::WantsCrouch(WindowCover.bConfirmed, bShimmying, bAtCoverCorner, IsWindowCoverStopped())
		|| bShimmyWindowCrouch;
	const bool bLocked = Stance == EOperativeStance::Prone || (ColdSurvival && ColdSurvival->IsFrostbitten());
	const auto RestoreShimmySpeed = [this]()
	{
		if (bShimmying)
		{
			ApplyMovementParams(OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Crouching, false, IsWounded(), bCarrying) * ColdSpeedMultiplier);
		}
	};
	if (bWant)
	{
		if (!bLocked && Stance != EOperativeStance::Crouching)
		{
			SetStance(EOperativeStance::Crouching);
			RestoreShimmySpeed();
			bWindowCoverForcedCrouch = CurrentCoverHeight == ECoverHeight::HighCover;
			UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: crouched %s"), *DisplayName.ToString(),
				bShimmyWindowCrouch ? TEXT("under the window while shimmying") : TEXT("at the window"));
		}
		return;
	}
	if (bWindowCoverForcedCrouch)
	{
		if (!bLocked && CurrentCoverHeight == ECoverHeight::HighCover && Stance == EOperativeStance::Crouching)
		{
			SetStance(EOperativeStance::Standing);
			RestoreShimmySpeed();
			if (bShimmying)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: stood back up past the window"), *DisplayName.ToString());
			}
		}
		bWindowCoverForcedCrouch = false;
	}
}

void AOperativeCharacter::UpdateWallSlideWindowCrouch(const FVector& WallOrigin, const FVector& WallNormal, bool bActive)
{
	UWorld* World = GetWorld();
	const bool bLocked = Stance == EOperativeStance::Prone || (ColdSurvival && ColdSurvival->IsFrostbitten()) || bCarrying || bInCover;
	if (!bActive || !World || bLocked)
	{
		if (bWallSlideWindowCrouch)
		{
			bWallSlideWindowCrouch = false;
			if (!bLocked && Stance == EOperativeStance::Crouching && IsStanceFromSquad())
			{
				SetStance(EOperativeStance::Standing);
				MarkStanceFromSquad();
			}
		}
		return;
	}
	const FVector Normal = WallNormal.GetSafeNormal2D();
	const FVector AlongAxis = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (Normal.IsNearlyZero() || AlongAxis.IsNearlyZero())
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const float FeetZ = SlotFeetZ(*this);
	TArray<WindowCoverRules::FWindowCrouchSpan> Spans;
	static const FName SillTag(TEXT("window_sill"));
	const float Limit = WindowBreakage::FaceMarginCm + WindowBreakage::OpeningPadCm;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Sill = *It;
		if (!Sill->ActorHasTag(SillTag))
		{
			continue;
		}
		const FBox SillBox = Sill->GetComponentsBoundingBox(true);
		if (!SillBox.IsValid)
		{
			continue;
		}
		const FVector Center = SillBox.GetCenter();
		if (FVector::Dist2D(Center, Here) > 2500.f
			|| !WindowCoverRules::SharesFloor(INDEX_NONE, INDEX_NONE, FeetZ + 90.f, Center.Z)
			|| !WindowCoverRules::OnSameWallPlane(Center, WallOrigin, Normal, Limit))
		{
			continue;
		}
		const FBox Opening = WindowBreakage::MakeOpeningBox(SillBox);
		float MinAlong = TNumericLimits<float>::Max();
		float MaxAlong = TNumericLimits<float>::Lowest();
		const FVector Corners[] = {
			FVector(Opening.Min.X, Opening.Min.Y, Center.Z),
			FVector(Opening.Max.X, Opening.Min.Y, Center.Z),
			FVector(Opening.Min.X, Opening.Max.Y, Center.Z),
			FVector(Opening.Max.X, Opening.Max.Y, Center.Z)
		};
		for (const FVector& Corner : Corners)
		{
			const float Along = static_cast<float>(FVector::DotProduct(Corner - WallOrigin, AlongAxis));
			MinAlong = FMath::Min(MinAlong, Along);
			MaxAlong = FMath::Max(MaxAlong, Along);
		}
		Spans.Add(WindowCoverRules::CrouchSpanForOpening(MinAlong, MaxAlong, WindowCoverRules::ShimmyCrouchMarginCm,
			static_cast<float>(SillBox.Max.Z) - FeetZ, static_cast<float>(Opening.Max.Z) - FeetZ));
	}
	const float HereAlong = static_cast<float>(FVector::DotProduct(Here - WallOrigin, AlongAxis));
	const bool bWant = WindowCoverRules::WantsPathCrouch(HereAlong, Spans);
	if (bWant)
	{
		if (Stance != EOperativeStance::Crouching)
		{
			SetStance(EOperativeStance::Crouching);
			MarkStanceFromSquad();
			bWallSlideWindowCrouch = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: crouched under the window during wall walk"), *DisplayName.ToString());
		}
	}
	else if (bWallSlideWindowCrouch)
	{
		bWallSlideWindowCrouch = false;
		if (Stance == EOperativeStance::Crouching && IsStanceFromSquad())
		{
			SetStance(EOperativeStance::Standing);
			MarkStanceFromSquad();
			UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: stood up after the window during wall walk"), *DisplayName.ToString());
		}
	}
}

void AOperativeCharacter::BreakWindowGlassFromShot(AActor* Sill)
{
	if (!Sill)
	{
		return;
	}
	static const FName BrokenTag(TEXT("window_broken"));
	if (Sill->ActorHasTag(BrokenTag))
	{
		return;
	}
	Sill->Tags.Add(BrokenTag);
	WindowBreakage::HideBrokenWindow(*Sill);
	VaultNavigation::SetWindowBroken(*Sill);
	GameplayEvents::Raise(this, FName(TEXT("WindowBroken")), Sill);
	if (WindowCover.Sill.Get() == Sill)
	{
		++WindowCover.GlassRounds;
	}
	if (WindowCover.LeftEdgeSill.Get() == Sill)
	{
		++WindowCover.LeftEdgeGlassRounds;
	}
	if (WindowCover.RightEdgeSill.Get() == Sill)
	{
		++WindowCover.RightEdgeGlassRounds;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: the round is spent on %s, the target is not hit"), *DisplayName.ToString(), *Sill->GetName());
}

void AOperativeCharacter::ResolveWindowGlassRound(const FVector& AimAt)
{
	// The flag is this shot's result, not a latch. A leftover true from the glass cartridge would swallow the next round.
	bWindowGlassShot = false;
	WindowCoverRules::FWindowCoverBinding Active;
	if (!GetActiveWindowForShot(AimAt, Active))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WindowCoverGlass), false, this);
	const WindowCoverRules::FWindowLineView View = WindowCoverRules::TraceThroughWindow(*World, this, Active, GetCoverFireOrigin(), AimAt, Params);
	if (View.bIntactGlass)
	{
		bWindowGlassShot = true;
		WindowGlassImpact = View.ImpactPoint;
	}
}

void AOperativeCharacter::RefreshWindowCover(bool bForce)
{
	UWorld* World = GetWorld();
	if (!bInCover || !World || !CoverSlot.IsValid())
	{
		return;
	}
	// A cover shot is not a window order. Passage stays shut unless something else already broke the pane.
	(void)WindowOrderRules::MayTakeWholeWindow(false, false, WindowOrderRules::Get());

	const FVector Here = GetActorLocation();
	const bool bLostSill = WindowCover.Sill.IsStale();
	FString SlotBuildingNow;
	const int32 SlotFloorNow = FloorOf(CoverSlot.WallActor.Get(), CoverSlot.WallComponent.Get(), SlotBuildingNow);
	const bool bFloorChanged = WindowCover.bHasInterval && SlotFloorNow != INDEX_NONE && WindowCover.Floor != INDEX_NONE
		&& SlotFloorNow != WindowCover.Floor;
	const FVector Segment = bShimmying && bHasPendingCover ? PendingCoverSlot.WorldLocation : FVector(FLT_MAX);
	const bool bNewSegment = bShimmying && !WindowCoverSegment.Equals(Segment, 1.f);
	WindowCoverRules::EWindowIntervalPlace Place = WindowCoverRules::EWindowIntervalPlace::Outside;
	if (WindowCover.bHasInterval)
	{
		const float Along = FVector::DotProduct(Here - WindowCover.IntervalOrigin, WindowCover.AlongAxis);
		Place = WindowCoverRules::PlaceOnInterval(Along, WindowCover.AlongMinCm, WindowCover.AlongMaxCm);
	}
	const bool bCrossed = WindowCover.bHasInterval && Place != WindowCoverLastPlace;
	const bool bWallChanged = WindowCoverWall.Get() != CoverSlot.WallComponent.Get();
	const bool bSameSpot = FVector::DistSquared2D(Here, WindowCoverSearchedAt) <= FMath::Square(5.f);
	const bool bStanceChanged = WindowCoverLastEdgeStance != Stance;
	if (!bForce && !bLostSill && !bCrossed && !bWallChanged && !bFloorChanged && !bNewSegment && !bStanceChanged && bSameSpot)
	{
		WindowCoverLastPlace = Place;
		return;
	}

	FString& SlotBuilding = SlotBuildingNow;
	const int32 SlotFloor = SlotFloorNow;
	const uint32 SlotWallId = CoverSlot.WallComponent.IsValid() ? CoverSlot.WallComponent->GetUniqueID() : 0;
	const FVector AlongAxis = FVector::CrossProduct(FVector::UpVector, CoverSlot.WallNormal).GetSafeNormal();
	static const FName SillTag(TEXT("window_sill"));
	const float FeetZ = SlotFeetZ(*this);

	WindowCoverRules::FWindowCoverBinding Best;
	Best.Reject = WindowCoverRules::EWindowCoverReject::SolidWall;
	bool bFound = false;
	float BestDistance = TNumericLimits<float>::Max();
	TArray<WindowCoverRules::FWindowCrouchSpan> CollectedSpans;
	TArray<WindowCoverRules::FWindowEdgeOpening> EdgeOpenings;
	TArray<AActor*> EdgeSills;
	TArray<FBox> EdgeBoxes;
	const float EdgeReachCm = GetCoverFacingConfig().AutoCornerSnapCm;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Sill = *It;
		if (!Sill->ActorHasTag(SillTag))
		{
			continue;
		}
		const FBox SillBox = Sill->GetComponentsBoundingBox(true);
		if (!SillBox.IsValid)
		{
			continue;
		}
		const FVector Center = SillBox.GetCenter();
		FString SillBuilding;
		const int32 SillFloor = FloorOf(Sill, nullptr, SillBuilding);
		if (!WindowCoverRules::SharesFloor(SlotFloor, SillFloor, CoverSlot.WallPoint.Z, Center.Z))
		{
			continue;
		}
		if (!SlotBuilding.IsEmpty() && !SillBuilding.IsEmpty() && SlotBuilding != SillBuilding)
		{
			continue;
		}
		float Limit = WindowBreakage::FaceMarginCm + WindowBreakage::OpeningPadCm;
		if (const UPrimitiveComponent* Wall = CoverSlot.WallComponent.Get())
		{
			Limit = WindowCoverRules::PlaneLimitCm(Wall->Bounds.BoxExtent, CoverSlot.WallNormal);
		}
		const bool bPlane = WindowCoverRules::OnSameWallPlane(Center, CoverSlot.WallPoint, CoverSlot.WallNormal, Limit);
		const FBox OpeningBox = WindowBreakage::MakeOpeningBox(SillBox);

		// Collect all windows on the current wall plane for shimmy ducking and window-edge corner detection.
		if (bPlane && FVector::Dist2D(Center, Here) <= 3000.f)
		{
			float SpanMin = TNumericLimits<float>::Max();
			float SpanMax = TNumericLimits<float>::Lowest();
			float HereMin = TNumericLimits<float>::Max();
			float HereMax = TNumericLimits<float>::Lowest();
			const FVector SpanCorners[] = {
				FVector(OpeningBox.Min.X, OpeningBox.Min.Y, Center.Z),
				FVector(OpeningBox.Max.X, OpeningBox.Min.Y, Center.Z),
				FVector(OpeningBox.Min.X, OpeningBox.Max.Y, Center.Z),
				FVector(OpeningBox.Max.X, OpeningBox.Max.Y, Center.Z)
			};
			for (const FVector& Corner : SpanCorners)
			{
				const float AlongFromWall = static_cast<float>(FVector::DotProduct(Corner - CoverSlot.WallPoint, AlongAxis));
				SpanMin = FMath::Min(SpanMin, AlongFromWall);
				SpanMax = FMath::Max(SpanMax, AlongFromWall);
				const float AlongFromHere = static_cast<float>(FVector::DotProduct(Corner - Here, AlongAxis));
				HereMin = FMath::Min(HereMin, AlongFromHere);
				HereMax = FMath::Max(HereMax, AlongFromHere);
			}
			const float SillAboveFeet = static_cast<float>(SillBox.Max.Z) - FeetZ;
			const float TopAboveFeet = static_cast<float>(OpeningBox.Max.Z) - FeetZ;
			CollectedSpans.Add(WindowCoverRules::CrouchSpanForOpening(SpanMin, SpanMax, WindowCoverRules::ShimmyCrouchMarginCm,
				SillAboveFeet, TopAboveFeet));
			// Only windows outside the operative's own interval (not past a real wall end) act as corner frame edges.
			const float Tol = WindowCoverRules::WindowFrameEdgeToleranceCm;
			const bool bOutside = (HereMin >= -Tol && HereMax > Tol && (!CoverSlot.bRightEdgeExposed || HereMin < CoverSlot.RightEdgeDistanceCm))
				|| (HereMax <= Tol && HereMin < -Tol && (!CoverSlot.bLeftEdgeExposed || -HereMax < CoverSlot.LeftEdgeDistanceCm));
			if (bOutside && FVector::Dist2D(Center, Here) <= WindowBreakage::ThroughLandingCm + EdgeReachCm)
			{
				WindowCoverRules::FWindowEdgeOpening EdgeOp;
				EdgeOp.AlongMinCm = HereMin;
				EdgeOp.AlongMaxCm = HereMax;
				EdgeOp.SillTopCm = SillAboveFeet;
				EdgeOp.OpeningTopCm = TopAboveFeet;
				EdgeOpenings.Add(EdgeOp);
				EdgeSills.Add(Sill);
				EdgeBoxes.Add(OpeningBox);
			}
		}

		if (!WindowCoverRules::IsNeighborSill(Center, CoverSlot.WallPoint))
		{
			continue;
		}

		WindowCoverRules::FWindowCoverOpening Opening;
		Opening.bPresent = true;
		Opening.Opening = OpeningBox;
		Opening.Floor = SillFloor == INDEX_NONE ? SlotFloor : SillFloor;
		Opening.WallNormal = CoverSlot.WallNormal;
		Opening.WallId = bPlane ? SlotWallId : (SlotWallId + 1);
		Opening.BuildingId = SillBuilding.IsEmpty() ? SlotBuilding : SillBuilding;
		float MinAlong = TNumericLimits<float>::Max();
		float MaxAlong = TNumericLimits<float>::Lowest();
		const FVector Corners[] = {
			FVector(Opening.Opening.Min.X, Opening.Opening.Min.Y, Center.Z),
			FVector(Opening.Opening.Max.X, Opening.Opening.Min.Y, Center.Z),
			FVector(Opening.Opening.Min.X, Opening.Opening.Max.Y, Center.Z),
			FVector(Opening.Opening.Max.X, Opening.Opening.Max.Y, Center.Z)
		};
		for (const FVector& Corner : Corners)
		{
			const float Along = FVector::DotProduct(Corner - Center, AlongAxis);
			MinAlong = FMath::Min(MinAlong, Along);
			MaxAlong = FMath::Max(MaxAlong, Along);
		}
		Opening.AlongMinCm = MinAlong;
		Opening.AlongMaxCm = MaxAlong;

		WindowCoverRules::FWindowCoverSlotFacts Facts;
		Facts.Floor = SlotFloor == INDEX_NONE ? Opening.Floor : SlotFloor;
		Facts.WallId = SlotWallId;
		Facts.WallNormal = CoverSlot.WallNormal;
		Facts.AlongCm = FVector::DotProduct(Here - Center, AlongAxis);
		Facts.bAtCorner = CoverSlot.IsEdgeExposed(CoverFacing) && CoverFacingRules::IsAtCorner(CoverSlot, CoverFacing, GetCoverFacingConfig());
		Facts.BuildingId = SlotBuilding;
		const float MidZ = (Opening.Opening.Min.Z + Opening.Opening.Max.Z) * 0.5f;
		const FVector Probe(CoverSlot.WorldLocation.X, CoverSlot.WorldLocation.Y, MidZ);
		Facts.FirePoint = WindowCoverRules::FirePointOnPlane(Probe, CoverSlot.WallNormal, FVector(CoverSlot.WallPoint.X, CoverSlot.WallPoint.Y, MidZ));

		FHitResult Synthetic;
		Synthetic.bBlockingHit = true;
		Synthetic.ImpactPoint = Facts.FirePoint;
		Synthetic.ImpactNormal = CoverSlot.WallNormal.GetSafeNormal2D();
		Synthetic.TraceStart = CoverSlot.WorldLocation.IsNearlyZero() ? CoverSlot.WallPoint + CoverSlot.WallNormal * 45.f : CoverSlot.WorldLocation;
		Synthetic.Location = Synthetic.ImpactPoint;
		AActor* Found = WindowBreakage::FindWindowSillAt(*World, Synthetic);
		const bool bOverFace = WindowBreakage::IsOverSill(SillBox, Facts.FirePoint, WindowBreakage::FaceMarginCm, WindowBreakage::FaceHeightCm);
		bool bSeen = Found == Sill;
		if (!bSeen && bOverFace)
		{
			FString FoundBuilding;
			const int32 FoundFloor = Found ? FloorOf(Found, nullptr, FoundBuilding) : INDEX_NONE;
			const float FoundZ = Found ? Found->GetComponentsBoundingBox(true).GetCenter().Z : Center.Z;
			bSeen = Found == nullptr || !WindowCoverRules::SharesFloor(SillFloor, FoundFloor, Center.Z, FoundZ);
		}
		if (!bSeen)
		{
			continue;
		}

		const WindowCoverRules::FWindowCoverMatch Match = WindowCoverRules::Match(Opening, Facts);
		const float Distance = FVector::DistSquared2D(Center, CoverSlot.WallPoint);
		if (bFound && Match.bConfirmed == Best.bConfirmed && Distance >= BestDistance)
		{
			continue;
		}
		if (bFound && !Match.bConfirmed && Best.bConfirmed)
		{
			continue;
		}
		bFound = true;
		BestDistance = Distance;
		const int32 KeptRounds = WindowCover.Sill.Get() == Sill ? WindowCover.GlassRounds : 0;
		Best = WindowCoverRules::FWindowCoverBinding();
		Best.Sill = Sill;
		Best.Floor = Opening.Floor;
		Best.WallId = SlotWallId;
		Best.SideNormal = CoverSlot.WallNormal;
		Best.AlongAxis = AlongAxis;
		Best.IntervalOrigin = Center;
		Best.AlongMinCm = Opening.AlongMinCm;
		Best.AlongMaxCm = Opening.AlongMaxCm;
		Best.SillTopCm = FMath::Max(0.f, static_cast<float>(SillBox.Max.Z) - FeetZ);
		Best.Opening = Opening.Opening;
		Best.bConfirmed = Match.bConfirmed;
		Best.bInsideInterval = Match.bInsideInterval;
		Best.bHasInterval = true;
		Best.GlassRounds = KeptRounds;
		Best.Reject = Match.Reject;
	}

	if (bFound && Best.Sill.IsValid())
	{
		Best.Glass = WindowCoverRules::GlassOver(*Best.Sill.Get());
	}
	const bool bWasConfirmed = WindowCover.bConfirmed;
	WindowCover = bFound ? Best : WindowCoverRules::FWindowCoverBinding();
	if (!bFound)
	{
		WindowCover.Reject = WindowCoverRules::EWindowCoverReject::SolidWall;
	}
	WindowCover.AlongAxis = AlongAxis;
	WindowCover.SpanOrigin = CoverSlot.WallPoint;
	WindowCover.WallSpans = MoveTemp(CollectedSpans);

	// Adjacent window frame edges (window edge = a corner for standing, or crouched where sill allows).
	if (!WindowCover.bConfirmed && Stance != EOperativeStance::Prone && !EdgeOpenings.IsEmpty())
	{
		const WindowCoverRules::FWindowEdgeCorner Edge = WindowCoverRules::FindWindowEdgeCorner(
			0.f, Stance == EOperativeStance::Crouching, EdgeOpenings, EdgeReachCm);
		WindowCover.bLeftEdgeExposed = Edge.bLeftExposed;
		WindowCover.bRightEdgeExposed = Edge.bRightExposed;
		WindowCover.LeftEdgeDistanceCm = Edge.LeftDistanceCm;
		WindowCover.RightEdgeDistanceCm = Edge.RightDistanceCm;
		if (Edge.bLeftExposed && EdgeSills.IsValidIndex(Edge.LeftOpeningIndex))
		{
			WindowCover.LeftEdgeSill = EdgeSills[Edge.LeftOpeningIndex];
			WindowCover.LeftEdgeOpening = EdgeBoxes[Edge.LeftOpeningIndex];
			WindowCover.LeftEdgeGlass = WindowCoverRules::GlassOver(*WindowCover.LeftEdgeSill.Get());
		}
		if (Edge.bRightExposed && EdgeSills.IsValidIndex(Edge.RightOpeningIndex))
		{
			WindowCover.RightEdgeSill = EdgeSills[Edge.RightOpeningIndex];
			WindowCover.RightEdgeOpening = EdgeBoxes[Edge.RightOpeningIndex];
			WindowCover.RightEdgeGlass = WindowCoverRules::GlassOver(*WindowCover.RightEdgeSill.Get());
		}
	}

	WindowCoverSearchedAt = Here;
	WindowCoverWall = CoverSlot.WallComponent;
	WindowCoverSegment = Segment;
	WindowCoverLastEdgeStance = Stance;
	if (WindowCover.bHasInterval)
	{
		const float Along = FVector::DotProduct(Here - WindowCover.IntervalOrigin, WindowCover.AlongAxis);
		WindowCoverLastPlace = WindowCoverRules::PlaceOnInterval(Along, WindowCover.AlongMinCm, WindowCover.AlongMaxCm);
	}
	else
	{
		WindowCoverLastPlace = WindowCoverRules::EWindowIntervalPlace::Outside;
	}
	if (WindowCover.bConfirmed && !bWasConfirmed)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: window %s, floor %d"), *DisplayName.ToString(),
			*GetNameSafe(WindowCover.Sill.Get()), WindowCover.Floor);
	}
	else if (!WindowCover.bConfirmed && bFound && (bForce || bCrossed))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[WindowCover] %s: %s"), *DisplayName.ToString(), WindowCoverRules::RejectName(WindowCover.Reject));
	}
}
