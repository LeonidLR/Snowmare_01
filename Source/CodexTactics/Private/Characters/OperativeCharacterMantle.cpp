// AOperativeCharacter: mantling, slice 1 (docs/design/MANTLING.md section 11, designer answers 11.6; docs/port/MANTLING_SLICE1_REPORT.md).
// One operative, Solo: approach -> reserve -> climb (the clip's root motion in MOVE_Flying, warped onto the real ledge top / far-side landing by
// the engine Motion Warping plugin - no manual translation on top of it) -> land -> crouch on a top / walk on. Patterns reused: the vault
// (OperativeCharacterVault.cpp: stop, resume target, the arc for the hop down), the ladder (LadderClimbComponent.cpp: the saved movement /
// rotation / weapon snapshot restored exactly once, path resume), the drop links (the link callback taken on the next tick).
// Pure rules: Characters/MantleRules. Links / probe / reservations: Interactables/MantleNavigation. UE-only (Godot had no mantling).

#include "Characters/OperativeCharacter.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "BonePose.h"
#include "Characters/MantleRules.h"
#include "Characters/OperativeAIController.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Misc/MemStack.h"
#include "Interactables/VaultNavigation.h"
#include "MotionWarpingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "RootMotionModifier_SkewWarp.h"
#include "Survival/WarmIdleRules.h"

namespace OperativeMantleInternal
{
	/** The Motion Warping target of every mantle clip (the landing: feet, facing the climb). */
	const FName MantleWarpTarget(TEXT("MantleLanding"));
	/** The first window's target (MantleAlign): where his feet must be at the clip's hand plant so the planted wrist sits on the lip. */
	const FName MantleContactTarget(TEXT("MantleContact"));

	/**
	 * The contact alignment (designer bug report 2026-11-16, docs/port/MANTLING_ALIGN_REPORT.md): 1 = the clip's hand plant is warped onto
	 * the ledge's lip (two warp windows: start -> plant, plant -> landing) and the climb hands over to the locomotion when the clip's motion
	 * is over (its idle tail is skipped); 0 = slice 1's single window over the whole clip (A/B for the designer and the smokes).
	 */
	static TAutoConsoleVariable<int32> CVarMantleAlign(TEXT("Codex.Mantle.Align"), 1,
		TEXT("Mantling: 1 (default) = the hands warped onto the ledge's lip and the climb ends when the clip's motion is over; 0 = slice 1's single warp over the whole clip."));
	/** The face is looked for this far ahead when a climb is ordered / reached (the run clips look further). */
	constexpr float MantleStandingReachCm = 220.f;
	constexpr float MantleRunReachCm = 560.f;
	/** A clicked top's base point counts as reached within this distance (2D), cm. */
	constexpr float MantleOrderArriveCm = 45.f;
	/** A clicked top's walk gives up after this long, s. */
	constexpr float MantleOrderTimeoutSeconds = 30.f;
	/** The hop down off a low top (the vault's arc), s. */
	constexpr float MantleHopSeconds = 0.6f;
	/** A run clip starts once the face is no farther than its own run-up (forward warp <= this; never stretched to start early). */
	constexpr float MantleRunStartWarp = 1.05f;
	/** The torso joints the wall guard keeps off the face (hands and feet may touch the edge / the wall). */
	static const FName MantleTorsoBones[] = { FName(TEXT("pelvis")), FName(TEXT("spine_03")), FName(TEXT("head")) };
}

const FMantleClipTrack* AOperativeCharacter::GetMantleClipTrack(int32 ClipIndex)
{
	UWorld* World = GetWorld();
	UMantleSubsystem* Mantle = World ? World->GetSubsystem<UMantleSubsystem>() : nullptr;
	if (!Mantle)
	{
		return nullptr;
	}
	if (const FMantleClipTrack* Cached = Mantle->FindClipTrack(ClipIndex))
	{
		return Cached;
	}
	const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	const UAnimSequenceBase* Clip = Mantle->GetClip(ClipIndex);
	const FMantleConfig& Config = MantleRules::GetConfig();
	if (!Anim || !Clip || Config.AlignSampleHz <= 0.f)
	{
		return nullptr;
	}
	const FBoneContainer& Bones = Anim->GetRequiredBones();
	static const FName BoneNames[] = { TEXT("root"), TEXT("pelvis"), TEXT("hand_l"), TEXT("hand_r"), TEXT("foot_l"), TEXT("foot_r") };
	FCompactPoseBoneIndex Indices[UE_ARRAY_COUNT(BoneNames)] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE),
		FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
	for (int32 Each = 0; Each < UE_ARRAY_COUNT(BoneNames); ++Each)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(BoneNames[Each]);
		Indices[Each] = MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (!Indices[Each].IsValid())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: no bone %s on %s - no contact alignment"), *DisplayName.ToString(), *BoneNames[Each].ToString(),
				*Clip->GetName());
			return nullptr;
		}
	}
	// The mesh's own turn (the skeleton's +Y forward onto the actor's +X, the mesh's relative yaw -90): the samples in the actor's frame.
	const FQuat Turn = GetBaseRotationOffset();
	FMantleClipTrack Track;
	Track.SampleSeconds = 1.f / Config.AlignSampleHz;
	TArray<FVector>* Out[] = { &Track.Root, &Track.Pelvis, &Track.HandL, &Track.HandR, &Track.FootL, &Track.FootR };
	const float Length = Clip->GetPlayLength();
	const int32 Count = FMath::Max(2, FMath::CeilToInt(Length / Track.SampleSeconds) + 1);
	for (int32 Sample = 0; Sample < Count; ++Sample)
	{
		FMemMark Mark(FMemStack::Get());
		FCSPose<FCompactPose> Pose;
		UMotionWarpingUtilities::ExtractComponentSpacePose(Clip, Bones, FMath::Min(Track.TimeAt(Sample), Length), /*bExtractRootMotion*/ false, Pose);
		for (int32 Each = 0; Each < UE_ARRAY_COUNT(BoneNames); ++Each)
		{
			Out[Each]->Add(Turn.RotateVector(Pose.GetComponentSpaceTransform(Indices[Each]).GetLocation()));
		}
	}
	Mantle->StoreClipTrack(ClipIndex, MoveTemp(Track));
	return Mantle->FindClipTrack(ClipIndex);
}

EMantleBody AOperativeCharacter::GetMantleBody() const
{
	const USkeletalMesh* MantleBodyAsset = GetMesh() ? GetMesh()->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* Skeleton = MantleBodyAsset ? MantleBodyAsset->GetSkeleton() : nullptr;
	return WarmIdleRules::UsesFemaleClips(MantleBodyAsset ? MantleBodyAsset->GetName() : FString(), Skeleton ? Skeleton->GetName() : FString()) ? EMantleBody::Female
		: EMantleBody::Male;
}

