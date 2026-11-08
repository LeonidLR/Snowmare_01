// UOperativeAnimInstance: the shotgun weapon layer (the D-1, designer decision 21, 2026-11-03; implemented 2026-11-08).
// The user's ShotgunLocomotionPack clips (UE5 SK_Mannequin) blended natively over ABP_Operative's pose - no AnimGraph node, the user's ABP
// stays as it is. The operatives' UE4 skeletons play IK-retargeted copies (/Game/Characters/Animations/Shotgun/{Male,Female}); the PMC
// operator skeleton (the Manny hierarchy) plays the pack clips by bone name.
// Clip paths: Content/Data/Weapons/weapon_anim_sets.json "shotgun". Rules: Characters/WeaponAnimLayerRules.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "CodexTactics.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/WeaponBallisticsRules.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace OperativeShotgunLayer
{
	constexpr float ShotgunMinWeight = 0.005f;

	static const TCHAR* const DirectionKeys[8] = { TEXT("fwd"), TEXT("fwd_right"), TEXT("right"), TEXT("bwd_right"), TEXT("bwd"), TEXT("bwd_left"),
		TEXT("left"), TEXT("fwd_left") };

	FString AnimSetsPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Weapons/weapon_anim_sets.json");
	}

	/** The "shotgun" block of weapon_anim_sets.json (read once per process). */
	TSharedPtr<FJsonObject> ShotgunBlock()
	{
		static TSharedPtr<FJsonObject> Cached;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FString Text;
			TSharedPtr<FJsonObject> Root;
			const TSharedPtr<FJsonObject>* Block = nullptr;
			if (FFileHelper::LoadFileToString(Text, *AnimSetsPath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
				&& Root.IsValid() && Root->TryGetObjectField(TEXT("shotgun"), Block) && Block)
			{
				Cached = *Block;
			}
		}
		return Cached;
	}

	/** Moves an eased share towards its target at 1 / BlendSeconds a second. */
	float EaseShare(float Share, float Target, float DeltaSeconds, float BlendSeconds)
	{
		return FMath::FInterpConstantTo(Share, Target, DeltaSeconds, 1.f / FMath::Max(0.01f, BlendSeconds));
	}

	/** Advances a looping clip time. */
	float Loop(float Time, float Delta, const UAnimSequenceBase* Clip)
	{
		const float Length = Clip ? Clip->GetPlayLength() : 0.f;
		return Length > KINDA_SMALL_NUMBER ? FMath::Fmod(FMath::Max(0.f, Time + Delta), Length) : 0.f;
	}
}

