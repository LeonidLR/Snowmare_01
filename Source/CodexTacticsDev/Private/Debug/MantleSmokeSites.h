#pragma once

// Shared helpers of the mantle smokes (MantleAlignSmoke, MantleDescentSmoke, MantleClickSmoke; not shipped): a test face of an obstacle
// kind / height on L_Gen_Depot_1, the body's signed distance to an obstacle's footprint, the cursor's ray from the real camera.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interactables/MantleNavigation.h"
#include "NavigationSystem.h"

namespace MantleSmokeSites
{
	/** A climbable face found for a smoke: where he stands (feet, StandOffCm out), the climb's direction and the probe from there. */
	struct FSite
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Feet = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FMantleSite Probe;
		bool bFound = false;
	};

	inline float TopOf(const AActor& Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor.GetActorBounds(false, Origin, Extent);
		return static_cast<float>(Origin.Z + Extent.Z);
	}

	/**
	 * A face of an obstacle of Kind (+-6 cm of HeightCm) with free navmesh ground StandOffCm out where the probe says go; nearest to Near.
	 * bNeedFarGround: the opposite face must have free navmesh ground FarOffCm out too (the descent cases).
	 */
	inline FSite FindSite(UWorld* World, AOperativeCharacter* Leader, const TCHAR* Kind, float HeightCm, float StandOffCm, const FVector& Near,
		const TSet<const AActor*>& Used, bool bNeedFarGround = false, float FarOffCm = 0.f, bool bRequireGo = true)
	{
		FSite Best;
		float BestDist = TNumericLimits<float>::Max();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const float Radius = Leader->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FMantleConfig& Config = MantleRules::GetConfig();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MantleSmokeSites), false, Leader);
		const FName KindName(Kind);
		auto FreeGround = [&](const FVector& Spot, float Bottom)
		{
			FNavLocation OnNav;
			return Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(50.f, 50.f, 100.f)) && FMath::Abs(OnNav.Location.Z - Bottom) <= 30.f
				&& !World->OverlapAnyTestByChannel(Spot + FVector(0.f, 0.f, HalfHeight + 6.f), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
		};
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(KindName) || Used.Contains(*It))
			{
				continue;
			}
			const FBox Local = It->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FTransform Transform = It->GetActorTransform();
			const float Bottom = static_cast<float>(Transform.TransformPosition(FVector(Local.GetCenter().X, Local.GetCenter().Y, Local.Min.Z)).Z);
			if (FMath::Abs(TopOf(**It) - Bottom - HeightCm) > 6.f)
			{
				continue;
			}
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const int32 Axis = Face % 2;
				const float Sign = Face < 2 ? 1.f : -1.f;
				FVector LocalFace = Local.GetCenter();
				LocalFace[Axis] = Sign > 0.f ? Local.Max[Axis] : Local.Min[Axis];
				FVector LocalNormal = FVector::ZeroVector;
				LocalNormal[Axis] = Sign;
				const int32 TangentAxis = 1 - Axis;
				if ((Local.Max[TangentAxis] - Local.Min[TangentAxis]) * Transform.GetScale3D().GetAbs()[TangentAxis] < 2.f * Radius + 20.f)
				{
					continue; // an end face: no square climb there
				}
				const FVector Normal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal2D();
				FVector FaceWorld = Transform.TransformPosition(LocalFace);
				FaceWorld.Z = Bottom;
				const FVector Spot = FaceWorld + Normal * StandOffCm;
				const float Dist = static_cast<float>(FVector::Dist2D(Spot, Near));
				if (Dist >= BestDist || !FreeGround(Spot, Bottom))
				{
					continue;
				}
				if (bNeedFarGround)
				{
					FVector FarLocal = Local.GetCenter();
					FarLocal[Axis] = Sign > 0.f ? Local.Min[Axis] : Local.Max[Axis];
					FVector FarFace = Transform.TransformPosition(FarLocal);
					FarFace.Z = Bottom;
					if (!FreeGround(FarFace - Normal * FarOffCm, Bottom))
					{
						continue;
					}
				}
				FMantleSite Probe;
				const bool bProbed = MantleNavigation::ProbeSite(*World, Spot, -Normal, StandOffCm + 100.f, Radius, HalfHeight, Params, Config, Probe)
					&& Probe.Obstacle.Get() == *It;
				if (bRequireGo && (!bProbed || !MantleRules::Decide(Config, Probe.Probe).IsGo()))
				{
					continue;
				}
				if (!bProbed)
				{
					// Not climbable (a wall): the face as measured from the box (the negative cases aim at it).
					Probe = FMantleSite();
					Probe.Obstacle = *It;
					Probe.Feet = Spot;
					Probe.FacePoint = FaceWorld;
					Probe.Normal = Normal;
					Probe.Dir = -Normal;
					Probe.TopZ = TopOf(**It);
					Probe.EdgeTop = FVector(FaceWorld.X, FaceWorld.Y, Probe.TopZ);
					Probe.Landing = Probe.EdgeTop;
					Probe.Probe.HeightCm = Probe.TopZ - Bottom;
				}
				BestDist = Dist;
				Best.Actor = *It;
				Best.Feet = Spot;
				Best.Dir = -Normal;
				Best.Probe = Probe;
				Best.bFound = true;
			}
		}
		return Best;
	}

	/**
	 * The signed horizontal distance of Point from Actor's box footprint (world cm through its scale): > 0 outside, < 0 inside (the depth).
	 * Points above TopZ - AboveMarginCm do not count (the body over the top is no clipping): returns +infinity for them.
	 */
	inline float FootprintGap(const AActor& Actor, const FVector& Point, float AboveMarginCm)
	{
		if (Point.Z > TopOf(Actor) - AboveMarginCm)
		{
			return TNumericLimits<float>::Max();
		}
		const FBox Local = Actor.CalculateComponentsBoundingBoxInLocalSpace(false);
		if (!Local.IsValid)
		{
			return TNumericLimits<float>::Max();
		}
		const FTransform Transform = Actor.GetActorTransform();
		const FVector Scale = Transform.GetScale3D().GetAbs();
		const FVector P = Transform.InverseTransformPosition(Point) - Local.GetCenter();
		const FVector Extent = Local.GetExtent();
		const float Dx = static_cast<float>((FMath::Abs(P.X) - Extent.X) * Scale.X);
		const float Dy = static_cast<float>((FMath::Abs(P.Y) - Extent.Y) * Scale.Y);
		if (Dx > 0.f || Dy > 0.f)
		{
			return FMath::Sqrt(FMath::Square(FMath::Max(Dx, 0.f)) + FMath::Square(FMath::Max(Dy, 0.f)));
		}
		return FMath::Max(Dx, Dy);
	}

	/** The body joints whose clipping counts (the torso and head; hands / feet may touch the edge / the wall). */
	inline TArray<FVector> TorsoJoints(const USkeletalMeshComponent& Body)
	{
		TArray<FVector> Out;
		for (const TCHAR* Bone : { TEXT("pelvis"), TEXT("spine_03"), TEXT("head") })
		{
			if (Body.GetBoneIndex(FName(Bone)) != INDEX_NONE)
			{
				Out.Add(Body.GetSocketLocation(FName(Bone)));
			}
		}
		return Out;
	}

	/** The cursor's ray: from the player's camera through Point, on ECC_Visibility (GetHitResultUnderCursor's channel, simple collision). */
	inline bool CameraRayHit(UWorld* World, const FVector& Point, FHitResult& OutHit, FVector* OutEye = nullptr)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (!PC || !PC->PlayerCameraManager)
		{
			return false;
		}
		const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
		if (OutEye)
		{
			*OutEye = Eye;
		}
		const FVector Direction = (Point - Eye).GetSafeNormal();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ClickableTrace), false);
		return World->LineTraceSingleByChannel(OutHit, Eye, Point + Direction * 300.f, ECC_Visibility, Params) && OutHit.bBlockingHit;
	}
}

#endif // !UE_BUILD_SHIPPING
