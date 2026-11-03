#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/KnockdownTypes.h"
#include "Engine/EngineTypes.h"
#include "Falls/FallRules.h"
#include "LedgeFallComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UAnimSequenceBase;

/** Where a body is in a drop / fall (smokes, the HUD). */
UENUM()
enum class ELedgeFallState : uint8
{
	None,
	/** At a drop link whose landing spot another man's drop holds (one at a time), or standing up first. */
	Waiting,
	/** Running off the edge at the planned speed (FallRules::PlanTakeoff). */
	RunUp,
	/** In the air (a drop, a knockback throw): the horizontal speed is kept, gravity does the rest. */
	Airborne,
	/** Landed: the land clip, the fall damage done; the path goes on after land.settle_s. */
	Landed
};

/** Why the last fall happened (smokes / logs). */
UENUM()
enum class ELedgeFallKind : uint8
{
	None,
	/** A generated drop link of his path (a move order off an edge). */
	DropLink,
	/** A knockdown threw him over an edge. */
	Knockback,
	/** Any other fall (pushed off, a hole link of the level generator, ...): only the landing is judged. */
	Other
};

/**
 * Falls from heights, running drops and knockback falls of one body (game designer items 6 / 10 / 11a, 2026-11-03; docs/systems/
 * stances-and-movement.md "Falls and drops"; pure rules FallRules). On AOperativeCharacter and AEnemyCharacter (default subobject).
 *  - A move order's path through a generated drop link (UDropLinkSubsystem) hands him over here at the link: he runs off the edge at his
 *    approach speed (sped up to what the gap needs, checked so he does not sail past a narrow lower roof, never faster than his sprint),
 *    falls KEEPING that speed (no air control), lands, takes the fall damage (FallRules::FallDamage, the ladder curve, one level free) and
 *    the path goes on to the order's destination. One man at a time per landing spot; turn-based combat refuses the drop.
 *  - A knockdown (UKnockdownComponent::StartKnockdown) near an open edge (no waist-high parapet) throws him over it: he falls, lands with the
 *    fall damage, then the knockdown plays (operatives and enemies).
 *  - Any other fall (walking off by any other means): the landing is judged the same way.
 *  - The landing (user decision 2026-11-03, FallRules::JudgeFall): below 9 m the ladder curve, 9-12 m a heavy wound (60-80 HP, then he lies
 *    in the knockdown pose for its downed time and gets up), 12 m and more kills - operatives and enemies.
 *  - The animation: GetAnimPhase (Grounded / Falling / Landing) drives the operative ABP's "FallStates" state machine (UOperativeAnimInstance
 *    bFallAirborne / bFallLanding / bFallGrounded; the fall loop and the land clip are in the ABP). The old C++ full-body slot clips are a
 *    fallback behind Codex.Fall.SlotClips (an ABP without the state machine). An enemy body falls in its current pose.
 * UE-only (no Godot reference).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API ULedgeFallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULedgeFallComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The fall loop / land clips (empty = no clip: an enemy body falls in its current pose). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Falls")
	TSoftObjectPtr<UAnimSequenceBase> FallLoopClip;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Falls")
	TSoftObjectPtr<UAnimSequenceBase> LandClip;

	/** The anim graph's full-body slot (the ladder's / the knockdown's). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Falls")
	FName MontageSlot = TEXT("FullBody");

	/** His run-up cap times this (1 = his sprint; the smokes slow a man down to make him fall short). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Falls", meta = (ClampMin = "0"))
	float TakeoffSpeedScale = 1.f;

	// --- API ---

	/** His path reached drop link Index (the link's callback or this component's own polling). True when the drop takes him (or he waits). */
	bool NotifyDropLinkReached(int32 Index);

	/**
	 * A knockdown on Side from SourceLocation (bHasSource) at an open edge within reach throws him over it: the knockdown then plays on landing.
	 * False (nothing done) when no edge / a parapet / turn-based / already falling.
	 */
	bool TryKnockbackFall(EKnockdownDirection Side, EKnockdownCause Cause, bool bHasSource, const FVector& SourceLocation);

	/** A new order mid-drop: walked to after the landing (the drop is never cut short). */
	void HandleNewOrder(const FVector& Destination);

	/** The drop / fall owns the body (any state but None, or a link reached this frame). */
	bool IsBusy() const { return State != ELedgeFallState::None || PendingLink != INDEX_NONE; }
	/** In the air (a drop or a knockback throw). */
	bool IsAirborne() const { return State == ELedgeFallState::Airborne; }
	/** Running off the edge, in the air or settling after the landing (no fire, cover, facing or stance change meanwhile). */
	bool IsDropping() const { return State == ELedgeFallState::RunUp || State == ELedgeFallState::Airborne || State == ELedgeFallState::Landed; }
	/** Waiting at a drop link for his turn (a new order replaces the wait: CancelWait). */
	bool IsWaiting() const { return State == ELedgeFallState::Waiting; }
	void CancelWait();
	ELedgeFallState GetState() const { return State; }
	ELedgeFallKind GetKind() const { return Kind; }
	/** The drop link he is taking (INDEX_NONE outside a link drop). */
	int32 GetActiveLink() const { return ActiveLink; }
	/** The planned run-up of the current / last drop (cm/s) and whether it was planned to reach the lower surface. */
	float GetPlannedSpeed() const { return PlannedSpeed; }
	bool WasPlannedToReach() const { return bPlannedReach; }

	/** Where a save taken now puts him: the landing of the link he is dropping off; his location otherwise. */
	FVector GetSaveLocation() const;

	/** The run-up he would take off Link from where he is now, at his speed now (the HUD's marker: does he reach the lower surface?). */
	FallRules::FTakeoffPlan PredictDrop(const struct FDropLink& Link) const;

	/** His run-up cap: his sprint speed (wounds, cold, a load) for an operative, takeoff.max_speed_cms else; x TakeoffSpeedScale. */
	float GetTakeoffCap() const;

	// --- Stats (smokes) ---
	int32 GetDropCount() const { return DropCount; }
	int32 GetKnockbackFallCount() const { return KnockbackFallCount; }
	int32 GetLandingCount() const { return LandingCount; }
	float GetLastFallHeight() const { return LastFallHeight; }
	float GetLastFallDamage() const { return LastFallDamage; }
	ELedgeFallKind GetLastFallKind() const { return LastFallKind; }
	/** The last drop link's landing came down on its lower surface (feet within 60 cm of the link end's height). */
	bool DidLastDropLandOnTarget() const { return bLastLandedOnTarget; }
	FVector GetLastLandingLocation() const { return LastLandingLocation; }
	/** The highest horizontal speed of the last fall (cm/s): the run-up kept in the air. */
	float GetLastAirSpeed() const { return LastAirSpeed; }
	/** The fall loop clip played on the slot in the last fall (the Codex.Fall.SlotClips fallback; smokes / shots). */
	bool PlayedFallClip() const { return bPlayedFallClip; }
	/** The last fall reached the ABP's Falling phase (GetAnimPhase) - the operative ABP's fall state (smokes). */
	bool ReachedAnimFalling() const { return bLastFallAnimFalling; }
	/** The last landing's outcome band (FallRules::JudgeFall). */
	FallRules::EFallOutcome GetLastFallOutcome() const { return LastFallOutcome; }
	/** Heavy-wound landings (knocked down by the fall) and deadly ones (smokes). */
	int32 GetHeavyWoundCount() const { return HeavyWoundCount; }
	int32 GetLethalFallCount() const { return LethalFallCount; }

	// --- The soft-lock safety net (user bug 2026-11-03: the whole squad stuck after a roof-hole drop; FallRules rescue.*) ---

	/**
	 * Where he stands now: on a navmesh poly that reaches the main area, off the navmesh, or on an island he cannot walk out of (neither back to
	 * where his last fall started nor to a player start). bOffNavmeshOnly skips the dead-end paths (cheap).
	 */
	FallRules::ENavStanding EvaluateStanding(bool bOffNavmeshOnly = false) const;

	/**
	 * An operative standing off the navmesh (his order cannot start) is put on the nearest point that reaches the main area. True when moved;
	 * bResumeOrder: his own order is asked again from there (false: the caller does it).
	 */
	bool RescueIfOffNavmesh(const TCHAR* Why, bool bResumeOrder = true);

	/** Starts the watch after a fall (every landing does; the smokes too): rescue.after_s of a bad standing within rescue.watch_s -> the rescue. */
	void ArmRescueWatch();

	/** Rescues done (smokes) and the last one's spots. */
	int32 GetRescueCount() const { return RescueCount; }
	FVector GetLastRescueFrom() const { return LastRescueFrom; }
	FVector GetLastRescueTo() const { return LastRescueTo; }
	bool IsRescueWatchArmed() const { return RescueWatchTime >= 0.f; }

	/** The animation phase of a fall this frame (FallRules::FallAnimPhase): the operative ABP's FallStates machine follows it. */
	FallRules::EFallAnimPhase GetAnimPhase() const { return AnimPhase; }