void UOperativeAnimInstance::LoadShotgunAnimSet()
{
	if (bShotgunSetLoaded)
	{
		return;
	}
	bShotgunSetLoaded = true;
	const TSharedPtr<FJsonObject> Block = OperativeShotgunLayer::ShotgunBlock();
	if (!Block.IsValid())
	{
		return;
	}
	// The operatives' UE4 skeletons get IK-retargeted copies (Scripts/Editor/setup_shotgun_animation.py; bone-name playback of the UE5 Manny
	// clips bends their chest so the gun points sideways); skeletons without an entry (the PMC operator = the Manny hierarchy) play the pack.
	FString RetargetFolder;
	const TSharedPtr<FJsonObject>* Retargeted = nullptr;
	const USkeletalMeshComponent* Body = GetSkelMeshComponent();
	const USkeletalMesh* BodyMesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* BodySkeleton = BodyMesh ? BodyMesh->GetSkeleton() : nullptr;
	if (BodySkeleton && Block->TryGetObjectField(TEXT("retargeted"), Retargeted) && Retargeted)
	{
		(*Retargeted)->TryGetStringField(BodySkeleton->GetName(), RetargetFolder);
	}
	ShotgunRetargetFolder = RetargetFolder;
	auto Load = [this, &RetargetFolder](const FString& PackPath) -> UAnimSequenceBase*
	{
		if (PackPath.IsEmpty())
		{
			return nullptr;
		}
		const FString Copy = WeaponAnimLayerRules::RetargetedClipPath(PackPath, RetargetFolder);
		const FString Path = !Copy.IsEmpty() && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Copy)) ? Copy : PackPath;
		UAnimSequenceBase* Clip = LoadObject<UAnimSequenceBase>(nullptr, *Path);
		if (Clip)
		{
			ShotgunClipRefs.AddUnique(Clip);
		}
		else
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Shotgun layer: no clip %s"), *Path);
		}
		return Clip;
	};
	auto Sequence = [&Load, &Block](const TCHAR* Key) -> UAnimSequence*
	{
		FString Path;
		return Block->TryGetStringField(Key, Path) ? Cast<UAnimSequence>(Load(Path)) : nullptr;
	};
	auto Directional = [&Load, &Block](const TCHAR* Key, TStaticArray<UAnimSequence*, 8>& Out)
	{
		const TSharedPtr<FJsonObject>* Set = nullptr;
		if (!Block->TryGetObjectField(Key, Set) || !Set)
		{
			return;
		}
		for (int32 Index = 0; Index < 8; ++Index)
		{
			FString Path;
			Out[Index] = (*Set)->TryGetStringField(OperativeShotgunLayer::DirectionKeys[Index], Path) ? Cast<UAnimSequence>(Load(Path)) : nullptr;
		}
	};
	auto Number = [&Block](const TCHAR* Key, float& Value)
	{
		double Read = 0.0;
		if (Block->TryGetNumberField(Key, Read))
		{
			Value = static_cast<float>(Read);
		}
	};
	ShotgunSet.StandIdle = Sequence(TEXT("stand_idle"));
	ShotgunSet.AimIdle = Sequence(TEXT("aim_idle"));
	ShotgunSet.CrouchIdle = Sequence(TEXT("crouch_idle"));
	ShotgunSet.Sprint = Sequence(TEXT("sprint"));
	Directional(TEXT("walk"), ShotgunSet.Walk);
	Directional(TEXT("aim_walk"), ShotgunSet.AimWalk);
	Directional(TEXT("jog"), ShotgunSet.Jog);
	Directional(TEXT("crouch"), ShotgunSet.Crouch);
	FString Path;
	ShotgunSet.FireAim = Block->TryGetStringField(TEXT("fire_aim"), Path) ? Load(Path) : nullptr;
	ShotgunSet.FireHip = Block->TryGetStringField(TEXT("fire_hip"), Path) ? Load(Path) : nullptr;
	ShotgunSet.Reload = Block->TryGetStringField(TEXT("reload"), Path) ? Load(Path) : nullptr;
	Number(TEXT("walk_speed"), ShotgunSet.WalkSpeed);
	Number(TEXT("jog_speed"), ShotgunSet.JogSpeed);
	Number(TEXT("crouch_speed"), ShotgunSet.CrouchSpeed);
	Number(TEXT("sprint_speed"), ShotgunSet.SprintSpeed);
	Number(TEXT("move_start_speed"), ShotgunSet.MoveStartSpeed);
	Number(TEXT("move_full_speed"), ShotgunSet.MoveFullSpeed);
	Number(TEXT("reload_shell_start_s"), ShotgunSet.ShellStart);
	Number(TEXT("reload_shell_end_s"), ShotgunSet.ShellEnd);
	Number(TEXT("blend_seconds"), ShotgunSet.BlendSeconds);
}

bool UOperativeAnimInstance::IsShotgunLayerEligible() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	return bShotgunWeapon && ShotgunSet.StandIdle && Operative && !bIsDead && !bKnockedDown && !bIsClimbing && !bIsVaulting && !bInCover && !bIsProne
		&& !bFallAirborne && !bSniperPose && !bBlizzardWalkActive && !Operative->bCarrying;
}

