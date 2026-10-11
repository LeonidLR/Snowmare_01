#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Quests/GateProgressRules.h"
#include "GateActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
struct FOverheadLabel;

/**
 * Heavy sliding blast gate of the checkpoint. The doors slide apart along the local Y axis (left -OpenDistance, right
 * +OpenDistance) at OpenSpeed; once open, their collision stops blocking the passage.
 * Opening (level JSON "gate_open_mode", GateProgressRules):
 *  - "generator_waves" (default, user request 2026-10-11): powering the terminal only unlocks the servos. During the wave
 *    fight the door creeps open (Progress, a visible crack of up to MaxCrackDistance per leaf; the collision and the
 *    navmesh block stay) only while the generator works; a generator breakdown pauses it; it opens fully only once the
 *    final wave is cleared with the generator up. HQ radio lines and a HUD / world line report it.
 *  - "instant": the terminal opens it at once (Godot).
 * Console: Codex.Gate.OpenMode, Codex.Gate.ProgressPerSecond.
 * Godot reference: Scenes/movements/gate.gd (open_distance 3.5 m, open_speed 2 m/s); design/levels/stage-01-bunker-gate.md.
 */
UCLASS()
class CODEXTACTICS_API AGateActor : public AActor
{
	GENERATED_BODY()

public:
	AGateActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The first gate of the level (nullptr without one). */
	static AGateActor* FindLevelGate(const UWorld* World);

	/** Opens fully now, whatever the progress (the instant mode, «Start Battle», the final wave cleared). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Gate")
	void OpenGate();

	/** The leaves are sliding fully open (not the slow creep of the wave fight). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	bool IsOpening() const { return bOpening; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	bool IsOpen() const { return bOpen; }

	/** Opening progress 0..1 (1 = fully open / sliding open). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	float GetOpenProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	EGateProgressStatus GetProgressStatus() const { return Status; }

	/** The terminal powered the paced servos (progress may run). */
	bool IsProgressUnlocked() const { return bProgressUnlocked; }

	/** The door's final wave has been cleared (latched; saved). */
	bool IsFinalWaveCleared() const { return bFinalWaveCleared; }

	/** The level's gate data with the console overrides. */
	FGateProgressConfig GetProgressConfig() const;

	/** "generator_waves": the door is paced by the generator and the waves. */
	bool UsesGeneratorPacing() const { return GetProgressConfig().Mode == EGateOpenMode::GeneratorWaves; }

	/** Generator running (quest) and no generator broken down. */
	bool IsGeneratorPowering() const;

	/** The HUD shows the gate line now (paced, powered, not open yet). */
	bool ShouldShowStatus() const;

	/** HUD / world label line, e.g. "BLAST DOOR 37% — GENERATOR OFFLINE". */
	FString GetStatusText() const;

	/** World label above the gate (ACodexTacticsHUD::DrawWorldLabels); false when there is nothing to show. */
	bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	/** Save-game load: snaps both leaves open (no blocking, no nav) or closed (blocking) at once. */
	void RestoreOpen(bool bInOpen);

	/**
	 * Save-game load: the paced state (unlocked by the powered terminal, progress, final wave cleared). The status is
	 * re-evaluated silently (no HQ line for the load itself).
	 */
	void RestoreProgress(bool bUnlocked, float InProgress, bool bInFinalWaveCleared);

	/** Slide distance of each door, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenDistance = 350.f;

	/** Slide speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenSpeed = 200.f;

	/**
	 * Visible creep of each leaf at 100 % progress before the final opening, cm (only the leaf meshes move: the collision
	 * stays closed, so the door is impassable for the whole fight). 3 waves: ~7.5 / 15 / 22.5 cm per leaf.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float MaxCrackDistance = 25.f;

	/** Size of one door leaf (X thickness, Y width, Z height), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate")
	FVector DoorSize = FVector(40.f, 350.f, 400.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> LeftDoor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> RightDoor;

private:
	UFUNCTION()
	void HandleGateOpened();

	/** The powered terminal unlocks the paced servos (generator_waves). */
	void UnlockProgress();
	void TickProgress(float DeltaSeconds);
	/** Records the status; posts the HQ line for the change unless the change is silent (load). */
	void SetStatus(EGateProgressStatus NewStatus);
	/** Moves the leaf meshes by the creep of the current progress (the collision boxes stay closed). */
	void ApplyCrack();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> LeftDoorMesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> RightDoorMesh;

	float LeftTargetY = 0.f;
	float RightTargetY = 0.f;
	bool bOpening = false;
	bool bOpen = false;

	/** Paced state (GateProgressRules). */
	bool bProgressUnlocked = false;
	float Progress = 0.f;
	bool bFinalWaveCleared = false;
	EGateProgressStatus Status = EGateProgressStatus::Locked;
	/** The next status change is recorded without a radio line (save-game load). */
	bool bSilentStatus = false;

	/** Leaf mesh rest positions relative to their boxes (the creep is added to them). */
	FVector LeftMeshRest = FVector::ZeroVector;
	FVector RightMeshRest = FVector::ZeroVector;
};
