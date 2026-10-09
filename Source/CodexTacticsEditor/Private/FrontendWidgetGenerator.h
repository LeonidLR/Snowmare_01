#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FrontendWidgetGenerator.generated.h"

class UWidgetBlueprint;

/**
 * Editor helper behind Scripts/Editor/create_frontend_assets.py: creates the placeholder frontend Widget Blueprints
 * (WBP_*) as children of the C++ frontend classes and writes each class's default widget tree
 * (ICodexDefaultTree::BuildDefaultTree) into the WBP, so the artist gets a real, editable designer layout.
 * Existing trees are kept unless bRebuildTree (the artist's edits win).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS()
class UFrontendWidgetGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates (or loads) PackagePath/AssetName as a Widget Blueprint of ParentClass, sets the screen's EntryButtonClass /
	 * SlotEntryClass on its class defaults (when given), fills an empty tree (or any tree with bRebuildTree) with the
	 * placeholder layout and compiles it. Not saved: the caller saves. Null with the reason in OutReport on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Editor")
	static UWidgetBlueprint* BuildFrontendWidget(const FString& PackagePath, const FString& AssetName, UClass* ParentClass,
		UClass* EntryButtonClass, UClass* SlotEntryClass, bool bRebuildTree, FString& OutReport);
};
