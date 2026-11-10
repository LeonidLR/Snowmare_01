// UOperativeAnimInstance: the injured locomotion layer (the game designer 2026-11-10: "operatives sometimes walk very slowly and I cannot see why -
// give a wounded operative a visible INJURED walk"; feat/injured-locomotion). UE-only (Godot had no injured walk).
// The designer's /Game/CON_Injured UE4-mannequin set (weapon_anim_sets.json "injured"; IK-retargeted copies /Game/Characters/Animations/Injured/
// {Male,Female} by Scripts/Editor/setup_injured_animation.py - the pack itself is never touched) blended natively over ABP_Operative, built like
// the prone crawl layer (OperativeAnimInstanceProneCrawl.cpp): no AnimGraph node, the user's ABP stays as it is.
// - Wounded (IsWounded) standing: the 8-direction injured walk (Walk_Injured_BS clips by the velocity relative to the body, one shared phase,
//   per-loop left-foot strike offsets) + the injured idle. Prone: the prone crawl layer plays the injured crawl forward (ActiveCrawlSet).
// - Cold >= "cold_from" walking (no wound): the slow injured walk (Walk_Injured_Slow) by a forward cone; still = the ABP's shiver idle.
// - LOWER body only (root -> the reference, pelvis, legs, foot IK bones): the clips are unarmed (hands on the wound, measured by the setup
//   script), so the torso stays the ABP's / the weapon layer's - the weapon pose, the aim offset, fire / reload / grenade / blizzard clips.
// Rules: Characters/InjuredLocomotionRules.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/InjuredLocomotionRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProneCrawlRules.h"
#include "Characters/WeaponLayerHandoffRules.h"
#include "HAL/IConsoleManager.h"

namespace OperativeInjuredLayer
{
	constexpr float InjuredMinWeight = 0.005f;

	TAutoConsoleVariable<int32> CVarInjuredLayer(TEXT("Codex.Anim.InjuredLayer"), 1,
		TEXT("1 = the native injured locomotion layer (CON_Injured, weapon_anim_sets.json \"injured\"): wounded = the injured walk / idle / crawl, cold = the slow injured walk; 0 = off (the ABP's locomotion; dev A/B)."));

	/** Bone's component-space rotation in Pose: its local rotations up the chain. */
	FQuat InjuredChainRotation(const FCompactPose& Pose, FCompactPoseBoneIndex Bone)
	{
		FQuat Rotation = FQuat::Identity;
		for (; Bone.GetInt() != INDEX_NONE; Bone = Pose.GetParentBoneIndex(Bone))
		{
			Rotation = Pose[Bone].GetRotation() * Rotation;
		}
		return Rotation;
	}

	FCompactPoseBoneIndex InjuredBone(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	}
}

void UOperativeAnimInstance::LoadInjuredAnimSet()
{
	LoadWeaponAnimSet(TEXT("injured"), InjuredSetData, InjuredRetargetFolder);
	// The injured crawl = the prone crawl set with the injured crawl in its forward slot and the injured crawl idle (the other directions keep the
	// mocap crawl - the pack has one forward crawl); its own authored speed and left-hand reach offset.
	InjuredCrawlSetData = CrawlSetData;
	bHasInjuredCrawl = CrawlSetData.StandIdle && CrawlSetData.Walk[0] && InjuredSetData.InjuredCrawl && InjuredSetData.InjuredCrawlIdle;
	if (bHasInjuredCrawl)
	{
		InjuredCrawlSetData.Walk[0] = InjuredSetData.InjuredCrawl;
		InjuredCrawlSetData.WalkSpeeds[0] = InjuredSetData.InjuredCrawlSpeed;
		InjuredCrawlSetData.StandIdle = InjuredSetData.InjuredCrawlIdle;
		if (const float* Offset = InjuredSetData.PhaseOffsets.Find(InjuredSetData.InjuredCrawl->GetFName()))
		{
			InjuredCrawlSetData.PhaseOffsets.Add(InjuredSetData.InjuredCrawl->GetFName(), *Offset);
		}
	}
}

