// UOperativeAnimInstance: the shotgun weapon layer (the D-1, designer decision 21, 2026-11-03; implemented 2026-11-08).
// The user's ShotgunLocomotionPack clips (UE5 SK_Mannequin) blended natively over ABP_Operative's pose - no AnimGraph node, the user's ABP
// stays as it is. The operatives' UE4 skeletons play IK-retargeted copies (/Game/Characters/Animations/Shotgun/{Male,Female}); the PMC
// operator skeleton (the Manny hierarchy) plays the pack clips by bone name.
// Clip paths: Content/Data/Weapons/weapon_anim_sets.json "shotgun" / "pistol" / "launcher". Rules: Characters/WeaponAnimLayerRules.
// The launcher set (the RPG-7 on the game designer's launcher pack, 2026-11-09): the same upper-body layer plus a native action clip (the shot,
// the reload fitted to the reload time) blended into the layer's pose and a pose-based aim offset (the pack's Rotation_* poses as mesh-space
// additives) - both before the torso is put on the ABP's pelvis, so the legs stay the ABP's.

#include "Characters/OperativeAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "CodexTactics.h"
#include "Characters/HitReactionRules.h"
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

	/** A block of weapon_anim_sets.json ("shotgun", "pistol"; the file read once per process). */
	TSharedPtr<FJsonObject> WeaponBlock(const TCHAR* Key)
	{
		static TSharedPtr<FJsonObject> Root;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *AnimSetsPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
			{
				Root.Reset();
			}
		}
		const TSharedPtr<FJsonObject>* Block = nullptr;
		return Root.IsValid() && Root->TryGetObjectField(Key, Block) && Block ? *Block : TSharedPtr<FJsonObject>();
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
	const USkeletalMeshComponent* BodyComponent = GetSkelMeshComponent();
	const USkeleton* BodySkeleton = BodyComponent && BodyComponent->GetSkeletalMeshAsset() ? BodyComponent->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;
	if (bShotgunSetLoaded && LoadedSetsSkeleton.Get() == BodySkeleton)
	{
		return;
	}
	if (bShotgunSetLoaded)
	{
		// The body changed its skeleton since the sets were loaded: start over with that skeleton's copies.
		ShotgunSetData = FShotgunAnimSet();
		PistolSetData = FShotgunAnimSet();
		LauncherSetData = FShotgunAnimSet();
		ShotgunClipRefs.Reset();
		LauncherShotClip = LauncherReloadClip = LauncherActionClip = nullptr;
		LauncherShotTime = -1.f;
		LayerTurnClip = nullptr;
		WeaponFireAdditiveClip = nullptr;
		WeaponFireAdditiveTime = -1.f;
	}
	bShotgunSetLoaded = true;
	LoadedSetsSkeleton = BodySkeleton;
	LoadWeaponAnimSet(TEXT("shotgun"), ShotgunSetData, ShotgunRetargetFolder);
	LoadWeaponAnimSet(TEXT("pistol"), PistolSetData, PistolRetargetFolder);
	LoadWeaponAnimSet(TEXT("launcher"), LauncherSetData, LauncherRetargetFolder);
}

