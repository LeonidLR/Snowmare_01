// AOperativeCharacter: Running into cover, the enter-clip mesh blend, wall clearance, the body-yaw blend, the wall-edge cover switch
// (game designer 2026-10-20, Tactics/CoverSwitchRules).
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// UE-only (user-found cover bugs 2026-10-07 / 2026-10-11; no Godot reference). Pure rules: Tactics/CoverFacingRules.

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverWrapRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugCoverSwitch(TEXT("Codex.Debug.CoverSwitch"), 0,
		TEXT("1: log every run-in probe of the wall-edge cover switch (distance, speed, heading, the best clip fit)."));
}

void AOperativeCharacter::TryEnterCoverFromRun()
{
	// User-found bug 2026-10-07: running into cover the run stopped dead at the slot and the enter clip popped the body
	// 76 cm back out (its start pose stands out from the wall facing it). Now the cover is entered where that start pose
	// stands (plus a glide for the run's momentum): the capsule goes to the slot, the mesh is offset so the body stays
	// where it ran and glides on, decelerating, while the clip blends in (UpdateCoverEntryBlend).
	if (!bHasPendingCover || !bHasMoveOrder || bInCover || bShimmying || IsRaging() || IsPanicking())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (!World || (TurnBased && TurnBased->IsActive()))
	{
		return; // grid walks end on their cell (UpdateCover enters there)
	}
	// Wall-edge cover switch: the pack's run-in transition takes over the last metres. While it waits for its moment the ordinary run-in
	// stays off; only when the switch gives up (no clip / no fit: bCoverSwitchPending cleared) does the ordinary run-in below go on.
	if (bCoverSwitchPending && (TryEnterCoverSwitchFromRun() || bCoverSwitchPending))
	{
		return;
	}
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const EOperativeStance EntryStance = CoverRules::DefaultStanceFor(PendingCoverSlot.Height) == EOperativeStance::Crouching
		|| Stance == EOperativeStance::Crouching ? EOperativeStance::Crouching : EOperativeStance::Standing;
	const bool bCrouchEntry = EntryStance == EOperativeStance::Crouching;
	const TArray<TObjectPtr<UAnimSequenceBase>>* EnterClips = Anim ? (bCrouchEntry ? &Anim->CoverCrouchEnter : &Anim->CoverStandEnter) : nullptr;
	const bool bHasEnterClip = Anim && Anim->bUseNativeCoverClips && EnterClips && EnterClips->ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; });
	const float ClipLead = bHasEnterClip ? (bCrouchEntry ? CoverEnterClipLeadCrouchCm : CoverEnterClipLeadStandCm) : 0.f;
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	const float Glide = FMath::Min(Speed * CoverEnterDecelSeconds * 0.5f, CoverEnterMaxGlideCm);
	const FVector Here = GetActorLocation();
	const float Distance = static_cast<float>(FVector::Dist2D(Here, PendingCoverSlot.WorldLocation));
	if (Distance > ClipLead + Glide || Distance < 1.f || Speed < 20.f)
	{
		return; // not there yet (or standing: HandleMoveFinished / UpdateCover enter at the slot)
	}
	// The enter clip starts facing the wall out in front of it: only an approach towards the wall matches it (walking in
	// along the wall it would glide the body sideways; then he enters on arrival as before).
	if (FVector::DotProduct(GetVelocity().GetSafeNormal2D(), -PendingCoverSlot.WallNormal.GetSafeNormal2D()) < 0.5f)
	{
		return;
	}
	const FCoverSlot Slot = PendingCoverSlot;
	bHasPendingCover = false;
	{
		TGuardValue<bool> FromRun(bCoverEntryFromRunRequest, true);
		EnterCover(Slot);
	}
	if (!bInCover)
	{
		return;
	}
	const FVector Delta = Here - GetActorLocation();
	CoverEntryClipOffset = Slot.WallNormal.GetSafeNormal2D() * ClipLead; // where the clip's start pose stands from the slot
	CoverEntryGlide = FVector(Delta.X, Delta.Y, 0.f) - CoverEntryClipOffset;
	CoverEntryBlendTime = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: runs into the cover (%.0f cm out, speed %.0f, clip lead %.0f, glide %.0f)"), *DisplayName.ToString(),
		Distance, Speed, ClipLead, CoverEntryGlide.Size2D());
}

