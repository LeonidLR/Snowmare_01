#pragma once

// Shared helpers of the mantle smokes (MantleAlignSmoke, MantleDescentSmoke, MantleClickSmoke; not shipped): a test face of an obstacle
// kind / height on L_Gen_Depot_1, the body's signed distance to an obstacle's footprint, the cursor's ray from the real camera.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/MantleRules.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interactables/MantleNavigation.h"
#include "Misc/Paths.h"
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

	/** One frame of a climb for the contact / penetration measures (world space; ClipSeconds = the montage position, -1 when not climbing). */
	struct FClimbSample
	{
		float ClipSeconds = -1.f;
		FVector Capsule = FVector::ZeroVector;
		FVector HandL = FVector::ZeroVector;
		FVector HandR = FVector::ZeroVector;
		FVector Pelvis = FVector::ZeroVector;
	};

	inline FClimbSample SampleClimb(const AOperativeCharacter& Leader)
	{
		FClimbSample Out;
		const USkeletalMeshComponent* Body = Leader.GetMesh();
		Out.ClipSeconds = Leader.IsMantling() ? Leader.GetMantleClipSeconds() : -1.f;
		Out.Capsule = Leader.GetActorLocation();
		if (Body)
		{
			Out.HandL = Body->GetSocketLocation(TEXT("hand_l"));
			Out.HandR = Body->GetSocketLocation(TEXT("hand_r"));
			Out.Pelvis = Body->GetSocketLocation(TEXT("pelvis"));
		}
		return Out;
	}

	/** The clip's authored motion (clip space: X forward, Z up, cm) every 0.1 s and its analysed hand plant, for the report. */
	inline void DumpClipTrack(AOperativeCharacter& Leader, int32 ClipIndex, const TCHAR* Prefix)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		const FMantleClipTrack* Track = Leader.GetMantleClipTrack(ClipIndex);
		if (!Track || Track->Num() == 0 || !Config.Clips.IsValidIndex(ClipIndex))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[%s] clip %d: no track"), Prefix, ClipIndex);
			return;
		}
		const FString Name = FPaths::GetBaseFilename(Config.Clips[ClipIndex].Path);
		const int32 Every = FMath::Max(1, FMath::RoundToInt(0.1f / Track->SampleSeconds));
		UE_LOG(LogCodexTactics, Display, TEXT("[%s] clip %s track (t root.x root.z | pelvis.x pelvis.z | handL.x handL.z | handR.x handR.z):"), Prefix, *Name);
		for (int32 Index = 0; Index < Track->Num(); Index += Every)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[%s]   %.2f %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f | %6.1f %6.1f"), Prefix, Track->TimeAt(Index), Track->Root[Index].X,
				Track->Root[Index].Z, Track->Pelvis[Index].X, Track->Pelvis[Index].Z, Track->HandL[Index].X, Track->HandL[Index].Z, Track->HandR[Index].X,
				Track->HandR[Index].Z);
		}
		const FMantleClipContact Contact = MantleRules::AnalyseClipTrack(Config, *Track, Config.Clips[ClipIndex].RiseCm);
		UE_LOG(LogCodexTactics, Display, TEXT("[%s] clip %s: plant %s at %.2f s, hand %s, root then %s, root at frame 0 %s, contact start %.1f cm, exit %.2f s"), Prefix, *Name,
			Contact.bValid ? (Contact.bSlide ? TEXT("found (the hands slide: the slowest frame at the ledge)") : TEXT("found")) : TEXT("NOT found"), Contact.ContactSeconds, *Contact.HandPoint.ToCompactString(), *Contact.RootAtContact.ToCompactString(),
			*Contact.RootStart.ToCompactString(), MantleRules::ContactStartCm(Config, Contact), Contact.ExitSeconds);
	}

	/** Where the body was at the climb's hand contact (forward = from the near face plane along the climb, up = over the top), cm. */
	struct FContactMeasure
	{
		bool bValid = false;
		/** The contact time came from the climb's own alignment window / the clip's analysed plant (else: the closest hand frame). */
		bool bFromClip = false;
		float Seconds = 0.f;
		float HandFwdCm = 0.f;
		float HandUpCm = 0.f;
		/** The nearer hand's distance to the aimed grip (the lip + align.grip_forward_cm / grip_up_cm). */
		float HandGapCm = TNumericLimits<float>::Max();
		float PelvisFwdCm = 0.f;
		float PelvisUpCm = 0.f;
		/** The capsule's distance in front of the face when the climb started (> 0 = in front). */
		float StartFaceCm = 0.f;
		/** Before the contact: the farthest the pelvis / the capsule's centre came past the near face plane (> 0 = through it). */
		float MaxPelvisFwdBeforeCm = -TNumericLimits<float>::Max();
		float MaxCapsuleFwdBeforeCm = -TNumericLimits<float>::Max();
	};

	/** The contact measure of one climb's samples: at ContactSeconds of the clip (< 0: the frame whose nearer hand is closest to the grip). */
	inline FContactMeasure MeasureContact(const TArray<FClimbSample>& Samples, const FVector& FacePoint, const FVector& Dir, float TopZ, float ContactSeconds)
	{
		const FMantleConfig& Config = MantleRules::GetConfig();
		FContactMeasure Out;
		auto Fwd = [&](const FVector& Point) { return static_cast<float>(FVector::DotProduct(Point - FacePoint, Dir)); };
		auto Gap = [&](const FVector& Hand)
		{
			return FMath::Sqrt(FMath::Square(Fwd(Hand) - Config.AlignGripForwardCm) + FMath::Square(static_cast<float>(Hand.Z) - TopZ - Config.AlignGripUpCm));
		};
		int32 At = INDEX_NONE;
		bool bStarted = false;
		for (int32 Index = 0; Index < Samples.Num(); ++Index)
		{
			const FClimbSample& S = Samples[Index];
			if (S.ClipSeconds < 0.f)
			{
				continue;
			}
			if (!bStarted)
			{
				bStarted = true;
				Out.StartFaceCm = -Fwd(S.Capsule);
			}
			if (ContactSeconds >= 0.f)
			{
				if (S.ClipSeconds + KINDA_SMALL_NUMBER < ContactSeconds)
				{
					Out.MaxPelvisFwdBeforeCm = FMath::Max(Out.MaxPelvisFwdBeforeCm, Fwd(S.Pelvis));
					Out.MaxCapsuleFwdBeforeCm = FMath::Max(Out.MaxCapsuleFwdBeforeCm, Fwd(S.Capsule));
				}
				else if (At == INDEX_NONE)
				{
					At = Index;
				}
			}
			else if (FMath::Min(Gap(S.HandL), Gap(S.HandR)) < Out.HandGapCm)
			{
				Out.HandGapCm = FMath::Min(Gap(S.HandL), Gap(S.HandR));
				At = Index;
			}
		}
		if (At == INDEX_NONE)
		{
			return Out;
		}
		const FClimbSample& S = Samples[At];
		const FVector& Hand = Gap(S.HandL) <= Gap(S.HandR) ? S.HandL : S.HandR;
		Out.bValid = true;
		Out.bFromClip = ContactSeconds >= 0.f;
		Out.Seconds = S.ClipSeconds;
		Out.HandGapCm = Gap(Hand);
		Out.HandFwdCm = Fwd(Hand);
		Out.HandUpCm = static_cast<float>(Hand.Z) - TopZ;
		Out.PelvisFwdCm = Fwd(S.Pelvis);
		Out.PelvisUpCm = static_cast<float>(S.Pelvis.Z) - TopZ;
		return Out;
	}

	/** The clip time of a climb's hand contact: the alignment's first window (if it had one), else the clip's analysed plant; -1 = none. */
	inline float ClimbContactSeconds(AOperativeCharacter& Leader)
	{
		if (Leader.GetLastMantleContactSeconds() > 0.f)
		{
			return Leader.GetLastMantleContactSeconds();
		}
		const FMantleConfig& Config = MantleRules::GetConfig();
		const int32 ClipIndex = Leader.GetLastMantleClipIndex();
		const FMantleClipTrack* Track = Config.Clips.IsValidIndex(ClipIndex) ? Leader.GetMantleClipTrack(ClipIndex) : nullptr;
		const FMantleClipContact Contact = Track ? MantleRules::AnalyseClipTrack(Config, *Track, Config.Clips[ClipIndex].RiseCm) : FMantleClipContact();
		return Contact.bValid ? Contact.ContactSeconds : -1.f;
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
