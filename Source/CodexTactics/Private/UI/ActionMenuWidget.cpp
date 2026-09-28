#include "UI/ActionMenuWidget.h"
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
#include "Interactables/InteractionSubsystem.h"
#include "UI/CodexTacticsHUD.h"

namespace
{
	const FLinearColor MenuPanelColor(0.03f, 0.045f, 0.06f, 0.93f);
	const FLinearColor MenuTitleColor(1.f, 0.85f, 0.35f);
	const FLinearColor MenuBodyColor(0.9f, 0.92f, 0.95f);

	void MenuSetFontSize(UTextBlock* Text, int32 Size)
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
	}

	/** Canvas / Slate default fonts have no emoji: drop them like the HUD feed does. */
	FText MenuClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}
}

TSharedRef<SWidget> UActionMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	return Super::RebuildWidget();
}

UButton* UActionMenuWidget::MakeButton(const FName& Name, TObjectPtr<UTextBlock>& OutLabel)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(Name.ToString() + TEXT("Label")));
	MenuSetFontSize(OutLabel, 12);
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f)));
	Button->AddChild(OutLabel);
	return Button;
}

void UActionMenuWidget::BuildDefaultLayout()
{
	// Full-screen overlay that lets clicks through; the panel sits in the centre (Godot anchors 0.5 / 0.5).
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(MenuPanelColor);
	Panel->SetPadding(FMargin(18.f, 14.f));
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);
	PanelSlot->SetVerticalAlignment(VAlign_Center);

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size"));
	Size->SetMinDesiredWidth(420.f);
	Size->SetMaxDesiredWidth(560.f);
	Size->SetMinDesiredHeight(160.f);
	Panel->SetContent(Size);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Size->AddChild(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	MenuSetFontSize(TitleText, 16);
	TitleText->SetColorAndOpacity(FSlateColor(MenuTitleColor));
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	DescriptionText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DescriptionText"));
	MenuSetFontSize(DescriptionText, 12);
	DescriptionText->SetColorAndOpacity(FSlateColor(MenuBodyColor));
	DescriptionText->SetAutoWrapText(true);
	UVerticalBoxSlot* DescSlot = Column->AddChildToVerticalBox(DescriptionText);
	DescSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	DescSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	Column->AddChildToVerticalBox(Buttons)->SetHorizontalAlignment(HAlign_Right);

	ConfirmButton = MakeButton(TEXT("ConfirmButton"), ConfirmText);
	RelocateButton = MakeButton(TEXT("RelocateButton"), RelocateText);
	TrapButton = MakeButton(TEXT("TrapButton"), TrapText);
	CancelButton = MakeButton(TEXT("CancelButton"), CancelText);
	for (UButton* Button : { ConfirmButton.Get(), RelocateButton.Get(), TrapButton.Get(), CancelButton.Get() })
	{
		Buttons->AddChildToHorizontalBox(Button)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
	}
}

void UActionMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Initialized before the first RebuildWidget: build the default layout now so the buttons can be bound.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.AddDynamic(this, &UActionMenuWidget::HandleConfirm);
	}
	if (RelocateButton)
	{
		RelocateButton->OnClicked.AddDynamic(this, &UActionMenuWidget::HandleRelocate);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &UActionMenuWidget::HandleCancel);
	}
	if (TrapButton)
	{
		TrapButton->OnClicked.AddDynamic(this, &UActionMenuWidget::HandleTrap);
	}
}

void UActionMenuWidget::ShowMenu(const FActionMenuSpec& Menu)
{
	if (TitleText)
	{
		TitleText->SetText(MenuClean(Menu.Title));
	}
	if (DescriptionText)
	{
		DescriptionText->SetText(MenuClean(Menu.Description));
	}
	if (ConfirmText)
	{
		ConfirmText->SetText(MenuClean(Menu.ConfirmText));
	}
	if (ConfirmButton)
	{
		ConfirmButton->SetIsEnabled(!Menu.bConfirmDisabled);
	}
	if (CancelText)
	{
		CancelText->SetText(MenuClean(Menu.CancelText));
	}
	if (RelocateButton)
	{
		RelocateButton->SetVisibility(Menu.bAllowRelocate ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (RelocateText)
	{
		RelocateText->SetText(MenuClean(Menu.RelocateText));
	}
	if (TrapButton)
	{
		TrapButton->SetVisibility(Menu.bAllowTrap ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		TrapButton->SetIsEnabled(!Menu.bTrapDisabled);
	}
	if (TrapText)
	{
		TrapText->SetText(MenuClean(Menu.TrapText));
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ReceiveMenuShown(Menu);
}

void UActionMenuWidget::HideMenu()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UActionMenuWidget::HandleConfirm()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->ConfirmActionMenu();
	}
}

void UActionMenuWidget::HandleRelocate()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->RelocateActionMenu();
	}
}

void UActionMenuWidget::HandleTrap()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->TrapActionMenu();
	}
}

void UActionMenuWidget::HandleCancel()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->CancelActionMenu();
	}
}
