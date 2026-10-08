#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CodexDefaultTree.generated.h"

class UWidgetTree;
class UTextBlock;
class UWidget;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCodexDefaultTree : public UInterface
{
	GENERATED_BODY()
};

/**
 * A frontend widget class that can lay out its own placeholder widget tree. Used twice: at runtime when the C++ class is
 * shown without a Widget Blueprint (empty tree), and by the editor generator (UFrontendWidgetGenerator, run by
 * Scripts/Editor/create_frontend_assets.py) that writes the same tree into the WBP_* asset the artist then restyles.
 * The C++ code finds its widgets by name (see each class: «Widget names»), so the artist may rearrange / restyle
 * them freely but should keep the names.
 */
class CODEXTACTICS_API ICodexDefaultTree
{
	GENERATED_BODY()

public:
	/** Builds the placeholder tree into an EMPTY widget tree (sets Tree.RootWidget). Called on the class default object. */
	virtual void BuildDefaultTree(UWidgetTree& Tree) const = 0;
};

/** Small helpers for the placeholder trees (plain engine widgets, engine font, no art). */
namespace CodexDefaultTree
{
	CODEXTACTICS_API UTextBlock* MakeText(UWidgetTree& Tree, FName Name, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false);

	/** Marks a named widget as a Blueprint variable (editor builds) so the artist sees it in the graph. */
	CODEXTACTICS_API void MarkVariable(UWidget* Widget);

	/** Placeholder palette. */
	inline const FLinearColor TitleColor(0.86f, 0.93f, 1.f);
	inline const FLinearColor BodyColor(0.72f, 0.78f, 0.86f);
	inline const FLinearColor HintColor(0.5f, 0.56f, 0.64f);
	inline const FLinearColor PanelColor(0.02f, 0.03f, 0.05f, 0.72f);
	inline const FLinearColor DimColor(0.f, 0.f, 0.f, 0.55f);
}
