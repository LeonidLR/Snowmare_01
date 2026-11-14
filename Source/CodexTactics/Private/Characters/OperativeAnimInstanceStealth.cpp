// Designer task 003: native stealth locomotion / wall / lean poses. UE-only; no Godot equivalent.
// All clips are the approved retargeted copies; the user's packs and Animation Blueprint remain unchanged.
#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "AnimationRuntime.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProneCrawlRules.h"
#include "Characters/WeaponLayerHandoffRules.h"

namespace OperativeStealthLayer
{
	constexpr float StealthMinWeight = 0.005f;
	static const TCHAR* const DirectionNames[8] = { TEXT("F"), TEXT("FR"), TEXT("R"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("L"), TEXT("FL") };
}

UAnimSequenceBase* UOperativeAnimInstance::GetStealthCrouchClip() const
{
	return StealthSetData.NamedClips.FindRef(TEXT("Stealth_Crouch_IP"));
}

const UAnimSequenceBase* UOperativeAnimInstance::GetStealthDominantClip() const
{
	const FOperativeClipState* Best = nullptr;
	for (const FOperativeClipState& Clip : StealthClips)
	{
		Best = Clip.Sequence && (!Best || Clip.Weight > Best->Weight) ? &Clip : Best;
	}
	return Best && StealthWeight > OperativeStealthLayer::StealthMinWeight ? Best->Sequence.Get() : nullptr;
}

void UOperativeAnimInstance::UpdateStealthLayer(float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FShotgunAnimSet& Set = StealthSetData;
	for (FOperativeClipState& Clip : StealthClips)
	{
		Clip = FOperativeClipState();
	}
	StealthFireBlockSeconds = FMath::Max(0.f, StealthFireBlockSeconds - DeltaSeconds);
	const bool bActive = Operative && Operative->IsInStealthMode() && Set.StandIdle && bIsCrouching && !bIsProne
		&& !bIsDead && !bKnockedDown && !bIsClimbing && !bIsVaulting && !bFallAirborne && !Operative->bCarrying;
	// Fire / reload / hits keep the weapon pose, even if stealth_break_on_fire is tuned off.
	const bool bCoverLoop = bInCover && CoverLoopMontage.IsValid() && Montage_IsPlaying(CoverLoopMontage.Get())
		&& !(CoverOneShotMontage.IsValid() && Montage_IsPlaying(CoverOneShotMontage.Get()));
	const bool bBodyBusy = IsPlayingStanceTransition() || bIsReloading || (GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f && !bCoverLoop)
		|| GetSlotMontageGlobalWeight(UpperBodySlot) > 0.01f || StealthFireBlockSeconds > 0.f
		|| (!bInCover && bIsAiming && AimTimer > 0.f);
	if (!Operative || bIsDead || bKnockedDown || bIsProne || bIsClimbing || bIsVaulting)
	{
		StealthWeight = 0.f;
	}
	else
	{
		StealthWeight = FMath::FInterpConstantTo(StealthWeight, bActive && !bBodyBusy ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds));
	}
	if (!bActive && StealthWeight <= OperativeStealthLayer::StealthMinWeight)
	{
		bStealthWasActive = bStealthWasMoving = bStealthWasAtWall = bStealthWasPeeking = false;
		StealthAction = nullptr;
		StealthDirection = FWeaponLayerDirectionState();
		return;
	}
	if (bBodyBusy)
	{
		StealthAction = nullptr;
	}
	const FVector Velocity = Operative->GetVelocity();
	const float MoveSpeed = static_cast<float>(Velocity.Size2D());
	const bool bMoving = MoveSpeed > Set.MoveStartSpeed;
	const bool bWall = bInCover;
	const bool bPeeking = bWall && Operative->IsPeekingRoundCorner();
	const FString WallPrefix = CoverFacing == ECoverFacing::Left ? TEXT("Stealth_LeftWall_") : TEXT("Stealth_RightWall_");
	auto Named = [&Set](const FString& Name) { return Set.NamedClips.FindRef(FName(*Name)); };
	auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
	if (MoveSpeed > 3.f || !StealthDirection.bValid)
	{
		const float Raw = MoveSpeed > 3.f ? ProneCrawlRules::RelativeMoveDeg(static_cast<float>(Operative->GetActorRotation().Yaw), static_cast<float>(Velocity.Rotation().Yaw)) : 0.f;
		WeaponLayerHandoffRules::StepLayerDirection(StealthDirection, Raw, DeltaSeconds, Set.DirectionSmoothSeconds, Set.DirectionDeadZoneDeg,
			Set.DirectionSnapDeg, Set.DirectionFadeSeconds);
	}
	TStaticArray<bool, 8> Available;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Available[Index] = Set.Walk[Index] && Set.Jog[Index];
	}
	const FWeaponLayerDirectionBlend Dir = BlendDirections(StealthDirection.Current, Available);
	const FWeaponLayerDirectionBlend FromDir = BlendDirections(StealthDirection.From, Available);
	const int32 LeadIndex = static_cast<int32>(Dir.Alpha < 0.5f ? Dir.A : Dir.B);
	const float SlowSpeed = BlendedNominalSpeed(Dir, Set.WalkSpeeds, Set.WalkSpeed);
	const float FastSpeed = BlendedNominalSpeed(Dir, Set.JogSpeeds, Set.JogSpeed);
	const float Fast = FMath::Clamp((LocomotionSpeed - SlowSpeed) / FMath::Max(1.f, FastSpeed - SlowSpeed), 0.f, 1.f);
	auto BeginAction = [this](UAnimSequence* Clip)
	{
		StealthAction = Clip;
		StealthActionTime = 0.f;
	};
	const float Yaw = static_cast<float>(Operative->GetActorRotation().Yaw);
	const float YawDelta = FMath::FindDeltaAngleDegrees(StealthLastYaw, Yaw);
	if (bActive && !bBodyBusy)
	{
		if (bPeeking != bStealthWasPeeking)
		{
			BeginAction(Named(WallPrefix + (bPeeking ? TEXT("Lean_Start_IP") : TEXT("Lean_Stop_IP"))));
		}
		else if (bWall && bStealthWasAtWall && CoverFacing != StealthLastSide)
		{
			BeginAction(Named(WallPrefix + TEXT("Turn_180_IP")));
		}
		else if (!bPeeking && bMoving != bStealthWasMoving)
		{
			if (bWall)
			{
				BeginAction(Named(WallPrefix + (bMoving ? (Fast > 0.5f ? TEXT("Fast_Start_IP") : TEXT("Slow_Start_IP")) : TEXT("Stop_IP"))));
			}
			else if (bMoving)
			{
				BeginAction(Named(FString::Printf(TEXT("Stealth_%s_%s_Start_IP"), Fast > 0.5f ? TEXT("Fast") : TEXT("Slow"), OperativeStealthLayer::DirectionNames[LeadIndex])));
			}
			else
			{
				const int32 Cardinal = ((LeadIndex + 1) / 2 * 2) % 8;
				BeginAction(Named(FString::Printf(TEXT("Stealth_Stop_%s_IP"), OperativeStealthLayer::DirectionNames[Cardinal])));
			}
		}
		else if (!bWall && !bMoving && bStealthWasActive && !StealthAction && FMath::Abs(YawDelta) > 45.f)
		{
			BeginAction(Named(FMath::Abs(YawDelta) > 135.f ? TEXT("Stealth_Turn_180_IP") : YawDelta < 0.f ? TEXT("Stealth_Turn_90_L_IP") : TEXT("Stealth_Turn_90_R_IP")));
		}
	}
	if (bWall != bStealthWasAtWall || (bMoving && bPeeking))
	{
		StealthAction = nullptr; // never carry a free locomotion start into a wall, or a lean into a walk
	}
	if (!bBodyBusy)
	{
		bStealthWasActive = bActive;
		bStealthWasMoving = bMoving;
		bStealthWasPeeking = bPeeking;
		bStealthWasAtWall = bWall;
		StealthLastSide = CoverFacing;
		StealthLastYaw = Yaw;
	}

	int32 Next = 0;
	auto Add = [this, &Next](UAnimSequence* Clip, float Weight, float Time)
	{
		if (Clip && Weight > OperativeStealthLayer::StealthMinWeight && Next < StealthClips.Num())
		{
			StealthClips[Next++] = { Clip, Time, Weight };
		}
	};
	float ActionShare = 0.f;
	if (StealthAction)
	{
		const float ActionLength = Length(StealthAction);
		const bool bGaitAction = bMoving && !bPeeking;
		StealthActionTime += DeltaSeconds * (bGaitAction ? FMath::Clamp(MoveSpeed / FMath::Lerp(SlowSpeed, FastSpeed, Fast), 0.2f, Set.MaxPlayRate) : 1.f);
		if (StealthActionTime >= ActionLength)
		{
			StealthAction = nullptr;
		}
		else
		{
			ActionShare = FMath::Clamp(FMath::Min(StealthActionTime, ActionLength - StealthActionTime) / FMath::Min(0.12f, ActionLength * 0.25f), 0.f, 1.f);
			Add(StealthAction, ActionShare, StealthActionTime);
		}
	}
	const float Base = 1.f - ActionShare;
	if (bPeeking)
	{
		UAnimSequence* Lean = Named(WallPrefix + TEXT("Lean_IP"));
		StealthIdleTime = FMath::Fmod(StealthIdleTime + DeltaSeconds, Length(Lean));
		Add(Lean, Base, StealthIdleTime);
		return;
	}
	UAnimSequence* Idle = bWall ? Named(WallPrefix + TEXT("Idle_IP")) : Set.StandIdle;
	StealthIdleTime = FMath::Fmod(StealthIdleTime + DeltaSeconds, Length(Idle));
	const float Move = MoveWeight(LocomotionSpeed, Set.MoveStartSpeed, Set.MoveFullSpeed);
	Add(Idle, Base * (1.f - Move), StealthIdleTime);
	auto At = [this, &Set](UAnimSequence* Clip)
	{
		return Clip ? SyncedClipTime(StealthPhase, Set.PhaseOffsets.FindRef(Clip->GetFName()), Clip->GetPlayLength()) : 0.f;
	};
	if (bWall)
	{
		UAnimSequence* Slow = Named(WallPrefix + TEXT("Slow_IP"));
		UAnimSequence* Quick = Named(WallPrefix + TEXT("Fast_IP"));
		const float Cycle = FMath::Lerp(FMath::Max(1.f, Set.ClipSpeeds.FindRef(Slow ? Slow->GetFName() : NAME_None)) * Length(Slow),
			FMath::Max(1.f, Set.ClipSpeeds.FindRef(Quick ? Quick->GetFName() : NAME_None)) * Length(Quick), Fast);
		const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, Cycle, Cycle, 0.f, Set.MaxPlayRate, Length(Slow));
		// The capsule faces the wall normal, so dot(Velocity, Forward) is nearly zero while shimmying.
		const float Sign = Operative->bShimmying && !Operative->IsShimmyForward() ? -1.f : 1.f;
		StealthPhase = FMath::Fmod(StealthPhase + DeltaSeconds * Rate * Sign + 1.f, 1.f);
		Add(Slow, Base * Move * (1.f - Fast), At(Slow));
		Add(Quick, Base * Move * Fast, At(Quick));
		return;
	}
	auto CycleOf = [&Set, &Length, Fast](const FWeaponLayerDirectionBlend& Blend)
	{
		auto Cycle = [&Blend, &Length](const TStaticArray<UAnimSequence*, 8>& Clips, const TStaticArray<float, 8>& Speeds, float Fallback)
		{
			return BlendedNominalSpeed(Blend, Speeds, Fallback) * FMath::Lerp(Length(Clips[static_cast<int32>(Blend.A)]), Length(Clips[static_cast<int32>(Blend.B)]), Blend.Alpha);
		};
		return FMath::Lerp(Cycle(Set.Walk, Set.WalkSpeeds, Set.WalkSpeed), Cycle(Set.Jog, Set.JogSpeeds, Set.JogSpeed), Fast);
	};
	const float FromShare = 1.f - StealthDirection.Fade;
	const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, CycleOf(Dir), CycleOf(FromDir), FromShare, Set.MaxPlayRate, Length(Set.Walk[LeadIndex]));
	StealthPhase = FMath::Fmod(StealthPhase + DeltaSeconds * Rate, 1.f);
	auto AddPair = [&Add, &At](const TStaticArray<UAnimSequence*, 8>& Clips, const FWeaponLayerDirectionBlend& Blend, float Weight)
	{
		UAnimSequence* A = Clips[static_cast<int32>(Blend.A)];
		UAnimSequence* B = Clips[static_cast<int32>(Blend.B)];
		Add(A, Weight * (1.f - Blend.Alpha), At(A));
		Add(B, Weight * Blend.Alpha, At(B));
	};
	AddPair(Set.Walk, Dir, Base * Move * (1.f - Fast) * (1.f - FromShare));
	AddPair(Set.Jog, Dir, Base * Move * Fast * (1.f - FromShare));
	AddPair(Set.Walk, FromDir, Base * Move * (1.f - Fast) * FromShare);
	AddPair(Set.Jog, FromDir, Base * Move * Fast * FromShare);
}

