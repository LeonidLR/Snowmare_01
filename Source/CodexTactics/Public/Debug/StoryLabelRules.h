#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the story-showcase labels (designer test 2026-11-22, item 6: "above every house its story name, above each locked / closed
 * door what it is"). A debug / test overlay (CodexTacticsDev UStoryLabelSubsystem, console Codex.Debug.StoryLabels); the texts are the
 * designer's language (Russian) - the labels are a test overlay, not game HUD (the HUD stays English, user decision 2026-10-08), so the
 * literals live in Private/Debug. Story numbers / ids: docs/design/levelgen/gdd/building-stories.md. UE-only (no Godot reference).
 */
namespace StoryLabelRules
{
	/** The story number of a layout story id ("locked_door" -> 3; 0 = unknown). */
	CODEXTACTICS_API int32 StoryNumber(const FString& StoryId);

	/** A story's label: "3 · <name>" (the id itself for an unknown story). */
	CODEXTACTICS_API FString StoryTitle(const FString& StoryId);

	/** A house's label: its stories' titles joined with " + " (none: the "open" title). */
	CODEXTACTICS_API FString HouseLabel(const TArray<FString>& StoryIds);

	/**
	 * A door's label: its state word ("locked" / "closed" / "open" / "barricaded" / "sealed" in the designer's language), " · <strength>" when
	 * known (wood / steel / reinforced), " · story N" when a story set it (StoryNumberOfDoor > 0). Empty for a state not shown (an open doorway
	 * nobody touched: bShowOpen false).
	 */
	CODEXTACTICS_API FString DoorLabel(const FString& State, const FString& Strength, int32 StoryNumberOfDoor, bool bShowOpen);

	/** The overlay is on: console Codex.Debug.StoryLabels -1 (default) = on the story-showcase maps only, 0 = off, >= 1 = on everywhere. */
	CODEXTACTICS_API bool ShouldShow(int32 Console, const FString& MapName);

	/**
	 * A door label is shown only where the player looks into a house (the storey cutaway rules): the selected man inside its building - doors of
	 * his storey (the storeys above are cut, those below lie under his floor); otherwise the ground storey's doors and every door a story set
	 * (bStoryDoor). SelectedStorey INDEX_NONE = he is in no building.
	 */
	CODEXTACTICS_API bool IsDoorLabelHidden(bool bSameBuilding, int32 LabelStorey, int32 SelectedStorey, bool bStoryDoor);

	/** The story numbers the layout's door_state(<opening>, <state>) bricks give per opening id. */
	CODEXTACTICS_API void DoorStoriesFromBricks(const FString& StoryId, const TArray<FString>& Bricks, TMap<FString, int32>& InOutDoorStory);
}
