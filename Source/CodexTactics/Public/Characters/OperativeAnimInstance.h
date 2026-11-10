#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Characters/BlizzardWalkRules.h"
#include "Characters/InjuredLocomotionRules.h"
#include "Characters/InteractionAnimRules.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/RifleLocomotionRules.h"
#include "Characters/WeaponAnimLayerRules.h"
#include "Characters/WeaponLayerHandoffRules.h"
#include "Data/WeaponStats.h"
#include "Ladders/LadderClimbComponent.h"
#include "Survival/ColdRules.h"
#include "Survival/WarmIdleRules.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTypes.h"
#include "UObject/ObjectKey.h"
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

/**
 * One weather idle clip set (user request 2026-10-12, /Game/90+_Weather_Animations NEW_version/Cold retargeted by
 * Scripts/Editor/setup_operative_weather_animation.py): 1Start, 2Loop, 3Tranzit, 4Loop (She Cold_Fire_*: 5Loop), 5Tranzit, 6End.
 * Missing transit / LoopB: LoopA repeats; missing End: the pose blends out.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWarmIdleClipSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> Start;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> LoopA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> TransitAB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> LoopB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> TransitBA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UAnimSequenceBase> End;

	bool IsValid() const { return LoopA != nullptr; }
};

/**
 * One interaction clip (game designer 2026-10-15): the standing (FullBody slot) and the crouched (upper-body slot) montage of the same
 * Polygonmaker clip, made by Scripts/Editor/setup_operative_interaction_animation.py. A missing / wrongly slotted UpperBody montage
 * falls back to the FullBody montage's clip played on the upper-body slot.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeInteractionClip
{
	GENERATED_BODY()

	FOperativeInteractionClip() = default;
	FOperativeInteractionClip(float InFullBodyStartSeconds, float InEffectFraction)
		: FullBodyStartSeconds(InFullBodyStartSeconds), EffectFraction(InEffectFraction) {}

	/** Standing: the whole body (slot FullBody). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UAnimMontage> FullBody;

	/** Crouched: the upper-body layer (the ABP's UpperBodySlot), the legs keep the crouch pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<UAnimMontage> UpperBody;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0.1"))
	float PlayRate = 1.f;

	/** Where the FullBody montage starts, s (0 = its start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0"))
	float FullBodyStartSeconds = 0.f;

	/**
	 * Rev 2026-10-19 (game designer: the animation FIRST, then the effect): the effect moment as a fraction of the whole clip - the
	 * lid is open (the loot window opens), the hand grabs (the items move), the charge is lifted (the trap disappears). Counted from
	 * the clip's start, also when the FullBody montage starts later (InteractionAnimRules::GetEffectDelay).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", ClampMax = "1"))
	float EffectFraction = 0.5f;
};

/** What PlayInteractionTimed started: the seconds until the clip ends and until its effect moment. */
struct FInteractionClipTiming
{
	/** A clip started (false: none for the stance / no clip / time frozen). */
	bool bPlayed = false;
	/** Seconds until the clip ends. */
	float Seconds = 0.f;
	/** Seconds until its effect moment (FOperativeInteractionClip::EffectFraction). */
	float EffectDelaySeconds = 0.f;
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
	/**
	 * Graph instances: the graph, then the ladder rung IK (LADDERS.md 6.2) on hands / feet and the run-and-peek's peek stance yaw lock -
	 * no ABP node needed.
	 */
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

	/** How far the rung IK moved each knee in the last evaluation (component space; 0 = no IK), [0] left, [1] right (smokes). */
	FVector GetKneeIKOffset(int32 Side) const { return KneeIKOffset[FMath::Clamp(Side, 0, 1)]; }

	/** The root yaw the peek stance yaw lock put on the last evaluated pose, deg (0 = no lock; smokes / logs). */
	float GetPeekLockCorrectionDeg() const { return PeekLockCorrectionDeg; }

private:
	/** Two-bone IK of the hands / feet onto their rungs (component space, after the whole graph). */
	void ApplyLadderIK(FPoseContext& Output) const;
	/**
	 * The run-and-peek's peek stance yaw lock (#55, the game designer's video Bug_2 2026-11-08), after the whole graph: a root yaw that draws
	 * the evaluated pose's body (its shoulder / hip lines, component space) at the lock's target yaw (UOperativeAnimInstance::
	 * BeginPeekStanceYawLock) - the pose blend's own ~160 deg turn counter-turned exactly, in the very evaluation that draws it.
	 */
	void ApplyPeekStanceYawLock(FPoseContext& Output) const;
	/** The lock for this evaluation (computed in PreUpdate): the drawn body's target yaw in component space, its strength, the shoulder share. */
	float PeekLockTargetCs = 0.f;
	float PeekLockAlpha = 0.f;
	float PeekLockShoulderWeight = 1.f;
	/** The largest root yaw the lock may put on this evaluation (the approach lock only takes out what is left, deg). */
	float PeekLockMaxCorrectionDeg = 360.f;
	mutable float PeekLockCorrectionDeg = 0.f;
	/** The evaluated pose's own drawn body yaw before the lock's correction (component space), last evaluation. */
	mutable float PeekLockNaturalCs = 0.f;
	mutable bool bPeekLockNaturalValid = false;

public:
	/** The pose's own drawn body yaw (component space) the lock saw in its last evaluation; false without one. */
	bool GetPeekLockNaturalCs(float& OutYawCs) const
	{
		OutYawCs = PeekLockNaturalCs;
		return bPeekLockNaturalValid && PeekLockAlpha > 0.f;
	}

	float GetWeaponProbeLower() const { return WeaponProbeLower; }
	float GetWeaponProbeUpper() const { return WeaponProbeUpper; }

private:
	FLadderIKFrame LadderIK;
	mutable FVector KneeIKOffset[2] = { FVector::ZeroVector, FVector::ZeroVector };
	TStaticArray<FOperativeClipState, static_cast<int32>(EOperativeClip::Count)> Clips;
	bool bUseNative = true;

	/** The weapon layer (the D-1 shotgun set, 2026-11-08) blended over the graph's pose: upper-body weight (the legs are never its) and its clips. */
	void BlendWeaponLayer(FPoseContext& Output) const;
	/** Smokes: records how far the layer moved the lower-body (non-torso) and upper-body bones from the graph's pose (local space). */
	void RecordWeaponProbe(const FCompactPose& Before, const FCompactPose& After) const;
	/** The probe: on (Instance->bWeaponLayerProbe), the largest local translation (cm) / rotation (deg) change of a lower / upper-body bone this frame. */
	bool bWeaponProbe = false;
	mutable float WeaponProbeLower = 0.f;
	mutable float WeaponProbeUpper = 0.f;
	/** The shotgun's per-shell reload clip on spine_01 and below (after the layer). */
	void BlendWeaponReload(FPoseContext& Output) const;
	/** An additive fire clip (the pistol's mesh-space recoil) on top of the layer's pose. */
	void BlendWeaponFireAdditive(FPoseContext& Output) const;
	const UAnimSequenceBase* WeaponFireAdditive = nullptr;
	float WeaponFireAdditiveTime = 0.f;
	TStaticArray<FOperativeClipState, 16> WeaponClips;
	float WeaponUpperWeight = 0.f;
	/** The shotgun's per-shell reload on the upper body (spine_01 and below it): clip, time, weight. */
	const UAnimSequenceBase* WeaponReloadClip = nullptr;
	float WeaponReloadTime = 0.f;
	float WeaponReloadWeight = 0.f;
	/**
	 * The launcher set (the RPG-7, 2026-11-09): its action clip (the shot / the reload) blended INTO the layer's pose before the torso is put on
	 * the ABP's pelvis, then the pose-based aim offset (the Rotation_* poses' last frame against their first, mesh-space rotation additives).
	 */
	void BlendWeaponAction(FCompactPose& Layer, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes) const;
	void BlendWeaponAimPoses(FCompactPose& Layer, FBlendedCurve& Curve, UE::Anim::FStackAttributeContainer& Attributes) const;
	const UAnimSequenceBase* WeaponActionClip = nullptr;
	float WeaponActionTime = 0.f;
	float WeaponActionWeight = 0.f;
	/** A "full" set (the launcher, designer 2026-11-09 b): every bone but the root from the layer; the lower body's own weight (pelvis + legs). */
	bool bWeaponFullBody = false;
	bool bWeaponRootReference = false;
	/** A full set's crouched upper-body mode ("crouch_body": "upper"): the torso faces the capsule, not the ABP's turned root (its crouch
	 * orientation warping turned the launcher ~23 deg off the target). */
	bool bWeaponTorsoOnCapsule = false;
	float WeaponLowerWeight = 0.f;
	/** A full set's "legs_abp" share: the root yaw, pelvis, legs and IK bones go back to the graph's pose by it, and spine_01 is re-expressed so
	 * the torso keeps the set's facing on the capsule (the reference root) over the ABP's pelvis. */
	float WeaponLegsAbpWeight = 0.f;
	/**
	 * A full-body set's torso yaw held to its forward clip's (FormationJitterSmoke 2026-11-09): the pack's directional clips twist the chest
	 * -27 .. +16 deg off the body (the ABP's rifle legs keep it within +-6), so every change of direction - a formation follower's slot
	 * corrections - swung the shoulders and the gun; the spine takes out the difference to the gait's forward clip at the same phase.
	 */
	void StabilizeWeaponTorso(FCompactPose& Layer) const;
	const UAnimSequenceBase* WeaponTorsoRef = nullptr;
	float WeaponTorsoRefTime = 0.f;
	float WeaponTorsoAlpha = 0.f;
	TStaticArray<const UAnimSequenceBase*, 9> WeaponAimPoses = TStaticArray<const UAnimSequenceBase*, 9>(InPlace, nullptr);
	const UAnimSequenceBase* WeaponAimBase = nullptr;
	TStaticArray<float, 9> WeaponAimWeights = TStaticArray<float, 9>(InPlace, 0.f);
	/**
	 * The prone crawl layer (the game designer's CrawlAndShoot_Bug_01, 2026-11-09): the 8-direction crawl clips blended over the graph's prone
	 * pose on every bone but the root (the root keeps its reference rotation / translation: the capsule moves the body and faces the threat).
	 */
	void BlendProneCrawlLayer(FPoseContext& Output) const;
	TStaticArray<FOperativeClipState, 6> CrawlClips;
	float CrawlWeight = 0.f;
	/**
	 * The injured locomotion layer (the game designer 2026-11-10; OperativeAnimInstanceInjured.cpp): the CON_Injured clips on the LOWER body (root
	 * -> the reference, pelvis, legs, foot IK bones) after the graph and the weapon layer; spine_01 keeps the graph's mesh-space rotation (the
	 * torso's weapon pose / aim; TorsoShare of the injured pelvis's rotation rides along) and the hand IK bones their mesh-space transform.
	 */
	void BlendInjuredLayer(FPoseContext& Output) const;
	TStaticArray<FOperativeClipState, 6> InjuredClips;
	float InjuredWeight = 0.f;
	float InjuredTorsoShare = 0.f;
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

	/**
	 * Rev 5e item 6 (user request 2026-10-14): prone while moving - the walk-to-prone / run-to-prone dives, full body. They play
	 * instead of StandToProneAnimation when AOperativeCharacter::DropProne dives (GetProneDive). Rev 5f: they must have ROOT MOTION
	 * (the clip's root travel moves the capsule; root locked on the first frame). Empty = loaded at start from the Claude-owned
	 * copies /Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Walk_RM / AS_Crawl_from_Run_RM (made from the user's
	 * /Game/Crawl_MocapAnimPack/Animations/Transitions_Set/Crawl_from_Walk / Crawl_from_Run by
	 * Scripts/Editor/create_prone_dive_root_motion_clips.py with Enable Root Motion on; the root travels 3.1 / 3.4 m forward).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> WalkToProneAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	TObjectPtr<UAnimSequenceBase> RunToProneAnimation;

	/** Rev 5f: blend-in of a dive clip, s (short: the dive's root motion is weighted by the montage while it blends in). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions", meta = (ClampMin = "0"))
	float ProneDiveBlendInSeconds = 0.08f;

	/** The dive going on now (None = no dive); for the AnimBP. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Stance Transitions")
	EProneDive ProneDive = EProneDive::None;

	/** The dive clip of Dive (WalkToProneAnimation / RunToProneAnimation), nullptr when unset / None. */
	UAnimSequenceBase* GetProneDiveClip(EProneDive Dive) const;

	/** Position (s) of the stance clip Clip playing now on the full-body slot; negative when it is not the one playing. */
	float GetStanceTransitionPosition(const UAnimSequenceBase* Clip) const;

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