bool AOperativeCharacter::ProbeMantleAhead(const FVector& Dir, float ReachCm, FMantleSite& Out) const
{
	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return false;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// The standing capsule's height for the headroom (crouched his capsule may be shorter; he stands up in the clip).
	const AOperativeCharacter* Default = GetClass()->GetDefaultObject<AOperativeCharacter>();
	const float StandingHalfHeight = Default && Default->GetCapsuleComponent() ? Default->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : HalfHeight;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeMantle), false, this);
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	if (!MantleNavigation::ProbeSite(*World, Feet, Dir, ReachCm, Capsule->GetScaledCapsuleRadius(), StandingHalfHeight, Params, Config, Out))
	{
		return false;
	}
	Out.Probe.bRunning = IsRunningForMantle();
	Out.Probe.Body = GetMantleBody();
	Out.Probe.Rotation = MantleRotation;
	if (const UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>(); Mantle && Mantle->IsReservedByOther(Out.Landing, this))
	{
		Out.Probe.bLandingFree = false; // another man's climb lands there
	}
	return true;
}

bool AOperativeCharacter::IsRunningForMantle() const
{
	// Running = above the walk by run_speed_fraction, or a sprint order he is moving on (a sprint flag while standing still is no run-up).
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	return Speed > MovementConfig.WalkSpeed * MantleRules::GetConfig().RunSpeedFraction || (bSprinting && Speed > MovementConfig.WalkSpeed * 0.5f);
}

bool AOperativeCharacter::TryStartMantle(const FVector& Dir, EMantleRefusal* OutRefusal, bool bStopAfter)
{
	auto Refuse = [this, OutRefusal](EMantleRefusal Refusal)
	{
		LastMantleRefusal = Refusal;
		if (OutRefusal)
		{
			*OutRefusal = Refusal;
		}
		return false;
	};
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!World || bMantling || bVaulting || !HealthComponent || !HealthComponent->IsAlive() || IsKnockedDown() || IsClimbingLadder() || IsDroppingOffLedge()
		|| Stance == EOperativeStance::Prone || bCarrying || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: busy - no climb now"), *DisplayName.ToString());
		return Refuse(EMantleRefusal::NoObstacle);
	}
	FMantleSite Site;
	if (!ProbeMantleAhead(Dir, OperativeMantleInternal::MantleRunReachCm, Site) || !Site.bValid)
	{
		return Refuse(EMantleRefusal::NoObstacle);
	}
	UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
	const FMantleConfig& Config = MantleRules::GetConfig();
	FMantleDecision Decision = MantleRules::Decide(Config, Site.Probe, [Mantle](int32 Index) { return Mantle && Mantle->GetClip(Index) != nullptr; });
	if (Decision.IsGo() && Decision.Gait == EMantleGait::Standing && Site.Destination == EMantleDestination::Top
		&& OperativeMantleInternal::CVarMantleAlign.GetValueOnGameThread() != 0 && Config.Clips.IsValidIndex(Decision.ClipIndex))
	{
		// The standing clip's own start: where its planted wrist meets the lip with no forward warp (the clip's rise / forward travel
		// says nothing about where its hands are); farther than that + standing_slack_cm he walks closer first.
		if (const FMantleClipTrack* Track = GetMantleClipTrack(Decision.ClipIndex))
		{
			const float Start = MantleRules::ContactStartCm(Config, MantleRules::AnalyseClipTrack(Config, *Track, Config.Clips[Decision.ClipIndex].RiseCm));
			if (Start > 0.f)
			{
				Decision.ApproachCm = Start;
				Decision.bApproachFirst = Site.Probe.FaceDistanceCm > Start + Config.StandingSlackCm;
			}
		}
	}
	if (!Decision.IsGo())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: no climb at %s (%.0f cm high, %.0f deep, %.0f cm off): %s"), *DisplayName.ToString(),
			*GetNameSafe(Site.Obstacle.Get()), Site.Probe.HeightCm, Site.Probe.DepthCm, Site.Probe.FaceDistanceCm, MantleRules::RefusalName(Decision.Refusal));
		return Refuse(Decision.Refusal);
	}
	if (Decision.bApproachFirst)
	{
		// Too far for the standing clip: walk up to its start, the climb follows on arrival (UpdateMantleTrigger).
		const FVector Base = Site.FacePoint + Site.Normal * Decision.ApproachCm;
		if (RequestMove(FVector(Base.X, Base.Y, Site.Feet.Z)) != EOperativeOrderResult::Accepted)
		{
			return Refuse(EMantleRefusal::NoObstacle);
		}
		bMantleOrderPending = true;
		MantleOrderBase = FVector(Base.X, Base.Y, Site.Feet.Z);
		MantleOrderDir = Site.Dir;
		MantleOrderSeconds = 0.f;
		return true;
	}
	return StartMantle(Site, Decision, bStopAfter, INDEX_NONE) || Refuse(LastMantleRefusal);
}