FCoverSwitchConfig AOperativeCharacter::GetCoverSwitchConfig() const
{
	FCoverSwitchConfig Config;
	Config.bEnabled = bCoverSwitchEnabled;
	Config.MaxRangeCm = CoverSwitchMaxRangeCm;
	Config.MinRangeCm = CoverSwitchMinRangeCm;
	Config.EdgeReachCm = CoverSwitchEdgeReachCm;
	return Config;
}

ECoverSwitchVerdict AOperativeCharacter::EvaluateCoverSwitch(const FCoverSlot& Target) const
{
	// His own object's other faces are the wrap round it along its wall (2026-10-20), never a switch.
	const bool bSameWall = bInCover && Target.IsValid() && (CoverTraceRules::IsSameWall(CoverSlot, Target) || CoverWrapWorld::IsSameObject(CoverSlot, Target));
	return CoverSwitchRules::Evaluate(bInCover, bShimmying, CoverSlot, Target, bSameWall, GetCoverSwitchConfig());
}

FCoverSlot AOperativeCharacter::ResolveCoverSwitchSlot(const FCoverSlot& Target, ECoverFacing* OutFacing) const
{
	UWorld* World = GetWorld();
	// Corner to corner (game designer 2026-10-20, second pass): from the corner he stands at, the NEAREST corner of the other object on the
	// protected side - the face whose normal agrees with his own wall's (wherever on the object the click was: its side face, the far end) -
	// and he arrives facing that corner, back along the new wall towards the open side he came over (the mirrored pose).
	FCoverFootprint Footprint;
	int32 ClickedEdge = INDEX_NONE;
	const FVector2D Protected = FVector2D(bInCover ? CoverSlot.WallNormal : Target.WallNormal).GetSafeNormal();
	// User 2026-10-21: the hovered face decides - a face on the protected side -> its nearest corner (below); another face (the end face
	// towards him) -> the slot there, where the cursor is (the old perpendicular move, below the corner rule).
	if (World && CoverSwitchRules::UsesCornerTarget(FVector2D(Target.WallNormal), Protected) && CoverWrapWorld::FootprintForSlot(Target, Footprint, ClickedEdge))
	{
		const bool bCrouched = Target.Height == ECoverHeight::LowCover;
		const FCoverFacingConfig FacingConfig = GetCoverFacingConfig();
		const float Offset = FCoverTraceConfig().SlotOffsetCm;
		FCoverSwitchCorner Corner;
		if (CoverSwitchRules::ChooseSwitchCorner(Footprint, FVector2D(GetActorLocation()), Protected, ClickedEdge, Offset,
			[&FacingConfig, bCrouched](ECoverFacing Facing) { return CoverFacingRules::CornerStandOff(Facing, bCrouched, FacingConfig); }, Corner))
		{
			const FVector2D FacePoint = Corner.SlotPoint - Corner.Normal * Offset;
			FCoverSlot Slot;
			if (CoverTraceRules::FindCoverSlotAt(World, FVector(FacePoint.X, FacePoint.Y, Target.WallPoint.Z), FVector(-Corner.Normal.X, -Corner.Normal.Y, 0.f), Slot)
				&& CoverWrapWorld::IsSameObject(Slot, Target) && FVector2D::DotProduct(FVector2D(Slot.WallNormal).GetSafeNormal(), Corner.Normal) >= 0.9f)
			{
				if (OutFacing)
				{
					*OutFacing = Corner.Facing;
				}
				return Slot;
			}
		}
	}
	// No footprint (a landscape / BSP / tilted wall): as the group cover order - on a high wall the corner he would walk to right after
	// entering (TrySnapToCoverCorner) is the slot at once, no side-step cutting the run-in clip short; a low cover keeps the clicked slot.
	FCoverSlot Result = Target;
	if (Target.Height == ECoverHeight::HighCover && World)
	{
		const ECoverFacing Facing = PredictCoverFacing(Target);
		float Shift = 0.f;
		FCoverSlot Corner;
		if (CoverFacingRules::ShouldSnapToCorner(Target, Facing, Shift, GetCoverFacingConfig(), false)
			&& CoverTraceRules::FindShimmySlot(World, Target, Target.WallPoint + CoverFacingRules::AlongWallDirection(Target, Facing) * Shift, Corner)
			&& (!Target.WallActor.IsValid() || Corner.WallActor == Target.WallActor))
		{
			Result = Corner;
		}
	}
	if (OutFacing)
	{
		*OutFacing = GetCoverSwitchArrivalFacing(Result);
	}
	return Result;
}

