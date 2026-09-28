#include "Characters/OperativeAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Survival/ColdSurvivalComponent.h"

namespace
{
	/** Playback rate range for speed-matched clips. */
	constexpr float MinPlayRate = 0.4f;
	constexpr float MaxPlayRate = 2.2f;
	/** Clips below this weight are not sampled. */
	constexpr float MinSampleWeight = 0.005f;

	int32 Slot(EOperativeClip Clip)
	{
		return static_cast<int32>(Clip);
	}
}

FAnimInstanceProxy* UOperativeAnimInstance::CreateAnimInstanceProxy()
{
	return new FOperativeAnimInstanceProxy(this);
}

void UOperativeAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateState();
	UpdateNativeBlend(DeltaSeconds);
}

void UOperativeAnimInstance::UpdateState()
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	if (!Operative)
	{
		return;
	}
	const FVector Velocity = Operative->GetVelocity();
	Speed = Velocity.Size2D();
	Direction = Speed > 1.f
		? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Operative->GetActorRotation().Yaw)
		: 0.f;
	bIsMoving = Speed > 5.f;
	bIsSprinting = Operative->IsSprinting();
	Stance = Operative->GetStance();
	bIsCrouching = Stance == EOperativeStance::Crouching;
	bIsProne = Stance == EOperativeStance::Prone;
	bIsReloading = Operative->bIsReloading;
	bIsDead = Operative->HealthComponent && !Operative->HealthComponent->IsAlive();
	if (const UColdSurvivalComponent* Cold = Operative->ColdSurvival)
	{
		ColdTier = Cold->GetTier();
		bIsFrostbitten = Cold->IsFrostbitten();
		bIsWeaponFrozen = Cold->IsWeaponFrozen();
	}
}

UAnimSequence* UOperativeAnimInstance::GetClip(EOperativeClip Clip) const
{
	switch (Clip)
	{
	case EOperativeClip::Idle: return Animations.Idle;
	case EOperativeClip::Walk: return Animations.Walk;
	case EOperativeClip::Run: return Animations.Run;
	case EOperativeClip::CrouchIdle: return Animations.CrouchIdle;
	case EOperativeClip::CrouchWalk: return Animations.CrouchWalk;
	case EOperativeClip::ProneIdle: return Animations.ProneIdle;
	case EOperativeClip::ProneCrawl: return Animations.ProneCrawl;
	default: return nullptr;
	}
}