	/** Standing / crouched hit from behind (empty = HitStand / HitCrouch). Upper body like the others. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> HitBackAnimation;

	/** At most one hit reaction per this many seconds (a burst plays one; HitReactionRules). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float HitReactionMinIntervalSeconds = 0.8f;

	/** Hits below this damage (cold ticks, chip damage) play no reaction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float HitReactionMinDamage = 1.f;

	/** Play the reaction in cover too (default off: the M4 cover clips keep the body; the damage flash still shows). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	bool bHitReactionInCover = false;

	/** Blend in / out of the upper-body hit reaction, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float HitReactionBlendInSeconds = 0.08f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float HitReactionBlendOutSeconds = 0.2f;

	/** Hit reactions started / the last clip (smokes). */
	int32 GetHitReactionsPlayed() const { return HitReactionsPlayed; }
	UAnimSequenceBase* GetLastHitReactionClip() const { return LastHitReactionClip.Get(); }
	/** Shots whose fire clip was left out because a stance transition was playing (#51; for the smokes). */
	int32 GetFireClipsSkippedForStance() const { return FireClipsSkippedForStance; }
	FName GetLastHitReactionSlot() const { return LastHitReactionSlot; }

	/** Seconds the left-hand IK stays off for an upper-body action (hit reaction, grenade throw, interaction) - smokes. */
	float GetLeftHandIKBlockSeconds() const { return LeftHandIKBlockSeconds; }

	/**
	 * Upper-body grenade throw: walking, running, crouched, prone (Godot grenade_throw_*). Played on UpperBodySlot (the
	 * graph's layered blend from spine_01) so the legs keep the locomotion. The grenade leaves the hand at the matching
	 * GrenadeThrow*ReleaseSeconds (the measured release frame), not at the end of the clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowWalkAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowRunAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowCrouchAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> GrenadeThrowProneAnimation;

	/**
	 * Release time of each throw clip, s from its start (hand at peak speed / just opening, measured 2026-10-07 on the
	 * Grenade_01root_root_*_UE clips: walk 1.28, run 1.73, crouch 0.89, prone 1.14). <= 0 = 70 % of the clip length.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowWalkReleaseSeconds = 1.28f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowRunReleaseSeconds = 1.73f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowCrouchReleaseSeconds = 0.89f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowProneReleaseSeconds = 1.14f;

	/** Blend in / out of the grenade throw on UpperBodySlot, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowBlendInSeconds = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float GrenadeThrowBlendOutSeconds = 0.2f;

	/** Full-body death: one of the standing variations at random, or the crouched / prone one (held on its last frame). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TArray<TObjectPtr<UAnimSequenceBase>> DeathStandAnimations;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> DeathCrouchAnimation;

	/**
	 * Vault over an obstacle (user request 2026-10-06: the Rifle_2 hurdle, M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_*):
	 * played on FullBodySlot when a vault starts, left / right foot first in turn, its play rate set so the clip lands
	 * (VaultLeftFootLandingTime / VaultRightFootLandingTime) when the vault arc ends; the arc moves the body (the clips are
	 * root-locked), the clip's recovery plays on when the operative stays put and blends out when he walks on.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> VaultLeftFootAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots")
	TObjectPtr<UAnimSequenceBase> VaultRightFootAnimation;

	/** Clip time at which the body lands (the end of the vault arc is matched to it), s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0.1"))
	float VaultLeftFootLandingTime = 0.96f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0.1"))
	float VaultRightFootLandingTime = 0.95f;

	/** Vaults shorter than this (a step down off an obstacle top) play no clip, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|One-shots", meta = (ClampMin = "0"))
	float VaultMinClipDuration = 0.6f;

	/** Vault clips started (smokes). */
	int32 GetVaultClipsPlayed() const { return VaultClipsPlayed; }

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

	/**
	 * Cold walk per tier (Godot cold_* 25 / 40 / 70 / 90) - left EMPTY on purpose (2026-11-10): the 70 / 90 tiers' walk is the injured layer's
	 * slow injured walk (weapon_anim_sets.json "injured" triggers.cold_from 0.7, OperativeAnimInstanceInjured.cpp; the game designer 2026-11-10) -
	 * a native lower-body layer that keeps the weapon torso and plays only while walking forward. Filled, these slots (with the idle ones) would
	 * switch on UpdateColdLayer's idle -> walk pose blend (ColdVisualWeight; bHasPose needs both) for the ABP on top of it. The 25 / 40 tiers have
	 * no walk clip (the normal walk); standing still the shiver is the warm-idle Cold_1 set (UWarmIdleSubsystem).
	 */
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

	/**
	 * On a ladder (docs/design/LADDERS.md 6.1; the climb clips play on FullBodySlot from ULadderClimbComponent): no aim, no left-hand IK,
	 * no aim offset, no sniper / interaction / weather / hit / grenade / reload clips, no cold layer. The rung IK runs natively.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsClimbing = false;

	/** The rung IK frame copied from the climb this update (the proxy applies it). */
	FLadderIKFrame LadderIKFrame;

	/** How far the rung IK moved the left (0) / right (1) knee in the last evaluated pose, cm, component space (smokes). */
	FVector GetLadderKneeIKOffset(int32 Side) const;

	/** Vaulting over an obstacle (Godot "Vault" state). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsVaulting = false;

	/**
	 * The ABP's "FallStates" state machine (2026-11-03, Scripts/Editor/setup_operative_fall_animation.py): exactly one is set from
	 * ULedgeFallComponent::GetAnimPhase - Falling (a drop / knockback in the air, or falling longer than anim.fall_min_s: the fall loop),
	 * Landing (anim.land_s after it: the land clip), Grounded (the locomotion). Montages on the full-body slot (a knockdown) still win.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bFallAirborne = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bFallLanding = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bFallGrounded = true;

	/** The current state of the ABP's "FallStates" machine (NAME_None without it; smokes / shots). */
	FName GetFallStateName() const;

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

	/** Times the corpse pose had to be held again after something took the FullBody slot from it (bug #44; smokes). */
	int32 DeathPoseRestores = 0;

	/**
	 * Sprint 14: in a knockdown (fall / downed / get-up; the clips play on FullBodySlot from UKnockdownComponent): no aim,
	 * no left-hand IK / aim offset, no upper-body clips, no stance transitions.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bKnockedDown = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	EColdTier ColdTier = EColdTier::Normal;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsFrostbitten = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|State")
	bool bIsWeaponFrozen = false;

	/** Current native blend weight of a clip (debug / tests). */
	float GetClipWeight(EOperativeClip Clip) const { return ClipWeights[static_cast<int32>(Clip)]; }

	// --- Tactical cover (Sprint 12, Gemini Sprint 12 spec; no Godot reference). State for the AnimBP and a C++
	// baseline that plays the clips below on FullBodySlot while the graph has no cover states (the user polishes the
	// ABP; Scripts/Editor/setup_operative_cover_animation.py assigns the M4 Cover Pack clips, index 0 = Left (pack *_L),
	// 1 = Right (*_R) = the side along the wall the operative FACES, i.e. ECoverFacing — user design rule 2026-10-06: the
	// actor yaw runs along the wall towards the threat side, the back against the wall; forward = shimmy towards that
	// side, backward = away from it, still facing it; CoverFacingRules). ---

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bInCover = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	ECoverHeight CoverHeight = ECoverHeight::None;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	ECoverFacing CoverFacing = ECoverFacing::Right;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bShimmying = false;

	/** +1 towards his right (facing away from the wall), -1 left, 0 still. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	float ShimmyDirection = 0.f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bLeaning = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bBlindFiring = false;

	/** The shimmy runs towards the facing side (forward clip); false = backwards, still facing the threat. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bCoverShimmyForward = false;

	/** At the exposed edge on the facing side: the corner-ready pose. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bCoverAtCorner = false;

	/**
	 * Corner pose with an active threat (AOperativeCharacter::IsCoverFireReady): hold the fire-ready idle
	 * (CoverStandFireIdle / CoverCrouchFireIdle); without it the corner pose is the look-around idle (CoverStandCorner).
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bCoverFireReady = false;

	/**
	 * Sustained corner aim (user request 2026-10-07): leaned out in the fire stance between shots — the shots play the
	 * fire clip straight from fire_idle, no idle -> fire / fire -> idle transitions in between.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover State")
	bool bCoverCornerAim = false;

	// --- Left-hand IK onto the rifle's handguard (user-approved plan 2026-10-07, LeftHandIKRules). The ABP's Two Bone IK
	// on hand_l (added by the user, see docs/port/HANDOFF.md «ABP: left-hand IK») reads these: effector = LeftHandIKOffset
	// in hand_r bone space, alpha = LeftHandIKAlpha. ---

	/** Left-hand IK on / off for this AnimBP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	bool bLeftHandIK = true;

	/** The weapon mesh's socket the left hand holds (Scripts/Editor/add_weapon_left_hand_socket.py). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	FName LeftHandGripSocket = TEXT("LeftHandGrip");

	/** Alpha blend time in / out, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK", meta = (ClampMin = "0"))
	float LeftHandIKBlendSeconds = 0.15f;

	/** The grip in hand_r bone space (Two Bone IK: Effector Location Space = Bone Space, Effector Target = hand_r). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	FVector LeftHandIKOffset = FVector::ZeroVector;

	/** The grip's rotation in hand_r bone space (for a later Transform (Modify) Bone on hand_l). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	FRotator LeftHandIKRotation = FRotator::ZeroRotator;

	/** 0..1, eased (smoothstep) over LeftHandIKBlendSeconds; 0 when reloading, throwing, hit, vaulting, dead, prone, no grip. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	float LeftHandIKAlpha = 0.f;

	/** The IK target stays within this share of the arm's reach (upper + lower arm): a farther grip slides back along the barrel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK", meta = (ClampMin = "0.5", ClampMax = "1"))
	float LeftHandIKReachFraction = 0.95f;

	/**
	 * The IK chain's root bone (its reach is measured from there): upperarm_l for the Two Bone IK node; clavicle_l when the
	 * graph uses a FABRIK from the clavicle (the shoulder swings forward: ~+15 cm of reach).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	FName LeftHandIKChainRoot = TEXT("upperarm_l");

	/** At most this far back along the barrel, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK", meta = (ClampMin = "0"))
	float LeftHandIKMaxSlideCm = 40.f;

	/** A target still beyond the reach after the slide fades the alpha out over this excess distance, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK", meta = (ClampMin = "0"))
	float LeftHandIKFadeCm = 15.f;

	/** Clips (name parts) that deliberately free the left hand: the IK is off while they play on the FullBody slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	TArray<FString> LeftHandIKFreeClips;

	/** How far the target still lies beyond the reach after the slide (fades the alpha), cm. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	float LeftHandIKExcessCm = 0.f;

	/** How far the target slid back from the socket this update (0 = the socket is in reach), cm. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	float LeftHandIKSlideCm = 0.f;

	/** Upper + lower arm length of the reference skeleton, cm (read once). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	float LeftHandArmReach = 0.f;

	/** Smokes: >= 0 forces LeftHandIKAlpha (to tell what the ABP's IK node changes). */
	float LeftHandIKAlphaOverrideForTesting = -1.f;

	/** The weapon has the grip socket (the offset is valid). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Left Hand IK")
	bool bLeftHandIKGripValid = false;

	// --- Aim offset (user request 2026-10-07, AimOffsetRules): the ABP's Aim Offset Player node with
	// /Game/RifleAnims/Animations/AimOffsets/AO_Idle/AO1D_Rifle_Idle reads AimPitch (axis "Pitch", -90..90) and
	// AimOffsetAlpha, see docs/port/HANDOFF.md section 6 "ABP: aim offset". ---

	/** Aim offset on / off for this AnimBP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffset = true;

	/** Keep the aim offset in the cover poses (leaned out / fire-ready); off = it fades out while in cover. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffsetInCover = true;

	/** Keep the aim offset in turn-based combat; off (default) = it fades out there (the shot still turns the body to the target) and returns in real time / tactical pause. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffsetInTurnBased = false;

	/** True while the offset is switched off by turn-based combat (see bAimOffsetInTurnBased). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffsetTurnBasedOff = false;

	/** AimPitch follows the target's pitch at this rate (FInterpTo speed, 1/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "0.1"))
	float AimPitchInterpSpeed = 10.f;

	/** The AO's range: AimPitch is clamped to +-this (AO1D_Rifle_Idle: -90..90), degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "1", ClampMax = "90"))
	float AimPitchClampDegrees = 90.f;

	/** AimOffsetAlpha blend time in / out, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "0"))
	float AimOffsetBlendSeconds = 0.2f;

	/** Pitch to the aim target in degrees (+ up, - down), eased; 0 with no target. The AO's "Pitch" axis. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	float AimPitch = 0.f;

	/** The raw (not eased) pitch from the muzzle height to the aim point, degrees; 0 with no target. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	float AimPitchTarget = 0.f;

	// --- 2D aim offset (user request 2026-10-07): /Game/RifleAnims/Animations/AimOffsets/AO_Aim/NotUsed_AimOffset2D/
	// AO_Rifle_Aim (axes Yaw -180..180, Pitch -90..90; RightCenter at Yaw +90) reads AimYaw on X and AimPitch on Y. The
	// yaw twists the upper body from the current pose's aim direction (the corner fire stance's lean-out direction, else
	// the barrel) to the target, limited to AimYawClampDegrees; the operative's shot waits until the rest is within its
	// aim cone (AOperativeCharacter::AimConeDeg). AimYaw is always computed; the gameplay counts on the twist only with
	// bAimOffsetYaw (tick it in the ABP's Class Defaults once the 2D node is in; the 1D node keeps it off). ---

	/**
	 * The ABP applies AimYaw (2D AO_Rifle_Aim wired): the shot may count on the upper-body twist (up to AimYawClampDegrees).
	 * Off (the 1D AO1D_Rifle_Idle, pitch only): the shot waits until the pose itself points at the target.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffsetYaw = false;

	/** The upper-body twist limit: AimYaw is clamped to +-this, degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "0", ClampMax = "180"))
	float AimYawClampDegrees = 60.f;

	/** A target farther than AimYawClampDegrees + this from the pose's aim is out of the twist's reach: AimYaw eases to 0 (deg). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "0", ClampMax = "120"))
	float AimYawReachMarginDegrees = 30.f;

	/** AimYaw follows the target at this rate (FInterpTo speed, 1/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Aim Offset", meta = (ClampMin = "0.1"))
	float AimYawInterpSpeed = 12.f;

	/** Yaw from the pose's aim direction to the target in degrees (+ = to his right), eased; 0 with no target. The AO's "Yaw" axis. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	float AimYaw = 0.f;

	/** The raw (not eased, clamped) yaw to the aim point, degrees; 0 with no target. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	float AimYawTarget = 0.f;

	/** The target is out of the twist's reach (AimOffsetRules::YawTargetWithinReach hysteresis state). */
	bool bAimYawOutOfReach = false;

