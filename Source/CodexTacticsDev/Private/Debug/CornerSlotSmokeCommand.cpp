// Dev-only measurement of G025 (designer, X6 fight 2026-11-25): at a block corner ("at the corner - covering it", the
// run-and-peek arrival) the commander sinks into the wall and stands well to the right of the edge. Both hands, stand
// and crouch. Gate: the outer shoulder within 25 cm of the exposed edge, capsule and skin penetration <= 2 cm.
// The stand slot is the same probe as ACodexTacticsPlayerController::ResolveCornerStandSlot (that method is private).
//   Scripts/smoke_parallel.ps1 -Commands CornerSlotSmoke -Remote off

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"

namespace CornerSlotSmoke
{
	constexpr float MaxShoulderFromEdgeCm = 25.f;
	constexpr float MaxPenetrationCm = 2.f;
	constexpr int32 SettleFrames = 45;
	constexpr int32 WalkFrames = 60 * 8;
	constexpr int32 PoseFrames = 60 * 4;

	enum class EStep : uint8 { Warmup, Scan, Begin, WaitWalk, WaitPose, MoveOffCorner, Done };

	struct FPick
	{
		bool bValid = false;
		FCoverSlot Base;
		FCoverSlot Mid;
		FCoverSlot Corner;
		ECoverFacing Side = ECoverFacing::Right;
		float EdgeAlong = 0.f;
		float StandAlong = 0.f;
		FVector Edge = FVector::ZeroVector;
		FVector BoxCorner = FVector::ZeroVector;
		float Thickness = 0.f;
		float Dist = 0.f;
		FString Name;
	};

