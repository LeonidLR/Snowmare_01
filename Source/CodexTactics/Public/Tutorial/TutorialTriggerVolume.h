#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TutorialTriggerVolume.generated.h"

class UBoxComponent;

/** Who may set a tutorial volume off. */
UENUM(BlueprintType)
enum class ETutorialVolumeFilter : uint8
{
	/** Only the operative the player leads (default: the lesson is about what the player does). */
	Leader UMETA(DisplayName = "Only The Leader"),
	/** Any squad member. */
	AnyOperative UMETA(DisplayName = "Any Operative"),
	/** A vehicle with squad members aboard (APC P1; the step counts its driver, else any crew member). */
	Vehicle UMETA(DisplayName = "Vehicle")
};

/**
 * A box the user places for the tutorial (Demo 01 C2): when the leader (or any operative) walks in it starts StepId
 * and / or BeatId, and counts as "volume" VolumeId for the steps JSON ("trigger": {"type": "volume", "volume": ...} or
 * "complete": {"volume": ...}). Fires once by default; with RequiredBeat it waits until that beat (or a later one) runs.
 * Placing it is harmless before any steps exist: without loaded steps / with the tutorial off it does not fire (and is
 * not used up). Modelled on ADialogueTriggerVolume (Godot dialogue_trigger.gd). UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ATutorialTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	ATutorialTriggerVolume();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<UBoxComponent> Box;

	/** Step to start when entered (steps JSON "id"); empty = none. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName StepId;

	/** Beat to start when entered (steps JSON "beats[].id": checkpoint, lesson rule); empty = none. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName BeatId;

	/** Name steps use in "trigger.volume" / "complete.volume"; empty = StepId. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName VolumeId;

	/** Only fires once this beat (or a later one) is running; empty = always. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName RequiredBeat;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	ETutorialVolumeFilter ActivatedBy = ETutorialVolumeFilter::Leader;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	bool bFireOnce = true;

	/** VolumeId, else StepId. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FName GetEffectiveVolumeId() const { return VolumeId.IsNone() ? StepId : VolumeId; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool HasFired() const { return bFired; }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void ResetVolume() { bFired = false; }

	/** Rev 5f: the fired flag as a save holds it (UTutorialSubsystem::RestoreSaveState; a used-up volume stays used up after a load). */
	void SetFired(bool bInFired) { bFired = bInFired; }

	/** Actor walked in: filter, once, then the tutorial; true when it fired (also used by smokes). */
	bool TryActivate(AActor* Actor);

	/** Re-checks the operatives already standing inside (a volume enabled / a step loaded while they stand in it). */
	void CheckOverlapping();

private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool bFired = false;
};
