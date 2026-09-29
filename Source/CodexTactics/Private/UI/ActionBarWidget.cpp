#include "UI/ActionBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "Interactables/RelocationSubsystem.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Combat/GrenadeSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "ActionBarWidget"

namespace
{
	// Godot movements_demo.tscn StyleBoxFlat_tactical_bar / slot_* / *_bar_* colours.
	const FLinearColor BarPanelColor = ACodexTacticsHUD::GodotColor(0.08f, 0.1f, 0.13f, 0.96f);
	const FLinearColor BarFrameColor = ACodexTacticsHUD::GodotColor(0.35f, 0.38f, 0.45f);
	const FLinearColor BarPurple = ACodexTacticsHUD::GodotColor(0.55f, 0.12f, 0.68f);
	const FLinearColor BarGreen = ACodexTacticsHUD::GodotColor(0.12f, 0.65f, 0.28f);
	const FLinearColor BarBlue = ACodexTacticsHUD::GodotColor(0.02f, 0.6f, 0.92f);
	const FLinearColor BarRed = ACodexTacticsHUD::GodotColor(0.88f, 0.2f, 0.2f);
	const FLinearColor BarWhite = ACodexTacticsHUD::GodotColor(0.96f, 0.96f, 0.98f);
	const FLinearColor BarOrange = ACodexTacticsHUD::GodotColor(0.85f, 0.32f, 0.06f, 0.88f);
	const FLinearColor BarLeaderOrange = ACodexTacticsHUD::GodotColor(1.f, 0.45f, 0.12f);
	const FLinearColor BarLeaderBorder = ACodexTacticsHUD::GodotColor(0.2f, 0.95f, 1.f);
	const FLinearColor BarSlotBorder = ACodexTacticsHUD::GodotColor(0.35f, 0.12f, 0.02f, 0.8f);
	const FLinearColor BarHealthFill = ACodexTacticsHUD::GodotColor(0.15f, 0.85f, 0.35f);
	const FLinearColor BarHealthBack = ACodexTacticsHUD::GodotColor(0.18f, 0.05f, 0.05f, 0.9f);
	const FLinearColor BarColdFill = ACodexTacticsHUD::GodotColor(0.18f, 0.72f, 1.f);
	const FLinearColor BarColdBack = ACodexTacticsHUD::GodotColor(0.04f, 0.08f, 0.15f, 0.9f);
	const FLinearColor BarTextColor(0.95f, 0.95f, 0.95f);
	const FLinearColor BarDarkText = ACodexTacticsHUD::GodotColor(0.1f, 0.1f, 0.1f);
	// Godot WeaponSelectorPanel: bg (0.08, 0.10, 0.14, 0.96), border (0.2, 0.75, 0.95), title (0.4, 0.9, 1.0).
	const FLinearColor SelectorBack = ACodexTacticsHUD::GodotColor(0.08f, 0.1f, 0.14f, 0.96f);
	const FLinearColor SelectorBorder = ACodexTacticsHUD::GodotColor(0.2f, 0.75f, 0.95f);
	const FLinearColor SelectorTitle = ACodexTacticsHUD::GodotColor(0.4f, 0.9f, 1.f);
	const FLinearColor SelectorButtonColor = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);
	const TCHAR* const SelectorIds[] = { TEXT("m16"), TEXT("pistol"), TEXT("grenade"), TEXT("knife") };

	/** Godot role tags (second line of «КУБ\nКОМ» etc.; the shape names were placeholder art). */
	const TCHAR* const BarRoleTags[] = { TEXT("КОМ"), TEXT("ИНЖ"), TEXT("МЕД"), TEXT("РЕЗ") };

	UProgressBar* BarMakeProgress(UWidgetTree* Tree, const FLinearColor& Fill, const FLinearColor& Back)
	{
		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Bar->SetFillColorAndOpacity(Fill);
		FProgressBarStyle Style = Bar->GetWidgetStyle();
		Style.BackgroundImage.TintColor = FSlateColor(Back);
		Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
		Bar->SetWidgetStyle(Style);
		return Bar;
	}
}

UTextBlock* UActionBarWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetJustification(ETextJustify::Center);
	return Text;
}

