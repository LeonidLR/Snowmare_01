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
 * Rules: MarksmanAIRules (tests CodexTactics.Marksman.*).
 */
UCLASS()
class CODEXTACTICS_API AMarksmanEnemyCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	AMarksmanEnemyCharacter();

	virtual void BeginPlay() override;

	/** Patrol waypoints relative to the actor (edited in the level); empty = he hunts the squad from the start. */
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

	/** Patrol -> Engage (another marksman's ambush alert, smokes). */
	void Alert();

protected:
	virtual void TickBehavior(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Marksman")
	TObjectPtr<UStaticMeshComponent> AimBeam;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeamMaterialInstance;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Marksman")
	bool bIsAimingAtTarget = false;

private:
	UFUNCTION()
	void HandleMarksmanDamaged(const FDamageSpec& Spec, float FinalDamage);

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
	void TickPatrol(float DeltaTime, AOperativeCharacter* Target, float Distance);
	void TickEngage(float DeltaTime, AOperativeCharacter* Target, float Distance);
	void MoveTo(const FVector& Goal, bool bSprint);
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
