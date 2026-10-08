#include "UI/DeathCinematicOverlayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

void UDeathCinematicOverlayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!DeathFadeBorder || !DeathFallenText)
	{
		BuildDefaultLayout();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDeathCinematicOverlayWidget::BuildDefaultLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DeathRoot"));
	WidgetTree->RootWidget = Root;
	DeathFadeBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeathFadeBorder"));
	DeathFadeBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	UOverlaySlot* FadeSlot = Root->AddChildToOverlay(DeathFadeBorder);
	FadeSlot->SetHorizontalAlignment(HAlign_Fill);
	FadeSlot->SetVerticalAlignment(VAlign_Fill);
	DeathFallenText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DeathFallenText"));
	FSlateFontInfo Font = DeathFallenText->GetFont();
	Font.Size = 40;
	Font.LetterSpacing = 200;
	DeathFallenText->SetFont(Font);
	DeathFallenText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.12f, 0.1f)));
	DeathFallenText->SetJustification(ETextJustify::Center);
	DeathFallenText->SetText(FText::GetEmpty());
	UOverlaySlot* TextSlot = Root->AddChildToOverlay(DeathFallenText);
	TextSlot->SetHorizontalAlignment(HAlign_Center);
	TextSlot->SetVerticalAlignment(VAlign_Center);
}

void UDeathCinematicOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UDeathCinematicOverlayWidget::Refresh()
{
	const UWorld* World = GetWorld();
	const UDeathCinematicSubsystem* DeathCam = World ? World->GetSubsystem<UDeathCinematicSubsystem>() : nullptr;
	ShownAlpha = DeathCam ? DeathCam->GetFadeAlpha() : 0.f;
	if (DeathFadeBorder)
	{
		DeathFadeBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, ShownAlpha));
	}
	if (DeathFallenText)
	{
		DeathFallenText->SetText(DeathCam ? DeathCam->GetDefeatText() : FText::GetEmpty());
	}
}

FText UDeathCinematicOverlayWidget::GetShownText() const
{
	return DeathFallenText ? DeathFallenText->GetText() : FText::GetEmpty();
}