void FOperativeAnimInstanceProxy::BlendStealthLayer(FPoseContext& Output) const
{
	// The fixed snapshot budget includes two gait directions, the fading direction, idle and one action.
	TArray<FCompactPose, TInlineAllocator<12>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<12>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<12>> Attributes;
	TArray<float, TInlineAllocator<12>> Weights;
	float Total = 0.f;
	for (const FOperativeClipState& Clip : StealthClips)
	{
		if (!Clip.Sequence || Clip.Weight < OperativeStealthLayer::StealthMinWeight)
		{
			continue;
		}
		FCompactPose& Pose = Poses.AddDefaulted_GetRef();
		Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve& Curve = Curves.AddDefaulted_GetRef();
		Curve.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer& Attribute = Attributes.AddDefaulted_GetRef();
		FAnimationPoseData PoseData(Pose, Curve, Attribute);
		Clip.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Clip.Time), false));
		// Stealth wall clips face along the wall; M4 clips encode that turn in their bones.
		// Keep the capsule's normal-facing cover contract and rotate only this sampled pose.
		const FString ClipName = Clip.Sequence->GetName();
		const float WallYaw = ClipName.StartsWith(TEXT("Stealth_LeftWall_")) ? -90.f
			: ClipName.StartsWith(TEXT("Stealth_RightWall_")) ? 90.f : 0.f;
		const FCompactPoseBoneIndex RootIndex(0);
		const FTransform& RootReference = Output.Pose.GetBoneContainer().GetRefPoseTransform(RootIndex);
		Pose[RootIndex] = RootReference;
		Pose[RootIndex].SetRotation((FQuat(FVector::UpVector, FMath::DegreesToRadians(WallYaw)) * RootReference.GetRotation()).GetNormalized());
		Weights.Add(Clip.Weight);
		Total += Clip.Weight;
	}
	if (Poses.IsEmpty() || Total <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	for (float& Weight : Weights)
	{
		Weight /= Total;
	}
	FCompactPose Layer;
	Layer.SetBoneContainer(&Output.Pose.GetBoneContainer());
	FBlendedCurve LayerCurve;
	LayerCurve.InitFrom(Output.Curve);
	UE::Anim::FStackAttributeContainer LayerAttributes;
	FAnimationPoseData LayerData(Layer, LayerCurve, LayerAttributes);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, LayerData);
	// Full body with an in-place root: the capsule owns translation; wall clips supply their along-wall basis.
	const float Weight = FMath::Min(1.f, StealthWeight);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		if (Index.IsRootBone())
		{
			const FTransform& Reference = Bones.GetRefPoseTransform(Index);
			Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Layer[Index].GetRotation(), Weight).GetNormalized());
			Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Reference.GetTranslation(), Weight));
			continue;
		}
		Output.Pose[Index].BlendWith(Layer[Index], Weight);
	}
}

