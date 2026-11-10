// UOperativeAnimInstance: the prone crawl layer (the game designer's CrawlAndShoot_Bug_01, 2026-11-09, fix/prone-crawl-under-fire).
// "When he is told to crawl away, he must crawl first. Use the 8-direction crawl animations in the pack Crawl_MocapAnimPack. He keeps his body
// facing the danger but moves in the direction we ordered." The pack's in-place loops Crawl_Walk_{F,45R,R,135R,B,135L,L,45L}_IPC + Crawl_Idle01
// (weapon_anim_sets.json "prone_crawl"; IK-retargeted copies /Game/Characters/Animations/Crawl/{Male,Female} by
// Scripts/Editor/setup_crawl_animation.py - the pack itself is never touched) blended natively over ABP_Operative's prone pose, full body, for
// every weapon (the weapon stays on its hand socket) - no AnimGraph node, the user's ABP stays as it is. UE-only (Godot had one crawl clip).
// The gameplay half (the fire hold, the facing) is AOperativeCharacter's (OperativeCharacterCombat.cpp / OperativeCharacterAim.cpp,
// ProneCrawlRules).

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProneCrawlRules.h"
#include "Characters/WeaponLayerHandoffRules.h"
#include "HAL/IConsoleManager.h"

namespace OperativeProneCrawlLayer
{
	constexpr float CrawlMinWeight = 0.005f;

	TAutoConsoleVariable<int32> CVarProneCrawlLayer(TEXT("Codex.Anim.ProneCrawlLayer"), 1,
		TEXT("1 = the native 8-direction prone crawl layer (Crawl_MocapAnimPack, weapon_anim_sets.json \"prone_crawl\") drives the crawling body; 0 = off (the ABP's prone locomotion; dev A/B)."));

	/** The directional set's availability mask. */
	TStaticArray<bool, 8> Available(const TStaticArray<UAnimSequence*, 8>& Set)
	{
		TStaticArray<bool, 8> Out;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out[Index] = Set[Index] != nullptr;
		}
		return Out;
	}
}

bool UOperativeAnimInstance::IsProneCrawlEligible() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	return Operative && CrawlSetData.StandIdle && OperativeProneCrawlLayer::CVarProneCrawlLayer.GetValueOnGameThread() != 0 && bIsProne && !bIsDead
		&& !bKnockedDown && !bIsClimbing && !bIsVaulting && !bInCover && !bFallAirborne && !bSniperPose && !Operative->bCarrying && !IsPlayingStanceTransition();
}

const UAnimSequenceBase* UOperativeAnimInstance::GetProneCrawlDominantClip() const
{
	const FOperativeClipState* Best = nullptr;
	for (const FOperativeClipState& Clip : CrawlClips)
	{
		Best = Clip.Sequence && (!Best || Clip.Weight > Best->Weight) ? &Clip : Best;
	}
	return Best && CrawlWeight > OperativeProneCrawlLayer::CrawlMinWeight ? Best->Sequence.Get() : nullptr;
}

