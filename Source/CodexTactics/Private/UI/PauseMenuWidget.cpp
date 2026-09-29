#include "UI/PauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MissionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/SaveLoadDialogWidget.h"

#define LOCTEXT_NAMESPACE "PauseMenuWidget"

namespace
{
	// Godot pause_menu_dialog.gd: bg (0.08, 0.10, 0.14, 0.96), border (0.2, 0.7, 0.9, 0.9), title (0.3, 0.9, 1.0),
	// subtitle (0.65, 0.72, 0.8), status (0.5, 0.8, 0.6) / none (0.7, 0.7, 0.7).
	const FLinearColor PauseBack = ACodexTacticsHUD::GodotColor(0.08f, 0.1f, 0.14f, 0.96f);
	const FLinearColor PauseBorder = ACodexTacticsHUD::GodotColor(0.2f, 0.7f, 0.9f, 0.9f);
	const FLinearColor PauseTitle = ACodexTacticsHUD::GodotColor(0.3f, 0.9f, 1.f);
	const FLinearColor PauseSubtitle = ACodexTacticsHUD::GodotColor(0.65f, 0.72f, 0.8f);
	const FLinearColor PauseStatus = ACodexTacticsHUD::GodotColor(0.5f, 0.8f, 0.6f);
	const FLinearColor PauseStatusNone = ACodexTacticsHUD::GodotColor(0.7f, 0.7f, 0.7f);
	const FLinearColor PauseButton = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);

	FText PauseClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}
}

void UPauseMenuWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PauseRoot"));
	WidgetTree->RootWidget = Root;
	// Godot: centred, 400 x 420.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseMenu"));
	Frame->SetBrushColor(PauseBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetSize(FVector2D(400.f, 420.f));
	Panel = Frame;

	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseBody"));
	Body->SetBrushColor(PauseBack);
	Body->SetPadding(FMargin(24.f, 18.f));
	Frame->SetContent(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Body->SetContent(Column);

	auto MakeText = [this](int32 Size, const FLinearColor& Color, const FString& Text)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(ETextJustify::Center);
		Block->SetAutoWrapText(true);
		Block->SetText(PauseClean(Text));
		return Block;
	};
	Column->AddChildToVerticalBox(MakeText(20, PauseTitle, TEXT("⏸️ МЕНЮ ПАУЗЫ")))->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	Column->AddChildToVerticalBox(MakeText(12, PauseSubtitle, TEXT("Управление сессией и прогрессом отряда")))->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

	auto MakeButton = [this, Column, &MakeText](const FString& Label, const FName& Name)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->SetBackgroundColor(PauseButton);
		UTextBlock* LabelText = MakeText(14, FLinearColor(0.95f, 0.95f, 0.95f), Label);
		LabelText->SetAutoWrapText(false);
		Button->AddChild(LabelText);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetHeightOverride(40.f);
		Size->AddChild(Button);
		Column->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		return Button;
	};
	MakeButton(TEXT("▶️ Продолжить игру"), TEXT("BtnResume"))->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleResume);
	MakeButton(TEXT("💾 Сохранить игру"), TEXT("BtnSave"))->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleSave);
	LoadButton = MakeButton(TEXT("📂 Загрузить игру"), TEXT("BtnLoad"));
	LoadButton->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleLoad);
	MakeButton(TEXT("🏠 В главное меню"), TEXT("BtnMainMenu"))->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleMainMenu);
	MakeButton(TEXT("❌ Выход из игры"), TEXT("BtnQuit"))->OnClicked.AddDynamic(this, &UPauseMenuWidget::HandleQuit);
	StatusText = MakeText(11, PauseStatus, TEXT("Готово"));
	Column->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
}

void UPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

bool UPauseMenuWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

FText UPauseMenuWidget::GetStatusText() const
{
	const USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	const TArray<FSaveSlotInfo> All = Saves ? Saves->GetAllSaves() : TArray<FSaveSlotInfo>();
	if (All.IsEmpty())
	{
		return LOCTEXT("NoSaves", "Нет сохраненных данных");
	}
	return FText::FromString(FString::Printf(TEXT("Слот: %s (%s, %s)"), *All[0].SlotName, *All[0].StageName,
		All[0].DateTime.IsEmpty() ? TEXT("—") : *All[0].DateTime));
}

bool UPauseMenuWidget::IsLoadEnabled() const
{
	const USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	return Saves && !Saves->GetAllSaves().IsEmpty();
}

void UPauseMenuWidget::Open()
{
	if (!Panel)
	{
		return;
	}
	Panel->SetVisibility(ESlateVisibility::Visible);
	UGameplayStatics::SetGamePaused(GetWorld(), true); // Godot Engine.time_scale = 0
	const bool bHasSaves = IsLoadEnabled();
	LoadButton->SetIsEnabled(bHasSaves);
	StatusText->SetText(GetStatusText());
	StatusText->SetColorAndOpacity(FSlateColor(bHasSaves ? PauseStatus : PauseStatusNone));
}

void UPauseMenuWidget::Close(bool bResume)
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bResume)
	{
		UGameplayStatics::SetGamePaused(GetWorld(), false);
	}
}

void UPauseMenuWidget::Resume()
{
	Close(true);
}

void UPauseMenuWidget::OpenSave()
{
	const APlayerController* PC = GetOwningPlayer();
	if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
	{
		Hud->OpenSaveLoadDialog(ESaveDialogMode::Save);
	}
}

void UPauseMenuWidget::OpenLoad()
{
	const APlayerController* PC = GetOwningPlayer();
	if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
	{
		Hud->OpenSaveLoadDialog(ESaveDialogMode::Load);
	}
}

void UPauseMenuWidget::ToMainMenu()
{
	Close(true);
	// Godot shows the start menu again; here the level restarts with the start menu (a fresh mission).
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->RestartMission(false);
	}
}

void UPauseMenuWidget::HandleResume() { Resume(); }
void UPauseMenuWidget::HandleSave() { OpenSave(); }
void UPauseMenuWidget::HandleLoad() { OpenLoad(); }
void UPauseMenuWidget::HandleMainMenu() { ToMainMenu(); }

void UPauseMenuWidget::HandleQuit()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, false);
}

#undef LOCTEXT_NAMESPACE
