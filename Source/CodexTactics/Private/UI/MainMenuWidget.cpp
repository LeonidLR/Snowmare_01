#include "UI/MainMenuWidget.h"
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

#define LOCTEXT_NAMESPACE "MainMenuWidget"

namespace
{
	// Godot StartMenu: DimOverlay, StyleBoxFlat_dialog, Title / Subtitle colours.
	const FLinearColor StartDimColor = ACodexTacticsHUD::GodotColor(0.04f, 0.06f, 0.1f, 0.88f);
	const FLinearColor StartPanelColor = ACodexTacticsHUD::GodotColor(0.08f, 0.09f, 0.12f, 0.9f);
	const FLinearColor StartFrameColor = ACodexTacticsHUD::GodotColor(0.3f, 0.4f, 0.55f, 1.f);
	const FLinearColor StartTitleColor = ACodexTacticsHUD::GodotColor(0.3f, 0.85f, 1.f);
	const FLinearColor StartSubtitleColor = ACodexTacticsHUD::GodotColor(0.7f, 0.75f, 0.82f);
	const FLinearColor StartButtonTextColor(0.05f, 0.05f, 0.05f);

	FText StartClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}
}

UTextBlock* UMainMenuWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
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

UButton* UMainMenuWidget::MakeButton(const FName& Name, const FName& TextName, const FText& Label, TObjectPtr<UTextBlock>& OutText, UVerticalBox* Column)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	OutText = MakeText(TextName, 14, StartButtonTextColor);
	OutText->SetText(Label);
	Button->AddChild(OutText);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetHeightOverride(42.f);
	Size->AddChild(Button);
	Column->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	return Button;
}

void UMainMenuWidget::BuildDefaultLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("StartRoot"));
	WidgetTree->RootWidget = Root;

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartDim"));
	Dim->SetBrushColor(StartDimColor);
	UOverlaySlot* DimSlot = Root->AddChildToOverlay(Dim);
	DimSlot->SetHorizontalAlignment(HAlign_Fill);
	DimSlot->SetVerticalAlignment(VAlign_Fill);

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartFrame"));
	Frame->SetBrushColor(StartFrameColor);
	Frame->SetPadding(FMargin(2.f));
	UOverlaySlot* FrameSlot = Root->AddChildToOverlay(Frame);
	FrameSlot->SetHorizontalAlignment(HAlign_Center);
	FrameSlot->SetVerticalAlignment(VAlign_Center);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartPanel"));
	Panel->SetBrushColor(StartPanelColor);
	Panel->SetPadding(FMargin(28.f, 22.f, 28.f, 8.f));
	Frame->SetContent(Panel);

	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("StartSize"));
	Size->SetWidthOverride(460.f);
	Size->SetMinDesiredHeight(320.f);
	Panel->SetContent(Size);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StartColumn"));
	Size->AddChild(Column);

	MenuTitleText = MakeText(TEXT("MenuTitleText"), 20, StartTitleColor);
	MenuTitleText->SetText(LOCTEXT("Title", "❄️ COLD GRAD: ТАКТИЧЕСКИЙ РЕЖИМ ❄️"));
	Column->AddChildToVerticalBox(MenuTitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	MenuSubtitleText = MakeText(TEXT("MenuSubtitleText"), 13, StartSubtitleColor);
	MenuSubtitleText->SetText(LOCTEXT("Subtitle", "Выберите режим для прохождения или тестирования:"));
	Column->AddChildToVerticalBox(MenuSubtitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StartSeparator"));
	Separator->SetBrushColor(StartFrameColor);
	Separator->SetPadding(FMargin(0.f, 1.f));
	Column->AddChildToVerticalBox(Separator)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	MenuGameButton = MakeButton(TEXT("MenuGameButton"), TEXT("MenuGameText"),
		LOCTEXT("Game", "🎮 1. Начать игру (Исследование ➔ Бой)"), MenuGameText, Column);
	MenuCombatButton = MakeButton(TEXT("MenuCombatButton"), TEXT("MenuCombatText"),
		LOCTEXT("Combat", "⚔️ 2. Начать бой (Тактическая подготовка)"), MenuCombatText, Column);
}

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (MenuGameButton)
	{
		MenuGameButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleGame);
	}
	if (MenuCombatButton)
	{
		MenuCombatButton->OnClicked.AddDynamic(this, &UMainMenuWidget::HandleCombat);
	}
	for (UTextBlock* Text : { MenuTitleText.Get(), MenuGameText.Get(), MenuCombatText.Get() })
	{
		if (Text)
		{
			Text->SetText(StartClean(Text->GetText()));
		}
	}
}

void UMainMenuWidget::HandleGame()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->StartMission(EMissionStartMode::Game);
	}
}

void UMainMenuWidget::HandleCombat()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->StartMission(EMissionStartMode::Combat);
	}
}


#undef LOCTEXT_NAMESPACE
