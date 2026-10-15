#include "UI/TutorialOverlayWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/CodexButtonFocus.h"
#include "UI/DialogueRules.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialSubsystem.h"

#define LOCTEXT_NAMESPACE "TutorialOverlay"

namespace TutorialOverlayLocal
{
	const FLinearColor CardColor(0.03f, 0.04f, 0.06f, 0.86f);
	const FLinearColor TitleColor(1.f, 0.78f, 0.35f);
	const FLinearColor BodyColor(0.93f, 0.95f, 0.97f);
	const FLinearColor KeyColor(1.f, 0.62f, 0.15f);
	const FLinearColor DoneColor(0.35f, 0.95f, 0.45f);

	UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, int32 Size, const FLinearColor& Color, bool bWrap = false)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetAutoWrapText(bWrap);
		Text->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Text;
	}

	UButton* MakeButton(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* LabelName, const FText& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->SetBackgroundColor(FLinearColor(0.2f, 0.2f, 0.22f, 0.9f));
		UTextBlock* Text = MakeText(Tree, LabelName, 12, BodyColor);
		Text->SetText(Label);
		Button->SetContent(Text);
		Button->SetVisibility(ESlateVisibility::Visible); // the only hit-testable parts of the overlay
		CodexButtonFocus::Disable(Button); // a focused button would take Enter / the game keys after a click
		return Button;
	}
}

void UTutorialOverlayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!TutorialHintCard && !TutorialRadioStrip)
	{
		BuildDefaultLayout();
	}
	BindButtons();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Refresh();
}