void UOperativeAnimInstance::LoadWeaponAnimSet(const TCHAR* BlockKey, FShotgunAnimSet& ShotgunSet, FString& OutRetargetFolder)
{
	const TSharedPtr<FJsonObject> Block = OperativeShotgunLayer::WeaponBlock(BlockKey);
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
	OutRetargetFolder = RetargetFolder;
	// "pack_playback" false (the launcher's UE4-mannequin pack): a skeleton without copies gets no set at all (by bone name the pack's clips would
	// bend another hierarchy, e.g. the PMC operator's UE5 one) - the layer stays off for it.
	bool bPackPlayback = true;
	Block->TryGetBoolField(TEXT("pack_playback"), bPackPlayback);
	ShotgunSet.bPackPlayback = bPackPlayback;
	if (!bPackPlayback && RetargetFolder.IsEmpty())
	{
		return;
	}
	auto Load = [this, &RetargetFolder, bPackPlayback](const FString& PackPath) -> UAnimSequenceBase*
	{
		if (PackPath.IsEmpty())
		{
			return nullptr;
		}
		const FString Copy = WeaponAnimLayerRules::RetargetedClipPath(PackPath, RetargetFolder);
		const bool bCopy = !Copy.IsEmpty() && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Copy));
		if (!bCopy && !bPackPlayback)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Weapon layer: no retargeted copy %s (the pack is not played by bone name)"), *Copy);
			return nullptr;
		}
		const FString Path = bCopy ? Copy : PackPath;
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
	ShotgunSet.Equip = Block->TryGetStringField(TEXT("equip"), Path) ? Load(Path) : nullptr;
	Number(TEXT("walk_speed"), ShotgunSet.WalkSpeed);
	Number(TEXT("jog_speed"), ShotgunSet.JogSpeed);
	Number(TEXT("crouch_speed"), ShotgunSet.CrouchSpeed);
	Number(TEXT("sprint_speed"), ShotgunSet.SprintSpeed);
	Number(TEXT("move_start_speed"), ShotgunSet.MoveStartSpeed);
	Number(TEXT("move_full_speed"), ShotgunSet.MoveFullSpeed);
	Number(TEXT("reload_shell_start_s"), ShotgunSet.ShellStart);
	Number(TEXT("reload_shell_end_s"), ShotgunSet.ShellEnd);
	Number(TEXT("reload_outro_start_s"), ShotgunSet.OutroStart);
	Number(TEXT("reload_blend_in_s"), ShotgunSet.ReloadBlendIn);
	Number(TEXT("reload_blend_out_s"), ShotgunSet.ReloadBlendOut);
	Number(TEXT("blend_seconds"), ShotgunSet.BlendSeconds);
	// The launcher set's own keys (2026-11-09).
	ShotgunSet.FireCrouch = Block->TryGetStringField(TEXT("fire_crouch"), Path) ? Load(Path) : nullptr;
	ShotgunSet.ReloadCrouch = Block->TryGetStringField(TEXT("reload_crouch"), Path) ? Load(Path) : nullptr;
	const TSharedPtr<FJsonObject>* AimPoses = nullptr;
	if (Block->TryGetObjectField(TEXT("aim_poses"), AimPoses) && AimPoses)
	{
		static const TCHAR* const AimKeys[9] = { TEXT("up_left"), TEXT("up"), TEXT("up_right"), TEXT("left"), TEXT("center"), TEXT("right"), TEXT("down_left"),
			TEXT("down"), TEXT("down_right") };
		for (int32 Index = 0; Index < 9; ++Index)
		{
			FString PosePath;
			ShotgunSet.AimPoses[Index] = Index != static_cast<int32>(EWeaponAimPose::Center) && (*AimPoses)->TryGetStringField(AimKeys[Index], PosePath)
				? Cast<UAnimSequence>(Load(PosePath)) : nullptr;
		}
	}
	ShotgunSet.AimPoseBase = Sequence(TEXT("aim_pose_base"));
	auto ClipList = [&Load, &Block](const TCHAR* Key, TArray<UAnimSequenceBase*>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Block->TryGetArrayField(Key, List) && List)
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				if (UAnimSequenceBase* Clip = Value.IsValid() ? Load(Value->AsString()) : nullptr)
				{
					Out.Add(Clip);
				}
			}
		}
	};
	ClipList(TEXT("hits"), ShotgunSet.Hits);
	ClipList(TEXT("deaths"), ShotgunSet.Deaths);
	Number(TEXT("reload_insert_s"), ShotgunSet.ReloadInsert);
	Number(TEXT("reload_crouch_insert_s"), ShotgunSet.ReloadCrouchInsert);
	Number(TEXT("aim_yaw_range_deg"), ShotgunSet.AimYawRange);
	Number(TEXT("aim_pitch_range_deg"), ShotgunSet.AimPitchRange);
	Number(TEXT("fire_blend_in_s"), ShotgunSet.FireBlendIn);
	Number(TEXT("fire_blend_out_s"), ShotgunSet.FireBlendOut);
	// A set without its own aim-walk loops (the launcher: the tube is always shouldered) aims with its walk / jog loops.
	ShotgunSet.bHasAimWalk = false;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		ShotgunSet.bHasAimWalk |= ShotgunSet.AimWalk[Index] != nullptr;
	}
	// "body": "full" (the launcher, designer 2026-11-09 b: its own legs) | "upper" (the default: the shotgun, the pistol).
	FString BodyMode;
	ShotgunSet.bFullBody = Block->TryGetStringField(TEXT("body"), BodyMode) && BodyMode.Equals(TEXT("full"), ESearchCase::IgnoreCase);
	Number(TEXT("max_play_rate"), ShotgunSet.MaxPlayRate);
	FString RootMode;
	ShotgunSet.bRootReference = Block->TryGetStringField(TEXT("root_translation"), RootMode) && RootMode.Equals(TEXT("reference"), ESearchCase::IgnoreCase);
	FString CrouchBody;
	ShotgunSet.bCrouchUpper = Block->TryGetStringField(TEXT("crouch_body"), CrouchBody) && CrouchBody.Equals(TEXT("upper"), ESearchCase::IgnoreCase);
	const TSharedPtr<FJsonObject>* Offsets = nullptr;
	if (Block->TryGetObjectField(TEXT("phase_offsets"), Offsets) && Offsets)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Offsets)->Values)
		{
			double Value = 0.0;
			if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value))
			{
				ShotgunSet.PhaseOffsets.Add(FName(*Pair.Key), static_cast<float>(Value));
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* JogBlend = nullptr;
	if (Block->TryGetArrayField(TEXT("jog_blend_speeds"), JogBlend) && JogBlend && JogBlend->Num() == 2)
	{
		ShotgunSet.JogBlendStartSpeed = static_cast<float>((*JogBlend)[0]->AsNumber());
		ShotgunSet.JogBlendFullSpeed = static_cast<float>((*JogBlend)[1]->AsNumber());
	}
	auto Speeds = [&Block](const TCHAR* Key, TStaticArray<float, 8>& Out)
	{
		const TSharedPtr<FJsonObject>* Set = nullptr;
		if (Block->TryGetObjectField(Key, Set) && Set)
		{
			for (int32 Index = 0; Index < 8; ++Index)
			{
				double Value = 0.0;
				Out[Index] = (*Set)->TryGetNumberField(OperativeShotgunLayer::DirectionKeys[Index], Value) ? static_cast<float>(Value) : 0.f;
			}
		}
	};
	Speeds(TEXT("walk_speeds"), ShotgunSet.WalkSpeeds);
	Speeds(TEXT("aim_walk_speeds"), ShotgunSet.AimWalkSpeeds);
	Speeds(TEXT("jog_speeds"), ShotgunSet.JogSpeeds);
	Speeds(TEXT("crouch_speeds"), ShotgunSet.CrouchSpeeds);
	ShotgunSet.TurnLeft = Sequence(TEXT("turn_left"));
	ShotgunSet.TurnRight = Sequence(TEXT("turn_right"));
	ShotgunSet.CrouchTurnLeft = Sequence(TEXT("crouch_turn_left"));
	ShotgunSet.CrouchTurnRight = Sequence(TEXT("crouch_turn_right"));
	ShotgunSet.AimTurnLeft = Sequence(TEXT("aim_turn_left"));
	ShotgunSet.AimTurnRight = Sequence(TEXT("aim_turn_right"));
	Number(TEXT("turn_clip_deg"), ShotgunSet.TurnClipDeg);
	// The retargeted copies' own stride (a longer-legged body steps further per cycle: the launcher's copies +16 % male / +12 % female,
	// measured by Scripts/Editor/setup_launcher_animation.py) - their authored speeds scale with it, so the play rate keeps the feet planted.
	const TSharedPtr<FJsonObject>* StrideScales = nullptr;
	double Stride = 1.0;
	if (BodySkeleton && Block->TryGetObjectField(TEXT("stride_scale"), StrideScales) && StrideScales
		&& (*StrideScales)->TryGetNumberField(BodySkeleton->GetName(), Stride) && Stride > 0.1)
	{
		const float Scale = static_cast<float>(Stride);
		ShotgunSet.WalkSpeed *= Scale;
		ShotgunSet.JogSpeed *= Scale;
		ShotgunSet.CrouchSpeed *= Scale;
		ShotgunSet.SprintSpeed *= Scale;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			ShotgunSet.WalkSpeeds[Index] *= Scale;
			ShotgunSet.AimWalkSpeeds[Index] *= Scale;
			ShotgunSet.JogSpeeds[Index] *= Scale;
			ShotgunSet.CrouchSpeeds[Index] *= Scale;
		}
	}
}

