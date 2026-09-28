#include "UI/PhaseBannersWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/WaveSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "PhaseBannersWidget"

namespace
{
	// Godot StyleBoxFlat_obj (pause banner), StyleBoxFlat_card (combat banner), cutscene colours.
	const FLinearColor BannerPanelColor = ACodexTacticsHUD::GodotColor(0.05f, 0.06f, 0.08f, 0.8f);
	const FLinearColor BannerFrameColor = ACodexTacticsHUD::GodotColor(0.8f, 0.65f, 0.2f);
	const FLinearColor BannerPauseText = ACodexTacticsHUD::GodotColor(1.f, 0.9f, 0.4f);
	const FLinearColor BannerCombatText = ACodexTacticsHUD::GodotColor(1.f, 0.85f, 0.3f);
	const FLinearColor BannerCutsceneTitle = ACodexTacticsHUD::GodotColor(1.f, 0.8f, 0.2f);
	const FLinearColor BannerCutsceneBody = ACodexTacticsHUD::GodotColor(0.9f, 0.92f, 0.96f);
	const FLinearColor BannerCutsceneSkip = ACodexTacticsHUD::GodotColor(0.5f, 0.6f, 0.7f);
	const FLinearColor BannerButtonText(0.05f, 0.05f, 0.05f);

	FText BannerClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}

	UBorder* BannerFramed(UWidgetTree* Tree, const FName& Name, UWidget* Content, const FMargin& Padding)
	{
		UBorder* Frame = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Frame->SetBrushColor(BannerFrameColor);
		Frame->SetPadding(FMargin(2.f));
		UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrushColor(BannerPanelColor);
		Panel->SetPadding(Padding);
		Panel->SetContent(Content);
		Frame->SetContent(Panel);
		return Frame;
	}
}

UTextBlock* UPhaseBannersWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(ETextJustify::Center);
	return Text;
}

void UPhaseBannersWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BannerRoot"));
	WidgetTree->RootWidget = Root;

	// Pre-combat cutscene (below the banners in z order, covers the screen).
	UOverlay* Cutscene = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CutscenePanel"));
	CutscenePanel = Cutscene;
	UCanvasPanelSlot* CutsceneSlot = Root->AddChildToCanvas(Cutscene);
	CutsceneSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	CutsceneSlot->SetOffsets(FMargin(0.f));
	UBorder* Black = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CutsceneBackground"));
	Black->SetBrushColor(FLinearColor::Black);
	UOverlaySlot* BlackSlot = Cutscene->AddChildToOverlay(Black);
	BlackSlot->SetHorizontalAlignment(HAlign_Fill);
	BlackSlot->SetVerticalAlignment(VAlign_Fill);
	UVerticalBox* Card = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CutsceneCard"));
	UOverlaySlot* CardSlot = Cutscene->AddChildToOverlay(Card);
	CardSlot->SetHorizontalAlignment(HAlign_Center);
	CardSlot->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Title = MakeText(TEXT("CutsceneTitle"), 18, BannerCutsceneTitle);
	Title->SetText(BannerClean(LOCTEXT("CutsceneTitle", "🎬 [КАТ-СЦЕНА: ПРОРЫВ В КАРАНТИННЫЙ ДВОР]")));
	Card->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	UTextBlock* Body = MakeText(TEXT("CutsceneBody"), 14, BannerCutsceneBody);
	Body->SetText(LOCTEXT("CutsceneBody", "Отряд осторожно пересекает линию гермоворот...\nСзади с грохотом блокируются пневмозамки.\nИз ледяного тумана двора доносятся глухие шорохи."));
	Card->AddChildToVerticalBox(Body)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
	CutsceneSkipText = MakeText(TEXT("CutsceneSkipText"), 12, BannerCutsceneSkip);
	Card->AddChildToVerticalBox(CutsceneSkipText);

	// Pause banner: top centre, 440 wide, 20 px from the top.
	PauseText = MakeText(TEXT("PauseText"), 15, BannerPauseText);
	PauseBanner = BannerFramed(WidgetTree, TEXT("PauseBanner"), PauseText, FMargin(16.f, 8.f));
	UCanvasPanelSlot* PauseSlot = Root->AddChildToCanvas(PauseBanner);
	PauseSlot->SetAnchors(FAnchors(0.5f, 0.f));
	PauseSlot->SetAlignment(FVector2D(0.5f, 0.f));
	PauseSlot->SetPosition(FVector2D(0.f, 20.f));
	PauseSlot->SetAutoSize(true);

	// Combat banner: 600 wide, 75 px from the top.
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CombatRow"));
	CombatText = MakeText(TEXT("CombatText"), 14, BannerCombatText);
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(CombatText);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	FinishPrepButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("FinishPrepButton"));
	UTextBlock* ButtonText = MakeText(TEXT("FinishPrepText"), 12, BannerButtonText);
	ButtonText->SetText(BannerClean(LOCTEXT("FinishPrep", "⚔️ Начать бой")));
	FinishPrepButton->AddChild(ButtonText);
	USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ButtonSize->SetWidthOverride(160.f);
	ButtonSize->SetHeightOverride(32.f);
	ButtonSize->AddChild(FinishPrepButton);
	Row->AddChildToHorizontalBox(ButtonSize)->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
	USizeBox* RowSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	RowSize->SetWidthOverride(760.f); // Godot 600 px at its font size; UE fonts render larger
	RowSize->AddChild(Row);
	CombatBanner = BannerFramed(WidgetTree, TEXT("CombatBanner"), RowSize, FMargin(14.f, 6.f));
	UCanvasPanelSlot* CombatSlot = Root->AddChildToCanvas(CombatBanner);
	CombatSlot->SetAnchors(FAnchors(0.5f, 0.f));
	CombatSlot->SetAlignment(FVector2D(0.5f, 0.f));
	CombatSlot->SetPosition(FVector2D(0.f, 75.f));
	CombatSlot->SetAutoSize(true);
}

void UPhaseBannersWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (FinishPrepButton)
	{
		FinishPrepButton->OnClicked.AddDynamic(this, &UPhaseBannersWidget::HandleFinishPreparation);
	}
	Refresh();
}

void UPhaseBannersWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

FText UPhaseBannersWidget::GetPauseText() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Flow || Flow->GetCombatMode() != ECodexCombatMode::TacticalPause)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("⏱️ РЕЖИМ ПРИКАЗОВ | Зарядов в волне: %d/%d | Время планирования: %04.1fс [ПРОБЕЛ — исполнить]"),
		Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges, FMath::Max(0.f, Flow->GetPauseTimeRemaining())));
}

FText UPhaseBannersWidget::GetCombatText() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Flow)
	{
		return FText::GetEmpty();
	}
	if (Flow->GetPhase() == ECodexGamePhase::Preparation)
	{
		return FText::FromString(FString::Printf(TEXT("⏱ ПОДГОТОВКА К БОЮ: %02d сек | [ПРОБЕЛ] — НИЖНЕЕ МЕНЮ"),
			FMath::CeilToInt(Flow->GetPreparationTimeRemaining())));
	}
	if (Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() != ECodexCombatMode::TurnBased)
	{
		const UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
		return FText::FromString(FString::Printf(TEXT("⚔️ ВОЛНА %d | ВРАГОВ ОСТАЛОСЬ: %d"), Flow->GetWaveIndex(),
			Waves ? Waves->GetAliveEnemyCount() : 0));
	}
	return FText::GetEmpty();
}

bool UPhaseBannersWidget::IsCutsceneShown() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && Flow->GetPhase() == ECodexGamePhase::Cutscene;
}

void UPhaseBannersWidget::Refresh()
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const FText Pause = GetPauseText();
	const FText Combat = GetCombatText();
	if (PauseBanner)
	{
		PauseBanner->SetVisibility(Pause.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (PauseText && !Pause.IsEmpty())
	{
		PauseText->SetText(BannerClean(Pause));
	}
	if (CombatBanner)
	{
		CombatBanner->SetVisibility(Combat.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (CombatText && !Combat.IsEmpty())
	{
		CombatText->SetText(BannerClean(Combat));
	}
	if (FinishPrepButton)
	{
		FinishPrepButton->SetVisibility(Flow && Flow->GetPhase() == ECodexGamePhase::Preparation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	const bool bCutscene = IsCutsceneShown();
	if (CutscenePanel)
	{
		CutscenePanel->SetVisibility(bCutscene ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (CutsceneSkipText && bCutscene)
	{
		CutsceneSkipText->SetText(FText::FromString(FString::Printf(TEXT("Продолжение через: %d сек... (или клик)"),
			FMath::CeilToInt(Flow->GetCutsceneTimeRemaining()))));
	}
}

FReply UPhaseBannersWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Godot: any click skips the cutscene.
	if (IsCutsceneShown())
	{
		if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->FinishCutscene();
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPhaseBannersWidget::HandleFinishPreparation()
{
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->FinishPreparation();
	}
}

#undef LOCTEXT_NAMESPACE
