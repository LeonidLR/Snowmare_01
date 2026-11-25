// G016 window cover. Pure matching plus the shot line that steps past glass HideBrokenWindow already hid.
#include "Tactics/WindowCoverRules.h"

#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Interactables/WindowBreakage.h"

namespace WindowCoverRules
{
	namespace
	{
		constexpr float ParallelNormalDot = 0.95f;
		const FName GlassTag(TEXT("window_glass"));
		const FName BrokenTag(TEXT("window_broken"));
		const FName KitWindowTag(TEXT("kit_part=window"));

		bool IsGlassActorOver(const AActor* Actor, const FBox& SillBox)
		{
			return Actor && Actor->ActorHasTag(GlassTag) && SillBox.IsValid
				&& WindowBreakage::IsOverSill(SillBox, Actor->GetComponentsBoundingBox(true).GetCenter());
		}

		bool IsShrunkKitInstance(const FHitResult& Hit)
		{
			const UInstancedStaticMeshComponent* Mesh = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
			if (!Mesh || !Mesh->ComponentTags.Contains(KitWindowTag) || Hit.Item < 0)
			{
				return false;
			}
			FTransform Instance;
			return Mesh->GetInstanceTransform(Hit.Item, Instance, /*bWorldSpace*/ true) && Instance.GetScale3D().GetMin() < 0.01f;
		}

		bool IsIntactKitInstance(const FHitResult& Hit, const FBox& SillBox)
		{
			UInstancedStaticMeshComponent* Mesh = Cast<UInstancedStaticMeshComponent>(Hit.GetComponent());
			if (!Mesh || !Mesh->ComponentTags.Contains(KitWindowTag) || Hit.Item < 0 || IsShrunkKitInstance(Hit))
			{
				return false;
			}
			FTransform Instance;
			if (!Mesh->GetInstanceTransform(Hit.Item, Instance, /*bWorldSpace*/ true))
			{
				return false;
			}
			const FBox Bounds = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetBounds().GetBox().TransformBy(Instance) : FBox(Instance.GetLocation(), Instance.GetLocation());
			return WindowBreakage::IsOverSill(SillBox, Bounds.GetCenter());
		}

