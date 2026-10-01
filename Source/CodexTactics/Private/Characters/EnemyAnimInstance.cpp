#include "Characters/EnemyAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace
{
	/** Speed-matched walk / run play rate range. */
	constexpr float MinEnemyLocomotionRate = 0.6f;
	constexpr float MaxEnemyLocomotionRate = 1.8f;

	UAnimSequenceBase* PickClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, int32 Index)
	{
		return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
	}
}

void UEnemyAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	// Godot enemy_base.gd: a random idle clip per enemy.
	IdleAnimation = IdleAnimations.IsEmpty() ? nullptr : IdleAnimations[FMath::RandRange(0, IdleAnimations.Num() - 1)].Get();
}

void UEnemyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(TryGetPawnOwner());
	if (!Enemy)
	{
		return;
	}
	const FVector Velocity = Enemy->GetVelocity();
	Speed = Velocity.Size2D();
	Direction = Speed > 1.f ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Enemy->GetActorRotation().Yaw) : 0.f;
	// Turn-based steps move the actor directly (no velocity): walk forward over the whole path.
	const UTurnBasedCombatSubsystem* TurnBased = Enemy->GetWorld() ? Enemy->GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (const float TacticalSpeed = TurnBased ? TurnBased->GetTacticalMoveSpeed(Enemy) : -1.f; TacticalSpeed >= 0.f)
	{
		Speed = TacticalSpeed;
		Direction = 0.f;
	}
	// Godot force_idle_animation: entering the turn-based fight cuts a running attack / hit clip.
	const bool bTurnBased = TurnBased && TurnBased->IsActive();
	if (bTurnBased && !bWasTurnBased && !Enemy->IsDying())
	{
		StopSlotAnimation(0.2f, OneShotSlot);
	}
	bWasTurnBased = bTurnBased;
	// Smoothed speed with hysteresis: starts walking above 25 cm/s, stops below 8 — crowd separation nudges and the
	// braking no longer flicker the idle / walk clips (user report 2026-10-01).
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 8.f);
	bIsMoving = bIsMoving ? SmoothedSpeed > 8.f : SmoothedSpeed > 25.f;
	bIsRunning = (RunAnimation && SmoothedSpeed > RunSpeedThreshold * (bIsRunning ? 0.85f : 1.f)) || !WalkAnimation;
	WalkPlayRate = bIsMoving ? FMath::Clamp(SmoothedSpeed / WalkClipSpeed, MinEnemyLocomotionRate, MaxEnemyLocomotionRate) : 1.f;
	RunPlayRate = bIsMoving ? FMath::Clamp(SmoothedSpeed / RunClipSpeed, MinEnemyLocomotionRate, MaxEnemyLocomotionRate) : 1.f;

	const UHealthComponent* Health = Enemy->GetHealthComponent();
	bIsDead = Enemy->IsDying() || (Health && !Health->IsAlive());
	const bool bWasJumping = bIsJumping;
	bIsJumping = Enemy->IsJumpAttacking();
	bIsBurning = Health && Health->HasStatusEffect(EStatusEffect::Burning);
	bIsFleeing = Enemy->IsFleeingFire();
	if (bIsJumping && !bWasJumping && !bIsDead)
	{
		PlayOneShot(JumpAttackAnimation, JumpAttackPlayRate);
	}
	if (bIsDead && !bDeathPlayed)
	{
		NotifyDeath();
	}

	AttackTimer = FMath::Max(0.f, AttackTimer - DeltaSeconds);
	HitTimer = FMath::Max(0.f, HitTimer - DeltaSeconds);
	bIsAttacking = AttackTimer > 0.f && !bIsDead;
	bIsHit = HitTimer > 0.f && !bIsDead;
}

float UEnemyAnimInstance::NotifyAttack()
{
	AttackVariant = AttackAnimations.IsEmpty() ? FMath::RandRange(0, 2) : FMath::RandRange(0, AttackAnimations.Num() - 1);
	AttackTimer = AttackHoldTime;
	bIsAttacking = true;
	UAnimSequenceBase* Clip = PickClip(AttackAnimations, AttackVariant);
	if (bIsDead || bIsJumping || !Clip)
	{
		return 0.f;
	}
	PlayOneShot(Clip, AttackPlayRate);
	return Clip->GetPlayLength() / AttackPlayRate;
}

void UEnemyAnimInstance::NotifyHit()
{
	HitVariant = HitAnimations.IsEmpty() ? FMath::RandRange(0, 2) : FMath::RandRange(0, HitAnimations.Num() - 1);
	HitTimer = HitHoldTime;
	bIsHit = true;
	if (!bIsDead && !bIsJumping)
	{
		// Moving: the upper body flinches, the legs keep running (no sliding full-body hit; TANDEM request 1).
		UAnimSequenceBase* Clip = PickClip(HitAnimations, HitVariant);
		if (bUpperBodyHitReactions && bIsMoving && Clip)
		{
			PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, 0.1f, 0.2f, HitPlayRate);
		}
		else
		{
			PlayOneShot(Clip, HitPlayRate);
		}
	}
}

void UEnemyAnimInstance::SetIdleVariation(int32 Index)
{
	if (!IdleAnimations.IsEmpty())
	{
		IdleAnimation = IdleAnimations[FMath::Abs(Index) % IdleAnimations.Num()].Get();
	}
}

void UEnemyAnimInstance::NotifyDeath()
{
	if (bDeathPlayed)
	{
		return;
	}
	bDeathPlayed = true;
	bIsDead = true;
	UAnimSequenceBase* Clip = DeathAnimations.IsEmpty() ? nullptr : DeathAnimations[FMath::RandRange(0, DeathAnimations.Num() - 1)].Get();
	if (Clip)
	{
		PlayOneShot(Clip, DeathPlayRate, FMath::Min(DeathStartOffset, Clip->GetPlayLength() * 0.9f), true);
	}
}

void UEnemyAnimInstance::PlayOneShot(UAnimSequenceBase* Clip, float PlayRate, float StartTime, bool bHold)
{
	if (!Clip)
	{
		return;
	}
	UAnimMontage* Montage = PlaySlotAnimationAsDynamicMontage(Clip, OneShotSlot, 0.15f, bHold ? 0.f : 0.2f, PlayRate, 1, -1.f, StartTime);
	// Stay on the last frame: the running instance copied bEnableAutoBlendOut from the montage when it started, so the
	// flag is cleared on the instance (as the engine does for Sequencer montages); on the asset it had no effect and the
	// body got back up into the idle after the clip.
	if (FAnimMontageInstance* Instance = Montage && bHold ? GetActiveInstanceForMontage(Montage) : nullptr)
	{
		Instance->bEnableAutoBlendOut = false;
	}
}
