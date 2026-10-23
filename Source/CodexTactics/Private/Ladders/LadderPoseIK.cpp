// LadderPoseIK: rung IK of the hands / feet on an evaluated pose (docs/design/LADDERS.md 6.2), shared by the operatives' and the
// climbing enemies' anim proxies (moved out of OperativeAnimInstance.cpp 2026-10-23). UE-only system (no Godot reference).

#include "Ladders/LadderPoseIK.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

namespace LadderPoseIKInternal
{
	struct FChain
	{
		const TCHAR* Root;
		const TCHAR* Joint;
		const TCHAR* End;
		/** The contact bone (ball of the foot); null = the end bone itself (the wrist). */
		const TCHAR* Contact;
	};

	// ELadderLimb order: HandL, HandR, FootL, FootR (UE4 mannequin names; the UE5 Manny shares them).
	const FChain Chains[] = {
		{ TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), nullptr },
		{ TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), nullptr },
		{ TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l") },
		{ TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r") },
	};

	FCompactPoseBoneIndex Find(const FBoneContainer& Bones, const TCHAR* Name)
	{
		const int32 MeshIndex = Name ? Bones.GetPoseBoneIndexForBoneName(FName(Name)) : INDEX_NONE;
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	}
}

void LadderPoseIK::Apply(FPoseContext& Output, const FTransform& ComponentTransform, const FLadderIKFrame& LadderIK, FVector KneeIKOffset[2])
{
	using namespace LadderPoseIKInternal;
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const FTransform& Component = ComponentTransform;
	FCSPose<FCompactPose> Pose;
	Pose.InitPose(Output.Pose);
	TArray<FBoneTransform> Changed;
	KneeIKOffset[0] = KneeIKOffset[1] = FVector::ZeroVector;
	for (int32 Limb = 0; Limb < static_cast<int32>(ELadderLimb::Count); ++Limb)
	{
		const float Alpha = FMath::Clamp(LadderIK.Alpha[Limb], 0.f, 1.f);
		if (Alpha <= 0.001f)
		{
			continue;
		}
		const FChain& Chain = Chains[Limb];
		const FCompactPoseBoneIndex RootIndex = Find(Bones, Chain.Root);
		const FCompactPoseBoneIndex JointIndex = Find(Bones, Chain.Joint);
		const FCompactPoseBoneIndex EndIndex = Find(Bones, Chain.End);
		const FCompactPoseBoneIndex ContactIndex = Chain.Contact ? Find(Bones, Chain.Contact) : EndIndex;
		if (RootIndex == INDEX_NONE || JointIndex == INDEX_NONE || EndIndex == INDEX_NONE || ContactIndex == INDEX_NONE)
		{
			continue;
		}
		FTransform Root = Pose.GetComponentSpaceTransform(RootIndex);
		FTransform Joint = Pose.GetComponentSpaceTransform(JointIndex);
		FTransform End = Pose.GetComponentSpaceTransform(EndIndex);
		const FVector Contact = Pose.GetComponentSpaceTransform(ContactIndex).GetLocation();
		// The grip point: the rung's centre + the limb's offset (towards the climber / up), at the limb's own spot across the rung.
		const bool bFoot = Limb >= static_cast<int32>(ELadderLimb::FootL);
		const FVector2D Offset = bFoot ? LadderIK.FootOffset : LadderIK.HandOffset;
		const FVector Rung = LadderIK.RungCentre[Limb];
		const float Across = FMath::Clamp(static_cast<float>(FVector::DotProduct(Component.TransformPosition(Contact) - Rung, LadderIK.Across)),
			-LadderIK.HalfWidth, LadderIK.HalfWidth);
		const FVector TargetWorld = Rung + LadderIK.Across * Across + LadderIK.Outward * Offset.X + FVector::UpVector * Offset.Y;
		// The end bone goes where the contact bone lands on the target (the foot keeps its orientation).
		const FVector EndTarget = Component.InverseTransformPosition(TargetWorld) - (Contact - End.GetLocation());
		const FVector RootP = Root.GetLocation();
		const FVector JointP = Joint.GetLocation();
		const FVector EndP = End.GetLocation();
		FVector Effector = FMath::Lerp(EndP, EndTarget, Alpha);
		// Pole (user report 2026-10-23: "the right knee jumps and bends unnaturally"): the clip straightens each leg fully while it stands on
		// a rung, so the knee's offset from the hip-ankle line vanishes and a pole taken from it flips the bend plane. The pole is the clip's
		// own bend direction while the limb is bent, faded into a fixed one as it straightens: knees forward to the ladder and a little out to
		// their side (as in the clip), elbows back to the climber, out and down.
		const FVector LineDir = (EndP - RootP).GetSafeNormal();
		const FVector Bend = (JointP - RootP) - LineDir * FVector::DotProduct(JointP - RootP, LineDir);
		const FVector Inward = Component.InverseTransformVectorNoScale(-LadderIK.Outward).GetSafeNormal();
		const FVector ClimberRight = Component.InverseTransformVectorNoScale(FVector::CrossProduct(FVector::UpVector, -LadderIK.Outward)).GetSafeNormal();
		const FVector UpCS = Component.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		const float Side = (Limb == static_cast<int32>(ELadderLimb::HandL) || Limb == static_cast<int32>(ELadderLimb::FootL)) ? -1.f : 1.f;
		FVector Fixed = bFoot ? Inward + ClimberRight * (Side * 0.7f) : -Inward + ClimberRight * (Side * 0.6f) - UpCS * 0.6f;
		Fixed = (Fixed - LineDir * FVector::DotProduct(Fixed, LineDir)).GetSafeNormal();
		const float Trust = FMath::Clamp(static_cast<float>(Bend.Size()) / 8.f, 0.f, 1.f);
		FVector PoleDir = (Bend.GetSafeNormal() * Trust + Fixed * (1.f - Trust)).GetSafeNormal();
		if (PoleDir.IsNearlyZero())
		{
			PoleDir = Fixed;
		}
		if (bFoot && FVector::DotProduct(PoleDir, Inward) < 0.2f)
		{
			PoleDir = (PoleDir + Inward * (0.2f - FVector::DotProduct(PoleDir, Inward))).GetSafeNormal(); // a knee never bends away from the ladder
		}
		// Reach: never past 98.5 % of the limb at full IK (a fully straight limb has no bend plane and would snap), faded with the alpha so the
		// IK hands back to the clip's own (possibly straight) leg without a knee pop.
		const double Reach = (JointP - RootP).Size() + (EndP - JointP).Size();
		const double MaxReach = Reach * (1.0 - 0.015 * Alpha);
		if ((Effector - RootP).Size() > MaxReach)
		{
			Effector = RootP + (Effector - RootP).GetSafeNormal() * MaxReach;
		}
		AnimationCore::SolveTwoBoneIK(Root, Joint, End, JointP + PoleDir * 40.0, Effector, false, 1.0, 1.0);
		if (bFoot)
		{
			KneeIKOffset[Limb == static_cast<int32>(ELadderLimb::FootL) ? 0 : 1] = Joint.GetLocation() - JointP;
		}
		Changed.Add(FBoneTransform(RootIndex, Root));
		Changed.Add(FBoneTransform(JointIndex, Joint));
		Changed.Add(FBoneTransform(EndIndex, End));
	}
	if (Changed.IsEmpty())
	{
		return;
	}
	Changed.Sort(FCompareBoneTransformIndex());
	Pose.LocalBlendCSBoneTransforms(Changed, 1.f);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Pose), Output.Pose);
}
