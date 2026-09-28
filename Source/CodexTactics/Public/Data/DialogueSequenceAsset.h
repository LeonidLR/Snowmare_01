#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DialogueSequenceAsset.generated.h"

/** One line of a dialogue (Godot resources/dialogue_line.gd). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FDialogueLine
{
	GENERATED_BODY()

	/** Speaker («Командир», «Инженер», «Медик-сапёр», «ШТАБ»…). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FString SpeakerName = TEXT("Командир");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (MultiLine = "true"))
	FString Text;

	/** Pause before the next line when the dialogue plays in the message feed, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = "0"))
	float DelayAfter = 4.f;
};

/**
 * A dialogue: lines shown in the bottom dialogue window (mission start, recruitment) or posted to the message feed
 * one by one (preparation, wave rest, victory). Imported from the Godot .tres files by
 * Scripts/Editor/import_dialogues.py — do not hand-edit the texts.
 * Godot reference: resources/dialogue_sequence.gd.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UDialogueSequenceAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FString Title;

	/** Text of the button on the last line (empty: «Понял! ▶» / «В бой! ▶» by context). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FString CustomFinishButtonText;

	/** The last button reads «🤝 Вступить в отряд». */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bIsRecruitmentDialogue = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDialogueLine> Lines;
};
