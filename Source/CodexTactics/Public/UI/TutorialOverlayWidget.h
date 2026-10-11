#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialOverlayWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;

/**
 * The tutorial overlay (Demo 01 C9, UX brief 03 §2.5): one persistent widget over the HUD widgets (z 14: above the
 * banners, below the dialogue window) with the hint card (title, body, key caps, "2 / 5", Skip), the objective tracker
 * (at most 3 lines), the radio / walk-and-talk subtitle strip (speaker + line, timed, the game never pauses) and the
 * lesson "Retry from checkpoint" prompt; draws screen-edge arrows towards off-screen tutorial markers. It never takes a
 * click except on its own buttons (the root and panels are self-hit-test-invisible). Reads UTutorialSubsystem every
 * frame. Built in C++ (fallback look); a Widget Blueprint subclass restyles it by naming its widgets as the
 * BindWidgetOptional members below (ACodexTacticsHUD::TutorialOverlayWidgetClass). UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UTutorialOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Pulls the views from UTutorialSubsystem (also called by smokes). */
	void Refresh();

	bool IsCardShown() const;
	FText GetShownCardBody() const;
	FText GetShownCardKeys() const;
	bool IsSubtitleShown() const;
	FText GetShownSubtitle() const;
	FText GetShownSpeaker() const;
	int32 GetShownObjectiveCount() const;
	bool IsRetryShown() const;
	/** Edge arrows drawn on the last refresh (smokes). */
	int32 GetEdgePointerCount() const { return EdgePointers.Num(); }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- Hint card ---
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> TutorialHintCard;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialCardTitle;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialCardBody;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialCardKeys;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialCardProgress;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialCardDone;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TutorialSkipButton;

	// --- Objective tracker ---
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> TutorialTracker;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialObjective1;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialObjective2;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialObjective3;

	// --- Radio / walk-and-talk strip ---
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> TutorialRadioStrip;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialRadioSpeaker;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialRadioLine;

	// --- Lesson retry prompt ---
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> TutorialRetryPanel;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialRetryText;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TutorialRetryButton;
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TutorialContinueButton;

	/** Edge arrow colour (amber = the marker colour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FLinearColor PointerColor = FLinearColor(1.f, 0.6f, 0.1f, 0.95f);

private:
	void BuildDefaultLayout();
	void BindButtons();
	void UpdateEdgePointers();

	UFUNCTION()
	void HandleSkipClicked();
	UFUNCTION()
	void HandleRetryClicked();
	UFUNCTION()
	void HandleContinueClicked();

	/** Arrow tip + direction in local widget space. */
	struct FEdgePointer
	{
		FVector2D Tip = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D(1.f, 0.f);
	};
	TArray<FEdgePointer> EdgePointers;
};