void UOperativeAnimInstance::UpdateNativeBlend(float DeltaSeconds)
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FOperativeMovementConfig Movement = Operative ? Operative->MovementConfig : FOperativeMovementConfig();

	// Stance crossfade.
	const FVector3f TargetStance(Stance == EOperativeStance::Standing ? 1.f : 0.f, bIsCrouching ? 1.f : 0.f, bIsProne ? 1.f : 0.f);
	const float StanceStep = DeltaSeconds / StanceBlendTime;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		StanceWeights[Axis] = FMath::FInterpConstantTo(StanceWeights[Axis], TargetStance[Axis], 1.f, StanceStep);
	}

	// Idle -> walk -> run by speed, using the gameplay speeds as blend anchors (Godot BlendSpace1D points).
	const float ProneSpeed = Movement.WalkSpeed * Movement.ProneSpeedMultiplier;
	const FVector3f TargetMove(
		Speed <= Movement.WalkSpeed
			? Speed / Movement.WalkSpeed
			: 1.f + FMath::Clamp((Speed - Movement.WalkSpeed) / FMath::Max(Movement.RunSpeed - Movement.WalkSpeed, 1.f), 0.f, 1.f),
		FMath::Clamp(Speed / Movement.CrouchSpeed, 0.f, 1.f),
		FMath::Clamp(Speed / FMath::Max(ProneSpeed, 1.f), 0.f, 1.f));
	const float MoveAlpha = FMath::Clamp(LocomotionBlendSpeed * DeltaSeconds, 0.f, 1.f);
	MoveBlend = FMath::Lerp(MoveBlend, TargetMove, MoveAlpha);

	const float StandWalk = MoveBlend.X <= 1.f ? MoveBlend.X : 2.f - MoveBlend.X;
	const float StandRun = FMath::Max(MoveBlend.X - 1.f, 0.f);
	ClipWeights[Slot(EOperativeClip::Idle)] = StanceWeights.X * FMath::Max(1.f - MoveBlend.X, 0.f);
	ClipWeights[Slot(EOperativeClip::Walk)] = StanceWeights.X * StandWalk;
	ClipWeights[Slot(EOperativeClip::Run)] = StanceWeights.X * StandRun;
	ClipWeights[Slot(EOperativeClip::CrouchIdle)] = StanceWeights.Y * (1.f - MoveBlend.Y);
	ClipWeights[Slot(EOperativeClip::CrouchWalk)] = StanceWeights.Y * MoveBlend.Y;
	ClipWeights[Slot(EOperativeClip::ProneIdle)] = StanceWeights.Z * (1.f - MoveBlend.Z);
	ClipWeights[Slot(EOperativeClip::ProneCrawl)] = StanceWeights.Z * MoveBlend.Z;

	// Advance clip times; moving clips are speed-matched to limit foot sliding.
	auto Rate = [this](float ClipSpeed) { return Speed > 1.f ? FMath::Clamp(Speed / ClipSpeed, MinPlayRate, MaxPlayRate) : 1.f; };
	const float Rates[] = { 1.f, Rate(Animations.WalkClipSpeed), Rate(Animations.RunClipSpeed), 1.f,
		Rate(Animations.CrouchWalkClipSpeed), 1.f, Rate(Animations.CrawlClipSpeed) };
	for (int32 Index = 0; Index < Slot(EOperativeClip::Count); ++Index)
	{
		if (const UAnimSequence* Sequence = GetClip(static_cast<EOperativeClip>(Index)))
		{
			const float Length = Sequence->GetPlayLength();
			ClipTimes[Index] = Length > 0.f ? FMath::Fmod(ClipTimes[Index] + DeltaSeconds * Rates[Index], Length) : 0.f;
		}
	}
}

void FOperativeAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UOperativeAnimInstance* Instance = CastChecked<UOperativeAnimInstance>(InAnimInstance);
	bUseNative = Instance->bUseNativeLocomotion;
	for (int32 Index = 0; Index < Slot(EOperativeClip::Count); ++Index)
	{
		Clips[Index].Sequence = Instance->GetClip(static_cast<EOperativeClip>(Index));
		Clips[Index].Time = Instance->ClipTimes[Index];
		Clips[Index].Weight = Instance->ClipWeights[Index];
	}
}

bool FOperativeAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (!bUseNative && HasRootNode())
	{
		return false; // the AnimBP graph drives the pose
	}

	constexpr int32 MaxClips = static_cast<int32>(EOperativeClip::Count);
	TArray<FCompactPose, TInlineAllocator<MaxClips>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<MaxClips>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<MaxClips>> Attributes;
	TArray<float, TInlineAllocator<MaxClips>> Weights;
	Poses.Reserve(MaxClips);
	Curves.Reserve(MaxClips);
	Attributes.Reserve(MaxClips);

	float TotalWeight = 0.f;
	for (const FOperativeClipState& Clip : Clips)
	{
		if (!Clip.Sequence || Clip.Weight < MinSampleWeight)
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
		Weights.Add(Clip.Weight);
		TotalWeight += Clip.Weight;
	}

	if (Poses.IsEmpty() || TotalWeight <= KINDA_SMALL_NUMBER)
	{
		Output.ResetToRefPose();
		return true;
	}
	for (float& Weight : Weights)
	{
		Weight /= TotalWeight;
	}
	FAnimationPoseData OutputData(Output);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, OutputData);
	return true;
}
