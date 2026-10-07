#include "Characters/OperativeAnimInstance.h"
#include "Characters/LeftHandIKRules.h"
#include "Characters/AimOffsetRules.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugLeftHandIK(TEXT("Codex.Debug.LeftHandIK"), 0,
		TEXT("1: draw the left-hand IK spheres (red socket, green target, yellow hand_l, blue shoulder) in PIE / game."));
}
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "CodexTactics.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/ColdAnimationRules.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace
{
	/** Playback rate range for speed-matched clips. */
	constexpr float MinPlayRate = 0.4f;
	constexpr float MaxPlayRate = 2.2f;
	/** Clips below this weight are not sampled. */
	constexpr float MinSampleWeight = 0.005f;

	/** Dev trace of what the AnimBP gets every frame (CodexTactics.AnimTrace 1). */
	TAutoConsoleVariable<int32> CVarAnimTrace(TEXT("CodexTactics.AnimTrace"), 0,
		TEXT("1 = log the operative / enemy anim inputs every frame (speed, direction, blend axes, play rates)."));

	int32 Slot(EOperativeClip Clip)
	{
		return static_cast<int32>(Clip);
	}
}

FAnimInstanceProxy* UOperativeAnimInstance::CreateAnimInstanceProxy()
{
	return new FOperativeAnimInstanceProxy(this);
}

void UOperativeAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner()))
	{
		BoundOperative = Operative;
		FiredHandle = Operative->OnWeaponFiredNative.AddUObject(this, &UOperativeAnimInstance::HandleWeaponFired);
		GrenadeHandle = Operative->OnGrenadeThrowNative.AddUObject(this, &UOperativeAnimInstance::HandleGrenadeThrow);
		if (UHealthComponent* Health = Operative->HealthComponent)
		{
			Health->OnHealthChanged.AddUniqueDynamic(this, &UOperativeAnimInstance::HandleHealthChanged);
			Health->OnDied.AddUniqueDynamic(this, &UOperativeAnimInstance::HandleDied);
		}
	}
	// Corner-ready pose (user rule 2026-10-06): until setup_operative_cover_animation.py assigns it, take the M4 pack's
	// look-at idles when the pack is in the project and the other cover clips come from it.
	if (bUseNativeCoverClips && (CoverStandIdle.ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; })))
	{
		auto Fill = [](TArray<TObjectPtr<UAnimSequenceBase>>& Clips, const TCHAR* Left, const TCHAR* Right)
		{
			Clips.SetNum(2);
			const TCHAR* Paths[2] = { Left, Right };
			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (!Clips[Index])
				{
					Clips[Index] = LoadObject<UAnimSequenceBase>(nullptr, Paths[Index], nullptr, LOAD_NoWarn | LOAD_Quiet);
				}
			}
		};
		Fill(CoverStandCorner, TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_look_at_idle_L.anim_M4_cvr_std_look_at_idle_L"),
			TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_look_at_idle_R.anim_M4_cvr_std_look_at_idle_R"));
		Fill(CoverCrouchCorner, TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_cvr_crch_look_at_idle_L.anim_M4_cvr_crch_look_at_idle_L"),
			TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_cvr_crch_look_at_idle_R.anim_M4_cvr_crch_look_at_idle_R"));
		// Fire-ready corner stance + its transitions (user rule 2026-10-06, see UpdateCoverLayer).
		auto Stand = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_%s.anim_M4_%s"), Name, Name); };
		auto Crouch = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_%s.anim_M4_%s"), Name, Name); };
		Fill(CoverStandFireIdle, *Stand(TEXT("cvr_std_fire_idle_L")), *Stand(TEXT("cvr_std_fire_idle_R")));
		Fill(CoverCrouchFireIdle, *Crouch(TEXT("cvr_crch_fire_idle_L")), *Crouch(TEXT("cvr_crch_fire_idle_R")));
		Fill(CoverStandFireEnter, *Stand(TEXT("cvr_std_idle_L_to_fire")), *Stand(TEXT("cvr_std_idle_R_to_fire")));
		Fill(CoverCrouchFireEnter, *Crouch(TEXT("cvr_crch_idle_to_fire_L")), *Crouch(TEXT("cvr_crch_idle_to_fire_R")));
		Fill(CoverStandFireExit, *Stand(TEXT("cvr_std_fire_to_std_idle_L")), *Stand(TEXT("cvr_std_fire_to_std_idle_R")));
		Fill(CoverCrouchFireExit, *Crouch(TEXT("cvr_crch_fire_to_idle_L")), *Crouch(TEXT("cvr_crch_fire_to_idle_R")));
		// Crouched set + stance switches at the wall (user request 2026-10-07; [_L, _R], pack _L = his own right).
		Fill(CoverCrouchIdle, *Crouch(TEXT("cvr_crch_idle_L")), *Crouch(TEXT("cvr_crch_idle_R")));
		Fill(CoverCrouchFire, *Crouch(TEXT("cvr_crch_fire_L")), *Crouch(TEXT("cvr_crch_fire_R")));
		Fill(CoverCrouchMoveForward, *Crouch(TEXT("cvr_crch_walk_fwd_loop_L")), *Crouch(TEXT("cvr_crch_walk_fwd_loop_R")));
		Fill(CoverCrouchMoveBackward, *Crouch(TEXT("cvr_crch_walk_bwd_loop_L")), *Crouch(TEXT("cvr_crch_walk_bwd_loop_R")));
		Fill(CoverCrouchEnter, *Crouch(TEXT("crch_idle_fwd_to_cvr_crch_idle_L")), *Crouch(TEXT("crch_idle_fwd_to_cvr_crch_idle_R")));
		Fill(CoverCrouchToStand, *Crouch(TEXT("cvr_crch_idle_L_to_cvr_stand_idle_L")), *Crouch(TEXT("cvr_crch_idle_R_to_cvr_stand_idle_R")));
		Fill(CoverStandToCrouch, *Stand(TEXT("cvr_stand_idle_L_to_cvr_crch_idle_L")), *Stand(TEXT("cvr_stand_idle_R_to_cvr_crch_idle_R")));
	}
}

void UOperativeAnimInstance::NativeUninitializeAnimation()
{
	if (AOperativeCharacter* Operative = BoundOperative.Get())
	{
		Operative->OnWeaponFiredNative.Remove(FiredHandle);
		Operative->OnGrenadeThrowNative.Remove(GrenadeHandle);
		if (UHealthComponent* Health = Operative->HealthComponent)
		{
			Health->OnHealthChanged.RemoveDynamic(this, &UOperativeAnimInstance::HandleHealthChanged);
			Health->OnDied.RemoveDynamic(this, &UOperativeAnimInstance::HandleDied);
		}
	}
	BoundOperative.Reset();
	Super::NativeUninitializeAnimation();
}

void UOperativeAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	StateDeltaSeconds = DeltaSeconds;
	UpdateState();
	UpdateStanceTransition();
	UpdateUpperBody(DeltaSeconds);
	UpdateColdLayer(DeltaSeconds);
	UpdateNativeBlend(DeltaSeconds);
	if (bUseRifle2Locomotion)
	{
		const AOperativeCharacter* Rifle2Owner = Cast<AOperativeCharacter>(TryGetPawnOwner());
		UpdateRifle2Locomotion(DeltaSeconds, Rifle2Owner && Rifle2Owner->IsMoving(), Speed, Direction,
			Rifle2Owner ? Rifle2Owner->GetActorRotation().Yaw : 0.f);
	}
	if (CVarAnimTrace.GetValueOnGameThread() > 0 && (Speed > 0.f || TryGetPawnOwner() && TryGetPawnOwner()->GetVelocity().SizeSquared2D() > 0.f))
	{
		if (const APawn* Pawn = TryGetPawnOwner())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("AnimTrace %s t=%.3f dt=%.3f vel=%.0f speed=%.0f dir=%.0f stand=%.0f/%.2f slow=%.0f/%.2f moving=%d aim=%d crouch=%d prone=%d slots=%.2f/%.2f"),
				*Pawn->GetName(), Pawn->GetWorld()->GetTimeSeconds(), DeltaSeconds, Pawn->GetVelocity().Size2D(), Speed, Direction,
				StandBlendSpeed, StandPlayRate, SlowBlendSpeed, SlowPlayRate, bIsMoving ? 1 : 0, bIsAiming ? 1 : 0, bIsCrouching ? 1 : 0,
				bIsProne ? 1 : 0, GetSlotMontageGlobalWeight(UpperBodySlot), GetSlotMontageGlobalWeight(FullBodySlot));
		}
	}
}

void UOperativeAnimInstance::EnterRifleLocoState(ERifleLocoState NewState, UAnimSequence* Clip)
{
	LocoState = NewState;
	LocoStateTime = 0.f;
	LocoClipLength = Clip ? Clip->GetPlayLength() : 0.f;
	bLocoIdle = NewState == ERifleLocoState::Idle;
	bLocoIdleBreak = NewState == ERifleLocoState::IdleBreak;
	bLocoTurn = NewState == ERifleLocoState::Turn;
	bLocoStart = NewState == ERifleLocoState::Start;
	bLocoWalk = NewState == ERifleLocoState::Walk;
	bLocoStop = NewState == ERifleLocoState::Stop;
	switch (NewState)
	{
	case ERifleLocoState::IdleBreak: LocoBreakClip = Clip; break;
	case ERifleLocoState::Turn: LocoTurnClip = Clip; LocoTurnEased = 0.f; break;
	case ERifleLocoState::Start: LocoStartClip = Clip; break;
	case ERifleLocoState::Stop: LocoStopClip = Clip; break;
	case ERifleLocoState::Walk: LocoWalkSeconds = 0.f; break;
	default: LocoIdleSeconds = 0.f; break;
	}
}

void UOperativeAnimInstance::UpdateRifle2Locomotion(float DeltaSeconds, bool bMoveIntent, float InSpeed, float InDirection, float ActorYaw)
{
	using namespace RifleLocomotionRules;
	LocoIdleClip = Rifle2IdleLoop;
	Rifle2BlendSpeed = FMath::Min(InSpeed, Rifle2WalkClipSpeed);
	Rifle2PlayRate = FMath::Max(1.f, InSpeed / FMath::Max(1.f, Rifle2WalkClipSpeed));
	LocoStateTime += DeltaSeconds;

	// The root yaw offset: standing still the body keeps its world facing while the actor turns; moving it catches up.
	const float ActorDelta = bLocoHasYaw ? DeltaYaw(LocoLastActorYaw, ActorYaw) : 0.f;
	LocoLastActorYaw = ActorYaw;
	bLocoHasYaw = true;
	const bool bStill = LocoState == ERifleLocoState::Idle || LocoState == ERifleLocoState::IdleBreak || LocoState == ERifleLocoState::Turn;
	if (bStill)
	{
		RootYawOffset = FMath::UnwindDegrees(RootYawOffset - ActorDelta);
	}
	else
	{
		RootYawOffset = FMath::FInterpTo(RootYawOffset, 0.f, DeltaSeconds, 10.f);
	}

	auto PickClip = [](const TArray<TObjectPtr<UAnimSequence>>& Clips, int32 Index) -> UAnimSequence*
	{
		if (Clips.IsValidIndex(Index) && Clips[Index])
		{
			return Clips[Index];
		}
		// The other foot of the same sector, else nothing (the state is skipped).
		const int32 Other = Index ^ 1;
		return Clips.IsValidIndex(Other) ? Clips[Other].Get() : nullptr;
	};

	switch (LocoState)
	{
	case ERifleLocoState::Idle:
	case ERifleLocoState::IdleBreak:
	{
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
			break;
		}
		if (ShouldTurnInPlace(RootYawOffset, InSpeed, bMoveIntent))
		{
			bool bRight = false;
			const int32 Bucket = TurnBucket(RootYawOffset, bRight);
			const TArray<TObjectPtr<UAnimSequence>>& Turns = bRight ? Rifle2TurnRight : Rifle2TurnLeft;
			if (UAnimSequence* Clip = Turns.IsValidIndex(Bucket) ? Turns[Bucket].Get() : nullptr)
			{
				LocoTurnDegrees = BucketDegrees(Bucket);
				bLocoTurnRight = bRight;
				EnterRifleLocoState(ERifleLocoState::Turn, Clip);
				break;
			}
		}
		if (LocoState == ERifleLocoState::IdleBreak)
		{
			if (LocoStateTime >= LocoClipLength - 0.25f)
			{
				EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
				LocoNextBreak = FMath::FRandRange(Rifle2IdleBreakMinSeconds, Rifle2IdleBreakMaxSeconds);
			}
			break;
		}
		// A small leftover offset (under the turn trigger) settles slowly; now and then an idle break.
		RootYawOffset = FMath::FInterpTo(RootYawOffset, 0.f, DeltaSeconds, 1.5f);
		LocoIdleSeconds += DeltaSeconds;
		if (LocoIdleSeconds >= LocoNextBreak && Rifle2IdleBreaks.Num() > 0)
		{
			if (UAnimSequence* Break = Rifle2IdleBreaks[FMath::RandRange(0, Rifle2IdleBreaks.Num() - 1)])
			{
				EnterRifleLocoState(ERifleLocoState::IdleBreak, Break);
			}
		}
		break;
	}
	case ERifleLocoState::Turn:
	{
		// The clip turns the body: the offset closes by the bucket's degrees over the clip (eased).
		const float Progress = LocoClipLength > 0.f ? FMath::Clamp(LocoStateTime / LocoClipLength, 0.f, 1.f) : 1.f;
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Progress, 2.f);
		RootYawOffset = FMath::UnwindDegrees(RootYawOffset + (bLocoTurnRight ? 1.f : -1.f) * LocoTurnDegrees * (Eased - LocoTurnEased));
		LocoTurnEased = Eased;
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
		}
		else if (Progress >= 1.f)
		{
			EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
		}
		break;
	}
	case ERifleLocoState::Start:
		if (ShouldStop(bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = bInCover ? nullptr : PickClip(Rifle2Stops, ClipIndex(Sector, StartFoot(Sector))); // into cover: the enter clip, no stop
			EnterRifleLocoState(Clip ? ERifleLocoState::Stop : ERifleLocoState::Idle, Clip);
		}
		else if (LocoStateTime >= FMath::Max(0.f, LocoClipLength - StartBlendOut))
		{
			EnterRifleLocoState(ERifleLocoState::Walk, nullptr);
		}
		break;
	case ERifleLocoState::Walk:
		LocoWalkSeconds += DeltaSeconds;
		if (ShouldStop(bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = bInCover ? nullptr : PickClip(Rifle2Stops, ClipIndex(Sector, StopFoot(LocoWalkSeconds, Rifle2WalkCycleSeconds)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Stop : ERifleLocoState::Idle, Clip);
		}
		break;
	case ERifleLocoState::Stop:
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
		}
		else if (LocoStateTime >= LocoClipLength)
		{
			EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
		}
		break;
	}
}