bool AOperativeCharacter::StartMantle(const FMantleSite& Site, const FMantleDecision& Decision, bool bStopAfter, int32 LinkIndex)
{
	using namespace OperativeMantleInternal;
	UWorld* World = GetWorld();
	const FMantleConfig& Config = MantleRules::GetConfig();
	UMantleSubsystem* Mantle = World ? World->GetSubsystem<UMantleSubsystem>() : nullptr;
	UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	UAnimSequenceBase* Clip = Mantle ? Mantle->GetClip(Decision.ClipIndex) : nullptr;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Mantle || !Anim || !Clip || !MotionWarping || !Movement || !Config.Clips.IsValidIndex(Decision.ClipIndex))
	{
		LastMantleRefusal = EMantleRefusal::MissingClip;
		return false;
	}
	const FMantleClip& ClipData = Config.Clips[Decision.ClipIndex];
	const bool bTop = Site.Destination == EMantleDestination::Top;
	// The landing: a top's planned spot; over a fence the clip's own travel past the face when that ground is free (no squeeze), else the probe's.
	FVector Landing = Site.Landing;
	if (!bTop)
	{
		const float Past = ClipData.ForwardCm - MantleRules::ClipStartCm(Config, ClipData) + (Decision.ApproachCm - Site.Probe.FaceDistanceCm);
		const FVector Far = Site.FacePoint + Site.Dir * FMath::Max(Past, static_cast<float>(FVector::Dist2D(Site.FacePoint, Site.Landing)));
		FMantleSite FarSite = Site;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeMantleFar), false, this);
		const FVector FarFeet(Far.X, Far.Y, Site.Landing.Z);
		if (!World->OverlapAnyTestByChannel(FarFeet + FVector(0.f, 0.f, 47.f), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius() - 2.f, 44.f), Params))
		{
			Landing = FarFeet;
		}
	}
	if (!Mantle->TryReserve(this, Landing))
	{
		LastMantleRefusal = EMantleRefusal::NoLanding;
		return false;
	}
	// Where the walk was heading (a path over the obstacle walks on from the landing; a clicked top stays there).
	if (!bStopAfter && !bMantleResume)
	{
		const AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
		const UPathFollowingComponent* Following = AIController ? AIController->GetPathFollowingComponent() : nullptr;
		bMantleResume = bHasMoveOrder;
		MantleResumeTarget = IsOwnMoveSteered() || !(Following && Following->GetPath().IsValid()) ? LastMoveDestination
			: Following->GetPath()->GetDestinationLocation();
		bMantleResumeOwnOrder = HasOwnMoveOrder();
	}
	bMantleStopAfter = bStopAfter;
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	Movement->StopMovementImmediately();
	if (bInCover)
	{
		LeaveCover(TEXT("mantle"));
	}
	bHasPendingCover = false;
	bMantleOrderPending = false;
	// The snapshot (restored exactly once by RestoreMantleState).
	bMantleSnapshotValid = true;
	MantleSavedMovementMode = static_cast<uint8>(Movement->MovementMode.GetValue());
	bMantleSavedOrientToMovement = Movement->bOrientRotationToMovement;
	bMantleSavedControllerRotation = Movement->bUseControllerDesiredRotation;
	bMantleSavedWeaponVisible = !WeaponMesh || WeaponMesh->IsVisible();
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->SetMovementMode(MOVE_Flying); // the clip's root motion moves him in X, Y and Z
	SetActorEnableCollision(false); // as the vault: the root motion carries the capsule over the lip (the landing was checked free)
	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(false, true); // the clips are authored unarmed: slung for the climb (designer Q3: clip-driven, no fire)
	}
	SetActorRotation(FRotator(0.f, Site.Dir.Rotation().Yaw, 0.f));
	UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, Anim->FullBodySlot, Config.BlendInSeconds, Config.BlendOutSeconds, 1.f, 1, -1.f, 0.f);
	FAnimMontageInstance* Instance = Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!Montage || !Instance)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: %s did not play (skeleton mismatch?) - no climb"), *DisplayName.ToString(), *Clip->GetName());
		RestoreMantleState(false);
		Mantle->Release(this);
		LastMantleRefusal = EMantleRefusal::MissingClip;
		return false;
	}
	Instance->bEnableAutoBlendOut = false; // held on its last frame until FinishMantle (the warp window ends exactly at the landing)
	MotionWarping->AddOrUpdateWarpTargetFromLocationAndRotation(MantleWarpTarget, Landing, Site.Dir.Rotation());
	const float PlayLength = Montage->GetPlayLength();
	float ExitSeconds = PlayLength;
	float ContactSeconds = 0.f;
	FVector ContactFeet = FVector::ZeroVector;
	MantleRootLock = FVector::ZeroVector;
	if (CVarMantleAlign.GetValueOnGameThread() != 0)
	{
		if (const FMantleClipTrack* Track = GetMantleClipTrack(Decision.ClipIndex))
		{
			const FMantleClipContact Contact = MantleRules::AnalyseClipTrack(Config, *Track, ClipData.RiseCm);
			// The climb ends when the clip's motion is over: its idle tail would be the "freeze" before the locomotion takes him on.
			ExitSeconds = FMath::Clamp(MantleNavigation::IsBatch2Enabled() ? Contact.ExitSeconds : Contact.StillExitSeconds, Track->SampleSeconds, PlayLength);
			// Root lock = the first frame: the mesh draws the body offset by the root's frame-0 position for the whole clip (HighClimb: 163 cm
			// up = the "thrown up, then drops" pop; the tall ledges: 36 cm back off the wall). The mesh is shifted back by it, weighted by the
			// montage's blend (UpdateMantleMeshShift): the body is drawn root-relative, where the root motion carries the capsule.
			MantleRootLock = Contact.RootStart;
			if (bTop && Contact.bValid && Contact.ContactSeconds > Track->SampleSeconds && Contact.ContactSeconds < ExitSeconds - Track->SampleSeconds)
			{
				// Window 1 (start -> the plant): his feet (the mesh origin, as the warp counts them) go where the planted wrist lands on the
				// lip (+ the grip offsets); drawn root-relative, the wrist sits HandPoint - RootAtContact from the origin at the plant.
				// Only forward and height are aligned; sideways the clip keeps its own drift.
				const FRotator Facing(0.f, Site.Dir.Rotation().Yaw, 0.f);
				const FVector Origin = GetMesh()->GetComponentLocation();
				const FVector Wrist = Site.EdgeTop + Site.Dir * Config.AlignGripForwardCm + FVector(0.f, 0.f, Config.AlignGripUpCm);
				ContactFeet = Wrist - Facing.RotateVector(Contact.HandPoint - Contact.RootAtContact);
				const FVector Natural = Origin + Facing.RotateVector(Contact.RootAtContact - Contact.RootStart);
				const FVector Across = FVector::CrossProduct(FVector::UpVector, Site.Dir).GetSafeNormal();
				ContactFeet += Across * FVector::DotProduct(Natural - ContactFeet, Across);
				ContactSeconds = Contact.ContactSeconds;
				MotionWarping->AddOrUpdateWarpTargetFromLocationAndRotation(MantleContactTarget, ContactFeet, Facing);
				URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(MotionWarping, Montage, 0.f, ContactSeconds, MantleContactTarget,
					EWarpPointAnimProvider::None, FTransform::Identity, NAME_None, /*bWarpTranslation*/ true, /*bIgnoreZAxis*/ false, /*bWarpRotation*/ true);
			}
		}
	}
	// The (last) window onto the landing: from the plant (or the start) to where the climb hands over.
	URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(MotionWarping, Montage, ContactSeconds, ExitSeconds, MantleWarpTarget,
		EWarpPointAnimProvider::None, FTransform::Identity, NAME_None, /*bWarpTranslation*/ true, /*bIgnoreZAxis*/ false, /*bWarpRotation*/ true);
	bMantling = true;
	MantleTimer = 0.f;
	MantleDuration = PlayLength;
	MantleExitSeconds = ExitSeconds;
	MantleMontage = Montage;
	MantleClipIndex = Decision.ClipIndex;
	MantleLinkIndex = LinkIndex;
	MantleBaseFeet = Site.Feet;
	MantleLandingFeet = Landing;
	MantleDir = Site.Dir;
	MantleFacePoint = Site.FacePoint;
	MantleObstacle = Site.Obstacle;
	MantleTopZ = Site.TopZ;
	MantleDepthCm = Site.Probe.DepthCm;
	MantleGuardPushCm = 0.f;
	bMantleOnTop = bTop;
	++MantleCount;
	++MantleRotation;
	LastMantleClass = Config.Classes[Decision.ClassIndex].Id;
	LastMantleClip = Clip->GetName();
	LastMantleGait = Decision.Gait;
	bLastMantleStopFirst = Decision.bStopFirst;
	LastMantleRefusal = EMantleRefusal::None;
	LastMantleCancel = EMantleCancel::None;
	UE_LOG(LogCodexTactics, Display,
		TEXT("[Mantle] %s climbs %s: %s %.0f cm (%.0f deep, %.0f cm off the face) with %s (%s%s), warp z x%.2f fwd x%.2f, %s, landing %s"),
		*DisplayName.ToString(), *GetNameSafe(Site.Obstacle.Get()), *LastMantleClass.ToString(), Site.Probe.HeightCm, Site.Probe.DepthCm,
		Site.Probe.FaceDistanceCm, *LastMantleClip, Decision.Gait == EMantleGait::Running ? TEXT("running") : TEXT("standing"),
		Decision.bStopFirst ? TEXT(", stopped: no run-up") : TEXT(""), Decision.VerticalWarp, Decision.ForwardWarp, bTop ? TEXT("on top") : TEXT("over"),
		*Landing.ToCompactString());
	if (!MantleShiftHandle.IsValid())
	{
		// Also the wall guard's: it reads the torso when this frame's pose is final (the root motion of this frame applied).
		// The shift follows this frame's blend weight: updated when the pose is final (and in Tick, for a mesh whose pose is not refreshed).
		MantleShiftHandle = GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &AOperativeCharacter::UpdateMantleMeshShift));
	}
	if (ContactSeconds > 0.f || ExitSeconds < PlayLength || !MantleRootLock.IsNearlyZero(1.f))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: hands on the lip at %.2f s (feet then %s), hands over at %.2f s of %.2f, root lock %s shifted out"),
			*DisplayName.ToString(), ContactSeconds, *ContactFeet.ToCompactString(), ExitSeconds, PlayLength, *MantleRootLock.ToCompactString());
	}
	return true;
}