void UOperativeAnimInstance::UpdateProneCrawlLayer(float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FShotgunAnimSet& Set = CrawlSetData;
	for (FOperativeClipState& Clip : CrawlClips)
	{
		Clip = FOperativeClipState();
	}
	if (!Operative)
	{
		CrawlWeight = 0.f;
		return;
	}
	const bool bEligible = IsProneCrawlEligible();
	const bool bOrdered = bEligible && Operative->IsOrderedCrawl();
	// Crawl first: a prone fire clip still playing out from before the order (a full-body clip: the body lay still in it) gives way at once.
	if (bOrdered && FireProneAnimation && IsPlayingSlotAnimation(FireProneAnimation, FullBodySlot))
	{
		StopSlotAnimation(0.15f, FullBodySlot);
		++CrawlFireClipsStopped;
	}
	// A full-body clip owns the body (a prone hit / reload, the death): the layer fades out under it.
	const bool bFullBodyClip = GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f;
	const float Target = ProneCrawlRules::LayerTarget(bEligible && !bFullBodyClip, bOrdered, LocomotionSpeed, Set.MoveStartSpeed);
	CrawlWeight = FMath::FInterpConstantTo(CrawlWeight, Target, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds));
	if (CrawlWeight <= OperativeProneCrawlLayer::CrawlMinWeight)
	{
		CrawlDirection = FWeaponLayerDirectionState();
		CrawlPlayRate = 0.f;
		return;
	}
	// The direction: the velocity relative to the body (the body faces the threat, the move goes the ordered way), eased and cross-faded on a
	// reversal like a full-body weapon set (WeaponLayerHandoffRules::StepLayerDirection). Its own, not the ABP's Direction: that one is held
	// below 30 cm/s and a frostbitten / wounded crawl is slower.
	const FVector Velocity = Operative->GetVelocity();
	const float MoveSpeed = static_cast<float>(Velocity.Size2D());
	if (MoveSpeed > 3.f || !CrawlDirection.bValid)
	{
		const float Raw = MoveSpeed > 3.f ? ProneCrawlRules::RelativeMoveDeg(static_cast<float>(Operative->GetActorRotation().Yaw), static_cast<float>(Velocity.Rotation().Yaw))
			: 0.f;
		WeaponLayerHandoffRules::StepLayerDirection(CrawlDirection, Raw, DeltaSeconds, Set.DirectionSmoothSeconds, Set.DirectionDeadZoneDeg,
			Set.DirectionSnapDeg, Set.DirectionFadeSeconds);
	}
	const TStaticArray<bool, 8> Avail = OperativeProneCrawlLayer::Available(Set.Walk);
	const FWeaponLayerDirectionBlend Dir = BlendDirections(CrawlDirection.Current, Avail);
	const FWeaponLayerDirectionBlend FromDir = BlendDirections(CrawlDirection.From, Avail);
	const float FromShare = 1.f - CrawlDirection.Fade;
	// Idle against the crawl by the smoothed speed (a pause on the path lies in the crawl idle, not in a frozen crawl frame).
	const float Move = MoveWeight(LocomotionSpeed, Set.MoveStartSpeed, Set.MoveFullSpeed);
	CrawlIdleTime = Set.StandIdle && Set.StandIdle->GetPlayLength() > KINDA_SMALL_NUMBER
		? FMath::Fmod(CrawlIdleTime + DeltaSeconds, Set.StandIdle->GetPlayLength()) : 0.f;
	int32 Next = 0;
	auto Add = [this, &Next](UAnimSequence* Clip, float Weight, float Time)
	{
		if (Clip && Weight > OperativeProneCrawlLayer::CrawlMinWeight && Next < CrawlClips.Num())
		{
			CrawlClips[Next++] = { Clip, Time, Weight };
		}
	};
	Add(Set.StandIdle, 1.f - Move, CrawlIdleTime);
	// One shared phase: the hands / knees of every loop in play cover their own stride per cycle (the cycle = each loop's authored crawl speed x
	// its length, the two directions blended like the clips, the old direction's during a reversal's cross-fade - so the clips that still show
	// keep their contacts planted), played at the body's real speed.
	auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
	auto CycleOf = [&Set, &Length](const FWeaponLayerDirectionBlend& Blend)
	{
		const float Nominal = BlendedNominalSpeed(Blend, Set.WalkSpeeds, Set.WalkSpeed);
		return Nominal * FMath::Lerp(Length(Set.Walk[static_cast<int32>(Blend.A)]), Length(Set.Walk[static_cast<int32>(Blend.B)]), Blend.Alpha);
	};
	UAnimSequence* Lead = Set.Walk[static_cast<int32>(Dir.Alpha < 0.5f ? Dir.A : Dir.B)];
	const float LeadLength = Length(Lead);
	const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, CycleOf(Dir), CycleOf(FromDir), FromShare, Set.MaxPlayRate, LeadLength);
	CrawlPhase = FMath::Fmod(CrawlPhase + DeltaSeconds * Rate, 1.f);
	CrawlPlayRate = Rate * LeadLength;
	auto At = [this, &Set](const UAnimSequence* Clip)
	{
		return Clip ? SyncedClipTime(CrawlPhase, Set.PhaseOffsets.FindRef(Clip->GetFName()), Clip->GetPlayLength()) : 0.f;
	};
	auto AddPair = [&Add, &At, &Set](const FWeaponLayerDirectionBlend& Blend, float Weight)
	{
		UAnimSequence* A = Set.Walk[static_cast<int32>(Blend.A)];
		UAnimSequence* B = Set.Walk[static_cast<int32>(Blend.B)];
		Add(A, Weight * (1.f - Blend.Alpha), At(A));
		Add(B, Weight * Blend.Alpha, At(B));
	};
	AddPair(Dir, Move * (1.f - FromShare));
	if (FromShare > OperativeProneCrawlLayer::CrawlMinWeight)
	{
		AddPair(FromDir, Move * FromShare);
	}
}

void FOperativeAnimInstanceProxy::BlendProneCrawlLayer(FPoseContext& Output) const
{
	// The layer's pose: its clips blended by weight (the same sampling as the weapon layer). A fixed inline budget: a crawl blend is at most six
	// clips, so nothing here allocates on the heap.
	TArray<FCompactPose, TInlineAllocator<6>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<6>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<6>> Attributes;
	TArray<float, TInlineAllocator<6>> Weights;
	float Total = 0.f;
	for (const FOperativeClipState& Clip : CrawlClips)
	{
		if (!Clip.Sequence || Clip.Weight < OperativeProneCrawlLayer::CrawlMinWeight)
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
	// Every bone but the root from the crawl (the pelvis faces the capsule: the body faces the threat, the clip's direction moves it); the root
	// keeps its reference transform - the ABP's prone root yaw / orientation warping turned towards the movement for ITS forward crawl and would
	// turn the strafing body off the threat (the same as the launcher's full-body set).
	const float Weight = FMath::Min(1.f, CrawlWeight);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		if (Index.IsRootBone())
		{
			const FTransform& Reference = Bones.GetRefPoseTransform(Index);
			Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Reference.GetRotation(), Weight).GetNormalized());
			Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Reference.GetTranslation(), Weight));
			continue;
		}
		Output.Pose[Index].BlendWith(Layer[Index], Weight);
	}
}