UButton* UActionBarWidget::MakeSlotButton(const FName& Name, const FLinearColor& Color, float Width, float Height, UTextBlock* Label, UHorizontalBox* Row)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Button->SetBackgroundColor(Color);
	Button->AddChild(Label);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(Width);
	Size->SetHeightOverride(Height);
	Size->AddChild(Button);
	UHorizontalBoxSlot* RowSlot = Row->AddChildToHorizontalBox(Size);
	RowSlot->SetPadding(FMargin(3.f, 0.f));
	RowSlot->SetVerticalAlignment(VAlign_Center);
	return Button;
}

void UActionBarWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BarRoot"));
	WidgetTree->RootWidget = Root;

	// Godot TacticalBar: bottom centre, 740 x 78, 10 px above the edge, 3 px frame.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BarFrame"));
	Frame->SetBrushColor(BarFrameColor);
	Frame->SetPadding(FMargin(3.f));
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 1.f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 1.f));
	FrameSlot->SetPosition(FVector2D(0.f, -10.f));
	FrameSlot->SetAutoSize(true);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BarPanel"));
	Panel->SetBrushColor(BarPanelColor);
	Panel->SetPadding(FMargin(8.f, 6.f));
	Frame->SetContent(Panel);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BarRow"));
	Panel->SetContent(Row);

	auto Disabled = [this, Row](const TCHAR* Name, const FLinearColor& Color, const FText& Label, const FText& Tooltip)
	{
		UTextBlock* Text = MakeText(NAME_None, 10, BarTextColor);
		Text->SetText(Label);
		UButton* Button = MakeSlotButton(Name, Color, 54.f, 56.f, Text, Row);
		Button->SetIsEnabled(false);
		Button->SetToolTipText(Tooltip);
	};
	Disabled(TEXT("BarTransferButton"), BarPurple, LOCTEXT("Transfer", "ПЕРЕД"), LOCTEXT("TransferTip", "Передача предметов (ещё не перенесено)"));
	Disabled(TEXT("BarInventoryButton"), BarGreen, LOCTEXT("Inventory", "ИНВ"), LOCTEXT("InventoryTip", "Личный инвентарь (ещё не перенесено)"));

	BarWeaponText = MakeText(TEXT("BarWeaponText"), 11, BarTextColor);
	UButton* WeaponButton = MakeSlotButton(TEXT("BarWeaponButton"), BarBlue, 160.f, 56.f, BarWeaponText, Row);
	WeaponButton->SetToolTipText(LOCTEXT("WeaponTip", "Выбор оружия (Клик — меню арсенала / режим стрельбы, [F] — огонь, [G] — граната)"));
	WeaponButton->OnClicked.AddDynamic(this, &UActionBarWidget::HandleWeaponSlot);

	BarRelocateText = MakeText(TEXT("BarRelocateText"), 11, BarTextColor);
	RelocateButton = MakeSlotButton(TEXT("BarRelocateButton"), BarRed, 54.f, 56.f, BarRelocateText, Row);
	RelocateButton->OnClicked.AddDynamic(this, &UActionBarWidget::HandleRelocate);

	BarStanceText = MakeText(TEXT("BarStanceText"), 22, BarDarkText);
	UButton* StanceButton = MakeSlotButton(TEXT("BarStanceButton"), BarWhite, 54.f, 56.f, BarStanceText, Row);
	StanceButton->OnClicked.AddDynamic(this, &UActionBarWidget::HandleStance);

	BarGuardText = MakeText(TEXT("BarGuardText"), 10, BarTextColor);
	BarGuardText->SetText(LOCTEXT("Guard", "ОБОР"));
	GuardButton = MakeSlotButton(TEXT("BarGuardButton"), BarWhite * 0.6f, 54.f, 56.f, BarGuardText, Row);
	GuardButton->SetToolTipText(LOCTEXT("GuardTip", "Зафиксировать позицию (Охрана фланга/тыла) [T]"));
	GuardButton->OnClicked.AddDynamic(this, &UActionBarWidget::HandleGuard);

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FActionBarSquadSlot& SquadSlot = Slots.AddDefaulted_GetRef();
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Column);
		ColumnSlot->SetPadding(FMargin(3.f, 0.f));
		ColumnSlot->SetVerticalAlignment(VAlign_Center);

		SquadSlot.Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		SquadSlot.Frame->SetPadding(FMargin(1.f));
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(74.f);
		Size->SetHeightOverride(40.f);
		Size->AddChild(SquadSlot.Frame);
		Column->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));

		SquadSlot.Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		SquadSlot.Label = MakeText(NAME_None, 11, BarTextColor);
		SquadSlot.Button->AddChild(SquadSlot.Label);
		SquadSlot.Frame->SetContent(SquadSlot.Button);

		SquadSlot.HealthBar = BarMakeProgress(WidgetTree, BarHealthFill, BarHealthBack);
		SquadSlot.ColdBar = BarMakeProgress(WidgetTree, BarColdFill, BarColdBack);
		for (UProgressBar* Bar : { SquadSlot.HealthBar.Get(), SquadSlot.ColdBar.Get() })
		{
			USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BarSize->SetWidthOverride(74.f);
			BarSize->SetHeightOverride(5.f);
			BarSize->AddChild(Bar);
			Column->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}
	}
	// Godot WeaponSelectorPanel: centre bottom, 320 wide, 94 px above the edge, hidden until the weapon slot is clicked.
	UBorder* SelectorFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WeaponSelectorPanel"));
	SelectorFrame->SetBrushColor(SelectorBorder);
	SelectorFrame->SetPadding(FMargin(2.f));
	SelectorFrame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* SelectorSlot = Root->AddChildToCanvas(SelectorFrame);
	SelectorSlot->SetAnchors(FAnchors(0.5f, 1.f));
	SelectorSlot->SetAlignment(FVector2D(0.5f, 1.f));
	SelectorSlot->SetPosition(FVector2D(0.f, -94.f));
	SelectorSlot->SetSize(FVector2D(320.f, 186.f));
	SelectorPanel = SelectorFrame;
	UBorder* SelectorBody = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("WeaponSelectorBody"));
	SelectorBody->SetBrushColor(SelectorBack);
	SelectorBody->SetPadding(FMargin(12.f, 10.f));
	SelectorFrame->SetContent(SelectorBody);
	UVerticalBox* SelectorColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WeaponSelectorColumn"));
	SelectorBody->SetContent(SelectorColumn);
	UTextBlock* SelectorTitleText = MakeText(TEXT("WeaponSelectorTitle"), 13, SelectorTitle);
	SelectorTitleText->SetText(FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(TEXT("⚔️ ВЫБОР ВООРУЖЕНИЯ"))));
	SelectorColumn->AddChildToVerticalBox(SelectorTitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	for (const TCHAR* Id : SelectorIds)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(FString::Printf(TEXT("BtnWep_%s"), Id)));
		Button->SetBackgroundColor(SelectorButtonColor); // Godot default dark theme buttons
		UTextBlock* Label = MakeText(NAME_None, 11, BarTextColor);
		Label->SetJustification(ETextJustify::Left);
		Button->AddChild(Label);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetHeightOverride(32.f);
		Size->AddChild(Button);
		SelectorColumn->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		SelectorButtons.Add(Button);
		SelectorTexts.Add(Label);
	}
	SelectorButtons[0]->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSelectM16);
	SelectorButtons[1]->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSelectPistol);
	SelectorButtons[2]->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSelectGrenade);
	SelectorButtons[3]->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSelectKnife);

	Slots[0].Button->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSlot0);
	Slots[1].Button->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSlot1);
	Slots[2].Button->OnClicked.AddDynamic(this, &UActionBarWidget::HandleSlot2);
}

void UActionBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UActionBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UActionBarWidget::Refresh()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return;
	}
	if (BarWeaponText)
	{
		BarWeaponText->SetText(FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(GetWeaponText().ToString())));
	}
	if (BarGuardText && GuardButton)
	{
		// Godot: «ЗАФИК» (green) while the leader guards its spot.
		BarGuardText->SetText(Leader->bGuarding ? LOCTEXT("GuardOn", "ЗАФИК") : LOCTEXT("Guard", "ОБОР"));
		GuardButton->SetBackgroundColor(Leader->bGuarding ? BarGreen : BarWhite * 0.6f);
		GuardButton->SetToolTipText(Leader->bGuarding ? LOCTEXT("GuardOnTip", "Боец на точке обороны! Нажмите [T] для возврата в строй")
			: LOCTEXT("GuardTip", "Зафиксировать позицию (Охрана фланга/тыла) [T]"));
	}
	if (IsWeaponSelectorOpen())
	{
		for (int32 Index = 0; Index < SelectorTexts.Num(); ++Index)
		{
			SelectorTexts[Index]->SetText(GetSelectorText(Index));
		}
		const USquadSubsystem* BarSquad = GetWorld()->GetSubsystem<USquadSubsystem>();
		const AOperativeCharacter* BarLeader = BarSquad ? BarSquad->GetLeader() : nullptr;
		if (SelectorButtons.IsValidIndex(2) && BarLeader)
		{
			SelectorButtons[2]->SetIsEnabled(BarLeader->GrenadesCount > 0); // Godot b_grenade.disabled = count <= 0
		}
	}
	if (BarRelocateText)
	{
		const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
		const ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer());
		const bool bActive = (Relocation && Relocation->IsPlacing()) || (PC && PC->IsRelocateSelectMode());
		BarRelocateText->SetText(bActive ? LOCTEXT("RelocateActive", "АКТИВ") : LOCTEXT("Relocate", "ПЕР"));
	}
	if (BarStanceText)
	{
		const EOperativeStance Stance = Leader->GetStance();
		BarStanceText->SetText(Stance == EOperativeStance::Prone ? LOCTEXT("P", "Л") : (Stance == EOperativeStance::Crouching ? LOCTEXT("C", "П") : LOCTEXT("S", "С")));
	}

	TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		FActionBarSquadSlot& SquadSlot = Slots[Index];
		const AOperativeCharacter* Member = Members.IsValidIndex(Index) ? Members[Index] : nullptr;
		SquadSlot.Label->SetText(GetSlotText(Index));
		SquadSlot.Button->SetIsEnabled(Member != nullptr);
		SquadSlot.HealthBar->SetVisibility(Member ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		SquadSlot.ColdBar->SetVisibility(Member ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		if (!Member)
		{
			SquadSlot.Frame->SetBrushColor(BarSlotBorder);
			continue;
		}
		const bool bLeader = Member == Leader;
		SquadSlot.Button->SetBackgroundColor(bLeader ? BarLeaderOrange : BarOrange);
		SquadSlot.Frame->SetBrushColor(bLeader ? BarLeaderBorder : BarSlotBorder);
		SquadSlot.Frame->SetPadding(FMargin(bLeader ? 3.f : 1.f));
		SquadSlot.HealthBar->SetPercent(Member->HealthComponent ? Member->HealthComponent->GetHealthFraction() : 0.f);
		SquadSlot.ColdBar->SetPercent(Member->ColdLevel / 100.f);
	}
}

