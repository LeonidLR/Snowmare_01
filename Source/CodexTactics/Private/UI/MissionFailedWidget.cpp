#include "UI/MissionFailedWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "MissionFailedWidget"

namespace
{
	// Godot GameOverPanel: DimOverlay, StyleBoxFlat_dialog, label colours.
	const FLinearColor FailedDimColor = ACodexTacticsHUD::GodotColor(0.1f, 0.02f, 0.04f, 0.92f);
	const FLinearColor FailedPanelColor = ACodexTacticsHUD::GodotColor(0.08f, 0.09f, 0.12f, 0.9f);
	const FLinearColor FailedFrameColor = ACodexTacticsHUD::GodotColor(0.3f, 0.4f, 0.55f, 1.f);
	const FLinearColor FailedTitleColor = ACodexTacticsHUD::GodotColor(1.f, 0.25f, 0.25f);
	const FLinearColor FailedReasonColor = ACodexTacticsHUD::GodotColor(0.92f, 0.88f, 0.88f);
	const FLinearColor FailedTipColor = ACodexTacticsHUD::GodotColor(0.65f, 0.7f, 0.78f);
	const FLinearColor FailedButtonTextColor = ACodexTacticsHUD::GodotColor(0.05f, 0.05f, 0.05f);

	FText FailedClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}
}

UTextBlock* UMissionFailedWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(ETextJustify::Center);
	Text->SetAutoWrapText(true);
	return Text;
}

void UMissionFailedWidget::BuildDefaultLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FailedRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FailedDim"));
	Dim->SetBrushColor(FailedDimColor);
	UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim);
	DimSlot->SetHorizontalAlignment(HAlign_Fill);
	DimSlot->SetVerticalAlignment(VAlign_Fill);

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FailedFrame"));
	Frame->SetBrushColor(FailedFrameColor);
	Frame->SetPadding(FMargin(2.f));
	UOverlaySlot* FrameSlot = Root->AddChildToOverlay(Frame);
	FrameSlot->SetHorizontalAlignment(HAlign_Center);
	FrameSlot->SetVerticalAlignment(VAlign_Center);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FailedPanel"));
	Panel->SetBrushColor(FailedPanelColor);
	Panel->SetPadding(FMargin(28.f, 22.f));
	Frame->SetContent(Panel);

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FailedSize"));
	Size->SetWidthOverride(480.f);
	Size->SetMinDesiredHeight(260.f);
	Panel->SetContent(Size);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FailedColumn"));
	Size->AddChild(Column);

	FailedTitleText = MakeText(TEXT("FailedTitleText"), 20, FailedTitleColor);
	FailedTitleText->SetText(LOCTEXT("Title", "❄️ MISSION FAILED ❄️"));
	Column->AddChildToVerticalBox(FailedTitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	FailedReasonText = MakeText(TEXT("FailedReasonText"), 14, FailedReasonColor);
	Column->AddChildToVerticalBox(FailedReasonText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	FailedTipText = MakeText(TEXT("FailedTipText"), 12, FailedTipColor);
	FailedTipText->SetText(LOCTEXT("Tip", "Tip: light barrels with matches or warm up at a running generator to avoid frostbite."));
	Column->AddChildToVerticalBox(FailedTipText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FailedSeparator"));
	Separator->SetBrushColor(FailedFrameColor);
	Separator->SetPadding(FMargin(0.f, 1.f));
	Column->AddChildToVerticalBox(Separator)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	FailedRestartButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("FailedRestartButton"));
	FailedRestartText = MakeText(TEXT("FailedRestartText"), 14, FailedButtonTextColor);
	FailedRestartText->SetText(LOCTEXT("Restart", "🔄 Restart"));
	FailedRestartButton->AddChild(FailedRestartText);
	USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FailedButtonSize"));
	ButtonSize->SetHeightOverride(42.f);
	ButtonSize->AddChild(FailedRestartButton);
	Column->AddChildToVerticalBox(ButtonSize);
}

void UMissionFailedWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (FailedRestartButton)
	{
		FailedRestartButton->OnClicked.AddDynamic(this, &UMissionFailedWidget::HandleRestart);
	}
	for (UTextBlock* Text : { FailedTitleText.Get(), FailedTipText.Get(), FailedRestartText.Get() })
	{
		if (Text)
		{
			Text->SetText(FailedClean(Text->GetText()));
		}
	}
}

void UMissionFailedWidget::ShowFailure(const FText& Reason)
{
	if (FailedReasonText)
	{
		FailedReasonText->SetText(FailedClean(Reason));
	}
	SetVisibility(ESlateVisibility::Visible);
}

void UMissionFailedWidget::HideScreen()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMissionFailedWidget::HandleRestart()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->RestartMission();
	}
}

#undef LOCTEXT_NAMESPACE