ECoverFacing AOperativeCharacter::GetCoverSwitchArrivalFacing(const FCoverSlot& Resolved) const
{
	return CoverSwitchRules::ArrivalFacing(Resolved, FCoverFacingConfig().CornerReachCm, PredictCoverFacing(Resolved));
}

void AOperativeCharacter::SetPendingCoverSwitch(bool bSwitch)
{
	bCoverSwitchPending = bSwitch && bHasPendingCover;
	CoverSwitchProbeMismatchCm = TNumericLimits<float>::Max();
	bCoverSwitchFacingPending = bCoverSwitchPending;
	if (bCoverSwitchPending)
	{
		CoverSwitchArrivalFacing = GetCoverSwitchArrivalFacing(PendingCoverSlot);
	}
}

EOperativeOrderResult AOperativeCharacter::OrderCoverSwitch(const FCoverSlot& Target)
{
	ECoverFacing Arrival = ECoverFacing::Right;
	const FCoverSlot Slot = ResolveCoverSwitchSlot(Target, &Arrival);
	return OrderCoverSwitchResolved(Slot, Arrival);
}

EOperativeOrderResult AOperativeCharacter::OrderCoverSwitchResolved(const FCoverSlot& Slot, ECoverFacing Arrival)
{
	const ECoverSwitchVerdict Verdict = EvaluateCoverSwitch(Slot);
	const ECoverFacing OldFacing = CoverFacing;
	const bool bWasInCover = bInCover;
	const FVector From = GetActorLocation();
	const EOperativeOrderResult Result = OrderTakeCover(Slot, true);
	if (Result == EOperativeOrderResult::Accepted && bHasPendingCover)
	{
		bCoverSwitchPending = true;
		CoverSwitchProbeMismatchCm = TNumericLimits<float>::Max();
		bCoverSwitchFacingPending = true;
		CoverSwitchArrivalFacing = Arrival;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: cover switch - dashes %.0f cm to the other cover's corner (%s, approach %d), arrives facing %s%s"),
			*DisplayName.ToString(), FVector::Dist2D(From, Slot.WorldLocation), CoverSwitchRules::VerdictName(Verdict),
			static_cast<int32>(CoverSwitchRules::ClassifyApproach(Slot, From)), Arrival == ECoverFacing::Left ? TEXT("left") : TEXT("right"),
			bWasInCover && Arrival == CoverSwitchRules::MirrorFacing(OldFacing) ? TEXT(" (mirrored)") : TEXT(""));
	}
	return Result;
}