void UTutorialOverlayWidget::BuildDefaultLayout()
{
	using namespace TutorialOverlayLocal;
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TutorialRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	// Hint card: bottom left, above the action bar row, in a column at most CardMaxWidth wide (never under the radio strip).
	USizeBox* CardColumn = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TutorialCardColumn"));
	CardColumn->SetMaxDesiredWidth(CardMaxWidth);
	CardColumn->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TutorialHintCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TutorialHintCard"));
	TutorialHintCard->SetBrushColor(CardColor);
	TutorialHintCard->SetPadding(FMargin(16.f, 12.f));
	TutorialHintCard->SetVisibility(ESlateVisibility::Collapsed);
	CardColumn->SetContent(TutorialHintCard);
	UOverlaySlot* CardSlot = Root->AddChildToOverlay(CardColumn);
	CardSlot->SetHorizontalAlignment(HAlign_Left);
	CardSlot->SetVerticalAlignment(VAlign_Bottom);
	CardSlot->SetPadding(FMargin(CardLeftMargin, 0.f, 0.f, 150.f));
	UVerticalBox* CardBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TutorialCardBox"));
	CardBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TutorialHintCard->SetContent(CardBox);
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TutorialCardHeader"));
	Header->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	CardBox->AddChildToVerticalBox(Header);
	TutorialCardTitle = MakeText(WidgetTree, TEXT("TutorialCardTitle"), 16, TitleColor);
	Header->AddChildToHorizontalBox(TutorialCardTitle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TutorialCardProgress = MakeText(WidgetTree, TEXT("TutorialCardProgress"), 11, FLinearColor(0.7f, 0.72f, 0.75f));
	UHorizontalBoxSlot* ProgressSlot = Header->AddChildToHorizontalBox(TutorialCardProgress);
	ProgressSlot->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
	ProgressSlot->SetVerticalAlignment(VAlign_Center);
	TutorialCardBody = MakeText(WidgetTree, TEXT("TutorialCardBody"), 13, BodyColor, true);
	TutorialCardBody->SetWrapTextAt(CardMaxWidth - 40.f);
	CardBox->AddChildToVerticalBox(TutorialCardBody)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	TutorialCardKeys = MakeText(WidgetTree, TEXT("TutorialCardKeys"), 15, KeyColor, true);
	CardBox->AddChildToVerticalBox(TutorialCardKeys)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TutorialCardFooter"));
	Footer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	CardBox->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
	TutorialCardDone = MakeText(WidgetTree, TEXT("TutorialCardDone"), 13, DoneColor);
	TutorialCardDone->SetText(LOCTEXT("Done", "Done!"));
	UHorizontalBoxSlot* DoneSlot = Footer->AddChildToHorizontalBox(TutorialCardDone);
	DoneSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	DoneSlot->SetVerticalAlignment(VAlign_Center);
	TutorialSkipButton = MakeButton(WidgetTree, TEXT("TutorialSkipButton"), TEXT("TutorialSkipLabel"), LOCTEXT("Skip", "Skip"));
	Footer->AddChildToHorizontalBox(TutorialSkipButton)->SetHorizontalAlignment(HAlign_Right);

	// Objective tracker: top left, under the objective banner.
	TutorialTracker = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TutorialTracker"));
	TutorialTracker->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* TrackerSlot = Root->AddChildToOverlay(TutorialTracker);
	TrackerSlot->SetHorizontalAlignment(HAlign_Left);
	TrackerSlot->SetVerticalAlignment(VAlign_Top);
	TrackerSlot->SetPadding(FMargin(18.f, 92.f, 0.f, 0.f));
	TutorialObjective1 = MakeText(WidgetTree, TEXT("TutorialObjective1"), 13, TitleColor);
	TutorialObjective2 = MakeText(WidgetTree, TEXT("TutorialObjective2"), 13, TitleColor);
	TutorialObjective3 = MakeText(WidgetTree, TEXT("TutorialObjective3"), 13, TitleColor);
	for (UTextBlock* Line : { TutorialObjective1.Get(), TutorialObjective2.Get(), TutorialObjective3.Get() })
	{
		TutorialTracker->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, 2.f));
	}

	// Radio / walk-and-talk strip: bottom centre, above the action bar; StripSideReserve stays free on both sides (the card column).
	TutorialRadioStrip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TutorialRadioStrip"));
	TutorialRadioStrip->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	TutorialRadioStrip->SetPadding(FMargin(18.f, 8.f));
	TutorialRadioStrip->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* RadioSlot = Root->AddChildToOverlay(TutorialRadioStrip);
	RadioSlot->SetHorizontalAlignment(HAlign_Center);
	RadioSlot->SetVerticalAlignment(VAlign_Bottom);
	RadioSlot->SetPadding(FMargin(StripSideReserve, 0.f, StripSideReserve, 118.f));
	UVerticalBox* RadioColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TutorialRadioColumn"));
	RadioColumn->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TutorialRadioStrip->SetContent(RadioColumn);
	UHorizontalBox* RadioBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TutorialRadioBox"));
	RadioBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	RadioColumn->AddChildToVerticalBox(RadioBox);
	TutorialRadioSpeaker = MakeText(WidgetTree, TEXT("TutorialRadioSpeaker"), 14, TitleColor);
	RadioBox->AddChildToHorizontalBox(TutorialRadioSpeaker)->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	TutorialRadioLine = MakeText(WidgetTree, TEXT("TutorialRadioLine"), 14, BodyColor, true);
	TutorialRadioLine->SetWrapTextAt(760.f);
	RadioBox->AddChildToHorizontalBox(TutorialRadioLine)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UHorizontalBox* RadioFooter = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TutorialRadioFooter"));
	RadioFooter->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RadioColumn->AddChildToVerticalBox(RadioFooter)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	TutorialRadioHint = MakeText(WidgetTree, TEXT("TutorialRadioHint"), 11, FLinearColor(0.7f, 0.72f, 0.75f));
	UHorizontalBoxSlot* HintSlot = RadioFooter->AddChildToHorizontalBox(TutorialRadioHint);
	HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HintSlot->SetVerticalAlignment(VAlign_Center);
	TutorialRadioSkipButton = MakeButton(WidgetTree, TEXT("TutorialRadioSkipButton"), TEXT("TutorialRadioSkipLabel"), LOCTEXT("RadioSkip", "SKIP"));
	UHorizontalBoxSlot* RadioSkipSlot = RadioFooter->AddChildToHorizontalBox(TutorialRadioSkipButton);
	RadioSkipSlot->SetHorizontalAlignment(HAlign_Right);
	RadioSkipSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));

	// Rev 5i: SKIP TUTORIAL, top right, while the tutorial is in its start beat (the pause menu entry stays available later).
	TutorialSkipAllButton = MakeButton(WidgetTree, TEXT("TutorialSkipAllButton"), TEXT("TutorialSkipAllLabel"), LOCTEXT("SkipTutorial", "SKIP TUTORIAL"));
	TutorialSkipAllButton->SetBackgroundColor(FLinearColor(0.45f, 0.28f, 0.05f, 0.95f));
	TutorialSkipAllButton->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* SkipAllSlot = Root->AddChildToOverlay(TutorialSkipAllButton);
	SkipAllSlot->SetHorizontalAlignment(HAlign_Right);
	SkipAllSlot->SetVerticalAlignment(VAlign_Top);
	SkipAllSlot->SetPadding(FMargin(0.f, 92.f, 18.f, 0.f));

	// Lesson retry prompt: upper centre.
	TutorialRetryPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TutorialRetryPanel"));
	TutorialRetryPanel->SetBrushColor(CardColor);
	TutorialRetryPanel->SetPadding(FMargin(18.f, 12.f));
	TutorialRetryPanel->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* RetrySlot = Root->AddChildToOverlay(TutorialRetryPanel);
	RetrySlot->SetHorizontalAlignment(HAlign_Center);
	RetrySlot->SetVerticalAlignment(VAlign_Top);
	RetrySlot->SetPadding(FMargin(0.f, 170.f, 0.f, 0.f));
	UVerticalBox* RetryBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TutorialRetryBox"));
	RetryBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TutorialRetryPanel->SetContent(RetryBox);
	TutorialRetryText = MakeText(WidgetTree, TEXT("TutorialRetryText"), 14, BodyColor, true);
	TutorialRetryText->SetWrapTextAt(460.f);
	RetryBox->AddChildToVerticalBox(TutorialRetryText);
	UHorizontalBox* RetryButtons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TutorialRetryButtons"));
	RetryButtons->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	RetryBox->AddChildToVerticalBox(RetryButtons)->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	TutorialRetryButton = MakeButton(WidgetTree, TEXT("TutorialRetryButton"), TEXT("TutorialRetryLabel"), LOCTEXT("Retry", "Retry from checkpoint"));
	RetryButtons->AddChildToHorizontalBox(TutorialRetryButton)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	TutorialContinueButton = MakeButton(WidgetTree, TEXT("TutorialContinueButton"), TEXT("TutorialContinueLabel"), LOCTEXT("Continue", "Carry on"));
	RetryButtons->AddChildToHorizontalBox(TutorialContinueButton);
}