bool UOperativeAnimInstance::IsShotgunLayerEligible() const
{
	const FShotgunAnimSet& ShotgunSet = ActiveWeaponSet();
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
	LoadShotgunAnimSet(); // no-op unless the body's skeleton changed since the sets were loaded
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	// The layer's weapons: the D-1 (shotgun set) and the pistol (pistol set, 2026-11-08).
	const EWeaponHandling Handling = Operative && Operative->CurrentWeapon ? Operative->CurrentWeapon->Stats.Handling : EWeaponHandling::Standard;
	const FString WeaponId = Operative && Operative->CurrentWeapon ? Operative->CurrentWeapon->WeaponId : FString();
	LayerHandling = Handling == EWeaponHandling::Shotgun || Handling == EWeaponHandling::Pistol || Handling == EWeaponHandling::Launcher ? Handling
		: EWeaponHandling::Standard;
	bShotgunWeapon = LayerHandling != EWeaponHandling::Standard;
	const FShotgunAnimSet& ShotgunSet = ActiveWeaponSet();
	if (WeaponFireAdditiveTime >= 0.f)
	{
		WeaponFireAdditiveTime += DeltaSeconds;
		if (!WeaponFireAdditiveClip || WeaponFireAdditiveTime >= WeaponFireAdditiveClip->GetPlayLength() || !bShotgunWeapon)
		{
			WeaponFireAdditiveTime = -1.f;
		}
	}
	// The draw: a layer weapon that just came into the hands plays its equip clip once (the pistol from the rifle / the sidearm switch).
	if (WeaponId != LayerWeaponId)
	{
		const bool bFirst = LayerWeaponId.IsEmpty();
		LayerWeaponId = WeaponId;
		if (!bFirst && bShotgunWeapon && ShotgunSet.Equip && IsShotgunLayerEligible() && !IsPlayingStanceTransition())
		{
			PlaySlotAnimationAsDynamicMontage(ShotgunSet.Equip, UpperBodySlot, 0.08f, 0.15f, 1.4f);
			++WeaponEquipClips;
		}
	}
	// A stance transition counts as full body from its first frame (its weight is still ~0 then): the layer blending in over the rise from
	// prone popped the torso ~15 cm (ShotgunSmoke 7).
	ShotgunMask = ResolveMask(IsShotgunLayerEligible(), GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f || IsPlayingStanceTransition(),
		GetSlotMontageGlobalWeight(UpperBodySlot) > 0.01f, IsWeaponLayerFullBody());
	const float BlendRate = DeltaSeconds / FMath::Max(0.01f, ShotgunSet.BlendSeconds);
	auto Step = [BlendRate](float Weight, float Target)
	{
		return FMath::Clamp(Weight + FMath::Sign(Target - Weight) * BlendRate, FMath::Min(Weight, Target), FMath::Max(Weight, Target));
	};
	ShotgunUpperWeight = Step(ShotgunUpperWeight, ShotgunMask == EWeaponLayerMask::Full ? 1.f : 0.f);
	// A "full" set (the launcher) also drives the pelvis and legs - under a hit reaction too (LowerBody); an "upper" set never does.
	ShotgunLowerWeight = Step(ShotgunLowerWeight, IsWeaponLayerFullBody() && ShotgunMask != EWeaponLayerMask::Off ? 1.f : 0.f);
	for (FOperativeClipState& Clip : ShotgunClips)
	{
		Clip = FOperativeClipState();
	}
	// The set's pose aim offset (the launcher's Rotation_* poses, the shotgun pack's AimOffset poses): the weights for the aim pitch / yaw
	// (UpdateAimOffset, this frame) x its alpha (0 while reloading, sprinting, without a target ...): the ABP's own aim offset node acts before
	// this layer, which replaces the torso.
	for (int32 Index = 0; Index < 9; ++Index)
	{
		LauncherAimWeights[Index] = 0.f;
	}
	if (bShotgunWeapon && AimOffsetAlpha > 0.01f)
	{
		TStaticArray<bool, 9> AimAvailable;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			AimAvailable[Index] = ShotgunSet.AimPoses[Index] != nullptr;
		}
		const TStaticArray<float, 9> Weights = AimPoseWeights(AimYaw, AimPitch, ShotgunSet.AimYawRange, ShotgunSet.AimPitchRange, AimAvailable);
		for (int32 Index = 0; Index < 9; ++Index)
		{
			LauncherAimWeights[Index] = Index == static_cast<int32>(EWeaponAimPose::Center) ? 0.f : Weights[Index] * AimOffsetAlpha;
		}
	}
	// The body's own turn this frame (a full-body set's turn in place follows it).
	const float BodyYaw = Operative ? static_cast<float>(Operative->GetActorRotation().Yaw) : 0.f;
	const float YawDelta = bLayerYawValid ? FMath::FindDeltaAngleDegrees(LayerLastYaw, BodyYaw) : 0.f;
	LayerLastYaw = BodyYaw;
	bLayerYawValid = Operative != nullptr;
	if (ShotgunUpperWeight <= OperativeShotgunLayer::ShotgunMinWeight && ShotgunLowerWeight <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		LayerTurnShare = 0.f;
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
	// Turn in place (a full-body set, designer 2026-11-09 b): standing still and turning, the pack's Turn_Left / Turn_Right (Sit_Turn_* crouched)
	// play by the body's own turn - the clip turns 90 deg in its root (dropped: the capsule turns), so its time follows the yaw and the feet
	// stay planted; the share eases in / out with the turn rate.
	float TurnWeight = 0.f;
	// Not with the Rifle2 graph's root yaw offset (it keeps the body still while the capsule turns: a turn clip would turn it twice).
	if (ShotgunSet.bFullBody && !bUseRifle2Locomotion && (ShotgunSet.TurnLeft || ShotgunSet.TurnRight))
	{
		const float YawRate = DeltaSeconds > KINDA_SMALL_NUMBER ? YawDelta / DeltaSeconds : 0.f;
		const bool bCrouchTurn = ShotgunCrouchShare >= 0.5f && (ShotgunSet.CrouchTurnLeft || ShotgunSet.CrouchTurnRight);
		const bool bAimTurn = !bCrouchTurn && ShotgunAimShare >= 0.5f && ShotgunSet.AimTurnLeft && ShotgunSet.AimTurnRight;
		UAnimSequence* TurnClip = YawDelta < 0.f ? (bCrouchTurn ? ShotgunSet.CrouchTurnLeft : (bAimTurn ? ShotgunSet.AimTurnLeft : ShotgunSet.TurnLeft))
			: (bCrouchTurn ? ShotgunSet.CrouchTurnRight : (bAimTurn ? ShotgunSet.AimTurnRight : ShotgunSet.TurnRight));
		const float Want = TurnInPlaceShare(YawRate, MoveSpeed, ShotgunSet.MoveStartSpeed);
		if (Want > 0.f && TurnClip && TurnClip != LayerTurnClip)
		{
			LayerTurnClip = TurnClip; // a new turn (or the other way round) starts the clip from its first frame
			LayerTurnTime = 0.f;
		}
		LayerTurnShare = OperativeShotgunLayer::EaseShare(LayerTurnShare, Want > 0.f ? 1.f : 0.f, DeltaSeconds, 0.15f);
		if (LayerTurnClip)
		{
			LayerTurnTime = FMath::Fmod(LayerTurnTime + TurnClipAdvance(YawDelta, ShotgunSet.TurnClipDeg, LayerTurnClip->GetPlayLength()),
				FMath::Max(0.1f, LayerTurnClip->GetPlayLength()));
		}
		if (LayerTurnShare <= 0.f)
		{
			LayerTurnClip = nullptr;
		}
		TurnWeight = LayerTurnShare;
	}
	else
	{
		LayerTurnShare = 0.f;
		LayerTurnClip = nullptr;
	}
	Add(LayerTurnClip, IdleWeight * TurnWeight, LayerTurnTime);
	const float StillWeight = IdleWeight * (1.f - TurnWeight);
	Add(ShotgunSet.CrouchIdle, StillWeight * ShotgunCrouchShare, IdleTime(ShotgunSet.CrouchIdle));
	Add(AimIdle, StillWeight * (1.f - ShotgunCrouchShare) * ShotgunAimShare, IdleTime(AimIdle));
	Add(ShotgunSet.StandIdle, StillWeight * (1.f - ShotgunCrouchShare) * (1.f - ShotgunAimShare), IdleTime(ShotgunSet.StandIdle));
	if (Move <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		return;
	}
	const float Stand = 1.f - ShotgunCrouchShare;
	// A full-body set blends walk -> jog over its own speed band (jog_blend_speeds; the pack's walk 154 / jog 439 cm/s are far apart: a long
	// mixed band slides the feet), else over walk_speed .. jog_speed.
	const float JogShare = ShotgunSet.JogBlendFullSpeed > ShotgunSet.JogBlendStartSpeed
		? JogWeight(MoveSpeed, ShotgunSet.JogBlendStartSpeed, ShotgunSet.JogBlendFullSpeed) : JogWeight(MoveSpeed, ShotgunSet.WalkSpeed, ShotgunSet.JogSpeed);
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
	// A set without aim-walk loops aims with its walk / jog loops (the launcher).
	const float AimMoveShare = ShotgunSet.bHasAimWalk ? ShotgunAimShare : 0.f;
	// The nominal speed of the loops in play: per direction where the set lists it (the launcher pack's walk forward 92 vs back / sides
	// 112.5 cm/s), so a full-body set's feet stay planted in every direction.
	const float WalkNominal = AimMoveShare >= 0.5f ? BlendedNominalSpeed(AimDir, ShotgunSet.AimWalkSpeeds, ShotgunSet.WalkSpeed)
		: BlendedNominalSpeed(WalkDir, ShotgunSet.WalkSpeeds, ShotgunSet.WalkSpeed);
	const float JogNominal = BlendedNominalSpeed(JogDir, ShotgunSet.JogSpeeds, ShotgunSet.JogSpeed);
	const float LeadNominal = bSprintLead ? ShotgunSet.SprintSpeed
		: bCrouchLead ? BlendedNominalSpeed(CrouchDir, ShotgunSet.CrouchSpeeds, ShotgunSet.CrouchSpeed)
		: FMath::Lerp(WalkNominal, JogNominal, AimMoveShare >= 0.5f ? 0.f : JogShare);
	const float LeadLength = Lead ? FMath::Max(0.1f, Lead->GetPlayLength()) : 1.f;
	if (ShotgunSet.bFullBody)
	{
		// A full-body set's feet: the shared phase advances by the body's speed over the blended CYCLE DISTANCE (each loop's natural speed x its
		// length, weighted like the clips below) - every loop then covers its own stride per cycle and the blend's feet stay planted (a single
		// lead clip's rate let the other gait's feet slide at the walk / jog / sprint mixes).
		auto Length = [](const UAnimSequence* Clip) { return Clip ? FMath::Max(0.1f, Clip->GetPlayLength()) : 1.f; };
		auto DirLength = [&Length](const TStaticArray<UAnimSequence*, 8>& Set, const FWeaponLayerDirectionBlend& Dir)
		{
			return FMath::Lerp(Length(Set[static_cast<int32>(Dir.A)]), Length(Set[static_cast<int32>(Dir.B)]), Dir.Alpha);
		};
		const float SprintW = ShotgunSet.Sprint ? ShotgunSprintShare : 0.f;
		const float CrouchW = ShotgunCrouchShare;
		const float UprightW = (1.f - CrouchW) * (1.f - SprintW);
		const float Weights[5] = { CrouchW, (1.f - CrouchW) * SprintW, UprightW * AimMoveShare, UprightW * (1.f - AimMoveShare) * (1.f - JogShare),
			UprightW * (1.f - AimMoveShare) * JogShare };
		const float Cycles[5] = {
			BlendedNominalSpeed(CrouchDir, ShotgunSet.CrouchSpeeds, ShotgunSet.CrouchSpeed) * DirLength(ShotgunSet.Crouch, CrouchDir),
			ShotgunSet.SprintSpeed * Length(ShotgunSet.Sprint),
			BlendedNominalSpeed(AimDir, ShotgunSet.AimWalkSpeeds, ShotgunSet.WalkSpeed) * DirLength(ShotgunSet.AimWalk, AimDir),
			BlendedNominalSpeed(WalkDir, ShotgunSet.WalkSpeeds, ShotgunSet.WalkSpeed) * DirLength(ShotgunSet.Walk, WalkDir),
			JogNominal * DirLength(ShotgunSet.Jog, JogDir) };
		float Cycle = 0.f;
		float Total = 0.f;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Cycle += Weights[Index] * Cycles[Index];
			Total += Weights[Index];
		}
		Cycle = Total > KINDA_SMALL_NUMBER ? Cycle / Total : LeadNominal * LeadLength;
		const float PhaseRate = Cycle > 1.f ? MoveSpeed / Cycle : 0.f;
		// The lead loop's own rate kept within 0.5 .. max_play_rate.
		ShotgunPhase = FMath::Fmod(ShotgunPhase + DeltaSeconds * FMath::Clamp(PhaseRate, 0.5f / LeadLength, ShotgunSet.MaxPlayRate / LeadLength), 1.f);
	}
	else
	{
		ShotgunPhase = FMath::Fmod(ShotgunPhase + DeltaSeconds * LoopPlayRate(MoveSpeed, LeadNominal, 0.5f, ShotgunSet.MaxPlayRate) / LeadLength, 1.f);
	}
	// Each loop on the shared phase plus its own sync offset (the left heel strike; phase_offsets).
	auto At = [this, &ShotgunSet](const UAnimSequence* Clip)
	{
		return Clip ? SyncedClipTime(ShotgunPhase, ShotgunSet.PhaseOffsets.FindRef(Clip->GetFName()), Clip->GetPlayLength()) : 0.f;
	};
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
	AddPair(ShotgunSet.AimWalk, AimDir, Upright * AimMoveShare);
	AddPair(ShotgunSet.Walk, WalkDir, Upright * (1.f - AimMoveShare) * (1.f - JogShare));
	AddPair(ShotgunSet.Jog, JogDir, Upright * (1.f - AimMoveShare) * JogShare);
}

