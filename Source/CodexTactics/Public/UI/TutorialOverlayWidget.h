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
	/**
	 * Default layout (user request 2026-10-13: the radio strip and the hint card must never overlap, they now show together
	 * in A0): the card lives in a bottom-left COLUMN (CardLeftMargin + at most CardMaxWidth wide); the radio strip is centred
	 * at the bottom with StripSideReserve kept free on BOTH sides, so it stays centred and can never reach the card column on
	 * any aspect ratio (a narrow screen only wraps the line more).
	 */
	static constexpr float CardLeftMargin = 24.f;
	static constexpr float CardMaxWidth = 460.f;
	static constexpr float StripSideReserve = CardLeftMargin + CardMaxWidth + 24.f;

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
	/** The radio strip's key hint ("[Enter] next  .  [Hold Enter] skip") and whether its SKIP button is shown (smokes). */
	FText GetShownRadioHint() const;
	bool IsRadioSkipShown() const;
	/** Last laid-out rectangles of the card and the strip; false before both were laid out (headless runs). */
	bool GetCardAndStripRects(FSlateRect& OutCard, FSlateRect& OutStrip) const;
	/** Edge arrows drawn on the last refresh (smokes). */
	int32 GetEdgePointerCount() const { return EdgePointers.Num(); }
	/** Rev 5i: the SKIP TUTORIAL button is shown (the start beat, smokes). */
	bool IsSkipTutorialShown() const;
	/**
	 * Rev 5i: what the SKIP TUTORIAL button does - pauses the game and asks "Skip the tutorial? All controls unlock." (Yes / No,
	 * the Modal layer's confirm dialog); Yes = UTutorialSubsystem::SkipTutorial. The pause is undone either way.
	 */
	void RequestSkipTutorial();

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
	/** "2 / 5   [Enter] next  .  [Hold Enter] skip" (dialogue skip, user request 2026-10-13). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TutorialRadioHint;
	/** Skips the whole radio / walk-and-talk conversation (UTutorialSubsystem::SkipSpeech). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TutorialRadioSkipButton;

	// --- Skip the whole tutorial (rev 5i) ---
	/** "SKIP TUTORIAL", top right, while the tutorial is in its start beat (UTutorialSubsystem::IsSkipOfferShown). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TutorialSkipAllButton;

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
	void HandleRadioSkipClicked();
	UFUNCTION()
	void HandleRetryClicked();
	UFUNCTION()
	void HandleContinueClicked();
	UFUNCTION()
	void HandleSkipAllClicked();

	/** Arrow tip + direction in local widget space. */
	struct FEdgePointer
	{
		FVector2D Tip = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D(1.f, 0.f);
	};
	TArray<FEdgePointer> EdgePointers;

	/** A shown marker's label on screen (2026-10-19: the role spots of the A3 cover lesson name their operative). */
	struct FMarkerLabel
	{
		FVector2D Position = FVector2D::ZeroVector;
		FText Text;
		FLinearColor Color = FLinearColor::White;
	};
	TArray<FMarkerLabel> MarkerLabels;

public:
	/** Marker labels drawn now (smokes). */
	int32 GetMarkerLabelCount() const { return MarkerLabels.Num(); }
};
