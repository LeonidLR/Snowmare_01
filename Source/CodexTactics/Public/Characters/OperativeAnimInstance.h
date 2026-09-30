#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Characters/OperativeMovementRules.h"
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

	/** Seconds the rifle stays raised (bIsAiming) after a shot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation", meta = (ClampMin = "0"))
	float AimHoldAfterShot = 1.5f;

	/** Played on UpperBodySlot for every shot (FireAimMontage while aiming, when set). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimMontage> FireAimMontage;

	/** Played on UpperBodySlot when a reload starts, stretched to the weapon's reload time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	TObjectPtr<UAnimSequenceBase> ReloadAnimation;

	/** Slot of the graph's upper-body layer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Animation")
	FName UpperBodySlot = TEXT("DefaultSlot");

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

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FOperativeAnimInstanceProxy;

	void UpdateState();
	void UpdateUpperBody(float DeltaSeconds);
	void UpdateColdLayer(float DeltaSeconds);
	void HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit);
	void UpdateNativeBlend(float DeltaSeconds);
	UAnimSequence* GetClip(EOperativeClip Clip) const;

	/** Stance weights (standing, crouching, prone), crossfaded. */
	FVector3f StanceWeights = FVector3f(1.f, 0.f, 0.f);
	/** Idle->moving blend inside each stance: X standing (0 idle, 1 walk, 2 run), Y crouch, Z prone. */
	FVector3f MoveBlend = FVector3f::ZeroVector;
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipWeights{InPlace, 0.f};
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipTimes{InPlace, 0.f};

	TWeakObjectPtr<AOperativeCharacter> BoundOperative;
	FDelegateHandle FiredHandle;
	float AimTimer = 0.f;
	bool bWasReloading = false;
};