	/** The operative has an aim target (a combat target or a shot within AimTargetHoldSeconds). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bHasAimTarget = false;

	/** The aim offset is blended in (AimOffsetAlpha > 0). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	bool bAimOffsetActive = false;

	/** 0..1, eased; 0 while reloading, throwing, hit, vaulting, sprinting, prone, dead, melee, weapon hidden. The AO node's Alpha. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Aim Offset")
	float AimOffsetAlpha = 0.f;

	/** Blend-in of the cover enter clip when he runs into the cover (eased; user-found bug 2026-10-07), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEnterFromRunBlendSeconds = 0.25f;

	/** The enter clip's blend-in when the actor turned more than 90 deg on entry (its back to the wall): fast, so the run pose never shows turned round, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEnterTurnedBlendSeconds = 0.03f;

	/** Weight of the cover enter clip now (0..1 while it blends in / plays), -1 when none plays. */
	float GetCoverEnterBlendWeight() const;

	/**
	 * The run-and-peek's arrival (AOperativeCharacter::IsCoverEntryPeekStance; the game designer's video 2026-11-08): the run blends straight
	 * into the corner peek stance (the fire idle round the corner) over this long - no enter clip, no back-to-the-wall idle, no idle -> fire
	 * step-out, s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverPeekStanceBlendSeconds = 0.3f;

	/**
	 * Leaving the corner fire pose (fix/corner-exit-jerk, RecoilProbe 2026-11-08: the upper body jumped 50-60 cm sideways): the fire idle stands
	 * leaned out round the corner - its root ~50 cm sideways in the pose - and the clip that follows (the fire -> idle exit, the shimmy loop,
	 * the walk off) has its root at the feet; their 0.1 - 0.2 s blend moved the body 10 - 17 cm a frame. Whatever follows the fire pose blends
	 * in over this long (and the exit clip is the fire pose's own side, not the side a shimmy / the leave turned the facing to), s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverFireLeaveBlendSeconds = 0.45f;

	/** Run-and-peek arrivals that blended straight into the peek stance's fire idle (smokes). */
	int32 GetCoverPeekStanceBlends() const { return CoverPeekStanceBlends; }

	/**
	 * The turn about the vertical the peek stance's blend will draw (#55 fix 2026-11-08): from the pose on the mesh now (the run) to the first
	 * frame of the fire idle of Side (crouched or standing), measured on the pelvis in component space the shortest way - the way the pose
	 * blend turns the body, signed, deg. The pack's fire idles stand turned ~170-190 deg from their root (back to the wall, the body round the
	 * corner), so the blend's direction differs per clip; the operative counter-rotates the mesh the same way (no spin). False without the clip
	 * or the bones.
	 */
	bool GetPeekStanceTurnDeg(ECoverFacing Side, bool bCrouch, float& OutTurnDeg) const;

	// --- Arrives already turned (#55, the game designer's video Bug_2 2026-11-08: "when he runs up and peeks out from the wall there seems to
	//     be another small extra turn"; RunCornerPeekRules "Arrives already turned") ---

	/**
	 * The drawn body's yaw (RunCornerPeekRules::MixBodyYaw of the shoulder line upperarm_l -> upperarm_r and the hip line thigh_l -> thigh_r)
	 * at ClipSeconds (the montage starts it at 0) of Side's fire idle (crouched or standing), in the mesh's component space, plus what the
	 * graph's layers on top of the clip add (the bias learnt from the arrivals so far, SettlePeekStanceYawLock), deg: where the peek stance
	 * will draw his body. False without the clip or the bones.
	 */
	bool GetPeekStanceBodyYawCs(ECoverFacing Side, bool bCrouch, float& OutYawCs, float ClipSeconds = 0.f) const;

	/** The drawn body's yaw of the pose on the mesh now (its last evaluation), component space, deg. False without the bones. */
	bool GetMeshBodyYawCs(float& OutYawCs) const;

	/**
	 * The run-and-peek's peek stance yaw lock (AOperativeCharacter::EnterCover of a peek stance arrival): from now the evaluated pose is turned
	 * about the vertical so that its drawn body yaw (component space, the mesh at its base yaw on the capsule) goes from EntryYawCs (the run's
	 * last frame, as the actor stands now) to FinalYawCs (the fire idle's own) eased over PathSeconds (RunCornerPeekRules::LockTargetYaw), then
	 * lets go over RunCornerPeekRules::LockFadeSeconds. Replaces the mesh's held yaw released by the blend weight (the first fix): exact
	 * frame by frame whatever the pose blend does (its nlerp over the ~160 deg pelvis turn, the run stopping under it).
	 */
	void BeginPeekStanceYawLock(float EntryYawCs, float FinalYawCs, float PathSeconds, ECoverFacing Side, bool bCrouch);

	/** The lock ends its path where it is now and lets go (leaving the cover, death). */
	void ReleasePeekStanceYawLock();

	/**
	 * The run-and-peek's APPROACH (the shotgun Engineer, main's verification 2026-11-08: his jog's torso sways under the turn): from the
	 * pre-turn's start the drawn body's yaw in WORLD space goes from StartWorldYaw to FinalWorldYaw linearly over PathSeconds (the pre-turn's
	 * own even plan) and holds there, the root yaw on the evaluated pose capped at RunCornerPeekRules::ApproachLockMaxCorrectionDeg - the
	 * actor's pre-turn does the turn, the lock takes out the run pose's sway, so the drawn body turns one way. The entry's lock replaces it.
	 */
	void BeginPeekApproachYawLock(float StartWorldYaw, float FinalWorldYaw, float PathSeconds);

	/** The approach lock (only) lets go - the run-and-peek dropped / another entry. */
	void ReleasePeekApproachYawLock();

	/**
	 * The drawn body's yaw of the pose as the graph and the weapon layer made it - before the peek stance yaw lock's root yaw (the lock's
	 * last evaluation; the mesh's pose without a lock), component space, deg. The pre-turn steers on it (the lock's own correction must not
	 * feed back). False without the bones.
	 */
	bool GetNaturalBodyYawCs(float& OutYawCs) const;

	/** The peek stance yaw lock is on (its path or its fade). */
	bool IsPeekStanceYawLockActive() const { return PeekStanceYawLock.bActive; }

	/** Peek stance yaw locks begun (smokes / stats). */
	int32 GetPeekStanceYawLocks() const { return PeekStanceYawLocks; }

	/** The root yaw the lock put on the last evaluated pose, deg (smokes / logs). */
	float GetPeekStanceYawLockCorrectionDeg() const;

	/** GFrameCounter of this instance's last NativeUpdateAnimation: whether the pose of the current frame has already been decided. */
	uint64 GetLastAnimUpdateFrame() const { return LastAnimUpdateFrame; }

	/** The drawn body's yaw for the lock and the pre-turn: this share of the shoulder line, the rest the hip line (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float PeekStanceBodyShoulderWeight = 1.f;

	/** Play the cover clips natively on FullBodySlot (untick once the AnimBP has its own cover states). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation")
	bool bUseNativeCoverClips = true;

	/** Standing / crouched cover idle, back to the wall, by facing (M4: anim_M4_cvr_std_idle_L / _R, cvr_crch_idle_L / _R). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandIdle = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchIdle = { nullptr, nullptr };

	/**
	 * Look-around corner pose at the exposed edge on the facing side while NO threat is known (M4 cvr_std_look_at_idle_L
	 * / _R, cvr_crch_look_at_idle_L / _R). Empty = loaded from /Game/M4_Cover_Pack at start when the pack is present
	 * (idle otherwise). With a known threat the fire-ready idle below is held instead.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandCorner = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchCorner = { nullptr, nullptr };

	/**
	 * Shimmy loops towards the facing side (M4 cvr_*_walk_fwd_loop) and away from it, walking backwards (walk_bwd_loop).
	 * [0] = _L, [1] = _R is the MOVEMENT direction along the wall for both: threat right -> right = fwd_loop_R, left =
	 * bwd_loop_L (the backing-away clip of the opposite suffix faces the threat).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandMoveForward = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandMoveBackward = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchMoveForward = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchMoveBackward = { nullptr, nullptr };

	/** Corner lean + shot (M4 cvr_std_fire_L / _R, cvr_crch_fire_L / _R), one shot each. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandFire = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchFire = { nullptr, nullptr };

	/**
	 * Fire-ready corner stance at the exposed edge on the threat side (M4 cvr_std_fire_idle_L / _R, cvr_crch_fire_idle_L / _R:
	 * [0] = _L, [1] = _R). Held while a threat is known; empty = loaded from /Game/M4_Cover_Pack at start, then the
	 * look-around corner pose stands in.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandFireIdle = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchFireIdle = { nullptr, nullptr };

	/** Cover idle -> fire-ready transition (M4 cvr_std_idle_L_to_fire / _R_to_fire, cvr_crch_idle_to_fire_L / _R). Empty = no transition. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandFireEnter = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchFireEnter = { nullptr, nullptr };

	/** Fire-ready -> cover idle transition (M4 cvr_std_fire_to_std_idle_L / _R, cvr_crch_fire_to_idle_L / _R). Empty = no transition. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandFireExit = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchFireExit = { nullptr, nullptr };

	/**
	 * Stand <-> crouch at the wall (user request 2026-10-07): the pack's cover stance switches [_L, _R]
	 * (cvr_stand_idle_L_to_cvr_crch_idle_L / cvr_crch_idle_L_to_cvr_stand_idle_L, R alike; measured: they keep the side).
	 * Empty = the pack's clips when present, else the generic stance transition.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandToCrouch = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchToStand = { nullptr, nullptr };

	/**
	 * Stand <-> crouch while in the corner fire stance: the two fire_idle clips cross-blend over this long instead of the
	 * idle -> idle switch clips above (they tuck the body back behind the corner; user PIE 2026-10-07), s.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverFireStanceSwitchBlendSeconds = 0.35f;

	/** A cover one-shot hands over to the next clip this much before its own blend-out starts, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverHandoffMarginSeconds = 0.05f;

	/** Blind fire round the corner (no M4 clip: the user supplies one; empty = the cover fire clip stands in). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverBlindFire = { nullptr, nullptr };

	/** Cover_Enter from the open (M4 std_idle_fwd_to_cvr_std_idle_L / _R, crch_idle_fwd_to_cvr_crch_idle_L / _R). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandEnter = { nullptr, nullptr };

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchEnter = { nullptr, nullptr };

	/**
	 * Wall-edge cover switch (game designer 2026-10-20, Tactics/CoverSwitchRules): the M4 pack's run-in transitions that end at the
	 * wall, played instead of Cover_Enter when the operative arrives from a switch dash. Six per stance, index =
	 * ECoverSwitchApproach * 2 + end side (CoverFacingRules::ClipIndex: 0 = _L, 1 = _R): std_jog_fwd_to_cvr_std_idle_L / _R
	 * (straight at the wall), std_jog_fwd_L_to_cvr_std_idle_L / _R (along the wall from his own right), std_jog_fwd_R_to_... (from
	 * his left). Empty entries are filled from /Game/M4_Cover_Pack at initialisation (nothing saved). Only the last metres play: the
	 * montage starts where the clip's body is nearest to the runner (AOperativeCharacter::GetCoverSwitchStartSeconds).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandSwitchEnter = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	/** The same ending crouched (low cover): std_jog_fwd[_L|_R]_to_cvr_crch_idle_L / _R (the pack's crouch folder). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchSwitchEnter = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	/**
	 * The pack's SPRINT run-ins (user 2026-10-21: "only the walking animation, no fast movement"), same indices: std_sprint_fwd[_L|_R]_to_cvr_
	 * {std|crch}_idle_L / _R. Measured (2026-10-21, root bone): the body comes in at ~960-1015 cm/s (the jog ones 400-530) and is still 0.97-1.27
	 * s in; from 2 m out 0.58-0.80 s are left (jog 0.83-1.02). A sprinting dash prefers them (AOperativeCharacter::CoverSwitchSprintPreferenceCm).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverStandSwitchSprintEnter = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> CoverCrouchSwitchSprintEnter = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	/** Blend-in of the switch run-in clip over the running pose (its jog matches the run, so a plain eased blend), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverSwitchBlendSeconds = 0.2f;

	/** A switch run-in clip of the stance is assigned / loaded (any of the six). */
	bool HasCoverSwitchClips(bool bCrouch) const;