void UOperativeAnimInstance::UpdateVaultClip(const AOperativeCharacter& Operative)
{
	const bool bVaulting = Operative.IsVaulting();
	if (bVaulting && !bWasVaulting && Operative.GetVaultDuration() >= VaultMinClipDuration)
	{
		// Left / right foot in turn (a missing one falls back to the other).
		UAnimSequenceBase* Clip = bVaultLeftNext ? VaultLeftFootAnimation : VaultRightFootAnimation;
		float Landing = bVaultLeftNext ? VaultLeftFootLandingTime : VaultRightFootLandingTime;
		if (!Clip)
		{
			Clip = bVaultLeftNext ? VaultRightFootAnimation : VaultLeftFootAnimation;
			Landing = bVaultLeftNext ? VaultRightFootLandingTime : VaultLeftFootLandingTime;
		}
		bVaultLeftNext = !bVaultLeftNext;
		if (Clip && !bIsDead)
		{
			// The clip's landing frame on the arc's end: faster for a running vault, slower for a careful one.
			const float PlayRate = FMath::Clamp(Landing / FMath::Max(Operative.GetVaultDuration(), 0.1f), 0.5f, 2.5f);
			VaultMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.1f, 0.25f, PlayRate);
			VaultClipsPlayed += VaultMontage.IsValid() ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("[Vault] %s plays %s at x%.2f (vault %.2f s)"), *Operative.DisplayName.ToString(), *Clip->GetName(),
				PlayRate, Operative.GetVaultDuration());
		}
	}
	else if (!bVaulting && bWasVaulting)
	{
		// Landed: walking on -> the recovery blends out into the locomotion; standing -> it plays to the end.
		UAnimMontage* Montage = VaultMontage.Get();
		if (Montage && Montage_IsPlaying(Montage) && Operative.IsMoving())
		{
			Montage_Stop(0.25f, Montage);
		}
	}
	bWasVaulting = bVaulting;
}

void UOperativeAnimInstance::UpdateColdLayer(float DeltaSeconds)
{
	// Godot cold_animation_controller.gd: presentation only. Eligible = standing locomotion, not aiming / sprinting /
	// reloading, alive (locomotion_controller.gd cold_eligible).
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	ColdVisualTier = ColdAnimationRules::SelectTier(ColdVisualTier, Operative ? Operative->ColdLevel : 0.f, ColdThresholds, ColdHysteresis);
	auto Resolve = [this](const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) -> UAnimSequenceBase*
	{
		TArray<bool> Set;
		for (const TObjectPtr<UAnimSequenceBase>& Clip : Clips)
		{
			Set.Add(Clip != nullptr);
		}
		const int32 Index = ColdAnimationRules::ResolveClipIndex(Set, ColdVisualTier);
		return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
	};
	if (ColdVisualTier > 0)
	{
		ColdIdleAnimation = Resolve(ColdIdleClips);
		ColdWalkAnimation = Resolve(ColdWalkClips);
		ColdMoveBlend = FMath::Clamp(Speed / ColdWalkFullSpeed, 0.f, 1.f);
	}
	const bool bHasPose = ColdIdleAnimation && ColdWalkAnimation;
	const bool bEligible = bHasPose && Stance == EOperativeStance::Standing && !bIsAiming && !bIsSprinting && !bIsReloading && !bIsDead;
	ColdVisualWeight = ColdAnimationRules::StepWeight(ColdVisualWeight, ColdVisualTier, bEligible, DeltaSeconds);
}

void UOperativeAnimInstance::HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit)
{
	AimTimer = AimHoldAfterShot;
	// Sprint 12: a shot from cover plays the corner fire (or blind fire) clip full body; the loop resumes after it.
	if (bInCover && bUseNativeCoverClips && !bIsProne && !bIsReloading)
	{
		UAnimSequenceBase* Clip = Shooter && Shooter->bIsBlindFiring ? PickCoverClip(CoverBlindFire) : nullptr;
		if (!Clip)
		{
			Clip = PickCoverClip(bIsCrouching ? CoverCrouchFire : CoverStandFire);
		}
		if (Clip)
		{
			// Shot from the corner (user rule 2026-10-06): from the plain cover idle the idle -> fire transition plays first, then
			// the shot, then the fire-ready idle again; already in the fire-ready pose the shot plays at once.
			const bool bCornerShot = !(Shooter && Shooter->bIsBlindFiring)
				&& (bCoverAtCorner || CoverHeight == ECoverHeight::LowCover || (Shooter && Shooter->IsCornerAimActive())); // sustained aim: from fire_idle, no cycle per shot; a low cover is fired over anywhere
			if (bCornerShot && PickCoverClip(bIsCrouching ? CoverCrouchFireIdle : CoverStandFireIdle))
			{
				if (CoverPendingFireClip.IsValid())
				{
					return; // the transition is playing and a shot is queued behind it
				}
				UAnimSequenceBase* Enter = bCoverInFirePose ? nullptr : PickCoverClip(bIsCrouching ? CoverCrouchFireEnter : CoverStandFireEnter);
				bCoverInFirePose = true;
				if (Enter && PlayCoverOneShot(Enter, 0.1f, 0.05f))
				{
					CoverPendingFireClip = Clip;
					return;
				}
			}
			PlayCoverOneShot(Clip, 0.1f, 0.2f);
			return;
		}
	}
	if (bIsProne)
	{
		// Godot ProneFire: the prone body shoots full body; the standing fire montage is not layered over it.
		if (FireProneAnimation && !bIsReloading && !IsPlayingStanceTransition())
		{
			PlaySlotAnimationAsDynamicMontage(FireProneAnimation, FullBodySlot, 0.05f, 0.15f);
		}
		return;
	}
	UAnimMontage* Montage = bIsAiming && FireAimMontage ? FireAimMontage : FireMontage;
	if (Montage && !bIsReloading)
	{
		Montage_Play(Montage);
	}
}

bool UOperativeAnimInstance::IsPlayingStanceTransition() const
{
	const UAnimMontage* Montage = StanceTransitionMontage.Get();
	return Montage && Montage_IsPlaying(Montage);
}

float UOperativeAnimInstance::GetStanceTransitionTimeLeft() const
{
	const UAnimMontage* Montage = StanceTransitionMontage.Get();
	if (!Montage || !Montage_IsPlaying(Montage))
	{
		return 0.f;
	}
	return FMath::Max(0.f, (Montage->GetPlayLength() - Montage_GetPosition(Montage)) / FMath::Max(StanceTransitionPlayRate, 0.1f)
		- StanceTransitionBlendTime);
}

