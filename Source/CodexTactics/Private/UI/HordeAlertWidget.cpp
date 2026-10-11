#include "UI/HordeAlertWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Combat/HordeIntroSubsystem.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

void UHordeAlertWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!HordeAlertTitle)
	{
		BuildDefaultLayout();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UHordeAlertWidget::BuildDefaultLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HordeAlertRoot"));
	WidgetTree->RootWidget = Root;
	HordeAlertBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HordeAlertBand"));
	HordeAlertBand->SetBrushColor(FLinearColor(0.02f, 0.f, 0.f, 0.6f));
	HordeAlertBand->SetPadding(FMargin(40.f, 14.f));
	UOverlaySlot* BandSlot = Root->AddChildToOverlay(HordeAlertBand);
	BandSlot->SetHorizontalAlignment(HAlign_Fill);
	BandSlot->SetVerticalAlignment(VAlign_Top);
	BandSlot->SetPadding(FMargin(0.f, 150.f, 0.f, 0.f)); // upper third: the shots keep the centre

	UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HordeAlertLines"));
	HordeAlertBand->SetContent(Lines);
	HordeAlertTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HordeAlertTitle"));
	FSlateFontInfo TitleFont = HordeAlertTitle->GetFont();
	TitleFont.Size = 46;
	TitleFont.LetterSpacing = 250;
	HordeAlertTitle->SetFont(TitleFont);
	HordeAlertTitle->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.16f, 0.1f)));
	HordeAlertTitle->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* TitleSlot = Lines->AddChildToVerticalBox(HordeAlertTitle);
	TitleSlot->SetHorizontalAlignment(HAlign_Center);
	HordeAlertSubtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HordeAlertSubtitle"));
	FSlateFontInfo SubtitleFont = HordeAlertSubtitle->GetFont();
	SubtitleFont.Size = 20;
	SubtitleFont.LetterSpacing = 120;
	HordeAlertSubtitle->SetFont(SubtitleFont);
	HordeAlertSubtitle->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.92f, 0.75f)));
	HordeAlertSubtitle->SetJustification(ETextJustify::Center);
	UVerticalBoxSlot* SubtitleSlot = Lines->AddChildToVerticalBox(HordeAlertSubtitle);
	SubtitleSlot->SetHorizontalAlignment(HAlign_Center);
}

void UHordeAlertWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UHordeAlertWidget::Refresh()
{
	const UWorld* World = GetWorld();
	const UHordeIntroSubsystem* Intro = World ? World->GetSubsystem<UHordeIntroSubsystem>() : nullptr;
	ShownAlpha = Intro ? Intro->GetAlertAlpha() : 0.f;
	SetRenderOpacity(ShownAlpha);
	if (HordeAlertTitle)
	{
		HordeAlertTitle->SetText(Intro ? Intro->GetAlertTitle() : FText::GetEmpty());
	}
	if (HordeAlertSubtitle)
	{
		HordeAlertSubtitle->SetText(Intro ? Intro->GetAlertSubtitle() : FText::GetEmpty());
	}
}

FText UHordeAlertWidget::GetShownTitle() const
{
	return HordeAlertTitle ? HordeAlertTitle->GetText() : FText::GetEmpty();
}

FText UHordeAlertWidget::GetShownSubtitle() const
{
	return HordeAlertSubtitle ? HordeAlertSubtitle->GetText() : FText::GetEmpty();
}