bool UOperativeAnimInstance::PlayShotgunFire()
{
	if (LayerHandling == EWeaponHandling::Launcher)
	{
		return PlayLauncherShot(); // the RPG-7: the pack's Shot / Sit_Shot natively on the upper body
	}
	const FShotgunAnimSet& ShotgunSet = ActiveWeaponSet();
	if (!IsShotgunLayerEligible() || (!ShotgunSet.FireAim && !ShotgunSet.FireHip) || bIsReloading)
	{
		return false;
	}
	UAnimSequenceBase* Clip = bIsAiming && ShotgunSet.FireAim ? ShotgunSet.FireAim : (ShotgunSet.FireHip ? ShotgunSet.FireHip : ShotgunSet.FireAim);
	if (Clip && Clip->IsValidAdditive())
	{
		// The pistol's recoil is an additive clip: played natively on top of the layer's aim (BlendWeaponFireAdditive), no montage.
		WeaponFireAdditiveClip = Clip;
		WeaponFireAdditiveTime = 0.f;
		++ShotgunFireClips;
		return true;
	}
	PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, 0.05f, 0.2f);
	++ShotgunFireClips;
	return true;
}

bool UOperativeAnimInstance::UpdateShotgunReload(const AOperativeCharacter* Operative, float DeltaSeconds)
{
	// The designer (2026-11-08, the render): "during the reload the character shakes strangely" - the pack's single 2.4 s reload clip was
	// restarted for every shell at x4.8 (measured: torso / head 15.3 cm, left hand 40 cm in one frame). Now ONE pass of the clip, natively on
	// the upper body: the intro (raise / tilt, the first shell) once, the pack's own shell-insertion loop (weapon_anim_sets.json
	// reload_shell_start_s .. _end_s, measured with Scripts/Editor/measure_shotgun_reload.py) once per shell at a near-natural rate, the
	// outro (pump, back to the aim) once at the end; no montage, so the layer keeps the legs and nothing restarts.
	const FShotgunAnimSet& ShotgunSet = ActiveWeaponSet();
	if (!Operative || !bShotgunWeapon || LayerHandling == EWeaponHandling::Launcher || !ShotgunSet.Reload || bIsProne || bKnockedDown || bIsClimbing
		|| !Operative->CurrentWeapon)
	{
		ShotgunReloadLastClip = -1;
		ShotgunReload = FShellReloadState();
		ShotgunReloadWeight = 0.f;
		return false;
	}
	const FWeaponAmmoStats& Ammo = Operative->CurrentWeapon->Stats.Ammo;
	const bool bPerRound = WeaponBallisticsRules::ReloadsPerRound(Ammo);
	const float Step = FMath::Max(0.1f, bPerRound ? WeaponBallisticsRules::ReloadStepSeconds(Ammo) : Operative->ReloadTimer);
	FShellReloadClip Clip;
	Clip.Length = ShotgunSet.Reload->GetPlayLength();
	Clip.LoopStart = ShotgunSet.ShellStart;
	Clip.LoopEnd = ShotgunSet.ShellEnd;
	Clip.OutroStart = ShotgunSet.OutroStart;
	// A magazine weapon (the pistol): the whole clip is the "loop", fitted to the reload time (wider rate band); the D-1: one loop per shell.
	Clip.LoopRate = bPerRound ? WeaponAnimLayerRules::ShellLoopRate(ShotgunSet.ShellStart, ShotgunSet.ShellEnd, Step)
		: WeaponAnimLayerRules::ShellLoopRate(0.f, Clip.Length, Step, 0.5f, 2.5f);
	WeaponAnimLayerRules::AdvanceShellReload(ShotgunReload, Clip, DeltaSeconds, bIsReloading);
	// A shell in: counted for the smokes (one insertion loop per shell at the matching rate).
	const int32 Shells = Operative->CurrentClip;
	if (ShotgunReloadLastClip >= 0 && Shells > ShotgunReloadLastClip && (bIsReloading || bWasReloading))
	{
		++ShotgunShellClips;
	}
	ShotgunReloadLastClip = Shells;
	// Weight: in over reload_blend_in_s, out over reload_blend_out_s - and out at once under any other upper / full-body clip (a shot
	// that interrupts the reload, a hit, a grenade, a stance change).
	const bool bOtherClip = GetSlotMontageGlobalWeight(UpperBodySlot) > 0.01f || GetSlotMontageGlobalWeight(FullBodySlot) > 0.01f || IsPlayingStanceTransition();
	const bool bWant = ShotgunReload.Phase != EShellReloadPhase::None && !bOtherClip;
	const float Seconds = bWant ? ShotgunSet.ReloadBlendIn : ShotgunSet.ReloadBlendOut;
	ShotgunReloadWeight = FMath::FInterpConstantTo(ShotgunReloadWeight, bWant ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(0.01f, Seconds));
	if (bOtherClip && ShotgunReloadWeight <= 0.f)
	{
		ShotgunReload = FShellReloadState(); // given way: the next reload starts from the intro
	}
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
		BlendWeaponReload(Output);
		BlendWeaponFireAdditive(Output);
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
	// The launcher: the shot / reload clip, then the aim poses, inside the layer's pose (the torso below then sits on the ABP's pelvis).
	BlendWeaponAction(Layer, LayerCurve, LayerAttributes);
	BlendWeaponAimPoses(Layer, LayerCurve, LayerAttributes);

	// Upper body ONLY (designer 2026-11-09): spine_01 and everything under it take the layer; the root, pelvis, legs, feet and IK bones keep the
	// ABP's pose (the rifle locomotion), in every state.
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	const float Weight = FMath::Min(1.f, WeaponUpperWeight);
	if (bWeaponFullBody)
	{
		// A "full" set (the launcher, designer 2026-11-09 b): every bone but the root from the set - the pelvis and legs at the lower weight (it
		// stays under a hit reaction), the torso at the upper weight; the root keeps the graph's (the capsule-relative placement).
		const float Lower = FMath::Min(1.f, WeaponLowerWeight);
		TArray<bool, TInlineAllocator<128>> UpperFull;
		UpperFull.SetNumZeroed(Output.Pose.GetNumBones());
		for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
			UpperFull[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && UpperFull[Parent.GetInt()]);
			if (Index.IsRootBone())
			{
				// The root: the graph's translation, but NOT its yaw - the ABP's orientation warping / root yaw offset turns the root towards the
				// movement for ITS forward-only legs; the set's own directional legs on that turned root walked ~20-35 deg off the travel and
				// their planted feet slid (ShotgunSmoke 2b). The set's root faces the capsule (the reference rotation).
				if (Lower > OperativeShotgunLayer::ShotgunMinWeight)
				{
					const FTransform& Reference = Output.Pose.GetBoneContainer().GetRefPoseTransform(Index);
					Output.Pose[Index].SetRotation(FQuat::Slerp(Output.Pose[Index].GetRotation(), Reference.GetRotation(), Lower).GetNormalized());
					if (bWeaponRootReference)
					{
						// "root": "reference" (the launcher): the graph's root translation too (the ABP's crouch lowers its root for ITS crouch clips;
						// the pack's crouch on that root sank its planted feet ~8 cm into the ground).
						Output.Pose[Index].SetTranslation(FMath::Lerp(Output.Pose[Index].GetTranslation(), Reference.GetTranslation(), Lower));
					}
				}
				continue;
			}
			const float BoneWeight = UpperFull[Index.GetInt()] ? Weight : Lower;
			if (BoneWeight > OperativeShotgunLayer::ShotgunMinWeight)
			{
				Output.Pose[Index].BlendWith(Layer[Index], BoneWeight);
			}
		}
		BlendWeaponReload(Output);
		BlendWeaponFireAdditive(Output);
		return;
	}
	if (Spine.GetInt() != INDEX_NONE && Weight > OperativeShotgunLayer::ShotgunMinWeight)
	{
		// The torso must face where the clip's torso faces on top of the ABP's pelvis (whose yaw / sway differs from the clip's): spine_01's
		// MESH-SPACE rotation is taken from the layer (the clip), so its local rotation is re-expressed under the ABP's pelvis chain - the same
		// as Layered Blend Per Bone's "Mesh Space Rotation Blend". Everything under spine_01 keeps the clip's local transforms (relative to it).
		auto MeshRotation = [](const FCompactPose& Pose, FCompactPoseBoneIndex Bone, bool bSkipRoot)
		{
			FQuat Rotation = FQuat::Identity;
			for (; Bone.GetInt() != INDEX_NONE; Bone = Pose.GetParentBoneIndex(Bone))
			{
				if (!(bSkipRoot && Bone.IsRootBone()))
				{
					Rotation = Pose[Bone].GetRotation() * Rotation;
				}
			}
			return Rotation;
		};
		const FCompactPoseBoneIndex SpineParent = Output.Pose.GetParentBoneIndex(Spine);
		// The root keeps the graph's (as before: the capsule-relative placement), so the clip's chain is taken below the root and put under the ABP's root.
		const FQuat ParentGraph = SpineParent.GetInt() != INDEX_NONE ? MeshRotation(Output.Pose, SpineParent, false) : FQuat::Identity;
		// A full set's crouched torso (bWeaponTorsoOnCapsule) is put on the reference root (the capsule's facing): the ABP's crouch root yaw
		// turned the launcher 23 deg off the target (RpgShots "tube yaw"); the upper sets keep the ABP's root (their torso turns with its legs).
		const FCompactPoseBoneIndex RootIndex(0);
		const FQuat RootRotation = bWeaponTorsoOnCapsule ? Bones.GetRefPoseTransform(RootIndex).GetRotation() : Output.Pose[RootIndex].GetRotation();
		const FQuat ParentLayer = SpineParent.GetInt() != INDEX_NONE ? RootRotation * MeshRotation(Layer, SpineParent, true) : FQuat::Identity;
		// The layer's spine_01 mesh rotation = ParentLayer * LocalLayer; under the ABP's pelvis: local = ParentGraph^-1 * ParentLayer * LocalLayer.
		const FQuat Compensated = (ParentGraph.Inverse() * ParentLayer * Layer[Spine].GetRotation()).GetNormalized();
		Layer[Spine].SetRotation(Compensated);
	}
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		if (Upper[Index.GetInt()] && Weight > OperativeShotgunLayer::ShotgunMinWeight)
		{
			Output.Pose[Index].BlendWith(Layer[Index], Weight);
		}
	}
	BlendWeaponReload(Output);
	BlendWeaponFireAdditive(Output);
}