void UTutorialOverlayWidget::BindButtons()
{
	if (TutorialSkipButton)
	{
		TutorialSkipButton->OnClicked.AddUniqueDynamic(this, &UTutorialOverlayWidget::HandleSkipClicked);
	}
	if (TutorialRadioSkipButton)
	{
		TutorialRadioSkipButton->OnClicked.AddUniqueDynamic(this, &UTutorialOverlayWidget::HandleRadioSkipClicked);
	}
	if (TutorialRetryButton)
	{
		TutorialRetryButton->OnClicked.AddUniqueDynamic(this, &UTutorialOverlayWidget::HandleRetryClicked);
	}
	if (TutorialContinueButton)
	{
		TutorialContinueButton->OnClicked.AddUniqueDynamic(this, &UTutorialOverlayWidget::HandleContinueClicked);
	}
	if (TutorialSkipAllButton)
	{
		TutorialSkipAllButton->OnClicked.AddUniqueDynamic(this, &UTutorialOverlayWidget::HandleSkipAllClicked);
	}
}

void UTutorialOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UTutorialOverlayWidget::Refresh()
{
	using namespace TutorialOverlayLocal;
	const UWorld* World = GetWorld();
	const UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;

	const FTutorialCardView Card = Tutorial ? Tutorial->GetCardView() : FTutorialCardView();
	if (TutorialHintCard)
	{
		TutorialHintCard->SetVisibility(Card.bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Card.bVisible)
	{
		if (TutorialCardTitle)
		{
			TutorialCardTitle->SetText(Card.Title);
			TutorialCardTitle->SetVisibility(Card.Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
		if (TutorialCardBody)
		{
			TutorialCardBody->SetText(Card.Body);
		}
		if (TutorialCardKeys)
		{
			TutorialCardKeys->SetText(Card.Keys);
			TutorialCardKeys->SetVisibility(Card.Keys.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
		if (TutorialCardProgress)
		{
			TutorialCardProgress->SetText(Card.Progress);
		}
		if (TutorialCardDone)
		{
			TutorialCardDone->SetVisibility(Card.bDone ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
		if (TutorialSkipButton)
		{
			TutorialSkipButton->SetVisibility(Card.bSkippable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}

	const TArray<FTutorialObjectiveView> Objectives = Tutorial ? Tutorial->GetObjectiveViews() : TArray<FTutorialObjectiveView>();
	if (TutorialTracker)
	{
		TutorialTracker->SetVisibility(Objectives.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	UTextBlock* Lines[] = { TutorialObjective1.Get(), TutorialObjective2.Get(), TutorialObjective3.Get() };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Lines); ++Index)
	{
		if (!Lines[Index])
		{
			continue;
		}
		if (!Objectives.IsValidIndex(Index))
		{
			Lines[Index]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FTutorialObjectiveView& Objective = Objectives[Index];
		Lines[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		Lines[Index]->SetText(FText::Format(LOCTEXT("ObjectiveLine", "{0} {1}"), FText::FromString(Objective.bDone ? TEXT("[x]") : TEXT("[ ]")), Objective.Text));
		Lines[Index]->SetColorAndOpacity(FSlateColor(Objective.bDone ? DoneColor : TitleColor));
	}

	const FTutorialSubtitleView Subtitle = Tutorial ? Tutorial->GetSubtitleView() : FTutorialSubtitleView();
	if (TutorialRadioStrip)
	{
		// Self-hit-test-invisible: the world still takes clicks through the strip, only its SKIP button is clickable.
		TutorialRadioStrip->SetVisibility(Subtitle.bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Subtitle.bVisible)
	{
		if (TutorialRadioSpeaker)
		{
			TutorialRadioSpeaker->SetText(FText::Format(LOCTEXT("SpeakerFmt", "{0}:"), Subtitle.Speaker));
			TutorialRadioSpeaker->SetColorAndOpacity(FSlateColor(Subtitle.SpeakerColor));
		}
		if (TutorialRadioLine)
		{
			TutorialRadioLine->SetText(Subtitle.Line);
		}
		if (TutorialRadioHint)
		{
			// In the turn-based fight Enter ends the turn, so only the SKIP button skips there.
			const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
			const FText Keys = TurnBased && TurnBased->IsActive() ? LOCTEXT("RadioHintTurnBased", "SKIP: end the conversation")
				: DialogueRules::GetSkipHintText(PlayerCommands::GetKeyLabel(PlayerCommands::DialogueNext), PlayerCommands::GetKeyLabel(PlayerCommands::DialogueSkip));
			TutorialRadioHint->SetText(Subtitle.LineCount > 1
				? FText::Format(LOCTEXT("RadioHintProgress", "{0} / {1}    {2}"), Subtitle.LineIndex + 1, Subtitle.LineCount, Keys) : Keys);
		}
		if (TutorialRadioSkipButton)
		{
			TutorialRadioSkipButton->SetVisibility(ESlateVisibility::Visible);
		}
	}

	const FTutorialRetryView Retry = Tutorial ? Tutorial->GetRetryView() : FTutorialRetryView();
	if (TutorialRetryPanel)
	{
		TutorialRetryPanel->SetVisibility(Retry.bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Retry.bVisible)
	{
		if (TutorialRetryText)
		{
			TutorialRetryText->SetText(Retry.Text);
		}
		if (TutorialRetryButton)
		{
			TutorialRetryButton->SetIsEnabled(Retry.bHasCheckpoint);
		}
	}
	if (TutorialSkipAllButton)
	{
		TutorialSkipAllButton->SetVisibility(Tutorial && Tutorial->IsSkipOfferShown() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	UpdateEdgePointers();
}

void UTutorialOverlayWidget::UpdateEdgePointers()
{
	EdgePointers.Reset();
	const UWorld* World = GetWorld();
	const UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
	APlayerController* PC = GetOwningPlayer();
	if (!Tutorial || !PC)
	{
		return;
	}
	int32 SizeX = 0;
	int32 SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (SizeX <= 0 || SizeY <= 0 || Scale <= 0.f)
	{
		return;
	}
	const FVector2D Size(SizeX / Scale, SizeY / Scale);
	const FVector2D Centre = Size * 0.5f;
	constexpr float EdgeMargin = 48.f;
	for (const ATutorialMarkerActor* Marker : Tutorial->GetVisibleMarkers())
	{
		if (!Marker->bScreenEdgePointer)
		{
			continue;
		}
		FVector2D Screen;
		const bool bProjected = PC->ProjectWorldLocationToScreen(Marker->GetActorLocation(), Screen, true);
		FVector2D Local = Screen / Scale;
		const bool bOnScreen = bProjected && Local.X >= EdgeMargin && Local.Y >= EdgeMargin && Local.X <= Size.X - EdgeMargin && Local.Y <= Size.Y - EdgeMargin;
		if (bOnScreen)
		{
			continue;
		}
		FVector2D Direction = (Local - Centre).GetSafeNormal();
		if (!bProjected)
		{
			Direction = -Direction; // behind the camera: the projection is mirrored
		}
		if (Direction.IsNearlyZero())
		{
			Direction = FVector2D(0.f, 1.f);
		}
		// Clamp the ray from the centre to the inset screen rectangle.
		const FVector2D Half = Centre - FVector2D(EdgeMargin, EdgeMargin);
		const float TX = FMath::Abs(Direction.X) > KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Direction.X) : BIG_NUMBER;
		const float TY = FMath::Abs(Direction.Y) > KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Direction.Y) : BIG_NUMBER;
		FEdgePointer& Pointer = EdgePointers.AddDefaulted_GetRef();
		Pointer.Direction = Direction;
		Pointer.Tip = Centre + Direction * FMath::Min(TX, TY);
	}
}

int32 UTutorialOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	for (const FEdgePointer& Pointer : EdgePointers)
	{
		const FVector2D Side(-Pointer.Direction.Y, Pointer.Direction.X);
		const FVector2D Base = Pointer.Tip - Pointer.Direction * 26.f;
		TArray<FVector2D> Points = { Pointer.Tip, Base + Side * 13.f, Base - Side * 13.f, Pointer.Tip };
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
			PointerColor, true, 4.f);
	}
	return MaxLayer + 1;
}

void UTutorialOverlayWidget::HandleSkipClicked()
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->SkipActiveCard();
	}
}

void UTutorialOverlayWidget::HandleRadioSkipClicked()
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr; Tutorial && Tutorial->SkipSpeech())
	{
		PlayerCommands::Report(this, PlayerCommands::DialogueSkip);
	}
}

void UTutorialOverlayWidget::HandleRetryClicked()
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->RetryFromCheckpoint();
	}
}

void UTutorialOverlayWidget::HandleContinueClicked()
{
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->DismissRetryPrompt();
	}
}

void UTutorialOverlayWidget::HandleSkipAllClicked()
{
	RequestSkipTutorial();
}

void UTutorialOverlayWidget::RequestSkipTutorial()
{
	UWorld* World = GetWorld();
	UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
	UCodexUISubsystem* UI = UCodexUISubsystem::Get(this);
	if (!Tutorial || !UI || !Tutorial->IsTutorialActive())
	{
		return;
	}
	// A misclick must not cost the tutorial: Yes / No first, the world paused meanwhile (the squad does not walk on).
	const bool bWasPaused = UGameplayStatics::IsGamePaused(World);
	UGameplayStatics::SetGamePaused(World, true);
	TWeakObjectPtr<UTutorialSubsystem> WeakTutorial(Tutorial);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("SkipTutorialTitle", "SKIP TUTORIAL"), LOCTEXT("SkipTutorialMessage", "Skip the tutorial? All controls unlock."),
		[WeakTutorial, WeakWorld, bWasPaused](ECodexConfirmResult Result)
		{
			if (UWorld* ResultWorld = WeakWorld.Get(); ResultWorld && !bWasPaused)
			{
				UGameplayStatics::SetGamePaused(ResultWorld, false);
			}
			if (Result == ECodexConfirmResult::Confirmed && WeakTutorial.IsValid())
			{
				WeakTutorial->SkipTutorial();
			}
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport(); // the dialog's buttons must not keep the game keys
			}
		});
}

bool UTutorialOverlayWidget::IsSkipTutorialShown() const
{
	return TutorialSkipAllButton && TutorialSkipAllButton->GetVisibility() == ESlateVisibility::Visible;
}

bool UTutorialOverlayWidget::IsCardShown() const
{
	return TutorialHintCard && TutorialHintCard->GetVisibility() != ESlateVisibility::Collapsed;
}

FText UTutorialOverlayWidget::GetShownCardBody() const
{
	return TutorialCardBody ? TutorialCardBody->GetText() : FText::GetEmpty();
}

FText UTutorialOverlayWidget::GetShownCardKeys() const
{
	return TutorialCardKeys ? TutorialCardKeys->GetText() : FText::GetEmpty();
}

bool UTutorialOverlayWidget::IsSubtitleShown() const
{
	return TutorialRadioStrip && TutorialRadioStrip->GetVisibility() != ESlateVisibility::Collapsed;
}

FText UTutorialOverlayWidget::GetShownSubtitle() const
{
	return TutorialRadioLine ? TutorialRadioLine->GetText() : FText::GetEmpty();
}

FText UTutorialOverlayWidget::GetShownSpeaker() const
{
	return TutorialRadioSpeaker ? TutorialRadioSpeaker->GetText() : FText::GetEmpty();
}

int32 UTutorialOverlayWidget::GetShownObjectiveCount() const
{
	int32 Count = 0;
	for (const UTextBlock* Line : { TutorialObjective1.Get(), TutorialObjective2.Get(), TutorialObjective3.Get() })
	{
		Count += Line && Line->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0;
	}
	return Count;
}

FText UTutorialOverlayWidget::GetShownRadioHint() const
{
	return TutorialRadioHint ? TutorialRadioHint->GetText() : FText::GetEmpty();
}

bool UTutorialOverlayWidget::IsRadioSkipShown() const
{
	return IsSubtitleShown() && TutorialRadioSkipButton && TutorialRadioSkipButton->GetVisibility() == ESlateVisibility::Visible;
}

bool UTutorialOverlayWidget::GetCardAndStripRects(FSlateRect& OutCard, FSlateRect& OutStrip) const
{
	if (!TutorialHintCard || !TutorialRadioStrip)
	{
		return false;
	}
	const FGeometry& Card = TutorialHintCard->GetCachedGeometry();
	const FGeometry& Strip = TutorialRadioStrip->GetCachedGeometry();
	if (Card.GetLocalSize().IsNearlyZero() || Strip.GetLocalSize().IsNearlyZero())
	{
		return false;
	}
	OutCard = Card.GetLayoutBoundingRect();
	OutStrip = Strip.GetLayoutBoundingRect();
	return true;
}

bool UTutorialOverlayWidget::IsRetryShown() const
{
	return TutorialRetryPanel && TutorialRetryPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
