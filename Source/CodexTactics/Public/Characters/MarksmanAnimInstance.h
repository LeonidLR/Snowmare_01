#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/OperativeMovementRules.h"
#include "MarksmanAnimInstance.generated.h"

/**
 * Animation of the Marksman (AMarksmanEnemyCharacter; UE-only archetype, TANDEM request 3) on the user's
 * Biochemical_Monster_1 model. Its graph (UOperativeAnimGraphLibrary::BuildMarksmanLocomotionGraph, made by
 * Scripts/Editor/setup_marksman_animation.py) crossfades the still pose by stance and aim — bIsCrouched / bIsProne /
 * bIsAiming pick IdleAnimation / StandAimAnimation, CrouchIdle / CrouchAim, ProneIdle / ProneAim — and walks / runs
 * standing. Stance changes play the transition clips on the full-body slot; the shot is the stance's fire clip (standing:
 * AttackAnimations full body, crouched: CrouchFireAnimation on the upper-body slot, prone: ProneFireAnimations), hits and
 * the death switch to the prone clips while lying. Clips missing in the monster pack come from RifleAnims (crouch) and
 * Crawl_MocapAnimPack (prone) — the same UE4 mannequin rig, made compatible skeletons.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UMarksmanAnimInstance : public UEnemyAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual float NotifyAttack() override;

	// --- Still poses per stance (the graph reads them) ---

	/** Standing, rifle up during the telegraphed aim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> StandAimAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> CrouchIdleAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> CrouchAimAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> ProneIdleAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> ProneAimAnimation;

	// --- Shots, hits, death per stance (standing = the base AttackAnimations / HitAnimations / DeathAnimations) ---

	/** Crouched shot, played on the upper-body slot so the legs stay crouched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> CrouchFireAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> ProneFireAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> ProneHitAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> ProneDeathAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips", meta = (ClampMin = "0.1"))
	float FirePlayRate = 1.f;

	// --- Stance transitions (full-body slot; empty = the graph's crossfade only) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> StandToProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> ProneToStandAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> CrouchToProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips")
	TObjectPtr<UAnimSequenceBase> ProneToCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman Clips", meta = (ClampMin = "0.1"))
	float StanceTransitionPlayRate = 1.5f;

	// --- State ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Marksman State")
	bool bIsCrouched = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Marksman State")
	bool bIsProne = false;

	/** The telegraphed aim before a shot (AMarksmanEnemyCharacter::IsAimingAtTarget). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Marksman State")
	bool bIsAiming = false;

	/** The last stance-transition clip started (smokes). */
	UAnimSequenceBase* GetLastTransition() const { return LastTransition; }

private:
	/** The transition clip from one stance to another (nullptr = crossfade only). */
	UAnimSequenceBase* PickTransition(EOperativeStance From, EOperativeStance To) const;

	EOperativeStance LastStance = EOperativeStance::Standing;
	bool bStanceKnown = false;
	TObjectPtr<UAnimSequenceBase> LastTransition;
	TArray<TObjectPtr<UAnimSequenceBase>> StandAttackAnimations;
	TArray<TObjectPtr<UAnimSequenceBase>> StandHitAnimations;
	TArray<TObjectPtr<UAnimSequenceBase>> StandDeathAnimations;
};