	/** The run-in clip for Approach ending facing ArrivalFacing (bSprint: the sprint set); nullptr when that one is missing. */
	UAnimSequenceBase* GetCoverSwitchClip(bool bCrouch, ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, bool bSprint = false) const;

	/**
	 * The clip's root track as world XY offsets from where it ends, for a mesh whose component yaw in the world is MeshWorldYaw
	 * (actor yaw + the Blueprint's mesh yaw), sampled at 30 Hz (cached per clip). False when the clip is missing.
	 */
	bool GetCoverSwitchTrack(bool bCrouch, ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, float MeshWorldYaw, FCoverSwitchTrack& OutTrack,
		bool bSprint = false) const;

	/** Name of the last switch run-in clip started (smokes / logs), empty before the first one. */
	const FString& GetLastCoverSwitchClip() const { return LastCoverSwitchClip; }
	/** Where the last switch clip was started, s (smokes). */
	float GetLastCoverSwitchStartSeconds() const { return LastCoverSwitchStart; }

	/** Ground speed of the shimmy loops at rate 1, cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover Animation", meta = (ClampMin = "1"))
	float CoverShimmyClipSpeed = 90.f;

	/** The cover loop playing now (nullptr = none; smokes). */
	UAnimSequenceBase* GetCoverLoopClip() const { return CoverLoopClip.Get(); }
	int32 GetCoverClipsPlayed() const { return CoverClipsPlayed; }
	/** Names of the cover clips played (loops, one-shots) in order, newest last, capped (smokes). */
	const TArray<FString>& GetCoverClipLog() const { return CoverClipLog; }

	/** The cover layer is in the fire-ready pose (idle -> fire played / fire_idle looping) - smokes. */
	bool IsCoverInFirePose() const { return bCoverInFirePose; }
	/**
	 * What the mesh REALLY plays on FullBodySlot (smokes): the sequence of the highest-weight active montage, that
	 * montage's blend weight, and the FullBody slot node's weight in the AnimGraph (a node missing / blended out = 0).
	 */
	void GetCoverPlayback(FString& OutClip, float& OutMontageWeight, float& OutSlotNodeWeight) const;

	// --- Sniper rifle set (user request 2026-10-09; Scripts/Editor/setup_operative_sniper_animation.py fills them from
	// /Game/Sniper_Animation). Index [0] stand, [1] knee (crouched), [2] prone (SniperRules::ClipIndex). While the weapon in
	// hands is a sniper rifle (EWeaponHandling::SniperRifle) and she stands still out of cover, the clips play natively on
	// FullBodySlot (no graph change): idle loop per stance, kneel / prone / rise transitions, shot + bolt, magazine reload,
	// hit reaction. Moving keeps the normal locomotion (no sniper walk clips). ---

	/** Play the sniper set (untick to leave the graph / rifle clips on). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	bool bUseSniperClips = true;

	/** Aimed idle loop per stance (AS_Stand_Aim_Idle, AS_Knee_Aim_Idle, AS_Prone_Aim_Idle). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperIdle = { nullptr, nullptr, nullptr };

	/**
	 * Into the stance's aim from standing: [0] relaxed stand -> stand aim (AS_Stand_Aim_Start, played when she comes to rest
	 * standing), [1] stand -> kneel aim (AS_Knee_Aim_Start, the stand -> crouch transition), [2] stand -> prone aim
	 * (AS_Prone_Aim_Idle_Start, the stand -> prone transition). Measured: pelvis 95 -> 41 cm / 95 -> 12 cm.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperAimStart = { nullptr, nullptr, nullptr };

	/** Out of the stance's aim back to standing: [0] AS_Stand_Aim_End, [1] knee -> stand (AS_Knee_Aim_End), [2] prone -> stand (AS_Prone_Aim_Idle_End). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperAimEnd = { nullptr, nullptr, nullptr };

	/** The shot (AS_Knee_Aim_Fire / AS_Prone_Aim_Fire; [0] AS_Stand_Aim_Fire is never used: no standing shot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperFire = { nullptr, nullptr, nullptr };

	/** The bolt worked after a shot while rounds remain (AS_*_Aim_Bullet_Reload). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperBoltCycle = { nullptr, nullptr, nullptr };

	/** Magazine reload, stretched to the weapon's reload time (AS_*_Aim_Magazine_Reload). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperReload = { nullptr, nullptr, nullptr };

	/** Hit reaction in the aim pose (AS_*_Aim_HitReact). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<UAnimSequenceBase>> SniperHitReact = { nullptr, nullptr, nullptr };

	/**
	 * 2D aim offsets built from the pack's Offset_F / L / R / U / D poses (AO_Sniper_Stand / _Knee / _Prone, axes Yaw / Pitch
	 * like AO_Rifle_Aim). The graph's Aim Offset Player uses them only once its Blend Space pin is bound to ActiveAimOffset
	 * (docs/port/HANDOFF.md section 6 «ABP: sniper set»); until then bSniperAimOffsetWired stays off.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", EditFixedSize)
	TArray<TObjectPtr<class UBlendSpace>> SniperAimOffsets = { nullptr, nullptr, nullptr };

	/** The rifle aim offset the graph uses outside the sniper poses (AO_Rifle_Aim; ActiveAimOffset falls back to it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	TObjectPtr<class UBlendSpace> RifleAimOffset;

	/**
	 * Tick once the graph's Aim Offset Player reads ActiveAimOffset: the sniper poses then keep AimOffsetAlpha. Off (default):
	 * AimOffsetAlpha fades to 0 in the sniper poses (the rifle AO was made on AS_Rifle_Aim and would bend the kneeling /
	 * prone pose wrongly); the body itself turns to the target.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	bool bSniperAimOffsetWired = false;

	/** The aim offset for the pose now: the stance's sniper AO in a sniper pose, else RifleAimOffset (bind the AO node's pin). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	TObjectPtr<class UBlendSpace> ActiveAimOffset;

	/** Blend of the idle loop and of a stance change between the loops, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", meta = (ClampMin = "0"))
	float SniperLoopBlendSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", meta = (ClampMin = "0"))
	float SniperShotBlendInSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", meta = (ClampMin = "0"))
	float SniperShotBlendOutSeconds = 0.15f;

	/**
	 * A sniper one-shot / kneel clip hands over to the next clip this long before its own blend-out starts, s (the FullBody slot
	 * never dips: the legs stay in the kneel / prone pose from shot to bolt to reload to the next shot; user report 2026-10-09).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", meta = (ClampMin = "0"))
	float SniperHandoffMarginSeconds = 0.05f;

	/** Below this ground speed she counts as standing still (sniper poses), cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation", meta = (ClampMin = "0"))
	float SniperStillSpeed = 20.f;

	/** A sniper rifle is in hands. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	bool bSniperWeapon = false;

	/** The sniper set owns the body now (still, out of cover, alive): graph hook for the user's own states. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Sniper Animation")
	bool bSniperPose = false;

	/** Plays the sniper shot of the current stance (queued behind a kneel / prone clip), then the bolt when bCycleBolt. */
	void PlaySniperShot(bool bCycleBolt);

	/** A turn-based grid shot shown now: the same clip a real-time shot plays (sniper / prone / rifle fire montage). */
	void PlayShotClip(AOperativeCharacter* Shooter, AActor* Target, bool bHit) { HandleWeaponFired(Shooter, Target, bHit); }

	/** The pawn's stance changed and the stance clip is not decided yet (it starts in the next anim update). */
	bool IsStanceChangePending() const;

	/** Smokes: the sniper loop / the last sniper clip started, the clips started in order (capped). */
	UAnimSequenceBase* GetSniperLoopClip() const { return SniperLoopClip.Get(); }
	const TArray<FString>& GetSniperClipLog() const { return SniperClipLog; }

	// --- The prone crawl layer (the game designer's CrawlAndShoot_Bug_01, 2026-11-09; OperativeAnimInstanceProneCrawl.cpp, ProneCrawlRules) ---
	// The user's Crawl_MocapAnimPack 8-direction in-place crawl loops + the crawl idle (weapon_anim_sets.json "prone_crawl", IK-retargeted copies
	// /Game/Characters/Animations/Crawl/{Male,Female} by Scripts/Editor/setup_crawl_animation.py) blended natively over the ABP's prone pose, full
	// body (the root keeps the reference: the capsule moves and faces), for every weapon: while he crawls on an order or moves prone. The direction
	// is the velocity relative to the body (the body faces the threat: strafe-crawl), the play rate the speed over the clips' authored crawl
	// speed. Off under a full-body montage (the dive, a stance clip, a prone fire / reload / hit) and when still (the ABP's rifle prone pose
	// then: the aim / fire / reload clips). No AnimGraph node.

	/** The crawl layer's blend weight this frame (0..1; smokes). */
	float GetProneCrawlWeight() const { return CrawlWeight; }
	/** The heaviest crawl clip this frame (null when the layer is off; smokes: "the clip of the move's direction plays"). */
	const UAnimSequenceBase* GetProneCrawlDominantClip() const;
	/** The direction the crawl clips play, deg clockwise from the body's forward (smokes). */
	float GetProneCrawlDirection() const { return CrawlDirection.Current; }
	/** The crawl loops' play rate this frame (the lead loop's: speed / its authored speed; smokes / logs). */
	float GetProneCrawlPlayRate() const { return CrawlPlayRate; }
	/** The crawl set is loaded for this body (its skeleton has retargeted copies). */
	bool HasProneCrawlSet() const { return CrawlSetData.StandIdle != nullptr; }
	/**
	 * Keeps the aim pose up (AimTimer, as after a shot) while a crawl holds his fire with a target: the pose waits under the full-body crawl
	 * layer and the rifle is on the target the moment he stops (the ABP's prone idle holds it ~30 deg off the body - outside the aim cone,
	 * so no first round would ever leave; ProneCrawlSmoke 2026-11-10).
	 */
	void HoldAimPose() { AimTimer = FMath::Max(AimTimer, AimHoldAfterShot); }
	/** Prone fire clips cut short because a crawl order started (crawl first; smokes). */
	int32 GetCrawlFireClipsStopped() const { return CrawlFireClipsStopped; }

	// --- The injured locomotion layer (the game designer 2026-11-10; OperativeAnimInstanceInjured.cpp, InjuredLocomotionRules) ---------------
	// The designer's /Game/CON_Injured clips (weapon_anim_sets.json "injured", IK-retargeted copies /Game/Characters/Animations/Injured/{Male,Female}
	// by Scripts/Editor/setup_injured_animation.py) on the LOWER body over the ABP (the torso stays the ABP's / the weapon layer's): wounded
	// (IsWounded) standing = the 8-direction injured walk + the injured idle, prone = the injured crawl forward (the prone crawl layer's forward
	// slot + idle); cold >= "cold_from" while walking forward = the slow injured walk (still = the ABP's shiver idle). No AnimGraph node.

	/** The injured layer's blend weight this frame (0..1; smokes). */
	float GetInjuredWeight() const { return InjuredWeight; }
	/** The gait the layer plays this frame (smokes). */
	InjuredLocomotionRules::EInjuredGait GetInjuredGait() const { return InjuredGait; }
	/** The heaviest injured clip this frame (null when the layer is off; smokes). */
	const UAnimSequenceBase* GetInjuredDominantClip() const;
	/** The direction the injured walk plays, deg clockwise from the body's forward (smokes). */
	float GetInjuredDirection() const { return InjuredDirection.Current; }
	/** The injured loops' play rate this frame (the lead loop's; smokes / logs). */
	float GetInjuredPlayRate() const { return InjuredPlayRate; }
	/** The injured set is loaded for this body (its skeleton has retargeted copies). */
	bool HasInjuredSet() const { return InjuredSetData.StandIdle != nullptr; }
	/** The prone crawl layer plays the injured crawl set now (wounded, prone). */
	bool IsInjuredCrawlActive() const { return bInjuredCrawlActive; }

