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
 * During a drop / knockback fall the operative plays the mannequin rifle fall loop and the land clip on the full-body slot (the ABP has no
 * falling state; root motion is ignored while they play). UE-only (no Godot reference).
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
	/** The fall loop clip played in the last fall (smokes / shots). */
	bool PlayedFallClip() const { return bPlayedFallClip; }

private:
	ACharacter* GetCharacter() const;
	float GetHalfHeight() const;
	float FeetZ() const;
	float Gravity() const;
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
};
