#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DialogueTriggerVolume.generated.h"

class UBoxComponent;
class UDialogueSequenceAsset;

/**
 * A zone that plays a dialogue in the message feed when a squad member walks in (Godot
 * Scenes/movements/dialogue_trigger.gd DialogueTrigger3D -> main.gd play_dialogue: line by line with delay_after).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ADialogueTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	ADialogueTriggerVolume();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Dialogue")
	TObjectPtr<UBoxComponent> Box;

	/** Dialogue to play (Godot dialogue export). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Dialogue")
	TSoftObjectPtr<UDialogueSequenceAsset> Dialogue;

	/** Godot trigger_once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Dialogue")
	bool bTriggerOnce = true;

	/** Godot trigger_on_squad_only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Dialogue")
	bool bSquadOnly = true;

	bool HasTriggered() const { return bHasTriggered; }

	/** Godot reset_trigger. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Dialogue")
	void ResetTrigger() { bHasTriggered = false; }

	/** Save-game load: the dialogue already played (a once-only trigger stays silent). */
	void RestoreTriggered(bool bTriggered) { bHasTriggered = bTriggered; }

	/** Plays the dialogue for Actor entering (squad filter, once); true when it played. */
	bool TryTrigger(AActor* Actor);

private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool bHasTriggered = false;
};
