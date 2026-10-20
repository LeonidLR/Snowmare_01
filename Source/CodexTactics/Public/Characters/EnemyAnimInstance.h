#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "EnemyAnimInstance.generated.h"

class UAnimSequenceBase;
class UAnimMontage;

/** One locomotion clip variant with its own ground speed (in-place clips: the speed the feet show at rate 1). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemyLocomotionVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy Clips")
	TObjectPtr<UAnimSequenceBase> Clip;

	/** Ground speed of Clip at rate 1, cm/s (speed-matched play rate); <= 0 = the ABP's WalkClipSpeed / RunClipSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy Clips")
	float GroundSpeed = 0.f;
};

/**
 * Enemy animation base class. The enemy's Animation Blueprint is a child of this class; its graph
 * (built by Scripts/Editor/setup_enemy_animation.py, editable) is idle / walk / run sequence players switched by
 * bIsMoving / bIsRunning, then a full-body Slot for the one-shots this class plays itself:
 * attacks (random of AttackAnimations), hit reactions, the pounce (JumpAttackAnimation) and the death (held on its last
 * frame). The clips and their speeds are set on the AnimBP's class defaults, per enemy (Godot EnemyAnimationConfig).
 * Blueprint graphs can also read Speed, Direction, bIsAttacking + AttackVariant, bIsHit + HitVariant, bIsDead, ...
 * Extras (game designer 2026-10-20, Military Zombie; UE-only, empty = off): per-instance walk / run / idle variants, idle breaks
 * (fidget idles, a calm activity), the alert roar and the spawn entrance (EnemyAnimRules; docs/systems/animation-setup.md).
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

	/**
	 * The enemy noticed the squad (patrol / guard break, AEnemyCharacter::BreakPatrol): plays one of AlertAnimations (the Military
	 * Zombie's rage roar) on OneShotSlot. @return seconds the enemy stands still for it (0 without a clip / when it cannot play).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	float NotifyAlert();

	/**
	 * Spawn / wake-up entrance (AEnemyCharacter::PlaySpawnEntrance: wave spawns, placed ones with bPlayEntranceWhenPlaced): plays
	 * one of EntranceAnimations from EntranceStartOffset. @return seconds the enemy stands still for it (0 without a clip).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy Animation")
	float NotifyEntrance();

	/** The alert roar / entrance montage is still playing (not interrupted by a hit, a knockdown, death or the turn-based fight). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy Animation")
	bool IsPerformancePlaying() const;

	/** Stops the alert roar / entrance montage (its stand-still lock ran out: the clip may be longer than the lock cap). */
	void StopPerformance(float BlendOutSeconds = 0.3f);

	/** The idle-break clip playing now (a fidget idle or the activity), nullptr when none (smokes, Blueprints). */
	UAnimSequenceBase* GetIdleBreakClip() const;

	/** Per-instance variants picked from AEnemyCharacter::GetVariantSeed (-1 = no variant list: WalkAnimation / RunAnimation). */
	int32 GetWalkVariantIndex() const { return WalkVariantIndex; }
	int32 GetRunVariantIndex() const { return RunVariantIndex; }

	/**
	 * Picks the variants again now (AEnemyCharacter::BeginPlay: its seed is final only then - a deferred spawn registers the mesh,
	 * and updates the anim once, before VariantSeedOverride / the spawn point are set).
	 */
	void RepickVariants(const class AEnemyCharacter& Enemy) { PickVariants(Enemy); }

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

	// --- Extras (game designer 2026-10-20, Military Zombie; UE-only). All empty = the behaviour before (every other enemy). ---

	/**
	 * Walk clip variants: one is picked per enemy at its first update (deterministic per spawn, AEnemyCharacter::GetVariantSeed)
	 * and replaces WalkAnimation / WalkClipSpeed for it. Empty: WalkAnimation.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Variants")
	TArray<FEnemyLocomotionVariant> WalkAnimationVariants;

	/** Run clip variants (the chase), picked like WalkAnimationVariants. Empty: RunAnimation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Variants")
	TArray<FEnemyLocomotionVariant> RunAnimationVariants;

	/** Pick the idle loop from IdleAnimations by the spawn seed too (off: random per enemy, Godot enemy_base.gd). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Variants")
	bool bSeededIdleVariant = false;

	/**
	 * Idle variety: while it stands calm (EnemyAnimRules::IsCalmIdle) it plays one of these as a one-shot every IdleBreakInterval
	 * seconds (never the idle loop it plays now) and then returns to its loop. Moving / an attack / a hit / the turn-based fight
	 * cut it. Empty: no fidgets.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle")
	TArray<TObjectPtr<UAnimSequenceBase>> IdleBreakAnimations;

	/**
	 * Calm idle activity (the Military Zombie's devour / feeding): instead of a fidget with IdleActivityChance, only while it does
	 * not hunt the squad (on patrol / guard duty, waiting); the game has no lasting corpses to feed on, so it is a random idle
	 * activity. Empty: none.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle")
	TArray<TObjectPtr<UAnimSequenceBase>> IdleActivityAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle", meta = (ClampMin = "0", ClampMax = "1"))
	float IdleActivityChance = 0.35f;

	/** Seconds of calm idle before the next break, min / max (random per break). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle")
	FVector2D IdleBreakInterval = FVector2D(6.f, 12.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle", meta = (ClampMin = "0.1"))
	float IdleBreakPlayRate = 1.f;

	/** Cross-fade into / out of an idle break, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Idle", meta = (ClampMin = "0"))
	float IdleBreakBlendSeconds = 0.35f;

	/** Alert roar when it notices the squad (patrol / guard break), before it charges (Military Zombie anim_rage). Empty: none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance")
	TArray<TObjectPtr<UAnimSequenceBase>> AlertAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance", meta = (ClampMin = "0.1"))
	float AlertPlayRate = 1.f;

	/** Longest stand-still for the roar, s (0 = the whole clip); the clip is cut there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance", meta = (ClampMin = "0"))
	float AlertMaxSeconds = 0.f;

	/** Spawn / wake-up entrance (Military Zombie anim_transformation). Empty: none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance")
	TArray<TObjectPtr<UAnimSequenceBase>> EntranceAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance", meta = (ClampMin = "0.1"))
	float EntrancePlayRate = 1.f;

	/** Seconds skipped at the start of the entrance clip (a long clip: start at its interesting part). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance", meta = (ClampMin = "0"))
	float EntranceStartOffset = 0.f;

	/** Longest stand-still for the entrance, s (0 = the rest of the clip); the clip is cut there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy Clips|Performance", meta = (ClampMin = "0"))
	float EntranceMaxSeconds = 0.f;

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

	/** Roaring at the squad / playing its spawn entrance (the enemy stands still meanwhile). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsPerforming = false;

	/** Playing an idle break (a fidget idle or the calm activity). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Enemy State")
	bool bIsIdleBreak = false;

protected:
	/** Plays Clip on OneShotSlot; bHold keeps the last frame (death). Returns the montage (nullptr without a clip). */
	UAnimMontage* PlayOneShot(UAnimSequenceBase* Clip, float PlayRate, float StartTime = 0.f, bool bHold = false);

private:
	/** Picks the per-instance walk / run / idle variants once the owner is known (its spawn seed). */
	void PickVariants(const class AEnemyCharacter& Enemy);
	/** Idle breaks: counts the calm idle, plays a fidget / the activity when due, cuts it when it must move. */
	void TickIdleBreaks(const class AEnemyCharacter& Enemy, float DeltaSeconds, bool bTurnBased);
	bool IsMontagePlaying(const UAnimMontage* Montage) const;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> IdleBreakMontage;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> IdleBreakClip;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PerformanceMontage;

	bool bVariantsPicked = false;
	int32 WalkVariantIndex = -1;
	int32 RunVariantIndex = -1;
	/** Deterministic stream of this enemy's idle-break choices (seeded with its variant seed). */
	FRandomStream IdleBreakRandom;
	float CalmIdleSeconds = 0.f;
	float NextIdleBreakSeconds = 0.f;

	float AttackTimer = 0.f;
	float HitTimer = 0.f;
	bool bDeathPlayed = false;
	bool bWasTurnBased = false;
};
