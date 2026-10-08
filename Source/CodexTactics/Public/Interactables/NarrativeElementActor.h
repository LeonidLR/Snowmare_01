#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "NarrativeElementActor.generated.h"

/** Godot NarrativeElement.NarrativeType. */
UENUM(BlueprintType)
enum class ENarrativeType : uint8
{
	Note,
	Tablet,
	Book,
	Newspaper,
	Signpost,
	Poster,
	Graffiti,
	Custom
};

/**
 * A readable note / signpost / poster (Godot Scenes/movements/narrative_element.gd, main.gd _trigger_menu_for_object
 * narrative branch): a marker is visible from afar; within ReadableDistance of the leader the text shows in the world;
 * a click walks the leader up and opens «📜 Title» with the text, "Read Aloud" posts it to the feed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ANarrativeElementActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ANarrativeElementActor();

	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void PerformAction(AOperativeCharacter* User) override;
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative")
	ENarrativeType NarrativeType = ENarrativeType::Note;

	/** Distance from the leader at which the text becomes readable in the world, cm (Godot readable_distance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative", meta = (ClampMin = "0"))
	float ReadableDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative")
	bool bInWorldText = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative")
	FString Title = TEXT("Sentry's Note");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative", meta = (MultiLine = "true"))
	FString ContentText = TEXT("Note text...");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative")
	FString AuthorOrSource = TEXT("Northern Line Checkpoint");

	/** In-world text height above the element, cm (Godot text_offset_y 1.2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Narrative")
	float TextOffset = 120.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Narrative")
	bool bHasBeenRead = false;

	/** Godot _get_type_icon. */
	FString GetTypeIcon() const;
	/**
	 * Title / content / source as shown to the player: the authored text when it is English, otherwise the narrative
	 * manifest sequence named like this actor (line 1 speaker_en = title, text_en = content, line 2 speaker_en = source),
	 * otherwise "[EN missing: <ActorName>#1]".
	 */
	void GetEnglishTexts(FString& OutTitle, FString& OutContent, FString& OutSource) const;
	/** The leader is within ReadableDistance now. */
	bool IsReadableNow() const;
};
