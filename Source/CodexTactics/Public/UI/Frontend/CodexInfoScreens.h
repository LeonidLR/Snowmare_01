#pragma once

#include "CoreMinimal.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "CodexInfoScreens.generated.h"

class UTextBlock;

/**
 * OPTIONS — phase 1 placeholder (the tabbed options screen comes in a later phase). BACK closes it.
 * Widget names (WBP_Options): TitleText, BodyText, BackButton (EntryId Back).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexOptionsScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void OnEntryActivated(FName EntryId) override;
};

/**
 * CREDITS — a simple list (CreditLines, edited on the WBP_Credits class defaults). BACK closes it.
 * Widget names (WBP_Credits): TitleText, CreditsText (filled from CreditLines when the list is not empty), BackButton.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexCreditsScreen : public UCodexActivatableScreen
{
	GENERATED_BODY()

public:
	UCodexCreditsScreen();

	/** One line per entry; empty lines become spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|UI")
	TArray<FText> CreditLines;

	/** The credit lines joined (what CreditsText shows). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	FText GetCreditsText() const;

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void BindScreenWidgets() override;
	virtual void RefreshScreen() override;
	virtual void OnEntryActivated(FName EntryId) override;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CreditsText;
};
