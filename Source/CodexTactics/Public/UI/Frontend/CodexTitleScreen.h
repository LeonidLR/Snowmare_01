#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "CodexTitleScreen.generated.h"

/**
 * Title screen «PRESS ANY KEY» over the 3D menu scene: any key, mouse button or gamepad button opens the main menu.
 * Widget names (WBP_TitleScreen): TitleText, PressAnyKeyText (the artist animates / restyles them freely).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexTitleScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexTitleScreen();

	/** Leaves the title for the main menu (what any key does). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI")
	void ContinueFromTitle();

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void NativeOnActivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	bool bLeaving = false;
};