const UAnimSequenceBase* UOperativeAnimInstance::GetShotgunDominantClip() const
{
	const FOperativeClipState* Best = nullptr;
	for (const FOperativeClipState& Clip : ShotgunClips)
	{
		Best = Clip.Sequence && (!Best || Clip.Weight > Best->Weight) ? &Clip : Best;
	}
	return Best && ShotgunMask != EWeaponLayerMask::Off ? Best->Sequence.Get() : nullptr;
}

void UOperativeAnimInstance::UpdateShotgunLayer(float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	bShotgunWeapon = Operative && Operative->CurrentWeapon && Operative->CurrentWeapon->Stats.Handling == EWeaponHandling::Shotgun;
	// A stance transition counts as full body from its first frame (its weight is still ~0 then): the layer blending in over the rise from
	// prone popped the torso ~15 cm (ShotgunSmoke 7).
	ShotgunMask = ResolveMask(IsShotgunLayerEligible(), GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f || IsPlayingStanceTransition(),
		GetSlotMontageGlobalWeight(UpperBodySlot) > 0.01f);
	const float BlendRate = DeltaSeconds / FMath::Max(0.01f, ShotgunSet.BlendSeconds);
	const float TargetUpper = ShotgunMask == EWeaponLayerMask::Full ? 1.f : 0.f;
	const float TargetLower = ShotgunMask == EWeaponLayerMask::Off ? 0.f : 1.f;
	ShotgunUpperWeight = FMath::Clamp(ShotgunUpperWeight + FMath::Sign(TargetUpper - ShotgunUpperWeight) * BlendRate, FMath::Min(ShotgunUpperWeight, TargetUpper),
		FMath::Max(ShotgunUpperWeight, TargetUpper));
	ShotgunLowerWeight = FMath::Clamp(ShotgunLowerWeight + FMath::Sign(TargetLower - ShotgunLowerWeight) * BlendRate, FMath::Min(ShotgunLowerWeight, TargetLower),
		FMath::Max(ShotgunLowerWeight, TargetLower));
	for (FOperativeClipState& Clip : ShotgunClips)
	{
		Clip = FOperativeClipState();
	}
	if (ShotgunUpperWeight <= OperativeShotgunLayer::ShotgunMinWeight && ShotgunLowerWeight <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		return;
	}

	// The clips: idle against the moving set by speed; the crouch / aim / sprint sets by eased shares (a stance or aim change cross-fades over
	// blend_seconds - switching them in one frame popped the torso ~20 cm, RecoilProbe 2026-11-08); each walking set two directions.
	const float MoveSpeed = Speed;
	const float Move = MoveWeight(MoveSpeed, ShotgunSet.MoveStartSpeed, ShotgunSet.MoveFullSpeed);
	const bool bSprint = bIsSprinting && ShotgunSet.Sprint && !bIsCrouching;
	ShotgunCrouchShare = OperativeShotgunLayer::EaseShare(ShotgunCrouchShare, bIsCrouching && ShotgunSet.CrouchIdle ? 1.f : 0.f, DeltaSeconds, ShotgunSet.BlendSeconds);
	ShotgunAimShare = OperativeShotgunLayer::EaseShare(ShotgunAimShare, bIsAiming ? 1.f : 0.f, DeltaSeconds, ShotgunSet.BlendSeconds);
	ShotgunSprintShare = OperativeShotgunLayer::EaseShare(ShotgunSprintShare, bSprint ? 1.f : 0.f, DeltaSeconds, ShotgunSet.BlendSeconds);
	auto Available = [](const TStaticArray<UAnimSequence*, 8>& Set)
	{
		TStaticArray<bool, 8> Out;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Out[Index] = Set[Index] != nullptr;
		}
		return Out;
	};
	int32 Next = 0;
	auto Add = [this, &Next](UAnimSequence* Clip, float Weight, float Time)
	{
		if (Clip && Weight > OperativeShotgunLayer::ShotgunMinWeight && Next < ShotgunClips.Num())
		{
			ShotgunClips[Next++] = { Clip, Time, Weight };
		}
	};
	// Idle: crouch / aim / stand by the shares on one shared clock (the pack's idles are 5.5 s each).
	UAnimSequence* AimIdle = ShotgunSet.AimIdle ? ShotgunSet.AimIdle : ShotgunSet.StandIdle;
	ShotgunIdleTime = OperativeShotgunLayer::Loop(ShotgunIdleTime, DeltaSeconds, ShotgunSet.StandIdle);
	const float IdleWeight = 1.f - Move;
	auto IdleTime = [this](const UAnimSequence* Clip) { return Clip ? FMath::Fmod(ShotgunIdleTime, FMath::Max(0.1f, Clip->GetPlayLength())) : 0.f; };
	Add(ShotgunSet.CrouchIdle, IdleWeight * ShotgunCrouchShare, IdleTime(ShotgunSet.CrouchIdle));
	Add(AimIdle, IdleWeight * (1.f - ShotgunCrouchShare) * ShotgunAimShare, IdleTime(AimIdle));
	Add(ShotgunSet.StandIdle, IdleWeight * (1.f - ShotgunCrouchShare) * (1.f - ShotgunAimShare), IdleTime(ShotgunSet.StandIdle));
	if (Move <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		return;
	}
	const float Stand = 1.f - ShotgunCrouchShare;
	const float JogShare = JogWeight(MoveSpeed, ShotgunSet.WalkSpeed, ShotgunSet.JogSpeed);
	const FWeaponLayerDirectionBlend WalkDir = BlendDirections(Direction, Available(ShotgunSet.Walk));
	const FWeaponLayerDirectionBlend AimDir = BlendDirections(Direction, Available(ShotgunSet.AimWalk));
	const FWeaponLayerDirectionBlend CrouchDir = BlendDirections(Direction, Available(ShotgunSet.Crouch));
	const FWeaponLayerDirectionBlend JogDir = BlendDirections(Direction, Available(ShotgunSet.Jog));
	// One normalized phase for every loop (the pack's loops start on the same foot), advanced by the speed against the dominant set's
	// nominal speed.
	const bool bCrouchLead = ShotgunCrouchShare >= 0.5f;
	const bool bSprintLead = !bCrouchLead && ShotgunSprintShare >= 0.5f && ShotgunSet.Sprint;
	UAnimSequence* Lead = bSprintLead ? ShotgunSet.Sprint
		: bCrouchLead ? ShotgunSet.Crouch[static_cast<int32>(CrouchDir.A)] : ShotgunSet.Walk[static_cast<int32>(WalkDir.A)];
	const float LeadNominal = bSprintLead ? ShotgunSet.SprintSpeed
		: bCrouchLead ? ShotgunSet.CrouchSpeed : FMath::Lerp(ShotgunSet.WalkSpeed, ShotgunSet.JogSpeed, ShotgunAimShare >= 0.5f ? 0.f : JogShare);
	const float LeadLength = Lead ? FMath::Max(0.1f, Lead->GetPlayLength()) : 1.f;
	ShotgunPhase = FMath::Fmod(ShotgunPhase + DeltaSeconds * LoopPlayRate(MoveSpeed, LeadNominal) / LeadLength, 1.f);
	auto At = [this](const UAnimSequence* Clip) { return Clip ? ShotgunPhase * Clip->GetPlayLength() : 0.f; };
	auto AddPair = [&Add, &At](const TStaticArray<UAnimSequence*, 8>& Set, const FWeaponLayerDirectionBlend& Dir, float Weight)
	{
		UAnimSequence* A = Set[static_cast<int32>(Dir.A)];
		UAnimSequence* B = Set[static_cast<int32>(Dir.B)];
		Add(A, Weight * (1.f - Dir.Alpha), At(A));
		Add(B, Weight * Dir.Alpha, At(B));
	};
	AddPair(ShotgunSet.Crouch, CrouchDir, Move * ShotgunCrouchShare);
	const float Sprint = ShotgunSet.Sprint ? ShotgunSprintShare : 0.f;
	Add(ShotgunSet.Sprint, Move * Stand * Sprint, At(ShotgunSet.Sprint));
	const float Upright = Move * Stand * (1.f - Sprint);
	AddPair(ShotgunSet.AimWalk, AimDir, Upright * ShotgunAimShare);
	AddPair(ShotgunSet.Walk, WalkDir, Upright * (1.f - ShotgunAimShare) * (1.f - JogShare));
	AddPair(ShotgunSet.Jog, JogDir, Upright * (1.f - ShotgunAimShare) * JogShare);
}