		/** Enter and exit of the segment on the box, as fractions of From->To. False when the segment misses. */
		bool SegmentCrossesBox(const FBox& Box, const FVector& From, const FVector& To, float& OutEnter, float& OutExit)
		{
			const FVector Delta = To - From;
			float Enter = 0.f;
			float Exit = 1.f;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const double Origin = From[Axis];
				const double Direction = Delta[Axis];
				const double MinB = Box.Min[Axis];
				const double MaxB = Box.Max[Axis];
				if (FMath::IsNearlyZero(Direction))
				{
					if (Origin < MinB || Origin > MaxB)
					{
						return false;
					}
					continue;
				}
				double Near = (MinB - Origin) / Direction;
				double Far = (MaxB - Origin) / Direction;
				if (Near > Far)
				{
					Swap(Near, Far);
				}
				Enter = FMath::Max(Enter, static_cast<float>(Near));
				Exit = FMath::Min(Exit, static_cast<float>(Far));
				if (Enter > Exit)
				{
					return false;
				}
			}
			if (Exit < 0.f || Enter > 1.f)
			{
				return false;
			}
			OutEnter = FMath::Clamp(Enter, 0.f, 1.f);
			OutExit = FMath::Clamp(Exit, 0.f, 1.f);
			return true;
		}
	}

	EWindowGlassState GlassOver(const AActor& Sill)
	{
		if (Sill.ActorHasTag(BrokenTag))
		{
			return EWindowGlassState::Broken;
		}
		UWorld* World = Sill.GetWorld();
		const FBox SillBox = Sill.GetComponentsBoundingBox(true);
		if (!World || !SillBox.IsValid)
		{
			return EWindowGlassState::Missing;
		}
		// The same pieces HideBrokenWindow hides. A window_glass actor ignores visibility, so it is found by the tag.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor != &Sill && IsGlassActorOver(Actor, SillBox) && !Actor->IsHidden())
			{
				return EWindowGlassState::Intact;
			}
			TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Actor);
			for (UInstancedStaticMeshComponent* Component : Components)
			{
				if (!Component->ComponentTags.Contains(KitWindowTag))
				{
					continue;
				}
				for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
				{
					FTransform Instance;
					if (!Component->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true) || Instance.GetScale3D().GetMin() < 0.01)
					{
						continue;
					}
					const FBox Bounds = Component->GetStaticMesh() ? Component->GetStaticMesh()->GetBounds().GetBox().TransformBy(Instance) : FBox(Instance.GetLocation(), Instance.GetLocation());
					if (WindowBreakage::IsOverSill(SillBox, Bounds.GetCenter()))
					{
						return EWindowGlassState::Intact;
					}
				}
			}
		}
		return EWindowGlassState::Missing;
	}

	bool SameWallSide(const FVector& SlotNormal, const FVector& OpeningNormal)
	{
		const FVector Slot = SlotNormal.GetSafeNormal2D();
		const FVector Opening = OpeningNormal.GetSafeNormal2D();
		if (Slot.IsNearlyZero() || Opening.IsNearlyZero())
		{
			return false;
		}
		return FMath::Abs(FVector::DotProduct(Slot, Opening)) >= ParallelNormalDot;
	}

	bool SameWall(uint32 SlotWallId, uint32 OpeningWallId, const FVector& SlotNormal, const FVector& OpeningNormal,
		const FString& SlotBuilding, const FString& OpeningBuilding)
	{
		if (!SlotBuilding.IsEmpty() && !OpeningBuilding.IsEmpty() && SlotBuilding != OpeningBuilding)
		{
			return false;
		}
		if (SlotWallId != 0 && OpeningWallId != 0 && SlotWallId != OpeningWallId)
		{
			return false;
		}
		return SameWallSide(SlotNormal, OpeningNormal);
	}

	FWindowCoverMatch Match(const FWindowCoverOpening& Opening, const FWindowCoverSlotFacts& Slot)
	{
		FWindowCoverMatch Out;
		if (!Opening.bPresent || !Opening.Opening.IsValid)
		{
			Out.Reject = EWindowCoverReject::SolidWall;
			return Out;
		}
		if (Opening.Floor != INDEX_NONE && Slot.Floor != INDEX_NONE && Opening.Floor != Slot.Floor)
		{
			Out.Reject = EWindowCoverReject::OtherFloor;
			return Out;
		}
		if (!SameWall(Slot.WallId, Opening.WallId, Slot.WallNormal, Opening.WallNormal, Slot.BuildingId, Opening.BuildingId))
		{
			Out.Reject = EWindowCoverReject::OtherWall;
			return Out;
		}
		if (Slot.AlongCm < Opening.AlongMinCm || Slot.AlongCm > Opening.AlongMaxCm)
		{
			Out.Reject = EWindowCoverReject::OutsideInterval;
			return Out;
		}
		Out.bInsideInterval = true;
		if (!Opening.Opening.IsInsideOrOn(Slot.FirePoint))
		{
			Out.Reject = EWindowCoverReject::FirePointOutside;
			return Out;
		}
		if (Slot.bAtCorner)
		{
			Out.Reject = EWindowCoverReject::CornerFallback;
			return Out;
		}
		Out.bConfirmed = true;
		Out.Reject = EWindowCoverReject::None;
		return Out;
	}

	EWindowIntervalPlace PlaceOnInterval(float AlongCm, float MinCm, float MaxCm)
	{
		return AlongCm >= MinCm && AlongCm <= MaxCm ? EWindowIntervalPlace::Inside : EWindowIntervalPlace::Outside;
	}

	bool CrossedOpeningBoundary(EWindowIntervalPlace Was, EWindowIntervalPlace Now)
	{
		return Was != Now;
	}

	bool WantsCrouch(bool bConfirmed, bool bShimmying, bool bAtCorner, bool bStopped)
	{
		return bConfirmed && bStopped && !bShimmying && !bAtCorner;
	}

	EWindowCoverTurn TurnForAim(const FVector& From, const FVector& Aim, const FVector& SlotNormal)
	{
		return FVector::DotProduct(Aim - From, SlotNormal) >= 0.f ? EWindowCoverTurn::Outward : EWindowCoverTurn::Inward;
	}

	float AimYaw(const FVector& From, const FVector& Aim)
	{
		const FVector Flat = (Aim - From).GetSafeNormal2D();
		return Flat.IsNearlyZero() ? 0.f : static_cast<float>(Flat.Rotation().Yaw);
	}

	FVector FirePointOnPlane(const FVector& Muzzle, const FVector& SlotNormal, const FVector& WallPoint)
	{
		const FVector Normal = SlotNormal.GetSafeNormal2D();
		const float Denom = FVector::DotProduct(-Normal, Normal);
		if (Normal.IsNearlyZero() || FMath::IsNearlyZero(Denom))
		{
			return WallPoint;
		}
		const float T = FVector::DotProduct(WallPoint - Muzzle, Normal) / Denom;
		return Muzzle + (-Normal) * T;
	}

	float PlaneLimitCm(const FVector& BoundsExtent, const FVector& SlotNormal)
	{
		const float HalfThick = FMath::Abs(FVector::DotProduct(BoundsExtent, SlotNormal.GetSafeNormal2D()));
		return FMath::Min(HalfThick, WindowBreakage::FaceMarginCm) + WindowBreakage::OpeningPadCm;
	}

	bool OnSameWallPlane(const FVector& Point, const FVector& WallPoint, const FVector& SlotNormal, float LimitCm)
	{
		const FVector Normal = SlotNormal.GetSafeNormal2D();
		if (Normal.IsNearlyZero())
		{
			return false;
		}
		return FMath::Abs(FVector::DotProduct(Point - WallPoint, Normal)) <= LimitCm;
	}

	bool IsNeighborSill(const FVector& SillCenter, const FVector& SlotWallPoint)
	{
		return FVector::Dist2D(SillCenter, SlotWallPoint) <= WindowBreakage::ThroughLandingCm;
	}

	bool SharesFloor(int32 SlotFloor, int32 SillFloor, float SlotZ, float SillZ)
	{
		if (SlotFloor != INDEX_NONE && SillFloor != INDEX_NONE)
		{
			return SlotFloor == SillFloor;
		}
		return FMath::Abs(SlotZ - SillZ) <= WindowBreakage::ThroughLandingCm;
	}

	FWindowShotResult DecideShot(const FWindowShotQuery& Query)
	{
		FWindowShotResult Out;
		if (!Query.bIsShot || !Query.bConfirmed)
		{
			return Out;
		}
		if (Query.bHitsFrameOrSolid || !Query.bThroughOpening)
		{
			Out.bBlocked = true;
			return Out;
		}
		if (Query.bBreakable && Query.Glass == EWindowGlassState::Intact && Query.GlassRoundsSpent == 0)
		{
			Out.bSpendRound = true;
			Out.bBreakGlass = true;
			return Out;
		}
		Out.bSpendRound = true;
		Out.bHitTarget = true;
		return Out;
	}

	bool TakesWindowPose(const FWindowCoverFighter& Fighter)
	{
		return Fighter.bOwnSlotConfirmed;
	}

	bool GrantsPassage()
	{
		return false;
	}

	const TCHAR* RejectName(EWindowCoverReject Reject)
	{
		switch (Reject)
		{
		case EWindowCoverReject::None: return TEXT("none");
		case EWindowCoverReject::OtherFloor: return TEXT("other floor");
		case EWindowCoverReject::OtherWall: return TEXT("other wall");
		case EWindowCoverReject::OutsideInterval: return TEXT("outside the opening");
		case EWindowCoverReject::FirePointOutside: return TEXT("fire point outside the opening");
		case EWindowCoverReject::SolidWall: return TEXT("solid wall");
		case EWindowCoverReject::CornerFallback: return TEXT("corner");
		}
		return TEXT("?");
	}

	FWindowLineView TraceThroughWindow(UWorld& World, const AActor* Shooter, const FWindowCoverBinding& Binding,
		const FVector& From, const FVector& To, const FCollisionQueryParams& InParams)
	{
		FWindowLineView View;
		FCollisionQueryParams Params = InParams;
		if (Shooter)
		{
			Params.AddIgnoredActor(Shooter);
		}
		const AActor* Sill = Binding.Sill.Get();
		const FBox SillBox = Sill ? Sill->GetComponentsBoundingBox(true) : FBox(ForceInit);
		const bool bBroken = Binding.GlassRounds > 0 || (Sill && Sill->ActorHasTag(BrokenTag));
		FVector Start = From;
		// A hidden pane is ignored. A shrunk kit instance is stepped past by the opening's own bottom pad (HideBrokenWindow
		// leaves the collision; the pad is the existing window tolerance, not a new margin).
		for (int32 Step = 0; Step < 4; ++Step)
		{
			FHitResult Hit;
			const bool bHit = World.LineTraceSingleByChannel(Hit, Start, To, ECC_Visibility, Params);
			View.bBlocking = bHit;
			View.Actor = Hit.GetActor();
			View.ImpactPoint = bHit ? Hit.ImpactPoint : To;
			View.Distance = bHit ? Hit.Distance : static_cast<float>(FVector::Dist(Start, To));
			View.bIntactGlass = false;
			if (!bHit)
			{
				break;
			}
			AActor* Blocker = Hit.GetActor();
			const bool bHiddenGlass = IsGlassActorOver(Blocker, SillBox) && Blocker->IsHidden();
			const bool bShrunk = IsShrunkKitInstance(Hit);
			const bool bIntactActor = !bBroken && IsGlassActorOver(Blocker, SillBox) && !Blocker->IsHidden();
			const bool bIntactKit = !bBroken && IsIntactKitInstance(Hit, SillBox);
			if (bHiddenGlass && Blocker)
			{
				Params.AddIgnoredActor(Blocker);
				continue;
			}
			if (bShrunk || (bBroken && (IsGlassActorOver(Blocker, SillBox) || IsIntactKitInstance(Hit, SillBox) || bShrunk)))
			{
				const FVector Dir = (To - From).GetSafeNormal();
				const FVector Restart = Hit.ImpactPoint + Dir * WindowBreakage::OpeningBottomCm;
				if (Dir.IsNearlyZero() || FVector::DistSquared(Restart, To) >= FVector::DistSquared(Hit.ImpactPoint, To))
				{
					break;
				}
				Start = Restart;
				continue;
			}
			if (bIntactActor || bIntactKit)
			{
				// The pane can sit just in front of the opening box. The shot counts when the whole line crosses that box.
				const FVector Delta = To - From;
				View.bIntactGlass = !Delta.IsNearlyZero() && Binding.Opening.IsValid
					&& FMath::LineBoxIntersection(Binding.Opening, From, To, Delta);
			}
			break;
		}
		// Glazed panes ignore visibility (build_levelgen_map COLLISION_VISUAL_GLASS). A line that clears the far face of
		// the opening still spends the round on that glass. A solid hit before the far face is the frame and stays in the way.
		if (!View.bIntactGlass && !bBroken && Binding.Glass == EWindowGlassState::Intact && Binding.Opening.IsValid)
		{
			float Enter = 0.f;
			float Exit = 0.f;
			if (SegmentCrossesBox(Binding.Opening, From, To, Enter, Exit))
			{
				const float Length = static_cast<float>(FVector::Dist(From, To));
				const float ExitCm = Exit * Length;
				const float HitCm = View.bBlocking ? static_cast<float>(FVector::Dist(From, View.ImpactPoint)) : Length + WindowBreakage::OpeningBottomCm;
				if (HitCm > ExitCm)
				{
					View.bIntactGlass = true;
					View.ImpactPoint = FMath::Lerp(From, To, (Enter + Exit) * 0.5f);
				}
			}
		}
		return View;
	}
}