void AOperativeCharacter::UpdateMantleMeshShift()
{
	if (bMantling)
	{
		ApplyMantleWallGuard(); // batch 2 item 1 (Tick and the bone-finalized callback: the pose of this frame)
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || (MantleRootLock.IsZero() && MantleMeshShift.IsZero() && !MantleShiftHandle.IsValid()))
	{
		return;
	}
	float Weight = 0.f;
	const UAnimInstance* Anim = Body->GetAnimInstance();
	if (const UAnimMontage* Montage = MantleMontage.Get(); Anim && Montage)
	{
		if (const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montage))
		{
			Weight = Instance->GetWeight(); // blending in / out, a cancel's quick blend; gone = 0
		}
	}
	const FVector Shift = -MantleRootLock * Weight;
	if (!Shift.Equals(MantleMeshShift, 0.01f))
	{
		Body->SetRelativeLocation(Body->GetRelativeLocation() - MantleMeshShift + Shift);
		MantleMeshShift = Shift;
	}
	if (Weight <= 0.f && !bMantling)
	{
		MantleRootLock = FVector::ZeroVector;
		if (MantleShiftHandle.IsValid())
		{
			Body->UnregisterOnBoneTransformsFinalizedDelegate(MantleShiftHandle);
			MantleShiftHandle.Reset();
		}
	}
}

void AOperativeCharacter::ApplyMantleWallGuard()
{
	// Batch 2 item 1 (MantlingProblem_03 / 04, "sinks into the obstacle"): while a torso joint is below the top it is kept align.guard_gap_cm
	// in front of the face - the capsule (collision off for the climb) is pushed back by the deficit; the warp brings the climb onto the
	// landing from wherever he is (it re-targets every frame), once the torso is over the top.
	USkeletalMeshComponent* Body = GetMesh();
	// Top climbs only: over a fence the clip's torso passes under the fence's top by design of the clip (HighFenceVault, see the batch-2 report) -
	// a guard there only holds him back until its cap.
	if (!Body || !bMantleOnTop || OperativeMantleInternal::CVarMantleAlign.GetValueOnGameThread() == 0 || !MantleNavigation::IsBatch2Enabled())
	{
		return;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	const AActor* Obstacle = MantleObstacle.Get();
	float Deficit = 0.f;
	for (const FName& Bone : OperativeMantleInternal::MantleTorsoBones)
	{
		if (Body->GetBoneIndex(Bone) == INDEX_NONE)
		{
			continue;
		}
		const FVector Joint = Body->GetSocketLocation(Bone);
		const float Fwd = static_cast<float>(FVector::DotProduct(Joint - MantleFacePoint, MantleDir));
		// Over the top (or past the middle of a thin fence: crossed) a joint is no clipping.
		if (Joint.Z > MantleTopZ - Config.AlignGapAboveMarginCm || Fwd > MantleDepthCm * 0.5f || Fwd + Config.AlignGuardGapCm <= 0.f)
		{
			continue;
		}
		// The face was measured at knee height: a sloped / stepped obstacle (a car wreck's hood) lies farther in at the torso's height - only the
		// real collision close to the joint counts (no collision data: the face's plane).
		FVector Closest;
		const float Distance = Obstacle ? Obstacle->ActorGetDistanceToCollision(Joint, ECC_Pawn, Closest) : -1.f;
		if (Distance >= Config.AlignGuardGapCm)
		{
			continue;
		}
		Deficit = FMath::Max(Deficit, Fwd + Config.AlignGuardGapCm);
	}
	if (Deficit <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	// The pushes add up while the warp keeps re-targeting forward; past align.guard_max_push_cm the clip is let go (a body that far "in" is
	// an obstacle the face probe misread - a sloped wreck - and a capped guard never snaps him far at the landing).
	const float Allowed = FMath::Max(0.f, Config.AlignGuardMaxPushCm - MantleGuardPushCm);
	if (Allowed <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float Push = FMath::Min(Deficit, Allowed);
	AddActorWorldOffset(-MantleDir * Push);
	MantleGuardPushCm += Push;
	if (MantleGuardPushCm >= Config.AlignGuardMaxPushCm)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: the wall guard reached its %.0f cm cap on %s - the clip runs unguarded from here"),
			*DisplayName.ToString(), Config.AlignGuardMaxPushCm, *LastMantleClip);
	}
}

void AOperativeCharacter::UpdateMantle(float DeltaTime)
{
	MantleTimer += DeltaTime;
	UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	UAnimMontage* Montage = MantleMontage.Get();
	const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!Instance || Instance->IsStopped())
	{
		// Someone else stopped the clip (it never runs out by itself: no auto blend-out).
		CancelMantle(EMantleCancel::Interrupted);
		return;
	}
	if (Instance->GetPosition() >= FMath::Min(MantleExitSeconds, MantleDuration) - KINDA_SMALL_NUMBER)
	{
		FinishMantle();
		return;
	}
	ApplyMantleWallGuard();
	if (MantleTimer > MantleDuration + MantleRules::GetConfig().TimeoutPadSeconds)
	{
		CancelMantle(EMantleCancel::Interrupted);
	}
}

