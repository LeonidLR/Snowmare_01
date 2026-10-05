#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/RifleLocomotionRules.h"
#include "Survival/ColdRules.h"
#include "OperativeAnimInstance.generated.h"

class AActor;
class AOperativeCharacter;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;

/** Locomotion clips of one operative (baseline set; the AnimBP may use its own). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeAnimSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing")
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing")
	TObjectPtr<UAnimSequence> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Standing")
	TObjectPtr<UAnimSequence> Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crouching")
	TObjectPtr<UAnimSequence> CrouchIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crouching")
	TObjectPtr<UAnimSequence> CrouchWalk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prone")
	TObjectPtr<UAnimSequence> ProneIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prone")
	TObjectPtr<UAnimSequence> ProneCrawl;

	/** Ground speed at which each moving clip plays at rate 1, cm/s (foot sliding tuning). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "1"))
	float WalkClipSpeed = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "1"))
	float RunClipSpeed = 480.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "1"))
	float CrouchWalkClipSpeed = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Play Rate", meta = (ClampMin = "1"))
	float CrawlClipSpeed = 55.f;
};

/** Clip slots blended by the native locomotion. */
enum class EOperativeClip : uint8
{
	Idle, Walk, Run, CrouchIdle, CrouchWalk, ProneIdle, ProneCrawl, Count
};

/** Game-thread snapshot copied to the proxy for evaluation. */
struct FOperativeClipState
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	float Time = 0.f;
	float Weight = 0.f;
};

/** Proxy that blends the clip snapshot when the instance has no graph (or native locomotion is forced). */
struct CODEXTACTICS_API FOperativeAnimInstanceProxy : public FAnimInstanceProxy
{
	FOperativeAnimInstanceProxy() = default;
	explicit FOperativeAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	TStaticArray<FOperativeClipState, static_cast<int32>(EOperativeClip::Count)> Clips;
	bool bUseNative = true;
};

