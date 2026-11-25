// AOperativeCharacter: fire through a window from cover (G016). The opening tolerances are WindowBreakage's.
// Passage through the pane stays in WindowOrderRules; this file never writes bWindowPathAllowed.
#include "Characters/OperativeCharacter.h"

#include "Camera/RoofCutawaySubsystem.h"
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
}

bool AOperativeCharacter::IsWindowCoverShot(const FVector& AimAt) const
{
	if (!IsWindowCoverConfirmed() || !WindowCover.Opening.IsValid)
	{
		return false;
	}
	const FVector From = GetMuzzleLocation();
	const FVector Delta = AimAt - From;
	return !Delta.IsNearlyZero() && FMath::LineBoxIntersection(WindowCover.Opening, From, AimAt, Delta);
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
	return IsWindowCoverConfirmed() && !bShimmying && !bAtCoverCorner && TryGetWindowCoverAimPoint(Aim) && IsWindowCoverShot(Aim);
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
	const bool bWant = WindowCoverRules::WantsCrouch(WindowCover.bConfirmed, bShimmying, bAtCoverCorner, IsWindowCoverStopped());
	const bool bLocked = Stance == EOperativeStance::Prone || (ColdSurvival && ColdSurvival->IsFrostbitten());
	if (bWant)
	{
		if (!bLocked && Stance != EOperativeStance::Crouching)
		{
			SetStance(EOperativeStance::Crouching);
			bWindowCoverForcedCrouch = CurrentCoverHeight == ECoverHeight::HighCover;
			UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: crouched at the window"), *DisplayName.ToString());
		}
		return;
	}
	if (bWindowCoverForcedCrouch)
	{
		if (!bLocked && CurrentCoverHeight == ECoverHeight::HighCover && Stance == EOperativeStance::Crouching)
		{
			SetStance(EOperativeStance::Standing);
		}
		bWindowCoverForcedCrouch = false;
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
	++WindowCover.GlassRounds;
	UE_LOG(LogCodexTactics, Display, TEXT("[WindowCover] %s: the round is spent on %s, the target is not hit"), *DisplayName.ToString(), *Sill->GetName());
}

void AOperativeCharacter::ResolveWindowGlassRound(const FVector& AimAt)
{
	// The flag is this shot's result, not a latch. A leftover true from the glass cartridge would swallow the next round.
	bWindowGlassShot = false;
	if (!IsWindowCoverConfirmed() || !IsWindowCoverShot(AimAt))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WindowCoverGlass), false, this);
	const WindowCoverRules::FWindowLineView View = WindowCoverRules::TraceThroughWindow(*World, this, WindowCover, GetCoverFireOrigin(), AimAt, Params);
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
	if (!bForce && !bLostSill && !bCrossed && !bWallChanged && !bFloorChanged && !bNewSegment && (bSameSpot || bShimmying))
	{
		WindowCoverLastPlace = Place;
		return;
	}

	FString& SlotBuilding = SlotBuildingNow;
	const int32 SlotFloor = SlotFloorNow;
	const uint32 SlotWallId = CoverSlot.WallComponent.IsValid() ? CoverSlot.WallComponent->GetUniqueID() : 0;
	const FVector AlongAxis = FVector::CrossProduct(FVector::UpVector, CoverSlot.WallNormal).GetSafeNormal();
	static const FName SillTag(TEXT("window_sill"));

	WindowCoverRules::FWindowCoverBinding Best;
	Best.Reject = WindowCoverRules::EWindowCoverReject::SolidWall;
	bool bFound = false;
	float BestDistance = TNumericLimits<float>::Max();

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
		if (!WindowCoverRules::IsNeighborSill(Center, CoverSlot.WallPoint))
		{
			continue;
		}
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

		WindowCoverRules::FWindowCoverOpening Opening;
		Opening.bPresent = true;
		Opening.Opening = WindowBreakage::MakeOpeningBox(SillBox);
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
		Facts.bAtCorner = bAtCoverCorner;
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
	WindowCoverSearchedAt = Here;
	WindowCoverWall = CoverSlot.WallComponent;
	WindowCoverSegment = Segment;
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