void UOperativeAnimInstance::UpdateStanceTransition()
{
	// Godot locomotion_controller.gd: ProneStart when the stance becomes prone, ProneEnd when it leaves it (auto-advance
	// at the clip's end); here the clip plays on the full-body slot over the already switched locomotion.
	if (!BoundOperative.IsValid())
	{
		return;
	}
	const EOperativeStance From = PreviousStance.Get(Stance);
	PreviousStance = Stance;
	if (From == Stance || bIsDead)
	{
		return;
	}
	if (bInCover && bUseNativeCoverClips && bCoverEnteredThisFrame)
	{
		return; // the cover enter clip carries the stance change (stand -> crouch at a low cover)
	}
	// Stand <-> crouch at the wall (user request 2026-10-07): the pack's cover transitions on the facing side
	// (cvr_crch_idle_L_to_cvr_stand_idle_L ... measured: they keep the side and end in the other stance's cover idle).
	if (bInCover && bUseNativeCoverClips && !bShimmying && From != EOperativeStance::Prone && Stance != EOperativeStance::Prone)
	{
		if (UAnimSequenceBase* CoverSwitch = PickCoverClip(Stance == EOperativeStance::Crouching ? CoverStandToCrouch : CoverCrouchToStand))
		{
			if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
			{
				Montage_Stop(0.15f, Loop);
			}
			CoverLoopMontage.Reset();
			CoverLoopClip.Reset();
			CoverPendingFireClip.Reset();
			bCoverInFirePose = false; // the switch starts from the cover idle; the fire stance is re-entered after it
			StanceTransitionMontage = PlaySlotAnimationAsDynamicMontage(CoverSwitch, FullBodySlot, 0.15f, 0.2f);
			if (StanceTransitionMontage.IsValid())
			{
				++CoverClipsPlayed;
				CoverClipLog.Add(CoverSwitch->GetName());
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance switch at the wall: %s (%s)"), *CoverSwitch->GetName(),
				StanceTransitionMontage.IsValid() ? TEXT("playing") : TEXT("not played"));
			return;
		}
	}
	if (bInCover)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance change in cover without a cover switch (native %d, shimmy %d)"),
			bUseNativeCoverClips ? 1 : 0, bShimmying ? 1 : 0);
	}
	UAnimSequenceBase* Clip = nullptr;
	switch (Stance)
	{
	case EOperativeStance::Prone:
		Clip = From == EOperativeStance::Crouching ? CrouchToProneAnimation.Get() : StandToProneAnimation.Get();
		break;
	case EOperativeStance::Crouching:
		Clip = From == EOperativeStance::Prone ? ProneToCrouchAnimation.Get() : StandToCrouchAnimation.Get();
		break;
	default:
		Clip = From == EOperativeStance::Prone ? ProneToStandAnimation.Get() : CrouchToStandAnimation.Get();
		break;
	}
	if (!Clip)
	{
		// No clip: the graph's crossfade; drop a transition still playing so it does not hold the old stance.
		if (IsPlayingStanceTransition())
		{
			StopSlotAnimation(StanceTransitionBlendTime, FullBodySlot);
		}
		return;
	}
	StopSlotAnimation(StanceTransitionBlendTime, UpperBodySlot);
	StanceTransitionMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, StanceTransitionBlendTime,
		StanceTransitionBlendTime, StanceTransitionPlayRate);
}

void UOperativeAnimInstance::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	// Godot play_hit_reaction: per stance, the pistol one with the pistol in hands; not over a throw or while dead.
	const AOperativeCharacter* Operative = BoundOperative.Get();
	if (Delta >= 0.f || NewHealth <= 0.f || !Operative || bIsDead)
	{
		return;
	}
	UAnimSequenceBase* Clip = HitStandAnimation;
	if (Operative->CurrentWeapon && Operative->CurrentWeapon->WeaponId == TEXT("pistol") && PistolHitAnimation)
	{
		Clip = PistolHitAnimation;
	}
	else if (Operative->GetStance() == EOperativeStance::Crouching)
	{
		Clip = HitCrouchAnimation;
	}
	else if (Operative->GetStance() == EOperativeStance::Prone)
	{
		// The prone body reacts as a whole (Godot hit_prone), unless it is still lying down / getting up.
		if (HitProneAnimation && !IsPlayingStanceTransition())
		{
			PlaySlotAnimationAsDynamicMontage(HitProneAnimation, FullBodySlot, 0.1f, 0.2f);
		}
		return;
	}
	if (Clip && !IsPlayingSlotAnimation(GrenadeThrowWalkAnimation, UpperBodySlot))
	{
		PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, 0.1f, 0.2f);
		LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Clip->GetPlayLength()); // the hit reaction has the arms
	}
}

void UOperativeAnimInstance::HandleGrenadeThrow()
{
	// Godot play_grenade_throw: prone / crouch / run / walk by the stance and speed. Godot released the grenade at 70 % of
	// the clip (get_grenade_throw_duration); the measured per-clip release time replaces that (GrenadeThrow*ReleaseSeconds).
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (!Operative)
	{
		return;
	}
	const EOperativeStance ThrowStance = Operative->GetStance();
	const bool bRunning = ThrowStance == EOperativeStance::Standing && (Operative->IsSprinting() || Operative->GetVelocity().Size2D() > 300.f);
	UAnimSequenceBase* Clip = ThrowStance == EOperativeStance::Prone ? GrenadeThrowProneAnimation
		: ThrowStance == EOperativeStance::Crouching ? GrenadeThrowCrouchAnimation
		: bRunning ? GrenadeThrowRunAnimation : GrenadeThrowWalkAnimation;
	const float ReleaseSeconds = ThrowStance == EOperativeStance::Prone ? GrenadeThrowProneReleaseSeconds
		: ThrowStance == EOperativeStance::Crouching ? GrenadeThrowCrouchReleaseSeconds
		: bRunning ? GrenadeThrowRunReleaseSeconds : GrenadeThrowWalkReleaseSeconds;
	if (!Clip)
	{
		return;
	}
	// The grenade leaves the hand at the clip's measured release frame (GrenadeSubsystem reads both fields right after
	// this event); the montage plays on the upper-body slot only, so the legs keep the locomotion.
	Operative->GrenadeThrowDuration = Clip->GetPlayLength();
	Operative->GrenadeReleaseSeconds = ReleaseSeconds;
	PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, GrenadeThrowBlendInSeconds, GrenadeThrowBlendOutSeconds);
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Clip->GetPlayLength()); // the left hand throws
}

void UOperativeAnimInstance::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	// Godot play_death: a random standing variation, or the crouched / prone death; full body, held at the end.
	const AOperativeCharacter* Operative = BoundOperative.Get();
	if (bDeathPlayed || !Operative)
	{
		return;
	}
	bDeathPlayed = true;
	UAnimSequenceBase* Clip = Operative->GetStance() == EOperativeStance::Crouching ? DeathCrouchAnimation.Get()
		: Operative->GetStance() == EOperativeStance::Prone ? DeathProneAnimation.Get()
		: DeathStandAnimations.IsEmpty() ? nullptr : DeathStandAnimations[FMath::RandRange(0, DeathStandAnimations.Num() - 1)].Get();
	if (!Clip)
	{
		return;
	}
	StopSlotAnimation(0.1f, UpperBodySlot);
	if (UAnimMontage* Montage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.15f, 0.f, 1.f, 1, -1.f,
		Stance == EOperativeStance::Standing ? FMath::Min(DeathStartOffset, Clip->GetPlayLength() * 0.9f) : 0.f))
	{
		// Held on the last frame (the running instance's flag; the montage asset's is copied only at the start).
		if (FAnimMontageInstance* Instance = GetActiveInstanceForMontage(Montage))
		{
			Instance->bEnableAutoBlendOut = false;
		}
	}
}

void UOperativeAnimInstance::PlayWorkingDevice(float Seconds)
{
	if (WorkingDeviceAnimation && !bIsDead)
	{
		PlaySlotAnimationAsDynamicMontage(WorkingDeviceAnimation, UpperBodySlot, 0.15f, 0.2f,
			WorkingDeviceAnimation->GetPlayLength() / FMath::Max(Seconds, 0.1f));
	}
}

