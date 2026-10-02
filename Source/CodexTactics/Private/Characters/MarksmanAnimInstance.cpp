#include "Characters/MarksmanAnimInstance.h"

#include "Animation/AnimSequenceBase.h"
#include "Characters/MarksmanEnemyCharacter.h"

void UMarksmanAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	StandAttackAnimations = AttackAnimations;
	StandHitAnimations = HitAnimations;
	StandDeathAnimations = DeathAnimations;
}

UAnimSequenceBase* UMarksmanAnimInstance::PickTransition(EOperativeStance From, EOperativeStance To) const
{
	if (To == EOperativeStance::Prone)
	{
		return From == EOperativeStance::Crouching ? CrouchToProneAnimation.Get() : StandToProneAnimation.Get();
	}
	if (From == EOperativeStance::Prone)
	{
		return To == EOperativeStance::Crouching ? ProneToCrouchAnimation.Get() : ProneToStandAnimation.Get();
	}
	return nullptr;
}

void UMarksmanAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(TryGetPawnOwner()))
	{
		const EOperativeStance Stance = Marksman->GetStance();
		bIsCrouched = Stance == EOperativeStance::Crouching;
		bIsProne = Stance == EOperativeStance::Prone;
		bIsAiming = Marksman->IsAimingAtTarget();
		// Lying: the prone clips (the full-body standing hit / death would stand the body up).
		AttackAnimations = bIsProne && !ProneFireAnimations.IsEmpty() ? ProneFireAnimations : StandAttackAnimations;
		HitAnimations = bIsProne ? ProneHitAnimations : StandHitAnimations;
		DeathAnimations = bIsProne && !ProneDeathAnimations.IsEmpty() ? ProneDeathAnimations : StandDeathAnimations;
		if (bStanceKnown && Stance != LastStance && !Marksman->IsDying())
		{
			if (UAnimSequenceBase* Clip = PickTransition(LastStance, Stance))
			{
				LastTransition = Clip;
				PlayOneShot(Clip, StanceTransitionPlayRate);
			}
		}
		LastStance = Stance;
		bStanceKnown = true;
	}
	Super::NativeUpdateAnimation(DeltaSeconds);
}

float UMarksmanAnimInstance::NotifyAttack()
{
	// Crouched: the upper body fires, the legs stay crouched.
	if (bIsCrouched && CrouchFireAnimation && !bIsDead)
	{
		AttackVariant = 0;
		bIsAttacking = true;
		PlaySlotAnimationAsDynamicMontage(CrouchFireAnimation, UpperBodySlot, 0.1f, 0.2f, FirePlayRate);
		return CrouchFireAnimation->GetPlayLength() / FirePlayRate;
	}
	return Super::NotifyAttack();
}
