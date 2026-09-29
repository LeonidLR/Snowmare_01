#include "UI/TransferDialogWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "TransferDialogWidget"

namespace
{
	// Godot transfer_dialog.gd: bg (0.08, 0.06, 0.12, 0.96), border (0.65, 0.18, 0.75), title (0.85, 0.45, 1.0).
	const FLinearColor TransferBack = ACodexTacticsHUD::GodotColor(0.08f, 0.06f, 0.12f, 0.96f);
	const FLinearColor TransferBorder = ACodexTacticsHUD::GodotColor(0.65f, 0.18f, 0.75f);
	const FLinearColor TransferTitle = ACodexTacticsHUD::GodotColor(0.85f, 0.45f, 1.f);
	const FLinearColor TransferButton = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);
	const FLinearColor TransferText(0.95f, 0.95f, 0.95f);
	constexpr int32 TransferItemCount = 14;

	const AOperativeCharacter* TransferLeader(const UWorld* World)
	{
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		return Squad ? Squad->GetLeader() : nullptr;
	}

	FText TransferClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}
}

void UTransferDialogWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TransferRoot"));
	WidgetTree->RootWidget = Root;

	// Godot: centre bottom, 640 x 336, 84 px above the edge.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TransferDialog"));
	Frame->SetBrushColor(TransferBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 1.f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 1.f));
	FrameSlot->SetPosition(FVector2D(0.f, -84.f));
	FrameSlot->SetSize(FVector2D(640.f, 336.f));
	Panel = Frame;

	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TransferBody"));
	Body->SetBrushColor(TransferBack);
	Body->SetPadding(FMargin(14.f, 10.f));
	Frame->SetContent(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TransferColumn"));
	Body->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TransferTitle"));
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 14;
	TitleText->SetFont(TitleFont);
	TitleText->SetColorAndOpacity(FSlateColor(TransferTitle));
	TitleText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("TransferGrid"));
	Grid->SetSlotPadding(FMargin(5.f, 3.f));
	Column->AddChildToVerticalBox(Grid)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	auto MakeButton = [this](const FName& Name, UTextBlock*& OutText)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->SetBackgroundColor(TransferButton);
		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = OutText->GetFont();
		Font.Size = 11;
		OutText->SetFont(Font);
		OutText->SetColorAndOpacity(FSlateColor(TransferText));
		Button->AddChild(OutText);
		return Button;
	};
	for (int32 Index = 0; Index < TransferItemCount; ++Index)
	{
		UTextBlock* Text = nullptr;
		UButton* Button = MakeButton(FName(*FString::Printf(TEXT("BtnTransfer%d"), Index)), Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetHeightOverride(28.f);
		Size->AddChild(Button);
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Size, Index / 2, Index % 2);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		ItemButtons.Add(Button);
		ItemTexts.Add(Text);
	}
	UTextBlock* CloseText = nullptr;
	UButton* CloseButton = MakeButton(TEXT("BtnClose"), CloseText);
	CloseText->SetText(LOCTEXT("Close", "Закрыть"));
	Column->AddChildToVerticalBox(CloseButton);
	CloseButton->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleClose);

	ItemButtons[0]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton0);
	ItemButtons[1]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton1);
	ItemButtons[2]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton2);
	ItemButtons[3]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton3);
	ItemButtons[4]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton4);
	ItemButtons[5]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton5);
	ItemButtons[6]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton6);
	ItemButtons[7]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton7);
	ItemButtons[8]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton8);
	ItemButtons[9]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton9);
	ItemButtons[10]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton10);
	ItemButtons[11]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton11);
	ItemButtons[12]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton12);
	ItemButtons[13]->OnClicked.AddDynamic(this, &UTransferDialogWidget::HandleButton13);
}

void UTransferDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UTransferDialogWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsOpen())
	{
		Refresh();
	}
}

bool UTransferDialogWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UTransferDialogWidget::Open()
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Visible);
		Refresh();
	}
}

void UTransferDialogWidget::Close()
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UTransferDialogWidget::Toggle()
{
	IsOpen() ? Close() : Open();
}

FText UTransferDialogWidget::GetTitleText() const
{
	const AOperativeCharacter* Leader = TransferLeader(GetWorld());
	return Leader ? TransferClean(FString::Printf(TEXT("🟣 ПЕРЕДАЧА: %s"), *Leader->DisplayName.ToUpper().ToString()))
		: TransferClean(TEXT("🟣 ВЫБЕРИТЕ ПРЕДМЕТ ИЛИ ПАТРОНЫ ДЛЯ ПЕРЕДАЧИ"));
}