void UOperativeAnimInstance::UpdateUpperBody(float DeltaSeconds)
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	AimTimer = FMath::Max(0.f, AimTimer - DeltaSeconds);
	bool bAttackMode = false;
	if (Operative && Operative->GetWorld())
	{
		const UTurnBasedCombatSubsystem* TurnBased = Operative->GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
		bAttackMode = TurnBased && TurnBased->IsActive() && TurnBased->IsAttackMode() && TurnBased->GetActiveUnit() == Operative;
	}
	// Godot: a sprinting operative does not shoot (and runs with the rifle down) — no aim pose while sprinting.
	if (bIsSprinting)
	{
		AimTimer = 0.f;
	}
	bIsAiming = !bIsDead && !bIsSprinting && (bAttackMode || AimTimer > 0.f);

	// Blend-space axes (user report 2026-10-01: legs and arms trembled while slowing down). The speed is smoothed and
	// has a dead zone with hysteresis (formation followers stop / restart and creep at 10-25 cm/s, and the raw speed
	// flicked the blend space between the idle and the walk); slower than the walk samples the pose stays the walk and
	// the clip slows down instead of mixing in the idle; above the samples' range the clip speeds up (no foot sliding).
	LocomotionSpeed = FMath::FInterpTo(LocomotionSpeed, Speed, DeltaSeconds, 8.f);
	bLocomotionMoving = bLocomotionMoving ? LocomotionSpeed > LocomotionStopSpeed : LocomotionSpeed > LocomotionStartSpeed;
	auto Axis = [this](float WalkSampleSpeed, float MaxSpeed, float& OutBlend, float& OutRate)
	{
		if (!bLocomotionMoving)
		{
			OutBlend = 0.f;
			OutRate = 1.f;
		}
		else if (LocomotionSpeed < WalkSampleSpeed)
		{
			OutBlend = WalkSampleSpeed;
			OutRate = FMath::Max(MinWalkPlayRate, LocomotionSpeed / WalkSampleSpeed);
		}
		else
		{
			OutBlend = FMath::Min(LocomotionSpeed, MaxSpeed);
			OutRate = FMath::Max(1.f, LocomotionSpeed / MaxSpeed);
		}
	};
	Axis(FMath::Min(WalkSampleSpeed, StandBlendSpaceMaxSpeed), StandBlendSpaceMaxSpeed, StandBlendSpeed, StandPlayRate);
	Axis(FMath::Min(WalkSampleSpeed, SlowBlendSpaceMaxSpeed), SlowBlendSpaceMaxSpeed, SlowBlendSpeed, SlowPlayRate);
	Axis(ProneBlendSpaceMaxSpeed, ProneBlendSpaceMaxSpeed, ProneBlendSpeed, PronePlayRate);

	// Reload: one clip per reload, stretched to the reload time left.
	// Prone: its own full-body clip (Godot ProneReload) or nothing.
	UAnimSequenceBase* Reload = bIsProne ? ReloadProneAnimation.Get() : ReloadAnimation.Get();
	const FName ReloadSlot = bIsProne ? FullBodySlot : UpperBodySlot;
	if (bIsReloading && !bWasReloading && Reload && Operative && !IsPlayingStanceTransition())
	{
		const float Duration = FMath::Max(0.1f, Operative->ReloadTimer);
		PlaySlotAnimationAsDynamicMontage(Reload, ReloadSlot, 0.2f, 0.2f, Reload->GetPlayLength() / Duration);
	}
	else if (!bIsReloading && bWasReloading && Reload)
	{
		StopSlotAnimation(0.2f, ReloadSlot);
	}
	bWasReloading = bIsReloading;
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
	// The blend-space direction follows the movement smoothly and is held below 30 cm/s: braking at the goal made the
	// raw velocity direction flip and the legs jump between the forward / side / back clips.
	if (Speed > 30.f)
	{
		const float RawDirection = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Operative->GetActorRotation().Yaw);
		const float Alpha = 1.f - FMath::Exp(-12.f * StateDeltaSeconds);
		Direction = FRotator::NormalizeAxis(Direction + FRotator::NormalizeAxis(RawDirection - Direction) * Alpha);
	}
	// Turn-based steps move the actor directly (no velocity): walk forward over the whole path.
	const UTurnBasedCombatSubsystem* TurnBased = Operative->GetWorld() ? Operative->GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (const float TacticalSpeed = TurnBased ? TurnBased->GetTacticalMoveSpeed(Operative) : -1.f; TacticalSpeed >= 0.f)
	{
		Speed = TacticalSpeed;
		Direction = 0.f;
	}
	// A vault moves the actor directly too (Godot "Vault" state; the graph may play its own clip on bIsVaulting).
	bIsVaulting = Operative->IsVaulting();
	if (bIsVaulting)
	{
		Speed = Operative->GetVaultSpeed();
		Direction = 0.f;
	}
	UpdateVaultClip(*Operative);
	// Sprint 12 cover state (the shimmy moves sideways: the blend-space direction already strafes).
	bInCover = Operative->bInCover;
	CoverHeight = Operative->CurrentCoverHeight;
	CoverFacing = Operative->CoverFacing;
	bShimmying = Operative->bShimmying;
	ShimmyDirection = Operative->ShimmyDirection;
	bLeaning = Operative->bIsCornerLeaning;
	bBlindFiring = Operative->bIsBlindFiring;
	bCoverShimmyForward = bShimmying && Operative->IsShimmyForward();
	bCoverAtCorner = bInCover && Operative->bAtCoverCorner;
	bCoverFireReady = bInCover && Operative->IsCoverFireReady();
	bCoverCornerAim = bInCover && Operative->IsCornerAimActive();
	// The stance first (user request 2026-10-07): entering a crouched cover from a standing run picked the STANDING
	// enter clip because the cover layer still saw last frame's stance.
	Stance = Operative->GetStance();
	bIsCrouching = Stance == EOperativeStance::Crouching;
	bIsProne = Stance == EOperativeStance::Prone;
	bIsReloading = Operative->bIsReloading;
	UpdateCoverLayer(*Operative);
	bIsMoving = Speed > 5.f;
	bIsSprinting = Operative->IsSprinting();
	bIsDead = Operative->HealthComponent && !Operative->HealthComponent->IsAlive();
	if (const UColdSurvivalComponent* Cold = Operative->ColdSurvival)
	{
		ColdTier = Cold->GetTier();
		bIsFrostbitten = Cold->IsFrostbitten();
		bIsWeaponFrozen = Cold->IsWeaponFrozen();
	}
	UpdateLeftHandIK(*Operative, StateDeltaSeconds);
	UpdateAimOffset(*Operative, StateDeltaSeconds);
}