	// --- The shotgun weapon layer (the D-1, designer decision 21, 2026-11-03; WeaponAnimLayerRules) ---------------------------------
	// The user's ShotgunLocomotionPack clips (Content/Data/Weapons/weapon_anim_sets.json "shotgun") blended natively over the ABP's
	// pose while a shotgun (EWeaponHandling::Shotgun) is in hands: idle / walk / jog / sprint / crouch by speed and direction, the aim
	// set while aiming; an upper-body montage (fire, reload, grenade, hit) owns the torso, so the layer is off then - it never drives the legs (the ABP's); a full-body
	// montage, cover, prone, ladders, vaults, carrying, death and knockdowns leave the ABP alone. No AnimGraph node.

	/** The layer's mask this frame (smokes, the HUD trace). */
	EWeaponLayerMask GetShotgunLayerMask() const { return ShotgunMask; }

	/** Smokes: turn the probe on, then read how far the layer moved the lower / upper-body bones from the ABP's pose last frame (cm + deg, local space). */
	bool bWeaponLayerProbe = false;
	float GetWeaponLayerLowerDeviation() const;
	float GetWeaponLayerUpperDeviation() const;

	/** The layer's current blend weight (0..1) on the upper body; an "upper" set's legs always come from the ABP (designer 2026-11-09). */
	float GetShotgunUpperWeight() const { return ShotgunUpperWeight; }
	/** The layer's weight on the pelvis and legs (0..1): a "full" set only (the launcher, designer 2026-11-09 b), else 0. */
	float GetShotgunLowerWeight() const { return ShotgunLowerWeight; }
	/** The share of that lower body given to the ABP's own legs while moving in a "legs_abp" gait (0 = the set's legs; the RPG-7 running / walking
	 * forward, the designer 2026-11-10). */
	float GetWeaponLayerLegsAbpWeight() const { return ShotgunLegsAbpWeight; }
	/** The set in hands drives the whole body ("body": "full" in weapon_anim_sets.json). */
	bool IsWeaponLayerFullBody() const { return bShotgunWeapon && ActiveWeaponSet().bFullBody && !(bIsCrouching && ActiveWeaponSet().bCrouchUpper); }
	/** The layer's clips this frame (sequence, time, weight) and its shared loop phase 0..1 (smokes / diagnostics). */
	const TStaticArray<FOperativeClipState, 16>& GetWeaponLayerClips() const { return ShotgunClips; }
	float GetWeaponLayerPhase() const { return ShotgunPhase; }
	/** The turn-in-place clip's share this frame (a full-body set; smokes). */
	float GetWeaponLayerTurnShare() const { return LayerTurnShare; }
	/** The layer gives the body to the ABP's own locomotion now (crouched with a "crouch_body": "abp" set, the cover mechanics; smokes). */
	bool IsWeaponLayerAbpHandoff() const { return bLayerAbpHandoff; }

	/** The heaviest shotgun locomotion clip this frame (smokes: "a known shotgun clip plays"). */
	const UAnimSequenceBase* GetShotgunDominantClip() const;

	/** The shotgun clip set loaded (every clip path resolved: the count). */
	int32 GetShotgunClipCount() const { return ShotgunClipRefs.Num(); }

	/** Shotgun fire / per-shell reload clips started (smokes). */
	int32 GetShotgunFireClips() const { return ShotgunFireClips; }
	int32 GetShotgunShellClips() const { return ShotgunShellClips; }
	/** The folder the shotgun clips come from for this skeleton (empty: the pack's own clips). */
	const FString& GetShotgunRetargetFolder() const
	{
		return LayerHandling == EWeaponHandling::Pistol ? PistolRetargetFolder : (LayerHandling == EWeaponHandling::Launcher ? LauncherRetargetFolder : ShotgunRetargetFolder);
	}
	/** The handling whose set the weapon layer plays (Shotgun / Pistol; Standard = none). */
	EWeaponHandling GetWeaponLayerHandling() const { return LayerHandling; }
	/** Equip clips played when a layer weapon came into the hands (the pistol draw; for the smokes). */
	int32 GetWeaponEquipClips() const { return WeaponEquipClips; }
	/** The weapon layer's reload clip weight on the upper body (0..1; the D-1 per shell, the pistol's magazine; for the smokes). */
	float GetWeaponReloadWeight() const { return ShotgunReloadWeight; }

	// --- The launcher set (the RPG-7 on the game designer's launcher pack, 2026-11-09; weapon_anim_sets.json "launcher") ----------------
	/** The launcher's action clip now (the shot / the reload / none) and its weight on the upper body (smokes). */
	ELauncherAction GetLauncherAction() const { return LauncherAction; }
	float GetLauncherActionWeight() const { return LauncherActionWeight; }
	const UAnimSequenceBase* GetLauncherActionClip() const { return LauncherActionClip; }
	/** The launcher's action clip time now, seconds (smokes). */
	float GetLauncherActionTime() const { return LauncherActionTime; }
	/** The sum of the aim poses' weights this frame (x the aim offset's alpha; 0 = level, no target; any set with aim poses - the launcher, the
	 * shotgun; smokes). */
	float GetLauncherAimWeightSum() const;
	/** The aim pose weights this frame (EWeaponAimPose order; smokes). */
	const TStaticArray<float, 9>& GetLauncherAimWeights() const { return LauncherAimWeights; }
	/** Launcher shot clips started (smokes). */
	int32 GetLauncherShotClips() const { return LauncherShotClips; }
	/** The launcher set's hit / death clips played (smokes). */
	int32 GetLauncherHitClips() const { return LauncherHitClips; }
	int32 GetLauncherDeathClips() const { return LauncherDeathClips; }

	/** The grenade throw clip is still playing (the arms throw: no rifle shot meanwhile, user rule 2026-10-07). */
	bool IsThrowingGrenade() const { return GrenadeThrowSecondsLeft > 0.f; }
	/** In the fire-ready pose (entered, not yet left through the exit transition). */
	bool IsInCoverFirePose() const { return bCoverInFirePose; }

	// --- Weather idle poses (user request 2026-10-12; Survival/WarmIdleRules, UWarmIdleSubsystem decides the pose, this layer plays
	// it on FullBodySlot - no graph change). Exploration only; cosmetic: any move / order / threat / knockdown ends it at once. The
	// sets are filled by Scripts/Editor/setup_operative_weather_animation.py (clips retargeted onto the operative / female skeletons). ---

	/** Play the weather poses (untick to leave the graph's idle on). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	bool bUseWarmIdleClips = true;

	/** Cold idle (Cold_1): shivering while cold and idle out of any warm zone. Male (AS_He_*, recruits too) / female (AS_She_*). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet ColdIdleMale;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet ColdIdleFemale;

	/** Standing hand warming (Cold_Fire_1): a burning barrel, the outer ring at a campfire / stove. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet HandWarmMale;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet HandWarmFemale;

	/** Sitting at the fire (Cold_Fire_2): the inner ring at a campfire / stove. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet SitWarmMale;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	FWarmIdleClipSet SitWarmFemale;

	/** Each loop clip plays this many times (random in the range) before the transit to the other loop. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation", meta = (ClampMin = "1"))
	int32 WarmIdleLoopsMin = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation", meta = (ClampMin = "1"))
	int32 WarmIdleLoopsMax = 4;

	/** Blend into the start clip / between the clips of a set, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation", meta = (ClampMin = "0"))
	float WarmIdleBlendSeconds = 0.3f;

	/** Blend-out when an order / move / threat interrupts the pose (orders take priority), s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation", meta = (ClampMin = "0"))
	float WarmIdleInterruptBlendSeconds = 0.2f;

	/** Above this ground speed the pose is cut (the operative walks), cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation", meta = (ClampMin = "0"))
	float WarmIdleStillSpeed = 15.f;

	/** Hide the weapon in the hands while a weather pose plays (the clips use both hands). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	bool bHideWeaponInWarmIdle = true;

	/** The weather pose playing now (graph hook for the user's own states). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	EWarmIdlePose WarmIdlePose = EWarmIdlePose::None;

	/** A weather pose owns the FullBody slot now (start / loops / transits / end clip). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	bool bWarmIdleActive = false;

	/** The pose UWarmIdleSubsystem wants (None = end it; the layer ends it cleanly). */
	void SetWarmIdleRequest(EWarmIdlePose Pose, EWarmExitReason ExitReason = EWarmExitReason::NoLongerWanted);
	EWarmIdlePose GetWarmIdleRequest() const { return WarmIdleRequest; }

	/** The clip set of a pose for this body (female set for the Female Soldier, else male). */
	const FWarmIdleClipSet* GetWarmClipSet(EWarmIdlePose Pose) const;
	bool UsesFemaleWarmClips() const;

	/** Smokes: the step / clip playing, the clips started in order (capped). */
	EWarmClipStep GetWarmClipStep() const { return WarmStep; }
	UAnimSequenceBase* GetWarmClip() const { return WarmClip.Get(); }
	const TArray<FString>& GetWarmClipLog() const { return WarmClipLog; }

	/**
	 * Game designer 2026-10-15 ("so it isn't monotonous"): variant clips mixed into the cold idle (both bodies), played instead of a
	 * loop clip now and then (tactical_heat.json cold_idle_variant_chance / _cooldown_seconds / _loops, WarmIdleRules::ShouldPlayColdVariant).
	 * Empty = the user's Polygonmaker /Game/Polygonmaker/Animations/Interactions/UE4/Anim_Regular_idle_cold (UE4 mannequin rig, played
	 * by bone name) is loaded at start. Sequences only (a montage asset is skipped).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Weather Animation")
	TArray<TObjectPtr<UAnimSequenceBase>> ColdIdleVariants;

	/** Smokes: cold idle variants started. */
	int32 GetColdVariantsPlayed() const { return ColdVariantsPlayed; }

	// --- Interaction clips (game designer 2026-10-15; OperativeAnimInstanceInteraction.cpp, InteractionAnimRules). A crate is opened
	// (chest open, then the loot window), searched after Take / Take All (chest search), a disarmed charge picked up (pickup ground).
	// Standing: the FullBody montage; crouched: the UpperBody montage (the graph's layered blend from spine_01 - the legs keep the
	// crouch); prone: nothing yet. Cosmetic: any movement, knockdown, death, cover, vault or carrying cuts it. No graph change. ---

	/** Play the interaction clips (untick: none, the loot window keeps the crate's OpenSeconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	bool bUseInteractionClips = true;

	/**
	 * Empty montages = the Claude-owned /Game/Characters/Operatives/Anims/Interactions/AM_Interact_ChestOpen(_Upper) loaded at start.
	 * EffectFraction 0.9 (2.7 s of 3.0): the loot window opens when the lid is up and he looks in, at the end of the clip.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	FOperativeInteractionClip ChestOpenClip = FOperativeInteractionClip(0.f, 0.9f);

	/** Empty = AM_Interact_ChestSearch(_Upper). EffectFraction 0.5 (1.6 s of 3.17): the grab - the items move into his kit then. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	FOperativeInteractionClip ChestSearchClip = FOperativeInteractionClip(0.f, 0.5f);

	/**
	 * Empty = AM_Interact_PickupGround(_Upper). FullBodyStartSeconds 1.0: after the crouched disarm work the standing operative's
	 * clip starts low (pelvis 82 -> 64 cm at 1.4 s, the hands at the ground; measured by the setup script) and rises with the charge.
	 * EffectFraction 0.5 (1.43 s of 2.87): the hands close on the charge - the mine / the wire disappears then (rev 2026-10-19).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	FOperativeInteractionClip PickupGroundClip = FOperativeInteractionClip(1.f, 0.5f);

	/** Above this ground speed an interaction clip is cut (he walks), cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation", meta = (ClampMin = "0"))
	float InteractionStillSpeed = 20.f;

	/** Blend-out when a move / another layer cuts the clip, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation", meta = (ClampMin = "0"))
	float InteractionInterruptBlendSeconds = 0.15f;

	/** Hide the weapon in the hands while an interaction clip plays (the clips are unarmed, both hands). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	bool bHideWeaponInInteraction = true;

	/** The interaction clip playing now (graph hook) and where. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	EOperativeInteractionAnim InteractionAnim = EOperativeInteractionAnim::None;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Interaction Animation")
	EInteractionAnimLayer InteractionLayer = EInteractionAnimLayer::None;

	/**
	 * Plays Anim on the layer of ChoiceStance (InteractionAnimRules::ChooseLayer): standing full body, crouched upper body, prone none;
	 * never while time stands still (tactical pause). Returns the seconds until the clip ends (0 = nothing plays). A clip already
	 * playing restarts (rev 2026-10-19: every take is its own search, cause -> effect).
	 */
	float PlayInteraction(EOperativeInteractionAnim Anim, EOperativeStance ChoiceStance) { return PlayInteractionTimed(Anim, ChoiceStance).Seconds; }

	/** PlayInteraction with the effect moment (FOperativeInteractionClip::EffectFraction) of the clip that started. */
	FInteractionClipTiming PlayInteractionTimed(EOperativeInteractionAnim Anim, EOperativeStance ChoiceStance);