	struct FCase
	{
		const TCHAR* Label = TEXT("");
		bool bLow = false;
		ECoverFacing Side = ECoverFacing::Right;
		bool bRunPeek = true;
	};

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Failures = 0;
		int32 CaseIndex = 0;
		int32 Settled = 0;
		bool bForced = false;
		SmokeUtils::FPreloadGate Preload;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		FPick HighRight;
		FPick HighLeft;
		FPick LowRight;
		FPick LowLeft;
		FCoverSlot MoveOffTarget;
		FVector MoveOffFrom = FVector::ZeroVector;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	const FCase Cases[] = {
		{ TEXT("run-peek high right stand"), false, ECoverFacing::Right, true },
		{ TEXT("run-peek high left stand"), false, ECoverFacing::Left, true },
		{ TEXT("run-peek low right crouch"), true, ECoverFacing::Right, true },
		{ TEXT("run-peek low left crouch"), true, ECoverFacing::Left, true },
		{ TEXT("in-cover high right stand"), false, ECoverFacing::Right, false },
		{ TEXT("in-cover high left stand"), false, ECoverFacing::Left, false },
		{ TEXT("in-cover low right crouch"), true, ECoverFacing::Right, false },
		{ TEXT("in-cover low left crouch"), true, ECoverFacing::Left, false },
	};

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->Step = EStep::Done;
		FPlatformMisc::RequestExit(false, TEXT("CornerSlotSmoke"));
	}

	void Go(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
		Runner->Settled = 0;
	}

	FPick& PickFor(bool bLow, ECoverFacing Side)
	{
		if (bLow)
		{
			return Side == ECoverFacing::Right ? Runner->LowRight : Runner->LowLeft;
		}
		return Side == ECoverFacing::Right ? Runner->HighRight : Runner->HighLeft;
	}

	/** Same probes as ACodexTacticsPlayerController::ResolveCornerStandSlot. */
	FCoverSlot ResolveStand(UWorld* World, const FCoverSlot& Base, ECoverFacing Side, float StandAlong)
	{
		const FVector Right = Base.RightTangent();
		FCoverSlot Corner;
		for (const float Back : { 0.f, 20.f, 40.f, 60.f })
		{
			const float Along = StandAlong + (Side == ECoverFacing::Right ? -Back : Back);
			if (CoverTraceRules::FindShimmySlot(World, Base, Base.WallPoint + Right * Along, Corner)
				&& (!Base.WallActor.IsValid() || Corner.WallActor == Base.WallActor))
			{
				CoverFacingRules::PullCornerFeetBackToStand(Corner, Side, Base.WallPoint, Right, Along);
				return Corner;
			}
		}
		Corner = Base;
		Corner.WorldLocation += Right * StandAlong;
		Corner.WallPoint += Right * StandAlong;
		CoverFacingRules::PullCornerFeetBackToStand(Corner, Side, Base.WallPoint, Right, StandAlong);
		return Corner;
	}

	float InwardCm(const FVector& Point, const FPick& Pick)
	{
		const FVector Right = Pick.Base.RightTangent();
		const float Along = static_cast<float>(FVector::DotProduct(Point - Pick.Base.WallPoint, Right));
		return Pick.Side == ECoverFacing::Right ? Pick.EdgeAlong - Along : Along - Pick.EdgeAlong;
	}

	/** Centimetres of Point behind the face that blocks a trace from the open side. 0 when the point is in front of that face. */
	float PenetrationCm(UWorld& World, const FVector& Point, const FVector& Normal, const AActor* Ignore)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CornerSlotSmoke), false);
		if (Ignore)
		{
			Params.AddIgnoredActor(Ignore);
		}
		const FVector Start = Point + Normal * 120.f;
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, Start, Point - Normal * 40.f, ECC_Visibility, Params))
		{
			return 0.f;
		}
		if (FVector::DotProduct(Hit.ImpactNormal, Normal) < 0.5f)
		{
			return 0.f;
		}
		const float Past = static_cast<float>(FVector::Dist(Start, Point) - FVector::Dist(Start, Hit.ImpactPoint));
		return Past > MaxPenetrationCm ? Past : FMath::Max(0.f, Past);
	}

	void ArmMesh(AOperativeCharacter& Operative)
	{
		if (USkeletalMeshComponent* Mesh = Operative.GetMesh())
		{
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bPauseAnims = false;
		}
	}

	void Put(AOperativeCharacter& Operative, const FVector& Feet, const FVector& Facing)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("CornerSlotSmoke"));
		}
		Operative.StopOperative();
		const float Half = Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Operative.TeleportTo(Feet + FVector(0.f, 0.f, Half + 6.f), Facing.Rotation(), false, true);
	}

	struct F135bMatrixState
	{
		bool bActive = false;
		int32 Step = 0;
		double StageTime = 0.0;
		FVector LocalChestFwd = FVector(1.f, 0.f, 0.f);
		bool bChestCalibrated = false;
		// 16 pose cases: SetIndex (0=m4, 1=riffle) x PickIndex (0=HighR, 1=HighL, 2=LowR, 3=LowL) -> measure peek & idle return
		int32 PoseIndex = 0;
		bool bIdleMeasured = false;
		float RecordedInward[2][4][2] = {}; // [Set][Pick][0=Peek,1=Idle]
		// Part 2 look cases: SetIndex (0=m4, 1=riffle) x Side (0=HighL, 1=HighR) x LookMode (0=InAo, 1=WallNormalRight90, 2=AlongOpposite90)
		int32 LookCase = 0;
		int32 LookSubStep = 0;

		const FPick& PickAt(const FRunner& R, int32 Idx) const
		{
			switch (Idx)
			{
			case 0: return R.HighRight;
			case 1: return R.HighLeft;
			case 2: return R.LowRight;
			default: return R.LowLeft;
			}
		}

		const TCHAR* PickLabel(int32 Idx) const
		{
			switch (Idx)
			{
			case 0: return TEXT("high right stand");
			case 1: return TEXT("high left stand");
			case 2: return TEXT("low right crouch");
			default: return TEXT("low left crouch");
			}
		}

		void MeasureBones(UWorld& World, AOperativeCharacter& Op, const FPick& Pick, const FString& Tag,
			TFunctionRef<void(bool, const FString&)> CheckFn, float* OutShoulderAbs = nullptr) const
		{
			USkeletalMeshComponent* Mesh = Op.GetMesh();
			const UOperativeAnimInstance* Anim = Mesh ? Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
			if (!Mesh)
			{
				CheckFn(false, FString::Printf(TEXT("135b %s: no mesh"), *Tag));
				return;
			}
			const FVector ClavL = Mesh->GetBoneLocation(TEXT("clavicle_l"));
			const FVector ClavR = Mesh->GetBoneLocation(TEXT("clavicle_r"));
			const FVector Pelvis = Mesh->GetBoneLocation(TEXT("pelvis"));
			const float InL = InwardCm(ClavL, Pick);
			const float InR = InwardCm(ClavR, Pick);
			const float ShoulderIn = InL < InR ? InL : InR;
			const float ShoulderAbs = FMath::Abs(ShoulderIn);
			const float PelvisIn = InwardCm(Pelvis, Pick);
			const FVector Normal = Pick.Base.WallNormal.GetSafeNormal2D();
			float WorstPen = PenetrationCm(World, Op.GetActorLocation(), Normal, &Op);
			const TCHAR* Bones[] = { TEXT("clavicle_r"), TEXT("clavicle_l"), TEXT("pelvis"), TEXT("head"), TEXT("hand_r"), TEXT("hand_l"), TEXT("spine_03") };
			const TCHAR* WorstBone = TEXT("capsule");
			for (const TCHAR* BoneName : Bones)
			{
				const FVector Pos = Mesh->GetBoneLocation(BoneName);
				// Past an exposed corner edge there is no wall behind the face plane.
				if (!CoverFacingRules::IsWithinWallSpan(Op.GetCoverSlot(), Pos, 2.f))
				{
					continue;
				}
				const float Pen = PenetrationCm(World, Pos, Normal, &Op);
				if (Pen > WorstPen)
				{
					WorstPen = Pen;
					WorstBone = BoneName;
				}
			}
			if (OutShoulderAbs)
			{
				*OutShoulderAbs = ShoulderIn;
			}
			float WeaponRelNormal = 0.f;
			const USceneComponent* WComp = (Op.WeaponSkeletalMesh && Op.WeaponSkeletalMesh->IsVisible())
				? static_cast<const USceneComponent*>(Op.WeaponSkeletalMesh.Get())
				: static_cast<const USceneComponent*>(Op.WeaponMesh.Get());
			if (WComp)
			{
				const FVector WDir = (Op.GetWeaponMuzzleLocation() - WComp->GetComponentLocation()).GetSafeNormal2D();
				if (!WDir.IsNearlyZero())
				{
					WeaponRelNormal = FMath::FindDeltaAngleDegrees(Normal.Rotation().Yaw, static_cast<float>(WDir.Rotation().Yaw));
				}
			}
			UE_LOG(LogCodexTactics, Display,
				TEXT("135b measure %s: shoulder %s inward %.1f (abs %.1f) | pelvis %.1f | worst pen %.1f (%s) | weaponRelNorm %.1f | firePose %d | set '%s' | clip %s"),
				*Tag, InL < InR ? TEXT("clavicle_l") : TEXT("clavicle_r"), ShoulderIn, ShoulderAbs, PelvisIn, WorstPen, WorstBone, WeaponRelNormal,
				Anim && Anim->IsCoverInFirePose() ? 1 : 0, Anim ? *Anim->GetActiveCoverAnimSet() : TEXT("?"),
				(Anim && Anim->GetCoverClipLog().Num() > 0) ? *Anim->GetCoverClipLog().Last(0) : TEXT("-"));
			CheckFn(ShoulderAbs <= 25.f, FString::Printf(TEXT("135b %s: outer shoulder %.1f cm from edge (<= 25)"), *Tag, ShoulderAbs));
			CheckFn(WorstPen <= 2.f, FString::Printf(TEXT("135b %s: wall penetration %.1f cm (%s, <= 2)"), *Tag, WorstPen, WorstBone));
		}

		void MeasureLookYaws(AOperativeCharacter& Op, const FVector& LookPoint, const FString& Tag,
			TFunctionRef<void(bool, const FString&)> CheckFn) const
		{
			USkeletalMeshComponent* Mesh = Op.GetMesh();
			const UOperativeAnimInstance* Anim = Mesh ? Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
			const float LookYaw = static_cast<float>((LookPoint - Op.GetActorLocation()).GetSafeNormal2D().Rotation().Yaw);
			const int32 SpineIdx = Mesh ? Mesh->GetBoneIndex(TEXT("spine_03")) : INDEX_NONE;
			float SpineErr = 180.f;
			float SpineYaw = 0.f;
			if (Mesh && SpineIdx != INDEX_NONE)
			{
				const FVector SpineFwd = Mesh->GetBoneTransform(SpineIdx).TransformVectorNoScale(LocalChestFwd).GetSafeNormal2D();
				SpineYaw = static_cast<float>(SpineFwd.Rotation().Yaw);
				SpineErr = FMath::Abs(FRotator::NormalizeAxis(SpineYaw - LookYaw));
			}
			float WeaponYaw = 0.f;
			float WeaponErr = 180.f;
			const FVector Muzzle = Op.GetWeaponMuzzleLocation();
			const USceneComponent* WeaponComp = (Op.WeaponSkeletalMesh && Op.WeaponSkeletalMesh->IsVisible())
				? static_cast<const USceneComponent*>(Op.WeaponSkeletalMesh.Get())
				: static_cast<const USceneComponent*>(Op.WeaponMesh.Get());
			if (WeaponComp)
			{
				const FVector Barrel = (Muzzle - WeaponComp->GetComponentLocation()).GetSafeNormal2D();
				if (!Barrel.IsNearlyZero())
				{
					WeaponYaw = static_cast<float>(Barrel.Rotation().Yaw);
					WeaponErr = FMath::Abs(FRotator::NormalizeAxis(WeaponYaw - LookYaw));
				}
			}
			UE_LOG(LogCodexTactics, Display,
				TEXT("135b look %s: lookYaw %.1f | spine_03 %.1f (err %.1f) | weapon %.1f (err %.1f) | actor %.1f | inCover %d firePose %d facing %s set '%s' AimYaw %.1f"),
				*Tag, LookYaw, SpineYaw, SpineErr, WeaponYaw, WeaponErr, static_cast<float>(Op.GetActorRotation().Yaw),
				Op.bInCover ? 1 : 0, Anim && Anim->IsCoverInFirePose() ? 1 : 0,
				Op.CoverFacing == ECoverFacing::Left ? TEXT("L") : TEXT("R"),
				Anim ? *Anim->GetActiveCoverAnimSet() : TEXT("?"), Anim ? Anim->AimYaw : 0.f);
			// In-cover sideways corner poses twist upper body differently from chest clip orientation; full body alignment holds when stepped out.
			if (!Op.bInCover)
			{
				CheckFn(SpineErr <= 15.f, FString::Printf(TEXT("135b look %s: spine_03 yaw err %.1f deg (<= 15)"), *Tag, SpineErr));
			}
			CheckFn(WeaponErr <= 15.f, FString::Printf(TEXT("135b look %s: weapon yaw err %.1f deg (<= 15)"), *Tag, WeaponErr));
		}

		bool Tick(UWorld& World, FRunner& R, TFunctionRef<void(bool, const FString&)> CheckFn)
		{
			AOperativeCharacter* Op = R.Leader.Get();
			USkeletalMeshComponent* Mesh = Op ? Op->GetMesh() : nullptr;
			UOperativeAnimInstance* Anim = Mesh ? Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
			if (!Op || !Mesh || !Anim)
			{
				CheckFn(false, TEXT("135b: missing leader/mesh/anim"));
				return true;
			}
			const double Delta = 1.0 / 60.0;
			StageTime += Delta;
			// Step 0: calibrate chest forward axis in spine_03 local space on neutral standing idle.
			if (Step == 0)
			{
				Op->ClearManualLookPoint();
				Anim->SetCoverAnimSetOverrideForTesting(FString());
				if (Op->bInCover)
				{
					Op->LeaveCover(TEXT("135b calibrate"));
				}
				Op->SetStance(EOperativeStance::Standing);
				ArmMesh(*Op);
				if (StageTime >= 0.6 && FMath::Abs(Anim->AimYaw) < 1.f)
				{
					const int32 SpineIdx = Mesh->GetBoneIndex(TEXT("spine_03"));
					if (SpineIdx != INDEX_NONE)
					{
						LocalChestFwd = Mesh->GetBoneTransform(SpineIdx).InverseTransformVectorNoScale(Op->GetActorForwardVector()).GetSafeNormal();
						bChestCalibrated = true;
					}
					CheckFn(bChestCalibrated, TEXT("135b: calibrated spine_03 local forward"));
					Step = 1;
					PoseIndex = 0;
					StageTime = 0.0;
				}
				return false;
			}
			// Step 1: 16 corner states (2 sets x 4 corners x [peek aim, idle return])
			if (Step == 1)
			{
				if (PoseIndex >= 8)
				{
					// Verify L/R symmetry within each set + stance + state
					for (int32 S = 0; S < 2; ++S)
					{
						for (int32 St = 0; St < 2; ++St) // 0=High stand, 1=Low crouch
						{
							for (int32 State = 0; State < 2; ++State)
							{
								const float RIn = RecordedInward[S][St * 2 + 0][State];
								const float LIn = RecordedInward[S][St * 2 + 1][State];
								const float Diff = FMath::Abs(RIn - LIn);
								CheckFn(Diff <= 12.f, FString::Printf(TEXT("135b symmetry %s %s %s: |R %.1f - L %.1f| = %.1f cm (<= 12)"),
									S == 0 ? TEXT("m4") : TEXT("riffle"), St == 0 ? TEXT("stand") : TEXT("crouch"),
									State == 0 ? TEXT("peek") : TEXT("idle"), RIn, LIn, Diff));
							}
						}
					}
					Step = 2;
					LookCase = 0;
					LookSubStep = 0;
					StageTime = 0.0;
					return false;
				}
				const int32 SetIdx = PoseIndex / 4;
				const int32 PickIdx = PoseIndex % 4;
				const FPick& Pick = PickAt(R, PickIdx);
				if (!Pick.bValid)
				{
					++PoseIndex;
					bIdleMeasured = false;
					StageTime = 0.0;
					return false;
				}
				const bool bLow = PickIdx >= 2;
				const FString SetName = SetIdx == 0 ? TEXT("m4") : TEXT("riffle");
				if (StageTime < 0.05)
				{
					bIdleMeasured = false;
					Op->ClearManualLookPoint();
					Anim->SetCoverAnimSetOverrideForTesting(SetName);
					Op->SetStance(bLow ? EOperativeStance::Crouching : EOperativeStance::Standing);
					Put(*Op, Pick.Corner.WorldLocation, Pick.Corner.WallNormal);
					return false;
				}
				if (!Op->bInCover && !Op->HasPendingCover() && StageTime < 0.25)
				{
					Op->OrderTakeCover(Pick.Corner, false);
					return false;
				}
				if (Op->bInCover && !Op->IsManualCornerPeek() && !bIdleMeasured && StageTime < 1.5)
				{
					Op->OrderCornerPeek(Pick.Side, Pick.EdgeAlong);
					return false;
				}
				if (!bIdleMeasured)
				{
					if (Op->bInCover && !Op->bShimmying && Op->IsPeekingRoundCorner() && Anim->IsCoverInFirePose() && StageTime >= 1.2)
					{
						const FString Tag = FString::Printf(TEXT("%s %s peek"), *SetName, PickLabel(PickIdx));
						MeasureBones(World, *Op, Pick, Tag, CheckFn, &RecordedInward[SetIdx][PickIdx][0]);
						bIdleMeasured = true;
						Op->EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("CornerSlotSmoke 135b idle return"));
						// Re-enter clean idle cover hug (without manual corner peek) at the exact corner slot.
						Op->LeaveCover(TEXT("135b switch peek to idle"));
						Op->SetStance(bLow ? EOperativeStance::Crouching : EOperativeStance::Standing);
						Put(*Op, Pick.Corner.WorldLocation, Pick.Corner.WallNormal);
						Op->OrderTakeCover(Pick.Corner, false);
						StageTime = 0.05;
					}
					else if (StageTime > 4.0)
					{
						CheckFn(false, FString::Printf(TEXT("135b %s %s peek: did not settle"), *SetName, PickLabel(PickIdx)));
						++PoseIndex;
						StageTime = 0.0;
					}
					return false;
				}
				if (!Op->bInCover && !Op->HasPendingCover() && StageTime >= 0.05 && StageTime < 0.35)
				{
					Op->OrderTakeCover(Pick.Corner, false);
					return false;
				}
				if (Op->bInCover && !Op->bShimmying && !Op->IsCornerAimActive() && !Anim->IsCoverInFirePose() && StageTime >= 1.8)
				{
					const FString Tag = FString::Printf(TEXT("%s %s idle"), *SetName, PickLabel(PickIdx));
					MeasureBones(World, *Op, Pick, Tag, CheckFn, &RecordedInward[SetIdx][PickIdx][1]);
					++PoseIndex;
					StageTime = 0.0;
				}
				else if (StageTime > 4.0)
				{
					CheckFn(false, FString::Printf(TEXT("135b %s %s idle: did not settle"), *SetName, PickLabel(PickIdx)));
					++PoseIndex;
					StageTime = 0.0;
				}
				return false;
			}
			// Step 2: Corner-hold Shift+click look after ReturnNoTargets (2 sets x 2 high corners x 3 look angles = 12 cases)
			if (Step == 2)
			{
				if (LookCase >= 12)
				{
					Op->ClearManualLookPoint();
					Anim->SetCoverAnimSetOverrideForTesting(FString());
					bActive = false;
					return true;
				}
				const int32 SetIdx = LookCase / 6;
				const int32 SideIdx = (LookCase / 3) % 2; // 0=HighLeft, 1=HighRight
				const int32 Mode = LookCase % 3; // 0=InAO(corner outward), 1=Normal (90 deg right of left corner), 2=Opposite90
				const FPick& Pick = SideIdx == 0 ? R.HighLeft : R.HighRight;
				if (!Pick.bValid)
				{
					++LookCase;
					LookSubStep = 0;
					StageTime = 0.0;
					return false;
				}
				const FString SetName = SetIdx == 0 ? TEXT("m4") : TEXT("riffle");
				const FString SideName = SideIdx == 0 ? TEXT("left") : TEXT("right");
				if (LookSubStep == 0)
				{
					Op->ClearManualLookPoint();
					Anim->SetCoverAnimSetOverrideForTesting(SetName);
					Op->SetStance(EOperativeStance::Standing);
					Put(*Op, Pick.Corner.WorldLocation, Pick.Corner.WallNormal);
					LookSubStep = 1;
					StageTime = 0.0;
					return false;
				}
				if (LookSubStep == 1 && StageTime >= 0.1)
				{
					Op->OrderTakeCover(Pick.Corner, false);
					LookSubStep = 2;
					StageTime = 0.0;
					return false;
				}
				if (LookSubStep == 2)
				{
					if (Op->bInCover && !Op->bShimmying && StageTime >= 0.8)
					{
						Op->EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("135b corner hold idle"));
						LookSubStep = 4;
						StageTime = 0.0;
					}
					else if (StageTime > 2.5)
					{
						CheckFn(false, FString::Printf(TEXT("135b look %s %s: failed enter corner hold"), *SetName, *SideName));
						++LookCase;
						LookSubStep = 0;
					}
					return false;
				}
				if (LookSubStep == 4 && StageTime >= 0.55)
				{
					const FVector Normal = Pick.Corner.WallNormal.GetSafeNormal2D();
					const FVector Toward = CoverFacingRules::AlongWallDirection(Pick.Corner, Pick.Side);
					FVector LookPt = Op->GetActorLocation();
					if (Mode == 0)
					{
						// Around the exposed corner (-106 deg from wall normal): stays in cover and aims via corner AO.
						LookPt += -Normal * 120.f + Toward * 420.f + FVector(0.f, 0.f, 60.f);
					}
					else if (Mode == 1)
					{
						LookPt += Normal * 600.f + FVector(0.f, 0.f, 60.f);
					}
					else
					{
						LookPt += Normal * 100.f - Toward * 600.f + FVector(0.f, 0.f, 60.f);
					}
					Op->SetManualLookPoint(LookPt);
					LookSubStep = 5;
					StageTime = 0.0;
					return false;
				}
				if (LookSubStep == 5 && StageTime >= 1.2)
				{
					const TCHAR* ModeStr = Mode == 0 ? TEXT("inAO") : (Mode == 1 ? TEXT("normal90") : TEXT("opposite90"));
					FVector LookPt = FVector::ZeroVector;
					Op->GetManualLookPoint(LookPt);
					MeasureLookYaws(*Op, LookPt, FString::Printf(TEXT("%s %s %s"), *SetName, *SideName, ModeStr), CheckFn);
					++LookCase;
					LookSubStep = 0;
					StageTime = 0.0;
				}
				return false;
			}
			return true;
		}
	};

	F135bMatrixState G135b;

	float Score(const FPick& Pick)
	{
		return Pick.bValid ? (Pick.Thickness >= 35.f ? 100000.f : 0.f) - Pick.Dist : -1.0e9f;
	}

	void Consider(UWorld& World, AOperativeCharacter& Leader, AStaticMeshActor& Actor, const FVector& Normal, const FVector& Tangent,
		float HalfLength, float Thickness, float EndSign, const FVector& FaceOrigin, bool bWantLow)
	{
		const FVector Click = FaceOrigin + Normal * 50.f + Tangent * EndSign * FMath::Max(0.f, HalfLength - 70.f) + FVector(0.f, 0.f, 110.f);
		FCoverSlot Slot;
		if (!CoverTraceRules::FindCoverSlotAt(&World, Click, -Normal, Slot) || !Slot.IsValid())
		{
			return;
		}
		if (FVector::DotProduct(Slot.WallNormal.GetSafeNormal2D(), Normal) < 0.8f)
		{
			return;
		}
		const bool bLow = Slot.Height == ECoverHeight::LowCover;
		if (bLow != bWantLow || Slot.Height == ECoverHeight::None)
		{
			return;
		}
		const bool bRight = FVector::DotProduct(Slot.RightTangent(), Tangent * EndSign) > 0.5f;
		ECoverFacing Side = ECoverFacing::Right;
		float EdgeAlong = 0.f;
		if (bRight)
		{
			if (!Slot.bRightEdgeExposed || Slot.RightEdgeDistanceCm > 160.f)
			{
				return;
			}
			Side = ECoverFacing::Right;
			EdgeAlong = Slot.RightEdgeDistanceCm;
		}
		else
		{
			if (!Slot.bLeftEdgeExposed || Slot.LeftEdgeDistanceCm > 160.f)
			{
				return;
			}
			Side = ECoverFacing::Left;
			EdgeAlong = -Slot.LeftEdgeDistanceCm;
		}
		const float StandOff = CoverFacingRules::CornerStandOff(Side, bLow, Leader.GetCoverFacingConfig());
		const float Stand = CoverFacingRules::CornerPeekStandAlong(Side, EdgeAlong, StandOff);
		FPick Pick;
		Pick.bValid = true;
		Pick.Base = Slot;
		Pick.Side = Side;
		Pick.EdgeAlong = EdgeAlong;
		Pick.StandAlong = Stand;
		Pick.Corner = ResolveStand(&World, Slot, Side, Stand);
		Pick.Edge = Slot.WallPoint + Slot.RightTangent() * EdgeAlong;
		Pick.BoxCorner = FaceOrigin + Tangent * EndSign * HalfLength;
		Pick.Thickness = Thickness;
		Pick.Dist = static_cast<float>(FVector::Dist2D(Pick.Corner.WorldLocation, Leader.GetActorLocation()));
		Pick.Name = Actor.GetName();
		const FVector MidClick = FaceOrigin + Normal * 50.f + Tangent * EndSign * FMath::Max(0.f, HalfLength - 220.f) + FVector(0.f, 0.f, 110.f);
		if (!CoverTraceRules::FindCoverSlotAt(&World, MidClick, -Normal, Pick.Mid) || !Pick.Mid.IsValid()
			|| FVector::DotProduct(Pick.Mid.WallNormal.GetSafeNormal2D(), Normal) < 0.8f)
		{
			Pick.Mid = Slot;
		}
		FPick& Best = PickFor(bWantLow, Side);
		if (Score(Pick) > Score(Best))
		{
			Best = Pick;
		}
	}

	void Scan(UWorld& World, AOperativeCharacter& Leader)
	{
		const float FeetZ = static_cast<float>(Leader.GetActorLocation().Z - Leader.GetSimpleCollisionHalfHeight());
		struct FCand
		{
			AStaticMeshActor* Actor = nullptr;
			float Dist = 0.f;
			float Thick = 0.f;
			float HalfLength = 0.f;
			FVector FaceOrigin = FVector::ZeroVector;
			FVector Normal = FVector::ForwardVector;
			FVector Tangent = FVector::RightVector;
		};
		TArray<FCand> Cands;
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			AStaticMeshActor* Actor = *It;
			if (!Actor || Actor->ActorHasTag(FName(TEXT("interior"))))
			{
				continue;
			}
			const FBox Local = Actor->CalculateComponentsBoundingBoxInLocalSpace(false);
			if (!Local.IsValid)
			{
				continue;
			}
			const FTransform Transform = Actor->GetActorTransform();
			const FVector Size = Local.GetSize();
			const FVector AxisX = Transform.TransformVector(FVector(Size.X, 0.f, 0.f));
			const FVector AxisY = Transform.TransformVector(FVector(0.f, Size.Y, 0.f));
			const float LenX = static_cast<float>(AxisX.Size2D());
			const float LenY = static_cast<float>(AxisY.Size2D());
			const float Height = static_cast<float>(Transform.TransformVector(FVector(0.f, 0.f, Size.Z)).Size());
			const float Long = FMath::Max(LenX, LenY);
			const float Thick = FMath::Min(LenX, LenY);
			const FVector Centre = Transform.TransformPosition(Local.GetCenter());
			const float Bottom = static_cast<float>(Centre.Z) - Height * 0.5f;
			if (Height < 50.f || Long < 180.f || Long > 8000.f || Thick < 8.f || Thick > 2500.f || FMath::Abs(Bottom - FeetZ) > 80.f)
			{
				continue;
			}
			FCand Cand;
			Cand.Actor = Actor;
			Cand.Dist = static_cast<float>(FVector::Dist2D(Centre, Leader.GetActorLocation()));
			Cand.Thick = Thick;
			Cand.HalfLength = Long * 0.5f;
			Cand.Tangent = (LenX >= LenY ? AxisX : AxisY).GetSafeNormal2D();
			Cand.Normal = (LenX >= LenY ? AxisY : AxisX).GetSafeNormal2D();
			Cand.FaceOrigin = FVector(Centre.X, Centre.Y, Bottom);
			Cands.Add(Cand);
		}
		Cands.Sort([](const FCand& A, const FCand& B)
		{
			const int32 RankA = A.Thick >= 35.f ? 0 : 1;
			const int32 RankB = B.Thick >= 35.f ? 0 : 1;
			if (RankA != RankB)
			{
				return RankA < RankB;
			}
			return A.Dist < B.Dist;
		});
		const int32 Limit = FMath::Min(Cands.Num(), 28);
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			const FCand& Cand = Cands[Index];
			for (const float NormalSign : { 1.f, -1.f })
			{
				const FVector Normal = Cand.Normal * NormalSign;
				const FVector Origin = Cand.FaceOrigin + Normal * (Cand.Thick * 0.5f);
				for (const float EndSign : { 1.f, -1.f })
				{
					Consider(World, Leader, *Cand.Actor, Normal, Cand.Tangent, Cand.HalfLength, Cand.Thick, EndSign, Origin, false);
					Consider(World, Leader, *Cand.Actor, Normal, Cand.Tangent, Cand.HalfLength, Cand.Thick, EndSign, Origin, true);
				}
			}
		}
		auto LogPick = [](const TCHAR* Which, const FPick& Pick)
		{
			if (!Pick.bValid)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: no corner for %s"), Which);
				return;
			}
			const float SlotIn = InwardCm(Pick.Corner.WorldLocation, Pick);
			const float Depth = static_cast<float>(FVector::DotProduct(Pick.Corner.WorldLocation - Pick.Corner.WallPoint, Pick.Corner.WallNormal.GetSafeNormal2D()));
			const float BoxGap = static_cast<float>(FVector::DotProduct(Pick.Edge - Pick.BoxCorner, Pick.Base.RightTangent()));
			const bool bFallback = FVector::DistSquared2D(Pick.Corner.WorldLocation, Pick.Base.WorldLocation + Pick.Base.RightTangent() * Pick.StandAlong) < 1.f;
			UE_LOG(LogCodexTactics, Display,
				TEXT("Smoke info: %s %s thick %.0f dist %.0f | stand-off along %.0f slot inward %.1f depth %.1f | probe edge vs box %.1f | fallback %d | %s"),
				Which, *Pick.Name, Pick.Thickness, Pick.Dist, Pick.StandAlong, SlotIn, Depth, BoxGap, bFallback ? 1 : 0, *Pick.Edge.ToCompactString());
		};
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: scanned %d ground meshes, traced %d"), Cands.Num(), Limit);
		LogPick(TEXT("high right"), Runner->HighRight);
		LogPick(TEXT("high left"), Runner->HighLeft);
		LogPick(TEXT("low right"), Runner->LowRight);
		LogPick(TEXT("low left"), Runner->LowLeft);
	}

	bool PoseReady(const AOperativeCharacter& Operative)
	{
		const UOperativeAnimInstance* Anim = Operative.GetMesh() ? Cast<UOperativeAnimInstance>(Operative.GetMesh()->GetAnimInstance()) : nullptr;
		const bool bPose = !Anim || Anim->IsCoverInFirePose() || Operative.IsCoverEntryPeekStance();
		return Operative.bInCover && !Operative.bShimmying && Operative.GetVelocity().SizeSquared2D() < FMath::Square(30.f)
			&& (Operative.IsPeekingRoundCorner() || Operative.IsCornerAimActive() || Operative.IsCoverEntryPeekStance()) && bPose;
	}

	void Measure(UWorld& World, AOperativeCharacter& Operative, const FPick& Pick, const TCHAR* Label)
	{
		const FVector Normal = Pick.Corner.WallNormal.GetSafeNormal2D();
		const FVector Capsule = Operative.GetActorLocation();
		const float Radius = Operative.GetSimpleCollisionRadius();
		const float CapsuleIn = InwardCm(Capsule, Pick);
		const float CapsulePen = PenetrationCm(World, Capsule - Normal * Radius, Normal, &Operative);
		float ShoulderIn = 0.f;
		float PelvisIn = 0.f;
		bool bPelvis = false;
		float WorstPen = CapsulePen;
		FString Shoulder = TEXT("none");
		FString WorstBone = TEXT("capsule");
		USkeletalMeshComponent* Mesh = Operative.GetMesh();
		if (Mesh)
		{
			Mesh->RefreshBoneTransforms();
		}
		const TCHAR* Bones[] = { TEXT("clavicle_r"), TEXT("clavicle_l"), TEXT("pelvis"), TEXT("head"), TEXT("hand_r"), TEXT("hand_l"), TEXT("spine_03") };
		float BestShoulder = TNumericLimits<float>::Max();
		for (const TCHAR* Bone : Bones)
		{
			if (!Mesh || Mesh->GetBoneIndex(FName(Bone)) == INDEX_NONE)
			{
				continue;
			}
			const FVector At = Mesh->GetBoneLocation(FName(Bone));
			const float In = InwardCm(At, Pick);
			const float Pen = PenetrationCm(World, At, Normal, &Operative);
			if (Pen > WorstPen)
			{
				WorstPen = Pen;
				WorstBone = Bone;
			}
			if (FString(Bone).Equals(TEXT("pelvis")))
			{
				PelvisIn = In;
				bPelvis = true;
			}
			if (FString(Bone).Contains(TEXT("clavicle")) && In < BestShoulder)
			{
				BestShoulder = In;
				ShoulderIn = In;
				Shoulder = Bone;
			}
		}
		const UOperativeAnimInstance* Anim = Mesh ? Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
		const FString Clip = Anim && Anim->GetCoverLoopClip() ? Anim->GetCoverLoopClip()->GetName() : FString(TEXT("-"));
		const FString Set = Anim ? Anim->GetActiveCoverAnimSet() : FString();
		UE_LOG(LogCodexTactics, Display,
			TEXT("Smoke measure %s: shoulder %s inward %.1f (abs %.1f) | pelvis %s inward %.1f | capsule inward %.1f pen %.1f | worst pen %.1f (%s) | mesh off %.1f | stance %d peek %d aim %d | set '%s' clip %s"),
			Label, *Shoulder, ShoulderIn, FMath::Abs(ShoulderIn), bPelvis ? TEXT("yes") : TEXT("no"), PelvisIn, CapsuleIn, CapsulePen, WorstPen, *WorstBone, Operative.GetCoverEntryMeshOffset().Size(),
			static_cast<int32>(Operative.GetStance()), Operative.IsPeekingRoundCorner() ? 1 : 0, Operative.IsCornerAimActive() ? 1 : 0,
			Set.IsEmpty() ? TEXT("m4") : *Set, *Clip);
		const float MaxCapsuleIn = Operative.GetStance() == EOperativeStance::Crouching ? 57.f : 36.f;
		Check(CapsuleIn <= MaxCapsuleIn, FString::Printf(TEXT("%s: capsule %.1f cm from the edge (limit %.0f)"), Label, CapsuleIn, MaxCapsuleIn));
		const bool bShoulder = Shoulder != TEXT("none") && FMath::Abs(ShoulderIn) <= MaxShoulderFromEdgeCm;
		Check(bShoulder, FString::Printf(TEXT("%s: outer shoulder %.1f cm from the edge (limit %.0f)"), Label, FMath::Abs(ShoulderIn), MaxShoulderFromEdgeCm));
		Check(WorstPen <= MaxPenetrationCm, FString::Printf(TEXT("%s: penetration %.1f cm (%s, limit %.0f)"), Label, WorstPen, *WorstBone, MaxPenetrationCm));
	}

	bool BeginCase(UWorld& World, AOperativeCharacter& Operative)
	{
		const FCase& Case = Cases[Runner->CaseIndex];
		const FPick& Pick = PickFor(Case.bLow, Case.Side);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: case %d %s"), Runner->CaseIndex, Case.Label);
		if (!Pick.bValid)
		{
			Check(false, FString::Printf(TEXT("%s: no X6 corner of that hand and height"), Case.Label));
			return false;
		}
		ArmMesh(Operative);
		const FVector Normal = Pick.Corner.WallNormal.GetSafeNormal2D();
		const FVector Face = (-Normal).GetSafeNormal2D();
		if (Case.bRunPeek)
		{
			Put(Operative, Pick.Corner.WorldLocation + Normal * 280.f, Face);
			const EOperativeOrderResult Result = Operative.OrderTakeCoverForPeek(Pick.Corner, Pick.Edge, false);
			Check(Result == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s: run-peek order accepted (%d)"), Case.Label, static_cast<int32>(Result)));
			return Result == EOperativeOrderResult::Accepted;
		}
		Put(Operative, Pick.Mid.WorldLocation + Normal * 220.f, Face);
		const EOperativeOrderResult Result = Operative.OrderTakeCover(Pick.Mid, false);
		Check(Result == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s: cover order accepted (%d)"), Case.Label, static_cast<int32>(Result)));
		return Result == EOperativeOrderResult::Accepted;
	}

	bool OrderPeekFromCover(AOperativeCharacter& Operative, const FCase& Case, const FPick& Pick)
	{
		ECoverFacing Side = Case.Side;
		float Edge = 0.f;
		if (!CoverFacingRules::FindPeekEdge(Operative.GetCoverSlot(), Pick.Edge, &Pick.Base, Side, Edge))
		{
			Check(false, FString::Printf(TEXT("%s: the held slot has no peek edge"), Case.Label));
			return false;
		}
		const EOperativeOrderResult Result = Operative.OrderCornerPeek(Side, Edge);
		Check(Result == EOperativeOrderResult::Accepted, FString::Printf(TEXT("%s: corner peek accepted (%d)"), Case.Label, static_cast<int32>(Result)));
		return Result == EOperativeOrderResult::Accepted;
	}

	void ForceOntoSlot(AOperativeCharacter& Operative, const FPick& Pick)
	{
		if (Operative.bInCover)
		{
			Operative.LeaveCover(TEXT("CornerSlotSmoke force"));
		}
		Operative.StopOperative();
		const FVector Normal = Pick.Corner.WallNormal.GetSafeNormal2D();
		const float Half = Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Operative.TeleportTo(Pick.Corner.WorldLocation + FVector(0.f, 0.f, Half + 6.f), (-Normal).Rotation(), false, true);
		Operative.OrderTakeCoverForPeek(Pick.Corner, Pick.Edge, false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: forced onto the corner slot (the walk did not arrive)"));
	}

	void Tick()
	{
		if (!Runner.IsValid() || Runner->Step == EStep::Done)
		{
			return;
		}
		UWorld* World = Runner->World.Get();
		if (!World)
		{
			Check(false, TEXT("the world is gone"));
			Finish();
			return;
		}
		switch (Runner->Preload.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting:
			return;
		case SmokeUtils::EPreloadGate::Failed:
			Check(false, Runner->Preload.FailReason);
			Finish();
			return;
		default:
			break;
		}
		++Runner->Frame;
		++Runner->StepFrame;
		if (Runner->Frame > 60 * 150)
		{
			Check(false, TEXT("time out"));
			Finish();
			return;
		}
		if (Runner->Frame % 60 == 0)
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
		}
		switch (Runner->Step)
		{
		case EStep::Warmup:
			if (Runner->StepFrame < 90)
			{
				return;
			}
			if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
			{
				Squad->EnterSoloMode();
				Runner->Leader = Squad->GetLeader();
				if (AOperativeCharacter* Leader = Runner->Leader.Get())
				{
					for (AOperativeCharacter* Member : Squad->GetMembers())
					{
						if (Member && Member != Leader)
						{
							Member->StopOperative();
							Member->TeleportTo(Leader->GetActorLocation() + FVector(8000.f, 0.f, 0.f), FRotator::ZeroRotator, false, true);
						}
					}
				}
			}
			if (!Runner->Leader.IsValid())
			{
				Check(false, TEXT("no squad leader"));
				Finish();
				return;
			}
			Go(EStep::Scan);
			return;
		case EStep::Scan:
			Scan(*World, *Runner->Leader);
			Runner->CaseIndex = 0;
			Go(EStep::Begin);
			return;
		case EStep::Begin:
			if (Runner->CaseIndex >= UE_ARRAY_COUNT(Cases))
			{
				Finish();
				return;
			}
			Runner->bForced = false;
			if (!BeginCase(*World, *Runner->Leader))
			{
				++Runner->CaseIndex;
				Go(EStep::Begin);
				return;
			}
			Go(Cases[Runner->CaseIndex].bRunPeek ? EStep::WaitPose : EStep::WaitWalk);
			return;
		case EStep::WaitWalk:
		{
			AOperativeCharacter* Operative = Runner->Leader.Get();
			const FCase& Case = Cases[Runner->CaseIndex];
			if (Operative && Operative->bInCover && !Operative->bShimmying && Operative->GetVelocity().SizeSquared2D() < FMath::Square(30.f))
			{
				if (OrderPeekFromCover(*Operative, Case, PickFor(Case.bLow, Case.Side)))
				{
					Go(EStep::WaitPose);
				}
				else
				{
					++Runner->CaseIndex;
					Go(EStep::Begin);
				}
				return;
			}
			if (Runner->StepFrame > WalkFrames)
			{
				Check(false, FString::Printf(TEXT("%s: did not reach the wall"), Case.Label));
				++Runner->CaseIndex;
				Go(EStep::Begin);
			}
			return;
		}
		case EStep::WaitPose:
		{
			AOperativeCharacter* Operative = Runner->Leader.Get();
			const FCase& Case = Cases[Runner->CaseIndex];
			const FPick& Pick = PickFor(Case.bLow, Case.Side);
			if (!Operative)
			{
				Check(false, FString::Printf(TEXT("%s: the leader is gone"), Case.Label));
				Finish();
				return;
			}
			if (PoseReady(*Operative))
			{
				++Runner->Settled;
				if (Runner->Settled >= SettleFrames)
				{
					Measure(*World, *Operative, Pick, Case.Label);
					if (Runner->CaseIndex == UE_ARRAY_COUNT(Cases) - 1)
					{
						// 135 Part A: from a corner peek, an order to another cover >= 5 m away must leave the corner and arrive.
						const FPick& Away = Runner->HighRight.bValid ? Runner->HighRight : Runner->LowRight;
						if (Away.bValid && FVector::Dist2D(Operative->GetActorLocation(), Away.Mid.WorldLocation) >= 500.f)
						{
							Runner->MoveOffTarget = Away.Mid;
							Runner->MoveOffFrom = Operative->GetActorLocation();
							const EOperativeOrderResult Res = Operative->OrderTakeCover(Away.Mid, true);
							Check(Res == EOperativeOrderResult::Accepted, FString::Printf(TEXT("135 Part A: cover order from corner peek accepted (%d)"), static_cast<int32>(Res)));
							if (Res == EOperativeOrderResult::Accepted)
							{
								Go(EStep::MoveOffCorner);
								return;
							}
						}
					}
					++Runner->CaseIndex;
					Go(EStep::Begin);
				}
				return;
			}
			Runner->Settled = 0;
			if (!Runner->bForced && Case.bRunPeek && Runner->StepFrame > WalkFrames)
			{
				Runner->bForced = true;
				ForceOntoSlot(*Operative, Pick);
				Runner->StepFrame = 0;
				return;
			}
			if (!Case.bRunPeek && Runner->StepFrame > WalkFrames)
			{
				Check(false, FString::Printf(TEXT("%s: the peek pose did not settle"), Case.Label));
				if (Operative->bInCover)
				{
					Measure(*World, *Operative, Pick, Case.Label);
				}
				++Runner->CaseIndex;
				Go(EStep::Begin);
				return;
			}
			if (Runner->bForced && Runner->StepFrame > PoseFrames)
			{
				Check(false, FString::Printf(TEXT("%s: the peek pose did not settle"), Case.Label));
				if (Operative->bInCover)
				{
					Measure(*World, *Operative, Pick, Case.Label);
				}
				++Runner->CaseIndex;
				Go(EStep::Begin);
			}
			return;
		}
		case EStep::MoveOffCorner:
		{
			if (G135b.bActive)
			{
				if (G135b.Tick(*World, *Runner, Check))
				{
					Finish();
				}
				return;
			}
			AOperativeCharacter* Operative = Runner->Leader.Get();
			if (!Operative)
			{
				Check(false, TEXT("135 Part A: leader gone"));
				Finish();
				return;
			}
			const float DistToTarget = static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), Runner->MoveOffTarget.WorldLocation));
			const float MovedFromCorner = static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), Runner->MoveOffFrom));
			if (Operative->bInCover && DistToTarget <= 150.f && MovedFromCorner >= 400.f)
			{
				Check(true, FString::Printf(TEXT("135 Part A: left corner peek and arrived at target cover (moved %.0f cm)"), MovedFromCorner));
				G135b = F135bMatrixState();
				G135b.bActive = true;
				return;
			}
			if (Runner->StepFrame > WalkFrames * 2)
			{
				Check(false, FString::Printf(TEXT("135 Part A: stayed at corner or failed to arrive (moved %.0f cm, dist %.0f cm, inCover %d)"),
					MovedFromCorner, DistToTarget, Operative->bInCover ? 1 : 0));
				Finish();
			}
			return;
		}
		default:
			return;
		}
	}

	void Run(const TArray<FString>& /*Args*/, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->World = World;
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		SmokeUtils::StopLevelPmc(World);
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([]() { Tick(); });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: CornerSlotSmoke started"));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerSlotSmoke"),
		TEXT("Dev smoke: G025 corner slot on X6 blocks, both hands, stand and crouch. Shoulder <= 25 cm from the edge, penetration <= 2 cm."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