void UOperativeAnimInstance::UpdateLeftHandIK(const AOperativeCharacter& Operative, float DeltaSeconds)
{
	// The grip in hand_r bone space: the socket on the weapon mesh, composed with the weapon's transform relative to its
	// attach parent (hand_r). Recomputed every update: a weapon switch / a new mesh / offset shows at once.
	const UStaticMeshComponent* Weapon = Operative.WeaponMesh;
	const UStaticMesh* WeaponAsset = Weapon ? Weapon->GetStaticMesh() : nullptr;
	const UStaticMeshSocket* Grip = WeaponAsset ? WeaponAsset->FindSocket(LeftHandGripSocket) : nullptr;
	bLeftHandIKGripValid = Grip && Weapon->GetAttachSocketName() == Operative.WeaponSocket;
	const USkeletalMeshComponent* Body = GetSkelMeshComponent();
	FTransform HandR = FTransform::Identity;
	FVector Shoulder = FVector::ZeroVector;
	LeftHandIKSlideCm = 0.f;
	LeftHandIKExcessCm = 0.f;
	if (bLeftHandIKGripValid)
	{
		const FTransform SocketInWeapon(Grip->RelativeRotation, Grip->RelativeLocation, Grip->RelativeScale);
		const FTransform InHand = LeftHandIKRules::GripInHandSpace(Weapon->GetRelativeTransform(), SocketInWeapon);
		LeftHandIKOffset = InHand.GetLocation();
		LeftHandIKRotation = InHand.Rotator();
		// The arm's reach (upper + lower arm of the reference skeleton) and the shoulder in hand_r space from the last
		// pose: a grip beyond reach slides back along the barrel to the furthest reachable point (user report
		// 2026-10-07: the m16 handguard is 72-74 cm from the shoulder on the pack's fire stances, the arm reaches 57).
		if (Body && Body->GetSkeletalMeshAsset())
		{
			if (LeftHandArmReach <= 0.f || LeftHandIKChainRoot != LeftHandArmReachRoot)
			{
				// The chain's length from LeftHandIKChainRoot down to hand_l (Two Bone IK: upperarm_l; a FABRIK from
				// clavicle_l reaches further).
				LeftHandArmReachRoot = LeftHandIKChainRoot;
				LeftHandArmReach = 0.f;
				const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
				const int32 Root = Ref.FindBoneIndex(LeftHandIKChainRoot);
				for (int32 Bone = Ref.FindBoneIndex(TEXT("hand_l")); Bone != INDEX_NONE && Bone != Root && Root != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
				{
					LeftHandArmReach += static_cast<float>(Ref.GetRefBonePose()[Bone].GetLocation().Size());
				}
			}
			HandR = Body->GetSocketTransform(TEXT("hand_r"), RTS_Component);
			// Robust to how the weapon is attached (a skeletal socket with its own offset, a weapon bone...): the grip's
			// world location taken into the hand_r BONE space the IK node uses (Effector Target = hand_r, Bone Space).
			const FTransform GripWorld = Weapon->GetSocketTransform(LeftHandGripSocket, RTS_World);
			const FTransform GripInHand = GripWorld.GetRelativeTransform(Body->GetComponentTransform()).GetRelativeTransform(HandR);
			LeftHandIKOffset = GripInHand.GetLocation();
			LeftHandIKRotation = GripInHand.Rotator();
			Shoulder = HandR.InverseTransformPosition(Body->GetSocketTransform(LeftHandIKChainRoot, RTS_Component).GetLocation());
			if (LeftHandArmReach > 0.f)
			{
				const FVector Axis = Weapon->GetRelativeTransform().TransformVectorNoScale(Operative.MuzzleOffset.GetSafeNormal());
				const FVector Reachable = LeftHandIKRules::SlideIntoReach(LeftHandIKOffset, Axis, Shoulder,
					LeftHandArmReach * LeftHandIKReachFraction, LeftHandIKMaxSlideCm);
				LeftHandIKSlideCm = static_cast<float>(FVector::Dist(Reachable, LeftHandIKOffset));
				LeftHandIKOffset = Reachable;
				LeftHandIKExcessCm = FMath::Max(0.f, static_cast<float>(FVector::Dist(Reachable, Shoulder)) - LeftHandArmReach * LeftHandIKReachFraction);
			}
		}
	}
	LeftHandIKBlockSeconds = FMath::Max(0.f, LeftHandIKBlockSeconds - DeltaSeconds);
	FLeftHandIKState State;
	State.bEnabled = bLeftHandIK;
	State.bHasGrip = bLeftHandIKGripValid && Operative.UsesAmmo(); // a melee weapon has no handguard
	State.bWeaponVisible = Weapon && Weapon->IsVisible();
	State.bReloading = bIsReloading;
	State.bUpperBodyAction = LeftHandIKBlockSeconds > 0.f;
	State.bVaulting = bIsVaulting;
	State.bDead = bIsDead;
	State.bProne = bIsProne;
	// Every pose where the right hand holds the rifle keeps the IK (cover idle / look-around / shimmy / enter / switches
	// included, 2026-10-07); only clips listed in LeftHandIKFreeClips free the left hand (none measured so far).
	{
		FString Playing;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		GetCoverPlayback(Playing, MontageWeight, SlotWeight);
		State.bTwoHandedPose = !(MontageWeight > 0.5f && LeftHandIKFreeClips.ContainsByPredicate([&Playing](const FString& Part) { return !Part.IsEmpty() && Playing.Contains(Part); }));
	}
	LeftHandIKLinear = LeftHandIKRules::StepAlpha(LeftHandIKLinear, LeftHandIKRules::WantsIK(State), DeltaSeconds, LeftHandIKBlendSeconds);
	// Still out of reach after the slide (a rifle hanging low): fade by the excess instead of a straight arm.
	LeftHandIKAlpha = FMath::SmoothStep(0.f, 1.f, LeftHandIKLinear) * LeftHandIKRules::ReachFade(LeftHandIKExcessCm, LeftHandIKFadeCm);
	if (LeftHandIKAlphaOverrideForTesting >= 0.f)
	{
		LeftHandIKAlpha = FMath::Clamp(LeftHandIKAlphaOverrideForTesting, 0.f, 1.f);
	}
	// Codex.Debug.LeftHandIK 1: red = the LeftHandGrip socket, green = the IK target (after the reach slide), yellow = the
	// left hand now, blue = the shoulder (upperarm_l) with the reach as a circle radius in the log line.
	if (CVarDebugLeftHandIK.GetValueOnGameThread() > 0 && Body && Weapon && Operative.GetWorld())
	{
		UWorld* World = Operative.GetWorld();
		const FTransform BodyXf = Body->GetComponentTransform();
		const FVector TargetWorld = BodyXf.TransformPosition(HandR.TransformPosition(LeftHandIKOffset));
		if (bLeftHandIKGripValid)
		{
			DrawDebugSphere(World, Weapon->GetSocketLocation(LeftHandGripSocket), 3.f, 8, FColor::Red, false, -1.f, SDPG_Foreground);
			DrawDebugSphere(World, TargetWorld, 3.5f, 8, FColor::Green, false, -1.f, SDPG_Foreground);
		}
		DrawDebugSphere(World, Body->GetBoneLocation(TEXT("hand_l")), 2.5f, 8, FColor::Yellow, false, -1.f, SDPG_Foreground);
		DrawDebugSphere(World, Body->GetBoneLocation(LeftHandIKChainRoot), 2.5f, 8, FColor::Blue, false, -1.f, SDPG_Foreground);
		DrawDebugString(World, TargetWorld + FVector(0.f, 0.f, 12.f), FString::Printf(TEXT("IK a=%.2f slide %.0f reach %.0f"),
			LeftHandIKAlpha, LeftHandIKSlideCm, LeftHandArmReach), nullptr, FColor::Green, 0.f, true, 1.f);
	}
}

void UOperativeAnimInstance::UpdateAimOffset(const AOperativeCharacter& Operative, float DeltaSeconds)
{
	// The pitch from the stance's muzzle height (stable: it does not move with the pose the offset itself bends) to the
	// point the tracer flies to (the target's capsule centre).
	FVector AimPoint = FVector::ZeroVector;
	bHasAimTarget = bAimOffset && Operative.GetAimTargetPoint(AimPoint);
	AimPitchTarget = bHasAimTarget ? AimOffsetRules::PitchToTarget(Operative.GetMuzzleLocation(), AimPoint, AimPitchClampDegrees) : 0.f;
	AimPitch = FMath::Clamp(FMath::FInterpTo(AimPitch, AimPitchTarget, DeltaSeconds, AimPitchInterpSpeed), -AimPitchClampDegrees, AimPitchClampDegrees);
	if (!bHasAimTarget && FMath::Abs(AimPitch) < 0.05f)
	{
		AimPitch = 0.f;
	}
	FAimOffsetState State;
	State.bEnabled = bAimOffset && (bAimOffsetInCover || !bInCover);
	State.bRangedWeapon = Operative.UsesAmmo();
	State.bWeaponVisible = Operative.WeaponMesh && Operative.WeaponMesh->IsVisible();
	State.bReloading = bIsReloading;
	State.bUpperBodyAction = LeftHandIKBlockSeconds > 0.f; // grenade throw / hit reaction windows (shared with the left-hand IK)
	State.bVaulting = bIsVaulting;
	State.bSprinting = bIsSprinting;
	State.bProne = bIsProne;
	State.bDead = bIsDead;
	AimOffsetLinear = AimOffsetRules::StepAlpha(AimOffsetLinear, AimOffsetRules::WantsAimOffset(State), DeltaSeconds, AimOffsetBlendSeconds);
	AimOffsetAlpha = FMath::SmoothStep(0.f, 1.f, AimOffsetLinear);
	bAimOffsetActive = AimOffsetAlpha > 0.01f;
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

// --- Tactical cover baseline (Sprint 12) ------------------------------------------------------------------------

UAnimSequenceBase* UOperativeAnimInstance::PickCoverClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) const
{
	// The pack names its sides facing the wall: *_L works at his own RIGHT (back to the wall), *_R at his left
	// (CoverFacingRules::ClipIndex: Right -> 0 = _L, Left -> 1 = _R; measured 2026-10-06).
	const int32 Index = CoverFacingRules::ClipIndex(CoverFacing);
	if (Clips.IsValidIndex(Index) && Clips[Index])
	{
		return Clips[Index];
	}
	// The other side's clip stands in (unmirrored: the user assigns / mirrors the missing side).
	return Clips.IsValidIndex(1 - Index) ? Clips[1 - Index].Get() : nullptr;
}

UAnimSequenceBase* UOperativeAnimInstance::PickShimmyClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, bool bForward) const
{
	// The walk clips' suffix is the movement direction along the wall in the pack's naming (_L = towards his own right):
	// forward = the facing side's clip, backing away = the other side's suffix (CoverFacingRules::ShimmyClipIndex).
	const int32 Index = CoverFacingRules::ShimmyClipIndex(CoverFacing, bForward);
	if (Clips.IsValidIndex(Index) && Clips[Index])
	{
		return Clips[Index];
	}
	return Clips.IsValidIndex(1 - Index) ? Clips[1 - Index].Get() : nullptr;
}