	/** Blend-out of Anim's montage, s. */
	float GetInteractionBlendOutSeconds(EOperativeInteractionAnim Anim) const;

	/** An interaction clip owns its layer now. */
	bool IsInteractionClipActive() const { return InteractionAnim != EOperativeInteractionAnim::None; }

	/** Smokes: "<clip>@<layer>" of every interaction clip started (capped). */
	const TArray<FString>& GetInteractionClipLog() const { return InteractionClipLog; }

	/** The operative anim instance of Actor's mesh, or nullptr. */
	static UOperativeAnimInstance* Of(const AActor* Actor);

	// --- Blizzard walk (game designer 2026-10-22; Characters/BlizzardWalkRules, OperativeAnimInstanceBlizzardWalk.cpp). Outdoors in a
	// blizzard a walking / jogging operative shields himself against the wind: the pack's Storm_1 loops (arms stretched out in front of
	// the face) play on UpperBodySlot (the graph's layered blend from spine_01), the legs keep the locomotion. No graph change.

	/** Play the storm upper body while walking in a blizzard (untick: never). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	bool bUseBlizzardWalk = true;

	/**
	 * The storm upper-body clips (LoopA / TransitAB / LoopB / TransitBA; Start / End unused), male (AS_He_*, recruits too) / female.
	 * Empty = the Claude-owned retargeted copies /Game/Characters/Operatives/Anims/Weather/Storm/{Male,Female}/AS_*_Storm_1_* loaded at
	 * start (Scripts/Editor/setup_operative_storm_animation.py).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	FWarmIdleClipSet BlizzardWalkMale;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	FWarmIdleClipSet BlizzardWalkFemale;

	/** Blend in / out of the storm upper body, s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "0"))
	float BlizzardWalkBlendSeconds = 0.3f;

	/** Blend-out when the rifle is needed (aim, a shot, reload, hit, grenade, cover...), s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "0"))
	float BlizzardWalkInterruptBlendSeconds = 0.15f;

	/** Starts above this ground speed, cm/s; keeps it down to BlizzardWalkStopSpeed; off above BlizzardWalkMaxSpeed (a run / sprint). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "0"))
	float BlizzardWalkStartSpeed = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "0"))
	float BlizzardWalkStopSpeed = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "0"))
	float BlizzardWalkMaxSpeed = 450.f;

	/** Loops of a loop clip before the transit to the other one (random in the range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "1"))
	int32 BlizzardWalkLoopsMin = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk", meta = (ClampMin = "1"))
	int32 BlizzardWalkLoopsMax = 4;

	/** Hide the weapon while the storm pose holds both hands in front of the face (it shows again as the pose blends out).
	 *  Default off (game designer 2026-10-26): the rifle stays in the right hand and he shields his face with it in hand. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	bool bHideWeaponInBlizzardWalk = false;

	/** The storm upper body plays (graph hook). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	bool bBlizzardWalkActive = false;

	/** 0..1 blizzard share of the current weather (BlizzardWalkRules::GetBlizzardAmount). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Blizzard Walk")
	float BlizzardAmount = 0.f;

	/** Why the layer is off now (smokes / logs). */
	EBlizzardWalkBlock GetBlizzardWalkBlock() const { return BlizzardWalkBlock; }
	/** The storm montages' blend weight on UpperBodySlot now (0..1; the layer's real weight, smokes). */
	float GetBlizzardWalkWeight() const;
	/** The storm clip playing now (nullptr = none). */
	UAnimSequenceBase* GetBlizzardWalkClip() const { return BlizzardWalkClip.Get(); }
	/** Indoors (a Closed / Shelter zone or a heat source's warm zone) at the last check. */
	bool IsBlizzardSheltered() const { return bBlizzardSheltered; }
	/** The storm clip set for this body (female / male, either as fallback), or nullptr. */
	const FWarmIdleClipSet* GetBlizzardWalkClipSet() const;
	/** Smokes: the loop a storm pose starts with (None = random, as in the game); the random pick made the pose checks flaky. */
	EWarmClipStep BlizzardWalkFirstStepForTesting = EWarmClipStep::None;
	/** Smokes: storm clips started (capped). */
	const TArray<FString>& GetBlizzardWalkClipLog() const { return BlizzardWalkClipLog; }

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

	/** The death clip / fall has started (smokes; fix/corpse-falls-to-ground: not while the body still falls). */
	bool HasDeathPlayed() const { return bDeathPlayed; }
	/** Killed off the ground: the death clip waits for the landing. */
	bool IsDeathDeferred() const { return bDeathDeferred; }
	/**
	 * The corpse pose holds (2026-11-09, UCorpseSubsystem freezes a hostile PMC body then): the death clip / fall has started (not deferred)
	 * and no montage is still advancing or blending (the held clip stops at its end).
	 */
	bool IsDeathPoseSettled() const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FOperativeAnimInstanceProxy;

