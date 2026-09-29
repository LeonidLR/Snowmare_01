#include "UI/VictoryPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/WaveVictorySubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/ProfileDialogWidget.h"

#define LOCTEXT_NAMESPACE "VictoryPanelWidget"

namespace
{
	// Godot movements_demo.tscn VictoryPanel: DimOverlay, StyleBoxFlat_dialog, StyleBoxFlat_card, label colours.
	const FLinearColor VictoryDim = ACodexTacticsHUD::GodotColor(0.02f, 0.08f, 0.12f, 0.92f);
	const FLinearColor VictoryBack = ACodexTacticsHUD::GodotColor(0.08f, 0.09f, 0.12f, 0.9f);
	const FLinearColor VictoryFrame = ACodexTacticsHUD::GodotColor(0.3f, 0.4f, 0.55f);
	const FLinearColor VictoryCardBack = ACodexTacticsHUD::GodotColor(0.1f, 0.12f, 0.16f, 0.9f);
	const FLinearColor VictoryCardFrame = ACodexTacticsHUD::GodotColor(0.2f, 0.25f, 0.35f);
	const FLinearColor VictoryTitleColor = ACodexTacticsHUD::GodotColor(0.3f, 1.f, 0.6f);
	const FLinearColor VictorySubtitleColor = ACodexTacticsHUD::GodotColor(0.9f, 0.95f, 1.f);
	const FLinearColor VictoryButtonText(0.05f, 0.05f, 0.05f);

	FText VictoryClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}

	/** The HUD font has no emoji: the enemy-type icons of the statistics become words. */
	FString StatsForFont(FString Text)
	{
		Text.ReplaceInline(TEXT("🐺 "), TEXT("гончие "));
		Text.ReplaceInline(TEXT("🏹 "), TEXT("плевуны "));
		Text.ReplaceInline(TEXT("❄️ "), TEXT("громилы "));
		TArray<FString> Lines;
		Text.Replace(TEXT("➔"), TEXT("->")).ParseIntoArrayLines(Lines, false);
		for (FString& Line : Lines)
		{
			Line = ACodexTacticsHUD::StripUnsupportedGlyphs(Line).Replace(TEXT("  "), TEXT(" "));
		}
		return FString::Join(Lines, TEXT("\n"));
	}
}

UTextBlock* UVictoryPanelWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

void UVictoryPanelWidget::BuildDefaultLayout()
{
	// The tree root stays visible (a collapsed root stops NativeTick); the inner layer is what shows / hides.
	UCanvasPanel* TreeRoot = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("VictoryRoot"));
	TreeRoot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = TreeRoot;
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("VictoryPanel"));
	UCanvasPanelSlot* LayerSlot = TreeRoot->AddChildToCanvas(Canvas);
	LayerSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	LayerSlot->SetOffsets(FMargin(0.f));
	Root = Canvas;

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DimOverlay"));
	Dim->SetBrushColor(VictoryDim);
	UCanvasPanelSlot* DimSlot = Canvas->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));

	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VictoryFrame"));
	Frame->SetBrushColor(VictoryFrame);
	Frame->SetPadding(FMargin(2.f));
	UCanvasPanelSlot* FrameSlot = Canvas->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetAutoSize(true);

	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VictoryBody"));
	Body->SetBrushColor(VictoryBack);
	Body->SetPadding(FMargin(24.f, 18.f));
	Frame->SetContent(Body);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("VictorySize"));
	Size->SetWidthOverride(560.f - 48.f);
	Size->SetMinDesiredHeight(380.f - 36.f);
	Body->SetContent(Size);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VictoryColumn"));
	Size->AddChild(Column);

	TitleText = MakeText(TEXT("VictoryTitle"), 20, VictoryTitleColor);
	TitleText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	SubtitleText = MakeText(TEXT("VictorySubtitle"), 13, VictorySubtitleColor);
	SubtitleText->SetJustification(ETextJustify::Center);
	SubtitleText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(SubtitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UBorder* StatsCardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatsCardFrame"));
	StatsCardBorder->SetBrushColor(VictoryCardFrame);
	StatsCardBorder->SetPadding(FMargin(1.f));
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatsCard"));
	Card->SetBrushColor(VictoryCardBack);
	Card->SetPadding(FMargin(12.f, 8.f));
	StatsCardBorder->SetContent(Card);
	StatsText = MakeText(TEXT("StatsLabel"), 12, VictorySubtitleColor);
	StatsText->SetAutoWrapText(true);
	Card->SetContent(StatsText);
	Column->AddChildToVerticalBox(StatsCardBorder)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	auto MakeButton = [this, Column](const FName& Name, float Height, UTextBlock*& OutText)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		OutText = MakeText(NAME_None, 13, VictoryButtonText);
		OutText->SetJustification(ETextJustify::Center);
		Button->AddChild(OutText);
		USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ButtonSize->SetHeightOverride(Height);
		ButtonSize->AddChild(Button);
		Column->AddChildToVerticalBox(ButtonSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		return Button;
	};
	UTextBlock* Label = nullptr;
	UButton* Next = MakeButton(TEXT("BtnNextWave"), 38.f, Label);
	NextText = Label;
	Next->OnClicked.AddDynamic(this, &UVictoryPanelWidget::HandleNext);
	UButton* Restart = MakeButton(TEXT("BtnRestart"), 34.f, Label);
	Label->SetText(VictoryClean(TEXT("🔄 Перезапустить уровень (X)")));
	Restart->OnClicked.AddDynamic(this, &UVictoryPanelWidget::HandleRestart);
}

void UVictoryPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (Root)
	{
		Root->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr)
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UVictoryPanelWidget::HandleGameFlowChanged);
	}
}

void UVictoryPanelWidget::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	Refresh();
}

void UVictoryPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

bool UVictoryPanelWidget::IsShown() const
{
	return Root && Root->GetVisibility() != ESlateVisibility::Collapsed;
}

FString UVictoryPanelWidget::GetShownText() const
{
	const UWaveVictorySubsystem* Victory = GetWorld() ? GetWorld()->GetSubsystem<UWaveVictorySubsystem>() : nullptr;
	if (!Victory)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s\n%s\n%s\n%s"), *Victory->GetVictoryTitle(), *Victory->GetVictorySubtitle(),
		*Victory->GetKillStatsText(), *Victory->GetNextButtonText());
}

void UVictoryPanelWidget::Refresh()
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const UWaveVictorySubsystem* Victory = GetWorld() ? GetWorld()->GetSubsystem<UWaveVictorySubsystem>() : nullptr;
	const bool bShow = Flow && Victory && Flow->GetPhase() == ECodexGamePhase::WaveCleared;
	if (!Root)
	{
		return;
	}
	if (!bShow)
	{
		if (IsShown())
		{
			Root->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}
	if (TitleText)
	{
		TitleText->SetText(VictoryClean(Victory->GetVictoryTitle()));
	}
	if (SubtitleText)
	{
		SubtitleText->SetText(VictoryClean(Victory->GetVictorySubtitle()));
	}
	if (StatsText)
	{
		StatsText->SetText(FText::FromString(StatsForFont(Victory->GetKillStatsText())));
	}
	if (NextText)
	{
		NextText->SetText(VictoryClean(Victory->GetNextButtonText()));
	}
	if (!IsShown())
	{
		Root->SetVisibility(ESlateVisibility::Visible);
	}
}

void UVictoryPanelWidget::PressNext()
{
	// Godot _on_next_wave_pressed: the profile closes first.
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ACodexTacticsHUD* Hud = PC->GetHUD<ACodexTacticsHUD>())
		{
			if (UProfileDialogWidget* Profile = Hud->GetProfileDialog())
			{
				Profile->Close();
			}
		}
	}
	if (UWaveVictorySubsystem* Victory = GetWorld()->GetSubsystem<UWaveVictorySubsystem>())
	{
		Victory->ContinueAfterWave();
	}
	Refresh();
}

void UVictoryPanelWidget::HandleNext()
{
	PressNext();
}

void UVictoryPanelWidget::HandleRestart()
{
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->RestartMission(/*bQuick*/ true);
	}
}

#undef LOCTEXT_NAMESPACE