private:
	ACharacter* GetCharacter() const;
	float GetHalfHeight() const;
	float FeetZ() const;
	/** The gravity of his ledge falls (the world's x air.gravity_scale): the run-up plan and the HUD prediction. */
	float Gravity() const;
	/** The world's gravity (cm/s2, positive). */
	float WorldGravity() const;
	/** The ledge fall's stronger gravity on his movement (air.gravity_scale) on / back to his own; idempotent. */
	void SetAirGravity(bool bOn);
	class UDropLinkSubsystem* GetLinks() const;

	void StartDrop();
	void EnterState(ELedgeFallState NewState);
	void TickWaiting(float DeltaTime);
	void TickRunUp(float DeltaTime);
	void TickAirborne(float DeltaTime);
	void TickLanded(float DeltaTime);
	void PollLinks(float DeltaTime);
	void BeginAirborne();
	void Land();
	void Finish(bool bResume);
	/** Gives the character movement back its own values (the walk-off flag, his speeds). */
	void RestoreMovement();
	/** Stops his path / AI move (an operative keeps his order's destination as the pending one). */
	void StopOwnerMove();
	void ResumePath();
	void Refuse(const FString& Line);

	void PlayFallLoop();
	void StopFallLoop(float BlendOut);
	void PlayLandClip();
	UAnimSequenceBase* LoadClip(const TSoftObjectPtr<UAnimSequenceBase>& Clip) const;
	void SetRootMotionIgnored(bool bIgnored);

	/** The slot clip fallback is on (Codex.Fall.SlotClips: 1 always, 2 when the ABP has no "FallStates" machine). */
	bool UseSlotClips() const;
	/** The landing's damage, floating text, counters; the knockdown of a heavy wound is returned (the caller starts it at a safe point). */
	FallRules::FFallVerdict ApplyLanding(ACharacter& Character, float Height);
	/** The ABP phase (GetAnimPhase) from his movement and this component's state. */
	void UpdateAnimPhase(float DeltaTime);
	/** A heavy-wound knockdown of a fall this component did not run (deferred out of the movement mode callback). */
	void StartFallKnockdown(EKnockdownDirection Side, EKnockdownCause Cause);

	/** The watch after a fall (State None): every 0.5 s while armed. */
	void TickRescueWatch(float DeltaTime);
	/** He may be moved by the rescue now: an operative alive, up, on the ground, on no ladder / vault / vehicle, no drop running. */
	bool CanBeRescued() const;
	/** Puts him on the nearest point that reaches the main area (FDropLinkReach) and lets his own order go on. True when moved. */
	bool RescueNow(FallRules::ENavStanding Standing, const TCHAR* Why, bool bResumeOrder = true);

	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	ELedgeFallState State = ELedgeFallState::None;
	ELedgeFallKind Kind = ELedgeFallKind::None;
	float StateTime = 0.f;

	/** A link reached inside the crowd update: the drop starts on the next tick. */
	int32 PendingLink = INDEX_NONE;
	int32 ActiveLink = INDEX_NONE;
	/** His horizontal speed when the link was reached (the momentum he brings). */
	float ApproachSpeed = 0.f;
	FVector RunDir = FVector::ForwardVector;
	float PlannedSpeed = 0.f;
	bool bPlannedReach = false;
	/** Feet height of the floor the fall started from (and the highest feet in the air). */
	float StartFeetZ = 0.f;
	float PeakFeetZ = 0.f;
	float RetryTimer = 0.f;
	/** The run-up's time limit for this drop (its edge distance at the planned speed). */
	float RunUpLimitSeconds = 1.5f;
	float PollTimer = 0.f;

	FVector PendingDestination = FVector::ZeroVector;
	bool bHasPendingDestination = false;
	bool bOwnOrderAtEntry = false;

	/** Knockback: the knockdown that plays on landing. */
	bool bKnockdownOnLanding = false;
	EKnockdownDirection KnockdownSide = EKnockdownDirection::Back;
	EKnockdownCause KnockdownCause = EKnockdownCause::None;

	/** The character movement values this component changes (restored in Finish). */
	float SavedMaxWalkSpeed = 0.f;
	bool bSavedWalkOffLedges = true;
	bool bMovementSaved = false;
	uint8 SavedRootMotionMode = 0;
	bool bRootMotionSaved = false;
	/** His movement's own GravityScale while the air gravity is on. */
	float SavedGravityScale = 1.f;
	bool bAirGravityOn = false;

	/** A fall this component did not start (walked / pushed off by other means): judged on landing. */
	bool bTrackingOtherFall = false;
	/** The knockdown played on landing must not throw him again. */
	bool bLandingKnockdown = false;

	TWeakObjectPtr<UAnimMontage> FallMontage;
	TWeakObjectPtr<UAnimMontage> LandMontage;

	int32 DropCount = 0;
	int32 KnockbackFallCount = 0;
	int32 LandingCount = 0;
	float LastFallHeight = 0.f;
	float LastFallDamage = 0.f;
	ELedgeFallKind LastFallKind = ELedgeFallKind::None;
	bool bLastLandedOnTarget = false;
	FVector LastLandingLocation = FVector::ZeroVector;
	float LastAirSpeed = 0.f;
	bool bPlayedFallClip = false;
	bool bLastFallAnimFalling = false;
	FallRules::EFallOutcome LastFallOutcome = FallRules::EFallOutcome::Unhurt;
	int32 HeavyWoundCount = 0;
	int32 LethalFallCount = 0;
	/** A heavy-wound knockdown of an "other" fall, started on the next tick. */
	bool bPendingFallKnockdown = false;

	/** Where his last fall started (feet; the dead-end check paths back there). */
	FVector FallStartFeet = FVector::ZeroVector;
	/** Seconds since the watched landing (-1 = no watch), the bad standing's seconds, the check clock. */
	float RescueWatchTime = -1.f;
	float RescueBadSeconds = 0.f;
	float RescueCheckClock = 0.f;
	/** The dead-end check is done (it found him fine once): only the cheap off-navmesh check for the rest of the watch. */
	bool bRescueDeadEndCleared = false;
	int32 RescueCount = 0;
	/** World seconds of the last rescue (rescue.cooldown_s; -1 = none yet). */
	double LastRescueTime = -1.0;
	FVector LastRescueFrom = FVector::ZeroVector;
	FVector LastRescueTo = FVector::ZeroVector;

	FallRules::EFallAnimPhase AnimPhase = FallRules::EFallAnimPhase::Grounded;
	/** Seconds the movement has been Falling (0 on the ground) and since the last Falling phase ended (-1 = long ago). */
	float FallingSeconds = 0.f;
	float SecondsSinceFallEnded = -1.f;
};