void AOperativeCharacter::FinishMantle()
{
	const FMantleConfig& Config = MantleRules::GetConfig();
	UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (UAnimMontage* Montage = MantleMontage.Get(); Anim && Montage)
	{
		if (FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage))
		{
			Instance->PushDisableRootMotion(); // nothing more from the clip: the landing below is the only correction
		}
		Anim->Montage_Stop(Config.BlendOutSeconds, Montage);
	}
	if (MotionWarping)
	{
		MotionWarping->RemoveWarpTarget(OperativeMantleInternal::MantleWarpTarget);
		MotionWarping->RemoveWarpTarget(OperativeMantleInternal::MantleContactTarget);
	}
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	LastMantleLandingError = static_cast<float>(FVector::Dist(Feet, MantleLandingFeet));
	if (LastMantleLandingError > Config.LandingSnapWarnCm)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: the warped clip ended %.0f cm off the planned landing - put on it"), *DisplayName.ToString(),
			LastMantleLandingError);
	}
	SetActorLocation(MantleLandingFeet + FVector(0.f, 0.f, HalfHeight + 1.f), false, nullptr, ETeleportType::TeleportPhysics);
	bMantling = false;
	MantleCooldown = static_cast<float>(GetWorld()->GetTimeSeconds()) + Config.CooldownSeconds;
	RestoreMantleState(false);
	if (UMantleSubsystem* Mantle = GetWorld() ? GetWorld()->GetSubsystem<UMantleSubsystem>() : nullptr)
	{
		Mantle->Release(this);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s landed %s (%.0f cm off the plan; the wall guard pushed %.0f cm)"), *DisplayName.ToString(),
		bMantleOnTop ? TEXT("on top") : TEXT("over"), LastMantleLandingError, MantleGuardPushCm);
	const FVector ToTarget = MantleResumeTarget - MantleLandingFeet;
	if (bMantleResume && !bMantleStopAfter && ToTarget.Size2D() > 60.f)
	{
		bMantleResume = false;
		ApplyMovementParams();
		RequestMove(MantleResumeTarget);
		bOwnMoveOrder = bMantleResumeOwnOrder && bHasMoveOrder;
	}
	else
	{
		bMantleResume = false;
		bHasMoveOrder = false;
		if (bMantleOnTop)
		{
			SetStance(EOperativeStance::Crouching); // designer Q3: on top he crouches
		}
	}
}

void AOperativeCharacter::RestoreMantleState(bool bFalling)
{
	if (!bMantleSnapshotValid)
	{
		return;
	}
	bMantleSnapshotValid = false;
	++MantleRestoreCount;
	SetActorEnableCollision(true);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = bMantleSavedOrientToMovement;
		Movement->bUseControllerDesiredRotation = bMantleSavedControllerRotation;
		Movement->Velocity = FVector::ZeroVector;
		const EMovementMode Saved = static_cast<EMovementMode>(MantleSavedMovementMode);
		Movement->SetMovementMode(bFalling ? MOVE_Falling : (Saved == MOVE_NavWalking ? MOVE_NavWalking : MOVE_Walking));
	}
	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(bMantleSavedWeaponVisible || !UsesPlaceholderBody(), true);
	}
}

void AOperativeCharacter::CancelMantle(EMantleCancel Reason)
{
	if (!bMantling)
	{
		return;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	LastMantleCancel = Reason;
	bMantling = false;
	MantleCooldown = static_cast<float>(GetWorld()->GetTimeSeconds()) + Config.CooldownSeconds;
	bMantleResume = false;
	UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (UAnimMontage* Montage = MantleMontage.Get(); Anim && Montage)
	{
		if (FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage))
		{
			Instance->PushDisableRootMotion();
		}
		Anim->Montage_Stop(0.15f, Montage);
	}
	if (MotionWarping)
	{
		MotionWarping->RemoveWarpTarget(OperativeMantleInternal::MantleWarpTarget);
		MotionWarping->RemoveWarpTarget(OperativeMantleInternal::MantleContactTarget);
	}
	if (UMantleSubsystem* Mantle = GetWorld() ? GetWorld()->GetSubsystem<UMantleSubsystem>() : nullptr)
	{
		Mantle->Release(this);
	}
	const bool bToBase = Reason == EMantleCancel::Knockdown || Reason == EMantleCancel::HeavyHit || Reason == EMantleCancel::Interrupted;
	if (bToBase)
	{
		// Designer Q5: he drops back to the base he climbed from (a free spot: he stood there), then the knockdown / hit plays there.
		SetActorLocation(MantleBaseFeet + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr,
			ETeleportType::TeleportPhysics);
		bHasMoveOrder = false;
	}
	RestoreMantleState(Reason != EMantleCancel::EndPlay);
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: climb cancelled (%d)%s"), *DisplayName.ToString(), static_cast<int32>(Reason),
		bToBase ? TEXT(" - dropped back to the base") : TEXT(""));
}

bool AOperativeCharacter::PlanMantleClick(const FHitResult& Hit, FMantleClickPlan& Out, FString* OutReason) const
{
	auto Fail = [OutReason](const TCHAR* Why)
	{
		if (OutReason)
		{
			*OutReason = Why;
		}
		return false;
	};
	UWorld* World = GetWorld();
	if (!World || !HealthComponent || !HealthComponent->IsAlive() || bMantling || bVaulting || IsMantleDescending() || IsKnockedDown() || IsClimbingLadder()
		|| IsDroppingOffLedge() || bCarrying || Stance == EOperativeStance::Prone)
	{
		return Fail(TEXT("busy"));
	}
	const bool bBatch2 = MantleNavigation::IsBatch2Enabled();
	// Batch 2 item 3: a click on the obstacle's side face (what the isometric camera shows) means its top just inside that face.
	FHitResult TopHit = Hit;
	if (Hit.ImpactNormal.Z < 0.7f && !(bBatch2 && MantleNavigation::TopHitFromFace(*World, Hit, TopHit)))
	{
		return Fail(TEXT("not a climbable top"));
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	if (!MantleNavigation::PlanTopClick(*World, TopHit, Feet, GetCapsuleComponent()->GetScaledCapsuleRadius(), Out.Base, Out.Dir))
	{
		return Fail(TEXT("not a climbable top"));
	}
	// A top on his level's reach only (a wall / a roof without a mark is no climb: the click stays the old order).
	EMantleRefusal ClassRefusal = EMantleRefusal::None;
	const float Height = static_cast<float>(TopHit.ImpactPoint.Z - Feet.Z);
	const int32 Class = MantleRules::ClassifyHeight(Config, Height, EMantleDestination::Top, ClassRefusal);
	if (Class == INDEX_NONE || !Config.Classes[Class].bEnabled || (Config.Classes[Class].bMarkedEdgeOnly && !TopHit.GetActor()->Tags.Contains(MantleRules::MarkedEdgeTag))
		|| FVector::Dist2D(Feet, Out.Base) > Config.OrderMaxDistanceCm)
	{
		return Fail(MantleRules::RefusalName(Class == INDEX_NONE ? ClassRefusal : EMantleRefusal::NotMarked));
	}
	Out.TopHit = TopHit;
	Out.Obstacle = TopHit.GetActor();
	Out.HeightCm = Height;
	Out.Landing = TopHit.ImpactPoint;
	if (!bBatch2)
	{
		return true; // before batch 2 the face's rules were checked on arrival only
	}
	// Batch 2 (the hover ghost shows exactly what the click does): the climb measured from the base he walks to - the landing, the headroom,
	// the warp limits, a reserved landing; no ghost and no order when it would be refused on arrival.
	const AOperativeCharacter* Default = GetClass()->GetDefaultObject<AOperativeCharacter>();
	const float StandingHalf = Default && Default->GetCapsuleComponent() ? Default->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : HalfHeight;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeMantlePlan), false, this);
	FMantleSite Site;
	if (!MantleNavigation::ProbeSite(*World, Out.Base, Out.Dir, OperativeMantleInternal::MantleStandingReachCm, GetCapsuleComponent()->GetScaledCapsuleRadius(),
		StandingHalf, Params, Config, Site) || !Site.bValid || Site.Obstacle.Get() != TopHit.GetActor())
	{
		return Fail(TEXT("no face to climb from the base"));
	}
	Site.Probe.Body = GetMantleBody();
	Site.Probe.Rotation = MantleRotation;
	if (const UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>(); Mantle && Mantle->IsReservedByOther(Site.Landing, this))
	{
		Site.Probe.bLandingFree = false;
	}
	UMantleSubsystem* Mantle = World->GetSubsystem<UMantleSubsystem>();
	const FMantleDecision Decision = MantleRules::Decide(Config, Site.Probe, [Mantle](int32 Index) { return Mantle && Mantle->GetClip(Index) != nullptr; });
	if (!Decision.IsGo())
	{
		return Fail(MantleRules::RefusalName(Decision.Refusal));
	}
	Out.Landing = Site.Landing;
	Out.bTop = Site.Destination == EMantleDestination::Top;
	return true;
}

