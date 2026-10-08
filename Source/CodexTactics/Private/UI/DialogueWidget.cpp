#include "UI/DialogueWidget.h"
#include "UI/CodexButtonFocus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/DialogueSequenceAsset.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueRules.h"
#include "UI/DialogueSubsystem.h"

#define LOCTEXT_NAMESPACE "DialogueWidget"

namespace
{
	// Godot bottom_dialogue_dialog.gd panel / label colours.
	const FLinearColor DialogPanelColor = ACodexTacticsHUD::GodotColor(0.06f, 0.09f, 0.14f, 0.96f);
	const FLinearColor DialogFrameColor = ACodexTacticsHUD::GodotColor(0.2f, 0.75f, 1.f, 0.85f);
	const FLinearColor DialogBadgeColor = ACodexTacticsHUD::GodotColor(0.4f, 0.85f, 1.f, 0.9f);
	const FLinearColor DialogProgressColor = ACodexTacticsHUD::GodotColor(0.65f, 0.75f, 0.85f, 0.8f);
	const FLinearColor DialogSpeechColor = ACodexTacticsHUD::GodotColor(1.f, 1.f, 1.f, 0.95f);
	const FLinearColor DialogHintColor = ACodexTacticsHUD::GodotColor(0.5f, 0.6f, 0.7f, 0.6f);
	const FLinearColor DialogButtonTextColor(0.05f, 0.05f, 0.05f);

	FText DialogClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}

	FLinearColor DialogGodot(const FLinearColor& SRGB)
	{
		return ACodexTacticsHUD::GodotColor(SRGB.R, SRGB.G, SRGB.B, SRGB.A);
	}
}

UTextBlock* UDialogueWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color, bool bCenter)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	if (bCenter)
	{
		Text->SetJustification(ETextJustify::Center);
	}
	return Text;
}

void UDialogueWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DialogRoot"));
	WidgetTree->RootWidget = Root;

	// Frame (2 px border) anchored bottom centre, 860 x 185, 20 px above the edge.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogFrame"));
	Frame->SetBrushColor(DialogFrameColor);
	Frame->SetPadding(FMargin(2.f));
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 1.f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 1.f));
	FrameSlot->SetPosition(FVector2D(0.f, -20.f));
	FrameSlot->SetSize(FVector2D(860.f, 185.f));

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogPanel"));
	Panel->SetBrushColor(DialogPanelColor);
	Panel->SetPadding(FMargin(14.f, 12.f));
	Frame->SetContent(Panel);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DialogRow"));
	Panel->SetContent(Row);

	// 1. Speaker card.
	DialogCardFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogCardFrame"));
	DialogCardFrame->SetPadding(FMargin(1.f));
	USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialogCardSize"));
	CardSize->SetWidthOverride(160.f);
	CardSize->AddChild(DialogCardFrame);
	Row->AddChildToHorizontalBox(CardSize)->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));

	DialogCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogCard"));
	DialogCard->SetPadding(FMargin(8.f));
	DialogCard->SetVerticalAlignment(VAlign_Center);
	DialogCardFrame->SetContent(DialogCard);

	UVerticalBox* CardColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogCardColumn"));
	DialogCard->SetContent(CardColumn);
	DialogPortraitText = MakeText(TEXT("DialogPortraitText"), 26, DialogSpeechColor, true);
	CardColumn->AddChildToVerticalBox(DialogPortraitText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	DialogNameText = MakeText(TEXT("DialogNameText"), 13, DialogSpeechColor, true);
	DialogNameText->SetAutoWrapText(true);
	CardColumn->AddChildToVerticalBox(DialogNameText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	DialogRoleText = MakeText(TEXT("DialogRoleText"), 10, DialogProgressColor, true);
	CardColumn->AddChildToVerticalBox(DialogRoleText);

	// 2. Separator.
	UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogSeparator"));
	Separator->SetBrushColor(DialogHintColor);
	Separator->SetPadding(FMargin(1.f, 0.f));
	Row->AddChildToHorizontalBox(Separator)->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));

	// 3. Content.
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogContent"));
	UHorizontalBoxSlot* ContentSlot = Row->AddChildToHorizontalBox(Content);
	ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UHorizontalBox* TopBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DialogTopBar"));
	Content->AddChildToVerticalBox(TopBar)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UTextBlock* Badge = MakeText(TEXT("DialogBadgeText"), 11, DialogBadgeColor, false);
	Badge->SetText(DialogClean(LOCTEXT("Badge", "💬 DIALOGUE")));
	TopBar->AddChildToHorizontalBox(Badge)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	DialogProgressText = MakeText(TEXT("DialogProgressText"), 11, DialogProgressColor, false);
	TopBar->AddChildToHorizontalBox(DialogProgressText);

	DialogSpeechText = MakeText(TEXT("DialogSpeechText"), 14, DialogSpeechColor, false);
	DialogSpeechText->SetAutoWrapText(true);
	UVerticalBoxSlot* SpeechSlot = Content->AddChildToVerticalBox(DialogSpeechText);
	SpeechSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SpeechSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UHorizontalBox* BottomBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DialogBottomBar"));
	Content->AddChildToVerticalBox(BottomBar);
	UTextBlock* Hint = MakeText(TEXT("DialogHintText"), 10, DialogHintColor, false);
	Hint->SetText(LOCTEXT("Hint", "[Space] next, [Esc] skip"));
	UHorizontalBoxSlot* HintSlot = BottomBar->AddChildToHorizontalBox(Hint);
	HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HintSlot->SetVerticalAlignment(VAlign_Center);

	auto AddButton = [this, BottomBar](const TCHAR* Name, const TCHAR* TextName, const FText& Label, float Width, UTextBlock** OutText) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		CodexButtonFocus::Disable(Button); // a focused HUD button would swallow the game keys (1-4, ...)
		UTextBlock* Text = MakeText(TextName, 12, DialogButtonTextColor, true);
		Text->SetText(DialogClean(Label));
		Text->SetAutoWrapText(false); // long finish labels ("Hang on! We're coming! ▶") widen the button
		Button->AddChild(Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetMinDesiredWidth(Width);
		Size->SetHeightOverride(30.f);
		Size->AddChild(Button);
		BottomBar->AddChildToHorizontalBox(Size)->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		if (OutText)
		{
			*OutText = Text;
		}
		return Button;
	};
	DialogSkipButton = AddButton(TEXT("DialogSkipButton"), TEXT("DialogSkipText"), LOCTEXT("Skip", "Skip ⏭"), 130.f, nullptr);
	UTextBlock* NextText = nullptr;
	DialogNextButton = AddButton(TEXT("DialogNextButton"), TEXT("DialogNextText"), LOCTEXT("Next", "Next ▶"), 140.f, &NextText);
	DialogNextText = NextText;
}

void UDialogueWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (DialogSkipButton)
	{
		DialogSkipButton->OnClicked.AddDynamic(this, &UDialogueWidget::HandleSkip);
	}
	if (DialogNextButton)
	{
		DialogNextButton->OnClicked.AddDynamic(this, &UDialogueWidget::HandleNext);
	}
}

void UDialogueWidget::Refresh()
{
	const UDialogueSubsystem* Dialogue = GetWorld() ? GetWorld()->GetSubsystem<UDialogueSubsystem>() : nullptr;
	const UDialogueSequenceAsset* Sequence = Dialogue ? Dialogue->GetCurrentSequence() : nullptr;
	if (!Sequence || !Sequence->Lines.IsValidIndex(Dialogue->GetLineIndex()))
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const int32 Index = Dialogue->GetLineIndex();
	const FDialogueLine& Line = Sequence->Lines[Index];
	const FString Speaker = Line.SpeakerName.IsEmpty() ? TEXT("Unknown") : Line.SpeakerName;
	const FDialogueSpeakerStyle Style = DialogueRules::GetSpeakerStyle(Speaker);
	if (DialogPortraitText)
	{
		DialogPortraitText->SetText(Style.Portrait);
		DialogPortraitText->SetColorAndOpacity(FSlateColor(DialogGodot(Style.NameColor)));
	}
	if (DialogNameText)
	{
		DialogNameText->SetText(DialogClean(FText::FromString(Speaker)));
		DialogNameText->SetColorAndOpacity(FSlateColor(DialogGodot(Style.NameColor)));
	}
	if (DialogRoleText)
	{
		DialogRoleText->SetText(Style.Role);
	}
	if (DialogCard)
	{
		DialogCard->SetBrushColor(DialogGodot(Style.CardColor));
	}
	if (DialogCardFrame)
	{
		DialogCardFrame->SetBrushColor(DialogGodot(Style.CardBorderColor));
	}
	if (DialogProgressText)
	{
		DialogProgressText->SetText(DialogueRules::GetProgressText(Index, Sequence->Lines.Num()));
	}
	if (DialogSpeechText)
	{
		DialogSpeechText->SetText(DialogClean(FText::FromString(Line.Text)));
	}
	if (DialogNextText)
	{
		// Godot GameState.is_combat_phase: from the pre-combat cutscene until the defence is over.
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		const ECodexGamePhase Phase = Flow ? Flow->GetPhase() : ECodexGamePhase::Exploration;
		const bool bCombat = Phase != ECodexGamePhase::Exploration && Phase != ECodexGamePhase::PostCombat && Phase != ECodexGamePhase::GameOver;
		DialogNextText->SetText(DialogClean(DialogueRules::GetNextButtonText(Index == Sequence->Lines.Num() - 1,
			Sequence->CustomFinishButtonText, Sequence->bIsRecruitmentDialogue, bCombat)));
	}
	// The full-screen root lets world clicks through; the panel itself is hit-testable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

FReply UDialogueWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Godot: a click on the dialogue panel advances (the buttons handle their own clicks first).
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		HandleNext();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UDialogueWidget::HandleSkip()
{
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->SkipDialogue();
	}
}

void UDialogueWidget::HandleNext()
{
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->AdvanceLine();
	}
}

#undef LOCTEXT_NAMESPACE