FText UTransferDialogWidget::GetItemText(ETransferItem Item) const
{
	const AOperativeCharacter* Leader = TransferLeader(GetWorld());
	const int32 Count = Leader ? TransferRules::GetAvailable(*Leader, Item) : 0;
	switch (Item)
	{
	case ETransferItem::Turret: return TransferClean(FString::Printf(TEXT("🛠️ Турель (%d шт.)"), Count));
	case ETransferItem::Barricade: return TransferClean(FString::Printf(TEXT("🧱 Баррикада (%d шт.)"), Count));
	case ETransferItem::Mine: return TransferClean(FString::Printf(TEXT("💣 Мина (%d шт.)"), Count));
	case ETransferItem::Medkit: return TransferClean(FString::Printf(TEXT("🩹 Аптечка (%d шт.)"), Count));
	case ETransferItem::RifleAmmo: return TransferClean(FString::Printf(TEXT("🔫 M16 [30 шт.] (Запас: %d)"), Count));
	case ETransferItem::PistolAmmo: return TransferClean(FString::Printf(TEXT("🔫 Пистолет [12 шт.] (Запас: %d)"), Count));
	case ETransferItem::CannedFood: return TransferClean(FString::Printf(TEXT("🥫 Консервы (%d шт.)"), Count));
	case ETransferItem::ShotgunAmmo: return TransferClean(TEXT("💥 Дробь 12k (8 шт.)"));
	case ETransferItem::Bread: return TransferClean(FString::Printf(TEXT("🍞 Хлеб (%d шт.)"), Count));
	case ETransferItem::FlameFuel: return TransferClean(TEXT("🔥 Топливо (25 ед.)"));
	case ETransferItem::Chocolate: return TransferClean(FString::Printf(TEXT("🍫 Шоколад (%d шт.)"), Count));
	case ETransferItem::CryoAmmo: return TransferClean(TEXT("❄️ Хладагент (15 ед.)"));
	case ETransferItem::Matches: return TransferClean(FString::Printf(TEXT("🪵 Спички (%d шт.)"), Count));
	default: return TransferClean(TEXT("⚡ Плазма (10 ед.)"));
	}
}

bool UTransferDialogWidget::IsItemEnabled(ETransferItem Item) const
{
	const AOperativeCharacter* Leader = TransferLeader(GetWorld());
	return Leader && TransferRules::GetAvailable(*Leader, Item) > 0;
}

bool UTransferDialogWidget::IsItemVisible(ETransferItem Item) const
{
	// Godot hides bread and the special ammo the operative does not carry.
	switch (Item)
	{
	case ETransferItem::Bread:
	case ETransferItem::ShotgunAmmo:
	case ETransferItem::FlameFuel:
	case ETransferItem::CryoAmmo:
	case ETransferItem::PlasmaAmmo:
		return IsItemEnabled(Item);
	default:
		return true;
	}
}

void UTransferDialogWidget::Refresh()
{
	if (TitleText)
	{
		TitleText->SetText(GetTitleText());
	}
	int32 Visible = 0;
	for (int32 Index = 0; Index < ItemButtons.Num(); ++Index)
	{
		const ETransferItem Item = static_cast<ETransferItem>(Index);
		ItemTexts[Index]->SetText(GetItemText(Item));
		ItemButtons[Index]->SetIsEnabled(IsItemEnabled(Item));
		UWidget* Cell = ItemButtons[Index]->GetParent();
		const bool bVisible = IsItemVisible(Item);
		Cell->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		// Godot GridContainer reflows around hidden lines.
		if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Cell->Slot); GridSlot && bVisible)
		{
			GridSlot->SetRow(Visible / 2);
			GridSlot->SetColumn(Visible % 2);
			++Visible;
		}
	}
}

void UTransferDialogWidget::Choose(ETransferItem Item)
{
	Close();
	if (USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>())
	{
		Transfer->StartTransferMode(Item);
	}
}

void UTransferDialogWidget::HandleButton0() { Choose(static_cast<ETransferItem>(0)); }
void UTransferDialogWidget::HandleButton1() { Choose(static_cast<ETransferItem>(1)); }
void UTransferDialogWidget::HandleButton2() { Choose(static_cast<ETransferItem>(2)); }
void UTransferDialogWidget::HandleButton3() { Choose(static_cast<ETransferItem>(3)); }
void UTransferDialogWidget::HandleButton4() { Choose(static_cast<ETransferItem>(4)); }
void UTransferDialogWidget::HandleButton5() { Choose(static_cast<ETransferItem>(5)); }
void UTransferDialogWidget::HandleButton6() { Choose(static_cast<ETransferItem>(6)); }
void UTransferDialogWidget::HandleButton7() { Choose(static_cast<ETransferItem>(7)); }
void UTransferDialogWidget::HandleButton8() { Choose(static_cast<ETransferItem>(8)); }
void UTransferDialogWidget::HandleButton9() { Choose(static_cast<ETransferItem>(9)); }
void UTransferDialogWidget::HandleButton10() { Choose(static_cast<ETransferItem>(10)); }
void UTransferDialogWidget::HandleButton11() { Choose(static_cast<ETransferItem>(11)); }
void UTransferDialogWidget::HandleButton12() { Choose(static_cast<ETransferItem>(12)); }
void UTransferDialogWidget::HandleButton13() { Choose(static_cast<ETransferItem>(13)); }
void UTransferDialogWidget::HandleClose() { Close(); }

#undef LOCTEXT_NAMESPACE