bool AOperativeCharacter::TryEnterCoverSwitchFromRun()
{
	// Called by TryEnterCoverFromRun once its checks passed (pending cover, running order, not in cover, not turn-based).
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const bool bCrouch = CoverRules::DefaultStanceFor(PendingCoverSlot.Height) == EOperativeStance::Crouching || Stance == EOperativeStance::Crouching;
	if (!Anim || !Anim->bUseNativeCoverClips || !Anim->HasCoverSwitchClips(bCrouch))
	{
		bCoverSwitchPending = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: cover switch without run-in clips - the ordinary entry"), *DisplayName.ToString());
		return false;
	}
	const FVector Here = GetActorLocation();
	const FVector Velocity2D = FVector(GetVelocity().X, GetVelocity().Y, 0.f);
	const float Speed = static_cast<float>(Velocity2D.Size());
	const float Distance = static_cast<float>(FVector::Dist2D(Here, PendingCoverSlot.WorldLocation));
	if (Distance > CoverSwitchClipLeadCm || Distance < 1.f || Speed < 200.f)
	{
		return false; // not there yet, or not running (a hitch / a repath: wait; arriving standing: the ordinary entry)
	}
	const FVector ToSlot = (PendingCoverSlot.WorldLocation - Here).GetSafeNormal2D();
	if (CVarDebugCoverSwitch.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: switch probe %.0f cm out, speed %.0f, heading %.2f, best fit so far %.0f cm"), *DisplayName.ToString(),
			Distance, Speed, FVector::DotProduct(Velocity2D.GetSafeNormal(), ToSlot), CoverSwitchProbeMismatchCm);
	}
	if (FVector::DotProduct(Velocity2D.GetSafeNormal(), ToSlot) < 0.3f)
	{
		return false; // the path still bends round something: wait until he heads for the slot
	}
	const FCoverSlot Slot = PendingCoverSlot;
	const FVector2D Runner(Here.X - Slot.WorldLocation.X, Here.Y - Slot.WorldLocation.Y);
	// The clip's body as the mesh draws it at the slot (actor yaw = the wall normal, plus the Blueprint's mesh yaw).
	const FVector2D RunDirection(Velocity2D.X, Velocity2D.Y);
	// Tracks 0-2: the jog run-ins by approach, 3-5: the sprint ones (2026-10-21); a sprinting runner prefers the sprint set.
	const bool bSprintRunner = Speed >= 500.f;
	auto Choose = [this, Anim, bCrouch, bSprintRunner, &Runner, &RunDirection](ECoverFacing Facing, float ActorYaw)
	{
		TArray<FCoverSwitchTrack> Tracks;
		TArray<float> Bias;
		Tracks.SetNum(6);
		Bias.Init(0.f, 6);
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const bool bSprintClip = Index >= 3;
			Anim->GetCoverSwitchTrack(bCrouch, static_cast<ECoverSwitchApproach>(Index % 3), Facing, ActorYaw + MeshBaseYaw, Tracks[Index], bSprintClip);
			Bias[Index] = bSprintClip == bSprintRunner ? -CoverSwitchSprintPreferenceCm : 0.f;
		}
		FCoverSwitchClipStart Choice = CoverSwitchRules::ChooseClipStart(Tracks, Runner, CoverSwitchMinClipSeconds, RunDirection, 0.5f, Bias);
		if (Choice.Track != INDEX_NONE)
		{
			Choice.Approach = Choice.Track % 3;
		}
		return Choice;
	};
	FCoverSwitchClipStart Choice = Choose(bCoverSwitchFacingPending ? CoverSwitchArrivalFacing : PredictCoverFacing(Slot), CoverFacingRules::FacingYaw(Slot, ECoverFacing::Right));
	// The moment: the clip's body close to him, or the fit no longer improving (a diagonal run-in nears the clips' line until the clip's
	// shortest allowed rest), at the latest CoverSwitchLastChanceCm out.
	const bool bGoodFit = Choice.Approach != INDEX_NONE && Choice.MismatchCm <= CoverSwitchGoodFitCm;
	const bool bWorse = Choice.Approach != INDEX_NONE && Choice.MismatchCm > CoverSwitchProbeMismatchCm + 1.f;
	if (Choice.Approach != INDEX_NONE && !bGoodFit && !bWorse && Distance > CoverSwitchLastChanceCm)
	{
		CoverSwitchProbeMismatchCm = FMath::Min(CoverSwitchProbeMismatchCm, Choice.MismatchCm);
		return false;
	}
	if (Choice.Approach == INDEX_NONE || Choice.MismatchCm > CoverSwitchMaxMismatchCm)
	{
		bCoverSwitchPending = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: no run-in clip fits the approach (%.0f cm off) - the ordinary entry"), *DisplayName.ToString(),
			Choice.MismatchCm);
		return false;
	}
	bHasPendingCover = false;
	bCoverSwitchPending = false;
	{
		TGuardValue<bool> FromRun(bCoverEntryFromRunRequest, true);
		TGuardValue<bool> Switch(bCoverSwitchEntryRequest, true);
		EnterCover(Slot);
	}
	if (!bInCover)
	{
		return true;
	}
	// The facing he ended with decides the clip's end side (a threat may have turned him on entry): its tracks.
	Choice = Choose(CoverFacing, GetActorRotation().Yaw);
	if (Choice.Approach == INDEX_NONE)
	{
		bCoverSwitchEntry = false; // the ordinary enter clip plays; its start pose stands out in front of the wall
		Choice.ClipOffset = FVector2D(Slot.WallNormal.GetSafeNormal2D() * (bCrouch ? CoverEnterClipLeadCrouchCm : CoverEnterClipLeadStandCm));
	}
	else
	{
		CoverSwitchApproach = static_cast<ECoverSwitchApproach>(Choice.Approach);
		bCoverSwitchSprintClip = Choice.Track >= 3;
		CoverSwitchStartSeconds = Choice.StartSeconds;
		LastCoverSwitchMismatchCm = Choice.MismatchCm;
		++CoverSwitchCount;
	}
	const FVector Delta = Here - GetActorLocation();
	CoverEntryClipOffset = FVector(Choice.ClipOffset.X, Choice.ClipOffset.Y, 0.f);
	CoverEntryGlide = FVector(Delta.X, Delta.Y, 0.f) - CoverEntryClipOffset;
	CoverEntryBlendTime = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: cover switch run-in %s from %.2f s (%.0f cm out, speed %.0f, clip body %.0f cm off the runner)"),
		*DisplayName.ToString(), *CoverSwitchRules::ClipName(CoverSwitchApproach, CoverFacing, bCrouch, bCoverSwitchSprintClip), CoverSwitchStartSeconds, Distance, Speed,
		Choice.MismatchCm);
	return true;
}