FText UActionBarWidget::GetWeaponText() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return FText::GetEmpty();
	}
	if (Leader->bIsReloading)
	{
		return LOCTEXT("Reloading", "Перезарядка...\n[G] Граната");
	}
	const FString Id = Leader->CurrentWeapon ? Leader->CurrentWeapon->WeaponId : FString();
	if (Id == TEXT("grenade"))
	{
		return FText::Format(LOCTEXT("GrenadeSlot", "Граната [G]\n[{0} шт.] Урон: {1}"), Leader->GrenadesCount, FMath::FloorToInt(Leader->GrenadeDamage));
	}
	if (Id == TEXT("knife"))
	{
		return LOCTEXT("KnifeSlot", "Нож\n[Ближний бой] | [G]");
	}
	const FText WeaponName = Leader->CurrentWeapon && !Leader->CurrentWeapon->WeaponName.IsEmpty() ? Leader->CurrentWeapon->WeaponName : LOCTEXT("M16", "M16");
	return FText::Format(LOCTEXT("Weapon", "{0}\n[{1}/{2}] | [G] Граната"), WeaponName, Leader->CurrentClip, Leader->ReserveAmmo);
}

FText UActionBarWidget::GetSlotText(int32 Index) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const int32 MemberCount = Squad ? Squad->GetMembers().Num() : 0;
	const TCHAR* Tag = BarRoleTags[FMath::Clamp(Index, 0, 3)];
	return FText::FromString(FString::Printf(TEXT("[%d] %s"), Index + 1, Index < MemberCount ? Tag : (Index == 3 ? TEXT("РЕЗ") : Tag)));
}