bool UOperativeAnimInstance::IsInjuredLayerEligible() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	return Operative && InjuredSetData.StandIdle && OperativeInjuredLayer::CVarInjuredLayer.GetValueOnGameThread() != 0 && !bIsProne && !bIsCrouching
		&& !bIsDead && !bKnockedDown && !bIsClimbing && !bIsVaulting && !bInCover && !bFallAirborne && !bSniperPose && !Operative->bCarrying
		&& !IsPlayingStanceTransition();
}

const UAnimSequenceBase* UOperativeAnimInstance::GetInjuredDominantClip() const
{
	const FOperativeClipState* Best = nullptr;
	for (const FOperativeClipState& Clip : InjuredClips)
	{
		Best = Clip.Sequence && (!Best || Clip.Weight > Best->Weight) ? &Clip : Best;
	}
	return Best && InjuredWeight > OperativeInjuredLayer::InjuredMinWeight ? Best->Sequence.Get() : nullptr;
}

void UOperativeAnimInstance::UpdateInjuredLayer(float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	using namespace InjuredLocomotionRules;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FShotgunAnimSet& Set = InjuredSetData;
	for (FOperativeClipState& Clip : InjuredClips)
	{
		Clip = FOperativeClipState();
	}
	if (!Operative)
	{
		InjuredWeight = 0.f;
		InjuredGait = EInjuredGait::None;
		bInjuredCrawlActive = false;
		return;
	}
	const bool bLayerOn = OperativeInjuredLayer::CVarInjuredLayer.GetValueOnGameThread() != 0;
	InjuredGait = ChooseGait(Set.Triggers, Operative->IsWounded(), Operative->ColdLevel / 100.f);
	bInjuredCrawlActive = bLayerOn && UsesInjuredCrawl(InjuredGait, bIsProne, bHasInjuredCrawl);
	if (InjuredGait != EInjuredGait::None)
	{
		InjuredClipGait = InjuredGait; // a fading layer keeps the clips of the gait that ended
	}
	// The direction: the velocity relative to the body, eased and cross-faded on a reversal like the crawl (its own: the ABP's Direction is held
	// below 30 cm/s and a wounded, frozen walk is slower).
	const FVector Velocity = Operative->GetVelocity();
	const float MoveSpeed = static_cast<float>(Velocity.Size2D());
	if (MoveSpeed > 3.f || !InjuredDirection.bValid)
	{
		const float Raw = MoveSpeed > 3.f ? ProneCrawlRules::RelativeMoveDeg(static_cast<float>(Operative->GetActorRotation().Yaw), static_cast<float>(Velocity.Rotation().Yaw))
			: 0.f;
		WeaponLayerHandoffRules::StepLayerDirection(InjuredDirection, Raw, DeltaSeconds, Set.DirectionSmoothSeconds, Set.DirectionDeadZoneDeg,
			Set.DirectionSnapDeg, Set.DirectionFadeSeconds);
	}
	const float Move = MoveWeight(LocomotionSpeed, Set.MoveStartSpeed, Set.MoveFullSpeed);
	const float Cone = ForwardConeShare(InjuredDirection.Current, Set.SlowConeFullDeg, Set.SlowConeZeroDeg);
	// A full-body clip owns the body (a hit, the warm idles, an interaction, the death): the layer fades out under it; an upper-body clip (fire,
	// reload, grenade, the blizzard shield) keeps the legs injured.
	const bool bFullBodyClip = GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f;
	const bool bColdWalkable = InjuredGait != EInjuredGait::Cold || Set.SlowWalk != nullptr;
	const float Target = LayerTarget(InjuredGait, IsInjuredLayerEligible() && !bFullBodyClip && bColdWalkable, Move, Cone);
	InjuredWeight = FMath::FInterpConstantTo(InjuredWeight, Target, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds));
	InjuredTorsoShare = TorsoPelvisShare(Set.TorsoPelvisShare, bIsAiming);
	if (InjuredWeight <= OperativeInjuredLayer::InjuredMinWeight)
	{
		InjuredWeight = 0.f;
		InjuredPlayRate = 0.f;
		if (Target <= 0.f)
		{
			InjuredClipGait = InjuredGait;
		}
		return;
	}
	auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
	int32 Next = 0;
	auto Add = [this, &Next](UAnimSequence* Clip, float Weight, float Time)
	{
		if (Clip && Weight > OperativeInjuredLayer::InjuredMinWeight && Next < InjuredClips.Num())
		{
			InjuredClips[Next++] = { Clip, Time, Weight };
		}
	};
	auto At = [this, &Set](const UAnimSequence* Clip)
	{
		return Clip ? SyncedClipTime(InjuredPhase, Set.PhaseOffsets.FindRef(Clip->GetFName()), Clip->GetPlayLength()) : 0.f;
	};
	if (InjuredClipGait == EInjuredGait::Cold)
	{
		// The slow walk alone (the layer's weight already carries the move and the forward cone): played at the body's speed over its stride.
		const float Len = Length(Set.SlowWalk);
		const float MaxRate = Set.SlowMaxPlayRate > 0.f ? Set.SlowMaxPlayRate : Set.MaxPlayRate;
		const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, Set.SlowWalkSpeed * Len, 0.f, 0.f, MaxRate, Len);
		InjuredPhase = FMath::Fmod(InjuredPhase + DeltaSeconds * Rate, 1.f);
		InjuredPlayRate = Rate * Len;
		Add(Set.SlowWalk, 1.f, At(Set.SlowWalk));
		return;
	}
	// Wounded: the injured idle against the 8-direction walk by the smoothed speed; one shared phase (each loop = one gait cycle, its stride =
	// its authored speed x its length, the two directions blended like the clips, the old direction's during a reversal's cross-fade), played
	// at the body's real speed - the feet of every loop in play strike together (the per-loop left-foot strike offsets).
	const TStaticArray<bool, 8> Avail = [&Set]()
	{
		TStaticArray<bool, 8> Out;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out[Index] = Set.Walk[Index] != nullptr;
		}
		return Out;
	}();
	const FWeaponLayerDirectionBlend Dir = BlendDirections(InjuredDirection.Current, Avail);
	const FWeaponLayerDirectionBlend FromDir = BlendDirections(InjuredDirection.From, Avail);
	const float FromShare = 1.f - InjuredDirection.Fade;
	InjuredIdleTime = Set.StandIdle && Set.StandIdle->GetPlayLength() > KINDA_SMALL_NUMBER
		? FMath::Fmod(InjuredIdleTime + DeltaSeconds, Set.StandIdle->GetPlayLength()) : 0.f;
	Add(Set.StandIdle, 1.f - Move, InjuredIdleTime);
	auto CycleOf = [&Set, &Length](const FWeaponLayerDirectionBlend& Blend)
	{
		const float Nominal = BlendedNominalSpeed(Blend, Set.WalkSpeeds, Set.WalkSpeed);
		return Nominal * FMath::Lerp(Length(Set.Walk[static_cast<int32>(Blend.A)]), Length(Set.Walk[static_cast<int32>(Blend.B)]), Blend.Alpha);
	};
	UAnimSequence* Lead = Set.Walk[static_cast<int32>(Dir.Alpha < 0.5f ? Dir.A : Dir.B)];
	const float LeadLength = Length(Lead);
	const float Rate = ProneCrawlRules::PhaseRate(MoveSpeed, CycleOf(Dir), CycleOf(FromDir), FromShare, Set.MaxPlayRate, LeadLength);
	InjuredPhase = FMath::Fmod(InjuredPhase + DeltaSeconds * Rate, 1.f);
	InjuredPlayRate = Rate * LeadLength;
	auto AddPair = [&Add, &At, &Set](const FWeaponLayerDirectionBlend& Blend, float Weight)
	{
		UAnimSequence* A = Set.Walk[static_cast<int32>(Blend.A)];
		UAnimSequence* B = Set.Walk[static_cast<int32>(Blend.B)];
		Add(A, Weight * (1.f - Blend.Alpha), At(A));
		Add(B, Weight * Blend.Alpha, At(B));
	};
	AddPair(Dir, Move * (1.f - FromShare));
	if (FromShare > OperativeInjuredLayer::InjuredMinWeight)
	{
		AddPair(FromDir, Move * FromShare);
	}
}