float UOperativeAnimInstance::GetWeaponLayerLowerDeviation() const
{
	return GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetWeaponProbeLower();
}

float UOperativeAnimInstance::GetWeaponLayerUpperDeviation() const
{
	return GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetWeaponProbeUpper();
}

void FOperativeAnimInstanceProxy::RecordWeaponProbe(const FCompactPose& Before, const FCompactPose& After) const
{
	const FBoneContainer& Bones = After.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(After.GetNumBones());
	float Lower = 0.f;
	float UpperMax = 0.f;
	for (const FCompactPoseBoneIndex Index : After.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = After.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		const float Move = static_cast<float>(FVector::Dist(Before[Index].GetTranslation(), After[Index].GetTranslation()))
			+ FMath::RadiansToDegrees(static_cast<float>(Before[Index].GetRotation().AngularDistance(After[Index].GetRotation())));
		float& Target = Upper[Index.GetInt()] ? UpperMax : Lower;
		Target = FMath::Max(Target, Move);
	}
	WeaponProbeLower = Lower;
	WeaponProbeUpper = UpperMax;
}

void FOperativeAnimInstanceProxy::BlendWeaponFireAdditive(FPoseContext& Output) const
{
	if (!WeaponFireAdditive)
	{
		return;
	}
	FCompactPose Pose;
	Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
	FBlendedCurve Curve;
	Curve.InitFrom(Output.Curve);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData Additive(Pose, Curve, Attributes);
	WeaponFireAdditive->GetAnimationPose(Additive, FAnimExtractContext(static_cast<double>(WeaponFireAdditiveTime), false));
	// Upper body only: the pelvis / legs / root stay the ABP's, whatever the additive clip holds there.
	const FCompactPose Before = Output.Pose;
	FAnimationPoseData Base(Output);
	FAnimationRuntime::AccumulateAdditivePose(Base, Additive, 1.f, WeaponFireAdditive->GetAdditiveAnimType());
	Output.Pose.NormalizeRotations();
	const int32 SpineMeshIndex = Output.Pose.GetBoneContainer().GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Output.Pose.GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex))
		: FCompactPoseBoneIndex(INDEX_NONE);
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		if (!Upper[Index.GetInt()])
		{
			Output.Pose[Index] = Before[Index];
		}
	}
}