FText UActionBarWidget::GetSelectorText(int32 Index) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || Index < 0 || Index > 3)
	{
		return FText::GetEmpty();
	}
	const FString Id = SelectorIds[Index];
	const FString Active = Leader->CurrentWeapon && Leader->CurrentWeapon->WeaponId == Id ? TEXT(" ◀ В РУКАХ") : TEXT("");
	const FWeaponAmmoState Ammo = Leader->GetAmmoState(Id);
	FString Line;
	switch (Index)
	{
	case 0: Line = FString::Printf(TEXT("🔫 [1] Автомат M16 [%d / %d]%s"), Ammo.Clip, Ammo.Reserve, *Active); break;
	case 1: Line = FString::Printf(TEXT("🔫 [2] Пистолет Beretta [%d / %d]%s"), Ammo.Clip, Ammo.Reserve, *Active); break;
	case 2: Line = FString::Printf(TEXT("🧨 [3] Граната [%d шт. | %d dmg | R:%.1fm]%s"), Leader->GrenadesCount,
		FMath::FloorToInt(Leader->GrenadeDamage), Leader->GrenadeEffectRadius / 100.f, *Active); break;
	default: Line = FString::Printf(TEXT("🔪 [4] Тактический нож [Ближний бой]%s"), *Active); break;
	}
	return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Line));
}

bool UActionBarWidget::IsWeaponSelectorOpen() const
{
	return SelectorPanel && SelectorPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UActionBarWidget::ToggleWeaponSelector()
{
	if (SelectorPanel)
	{
		SelectorPanel->SetVisibility(IsWeaponSelectorOpen() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		Refresh();
	}
}

bool UActionBarWidget::SelectWeapon(const FString& WeaponId)
{
	USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || (WeaponId == TEXT("grenade") && Leader->GrenadesCount <= 0))
	{
		return false;
	}
	UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
	const bool bSwitched = TurnBased && TurnBased->IsActive() ? TurnBased->SwitchActiveUnitWeapon(WeaponId) : Leader->SwitchToWeaponById(WeaponId);
	if (!bSwitched)
	{
		return false;
	}
	if (SelectorPanel)
	{
		SelectorPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		const FText Name = Leader->CurrentWeapon ? Leader->CurrentWeapon->WeaponName : FText::FromString(WeaponId);
		const int32 Damage = Leader->CurrentWeapon ? FMath::FloorToInt(Leader->CurrentWeapon->BaseDamage) : 0;
		Messages->PostMessage(Leader->DisplayName, FText::Format(LOCTEXT("Equipped", "🔫 Экипировано: {0} (Урон: {1})"), Name, Damage));
	}
	// Godot: the grenade starts the throw aim (outside turn-based combat the grid does not take the click).
	UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>();
	if (Grenades && WeaponId == TEXT("grenade") && !(TurnBased && TurnBased->IsActive()))
	{
		Grenades->StartAim(Leader);
	}
	else if (Grenades && Grenades->IsAiming())
	{
		Grenades->CancelAim(false);
	}
	Refresh();
	return true;
}

void UActionBarWidget::HandleGuard()
{
	if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer()))
	{
		PC->GuardKey();
	}
}

void UActionBarWidget::HandleWeaponSlot()
{
	ToggleWeaponSelector();
}

void UActionBarWidget::HandleSelectM16()
{
	SelectWeapon(TEXT("m16"));
}

void UActionBarWidget::HandleSelectPistol()
{
	SelectWeapon(TEXT("pistol"));
}

void UActionBarWidget::HandleSelectGrenade()
{
	SelectWeapon(TEXT("grenade"));
}

void UActionBarWidget::HandleSelectKnife()
{
	SelectWeapon(TEXT("knife"));
}

void UActionBarWidget::HandleRelocate()
{
	if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleRelocateSelectMode();
	}
}

void UActionBarWidget::HandleStance()
{
	if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer()))
	{
		PC->CycleLeaderStance();
	}
}

void UActionBarWidget::SelectSlot(int32 Index)
{
	if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer()))
	{
		PC->SelectSquadMember(Index);
	}
}

void UActionBarWidget::HandleSlot0()
{
	SelectSlot(0);
}

void UActionBarWidget::HandleSlot1()
{
	SelectSlot(1);
}

void UActionBarWidget::HandleSlot2()
{
	SelectSlot(2);
}

#undef LOCTEXT_NAMESPACE