bool AOperativeCharacter::OrderMantleTop(const FHitResult& TopHit, bool bSprint, FString* OutReason)
{
	FMantleClickPlan Plan;
	if (!PlanMantleClick(TopHit, Plan, OutReason))
	{
		return false;
	}
	if (FVector::Dist2D(GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), Plan.Base)
		<= OperativeMantleInternal::MantleOrderArriveCm + 40.f && TryStartMantle(Plan.Dir, nullptr, true))
	{
		return true; // already at the face
	}
	if (OrderMoveTo(Plan.Base, bSprint) != EOperativeOrderResult::Accepted)
	{
		if (OutReason)
		{
			*OutReason = TEXT("unreachable");
		}
		return false;
	}
	bMantleOrderPending = true;
	MantleOrderBase = Plan.Base;
	MantleOrderDir = Plan.Dir;
	MantleOrderSeconds = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: climb order onto %s (%.0f cm) - walking to its face"), *DisplayName.ToString(),
		*GetNameSafe(Plan.Obstacle), Plan.HeightCm);
	return true;
}

bool AOperativeCharacter::NotifyMantleLinkReached(int32 LinkIndex)
{
	if (bMantling || bVaulting)
	{
		return true; // already on it
	}
	const AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	const UPathFollowingComponent* Following = AIController ? AIController->GetPathFollowingComponent() : nullptr;
	bMantleResume = true;
	MantleResumeTarget = IsOwnMoveSteered() || !(Following && Following->GetPath().IsValid()) ? LastMoveDestination : Following->GetPath()->GetDestinationLocation();
	bMantleResumeOwnOrder = HasOwnMoveOrder();
	PendingMantleLink = LinkIndex; // the smart link calls back from inside the crowd update: taken on the next tick
	return true;
}

void AOperativeCharacter::HandleMantleLink(int32 LinkIndex)
{
	UWorld* World = GetWorld();
	UMantleSubsystem* Mantle = World ? World->GetSubsystem<UMantleSubsystem>() : nullptr;
	const FMantleLink* Link = Mantle ? Mantle->GetLink(LinkIndex) : nullptr;
	if (!Link)
	{
		bMantleResume = false;
		return;
	}
	if (Link->bDown)
	{
		// Batch 2 (MantlingProblem_05): the walk off the edge and the fall - no vault-over arc through the top's corner.
		if (MantleNavigation::IsBatch2Enabled() ? StartMantleDescent(Link->End, MantleResumeTarget, bMantleResume, bMantleResumeOwnOrder)
			: StartMantleHop(Link->End))
		{
			bMantleResume = false;
			return;
		}
	}
	else
	{
		FMantleSite Site;
		if (ProbeMantleAhead(Link->Dir, OperativeMantleInternal::MantleStandingReachCm, Site) && Site.Obstacle.Get() == Link->Obstacle.Get())
		{
			const FMantleDecision Decision = MantleRules::Decide(MantleRules::GetConfig(), Site.Probe,
				[Mantle](int32 Index) { return Mantle->GetClip(Index) != nullptr; });
			if (Decision.IsGo() && StartMantle(Site, Decision, false, LinkIndex))
			{
				return;
			}
			LastMantleRefusal = Decision.IsGo() ? LastMantleRefusal : Decision.Refusal;
		}
	}
	// It cannot be taken now (occupied / changed): the link off, the order paths again without it.
	Mantle->DisableLink(LinkIndex, MantleRules::RefusalName(LastMantleRefusal));
	const bool bResume = bMantleResume;
	bMantleResume = false;
	if (bResume)
	{
		RequestMove(MantleResumeTarget);
		bOwnMoveOrder = bMantleResumeOwnOrder && bHasMoveOrder;
	}
}

bool AOperativeCharacter::StartMantleHop(const FVector& LandingFeet)
{
	// The vault's own short arc (UpdateObstacleStepOff's step down): collision off, flown, the walk resumes at the landing.
	bVaultResume = bMantleResume;
	VaultResumeTarget = MantleResumeTarget;
	bMantleResume = false;
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	SetActorEnableCollision(false);
	if (bInCover)
	{
		LeaveCover(TEXT("mantle hop"));
	}
	bHasPendingCover = false;
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	bVaulting = true;
	VaultTimer = 0.f;
	VaultDuration = OperativeMantleInternal::MantleHopSeconds;
	VaultHeight = 30.f;
	VaultStart = GetActorLocation();
	VaultLanding = LandingFeet + FVector(0.f, 0.f, HalfHeight);
	const FVector Out = (VaultLanding - VaultStart).GetSafeNormal2D();
	if (!Out.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Out.Rotation().Yaw, 0.f));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s hops down off the top to %s"), *DisplayName.ToString(), *LandingFeet.ToCompactString());
	return true;
}

const AActor* AOperativeCharacter::GetMantleTopUnderFeet() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const AActor* Floor = Movement && Movement->IsMovingOnGround() ? Movement->CurrentFloor.HitResult.GetActor() : nullptr;
	bool bMarked = false;
	return Floor && !Cast<APawn>(Floor) && !VaultNavigation::IsVaultable(Floor) && MantleRules::IsEligibleTags(Floor->Tags, bMarked) ? Floor : nullptr;
}

