// Godot reference: none (Unreal-only). Godot found nodes by name; UE's actor label (the Outliner name the tutorial /
// level data use as a selector) is editor-only data and does not exist in a packaged game.
#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * An actor's editor label, also in cooked builds: the cook stamps every placed actor with a "Label=<label>" tag
 * (CodexTacticsEditor, OnObjectPreSave while cooking), and the game reads the label back from it.
 */
namespace ActorLabelTag
{
	/** Prefix of the tag the cook writes. */
	inline constexpr const TCHAR* Prefix = TEXT("Label=");

	/** The cook-time tag for Label (None for an empty label). */
	CODEXTACTICS_API FName MakeTag(const FString& Label);

	/** The label stored in a tag list (empty without one). */
	CODEXTACTICS_API FString FindInTags(const TArray<FName>& Tags);

	/** The actor's label: the editor label in editor builds (also -game smokes), the cooked tag in a packaged game. */
	CODEXTACTICS_API FString GetLabel(const AActor& Actor);
}
