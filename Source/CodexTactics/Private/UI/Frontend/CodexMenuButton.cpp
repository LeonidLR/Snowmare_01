#include "UI/Frontend/CodexMenuButton.h"
#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"

void UCodexMenuButton::SetButtonText(const FText& InText)
{
	ButtonText = InText;
	RefreshLabel();
}

FText UCodexMenuButton::GetDisplayedDescription() const
{
	return !GetIsEnabled() && !DisabledReasonText.IsEmpty() ? DisabledReasonText : DescriptionText;
}

void UCodexMenuButton::SetEntryEnabled(bool bEnabled, const FText& DisabledReason)
{
	DisabledReasonText = bEnabled ? FText() : DisabledReason;
	SetIsEnabled(bEnabled);
}

void UCodexMenuButton::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree(*WidgetTree);
	}
	if (!ButtonLabel)
	{
		ButtonLabel = Cast<UTextBlock>(GetWidgetFromName(TEXT("ButtonLabel")));
	}
	Super::NativeOnInitialized();
	RefreshLabel();
}

void UCodexMenuButton::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshLabel();
}

void UCodexMenuButton::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();
	if (UCommonTextBlock* CommonLabel = Cast<UCommonTextBlock>(ButtonLabel))
	{
		if (const TSubclassOf<UCommonTextStyle> TextStyle = GetCurrentTextStyleClass())
		{
			CommonLabel->SetStyle(TextStyle);
		}
	}
}

void UCodexMenuButton::RefreshLabel()
{
	if (ButtonLabel && !ButtonText.IsEmpty())
	{
		ButtonLabel->SetText(ButtonText);
	}
}

void UCodexMenuButton::BuildDefaultTree(UWidgetTree& Tree) const
{
	USizeBox* Size = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
	Size->SetMinDesiredWidth(160.f);
	Size->SetHeightOverride(46.f);
	Tree.RootWidget = Size;
	UOverlay* Content = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ButtonContent"));
	Size->AddChild(Content);
	// A UCommonTextBlock so the button style's text styles (normal / hovered / disabled) apply per state.
	UCommonTextBlock* Label = Tree.ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), TEXT("ButtonLabel"));
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 20;
	Font.TypefaceFontName = TEXT("Bold");
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(CodexDefaultTree::TitleColor));
	Label->SetText(NSLOCTEXT("CodexMenuButton", "Placeholder", "MENU ENTRY"));
	CodexDefaultTree::MarkVariable(Label);
	UOverlaySlot* LayerSlot = Content->AddChildToOverlay(Label);
	LayerSlot->SetVerticalAlignment(VAlign_Center);
	LayerSlot->SetHorizontalAlignment(HAlign_Left);
	LayerSlot->SetPadding(FMargin(18.f, 0.f));
}