bool AOperativeCharacter::FindMantleDescent(const AActor& Top, const FVector& Goal, FVector& OutLanding, float* OutTotalCm) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const FBox Local = Top.CalculateComponentsBoundingBoxInLocalSpace(false);
	if (!Nav || !Capsule || !Local.IsValid)
	{
		return false;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	const AOperativeCharacter* Default = GetClass()->GetDefaultObject<AOperativeCharacter>();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float StandingHalf = Default && Default->GetCapsuleComponent() ? Default->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: Capsule->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());
	const FTransform Transform = Top.GetActorTransform();
	const FVector Centre = Local.GetCenter();
	const FVector Extent = Local.GetExtent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeMantleDescent), false, this);
	TArray<FMantleDescentOption> Options;
	bool bMarkedGround = false; // the landing's ground must be the ground, not the next crate's top
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		for (const float Sign : { 1.f, -1.f })
		{
			const int32 Tangent = 1 - Axis;
			FVector ALocal = Centre;
			ALocal[Axis] += Sign * Extent[Axis];
			FVector BLocal = ALocal;
			ALocal[Tangent] -= Extent[Tangent];
			BLocal[Tangent] += Extent[Tangent];
			FVector NormalLocal = FVector::ZeroVector;
			NormalLocal[Axis] = Sign;
			const FVector Normal = Transform.TransformVectorNoScale(NormalLocal).GetSafeNormal2D();
			FVector A = Transform.TransformPosition(ALocal);
			FVector B = Transform.TransformPosition(BLocal);
			A.Z = Feet.Z;
			B.Z = Feet.Z;
			FMantleDescentOption& Option = Options.AddDefaulted_GetRef();
			Option.Exit = MantleRules::DescentExitOnFace(A, B, Goal, Radius + 5.f);
			Option.Landing = Option.Exit + Normal * (Radius + Config.DescentLandOutCm);
			Option.WalkCm = static_cast<float>(FVector::Dist2D(Feet, Option.Exit));
			// The ground past the face: a drop of descent.min_drop_cm .. max_drop_cm onto the navmesh, room for a standing capsule.
			FHitResult Ground;
			const FVector Over(Option.Landing.X, Option.Landing.Y, Feet.Z + 50.f);
			if (!World->LineTraceSingleByChannel(Ground, Over, Over - FVector(0.f, 0.f, Config.DescentMaxDropCm + 100.f), ECC_Visibility, Params)
				|| Ground.bStartPenetrating || Ground.GetActor() == &Top || Cast<APawn>(Ground.GetActor())
				|| (Ground.GetActor() && MantleRules::IsEligibleTags(Ground.GetActor()->Tags, bMarkedGround)))
			{
				continue;
			}
			const float Drop = static_cast<float>(Feet.Z - Ground.ImpactPoint.Z);
			FNavLocation OnNav;
			const float Reach = Radius + Config.DescentLandOutCm;
			if (Drop < Config.DescentMinDropCm || Drop > Config.DescentMaxDropCm
				|| !Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(Reach, Reach, 60.f))
				|| FMath::Abs(OnNav.Location.Z - Ground.ImpactPoint.Z) > 40.f
				|| World->OverlapAnyTestByChannel(Ground.ImpactPoint + FVector(0.f, 0.f, StandingHalf + 4.f), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(Radius, StandingHalf), Params))
			{
				continue;
			}
			Option.Landing.Z = Ground.ImpactPoint.Z;
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, OnNav.Location, Goal, const_cast<AOperativeCharacter*>(this),
				ChooseMoveFilter());
			if (Path && Path->IsValid())
			{
				Option.bValid = true;
				Option.bPathComplete = !Path->IsPartial();
				Option.PathCm = static_cast<float>(Path->GetPathLength());
			}
		}
	}
	const int32 Best = MantleRules::ChooseDescent(Options);
	if (Best == INDEX_NONE)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: no way down off %s towards %s (no face with free navmesh ground %.0f..%.0f cm below)"),
			*DisplayName.ToString(), *GetNameSafe(&Top), *Goal.ToCompactString(), Config.DescentMinDropCm, Config.DescentMaxDropCm);
		return false;
	}
	OutLanding = Options[Best].Landing;
	if (OutTotalCm)
	{
		*OutTotalCm = Options[Best].WalkCm + Options[Best].PathCm;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: the way down off %s over face %d of %d (walk %.0f + path %.0f cm%s) to %s"), *DisplayName.ToString(),
		*GetNameSafe(&Top), Best, Options.Num(), Options[Best].WalkCm, Options[Best].PathCm, Options[Best].bPathComplete ? TEXT("") : TEXT(", partial"),
		*OutLanding.ToCompactString());
	return true;
}

bool AOperativeCharacter::TryMantleStepDown(const FVector& Destination)
{
	if (!MantleNavigation::IsBatch2Enabled())
	{
		return TryMantleHopDownLegacy(Destination);
	}
	const AActor* Top = GetMantleTopUnderFeet();
	if (!Top || bMantling || bVaulting || IsMantleDescending())
	{
		return false;
	}
	FVector Landing;
	if (!FindMantleDescent(*Top, Destination, Landing))
	{
		return false;
	}
	return StartMantleDescent(Landing, Destination, true, !bAutonomousOrder);
}

bool AOperativeCharacter::TryMantleHopDownLegacy(const FVector& Destination)
{
	// Before batch 2 (Codex.Mantle.Batch2 0): the vault's arc to the first free spot round him, starting in the goal's direction.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const AActor* Floor = Movement && Movement->IsMovingOnGround() ? Movement->CurrentFloor.HitResult.GetActor() : nullptr;
	bool bMarked = false;
	if (!Floor || bMantling || bVaulting || !MantleRules::IsEligibleTags(Floor->Tags, bMarked))
	{
		return false;
	}
	const FVector ToGoal = (Destination - GetActorLocation()).GetSafeNormal2D();
	const FRotator Saved = GetActorRotation();
	if (!ToGoal.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, ToGoal.Rotation().Yaw, 0.f));
	}
	FVector Spot;
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (!VaultNavigation::FindStepOffSpot(*this, Spot) || GetActorLocation().Z - Spot.Z > MantleRules::GetConfig().LinkDownBelowCm + 20.f)
	{
		SetActorRotation(Saved);
		return false;
	}
	bMantleResume = true;
	MantleResumeTarget = Destination;
	bMantleResumeOwnOrder = HasOwnMoveOrder();
	LastMoveDestination = Destination;
	bHasMoveOrder = true;
	return StartMantleHop(Spot - FVector(0.f, 0.f, HalfHeight));
}

bool AOperativeCharacter::StartMantleDescent(const FVector& LandingFeet, const FVector& Goal, bool bResume, bool bResumeOwnOrder)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || bMantling || bVaulting || IsMantleDescending() || IsKnockedDown() || IsClimbingLadder() || IsDroppingOffLedge()
		|| !HealthComponent || !HealthComponent->IsAlive())
	{
		return false;
	}
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	Movement->StopMovementImmediately();
	if (bInCover)
	{
		LeaveCover(TEXT("mantle descent"));
	}
	bHasPendingCover = false;
	bMantleOrderPending = false;
	MantleDescentLanding = LandingFeet;
	MantleDescentGoal = Goal;
	bMantleDescentResume = bResume;
	bMantleDescentResumeOwn = bResumeOwnOrder;
	MantleDescentTopZ = static_cast<float>(GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	MantleDescentTimer = 0.f;
	LastMoveDestination = Goal;
	bHasMoveOrder = true;
	++MantleDescentCount;
	// Item 8 (MantlingProblem_07): prone / crouched he gets up first (the stance clip plays), then walks off.
	const bool bStandUp = Stance != EOperativeStance::Standing;
	if (bStandUp)
	{
		SetStance(EOperativeStance::Standing);
	}
	MantleDescentPhase = bStandUp ? EMantleDescentPhase::StandUp : EMantleDescentPhase::Walk;
	bMantleDescentSavedOrient = Movement->bOrientRotationToMovement;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s comes down off the top: %swalks off the edge towards %s, then on to %s"), *DisplayName.ToString(),
		bStandUp ? TEXT("stands up, ") : TEXT(""), *LandingFeet.ToCompactString(), *Goal.ToCompactString());
	return true;
}