bool UOperativeAnimInstance::PlayShotgunFire()
{
	if (!IsShotgunLayerEligible() || (!ShotgunSet.FireAim && !ShotgunSet.FireHip) || bIsReloading)
	{
		return false;
	}
	UAnimSequenceBase* Clip = bIsAiming && ShotgunSet.FireAim ? ShotgunSet.FireAim : (ShotgunSet.FireHip ? ShotgunSet.FireHip : ShotgunSet.FireAim);
	PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, 0.05f, 0.2f);
	++ShotgunFireClips;
	return true;
}

bool UOperativeAnimInstance::UpdateShotgunReload(const AOperativeCharacter* Operative)
{
	if (!Operative || !bShotgunWeapon || !ShotgunSet.Reload || bIsProne || bKnockedDown || bIsClimbing || !Operative->CurrentWeapon)
	{
		ShotgunReloadLastClip = -1;
		return false;
	}
	const FWeaponAmmoStats& Ammo = Operative->CurrentWeapon->Stats.Ammo;
	const int32 Clip = Operative->CurrentClip;
	const bool bStart = bIsReloading && !bWasReloading;
	const bool bNextShell = bIsReloading && bWasReloading && WeaponBallisticsRules::ReloadsPerRound(Ammo) && ShotgunReloadLastClip >= 0 && Clip > ShotgunReloadLastClip;
	if ((bStart || bNextShell) && !IsPlayingStanceTransition())
	{
		float Start = 0.f;
		float End = 0.f;
		const float Step = FMath::Max(0.1f, WeaponBallisticsRules::ReloadStepSeconds(Ammo));
		const float Rate = WeaponAnimLayerRules::ShellReloadRate(ShotgunSet.Reload->GetPlayLength(), ShotgunSet.ShellStart, ShotgunSet.ShellEnd,
			WeaponBallisticsRules::ReloadsPerRound(Ammo) ? Step : FMath::Max(0.1f, Operative->ReloadTimer), Start, End);
		PlaySlotAnimationAsDynamicMontage(ShotgunSet.Reload, UpperBodySlot, 0.1f, 0.15f, Rate, 1, -1.f, Start);
		++ShotgunShellClips;
	}
	else if (!bIsReloading && bWasReloading)
	{
		StopSlotAnimation(0.15f, UpperBodySlot);
	}
	ShotgunReloadLastClip = bIsReloading ? Clip : -1;
	return true;
}

void FOperativeAnimInstanceProxy::BlendWeaponLayer(FPoseContext& Output) const
{
	// The layer's pose: its clips blended by weight (the same sampling as the native locomotion).
	TArray<FCompactPose, TInlineAllocator<12>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<12>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<12>> Attributes;
	TArray<float, TInlineAllocator<12>> Weights;
	float Total = 0.f;
	for (const FOperativeClipState& Clip : WeaponClips)
	{
		if (!Clip.Sequence || Clip.Weight < OperativeShotgunLayer::ShotgunMinWeight)
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

	// Per bone: spine_01 and everything under it = the torso (upper weight), the rest below the root = pelvis / legs (lower weight); the
	// root keeps the graph's (the capsule-relative placement, root motion off anyway).
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		if (Index.IsRootBone())
		{
			continue;
		}
		const float Weight = Upper[Index.GetInt()] ? WeaponUpperWeight : WeaponLowerWeight;
		if (Weight > OperativeShotgunLayer::ShotgunMinWeight)
		{
			Output.Pose[Index].BlendWith(Layer[Index], FMath::Min(1.f, Weight));
		}
	}
}
