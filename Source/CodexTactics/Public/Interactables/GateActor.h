#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Quests/GateProgressRules.h"
#include "GateActor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FOverheadLabel;

/**
 * Heavy sliding blast gate of the checkpoint. The doors slide apart along the local Y axis (left -OpenDistance, right
 * +OpenDistance) at OpenSpeed; once open, their collision stops blocking the passage.
 * Opening (level JSON "gate_open_mode", GateProgressRules):
 *  - "generator_waves" (default, user request 2026-10-11): powering the terminal only unlocks the servos. During the wave
 *    fight the internal Progress advances only while the generator works (the door stays VISUALLY CLOSED, leaves at rest,
 *    collision and navmesh block stay); a generator breakdown pauses it; the leaves move only when it opens fully, once
 *    the final wave is cleared with the generator up. A rotating amber warning beacon on the door spins (light on) while
 *    the process runs and the generator works, and stops when the generator is down. HQ radio lines and a HUD / world
 *    line report the progress.
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

	/** The first gate of the level that is not set dressing (nullptr without one): the quest chain's / the exit's gate. */
	static AGateActor* FindLevelGate(const UWorld* World);

	/**
	 * 2026-10-22 b: static set dressing - the gate stands fully open from the start (leaves slid apart in the editor too, no collision,
	 * no navmesh block, no beacon), ignores the quest chain / the terminal / saves and is never the level's gate (FindLevelGate skips
	 * it). Used for the lake compound's open blast door recreated behind the squad's start on L_Demo01_B_FrozenFactory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	bool bSetDressing = false;

	/**
	 * Set dressing only: the leaves stand open (no collision, no navmesh block). Off (default; user decision 2026-10-22 for the Frozen Factory
	 * start: "no way back, don't confuse the player"): CLOSED - leaves shut, blocking pawns and the navmesh, still inert.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate", meta = (EditCondition = "bSetDressing"))
	bool bSetDressingOpen = false;

	/** The level's JSON ends the level through this gate ("gate_exit" on): its HQ lines send the squad OUT, not in. */
	bool IsExitGate() const;

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

	/** The warning beacon spins (light on) now. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	bool IsBeaconSpinning() const { return bBeaconSpinning; }

	/** Accumulated beacon yaw, degrees (grows only while it spins). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	float GetBeaconYaw() const { return BeaconYaw; }

	/** The beacon light is on. */
	bool IsBeaconLightOn() const;

	/** Both leaves sit in their closed place (boxes and meshes at rest). */
	bool AreLeavesClosed() const;

	/** Slide distance of each door, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenDistance = 350.f;

	/** Slide speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenSpeed = 200.f;

	/** Beacon position relative to the gate actor (above the door by default); applied on construction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon")
	FVector BeaconOffset = FVector(0.f, 0.f, 430.f);

	/** Beacon spin speed, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon", meta = (ClampMin = "0"))
	float BeaconSpinSpeed = 240.f;

	/** Beacon lens material (Gemini slot, TANDEM request 19); null = the engine default material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon")
	TObjectPtr<UMaterialInterface> BeaconMaterial;

	/** Beacon light colour, intensity (cd) and reach (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon")
	FLinearColor BeaconColor = FLinearColor(1.f, 0.55f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon", meta = (ClampMin = "0"))
	float BeaconIntensity = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Beacon", meta = (ClampMin = "0"))
	float BeaconRadius = 1800.f;

	/** Size of one door leaf (X thickness, Y width, Z height), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate")
	FVector DoorSize = FVector(40.f, 350.f, 400.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> LeftDoor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> RightDoor;

	/**
	 * 2026-10-21: an art mesh for the LEFT leaf (the right one shows it mirrored across the gate's centre line). Set: it sits at
	 * LeafMeshTransform relative to the leaf box, never stretched (VisualMeshRules); empty: the engine cube filling DoorSize, as
	 * before. The leaf boxes stay the collision / navmesh shape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Visual")
	TObjectPtr<UStaticMesh> LeafMesh;

	/** Where LeafMesh sits relative to the left leaf box centre; the right leaf uses its mirror image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate|Visual")
	FTransform LeafMeshTransform;

private:
	/** The cube or LeafMesh on both leaves (OnConstruction). */
	void ApplyLeafMeshes();

	/** bSetDressing: leaves open (bSetDressingOpen: no collision / navmesh, open status) or shut (blocking, the navmesh cut), beacon off. */
	void ApplySetDressingOpen();

	/** The engine cube the leaves show without LeafMesh. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderLeafMesh;

	UFUNCTION()
	void HandleGateOpened();

	/** The powered terminal unlocks the paced servos (generator_waves). */
	void UnlockProgress();
	void TickProgress(float DeltaSeconds);
	/** Records the status; posts the HQ line for the change unless the change is silent (load). */
	void SetStatus(EGateProgressStatus NewStatus);
	/** Spins the beacon / switches its light for this frame. */
	void UpdateBeacon(float DeltaSeconds, bool bGeneratorWorking);
	void SetBeaconOn(bool bOn);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> LeftDoorMesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> RightDoorMesh;

	/** Rotates around Z; carries the lens mesh and the spot light. */
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate|Beacon")
	TObjectPtr<USceneComponent> BeaconPivot;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate|Beacon")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate|Beacon")
	TObjectPtr<USpotLightComponent> BeaconLight;

	bool bBeaconSpinning = false;
	float BeaconYaw = 0.f;

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

	/** Leaf mesh rest positions relative to their boxes. */
	FVector LeftMeshRest = FVector::ZeroVector;
	FVector RightMeshRest = FVector::ZeroVector;
};