float AOperativeCharacter::UpdateCoverWallClearance(float DeltaTime)
{
	USkeletalMeshComponent* SkinMesh = GetMesh();
	float Target = 0.f;
	if (bInCover && SkinMesh && SkinMesh->GetSkeletalMeshAsset() && CoverWallClearanceCm > 0.f)
	{
		if (CoverClearanceBonesMesh.Get() != SkinMesh->GetSkeletalMeshAsset())
		{
			CoverClearanceBonesMesh = SkinMesh->GetSkeletalMeshAsset();
			CoverClearanceBones.Reset();
			const FReferenceSkeleton& RefSkeleton = SkinMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
			{
				const FString Name = RefSkeleton.GetBoneName(Index).ToString();
				if (!Name.StartsWith(TEXT("ik_")) && !Name.StartsWith(TEXT("VB ")) && !Name.Equals(TEXT("root")))
				{
					CoverClearanceBones.Add(Index);
				}
			}
		}
		const TArray<FTransform>& Pose = SkinMesh->GetComponentSpaceTransforms();
		const FTransform& ToWorld = SkinMesh->GetComponentTransform();
		const FVector Normal = CoverSlot.WallNormal.GetSafeNormal2D();
		CoverWallPushDir = Normal; // the slot is cleared on leaving: the push eases out along the last wall
		float MinDepth = TNumericLimits<float>::Max();
		for (const int32 Bone : CoverClearanceBones)
		{
			if (Pose.IsValidIndex(Bone))
			{
				// Only where the wall is: past an exposed edge (the corner fire stance) nothing lies behind the face plane.
				const FVector BoneWorld = ToWorld.TransformPosition(Pose[Bone].GetLocation());
				if (CoverFacingRules::IsWithinWallSpan(CoverSlot, BoneWorld, CoverWallClearanceCm))
				{
					MinDepth = FMath::Min(MinDepth, static_cast<float>(FVector::DotProduct(BoneWorld - CoverSlot.WallPoint, Normal)));
				}
			}
		}
		// The measured depth already contains the push applied so far: the push that is still missing (or in excess).
		if (MinDepth < TNumericLimits<float>::Max())
		{
			Target = FMath::Clamp(CoverWallPush + (CoverWallClearanceCm - MinDepth), 0.f, CoverWallClearanceMaxPushCm);
		}
	}
	// Rises fast (a limb in the wall is worse than a short shove), eases out when the need is gone.
	CoverWallPush = Target >= CoverWallPush ? FMath::Min(Target, CoverWallPush + CoverWallClearanceRiseCmPerSec * DeltaTime)
		: FMath::Lerp(CoverWallPush, Target, 1.f - FMath::Exp(-CoverWallClearanceInterp * DeltaTime));
	if (CoverWallPush < 0.05f && Target <= 0.f)
	{
		CoverWallPush = 0.f;
	}
	return CoverWallPush;
}