/**
 * Operative animation base class. Exposes the gameplay state an AnimBP needs (speed, stance, sprint, reload,
 * cold, death) and, until the AnimBP graph is built, blends the baseline locomotion clips natively:
 * idle / walk / run by speed, crouch idle / walk, prone idle / crawl, with stance crossfades.
 * ABP_Operative's graph (built by Scripts/Editor/setup_operative_animation.py) reads Direction, the blend-space speed
 * axes (StandBlendSpeed / SlowBlendSpeed) with their play rates, bIsAiming, bIsCrouching and bIsProne; shots and
 * reloads play FireMontage / ReloadAnimation on UpperBodySlot.
 * Parent class of /Game/Characters/Operatives/ABP_Operative.
 * Godot reference: Scripts/components/locomotion_controller.gd (StandLocomotion / CrouchLocomotion / ProneIdle).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UOperativeAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** Baseline clips. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	FOperativeAnimSet Animations;

	/**
	 * Blend the baseline clips in C++. Untick once the AnimBP graph is built so the graph drives the pose.
	 * Instances without a graph always use the native blend.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	bool bUseNativeLocomotion = true;

	/** Stance crossfade time, s (Godot stance transitions ~0.25 s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0.01"))
	float StanceBlendTime = 0.25f;

	/** How fast the idle / walk / run weights follow the speed, 1/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0.1"))
	float LocomotionBlendSpeed = 10.f;

	/** Top speed axis of the standing blend space (run samples), cm/s; faster movement raises StandPlayRate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "1"))
	float StandBlendSpaceMaxSpeed = 480.f;

	/** Top speed axis of the crouch / aim blend spaces (walk samples), cm/s; faster movement raises SlowPlayRate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "1"))
	float SlowBlendSpaceMaxSpeed = 120.f;

	/** Speed of the walk samples of the standing / crouch blend spaces (RifleAnims: 120 cm/s); slower walking slows the clip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "1"))
	float WalkSampleSpeed = 120.f;

	/** The legs start walking above / stop below these smoothed speeds, cm/s (dead zone against creeping / restarts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0"))
	float LocomotionStartSpeed = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0"))
	float LocomotionStopSpeed = 10.f;

	/** Slowest play rate of a slow walk. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0.05"))
	float MinWalkPlayRate = 0.3f;

	/** Top speed axis of the prone blend spaces (the crawl clips' own speed), cm/s; faster crawling raises PronePlayRate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "1"))
	float ProneBlendSpaceMaxSpeed = 21.f;

	/** Seconds the rifle stays raised (bIsAiming) after a shot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0"))
	float AimHoldAfterShot = 1.5f;

	/** Played on UpperBodySlot for every shot (FireAimMontage while aiming, when set). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimMontage> FireAimMontage;

	/** Full-body shot while prone (Godot ProneFire / fire_prone_animation); nothing plays prone when unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimSequenceBase> FireProneAnimation;

	/** Full-body reload while prone (Godot ProneReload); the standing reload is not layered over a prone body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimSequenceBase> ReloadProneAnimation;

	// --- Stance transitions (Godot ProneStart / ProneEnd between the locomotion states), full body. Empty = the
	// graph's stance crossfade only. ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> StandToProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> ProneToStandAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> CrouchToProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> ProneToCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> StandToCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> CrouchToStandAnimation;

	/** Blend in / out of a transition clip, s (Godot xfade 0.15). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions", meta = (ClampMin = "0"))
	float StanceTransitionBlendTime = 0.15f;

	/** Play-rate multiplier of the transition clips (1 = the clip's own speed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions", meta = (ClampMin = "0.1"))
	float StanceTransitionPlayRate = 1.f;

	/** True while a stance transition clip plays. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Animation")
	bool IsPlayingStanceTransition() const;

	/** Seconds left of the stance clip playing (0 when none). */
	float GetStanceTransitionTimeLeft() const;

	/** Played on UpperBodySlot when a reload starts, stretched to the weapon's reload time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimSequenceBase> ReloadAnimation;

	/** Slot of the graph's upper-body layer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	FName UpperBodySlot = TEXT("DefaultSlot");

	/** Full-body slot (death) of the graph. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	FName FullBodySlot = TEXT("FullBody");

	// --- One-shots (Godot locomotion_controller.gd play_hit_reaction / play_grenade_throw / play_death, character
	// animation config hit_* / grenade_throw_* / death_*). Empty = nothing plays (no clips in the pack yet). ---

	/** Hit reaction per stance (Godot hit_stand / hit_crouch / hit_prone) and with the pistol in hands; upper body,
	 * the prone one full body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> HitStandAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> HitCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> HitProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> PistolHitAnimation;

	/** Upper-body grenade throw: walking, running, crouched, prone (Godot grenade_throw_*); its length sets the release time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowWalkAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowRunAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowProneAnimation;

	/** Full-body death: one of the standing variations at random, or the crouched / prone one (held on its last frame). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TArray<TObjectPtr<UAnimSequenceBase>> DeathStandAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> DeathCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> DeathProneAnimation;

	/** Godot death_start_offset (tuned for the standing variations; crouched / prone deaths start at 0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float DeathStartOffset = 0.f;

	/** Upper-body "working device" (set-up / repair / defusal; Godot action_working_device). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> WorkingDeviceAnimation;

	/** Plays the working-device clip for Seconds (Godot play_action_animation("working_device", 0.8)). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Animation")
	void PlayWorkingDevice(float Seconds);

	/** Cold presentation layer (Godot character_animation_config.gd cold_*): levels start at these cold values. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cold Animation")
	TArray<float> ColdThresholds = { 25.f, 40.f, 70.f, 90.f };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cold Animation", meta = (ClampMin = "0"))
	float ColdHysteresis = 5.f;

	/** Idle / walk clip per cold level 1..4 (Godot cold_idle, cold_idle, cold_idle_slow, cold_crawl_idle); a missing one uses the level below. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cold Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> ColdIdleClips = { nullptr, nullptr, nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cold Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> ColdWalkClips = { nullptr, nullptr, nullptr, nullptr };

	/** Speed at which the cold walk clip fully replaces the cold idle one, cm/s (Godot speed / 0.35 m/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cold Animation", meta = (ClampMin = "1"))
	float ColdWalkFullSpeed = 35.f;

	// --- State for AnimBP graphs (updated every frame) ---

	/** Cold level shown, 0..4 (0 = none). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	int32 ColdVisualTier = 0;

	/** Weight of the cold layer over the locomotion (0 in combat / actions, fades 0.3 s otherwise). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float ColdVisualWeight = 0.f;

	/** Cold idle -> walk blend by speed (0..1). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float ColdMoveBlend = 0.f;

	/** The current level's clips (the graph's cold players read them). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	TObjectPtr<UAnimSequenceBase> ColdIdleAnimation;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	TObjectPtr<UAnimSequenceBase> ColdWalkAnimation;

	/** Speed on the standing blend space's axis (clamped to StandBlendSpaceMaxSpeed). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float StandBlendSpeed = 0.f;

	/** Play rate of the standing blend space: > 1 when moving faster than its top samples. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float StandPlayRate = 1.f;

	/** Speed on the crouch / aim blend spaces' axis (clamped to SlowBlendSpaceMaxSpeed). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float SlowBlendSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float SlowPlayRate = 1.f;

	/** Prone blend-space speed axis (capped at ProneBlendSpaceMaxSpeed) and the play rate for faster crawling. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float ProneBlendSpeed = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float PronePlayRate = 1.f;

	/** Vaulting over an obstacle (Godot "Vault" state). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsVaulting = false;

	/** Rifle raised: the turn-based attack mode of this operative, or a recent shot. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsAiming = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float Speed = 0.f;

	/** Movement direction relative to facing, degrees (-180..180). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	float Direction = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsMoving = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsSprinting = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	EOperativeStance Stance = EOperativeStance::Standing;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsCrouching = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsProne = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsReloading = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsDead = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	EColdTier ColdTier = EColdTier::Normal;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsFrostbitten = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsWeaponFrozen = false;

	/** Current native blend weight of a clip (debug / tests). */
	float GetClipWeight(EOperativeClip Clip) const { return ClipWeights[static_cast<int32>(Clip)]; }

	// --- Rifle_2 locomotion (ABP_Operative_Rifle2, Scripts/Editor/setup_operative_rifle2_animation.py; RifleLocomotionRules) ---

	/** Runs the Idle / Turn / Start / Walk / Stop machine below (set on ABP_Operative_Rifle2's defaults). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion")
	bool bUseRifle2Locomotion = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion")
	TObjectPtr<UAnimSequence> Rifle2IdleLoop;

	/** Idle breaks played now and then (M_Neutral_Stand_Idle_Break_v01..06). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion")
	TArray<TObjectPtr<UAnimSequence>> Rifle2IdleBreaks;

	/** Stand turns 45 / 90 / 135 / 180 to the left and to the right (4 each). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", EditFixedSize)
	TArray<TObjectPtr<UAnimSequence>> Rifle2TurnLeft;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", EditFixedSize)
	TArray<TObjectPtr<UAnimSequence>> Rifle2TurnRight;

	/** Walk starts / stops: 16 each, sector * 2 + foot (sectors F, FR, RR, BR, B, BL, LL, FL; foot 0 left, 1 right). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", EditFixedSize)
	TArray<TObjectPtr<UAnimSequence>> Rifle2Starts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", EditFixedSize)
	TArray<TObjectPtr<UAnimSequence>> Rifle2Stops;

	/** Ground speed of the walk loops (the blend space's top row), cm/s; faster walking speeds the loop up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", meta = (ClampMin = "1"))
	float Rifle2WalkClipSpeed = 150.f;

	/** One walk cycle (two steps), s: the stop clip's foot follows the half cycle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", meta = (ClampMin = "0.1"))
	float Rifle2WalkCycleSeconds = 1.1f;

	/** An idle break every this many seconds (random in the range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", meta = (ClampMin = "1"))
	float Rifle2IdleBreakMinSeconds = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Rifle2 Locomotion", meta = (ClampMin = "1"))
	float Rifle2IdleBreakMaxSeconds = 16.f;

	/** The machine's state as flags for the AnimGraph state machine transitions (exactly one is true). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoIdle = true;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoIdleBreak = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoTurn = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoStart = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoWalk = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	bool bLocoStop = false;

	/** The clips the state players read (chosen on entering the state). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	TObjectPtr<UAnimSequence> LocoIdleClip;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	TObjectPtr<UAnimSequence> LocoBreakClip;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	TObjectPtr<UAnimSequence> LocoTurnClip;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	TObjectPtr<UAnimSequence> LocoStartClip;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	TObjectPtr<UAnimSequence> LocoStopClip;

	/** Mesh yaw against the actor's, degrees (Rotate Root Bone): the body keeps facing while the actor turns, a turn clip catches up. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	float RootYawOffset = 0.f;

	/** Walk blend space speed axis (capped at Rifle2WalkClipSpeed) and play rate above it. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	float Rifle2BlendSpeed = 0.f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Rifle2 State")
	float Rifle2PlayRate = 1.f;

	ERifleLocoState GetRifleLocoState() const { return LocoState; }
	/** Feeds one frame of the Rifle_2 machine (also used by tests / smokes with a stand-in intent). */
	void UpdateRifle2Locomotion(float DeltaSeconds, bool bMoveIntent, float InSpeed, float InDirection, float ActorYaw);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FOperativeAnimInstanceProxy;

	void UpdateState();
	void UpdateUpperBody(float DeltaSeconds);
	void UpdateColdLayer(float DeltaSeconds);
	void HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit);
	void HandleGrenadeThrow();
	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth, float Delta);
	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);
	void UpdateNativeBlend(float DeltaSeconds);
	void UpdateStanceTransition();
	UAnimSequence* GetClip(EOperativeClip Clip) const;

	/** Stance weights (standing, crouching, prone), crossfaded. */
	FVector3f StanceWeights = FVector3f(1.f, 0.f, 0.f);
	/** Idle->moving blend inside each stance: X standing (0 idle, 1 walk, 2 run), Y crouch, Z prone. */
	FVector3f MoveBlend = FVector3f::ZeroVector;
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipWeights{InPlace, 0.f};
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipTimes{InPlace, 0.f};

	TWeakObjectPtr<AOperativeCharacter> BoundOperative;
	FDelegateHandle FiredHandle;
	FDelegateHandle GrenadeHandle;
	bool bDeathPlayed = false;
	float AimTimer = 0.f;
	bool bWasReloading = false;
	/** Stance seen last update (transitions start on a change); unset until the first update. */
	TOptional<EOperativeStance> PreviousStance;
	TWeakObjectPtr<UAnimMontage> StanceTransitionMontage;
	float StateDeltaSeconds = 0.f;
	/** Smoothed speed and moving state driving the blend-space axes. */
	float LocomotionSpeed = 0.f;
	bool bLocomotionMoving = false;

	// Rifle_2 machine.
	void EnterRifleLocoState(ERifleLocoState NewState, UAnimSequence* Clip);
	ERifleLocoState LocoState = ERifleLocoState::Idle;
	float LocoStateTime = 0.f;
	float LocoClipLength = 0.f;
	float LocoWalkSeconds = 0.f;
	float LocoIdleSeconds = 0.f;
	float LocoNextBreak = 10.f;
	float LocoLastActorYaw = 0.f;
	bool bLocoHasYaw = false;
	/** Turn in progress: degrees and direction it rotates the mesh. */
	float LocoTurnDegrees = 0.f;
	bool bLocoTurnRight = false;
	float LocoTurnEased = 0.f;
};