void UOperativeAnimInstance::UpdateCoverLayer(const AOperativeCharacter& Operative)
{
	const bool bEntered = bInCover && !bWasInCover;
	const bool bLeft = !bInCover && bWasInCover;
	bWasInCover = bInCover;
	bCoverEnteredThisFrame = bEntered;
	if (!bUseNativeCoverClips || bIsDead)
	{
		return;
	}
	if (bLeft)
	{
		if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
		{
			Montage_Stop(0.25f, Loop);
		}
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
		CoverPendingFireClip.Reset();
		bCoverEnterPlaying = false;
		// A cover one-shot (enter / fire / transition) must not outlive the cover: it would hold the cover pose while the
		// capsule walks off (user-found bug 2026-10-07: grid walks slid across the floor in the cover pose).
		if (UAnimMontage* OneShot = CoverOneShotMontage.Get(); OneShot && Montage_IsPlaying(OneShot))
		{
			Montage_Stop(0.2f, OneShot);
		}
		CoverOneShotMontage.Reset();
		// Leaving the fire-ready pose: the exit transition plays when he stays put (walking on blends straight out; an
		// open shot at a target in front of the wall blends straight into the normal shooting pose; a move order / grid
		// walk leaves walking).
		if (bCoverInFirePose && Speed < 20.f && !Operative.IsCoverOpenShotActive() && !Operative.IsMovingOffCover())
		{
			if (UAnimSequenceBase* Exit = PickCoverClip(bIsCrouching ? CoverCrouchFireExit : CoverStandFireExit))
			{
				PlayCoverOneShot(Exit, 0.1f, 0.2f);
			}
		}
		bCoverInFirePose = false;
		return;
	}
	if (!bInCover)
	{
		if (Speed > 20.f)
		{
			if (UAnimMontage* OneShot = CoverOneShotMontage.Get(); OneShot && Montage_IsPlaying(OneShot))
			{
				Montage_Stop(0.2f, OneShot); // the fire -> idle exit never plays over a walk
			}
			CoverOneShotMontage.Reset();
		}
		return;
	}
	if (bIsProne)
	{
		return;
	}
	// Cover_Enter once, then the loop. Back at the wall after an open shot (he only stepped off it): straight to the loop.
	if (bEntered && Operative.IsQuietCoverEntry())
	{
		bCoverEnterPlaying = false;
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
	}
	else if (bEntered)
	{
		if (UAnimSequenceBase* Enter = PickCoverClip(bIsCrouching ? CoverCrouchEnter : CoverStandEnter))
		{
			if (Operative.IsCoverEnteringFromRun())
			{
				// User-found bug 2026-10-07 (run -> hard stop -> enter clip): no stop between them, an eased blend over the
				// last steps (the capsule is already at the slot; the mesh offset carries the body from where it ran).
				FAlphaBlendArgs BlendIn(FMath::Max(CoverEnterFromRunBlendSeconds, 0.f));
				BlendIn.BlendOption = EAlphaBlendOption::HermiteCubic;
				FAlphaBlendArgs BlendOut(0.2f);
				CoverOneShotMontage = PlaySlotAnimationAsDynamicMontage_WithBlendArgs(Enter, FullBodySlot, BlendIn, BlendOut);
			}
			else
			{
				StopSlotAnimation(0.15f, FullBodySlot);
				CoverOneShotMontage = PlaySlotAnimationAsDynamicMontage(Enter, FullBodySlot, 0.15f, 0.2f);
			}
			CoverClipsPlayed += CoverOneShotMontage.IsValid() ? 1 : 0;
			bCoverEnterPlaying = CoverOneShotMontage.IsValid();
			if (bCoverEnterPlaying)
			{
				CoverClipLog.Add(Enter->GetName());
			}
			CoverLoopMontage.Reset();
			CoverLoopClip.Reset();
		}
	}
	{
		UAnimMontage* OneShot = CoverOneShotMontage.Get();
		bool bOneShotPlaying = OneShot && Montage_IsPlaying(OneShot);
		if (bCoverEnterPlaying && (!bOneShotPlaying || bShimmying))
		{
			// The enter clip is done, or a shimmy ordered right after the entry cuts it short (never a walk blocked by it).
			if (bOneShotPlaying)
			{
				Montage_Stop(0.15f, OneShot);
				bOneShotPlaying = false;
			}
			bCoverEnterPlaying = false;
		}
		if (bShimmying && bOneShotPlaying)
		{
			// A shimmy cuts any cover one-shot (the fire -> idle exit, a transition): the side-step walk shows at once
			// instead of the body sliding along the wall in the exit pose (user request 2026-10-07, seen crouched).
			Montage_Stop(0.15f, OneShot);
			bOneShotPlaying = false;
			CoverPendingFireClip.Reset();
		}
		if (UAnimSequenceBase* Pending = CoverPendingFireClip.Get())
		{
			// The shot queued behind the idle -> fire transition starts as that ends.
			if (!bOneShotPlaying || OneShot->GetPlayLength() - Montage_GetPosition(OneShot) <= 0.12f)
			{
				CoverPendingFireClip.Reset();
				PlayCoverOneShot(Pending, 0.1f, 0.2f);
				return;
			}
		}
		if (bOneShotPlaying)
		{
			return; // enter / transition / fire clip in progress
		}
	}
	if (IsPlayingStanceTransition())
	{
		return; // stand <-> crouch at the wall: the stance clip plays, the loop follows
	}
	// Fire-ready pose (user rule 2026-10-06): a threat is known and he stands at the exposed edge on its side -> the
	// idle -> fire transition, then the fire-ready idle; the exit transition when the threat is gone for the hold time,
	// he shimmies off the edge or leaves the cover. Without the clips the look-around corner pose stands in.
	UAnimSequenceBase* FireIdle = PickCoverClip(bIsCrouching ? CoverCrouchFireIdle : CoverStandFireIdle);
	const bool bWantFirePose = bCoverFireReady && FireIdle;
	if (bWantFirePose && !bCoverInFirePose)
	{
		bCoverInFirePose = true;
		if (UAnimSequenceBase* Enter = PickCoverClip(bIsCrouching ? CoverCrouchFireEnter : CoverStandFireEnter))
		{
			if (PlayCoverOneShot(Enter, 0.2f, 0.1f))
			{
				return;
			}
		}
	}
	else if (!bWantFirePose && bCoverInFirePose)
	{
		bCoverInFirePose = false;
		if (UAnimSequenceBase* Exit = PickCoverClip(bIsCrouching ? CoverCrouchFireExit : CoverStandFireExit))
		{
			if (PlayCoverOneShot(Exit, 0.1f, 0.2f))
			{
				return;
			}
		}
	}
	// Reload behind the corner (user request 2026-10-07; the pack has no cover reload clip): after the fire -> idle exit
	// the cover loop steps aside so the normal reload (UpperBodySlot over the graph's idle) shows; the loop resumes after.
	if (bIsReloading && !bShimmying)
	{
		if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
		{
			Montage_Stop(0.25f, Loop);
		}
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
		return;
	}
	// The loop wanted now: shimmy forward (towards the threat side he faces) / backwards (away from it, still facing it),
	// the corner-ready pose at the exposed edge on that side, else the idle. Never the plain locomotion walk in cover.
	UAnimSequenceBase* Wanted = nullptr;
	float PlayRate = 1.f;
	if (bShimmying)
	{
		Wanted = PickShimmyClip(bIsCrouching ? (bCoverShimmyForward ? CoverCrouchMoveForward : CoverCrouchMoveBackward)
			: (bCoverShimmyForward ? CoverStandMoveForward : CoverStandMoveBackward), bCoverShimmyForward);
		PlayRate = FMath::Clamp(Speed / FMath::Max(CoverShimmyClipSpeed, 1.f), 0.5f, 2.f);
	}
	if (!Wanted && bCoverInFirePose && FireIdle)
	{
		Wanted = FireIdle; // threat known at the edge: ready to fire
	}
	if (!Wanted && bCoverAtCorner)
	{
		Wanted = PickCoverClip(bIsCrouching ? CoverCrouchCorner : CoverStandCorner); // no threat: looking round the corner
	}
	if (!Wanted)
	{
		Wanted = PickCoverClip(bIsCrouching ? CoverCrouchIdle : CoverStandIdle);
	}
	if (!Wanted)
	{
		return; // no clips assigned: the graph's locomotion shows (back to the wall by the actor rotation)
	}
	UAnimMontage* Loop = CoverLoopMontage.Get();
	if (CoverLoopClip.Get() == Wanted && Loop && Montage_IsPlaying(Loop))
	{
		Montage_SetPlayRate(Loop, PlayRate); // the shimmy pace follows the ground speed
		return;
	}
	if (Loop && Montage_IsPlaying(Loop))
	{
		Montage_Stop(0.2f, Loop);
	}
	CoverLoopClip = Wanted;
	CoverLoopMontage = PlaySlotAnimationAsDynamicMontage(Wanted, FullBodySlot, 0.2f, 0.2f, PlayRate, /*LoopCount*/ 1000);
	CoverClipsPlayed += CoverLoopMontage.IsValid() ? 1 : 0;
	if (CoverLoopMontage.IsValid())
	{
		CoverClipLog.Add(Wanted->GetName());
		if (CoverClipLog.Num() > 40)
		{
			CoverClipLog.RemoveAt(0, CoverClipLog.Num() - 40);
		}
	}
}

