#include "UI/TurnBasedHudWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "UI/CodexButtonFocus.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "TurnBasedHudWidget"

namespace
{
	// Godot gorky17_combat_hud.gd colours.
	const FLinearColor TbPanelColor = ACodexTacticsHUD::GodotColor(0.08f, 0.11f, 0.16f, 0.92f);
	const FLinearColor TbFrameColor = ACodexTacticsHUD::GodotColor(0.2f, 0.85f, 1.f, 0.75f);
	const FLinearColor TbSquadPhaseColor = ACodexTacticsHUD::GodotColor(1.f, 0.85f, 0.2f);
	const FLinearColor TbEnemyPhaseColor = ACodexTacticsHUD::GodotColor(1.f, 0.35f, 0.35f);
	const FLinearColor TbNameColor = ACodexTacticsHUD::GodotColor(0.9f, 0.95f, 1.f);
	const FLinearColor TbApColor = ACodexTacticsHUD::GodotColor(0.2f, 1.f, 0.5f);
	const FLinearColor TbHpColor = ACodexTacticsHUD::GodotColor(1.f, 0.4f, 0.4f);
	const FLinearColor TbButtonText(0.05f, 0.05f, 0.05f);

	FText TbClean(const FText& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text.ToString()));
	}
}

UTextBlock* UTurnBasedHudWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

UButton* UTurnBasedHudWidget::MakeButton(const FName& Name, const FText& Label, float Height, UPanelWidget* Parent, UTextBlock** OutText)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	CodexButtonFocus::Disable(Button); // a focused HUD button would swallow the game keys
	UTextBlock* Text = MakeText(NAME_None, 11, TbButtonText);
	Text->SetText(TbClean(Label));
	Text->SetJustification(ETextJustify::Center);
	Button->AddChild(Text);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetHeightOverride(Height);
	Size->AddChild(Button);
	if (UVerticalBox* Column = Cast<UVerticalBox>(Parent))
	{
		Column->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
	}
	else if (UHorizontalBox* Row = Cast<UHorizontalBox>(Parent))
	{
		UHorizontalBoxSlot* RowSlot = Row->AddChildToHorizontalBox(Size);
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RowSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}
	if (OutText)
	{
		*OutText = Text;
	}
	return Button;
}

void UTurnBasedHudWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TbRoot"));
	WidgetTree->RootWidget = Root;

	// Godot action panel: bottom right, 250 x 235, 15 px from the edges, 2 px cyan frame.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TbFrame"));
	Frame->SetBrushColor(TbFrameColor);
	Frame->SetPadding(FMargin(2.f));
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(1.f, 1.f));
	FrameSlot->SetAlignment(FVector2D(1.f, 1.f));
	FrameSlot->SetPosition(FVector2D(-15.f, -105.f)); // above the bottom action bar
	FrameSlot->SetSize(FVector2D(290.f, 250.f));

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TbPanel"));
	Panel->SetBrushColor(TbPanelColor);
	Panel->SetPadding(FMargin(10.f, 8.f));
	Frame->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TbColumn"));
	Panel->SetContent(Column);

	TbPhaseText = MakeText(TEXT("TbPhaseText"), 14, TbSquadPhaseColor);
	TbPhaseText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(TbPhaseText)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
	TbUnitText = MakeText(TEXT("TbUnitText"), 12, TbNameColor);
	Column->AddChildToVerticalBox(TbUnitText)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));

	UHorizontalBox* Stats = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TbStats"));
	Column->AddChildToVerticalBox(Stats)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	TbApText = MakeText(TEXT("TbApText"), 12, TbApColor);
	Stats->AddChildToHorizontalBox(TbApText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TbHpText = MakeText(TEXT("TbHpText"), 12, TbHpColor);
	Stats->AddChildToHorizontalBox(TbHpText);

	TbPassButton = MakeButton(TEXT("TbPassButton"), LOCTEXT("Pass", "🛑 КОНЕЦ ХОДА ОТРЯДА"), 32.f, Column);
	TbPassButton->SetToolTipText(LOCTEXT("PassTip", "Завершить ход отряда и передать управление врагам [Enter]"));
	TbNextButton = MakeButton(TEXT("TbNextButton"), LOCTEXT("Next", "⏭️ СЛЕДУЮЩИЙ БОЕЦ [Tab]"), 26.f, Column);
	TbNextButton->SetToolTipText(LOCTEXT("NextTip", "Завершить ход текущего бойца и перейти к следующему [Tab]"));
	UTextBlock* StanceText = nullptr;
	TbStanceButton = MakeButton(TEXT("TbStanceButton"), LOCTEXT("Stance", "🛡️ СТОЙКА: СТОЯ [C]"), 26.f, Column, &StanceText);
	TbStanceText = StanceText;
	TbStanceButton->SetToolTipText(LOCTEXT("StanceTip", "Сменить стойку: Стоя / Присев / Лёжа [C] (1 AP)"));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TbRow"));
	Column->AddChildToVerticalBox(Row);
	TbTurnButton = MakeButton(TEXT("TbTurnButton"), LOCTEXT("Turn", "🔄 Поворот [R]"), 26.f, Row);
	TbTurnButton->SetToolTipText(LOCTEXT("TurnTip", "Повернуть бойца на 90 градусов (1 AP) [R]"));
	TbBarrelButton = MakeButton(TEXT("TbBarrelButton"), LOCTEXT("Barrel", "📦 Бочка [F]"), 26.f, Row);
	TbBarrelButton->SetToolTipText(LOCTEXT("BarrelTip", "Толкнуть соседнюю бочку (2 AP) [F]"));
}

void UTurnBasedHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (TbPassButton)
	{
		TbPassButton->OnClicked.AddDynamic(this, &UTurnBasedHudWidget::HandlePass);
	}
	if (TbNextButton)
	{
		TbNextButton->OnClicked.AddDynamic(this, &UTurnBasedHudWidget::HandleNext);
	}
	if (TbStanceButton)
	{
		TbStanceButton->OnClicked.AddDynamic(this, &UTurnBasedHudWidget::HandleStance);
	}
	if (TbTurnButton)
	{
		TbTurnButton->OnClicked.AddDynamic(this, &UTurnBasedHudWidget::HandleTurn);
	}
	if (TbBarrelButton)
	{
		TbBarrelButton->OnClicked.AddDynamic(this, &UTurnBasedHudWidget::HandleBarrel);
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr)
	{
		TurnBased->OnStateChanged.AddDynamic(this, &UTurnBasedHudWidget::HandleStateChanged);
	}
	Refresh();
}

void UTurnBasedHudWidget::HandleStateChanged()
{
	Refresh();
}

FText UTurnBasedHudWidget::GetPhaseText() const
{
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (!TurnBased || !TurnBased->IsActive())
	{
		return FText::GetEmpty();
	}
	return TurnBased->GetPhase() == ETurnPhase::Squad ? LOCTEXT("SquadPhase", "⚔️ ХОД ОТРЯДА") : LOCTEXT("EnemyPhase", "🐺 ХОД ПРОТИВНИКА...");
}

FText UTurnBasedHudWidget::GetApText() const
{
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const FTurnUnitState* State = TurnBased ? TurnBased->GetUnitState(TurnBased->GetActiveUnit()) : nullptr;
	return State ? FText::FromString(FString::Printf(TEXT("AP: %d/%d"), State->AP, State->MaxAP)) : FText::GetEmpty();
}

void UTurnBasedHudWidget::Refresh()
{
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (!TurnBased || !TurnBased->IsActive())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	const bool bSquadPhase = TurnBased->GetPhase() == ETurnPhase::Squad;
	const AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
	const FTurnUnitState* State = TurnBased->GetUnitState(Unit);
	if (TbPhaseText)
	{
		TbPhaseText->SetText(TbClean(GetPhaseText()));
		TbPhaseText->SetColorAndOpacity(FSlateColor(bSquadPhase ? TbSquadPhaseColor : TbEnemyPhaseColor));
	}
	if (TbUnitText)
	{
		TbUnitText->SetText(bSquadPhase && Unit ? FText::Format(LOCTEXT("Unit", "Боец: {0}"), Unit->DisplayName) : LOCTEXT("Enemies", "Враг: Противник"));
	}
	if (TbApText)
	{
		TbApText->SetText(bSquadPhase ? GetApText() : FText::GetEmpty());
	}
	if (TbHpText)
	{
		TbHpText->SetText(bSquadPhase && Unit && Unit->HealthComponent
			? FText::FromString(FString::Printf(TEXT("HP: %.0f"), Unit->HealthComponent->GetCurrentHealth())) : FText::GetEmpty());
	}
	if (TbStanceText && State)
	{
		const TCHAR* Stance = State->Stance == EOperativeStance::Prone ? TEXT("ЛЁЖА") : (State->Stance == EOperativeStance::Crouching ? TEXT("ПРИСЕВ") : TEXT("СТОЯ"));
		TbStanceText->SetText(FText::FromString(FString::Printf(TEXT("СТОЙКА: %s [C]"), Stance)));
	}
	for (UButton* Button : { TbPassButton.Get(), TbNextButton.Get(), TbStanceButton.Get(), TbTurnButton.Get() })
	{
		if (Button)
		{
			Button->SetIsEnabled(bSquadPhase && !TurnBased->IsUnitMoving());
		}
	}
}

void UTurnBasedHudWidget::HandlePass()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->PassSquadTurn();
	}
}

void UTurnBasedHudWidget::HandleNext()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->EndCurrentUnitTurn();
	}
}

void UTurnBasedHudWidget::HandleStance()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->CycleActiveUnitStance();
	}
}

void UTurnBasedHudWidget::HandleTurn()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->RotateActiveUnitClockwise();
	}
}

void UTurnBasedHudWidget::HandleBarrel()
{
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->TryPushAdjacentBarrel();
	}
}

#undef LOCTEXT_NAMESPACE
