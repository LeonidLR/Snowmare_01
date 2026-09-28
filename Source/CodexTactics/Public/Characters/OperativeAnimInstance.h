#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Characters/OperativeMovementRules.h"
#include "Survival/ColdRules.h"
#include "OperativeAnimInstance.generated.h"

class UAnimSequence;

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
 * Parent class of /Game/Characters/Operatives/ABP_Operative.
 * Godot reference: Scripts/components/locomotion_controller.gd (StandLocomotion / CrouchLocomotion / ProneIdle).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UOperativeAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
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

	// --- State for AnimBP graphs (updated every frame) ---

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
	void UpdateNativeBlend(float DeltaSeconds);
	UAnimSequence* GetClip(EOperativeClip Clip) const;

	/** Stance weights (standing, crouching, prone), crossfaded. */
	FVector3f StanceWeights = FVector3f(1.f, 0.f, 0.f);
	/** Idle->moving blend inside each stance: X standing (0 idle, 1 walk, 2 run), Y crouch, Z prone. */
	FVector3f MoveBlend = FVector3f::ZeroVector;
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipWeights{InPlace, 0.f};
	TStaticArray<float, static_cast<int32>(EOperativeClip::Count)> ClipTimes{InPlace, 0.f};
};