void AOperativeCharacter::UpdateMantleDescent(float DeltaTime)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || IsKnockedDown() || IsClimbingLadder() || !HealthComponent || !HealthComponent->IsAlive())
	{
		EndMantleDescent(false);
		return;
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	MantleDescentTimer += DeltaTime;
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	switch (MantleDescentPhase)
	{
	case EMantleDescentPhase::StandUp:
		// The stance clip roots him (GetMoveHoldSeconds); at most descent.stand_up_max_s.
		if ((MantleDescentTimer > DeltaTime && GetMoveHoldSeconds() <= 0.f) || MantleDescentTimer >= Config.DescentStandUpMaxSeconds)
		{
			MantleDescentPhase = EMantleDescentPhase::Walk;
			MantleDescentTimer = 0.f;
		}
		return;
	case EMantleDescentPhase::Walk:
	{
		if (Movement->IsFalling())
		{
			MantleDescentPhase = EMantleDescentPhase::Air; // walked off the edge: the fall (the ABP's fall states)
			return;
		}
		if (Feet.Z < MantleDescentTopZ - Config.DescentMinDropCm)
		{
			EndMantleDescent(true); // stepped down without a fall (a low drop)
			return;
		}
		if (MantleDescentTimer > Config.DescentWalkTimeoutSeconds)
		{
			// Something holds him on the top (a man in the way): the old hop takes him down - logged, never a silent soft-lock.
			UE_LOG(LogCodexTactics, Warning, TEXT("[Mantle] %s: still on the top %.1f s after the walk to its edge started - the hop down instead"),
				*DisplayName.ToString(), MantleDescentTimer);
			const FVector Landing = MantleDescentLanding;
			bMantleResume = bMantleDescentResume;
			MantleResumeTarget = MantleDescentGoal;
			bMantleResumeOwnOrder = bMantleDescentResumeOwn;
			MantleDescentPhase = EMantleDescentPhase::None;
			Movement->bOrientRotationToMovement = bMantleDescentSavedOrient;
			++MantleDescentHopFallbacks;
			StartMantleHop(Landing);
			return;
		}
		const FVector Dir = (MantleDescentLanding - Feet).GetSafeNormal2D();
		Movement->bOrientRotationToMovement = true; // he faces where he walks off (restored at the end)
		ApplyMovementParams();
		AddMovementInput(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, 1.f);
		return;
	}
	case EMantleDescentPhase::Air:
		if (Movement->IsMovingOnGround())
		{
			EndMantleDescent(true);
		}
		return;
	default:
		return;
	}
}

void AOperativeCharacter::EndMantleDescent(bool bResume)
{
	if (MantleDescentPhase == EMantleDescentPhase::None)
	{
		return;
	}
	MantleDescentPhase = EMantleDescentPhase::None;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = bMantleDescentSavedOrient;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s is down off the top at %s (%.0f cm off the aimed landing)"), *DisplayName.ToString(), *Feet.ToCompactString(),
		FVector::Dist2D(Feet, MantleDescentLanding));
	if (bResume && bMantleDescentResume && FVector::Dist2D(Feet, MantleDescentGoal) > 60.f)
	{
		RequestMove(MantleDescentGoal);
		bOwnMoveOrder = bMantleDescentResumeOwn && bHasMoveOrder;
	}
	else
	{
		bHasMoveOrder = false;
	}
}

void AOperativeCharacter::UpdateMantleTrigger(float DeltaTime)
{
	using namespace OperativeMantleInternal;
	const bool bCoolingDown = GetWorld()->GetTimeSeconds() < MantleCooldown;
	if (PendingMantleLink != INDEX_NONE)
	{
		const int32 Link = PendingMantleLink;
		PendingMantleLink = INDEX_NONE;
		HandleMantleLink(Link);
		return;
	}
	const AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	const UPathFollowingComponent* Following = AIController ? AIController->GetPathFollowingComponent() : nullptr;
	const bool bMoving = Following && Following->GetStatus() == EPathFollowingStatus::Moving;
	const FMantleConfig& Config = MantleRules::GetConfig();
	const bool bRunning = IsRunningForMantle();
	if (bMantleOrderPending)
	{
		MantleOrderSeconds += DeltaTime;
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const float Left = static_cast<float>(FVector::Dist2D(Feet, MantleOrderBase));
		// Running up to it (an order he runs): the run clip as soon as the face lies in its window (designer Q2 = A).
		if (bRunning && bMoving && Left < MantleRunReachCm && !bCoolingDown)
		{
			FMantleSite Site;
			UMantleSubsystem* Mantle = GetWorld()->GetSubsystem<UMantleSubsystem>();
			if (ProbeMantleAhead(MantleOrderDir, MantleRunReachCm, Site) && Mantle)
			{
				const FMantleDecision Decision = MantleRules::Decide(Config, Site.Probe, [Mantle](int32 Index) { return Mantle->GetClip(Index) != nullptr; });
				if (Decision.IsGo() && Decision.Gait == EMantleGait::Running && Decision.ForwardWarp <= MantleRunStartWarp && StartMantle(Site, Decision, true, INDEX_NONE))
				{
					return;
				}
			}
		}
		const bool bArrived = Left <= MantleOrderArriveCm || (!bMoving && MantleOrderSeconds > 0.2f);
		if (bArrived)
		{
			bMantleOrderPending = false;
			EMantleRefusal Refusal = EMantleRefusal::None;
			if (!TryStartMantle(MantleOrderDir, &Refusal, true))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: the ordered climb is refused at the face: %s"), *DisplayName.ToString(), MantleRules::RefusalName(Refusal));
			}
		}
		else if (MantleOrderSeconds > MantleOrderTimeoutSeconds || !bHasMoveOrder)
		{
			bMantleOrderPending = false;
		}
		return;
	}
	// A path over an obstacle, running: the run clip when the face lies in its window (else the link at the face plays the standing one).
	if (!bMantlePathAllowed || !bRunning || !bMoving || bCoolingDown || !Following->GetPath().IsValid())
	{
		return;
	}
	UMantleSubsystem* Mantle = GetWorld()->GetSubsystem<UMantleSubsystem>();
	if (!Mantle || Mantle->GetLinks().IsEmpty())
	{
		return;
	}
	TArray<int32> OnPath;
	Mantle->FindLinksOnPath(*Following->GetPath(), OnPath);
	for (const int32 Index : OnPath)
	{
		const FMantleLink* Link = Mantle->GetLink(Index);
		if (!Link || Link->bDown || FVector::Dist2D(GetActorLocation(), Link->Start) > MantleRunReachCm)
		{
			continue;
		}
		FMantleSite Site;
		if (ProbeMantleAhead(Link->Dir, MantleRunReachCm, Site) && Site.Obstacle.Get() == Link->Obstacle.Get())
		{
			const FMantleDecision Decision = MantleRules::Decide(Config, Site.Probe, [Mantle](int32 Clip) { return Mantle->GetClip(Clip) != nullptr; });
			if (Decision.IsGo() && Decision.Gait == EMantleGait::Running && Decision.ForwardWarp <= MantleRunStartWarp)
			{
				StartMantle(Site, Decision, false, Index);
			}
		}
		break; // only the next link ahead
	}
}
