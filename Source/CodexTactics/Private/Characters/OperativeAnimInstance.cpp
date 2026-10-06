#include "Characters/OperativeAnimInstance.h"
#include "CodexTactics.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Characters/ColdAnimationRules.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Survival/ColdSurvivalComponent.h"
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
			UAnimSequence* Clip = PickClip(Rifle2Stops, ClipIndex(Sector, StartFoot(Sector)));
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
			UAnimSequence* Clip = PickClip(Rifle2Stops, ClipIndex(Sector, StopFoot(LocoWalkSeconds, Rifle2WalkCycleSeconds)));
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
			if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
			{
				Montage_Stop(0.1f, Loop);
			}
			CoverOneShotMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.1f, 0.2f);
			CoverClipsPlayed += CoverOneShotMontage.IsValid() ? 1 : 0;
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
	}
}

void UOperativeAnimInstance::HandleGrenadeThrow()
{
	// Godot play_grenade_throw: prone / crouch / run / walk by the stance and speed; the grenade leaves the hand at 70 %
	// of the clip (Godot get_grenade_throw_duration), so the throw duration follows the clip.
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (!Operative)
	{
		return;
	}
	UAnimSequenceBase* Clip = Operative->GetStance() == EOperativeStance::Prone ? GrenadeThrowProneAnimation
		: Operative->GetStance() == EOperativeStance::Crouching ? GrenadeThrowCrouchAnimation
		: (Operative->IsSprinting() || Operative->GetVelocity().Size2D() > 300.f) ? GrenadeThrowRunAnimation : GrenadeThrowWalkAnimation;
	if (!Clip)
	{
		return;
	}
	Operative->GrenadeThrowDuration = Clip->GetPlayLength();
	PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, 0.1f, 0.2f);
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
	UpdateCoverLayer(*Operative);
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

// --- Tactical cover baseline (Sprint 12) ------------------------------------------------------------------------

UAnimSequenceBase* UOperativeAnimInstance::PickCoverClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) const
{
	const int32 Index = CoverFacing == ECoverFacing::Left ? 0 : 1;
	if (Clips.IsValidIndex(Index) && Clips[Index])
	{
		return Clips[Index];
	}
	// The other side's clip stands in (mirrored by the user later).
	return Clips.IsValidIndex(1 - Index) ? Clips[1 - Index].Get() : nullptr;
}

void UOperativeAnimInstance::UpdateCoverLayer(const AOperativeCharacter& Operative)
{
	const bool bEntered = bInCover && !bWasInCover;
	const bool bLeft = !bInCover && bWasInCover;
	bWasInCover = bInCover;
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
		return;
	}
	if (!bInCover || bIsProne)
	{
		return;
	}
	// Cover_Enter once, then the loop.
	if (bEntered)
	{
		if (UAnimSequenceBase* Enter = PickCoverClip(bIsCrouching ? CoverCrouchEnter : CoverStandEnter))
		{
			StopSlotAnimation(0.15f, FullBodySlot);
			CoverOneShotMontage = PlaySlotAnimationAsDynamicMontage(Enter, FullBodySlot, 0.15f, 0.2f);
			CoverClipsPlayed += CoverOneShotMontage.IsValid() ? 1 : 0;
			CoverLoopMontage.Reset();
			CoverLoopClip.Reset();
		}
	}
	if (UAnimMontage* OneShot = CoverOneShotMontage.Get(); OneShot && Montage_IsPlaying(OneShot))
	{
		return; // enter / fire clip in progress
	}
	if (IsPlayingStanceTransition())
	{
		return; // stand <-> crouch at the wall: the stance clip plays, the loop follows
	}
	// The loop wanted now: shimmy towards / away from the corner, else the idle.
	UAnimSequenceBase* Wanted = nullptr;
	float PlayRate = 1.f;
	if (bShimmying && Speed > 5.f)
	{
		// Forward = towards the corner he works: right facing moves right (+1), left facing moves left (-1).
		const bool bTowardsCorner = (CoverFacing == ECoverFacing::Right) == (ShimmyDirection > 0.f);
		Wanted = PickCoverClip(bIsCrouching ? (bTowardsCorner ? CoverCrouchMoveForward : CoverCrouchMoveBackward)
			: (bTowardsCorner ? CoverStandMoveForward : CoverStandMoveBackward));
		PlayRate = FMath::Clamp(Speed / FMath::Max(CoverShimmyClipSpeed, 1.f), 0.5f, 2.f);
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
		return;
	}
	if (Loop && Montage_IsPlaying(Loop))
	{
		Montage_Stop(0.2f, Loop);
	}
	CoverLoopClip = Wanted;
	CoverLoopMontage = PlaySlotAnimationAsDynamicMontage(Wanted, FullBodySlot, 0.2f, 0.2f, PlayRate, /*LoopCount*/ 1000);
	CoverClipsPlayed += CoverLoopMontage.IsValid() ? 1 : 0;
}