void FOperativeAnimInstanceProxy::BlendInjuredLayer(FPoseContext& Output) const
{
	using namespace OperativeInjuredLayer;
	// The layer's pose: its clips blended by weight (the crawl layer's sampling; at most six clips, inline - nothing allocates on the heap).
	TArray<FCompactPose, TInlineAllocator<6>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<6>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<6>> Attributes;
	TArray<float, TInlineAllocator<6>> Weights;
	float Total = 0.f;
	for (const FOperativeClipState& Clip : InjuredClips)
	{
		if (!Clip.Sequence || Clip.Weight < InjuredMinWeight)
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

	const float Weight = FMath::Min(1.f, InjuredWeight);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FCompactPoseBoneIndex Spine = InjuredBone(Bones, TEXT("spine_01"));
	const FCompactPoseBoneIndex HandIK = InjuredBone(Bones, TEXT("ik_hand_root"));
	const FCompactPoseBoneIndex SpineParent = Spine.GetInt() != INDEX_NONE ? Output.Pose.GetParentBoneIndex(Spine) : FCompactPoseBoneIndex(INDEX_NONE);
	// The graph's (+ the weapon layer's) torso and hand IK placement before the legs change: kept in component space.
	const FCompactPoseBoneIndex Root(0);
	const FTransform GraphRoot = Output.Pose[Root];
	const FQuat SpineParentGraph = SpineParent.GetInt() != INDEX_NONE ? InjuredChainRotation(Output.Pose, SpineParent) : FQuat::Identity;
	const float TorsoShare = FMath::Clamp(InjuredTorsoShare, 0.f, 1.f);
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || Index == HandIK || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		if (Index.IsRootBone())
		{
			// The root faces the capsule (the reference): the ABP's orientation warping / root yaw turns it towards the movement for ITS forward
			// legs - the directional injured legs on that turned root would walk off the travel and slide (the weapon sets' and the crawl's rule).
			const FTransform& Reference = Bones.GetRefPoseTransform(Index);
			Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Reference.GetRotation(), Weight).GetNormalized());
			Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Reference.GetTranslation(), Weight));
			continue;
		}
		if (Index == Spine && SpineParent.GetInt() != INDEX_NONE)
		{
			// The torso keeps the graph's mesh-space rotation on the injured pelvis (its local rotation re-expressed under the new pelvis chain -
			// parents are final, they come first), TorsoShare of the pelvis's own limp riding along (0 while aiming: the aim stays exact). The
			// local translation stays: the torso rides the pelvis's bob.
			const FQuat Graph = Output.Pose[Index].GetRotation();
			const FQuat Kept = (InjuredChainRotation(Output.Pose, SpineParent).Inverse() * SpineParentGraph * Graph).GetNormalized();
			Output.Pose[Index].SetRotation(FQuat::Slerp(Kept, Graph, TorsoShare).GetNormalized());
			continue;
		}
		if (Index == HandIK && Parent.GetInt() != INDEX_NONE)
		{
			// The hand IK bones (under the root) keep their component-space transform: the root under them turned.
			const FTransform ParentNow = Parent.IsRootBone() ? Output.Pose[Parent] : FTransform(InjuredChainRotation(Output.Pose, Parent));
			const FTransform ParentGraph = Parent.IsRootBone() ? GraphRoot : ParentNow;
			Output.Pose[Index] = Output.Pose[Index] * ParentGraph * ParentNow.Inverse();
			continue;
		}
		if (!Upper[Index.GetInt()])
		{
			Output.Pose[Index].BlendWith(Layer[Index], Weight);
		}
	}
}