	/** The shotgun set's clips, one array so the GC keeps them (WeaponAnimLayer data). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequenceBase>> ShotgunClipRefs;

	struct FShotgunAnimSet
	{
		UAnimSequence* StandIdle = nullptr;
		UAnimSequence* AimIdle = nullptr;
		UAnimSequence* CrouchIdle = nullptr;
		UAnimSequence* Sprint = nullptr;
		TStaticArray<UAnimSequence*, 8> Walk;
		TStaticArray<UAnimSequence*, 8> AimWalk;
		TStaticArray<UAnimSequence*, 8> Jog;
		TStaticArray<UAnimSequence*, 8> Crouch;
		UAnimSequenceBase* FireAim = nullptr;
		UAnimSequenceBase* FireHip = nullptr;
		UAnimSequenceBase* Reload = nullptr;
		float WalkSpeed = 150.f;
		float JogSpeed = 350.f;
		float CrouchSpeed = 120.f;
		float SprintSpeed = 550.f;
		float MoveStartSpeed = 15.f;
		float MoveFullSpeed = 60.f;
		float ShellStart = 0.f;
		float ShellEnd = 0.f;
		float OutroStart = 0.f;
		float ReloadBlendIn = 0.15f;
		float ReloadBlendOut = 0.12f;
		float BlendSeconds = 0.2f;
		/** The draw when the weapon comes into the hands (the pistol's equip clip; upper body). */
		UAnimSequenceBase* Equip = nullptr;
		// The launcher set (2026-11-09): crouched shot / reload, the nine aim poses (EWeaponAimPose order, the centre unused), hits, deaths.
		UAnimSequenceBase* FireCrouch = nullptr;
		UAnimSequenceBase* ReloadCrouch = nullptr;
		TStaticArray<UAnimSequence*, 9> AimPoses;
		/** The aim poses' common base ("aim_pose_base": the shotgun pack's single-frame front_C); null = each pose clip's own first frame. */
		UAnimSequence* AimPoseBase = nullptr;
		TArray<UAnimSequenceBase*> Hits;
		TArray<UAnimSequenceBase*> Deaths;
		float ReloadInsert = 0.f;
		float ReloadCrouchInsert = 0.f;
		float AimYawRange = 90.f;
		float AimPitchRange = 90.f;
		float FireBlendIn = 0.05f;
		float FireBlendOut = 0.15f;
		/** A skeleton with no retargeted copies may play the pack clips by bone name ("pack_playback"; false for the launcher's UE4 pack). */
		bool bPackPlayback = true;
		/** "body": "full" = the set drives the pelvis and legs too (the launcher), "upper" = spine_01 and below only (the shotgun, the pistol). */
		bool bFullBody = false;
		/** The loops' play rate cap ("max_play_rate"; a full-body set needs the real speed ratio, else the feet slide). */
		float MaxPlayRate = 1.6f;
		/** "root_translation": "reference" = a full set also drops the graph's root TRANSLATION (default "graph": only its rotation is dropped). */
		bool bRootReference = false;
		/** "crouch_body": "upper" = a full set drives only the upper body while crouched (the ABP's crouch legs; the launcher pack's crouch walk
		 * shuffles its feet: they slid 76 % of the speed). */
		bool bCrouchUpper = false;
		/** "legs_abp": ["run", "sprint", "walk", "walk_forward"] = a full set's gaits whose LOWER body is the ABP's locomotion (the M16 legs, as an
		 * "upper" set) - blended by those gaits' clip weight (WeaponAnimLayerRules::LegsAbpWeight; the RPG-7, the designer 2026-11-10). None = the
		 * set's own legs in every gait. */
		WeaponAnimLayerRules::FWeaponLegsAbpGaits LegsAbp;
		/** "legs_abp_forward_cone_deg": "walk_forward" counts fully within this many deg of straight ahead (WeaponAnimLayerRules::ForwardConeAngle). */
		float LegsAbpForwardConeDeg = 0.f;
		/** "legs_abp_run_speeds": [start, full] cm/s - the legs' own walk -> run band for "run" (unset = the clips' jog share: jog_blend_speeds). */
		float LegsAbpRunStartSpeed = 0.f;
		float LegsAbpRunFullSpeed = 0.f;
		/** Per-loop sync offsets ("phase_offsets": clip name -> the share of the loop where its left heel strikes; none = 0). */
		TMap<FName, float> PhaseOffsets;
		/** The walk -> jog blend band ("jog_blend_speeds": [start, full] cm/s; unset = walk_speed .. jog_speed). */
		float JogBlendStartSpeed = 0.f;
		float JogBlendFullSpeed = 0.f;
		/** The set has its own aim-walk loops (else aiming walks with the walk / jog loops). */
		bool bHasAimWalk = false;
		/** Per-direction authored ground speeds of the loops (cm/s, 0 = the set's single speed): the play rate per direction. */
		TStaticArray<float, 8> WalkSpeeds = TStaticArray<float, 8>(InPlace, 0.f);
		TStaticArray<float, 8> AimWalkSpeeds = TStaticArray<float, 8>(InPlace, 0.f);
		TStaticArray<float, 8> JogSpeeds = TStaticArray<float, 8>(InPlace, 0.f);
		TStaticArray<float, 8> CrouchSpeeds = TStaticArray<float, 8>(InPlace, 0.f);
		/** Turn in place (a full-body set): the clips turn TurnClipDeg in the root; played by the body's own turn. */
		UAnimSequence* TurnLeft = nullptr;
		UAnimSequence* TurnRight = nullptr;
		UAnimSequence* CrouchTurnLeft = nullptr;
		UAnimSequence* CrouchTurnRight = nullptr;
		/** Turn in place while aiming (the shotgun pack's aim_turn_90_*); null = the plain turn clips. */
		UAnimSequence* AimTurnLeft = nullptr;
		UAnimSequence* AimTurnRight = nullptr;
		float TurnClipDeg = 90.f;
		/** "crouch_body": "abp" - crouched, the ABP's crouch locomotion takes the whole body (the D-1, game designer 2026-11-09); else the set's crouch. */
		bool bCrouchAbp = false;
		/** "cover_mode": "abp" - the cover approach's last CoverApproachCm and the open shot off the wall are the ABP's too (in cover every set is). */
		bool bCoverAbp = false;
		/** "torso_follows_forward": a full-body set's chest keeps the yaw of its gait's forward clip in every direction (StabilizeWeaponTorso). */
		bool bTorsoForward = false;
		/** The last this-many cm of a walk to a cover slot are the ABP's ("cover_approach_abp_cm"; WeaponLayerHandoffRules). */
		float CoverApproachCm = 700.f;
		/** A full-body set's directional legs follow the direction eased over this ("direction_smooth_s"; 0 = at once) with a dead zone
		 * ("direction_dead_zone_deg"): WeaponLayerHandoffRules::SmoothLayerDirection, FormationJitterSmoke. */
		float DirectionSmoothSeconds = 0.f;
		float DirectionDeadZoneDeg = 0.f;
		/** ... and a bigger change than this cross-fades the directional clips over DirectionFadeSeconds ("direction_snap_deg" /
		 * "direction_fade_s"; 0 = it sweeps through the clips between). */
		float DirectionSnapDeg = 0.f;
		float DirectionFadeSeconds = 0.f;
		// The injured set's own keys (2026-11-10, "injured"): the slow walk (a forward loop), the injured crawl + its idle, their authored speeds
		// (x stride_scale / crawl_stride_scale), the triggers, the slow walk's forward cone [full, zero] deg, the torso's share of the pelvis.
		UAnimSequence* SlowWalk = nullptr;
		UAnimSequence* InjuredCrawl = nullptr;
		UAnimSequence* InjuredCrawlIdle = nullptr;
		float SlowWalkSpeed = 60.f;
		/** The slow walk's own play-rate cap ("slow_max_play_rate"; <= 0 = max_play_rate): the freezing body walks ~2.2x its authored pace. */
		float SlowMaxPlayRate = 0.f;
		float InjuredCrawlSpeed = 20.f;
		InjuredLocomotionRules::FInjuredTriggers Triggers;
		float SlowConeFullDeg = 35.f;
		float SlowConeZeroDeg = 80.f;
		float TorsoPelvisShare = 0.f;
		FShotgunAnimSet()
		{
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Walk[Index] = AimWalk[Index] = Jog[Index] = Crouch[Index] = nullptr;
			}
			for (int32 Index = 0; Index < 9; ++Index)
			{
				AimPoses[Index] = nullptr;
			}
		}
	};
	void LoadShotgunAnimSet();
	void UpdateShotgunLayer(float DeltaSeconds);
	/** The shotgun layer may drive the body now (shotgun in hands, alive, up, not in cover / prone / on a ladder / vaulting / carrying). */
	bool IsShotgunLayerEligible() const;
	/** The shot clip of the shotgun (aim / hip) on the upper-body slot; false = not the shotgun set. */
	bool PlayShotgunFire();
	/** The per-shell reload (intro, the shell-insertion loop per shell, outro - natively on the upper body); true when the shotgun set handles
	 * the reload this frame. */
	bool UpdateShotgunReload(const AOperativeCharacter* Operative, float DeltaSeconds);
	/** The weapon sets of the layer: weapon_anim_sets.json "shotgun" / "pistol" (the pistol, 2026-11-08). */
	FShotgunAnimSet ShotgunSetData;
	FShotgunAnimSet PistolSetData;
	FShotgunAnimSet LauncherSetData;
	/** The set of the weapon in hands (the pistol's for EWeaponHandling::Pistol, the launcher's for Launcher, else the shotgun's). */
	const FShotgunAnimSet& ActiveWeaponSet() const
	{
		return LayerHandling == EWeaponHandling::Pistol ? PistolSetData : (LayerHandling == EWeaponHandling::Launcher ? LauncherSetData : ShotgunSetData);
	}
	FString LauncherRetargetFolder;
	/** The body skeleton the sets were loaded for: a body swapped after the anim instance started (the Medic-Sapper's member Blueprint / body
	 * mesh) reloads them, so it plays its own skeleton's copies. */
	TWeakObjectPtr<const USkeleton> LoadedSetsSkeleton;
	/**
	 * The launcher's upper-body action (the shot, then the reload fitted to the reload time after the shot's follow-through; natively, no
	 * montage - the layer keeps the torso and the aim offset). Returns true when the launcher set handles this frame's reload (no rifle reload
	 * montage then). Publishes the rocket's insertion share to the operative (AOperativeCharacter::LauncherRoundInsertFraction).
	 */
	bool UpdateLauncherAction(AOperativeCharacter* Operative, float DeltaSeconds);
	/** The launcher's shot clip (stance: standing / crouched) from its first frame. */
	bool PlayLauncherShot();
	/** The launcher set's hit / standing death clip, or null when it has none (OperativeAnimInstanceShotgun.cpp). */
	UAnimSequenceBase* PickLauncherHitClip();
	UAnimSequenceBase* PickLauncherDeathClip(const AOperativeCharacter& Operative) const;
	ELauncherAction LauncherAction = ELauncherAction::None;
	UAnimSequenceBase* LauncherShotClip = nullptr;
	float LauncherShotTime = -1.f;
	/** The reload in progress: its clip, the shot's follow-through it waits for, whether it was seen starting. */
	UAnimSequenceBase* LauncherReloadClip = nullptr;
	float LauncherReloadLeadIn = 0.f;
	bool bLauncherReloadActive = false;
	UAnimSequenceBase* LauncherActionClip = nullptr;
	float LauncherActionTime = 0.f;
	float LauncherActionWeight = 0.f;
	TStaticArray<float, 9> LauncherAimWeights = TStaticArray<float, 9>(InPlace, 0.f);
	int32 LauncherShotClips = 0;
	int32 LauncherHitClips = 0;
	mutable int32 LauncherDeathClips = 0;
	/** Reads one block of weapon_anim_sets.json into Set (clips by path, the skeleton's retargeted copies first). */
	void LoadWeaponAnimSet(const TCHAR* BlockKey, FShotgunAnimSet& ShotgunSet, FString& OutRetargetFolder);
	EWeaponHandling LayerHandling = EWeaponHandling::Standard;
	FString LayerWeaponId;
	int32 WeaponEquipClips = 0;
	FString PistolRetargetFolder;
	bool bShotgunSetLoaded = false;
	/** The retargeted-copy folder for this mesh's skeleton (weapon_anim_sets.json "retargeted"); empty = the pack clips play. */
	FString ShotgunRetargetFolder;
	bool bShotgunWeapon = false;
	EWeaponLayerMask ShotgunMask = EWeaponLayerMask::Off;
	float ShotgunUpperWeight = 0.f;
	float ShotgunLowerWeight = 0.f;
	/** The share of a full set's lower body handed to the ABP's legs ("legs_abp"; WeaponAnimLayerRules::LegsAbpWeight eased over blend_seconds). */
	float ShotgunLegsAbpWeight = 0.f;
	/** Turn in place of a full-body set: the clip, its share, its time, the body's yaw last frame. */
	UAnimSequence* LayerTurnClip = nullptr;
	float LayerTurnShare = 0.f;
	float LayerTurnTime = 0.f;
	/** The layer hands the body to the ABP this frame (crouched "abp" / the cover mechanics; WeaponLayerHandoffRules::DefersToAbp). */
	bool bLayerAbpHandoff = false;
	/** The direction the layer's directional clips follow (WeaponLayerHandoffRules::StepLayerDirection of Direction; a full-body set). */
	FWeaponLayerDirectionState LayerDirection;
	/** The torso reference this frame (the gait's forward clip at the phase) and its share (FOperativeAnimInstanceProxy::StabilizeWeaponTorso). */
	UAnimSequence* LayerTorsoRef = nullptr;
	float LayerTorsoRefTime = 0.f;
	float LayerTorsoAlpha = 0.f;
	float LayerLastYaw = 0.f;
	bool bLayerYawValid = false;
	float ShotgunPhase = 0.f;
	float ShotgunIdleTime = 0.f;
	/** Eased shares (0..1, over blend_seconds) of the crouch / aim / sprint sets: a stance or aim change cross-fades the clip sets
	 * instead of switching them in one frame (a ~20 cm torso pop each time, RecoilProbe 2026-11-08). */
	float ShotgunCrouchShare = 0.f;
	float ShotgunAimShare = 0.f;
	float ShotgunSprintShare = 0.f;
	/** 16: a full-body set cross-fading its directional clips on a reversal plays two pairs per gait (FormationJitterSmoke 2026-11-09). */
	TStaticArray<FOperativeClipState, 16> ShotgunClips;
	int32 ShotgunReloadLastClip = -1;
	FShellReloadState ShotgunReload;
	float ShotgunReloadWeight = 0.f;
	/** An additive fire clip played natively over the layer (the pistol's recoil; a montage would make the layer give the arms back to the
	 * rifle pose for every shot): the clip and its time (< 0 = none). */
	UAnimSequenceBase* WeaponFireAdditiveClip = nullptr;
	float WeaponFireAdditiveTime = -1.f;
	int32 ShotgunFireClips = 0;
	int32 ShotgunShellClips = 0;

	// The prone crawl layer (OperativeAnimInstanceProneCrawl.cpp).
	/** The "prone_crawl" block: StandIdle = the crawl idle, Walk = the 8 in-place crawl loops, WalkSpeeds = their authored crawl speeds. */
	FShotgunAnimSet CrawlSetData;
	FString CrawlRetargetFolder;
	void UpdateProneCrawlLayer(float DeltaSeconds);
	/** The crawl layer may drive the body (prone, alive, up, out of cover, not on a ladder / vaulting / carrying / in the sniper pose / a stance clip). */
	bool IsProneCrawlEligible() const;
	float CrawlWeight = 0.f;
	float CrawlPhase = 0.f;
	float CrawlIdleTime = 0.f;
	float CrawlPlayRate = 0.f;
	FWeaponLayerDirectionState CrawlDirection;
	/** The crawl idle + two directional loops + the previous direction's two during a reversal's cross-fade. */
	TStaticArray<FOperativeClipState, 6> CrawlClips;
	int32 CrawlFireClipsStopped = 0;
	/** The crawl set this frame: the injured crawl's while bInjuredCrawlActive, else the prone crawl's. */
	const FShotgunAnimSet& ActiveCrawlSet() const { return bInjuredCrawlActive ? InjuredCrawlSetData : CrawlSetData; }

	// The injured locomotion layer (OperativeAnimInstanceInjured.cpp).
	/** The "injured" block: StandIdle = the injured idle, Walk = the 8 injured walk loops, SlowWalk / InjuredCrawl / InjuredCrawlIdle. */
	FShotgunAnimSet InjuredSetData;
	/** The prone crawl set with the injured crawl in its forward slot and its idle (built from CrawlSetData; empty without the clips). */
	FShotgunAnimSet InjuredCrawlSetData;
	FString InjuredRetargetFolder;
	bool bHasInjuredCrawl = false;
	/** Reads "injured" (after "prone_crawl": the injured crawl set is built on the crawl set). */
	void LoadInjuredAnimSet();
	/** Runs before the prone crawl layer (it decides which crawl set plays). */
	void UpdateInjuredLayer(float DeltaSeconds);
	/** The standing injured layer may drive the legs (standing, alive, up, out of cover, not on a ladder / vaulting / falling / carrying / in the
	 * sniper pose / a stance clip). */
	bool IsInjuredLayerEligible() const;
	InjuredLocomotionRules::EInjuredGait InjuredGait = InjuredLocomotionRules::EInjuredGait::None;
	/** The gait whose clips play (kept while the layer fades out after the gait ended). */
	InjuredLocomotionRules::EInjuredGait InjuredClipGait = InjuredLocomotionRules::EInjuredGait::None;
	bool bInjuredCrawlActive = false;
	float InjuredWeight = 0.f;
	float InjuredPhase = 0.f;
	float InjuredIdleTime = 0.f;
	float InjuredPlayRate = 0.f;
	float InjuredTorsoShare = 0.f;
	FWeaponLayerDirectionState InjuredDirection;
	/** The injured idle + two directional loops + the previous direction's two during a reversal's cross-fade (or the slow walk alone). */
	TStaticArray<FOperativeClipState, 6> InjuredClips;

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
	/** Killed off the ground (fix/corpse-falls-to-ground): the death clip waits until the body has landed (AOperativeCharacter::IsCorpseFalling). */
	bool bDeathDeferred = false;
	/** HandleDied called for the landed body: no second deferral. */
	bool bDeathLanded = false;
	/** The death clip for his stance (Godot play_death: a random standing one, the crouched / prone one); null = the knockdown fall. */
	UAnimSequenceBase* PickDeathClip(const AOperativeCharacter& Operative) const;
	/**
	 * Bug #44: dead, the clip that first held the FullBody slot (death clip / held fall) is the corpse pose; anything that later takes the
	 * slot (a cover exit, a stance clip) is replaced by it again, held on its last frame (DeathPoseRestores, a warning line).
	 */
	void UpdateDeathPose(const AOperativeCharacter& Operative, float DeltaSeconds);
	TWeakObjectPtr<UAnimSequenceBase> DeathPoseClip;
	float DeathPoseSeconds = 0.f;
	/** Hit reactions (HitReactionRules): game time of the last one, count / clip / slot for the smokes. */
	double LastHitReactionTime = -1000.0;
	int32 HitReactionsPlayed = 0;
	int32 FireClipsSkippedForStance = 0;
	TWeakObjectPtr<UAnimSequenceBase> LastHitReactionClip;
	FName LastHitReactionSlot;
	/** Vault clip: started on the vault's first frame, foot alternating. */
	void UpdateVaultClip(const AOperativeCharacter& Operative);
	bool bWasVaulting = false;
	/** Cover baseline: the loop (idle / shimmy) by stance, facing and direction; the enter clip on the first frame. */
	void UpdateCoverLayer(const AOperativeCharacter& Operative);
	UAnimSequenceBase* PickCoverClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) const;
	/** PickCoverClip for a given side (the fire pose's exit after the facing changed). */
	UAnimSequenceBase* PickCoverClipFor(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, ECoverFacing Facing) const;
	/** Shimmy loop: _L / _R = the movement direction (CoverFacingRules::ShimmyClipIndex), not the facing side. */
	UAnimSequenceBase* PickShimmyClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, bool bForward) const;
	/** Plays a cover one-shot on FullBodySlot (stops the loop), logs it; true when it started. */
	bool PlayCoverOneShot(UAnimSequenceBase* Clip, float BlendIn, float BlendOut);
	bool bWasInCover = false;
	/** The fire-ready pose is entered (enter transition played / fire-ready idle looping) until the exit transition plays. */
	bool bCoverInFirePose = false;
	/** The side the fire pose was played on (its exit clip uses it) and when the fire pose ended (the leave blend window). */
	ECoverFacing CoverFirePoseFacing = ECoverFacing::Right;
	double CoverFireLeftTime = -1.0e9;
	/** Within CoverFireLeaveBlendSeconds after the fire pose ended (the clips that follow blend in slowly). */
	bool IsLeavingCoverFirePose(double Now) const { return Now - CoverFireLeftTime < CoverFireLeaveBlendSeconds; }
	/** The fire pose ends now: its side's exit clip blends in over CoverFireLeaveBlendSeconds (UpdateCoverLayer). */
	bool PlayCoverFireExit(double Now);
	/** Blend-out time of the cover one-shot playing (the follow-up clip blends in over what is left of it). */
	float CoverOneShotBlendOut = 0.2f;
	/**
	 * This frame's hand-off from an ending cover one-shot to the next clip (>= 0: the blend time, the old one-shot is faded
	 * out over the same time so the FullBody slot stays fully covered; user PIE 2026-10-07: idle_to_fire blended out over
	 * 0.1 s while fire_idle blended in over 0.2 s - the slot dropped to 45 % and the graph's plain rifle idle, 70 cm back
	 * at the wall, flashed through: the "tuck back" blip after stepping out).
	 */
	float CoverHandoffBlend = -1.f;
	TWeakObjectPtr<UAnimMontage> CoverHandoffMontage;
	/** The blend-in for the next cover clip: the hand-off time when one is due this frame. */
	float TakeCoverHandoffBlend(float Default);
	/**
	 * Before a cover clip starts on FullBodySlot with BlendIn: every FullBody montage still playing fades out over
	 * BlendIn x 2 + 0.05 s - longer than the new one fades in, so the slot's total weight stays >= 1 (the slot normalises
	 * above 1). Measured 2026-10-07 (CornerEntrySmoke): equal in/out times dipped the slot to 0.45 mid-way and the graph's
	 * plain rifle idle - the body 70 cm back at the wall - flashed through every cover clip change.
	 */
	void FadeOutCoverMontages(float BlendIn);
	/**
	 * Plays a cover clip on FullBodySlot without the engine's "one montage per group" stop (that stop shortens every
	 * fade-out to the new clip's blend-in, which is what dipped the slot): the old ones fade out over FadeOutCoverMontages.
	 */
	UAnimMontage* PlayCoverMontage(UAnimSequenceBase* Clip, const FAlphaBlendArgs& BlendIn, float BlendOut, float PlayRate = 1.f, int32 LoopCount = 1,
		float StartSeconds = 0.f);
	/** Component-space root tracks of the switch clips (GetCoverSwitchTrack), sampled once per clip. */
	mutable TMap<TObjectKey<UAnimSequenceBase>, FCoverSwitchTrack> CoverSwitchTrackCache;
	FString LastCoverSwitchClip;
	float LastCoverSwitchStart = 0.f;
	/** The cover enter clip (from the open) is the one-shot playing: a shimmy ordered meanwhile cuts it short. */
	bool bCoverEnterPlaying = false;
	/** The cover was entered in this update (the stance switch clip yields to the enter clip). */
	bool bCoverEnteredThisFrame = false;
	/**
	 * The cover entry was a run-and-peek peek stance arrival: the fire-ready pose it wants is entered without the idle -> fire step-out, its
	 * fire idle blending in from the run (CoverPeekStanceBlendSeconds). Cleared once the fire idle plays, on leaving the cover, or after
	 * CoverPeekStanceWaitSeconds without the fire pose (then the ordinary cover loops / step-out go on).
	 */
	bool bCoverPeekStanceEntry = false;
	double CoverPeekStanceEntryTime = 0.0;
	static constexpr double CoverPeekStanceWaitSeconds = 0.5;
	int32 CoverPeekStanceBlends = 0;
	/** The peek stance's fire idle while it blends in from the run (GetCoverEnterBlendWeight reports its weight); reset by the next loop. */
	TWeakObjectPtr<UAnimMontage> CoverPeekStanceMontage;
	/** The peek stance yaw lock (BeginPeekStanceYawLock): its path (component space yaws, s) from BeginTime (world seconds). */
	struct FPeekStanceYawLockState
	{
		bool bActive = false;
		/** The approach lock: the yaws are world yaws, the path linear, held until released / replaced (BeginPeekApproachYawLock). */
		bool bWorld = false;
		/** From this many seconds in, the lock fades out (its path's end, or the moment it was released). */
		float HoldSeconds = 0.f;
		/** Its path is over and the pose's own facing has been compared with the prediction (SettlePeekStanceYawLock). */
		bool bSettled = false;
		/** Its fire idle (PeekStanceBiasSlot). */
		int32 BiasSlot = 0;
		float EntryYawCs = 0.f;
		float FinalYawCs = 0.f;
		float PathSeconds = 0.f;
		double BeginTime = 0.0;
	};
	FPeekStanceYawLockState PeekStanceYawLock;
	int32 PeekStanceYawLocks = 0;
	uint64 LastAnimUpdateFrame = 0;
	/** Seconds into the lock now (0 in the frame it began). */
	double PeekStanceYawLockSeconds() const;
	/**
	 * The lock's path is over (the fire idle fully in): its pose's own drawn yaw vs the predicted one - learnt into PeekStanceBodyBias and, when
	 * they differ, eased there over RunCornerPeekRules::SettleSeconds (NativeUpdateAnimation).
	 */
	void SettlePeekStanceYawLock();
	/**
	 * What the graph's layers add to each fire idle's drawn body yaw (the left-hand IK on the left shoulder...), learnt, deg: [stand _L, _R,
	 * crouch _L, _R] for a rifle, the same four for a shotgun (its left hand on the pump: no IK, no bias - main's verification 2026-11-08).
	 */
	float PeekStanceBodyBias[8] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	int32 PeekStanceBiasSlot(ECoverFacing Side, bool bCrouch) const;
	/** Left-hand IK: linear blend progress, the upper-body action windows (grenade throw / hit reaction) left, s. */
	float LeftHandIKLinear = 0.f;
	/** The chain root the cached LeftHandArmReach was measured from. */
	FName LeftHandArmReachRoot;
	float LeftHandIKBlockSeconds = 0.f;

	/** What is left of the grenade throw clip, s (IsThrowingGrenade). */
	float GrenadeThrowSecondsLeft = 0.f;
	/** Reads the grip socket into LeftHandIKOffset / Rotation and steps the alpha (game thread, UpdateState). */
	void UpdateLeftHandIK(const class AOperativeCharacter& Operative, float DeltaSeconds);
	/** AimPitch / AimOffsetAlpha from the operative's aim target (game thread, UpdateState). */
	void UpdateAimOffset(const class AOperativeCharacter& Operative, float DeltaSeconds);
	float AimOffsetLinear = 0.f;
	/** A shot waiting for the enter transition / the previous one-shot to end. */
	TWeakObjectPtr<UAnimSequenceBase> CoverPendingFireClip;
	TArray<FString> CoverClipLog;
	TWeakObjectPtr<UAnimSequenceBase> CoverLoopClip;
	TWeakObjectPtr<UAnimMontage> CoverLoopMontage;
	TWeakObjectPtr<UAnimMontage> CoverOneShotMontage;
	int32 CoverClipsPlayed = 0;
	bool bVaultLeftNext = true;
	int32 VaultClipsPlayed = 0;
	TWeakObjectPtr<UAnimMontage> VaultMontage;
	float AimTimer = 0.f;
	bool bWasReloading = false;
	/** Stance seen last update (transitions start on a change); unset until the first update. */
	TOptional<EOperativeStance> PreviousStance;
	TWeakObjectPtr<UAnimMontage> StanceTransitionMontage;
	float StateDeltaSeconds = 0.f;
	/** Smoothed speed and moving state driving the blend-space axes. */
	float LocomotionSpeed = 0.f;
	bool bLocomotionMoving = false;

	// Sniper layer (OperativeAnimInstanceSniper.cpp).
	void UpdateSniperLayer(const AOperativeCharacter& Operative);
	UAnimSequenceBase* SniperClipFor(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, EOperativeStance InStance) const;
	/** Plays a sniper one-shot on FullBodySlot (the loop fades out), logs it. */
	UAnimMontage* PlaySniperOneShot(UAnimSequenceBase* Clip, float BlendIn, float BlendOut, float PlayRate = 1.f);
	/** The kneel / prone / rise clip of a stance change with the sniper rifle (nullptr = the generic transition). */
	UAnimSequenceBase* PickSniperTransition(EOperativeStance From, EOperativeStance To) const;
	/** Magazine reload start in a sniper pose (true = handled, the upper-body reload is skipped). */
	bool StartSniperReload(const AOperativeCharacter& Operative);
	/** Hit reaction in a sniper pose (true = handled). */
	bool PlaySniperHitReaction(EOperativeStance InStance);
	TWeakObjectPtr<UAnimMontage> SniperLoopMontage;
	TWeakObjectPtr<UAnimSequenceBase> SniperLoopClip;
	TWeakObjectPtr<UAnimMontage> SniperOneShotMontage;
	/** Blend-out of the sniper one-shot playing (the next clip takes over before it starts). */
	float SniperOneShotBlendOut = 0.2f;
	bool bSniperFireQueued = false;
	/** The queued shot (behind a kneel clip) works the bolt after it. */
	bool bSniperQueuedFireBolt = false;
	bool bSniperBoltQueued = false;
	/** The stance of the shot the queued bolt follows (a stance change drops it). */
	EOperativeStance SniperBoltStance = EOperativeStance::Standing;
	bool bSniperBoltPlaying = false;
	bool bSniperWasPosed = false;
	bool bSniperTransitionPlaying = false;
	TArray<FString> SniperClipLog;

	// Weather layer (OperativeAnimInstanceWarmIdle.cpp).
	void UpdateWarmIdleLayer(const AOperativeCharacter& Operative);
	/** Plays one clip of the active set on FullBodySlot (the previous one fades out under it), logs it. */
	bool PlayWarmClip(EWarmClipStep Step, float BlendIn);
	/** Ends the active pose with Style (End clip / blend-out / cut). */
	void EndWarmPose(EWarmExitStyle Style);
	void ResetWarmState();
	/** Hides / shows the weapon in the hands for a weather pose or an interaction clip (restores what was visible). */
	void SetWarmWeaponHidden(bool bHidden);
	EWarmIdlePose WarmIdleRequest = EWarmIdlePose::None;
	/** Cold idle variant: the loop step to go back to after it, the game time of the pose start / the last variant, the count. */
	EWarmClipStep WarmVariantReturn = EWarmClipStep::LoopA;
	double LastColdVariantTime = 0.0;
	int32 ColdVariantsPlayed = 0;

	// Interaction layer (OperativeAnimInstanceInteraction.cpp).
	/** Loads the Claude-owned montages / the cold variant clip into empty fields (NativeInitializeAnimation). */
	void LoadDefaultInteractionClips();
	/** Cuts the clip on movement / another layer taking the body; resets the state once it ended. */
	void UpdateInteractionLayer(const AOperativeCharacter& Operative);
	const FOperativeInteractionClip* GetInteractionClip(EOperativeInteractionAnim Anim) const;
	void ResetInteractionState();
	TWeakObjectPtr<UAnimMontage> InteractionMontage;
	TArray<FString> InteractionClipLog;
	EWarmExitReason WarmExitReason = EWarmExitReason::NoLongerWanted;
	/** The pose whose set plays (stays set through its End clip). */
	EWarmIdlePose WarmActivePose = EWarmIdlePose::None;
	EWarmClipStep WarmStep = EWarmClipStep::None;
	int32 WarmLoopsDone = 0;
	int32 WarmLoopsTarget = 2;
	TWeakObjectPtr<UAnimMontage> WarmMontage;
	TWeakObjectPtr<UAnimSequenceBase> WarmClip;
	TArray<FString> WarmClipLog;
	bool bWarmWeaponHidden = false;
	bool bWarmWeaponWasVisible = true;
	TOptional<bool> bWarmFemaleCached;

	// Blizzard walk layer (OperativeAnimInstanceBlizzardWalk.cpp).
	/** Loads the Claude-owned storm copies into empty clip sets (NativeInitializeAnimation). */
	void LoadDefaultBlizzardWalkClips();
	void UpdateBlizzardWalkLayer(const AOperativeCharacter& Operative);
	bool PlayBlizzardWalkClip(EWarmClipStep Step, float BlendIn);
	void StopBlizzardWalk(float BlendOut);
	/** Another montage than the storm one on UpperBodySlot (fire, reload, hit, grenade, interaction, working device). */
	bool HasOtherUpperBodyMontage() const;
	EBlizzardWalkBlock BlizzardWalkBlock = EBlizzardWalkBlock::NoBlizzard;
	EWarmClipStep BlizzardWalkStep = EWarmClipStep::None;
	int32 BlizzardWalkLoopsDone = 0;
	int32 BlizzardWalkLoopsTarget = 2;
	TWeakObjectPtr<UAnimMontage> BlizzardWalkMontage;
	TWeakObjectPtr<UAnimSequenceBase> BlizzardWalkClip;
	TArray<FString> BlizzardWalkClipLog;
	/** The shelter check is a zone scan: every BlizzardShelterCheckSeconds of game time, not every frame. */
	bool bBlizzardSheltered = false;
	double BlizzardShelterCheckTime = -1.0;
	/** The weapon is hidden for the storm pose. */
	bool bBlizzardWeaponHidden = false;

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
