#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanAIRules.h"
#include "MarksmanEnemyCharacter.generated.h"

class AOperativeCharacter;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * Marksman enemy: a scoped-rifle tactician (UE-only archetype, no Godot reference; Gemini's spec in
 * docs/port/TANDEM.md, request 3). Patrols PatrolRoute until he sees an operative; holds 20-35 m, retreats at a sprint
 * under 12 m, flanks (45-90 deg off the target's facing) a target camping in hard cover, goes prone on open / high
 * ground and crouches behind low cover. Every shot follows a 2 s telegraphed aim (beam) that breaks without a line of
 * fire. A hit from afar while unaware drops him prone (ambush), alerts nearby marksmen, then he relocates.
 * Sprint 11 (TANDEM «SPRINT 11 DIRECTIVE»): with AssignedPatrolRoute set he walks that spline route from map start instead
 * of PatrolRoute (the legacy points stay the fallback) and breaks into Engage when he sees an operative (Sprint 08 sight),
 * is hit (cover, laser, return fire), his escort is alerted or a trap goes off within 20 m.
 * Rules: MarksmanAIRules (tests CodexTactics.Marksman.*), PatrolRouteRules (CodexTactics.AI.PatrolRoute.*).
 */
UCLASS()
class CODEXTACTICS_API AMarksmanEnemyCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	AMarksmanEnemyCharacter();

	virtual void BeginPlay() override;

	/**
	 * Legacy patrol waypoints relative to the actor (edited in the level); empty = he hunts the squad from the start.
	 * Used only while AssignedPatrolRoute is null (user decision 2026-10-06: maps with these points keep working).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Marksman", meta = (MakeEditWidget = true))
	TArray<FVector> PatrolRoute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Marksman")
	FMarksmanConfig MarksmanConfig;

	/** Aim telegraph material (Gemini's M_SniperScope_Beam when present; scalar "AimProgress" 0..1 is set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Marksman")
	TSoftObjectPtr<UMaterialInterface> BeamMaterial;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Marksman")
	bool IsAimingAtTarget() const { return bIsAimingAtTarget; }

	/** 0..1 of the current aim. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Marksman")
	float GetAimProgress() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Marksman")
	EMarksmanAIState GetAIState() const { return AIState; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Marksman")
	EOperativeStance GetStance() const { return Stance; }

	/** Shots fired / hits landed (smokes, stats). */
	int32 GetShotsFired() const { return ShotsFired; }

	/** Sets the stance and the capsule height (prone = 1/3), keeping the feet on the ground. */
	void SetMarksmanStance(EOperativeStance NewStance);

	/** Line of fire from his scope to Target right now (smokes / debugging). */
	bool HasLineOfFireTo(const AOperativeCharacter* Target) const;

	/** Patrol -> Engage (another marksman's ambush alert, smokes). */
	void Alert();

	/** Smokes (Sprint 12 CoverSmoke): his laser rests on Target now (bIsAimingAtTarget); freeze him so the tick keeps it. */
	void ForceAimForTesting(AActor* Target) { SetCurrentTargetForTesting(Target); StartAim(); }

	/** A spline route puts him (back) on patrol: aim and hold dropped, AIState Patrol. */
	virtual void StartPatrol(APatrolRouteActor* Route, AEnemyCharacter* Leader) override;
	virtual bool IsOnPatrol() const override;
	virtual void BreakPatrol(EPatrolAlertCause Cause, const FVector& AlertLocation) override;

protected:
	virtual void TickBehavior(float DeltaTime) override;
	/** Walks to a spline waypoint standing (his own move: stance, MoveGoal), at Speed. */
	virtual void IssuePatrolMove(const FVector& Goal, float Speed) override;
	/** Turn-based hold: cancels a pending get-up run and the aim (bug fix 2026-10-06). */
	virtual void OnTurnBasedHeldChanged(bool bHeld) override;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Marksman")
	TObjectPtr<UStaticMeshComponent> AimBeam;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterialInstance;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman")
	bool bIsAimingAtTarget = false;

private:
	UFUNCTION()
	void HandleMarksmanDamaged(const FDamageSpec& Spec, float FinalDamage);
	/** One log line per death for the AI coach: the range to the closest operative. */
	UFUNCTION()
	void HandleMarksmanDied(AActor* Victim, const FString& AttackerSource);

	/** Line of fire from the scope (stance height) to the target's stance height; Cover < 1 behind a barricade. */
	struct FMarksmanLine
	{
		bool bHasLos = false;
		bool bTargetInCover = false;
		float Cover = 1.f;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};
	FMarksmanLine TraceLine(const AOperativeCharacter* Target) const;
	bool HasLowCoverTowards(const AActor* Target) const;
	FVector GetFeet() const;

	AOperativeCharacter* FindClosestOperative(float& OutDistance) const;
	/** Centre of the living squad (Fallback without one). */
	FVector GetSquadCentroid(const FVector& Fallback) const;
	/** Seconds the advance has made no progress (a partial path ended at an obstacle): then he flanks round it. */
	float ApproachStallTime = 0.f;
	float ApproachBestDistance = TNumericLimits<float>::Max();
	/**
	 * Sprint 06-D: a reachable point 20-33 m from Target with a line of fire from the scope (a ring of samples, the
	 * shortest full navmesh path wins). False: none found.
	 */
	bool FindFiringPosition(const AOperativeCharacter* Target, FVector& OutPosition) const;
	FVector FiringPosition = FVector::ZeroVector;
	bool bHasFiringPosition = false;
	float FiringSearchCooldown = 0.f;
	void TickPatrol(float DeltaTime, AOperativeCharacter* Target, float Distance);
	void TickEngage(float DeltaTime, AOperativeCharacter* Target, float Distance);
	/**
	 * Sprint 06-H: from prone he first stands up (RiseDelay, movement stopped) and only then runs — no sliding on his
	 * stomach. Standing or crouching he moves at once.
	 */
	void MoveTo(const FVector& Goal, bool bSprint);
	void ExecuteMoveTo(const FVector& Goal, bool bSprint);
	void FinishRise();
	FTimerHandle RiseTimerHandle;
	bool bPendingSprint = false;
	/** Seconds from prone to standing before the run starts. */
	static constexpr float RiseDelay = 0.45f;
	/** A wave (or its preparation) is on: no patrolling. */
	bool IsFightOn() const;
	/** World time of the last retreat / back-off (kiting cooldown). */
	float LastKiteTime = -1000.f;
	bool CanKite() const;
	/** Codex.Marksman.* console variables (the AI coach's experiments, -dpcvars=) override MarksmanConfig at BeginPlay. */
	void ApplyTuningOverrides();
	/** Sprint 06-G: the operative who shot him; he answers that one (not the closest) for RetaliationSeconds. */
	TWeakObjectPtr<AOperativeCharacter> RetaliationTarget;
	float RetaliationTime = 0.f;
	static constexpr float RetaliationSeconds = 6.f;
	void StartRetreat(const AOperativeCharacter* Target);
	void StartFlank(const AOperativeCharacter* Target);
	void StartAim();
	void CancelAim();
	void Fire(AOperativeCharacter* Target, const FMarksmanLine& Line, float Distance);
	void UpdateBeam(const FMarksmanLine& Line);

	EMarksmanAIState AIState = EMarksmanAIState::Patrol;
	EOperativeStance Stance = EOperativeStance::Standing;
	int32 PatrolIndex = 0;
	float StateTimer = 0.f;
	float AimTimer = 0.f;
	float CampTimer = 0.f;
	FVector MoveGoal = FVector::ZeroVector;
	FVector SpawnLocation = FVector::ZeroVector;
	bool bHolding = false;
	/** Capsule radius while standing (the prone capsule narrows to its half height). */
	float StandRadius = 0.f;
	int32 ShotsFired = 0;
};
