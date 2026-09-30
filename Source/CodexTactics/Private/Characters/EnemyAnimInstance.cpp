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
	bIsMoving = Speed > 5.f;
	bIsRunning = (RunAnimation && Speed > RunSpeedThreshold) || !WalkAnimation;
	WalkPlayRate = bIsMoving ? FMath::Clamp(Speed / WalkClipSpeed, MinEnemyLocomotionRate, MaxEnemyLocomotionRate) : 1.f;
	RunPlayRate = bIsMoving ? FMath::Clamp(Speed / RunClipSpeed, MinEnemyLocomotionRate, MaxEnemyLocomotionRate) : 1.f;

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
		PlayOneShot(PickClip(HitAnimations, HitVariant), HitPlayRate);
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
	if (Montage && bHold)
	{
		Montage->bEnableAutoBlendOut = false; // stay on the last frame
	}
}