void AOperativeCharacter::UpdateCoverEntryBlend(float DeltaTime)
{
	FVector Wanted = FVector::ZeroVector;
	if (CoverEntryBlendTime >= 0.f)
	{
		if (!bInCover)
		{
			CoverEntryBlendTime = -1.f; // left again: no offset
		}
		else
		{
			CoverEntryBlendTime += DeltaTime;
			const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
			const float Weight = Anim ? Anim->GetCoverEnterBlendWeight() : -1.f;
			// The clip's start pose stands CoverEntryClipOffset out: while it blends in (weight w) the rest of the pose (the
			// run) must be drawn there too; the glide (momentum + approach error) eases out over CoverEnterDecelSeconds.
			const float W = Weight >= 0.f ? FMath::Clamp(Weight, 0.f, 1.f) : FMath::Clamp(CoverEntryBlendTime / 0.25f, 0.f, 1.f);
			const float S = FMath::Clamp(CoverEntryBlendTime / FMath::Max(CoverEnterDecelSeconds, 0.05f), 0.f, 1.f);
			const FVector ClipPart = Weight >= 0.f || CoverEntryBlendTime < 0.25f ? (1.f - W) * CoverEntryClipOffset : FVector::ZeroVector;
			Wanted = ClipPart + CoverEntryGlide * FMath::Square(1.f - S);
			if ((W >= 1.f || Weight < 0.f) && S >= 1.f && CoverEntryBlendTime >= 0.25f)
			{
				CoverEntryBlendTime = -1.f;
				Wanted = FVector::ZeroVector;
			}
		}
	}
	// Low cover (user request 2026-10-07): the pack's fire stance is a sideways lean round an edge (crouched _L +73 cm,
	// _R -40 cm); over a low cover he fires over the top where he is, so the clips' sideways root step is cancelled.
	// EXCEPT in the corner peek (game designer 2026-10-20: "at the low wall's end he looks fully hidden behind the barricade
	// although the peek sight sees round the corner"): there the root step IS the lean out past the end, so the cancel eases out
	// (CoverLeanCancelAlpha 1 -> 0 over CoverLeanBlendSeconds) while the peek is on and back in when it is over.
	const float CancelTarget = IsPeekingRoundCorner() ? 0.f : 1.f;
	CoverLeanCancelAlpha = FMath::FInterpConstantTo(CoverLeanCancelAlpha, CancelTarget, DeltaTime, 1.f / FMath::Max(CoverLeanBlendSeconds, 0.05f));
	if (bInCover && CurrentCoverHeight == ECoverHeight::LowCover && !bShimmying && GetMesh() && CoverLeanCancelAlpha > 0.f)
	{
		const FVector RootComponentSpace = GetMesh()->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation();
		const FVector RootWorldOffset = GetMesh()->GetComponentTransform().TransformVector(RootComponentSpace);
		const FVector Tangent = CoverSlot.RightTangent();
		Wanted -= Tangent * FVector::DotProduct(RootWorldOffset, Tangent) * CoverLeanCancelAlpha;
	}
	// The corner peek leans a little further out than the clip's own step-out (CornerLeanExtraPack*Cm: the _R clips' head is only 14 cm past the
	// edge): an eased share of it on the mesh while the peek is on, tracked with the other offsets and gone when it is over.
	const bool bLeansOut = IsPeekingRoundCorner();
	CornerLeanExtraAlpha = FMath::FInterpConstantTo(CornerLeanExtraAlpha, bLeansOut ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(CoverLeanBlendSeconds, 0.05f));
	if (bInCover && CornerLeanExtraAlpha > 0.f)
	{
		const float Extra = CoverFacingRules::ClipIndex(CoverFacing) == 0 ? CornerLeanExtraPackLCm : CornerLeanExtraPackRCm;
		Wanted += CoverFacingRules::AlongWallDirection(CoverSlot, CoverFacing) * Extra * CornerLeanExtraAlpha;
	}
	// The body's yaw on entry: (1 - enter clip weight) of the turn the actor made at once is drawn back on the mesh.
	float WantedYaw = 0.f;
	if (bCoverEntryAwaitClip)
	{
		const UOperativeAnimInstance* AwaitAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		// The held yaw eases out WITH the clip's weight: it used to hold fully until 0.5 and drop to 0 in one frame, which
		// swung the pose (pelvis ~1 m off the pivot) round the capsule - a 79 cm pop and arms through the wall.
		// User bug 2026-10-11 (bb3345c): the weight is -1 once the clip is no longer playing - cut short by the auto-snap shimmy
		// to a near corner, or done - and -1 counted as 0, so the full ~180 deg turn stayed on the mesh for good: he stood
		// turned to the wall / round, shimmied "backwards" (the forward clip on a reversed body) and never fired from there.
		const float EnterWeight = AwaitAnim ? AwaitAnim->GetCoverEnterBlendWeight() : -1.f;
		CoverEntryAwaitSeconds += DeltaTime;
		bCoverEntryClipSeen |= EnterWeight >= 0.f;
		const float Held = bInCover && AwaitAnim ? CoverFacingRules::EntryHeldYawFraction(EnterWeight, bCoverEntryClipSeen, CoverEntryAwaitSeconds) : 0.f;
		if (Held > 0.f)
		{
			WantedYaw = CoverEntryYawOffset * Held;
		}
		else
		{
			bCoverEntryAwaitClip = false;
			if (Held < 0.f && !FMath::IsNearlyZero(CoverEntryAppliedYaw, 0.5f))
			{
				// Clip gone before it brought the body round: the rest of the turn eases out over CoverEntryYawBlendSeconds.
				CoverEntryYawOffset = CoverEntryAppliedYaw;
				CoverEntryYawTime = 0.f;
			}
		}
	}
	if (CoverEntryYawTime >= 0.f)
	{
		CoverEntryYawTime += DeltaTime;
		const float W = FMath::Clamp(CoverEntryYawTime / FMath::Max(CoverEntryYawBlendSeconds, 0.05f), 0.f, 1.f);
		const bool bKeep = bInCover || bBodyYawFree;
		WantedYaw = bKeep ? CoverEntryYawOffset * (1.f - FMath::SmoothStep(0.f, 1.f, W)) : 0.f;
		if (!bKeep || W >= 1.f || CoverEntryYawTime > 2.f)
		{
			CoverEntryYawTime = -1.f;
			bBodyYawFree = false;
			WantedYaw = 0.f;
		}
	}
	if (!FMath::IsNearlyEqual(WantedYaw, CoverEntryAppliedYaw, 0.01f) && GetMesh())
	{
		GetMesh()->AddRelativeRotation(FRotator(0.f, WantedYaw - CoverEntryAppliedYaw, 0.f));
		CoverEntryAppliedYaw = WantedYaw;
	}
	// Additive on the mesh's relative location (crouch / other systems move it too), tracked in the actor's local frame
	// so a turn during the blend never leaves a residue.
	const FVector WantedLocal = Wanted.IsNearlyZero(0.01) ? FVector::ZeroVector : GetActorRotation().UnrotateVector(Wanted);
	if (!WantedLocal.Equals(CoverEntryAppliedOffset, 0.01) && GetMesh())
	{
		GetMesh()->AddRelativeLocation(WantedLocal - CoverEntryAppliedOffset);
		CoverEntryAppliedOffset = WantedLocal;
	}
	// Keep every bone out of the wall (the entry blend of an old run pose swings the arms through it): measured after the
	// entry offset / yaw above, so the capsule's jump to the slot is not mistaken for a body in the wall.
	if (GetMesh())
	{
		// Rev 2026-10-19: tracked as the actor-local vector it put on the mesh (as the run-in offset above), so a turn, a teleport or
		// another wall while it is on never leaves a residue.
		const float Push = UpdateCoverWallClearance(DeltaTime);
		const FVector PushLocal = Push <= 0.f ? FVector::ZeroVector : GetActorRotation().UnrotateVector(CoverWallPushDir * Push);
		if (!PushLocal.Equals(CoverWallPushAppliedLocal, 0.01))
		{
			GetMesh()->AddRelativeLocation(PushLocal - CoverWallPushAppliedLocal);
			CoverWallPushAppliedLocal = PushLocal;
		}
	}
}