void UOperativeAnimInstance::GetCoverPlayback(FString& OutClip, float& OutMontageWeight, float& OutSlotNodeWeight) const
{
	OutClip = TEXT("none");
	OutMontageWeight = 0.f;
	OutSlotNodeWeight = GetSlotNodeGlobalWeight(FullBodySlot);
	if (const FAnimMontageInstance* Instance = GetActiveMontageInstance(); Instance && Instance->Montage)
	{
		OutMontageWeight = Instance->GetWeight();
		for (const FSlotAnimationTrack& Track : Instance->Montage->SlotAnimTracks)
		{
			if (Track.AnimTrack.AnimSegments.Num() > 0 && Track.AnimTrack.AnimSegments[0].GetAnimReference())
			{
				OutClip = Track.AnimTrack.AnimSegments[0].GetAnimReference()->GetName();
				break;
			}
		}
	}
}

float UOperativeAnimInstance::GetCoverEnterBlendWeight() const
{
	if (!bCoverEnterPlaying)
	{
		return -1.f; // no enter clip playing (none assigned, done, or cut short)
	}
	const UAnimMontage* OneShot = CoverOneShotMontage.Get();
	const FAnimMontageInstance* Instance = OneShot ? GetActiveInstanceForMontage(OneShot) : nullptr;
	return Instance ? Instance->GetWeight() : -1.f;
}

bool UOperativeAnimInstance::PlayCoverOneShot(UAnimSequenceBase* Clip, float BlendIn, float BlendOut)
{
	if (!Clip)
	{
		return false;
	}
	if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
	{
		Montage_Stop(BlendIn, Loop);
	}
	CoverLoopMontage.Reset();
	CoverLoopClip.Reset();
	bCoverEnterPlaying = false;
	CoverOneShotMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, BlendIn, BlendOut);
	if (!CoverOneShotMontage.IsValid())
	{
		return false;
	}
	++CoverClipsPlayed;
	CoverClipLog.Add(Clip->GetName());
	if (CoverClipLog.Num() > 40)
	{
		CoverClipLog.RemoveAt(0, CoverClipLog.Num() - 40);
	}
	return true;
}
