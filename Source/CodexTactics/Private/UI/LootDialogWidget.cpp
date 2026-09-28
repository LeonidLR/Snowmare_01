#include "UI/LootDialogWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "LootDialogWidget"

namespace
{
	// Godot loot_dialog.gd style: dark green panel, green title.
	const FLinearColor PanelColor(0.08f, 0.12f, 0.09f, 0.97f);
	const FLinearColor FrameColor(0.2f, 0.85f, 0.35f, 1.f);
	const FLinearColor TitleColor(0.3f, 1.f, 0.5f);
	const FLinearColor HintColor(0.8f, 0.85f, 0.9f);
	const FLinearColor EmptyColor(0.6f, 0.6f, 0.6f);
	const FLinearColor ButtonTextColor(0.05f, 0.05f, 0.05f);

	FText Clean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}
}

void ULootEntryButton::Setup(ULootDialogWidget* InOwner, ELootItem InItem)
{
	Owner = InOwner;
	Item = InItem;
	OnClicked.AddUniqueDynamic(this, &ULootEntryButton::HandleClicked);
}

void ULootEntryButton::HandleClicked()
{
	if (ULootDialogWidget* Dialog = Owner.Get())
	{
		Dialog->HandleItem(Item);
	}
}

UTextBlock* ULootDialogWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

void ULootDialogWidget::BuildDefaultLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	// Green frame = outer border, panel = inner border (Godot StyleBoxFlat border 2 px).
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
	Frame->SetBrushColor(FrameColor);
	Frame->SetPadding(FMargin(2.f));
	UOverlaySlot* FrameSlot = Root->AddChildToOverlay(Frame);
	FrameSlot->SetHorizontalAlignment(HAlign_Center);
	FrameSlot->SetVerticalAlignment(VAlign_Center);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(16.f, 14.f));
	Frame->SetContent(Panel);

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size"));
	Size->SetWidthOverride(540.f);
	Size->SetHeightOverride(420.f);
	Panel->SetContent(Size);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Size->AddChild(Column);

	TitleText = MakeText(TEXT("TitleText"), 16, TitleColor);
	TitleText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	SubtitleText = MakeText(TEXT("SubtitleText"), 11, HintColor);
	SubtitleText->SetJustification(ETextJustify::Center);
	SubtitleText->SetAutoWrapText(true);
	SubtitleText->SetText(LOCTEXT("Hint", "Кликните на конкретный ресурс, чтобы забрать его, или нажмите «Забрать всё»:"));
	Column->AddChildToVerticalBox(SubtitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Scroll"));
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(Scroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ItemsGrid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("ItemsGrid"));
	ItemsGrid->SetSlotPadding(FMargin(4.f));
	Scroll->AddChild(ItemsGrid);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	Column->AddChildToVerticalBox(Buttons)->SetHorizontalAlignment(HAlign_Center);

	LootAllButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("LootAllButton"));
	LootAllText = MakeText(TEXT("LootAllText"), 12, ButtonTextColor);
	LootAllText->SetText(LOCTEXT("LootAll", "📦 Забрать ВСЁ"));
	LootAllButton->AddChild(LootAllText);
	Buttons->AddChildToHorizontalBox(LootAllButton)->SetPadding(FMargin(6.f, 10.f, 6.f, 0.f));

	CloseButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseButton"));
	CloseText = MakeText(TEXT("CloseText"), 12, ButtonTextColor);
	CloseText->SetText(LOCTEXT("Close", "✖ Закрыть"));
	CloseButton->AddChild(CloseText);
	Buttons->AddChildToHorizontalBox(CloseButton)->SetPadding(FMargin(6.f, 10.f, 6.f, 0.f));
}

void ULootDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (LootAllButton)
	{
		LootAllButton->OnClicked.AddDynamic(this, &ULootDialogWidget::HandleLootAll);
	}
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &ULootDialogWidget::HandleClose);
	}
	if (LootAllText)
	{
		LootAllText->SetText(Clean(LootAllText->GetText()));
	}
	if (CloseText)
	{
		CloseText->SetText(Clean(CloseText->GetText()));
	}
}

void ULootDialogWidget::ShowCrate(ALootCrateActor* Crate)
{
	if (!Crate)
	{
		HideDialog();
		return;
	}
	if (TitleText)
	{
		TitleText->SetText(Clean(FText::FromString(Crate->CrateName.ToString().ToUpper())));
	}
	const TArray<FLootEntry> Items = Crate->GetItems();
	if (ItemsGrid)
	{
		ItemsGrid->ClearChildren();
		if (Items.IsEmpty())
		{
			UTextBlock* Empty = MakeText(NAME_None, 12, EmptyColor);
			Empty->SetText(LOCTEXT("Empty", "Ящик пуст. Все припасы забраны."));
			ItemsGrid->AddChildToUniformGrid(Empty, 0, 0);
		}
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			ULootEntryButton* Button = WidgetTree->ConstructWidget<ULootEntryButton>(ULootEntryButton::StaticClass());
			UTextBlock* Label = MakeText(NAME_None, 11, ButtonTextColor);
			Label->SetText(Clean(Items[Index].GetLabel()));
			Button->AddChild(Label);
			Button->Setup(this, Items[Index].Item);
			ItemsGrid->AddChildToUniformGrid(Button, Index / 2, Index % 2); // Godot GridContainer columns = 2
		}
	}
	if (LootAllButton)
	{
		LootAllButton->SetIsEnabled(!Items.IsEmpty());
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void ULootDialogWidget::HideDialog()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void ULootDialogWidget::HandleItem(ELootItem Item)
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->LootItem(Item);
	}
}

void ULootDialogWidget::HandleLootAll()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->LootAll();
	}
}

void ULootDialogWidget::HandleClose()
{
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->CloseLootDialog();
	}
}

#undef LOCTEXT_NAMESPACE