void AOperativeCharacter::EnforceMeshBaseOffset()
{
	USkeletalMeshComponent* SkinMesh = GetMesh();
	if (!SkinMesh || !bMeshBaseCaptured)
	{
		return;
	}
	const FVector Tracked = CoverEntryAppliedOffset + CoverWallPushAppliedLocal;
	const FVector Location = SkinMesh->GetRelativeLocation();
	const FVector2D Expected = MeshBaseXY + FVector2D(Tracked);
	const float ExpectedYaw = MeshBaseYaw + CoverEntryAppliedYaw;
	const float YawError = FMath::Abs(FRotator::NormalizeAxis(SkinMesh->GetRelativeRotation().Yaw - ExpectedYaw));
	if (FVector2D::Distance(FVector2D(Location), Expected) <= 1.f && YawError <= 0.5f)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Warning, TEXT("%s: mesh %.0f cm / %.1f deg off its place on the capsule (untracked) - put back"), *DisplayName.ToString(),
		FVector2D::Distance(FVector2D(Location), Expected), YawError);
	SkinMesh->SetRelativeLocation(FVector(Expected.X, Expected.Y, Location.Z));
	FRotator Rotation = SkinMesh->GetRelativeRotation();
	Rotation.Yaw = ExpectedYaw;
	SkinMesh->SetRelativeRotation(Rotation);
}

