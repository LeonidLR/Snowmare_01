// AOperativeCharacter: mantling, slice 1 (docs/design/MANTLING.md section 11, designer answers 11.6; docs/port/MANTLING_SLICE1_REPORT.md).
// One operative, Solo: approach -> reserve -> climb (the clip's root motion in MOVE_Flying, warped onto the real ledge top / far-side landing by
// the engine Motion Warping plugin - no manual translation on top of it) -> land -> crouch on a top / walk on. Patterns reused: the vault
// (OperativeCharacterVault.cpp: stop, resume target, the arc for the hop down), the ladder (LadderClimbComponent.cpp: the saved movement /
// rotation / weapon snapshot restored exactly once, path resume), the drop links (the link callback taken on the next tick).
// Pure rules: Characters/MantleRules. Links / probe / reservations: Interactables/MantleNavigation. UE-only (Godot had no mantling).

#include "Characters/OperativeCharacter.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
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
#include "Interactables/MantleNavigation.h"
#include "Interactables/VaultNavigation.h"
#include "MotionWarpingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "RootMotionModifier_SkewWarp.h"
#include "Survival/WarmIdleRules.h"

namespace OperativeMantleInternal
{
	/** The Motion Warping target of every mantle clip (the landing: feet, facing the climb). */
	const FName MantleWarpTarget(TEXT("MantleLanding"));
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
	const FMantleDecision Decision = MantleRules::Decide(MantleRules::GetConfig(), Site.Probe,
		[Mantle](int32 Index) { return Mantle && Mantle->GetClip(Index) != nullptr; });
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
	URootMotionModifier_SkewWarp::AddRootMotionModifierSkewWarp(MotionWarping, Montage, 0.f, Montage->GetPlayLength(), MantleWarpTarget,
		EWarpPointAnimProvider::None, FTransform::Identity, NAME_None, /*bWarpTranslation*/ true, /*bIgnoreZAxis*/ false, /*bWarpRotation*/ true);
	bMantling = true;
	MantleTimer = 0.f;
	MantleDuration = Montage->GetPlayLength();
	MantleMontage = Montage;
	MantleClipIndex = Decision.ClipIndex;
	MantleLinkIndex = LinkIndex;
	MantleBaseFeet = Site.Feet;
	MantleLandingFeet = Landing;
	MantleDir = Site.Dir;
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
	return true;
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
	if (Instance->GetPosition() >= MantleDuration - KINDA_SMALL_NUMBER)
	{
		FinishMantle();
		return;
	}
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
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s landed %s (%.0f cm off the plan)"), *DisplayName.ToString(), bMantleOnTop ? TEXT("on top") : TEXT("over"),
		LastMantleLandingError);
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

bool AOperativeCharacter::OrderMantleTop(const FHitResult& TopHit, bool bSprint, FString* OutReason)
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
	if (!World || !HealthComponent || !HealthComponent->IsAlive() || bMantling || bVaulting || IsKnockedDown() || IsClimbingLadder() || IsDroppingOffLedge()
		|| bCarrying || Stance == EOperativeStance::Prone)
	{
		return Fail(TEXT("busy"));
	}
	const FMantleConfig& Config = MantleRules::GetConfig();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	FVector Base;
	FVector Dir;
	if (!MantleNavigation::PlanTopClick(*World, TopHit, Feet, GetCapsuleComponent()->GetScaledCapsuleRadius(), Base, Dir))
	{
		return Fail(TEXT("not a climbable top"));
	}
	// A top on his level's reach only (a wall / a roof without a mark is no climb: the click stays the old order).
	EMantleRefusal ClassRefusal = EMantleRefusal::None;
	const float Height = static_cast<float>(TopHit.ImpactPoint.Z - Feet.Z);
	const int32 Class = MantleRules::ClassifyHeight(Config, Height, EMantleDestination::Top, ClassRefusal);
	if (Class == INDEX_NONE || !Config.Classes[Class].bEnabled || (Config.Classes[Class].bMarkedEdgeOnly && !TopHit.GetActor()->Tags.Contains(MantleRules::MarkedEdgeTag))
		|| FVector::Dist2D(Feet, Base) > Config.OrderMaxDistanceCm)
	{
		return Fail(MantleRules::RefusalName(Class == INDEX_NONE ? ClassRefusal : EMantleRefusal::NotMarked));
	}
	if (FVector::Dist2D(Feet, Base) <= OperativeMantleInternal::MantleOrderArriveCm + 40.f && TryStartMantle(Dir, nullptr, true))
	{
		return true; // already at the face
	}
	if (OrderMoveTo(Base, bSprint) != EOperativeOrderResult::Accepted)
	{
		return Fail(TEXT("unreachable"));
	}
	bMantleOrderPending = true;
	MantleOrderBase = Base;
	MantleOrderDir = Dir;
	MantleOrderSeconds = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Mantle] %s: climb order onto %s (%.0f cm) - walking to its face"), *DisplayName.ToString(),
		*GetNameSafe(TopHit.GetActor()), Height);
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
		if (StartMantleHop(Link->End))
		{
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

bool AOperativeCharacter::TryMantleStepDown(const FVector& Destination)
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const AActor* Floor = Movement && Movement->IsMovingOnGround() ? Movement->CurrentFloor.HitResult.GetActor() : nullptr;
	bool bMarked = false;
	if (!Floor || bMantling || bVaulting || !MantleRules::IsEligibleTags(Floor->Tags, bMarked))
	{
		return false;
	}
	// Facing the destination: the step-off search starts in his facing (VaultNavigation::FindStepOffSpot).
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