void FOperativeAnimInstanceProxy::BlendWeaponReload(FPoseContext& Output) const
{
	// The per-shell reload: the clip's pose at its time on spine_01 and every bone under it (the arms, the gun hand), local space - the legs
	// keep the locomotion / the layer.
	if (!WeaponReloadClip || WeaponReloadWeight <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		return;
	}
	FCompactPose Pose;
	Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
	FBlendedCurve Curve;
	Curve.InitFrom(Output.Curve);
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(Pose, Curve, Attributes);
	WeaponReloadClip->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(WeaponReloadTime), false));
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	if (SpineMeshIndex == INDEX_NONE)
	{
		return;
	}
	const FCompactPoseBoneIndex Spine = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex));
	TArray<bool, TInlineAllocator<128>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	const float Weight = FMath::Min(1.f, WeaponReloadWeight);
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Index);
		Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
		if (Upper[Index.GetInt()])
		{
			Output.Pose[Index].BlendWith(Pose[Index], Weight);
		}
	}
}

// --- The launcher set (the RPG-7, 2026-11-09) ---------------------------------------------------------------------------------------------

bool UOperativeAnimInstance::PlayLauncherShot()
{
	const FShotgunAnimSet& Set = LauncherSetData;
	if (LayerHandling != EWeaponHandling::Launcher || !IsShotgunLayerEligible())
	{
		return false;
	}
	UAnimSequenceBase* Clip = bIsCrouching && Set.FireCrouch ? Set.FireCrouch : (Set.FireAim ? Set.FireAim : Set.FireHip);
	if (!Clip)
	{
		return false;
	}
	// The pack's kick starts on frame 0 (measured by Scripts/Editor/setup_launcher_animation.py): the rocket leaves with the trigger.
	LauncherShotClip = Clip;
	LauncherShotTime = 0.f;
	++LauncherShotClips;
	++ShotgunFireClips;
	return true;
}

