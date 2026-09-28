#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RelocationSubsystem.generated.h"

class AInteractableActor;
class AOperativeCharacter;
class ARelocationGhostActor;

/**
 * Moving objects around the level («Переместить» / «Вытолкать»):
 *  1. placement: a ghost of the object follows the cursor (wheel / R rotate 45°, LMB confirm, RMB cancel),
 *     cyan inside the allowed radius, red outside;
 *  2. task: the worker walks to the object, braces against it and pushes it (carry speed) to the new spot,
 *     then sets it down and steps back.
 * In the tactical pause the move is planned and runs on release; in live combat tasks are dropped
 * («Боевая тревога!»). Frozen (>= 80 % cold) or badly wounded (< 50 % HP) operatives cannot lift.
 * Godot reference: main.gd `_start_relocate_for_node`, `_process_relocate_preview`, `_handle_relocate_click`,
 * `_execute_relocate_task`, `active_relocate_tasks` processing, `_cancel_or_finalize_active_relocates_for_combat`.
 */
UCLASS()
class CODEXTACTICS_API URelocationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Enters placement mode for Target with Worker (the leader by default). Returns false with a feed line if refused. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Relocation")
	bool StartRelocate(AInteractableActor* Target, AOperativeCharacter* Worker = nullptr);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Relocation")
	bool IsPlacing() const { return PlacingObject.IsValid(); }

	/** Moves the ghost to the ground point under the cursor. */
	void UpdatePreview(const FVector& GroundPoint);

	/** Rotates the placement by Steps * 45°. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Relocation")
	void RotatePreview(int32 Steps);

	/** LMB in placement mode: plan (pause) or start the move to GroundPoint. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Relocation")
	void ConfirmPlacement(const FVector& GroundPoint);

	/** RMB in placement mode. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Relocation")
	void CancelPlacement();

	/** Starts a move task right away (no placement UI). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Relocation")
	void ExecuteRelocate(AOperativeCharacter* Worker, AInteractableActor* Object, const FVector& TargetLocation, float TargetYaw);

	/** Height of the ground plane under the object being placed (for cursor ray intersection). */
	float GetPlacementGroundZ() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Relocation")
	int32 GetActiveTaskCount() const { return Tasks.Num(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Relocation")
	bool HasPlannedTask(const AOperativeCharacter* Worker) const;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	struct FRelocateTask
	{
		TWeakObjectPtr<AOperativeCharacter> Worker;
		TWeakObjectPtr<AInteractableActor> Object;
		FVector Target = FVector::ZeroVector;
		float TargetYaw = 0.f;
		/** Actor Z the object keeps (Godot initial_ground_y). */
		float GroundZ = 0.f;
		int32 Stage = 1;
		float RetryTime = 0.f;
	};

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandlePauseReleased();

	/** Returns true when the task finished. */
	bool TickTask(FRelocateTask& Task, float DeltaTime);
	void SetObjectCarried(AInteractableActor& Object, bool bCarried) const;
	void StepBack(AOperativeCharacter& Worker, const AInteractableActor& Object, float Distance) const;
	/** Drops every task where it is (combat alarm). */
	void DropAllTasks();
	bool CheckLift(const AOperativeCharacter& Worker, const AInteractableActor* Object) const;
	float GetRadius(const AOperativeCharacter& Worker) const;
	FVector GetOrigin(const AOperativeCharacter& Worker) const;
	void Post(const FText& Speaker, const FText& Text) const;

	TWeakObjectPtr<AInteractableActor> PlacingObject;
	TWeakObjectPtr<AOperativeCharacter> PlacingWorker;
	float PlacingYaw = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<ARelocationGhostActor> Ghost;

	TArray<FRelocateTask> Tasks;
	/** Pause plans per worker, executed on release. */
	TArray<FRelocateTask> PlannedTasks;
};
