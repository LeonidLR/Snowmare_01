#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "EnemyAnimInstance.generated.h"

class UAnimSequenceBase;

/**
 * Enemy animation base class. The enemy's Animation Blueprint is a child of this class; its graph
 * (built by Scripts/Editor/setup_enemy_animation.py, editable) is idle / walk / run sequence players switched by
 * bIsMoving / bIsRunning, then a full-body Slot for the one-shots this class plays itself:
 * attacks (random of AttackAnimations), hit reactions, the pounce (JumpAttackAnimation) and the death (held on its last
 * frame). The clips and their speeds are set on the AnimBP's class defaults, per enemy (Godot EnemyAnimationConfig).
 * Blueprint graphs can also read Speed, Direction, bIsAttacking + AttackVariant, bIsHit + HitVariant, bIsDead, ...
 * Godot reference: Scenes/movements/enemy_base.gd (_play_anim / play_hit_reaction / _die), resources/enemy_animation_config.gd.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Called by AEnemyCharacter when it bites / is hit / dies (also usable from Blueprints).
	 * @return seconds the attack clip plays (0 without one) — the enemy stands still that long (Godot is_attacking).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	virtual float NotifyAttack();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	void NotifyHit();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	void NotifyDeath();

	/** Idle clip number Index (wrapping) — Godot set_idle_variation for same-type enemies on the grid. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	void SetIdleVariation(int32 Index);

	/** True when a death clip is set (the body then stays DeathDecayDelay seconds, Godot death_decay_delay). */
	bool HasDeathAnimation() const { return !DeathAnimations.IsEmpty(); }

	// --- Clips (Godot EnemyAnimationConfig) ---

	/** One is picked at spawn (Godot idle_animations). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> IdleAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TObjectPtr<UAnimSequenceBase> WalkAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TObjectPtr<UAnimSequenceBase> RunAnimation;

	/** Above this ground speed (cm/s) the run clip plays instead of the walk clip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0"))
	float RunSpeedThreshold = 300.f;

	/** Ground speed of the walk / run clips at rate 1, cm/s (speed-matched play rate, limits foot sliding). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "1"))
	float WalkClipSpeed = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "1"))
	float RunClipSpeed = 450.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> AttackAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0.1"))
	float AttackPlayRate = 1.3f;

	/** Empty: no hit reaction (Godot hit_animation ""). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> HitAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0.1"))
	float HitPlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TArray<TObjectPtr<UAnimSequenceBase>> DeathAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0.1"))
	float DeathPlayRate = 1.f;

	/** Seconds skipped at the start of the death clip (Godot death_start_offset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0"))
	float DeathStartOffset = 0.f;

	/** Seconds the body stays after death when a death clip is set (Godot death_decay_delay). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0.5"))
	float DeathDecayDelay = 5.f;

	/** The cutter's pounce (Godot jump_attack_animation / jump_attack_speed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	TObjectPtr<UAnimSequenceBase> JumpAttackAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips", meta = (ClampMin = "0.1"))
	float JumpAttackPlayRate = 1.85f;

	/** Full-body slot of the graph the one-shots play on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips")
	FName OneShotSlot = TEXT("DefaultSlot");

	/**
	 * Slot of the upper-body layer (above the spine / neck, set up by Scripts/Editor/setup_enemy_animation.py): hit
	 * reactions while moving play there so the legs keep running (TANDEM request 1). Off = full-body hits.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	FName UpperBodySlot = TEXT("UpperBody");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	bool bUpperBodyHitReactions = false;

	/** Seconds bIsAttacking / bIsHit stay true after the event. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Animation", meta = (ClampMin = "0.05"))
	float AttackHoldTime = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Animation", meta = (ClampMin = "0.05"))
	float HitHoldTime = 0.2f;

	// --- State (updated every frame) ---

	/** The idle clip picked for this enemy (the graph's idle player reads it). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	float Speed = 0.f;

	/** Movement direction relative to the facing, degrees (-180..180). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	float Direction = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsMoving = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsRunning = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	float WalkPlayRate = 1.f;

	/** Speed smoothed for the idle / walk / run choice and the play rates (no flicker on crowd nudges). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Animation")
	float SmoothedSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	float RunPlayRate = 1.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsAttacking = false;

	/** Which attack clip, picked at random per bite. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	int32 AttackVariant = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsHit = false;

	/** Which hit-reaction clip, picked at random per hit. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	int32 HitVariant = 0;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsDead = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsJumping = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsBurning = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsFleeing = false;

protected:
	/** Plays Clip on OneShotSlot; bHold keeps the last frame (death). */
	void PlayOneShot(UAnimSequenceBase* Clip, float PlayRate, float StartTime = 0.f, bool bHold = false);

private:

	float AttackTimer = 0.f;
	float HitTimer = 0.f;
	bool bDeathPlayed = false;
	bool bWasTurnBased = false;
};