bool UOperativeAnimInstance::UpdateLauncherAction(AOperativeCharacter* Operative, float DeltaSeconds)
{
	using namespace WeaponAnimLayerRules;
	const FShotgunAnimSet& Set = LauncherSetData;
	const bool bLauncher = Operative && Operative->CurrentWeapon && Operative->CurrentWeapon->Stats.Handling == EWeaponHandling::Launcher && Set.StandIdle;
	if (!bLauncher)
	{
		LauncherShotTime = -1.f;
		LauncherShotClip = nullptr;
		bLauncherReloadActive = false;
		LauncherReloadClip = nullptr;
		LauncherAction = ELauncherAction::None;
		LauncherActionWeight = 0.f;
		LauncherActionClip = nullptr;
		return false;
	}
	LayerHandling = EWeaponHandling::Launcher; // UpdateShotgunLayer runs later this frame and resolves the same
	// The shot clock.
	if (LauncherShotTime >= 0.f)
	{
		LauncherShotTime += DeltaSeconds;
		if (!LauncherShotClip || LauncherShotTime >= LauncherShotClip->GetPlayLength())
		{
			LauncherShotTime = -1.f;
		}
	}
	const float ShotLength = LauncherShotClip ? LauncherShotClip->GetPlayLength() : 0.f;
	// The reload: the whole clip fitted to the weapon's reload time after the shot's follow-through (the reload starts at the launch).
	float ReloadClipTime = -1.f;
	UAnimSequenceBase* Reload = bIsCrouching && Set.ReloadCrouch ? Set.ReloadCrouch : Set.Reload;
	if (bIsReloading && Reload && Operative->CurrentWeapon)
	{
		if (!bLauncherReloadActive)
		{
			bLauncherReloadActive = true;
			LauncherReloadLeadIn = LauncherShotTime >= 0.f ? FMath::Max(0.f, ShotLength - LauncherShotTime) : 0.f;
		}
		LauncherReloadClip = Reload;
		const float Total = FMath::Max(0.05f, WeaponBallisticsRules::ReloadStepSeconds(Operative->CurrentWeapon->Stats.Ammo));
		const float Elapsed = Total * Operative->GetReloadProgress01();
		const float Length = Reload->GetPlayLength();
		ReloadClipTime = LauncherReloadClipTime(Elapsed, Total, Length, LauncherReloadLeadIn);
		const float Insert = Reload == Set.ReloadCrouch ? Set.ReloadCrouchInsert : Set.ReloadInsert;
		Operative->LauncherRoundInsertFraction = LauncherInsertFraction(Insert, Length, Total, LauncherReloadLeadIn);
	}
	else
	{
		bLauncherReloadActive = false;
	}
	LauncherAction = ResolveLauncherAction(LauncherShotTime, ShotLength, ReloadClipTime);
	if (LauncherAction == ELauncherAction::Shot)
	{
		LauncherActionClip = LauncherShotClip;
		LauncherActionTime = LauncherShotTime;
	}
	else if (LauncherAction == ELauncherAction::Reload)
	{
		LauncherActionClip = LauncherReloadClip;
		LauncherActionTime = ReloadClipTime;
	}
	// The weight: in fast for the shot, a little slower for the reload; out from the clip's own last pose (the shouldered idle).
	const bool bWant = LauncherAction != ELauncherAction::None && LauncherActionClip;
	const float Seconds = bWant ? (LauncherAction == ELauncherAction::Shot ? Set.FireBlendIn : Set.ReloadBlendIn)
		: (LauncherActionClip == LauncherShotClip ? Set.FireBlendOut : Set.ReloadBlendOut);
	LauncherActionWeight = FMath::FInterpConstantTo(LauncherActionWeight, bWant ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(0.01f, Seconds));
	if (!bWant && LauncherActionWeight <= 0.f)
	{
		LauncherActionClip = nullptr;
	}
	// The launcher set owns the reload while its layer can play (not prone / in cover / on a ladder ...): no rifle reload montage then.
	return IsShotgunLayerEligible();
}

UAnimSequenceBase* UOperativeAnimInstance::PickLauncherHitClip()
{
	const FShotgunAnimSet& Set = LauncherSetData;
	if (LayerHandling != EWeaponHandling::Launcher || Set.Hits.IsEmpty() || !IsShotgunLayerEligible())
	{
		return nullptr;
	}
	++LauncherHitClips;
	return Set.Hits[LauncherHitClips % Set.Hits.Num()]; // alternate the pack's two hits
}

