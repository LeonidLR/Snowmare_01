#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DialogueSubsystem.generated.h"

class UDialogueSequenceAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueChanged, bool, bOpen);
DECLARE_MULTICAST_DELEGATE(FOnDialogueFinishedNative);

/**
 * Story dialogues. StartDialogue shows a sequence line by line in the bottom dialogue window (the world keeps running,
 * player orders are blocked; Space / Enter / click = next, Esc = skip). PlayInFeed posts the lines to the message feed
 * one after another with each line's delay (non-blocking).
 * Godot reference: Scenes/ui/dialogue/bottom_dialogue_dialog.gd (BottomDialogueController), main.gd play_dialogue.
 */
UCLASS()
class CODEXTACTICS_API UDialogueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Opens the bottom window with Sequence; OnFinished runs when it closes (last line or skip). */
	void StartDialogue(const UDialogueSequenceAsset* Sequence, FSimpleDelegate OnFinished = FSimpleDelegate());

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Dialogue")
	void StartDialogueBP(const UDialogueSequenceAsset* Sequence) { StartDialogue(Sequence); }

	/** Next line (closes after the last one). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Dialogue")
	void AdvanceLine();

	/** Closes the window at once. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Dialogue")
	void SkipDialogue();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Dialogue")
	bool IsDialogueOpen() const { return Current != nullptr; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Dialogue")
	int32 GetLineIndex() const { return LineIndex; }

	const UDialogueSequenceAsset* GetCurrentSequence() const { return Current; }

	/** Posts the lines to the message feed one by one (Godot play_dialogue); OnFinished after the last delay. */
	void PlayInFeed(const UDialogueSequenceAsset* Sequence, FSimpleDelegate OnFinished = FSimpleDelegate());

	/** Window opened / closed or the line changed (bOpen = still open). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Dialogue")
	FOnDialogueChanged OnDialogueChanged;

private:
	/**
	 * English version of a DA_Dialogue* asset: lines, speakers and delays come from narrative_manifest.json
	 * (sequence id = asset name); missing lines become "[EN missing: <seq>#<n>]". Transient sequences pass through.
	 */
	const UDialogueSequenceAsset* ResolveSequence(const UDialogueSequenceAsset* Sequence);

	void Close();
	void PostNextFeedLine(int32 PlayId, int32 Index);

	UPROPERTY(Transient)
	TObjectPtr<const UDialogueSequenceAsset> Current;

	UPROPERTY(Transient)
	TObjectPtr<const UDialogueSequenceAsset> FeedSequence;

	FSimpleDelegate OnCurrentFinished;
	FSimpleDelegate OnFeedFinished;
	int32 LineIndex = 0;
	/** A newer PlayInFeed cancels the older one (Godot _active_dialogue_id). */
	int32 FeedPlayId = 0;
};