float AOperativeCharacter::GetMeshBaseOffsetCm() const
{
	const USkeletalMeshComponent* SkinMesh = GetMesh();
	return SkinMesh && bMeshBaseCaptured ? static_cast<float>(FVector2D::Distance(FVector2D(SkinMesh->GetRelativeLocation()), MeshBaseXY)) : 0.f;
}

void AOperativeCharacter::ResetBodyOffsets()
{
	CoverEntryBlendTime = -1.f;
	CoverEntryGlide = FVector::ZeroVector;
	CoverEntryClipOffset = FVector::ZeroVector;
	CoverEntryAppliedOffset = FVector::ZeroVector;
	CoverEntryYawTime = -1.f;
	CoverEntryYawOffset = 0.f;
	CoverEntryAppliedYaw = 0.f;
	bCoverEntryAwaitClip = false;
	bBodyYawFree = false;
	CoverWallPush = 0.f;
	CoverWallPushAppliedLocal = FVector::ZeroVector;
	CoverLeanCancelAlpha = 1.f;
	CornerLeanExtraAlpha = 0.f;
	EnforceMeshBaseOffset();
}

void AOperativeCharacter::StartBodyYawBlend(float OldYaw)
{
	// The actor turned at once (a turn-based shot's FaceAimAt): the body keeps OldYaw and turns over CoverEntryYawBlendSeconds.
	if (bInCover || !GetMesh())
	{
		return;
	}
	CoverEntryYawOffset = FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw + CoverEntryAppliedYaw);
	CoverEntryYawTime = FMath::Abs(CoverEntryYawOffset) > 1.f ? 0.f : -1.f;
	bBodyYawFree = CoverEntryYawTime >= 0.f;
}