UAnimSequenceBase* UOperativeAnimInstance::PickLauncherDeathClip(const AOperativeCharacter& Operative) const
{
	const FShotgunAnimSet& Set = LauncherSetData;
	if (LayerHandling != EWeaponHandling::Launcher || Set.Deaths.IsEmpty() || Operative.GetStance() != EOperativeStance::Standing || bInCover)
	{
		return nullptr;
	}
	// Dead_01 falls backwards (a blow from the front), Dead_02 forwards (from behind); both end lying (pelvis ~16 cm, measured).
	FVector Source;
	const bool bFromBehind = Operative.GetLastHitSource(Source)
		&& HitReactionRules::IsFromBehind(Operative.GetActorForwardVector(), Operative.GetActorLocation(), Source);
	++LauncherDeathClips;
	return Set.Deaths[bFromBehind && Set.Deaths.Num() > 1 ? 1 : 0];
}

float UOperativeAnimInstance::GetLauncherAimWeightSum() const
{
	float Sum = 0.f;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Sum += LauncherAimWeights[Index];
	}
	return Sum;
}

void FOperativeAnimInstanceProxy::BlendWeaponAction(FCompactPose& Layer, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes) const
{
	if (!WeaponActionClip || WeaponActionWeight <= OperativeShotgunLayer::ShotgunMinWeight)
	{
		return;
	}
	FCompactPose Pose;
	Pose.SetBoneContainer(&Layer.GetBoneContainer());
	FBlendedCurve ActionCurve;
	ActionCurve.InitFrom(Curve);
	UE::Anim::FStackAttributeContainer ActionAttributes;
	FAnimationPoseData PoseData(Pose, ActionCurve, ActionAttributes);
	WeaponActionClip->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(WeaponActionTime), false));
	const float Weight = FMath::Min(1.f, WeaponActionWeight);
	const FBoneContainer& Bones = Layer.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	if (bWeaponFullBody && Spine.GetInt() != INDEX_NONE)
	{
		// The full-body set's legs keep walking under a shot / reload (the clips are standing still): the action takes spine_01 and below, its
		// torso re-expressed on the layer's pelvis (mesh-space rotation, as the upper-body layer does on the ABP's pelvis).
		auto MeshRotation = [](const FCompactPose& InPose, FCompactPoseBoneIndex Bone)
		{
			FQuat Rotation = FQuat::Identity;
			for (; Bone.GetInt() != INDEX_NONE; Bone = InPose.GetParentBoneIndex(Bone))
			{
				Rotation = InPose[Bone].GetRotation() * Rotation;
			}
			return Rotation;
		};
		const FCompactPoseBoneIndex SpineParent = Layer.GetParentBoneIndex(Spine);
		if (SpineParent.GetInt() != INDEX_NONE)
		{
			Pose[Spine].SetRotation((MeshRotation(Layer, SpineParent).Inverse() * MeshRotation(Pose, SpineParent) * Pose[Spine].GetRotation()).GetNormalized());
		}
		TArray<bool, TInlineAllocator<128>> Upper;
		Upper.SetNumZeroed(Layer.GetNumBones());
		for (const FCompactPoseBoneIndex Index : Layer.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex Parent = Layer.GetParentBoneIndex(Index);
			Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
			if (Upper[Index.GetInt()])
			{
				Layer[Index].BlendWith(Pose[Index], Weight);
			}
		}
		Layer.NormalizeRotations();
		return;
	}
	for (const FCompactPoseBoneIndex Index : Layer.ForEachBoneIndex())
	{
		Layer[Index].BlendWith(Pose[Index], Weight);
	}
	Layer.NormalizeRotations();
}

void FOperativeAnimInstanceProxy::BlendWeaponAimPoses(FCompactPose& Layer, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes) const
{
	// Each Rotation_* clip turns the shouldered idle (its first frame) into the aimed pose (its last frame): that difference, as a mesh-space
	// rotation additive (the AnimGraph's aim offset kind), weighted by the bilinear pose weights x the aim offset alpha. A full-body set keeps
	// its legs and pelvis (the aim bends the torso only; the pelvis share of a pose's twist is dropped).
	const FCompactPose LegsBefore = bWeaponFullBody ? Layer : FCompactPose();
	for (int32 Index = 0; Index < 9; ++Index)
	{
		const UAnimSequenceBase* Clip = WeaponAimPoses[Index];
		const float Weight = WeaponAimWeights[Index];
		if (!Clip || Weight <= OperativeShotgunLayer::ShotgunMinWeight)
		{
			continue;
		}
		FCompactPose Aimed;
		Aimed.SetBoneContainer(&Layer.GetBoneContainer());
		FCompactPose Start;
		Start.SetBoneContainer(&Layer.GetBoneContainer());
		FBlendedCurve AimedCurve;
		AimedCurve.InitFrom(Curve);
		FBlendedCurve StartCurve;
		StartCurve.InitFrom(Curve);
		UE::Anim::FStackAttributeContainer AimedAttributes;
		UE::Anim::FStackAttributeContainer StartAttributes;
		FAnimationPoseData AimedData(Aimed, AimedCurve, AimedAttributes);
		FAnimationPoseData StartData(Start, StartCurve, StartAttributes);
		Clip->GetAnimationPose(AimedData, FAnimExtractContext(static_cast<double>(Clip->GetPlayLength()), false));
		(WeaponAimBase ? WeaponAimBase : Clip)->GetAnimationPose(StartData, FAnimExtractContext(0.0, false));
		FAnimationRuntime::ConvertPoseToMeshRotation(Aimed);
		FAnimationRuntime::ConvertPoseToMeshRotation(Start);
		FAnimationRuntime::ConvertPoseToAdditive(Aimed, Start);
		FAnimationPoseData Base(Layer, Curve, Attributes);
		FAnimationRuntime::AccumulateAdditivePose(Base, AimedData, FMath::Min(1.f, Weight), AAT_RotationOffsetMeshSpace);
	}
	Layer.NormalizeRotations();
	if (bWeaponFullBody && LegsBefore.GetNumBones() == Layer.GetNumBones())
	{
		const FBoneContainer& Bones = Layer.GetBoneContainer();
		const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
		const FCompactPoseBoneIndex Spine = SpineMeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
		TArray<bool, TInlineAllocator<128>> Upper;
		Upper.SetNumZeroed(Layer.GetNumBones());
		for (const FCompactPoseBoneIndex Index : Layer.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex Parent = Layer.GetParentBoneIndex(Index);
			Upper[Index.GetInt()] = Index == Spine || (Parent.GetInt() != INDEX_NONE && Upper[Parent.GetInt()]);
			if (!Upper[Index.GetInt()])
			{
				Layer[Index] = LegsBefore[Index];
			}
		}
	}
}
