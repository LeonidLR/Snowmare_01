#include "UI/InventoryDrawerWidget.h"
#include "Interactables/RelocationSubsystem.h"
#include "UI/InventoryDragDropOperation.h"
#include "Subsystems/CodexEventBus.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "UI/CodexButtonFocus.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "InventoryDrawerWidget"

namespace
{
	// Godot inventory_drawer.gd: bg (0.07, 0.09, 0.13, 0.96), border (0.12, 0.65, 0.35), title (0.3, 0.9, 0.5).
	const FLinearColor DrawerBack = ACodexTacticsHUD::GodotColor(0.07f, 0.09f, 0.13f, 0.96f);
	const FLinearColor DrawerBorder = ACodexTacticsHUD::GodotColor(0.12f, 0.65f, 0.35f);
	const FLinearColor DrawerTitle = ACodexTacticsHUD::GodotColor(0.3f, 0.9f, 0.5f);
	const FLinearColor DrawerButton = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);
	const FLinearColor DrawerText(0.95f, 0.95f, 0.95f);

	const AOperativeCharacter* DrawerLeader(const UWorld* World)
	{
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		return Squad ? Squad->GetLeader() : nullptr;
	}

	int32 DrawerSquadCount(const UWorld* World, EDeployableType Type)
	{
		int32 Total = 0;
		if (const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Total += Member->GetDeployableCount(Type);
			}
		}
		return Total;
	}

	FText DrawerClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}

	// Sprint 13 drag visual.
	const FLinearColor DragBack = ACodexTacticsHUD::GodotColor(0.08f, 0.06f, 0.12f, 0.92f);
	const FLinearColor DragBorder = ACodexTacticsHUD::GodotColor(0.65f, 0.18f, 0.75f);
}

void UInventoryDrawerWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DrawerRoot"));
	WidgetTree->RootWidget = Root;

	// Godot: centre bottom, 540 wide (276 high before the Sprint 13 ammo lines; now sized to its lines), 94 px above the edge.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryDrawer"));
	Frame->SetBrushColor(DrawerBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 1.f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 1.f));
	FrameSlot->SetPosition(FVector2D(0.f, -94.f));
	FrameSlot->SetAutoSize(true);
	Panel = Frame;

	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DrawerWidth"));
	Width->SetWidthOverride(540.f);
	Frame->SetContent(Width);
	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DrawerBody"));
	Body->SetBrushColor(DrawerBack);
	Body->SetPadding(FMargin(14.f, 10.f));
	Width->AddChild(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DrawerColumn"));
	Body->SetContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DrawerTitle"));
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 14;
	TitleText->SetFont(TitleFont);
	TitleText->SetColorAndOpacity(FSlateColor(DrawerTitle));
	TitleText->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), TEXT("DrawerGrid"));
	Grid->SetSlotPadding(FMargin(5.f, 3.f));
	Column->AddChildToVerticalBox(Grid)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	auto MakeButton = [this](const FName& Name, UTextBlock*& OutText)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		CodexButtonFocus::Disable(Button); // a focused HUD button would swallow the game keys
		Button->SetBackgroundColor(DrawerButton);
		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = OutText->GetFont();
		Font.Size = 11;
		OutText->SetFont(Font);
		OutText->SetColorAndOpacity(FSlateColor(DrawerText));
		Button->AddChild(OutText);
		return Button;
	};
	const TCHAR* Names[] = { TEXT("BtnInvTurret"), TEXT("BtnInvBarricade"), TEXT("BtnInvMine"), TEXT("BtnInvMedkit"),
		TEXT("BtnInvCan"), TEXT("BtnInvBread"), TEXT("BtnInvChoco"), TEXT("BtnInvMatch"), TEXT("BtnInvTripwire"),
		TEXT("BtnInvAmmoM16"), TEXT("BtnInvAmmoPistol"), TEXT("BtnInvAmmoShotgun"), TEXT("BtnInvAmmoFuel"), TEXT("BtnInvAmmoCryo"),
		TEXT("BtnInvAmmoPlasma") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		UTextBlock* Text = nullptr;
		UButton* Button = MakeButton(Names[Index], Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetHeightOverride(32.f);
		Size->AddChild(Button);
		UUniformGridSlot* GridSlot = Grid->AddChildToUniformGrid(Size, Index / 2, Index % 2);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
		SlotButtons.Add(Button);
		SlotTexts.Add(Text);
	}
	UTextBlock* HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DrawerDragHint"));
	FSlateFontInfo HintFont = HintText->GetFont();
	HintFont.Size = 9;
	HintText->SetFont(HintFont);
	HintText->SetColorAndOpacity(FSlateColor(DrawerTitle * 0.85f));
	HintText->SetJustification(ETextJustify::Center);
	HintText->SetText(LOCTEXT("DragHint", "Передача: перетащите строку на бойца или его портрет (до 2 м, иначе боец подойдёт сам)"));
	Column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	UTextBlock* CloseText = nullptr;
	UButton* CloseButton = MakeButton(TEXT("BtnClose"), CloseText);
	CloseText->SetText(LOCTEXT("Close", "Закрыть"));
	Column->AddChildToVerticalBox(CloseButton);
	CloseButton->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleClose);

	SlotButtons[0]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleTurret);
	SlotButtons[1]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleBarricade);
	SlotButtons[2]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleMine);
	SlotButtons[3]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleMedkit);
	SlotButtons[4]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleCannedFood);
	SlotButtons[5]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleBread);
	SlotButtons[6]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleChocolate);
	SlotButtons[8]->OnClicked.AddDynamic(this, &UInventoryDrawerWidget::HandleTripwire);
}

void UInventoryDrawerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UInventoryDrawerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsOpen())
	{
		Refresh();
	}
}

bool UInventoryDrawerWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UInventoryDrawerWidget::Open()
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Visible);
		Refresh();
	}
}

void UInventoryDrawerWidget::Close()
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UInventoryDrawerWidget::Toggle()
{
	IsOpen() ? Close() : Open();
}

FText UInventoryDrawerWidget::GetTitleText() const
{
	const AOperativeCharacter* Leader = DrawerLeader(GetWorld());
	return Leader ? DrawerClean(FString::Printf(TEXT("🎒 ЛИЧНЫЙ ИНВЕНТАРЬ: %s"), *Leader->DisplayName.ToUpper().ToString()))
		: DrawerClean(TEXT("🎒 ЛИЧНЫЙ ИНВЕНТАРЬ ОПЕРАТИВНИКА"));
}

FText UInventoryDrawerWidget::GetSlotText(EInventoryDrawerSlot Line) const
{
	const AOperativeCharacter* Leader = DrawerLeader(GetWorld());
	if (!Leader)
	{
		return FText::GetEmpty();
	}
	auto Deployable = [this, Leader](EDeployableType Type, const TCHAR* Icon, const TCHAR* Name)
	{
		const int32 Count = Leader->GetDeployableCount(Type);
		const int32 Max = DeployableRules::GetMaxCarried(Type);
		const TCHAR* Active = Leader->SelectedDeployType == Type ? TEXT(" [F]") : TEXT("");
		const int32 InSquad = DrawerSquadCount(GetWorld(), Type);
		if (Count > 0)
		{
			return DrawerClean(FString::Printf(TEXT("%s %s%s: %d/%d шт."), Icon, Name, Active, Count, Max));
		}
		if (InSquad > 0)
		{
			return DrawerClean(FString::Printf(TEXT("%s %s%s: 0 (%d в отряде)"), Icon, Name, Active, InSquad));
		}
		return DrawerClean(FString::Printf(TEXT("%s %s%s: 0/%d шт."), Icon, Name, Active, Max));
	};
	switch (Line)
	{
	case EInventoryDrawerSlot::Turret: return Deployable(EDeployableType::Turret, TEXT("🛠️"), TEXT("Турель"));
	case EInventoryDrawerSlot::Barricade: return Deployable(EDeployableType::Barricade, TEXT("🧱"), TEXT("Баррикада"));
	case EInventoryDrawerSlot::Mine: return Deployable(EDeployableType::Mine, TEXT("💣"), TEXT("Мина"));
	case EInventoryDrawerSlot::Medkit: return DrawerClean(FString::Printf(TEXT("🩹 Аптечка [H]: %d шт."), Leader->MedkitsCount));
	case EInventoryDrawerSlot::CannedFood: return DrawerClean(FString::Printf(TEXT("🥫 Консервы [J]: %d шт."), Leader->CannedFoodCount));
	case EInventoryDrawerSlot::Bread: return DrawerClean(FString::Printf(TEXT("🍞 Хлеб [K]: %d шт."), Leader->BreadCount));
	case EInventoryDrawerSlot::Chocolate: return DrawerClean(FString::Printf(TEXT("🍫 Шоколад [L]: %d шт."), Leader->ChocolateCount));
	case EInventoryDrawerSlot::Tripwire:
	{
		const URelocationSubsystem* Relocation = GetWorld() ? GetWorld()->GetSubsystem<URelocationSubsystem>() : nullptr;
		return DrawerClean(FString::Printf(TEXT("🪤 Растяжка (2 гранаты): в отряде %d"), Relocation ? Relocation->GetSquadGrenades() : 0));
	}
	case EInventoryDrawerSlot::RifleAmmo: return DrawerClean(FString::Printf(TEXT("🔫 Патроны M16: %d"), Leader->GetReserve(TEXT("m16"))));
	case EInventoryDrawerSlot::PistolAmmo: return DrawerClean(FString::Printf(TEXT("🔫 Патроны 9мм: %d"), Leader->GetReserve(TEXT("pistol"))));
	case EInventoryDrawerSlot::ShotgunAmmo: return DrawerClean(FString::Printf(TEXT("💥 Дробь 12k: %d"), Leader->GetReserve(TEXT("shotgun"))));
	case EInventoryDrawerSlot::FlameFuel: return DrawerClean(FString::Printf(TEXT("🔥 Топливо: %d ед."), Leader->GetReserve(TEXT("flamethrower"))));
	case EInventoryDrawerSlot::CryoAmmo: return DrawerClean(FString::Printf(TEXT("❄️ Хладагент: %d ед."), Leader->GetReserve(TEXT("cryo_emitter"))));
	case EInventoryDrawerSlot::PlasmaAmmo: return DrawerClean(FString::Printf(TEXT("⚡ Плазма: %d ед."), Leader->GetReserve(TEXT("plasma_carbine"))));
	default: return DrawerClean(FString::Printf(TEXT("🪵 Спички: %d шт."), Leader->MatchesCount));
	}
}

bool UInventoryDrawerWidget::IsSlotEnabled(EInventoryDrawerSlot Line) const
{
	const AOperativeCharacter* Leader = DrawerLeader(GetWorld());
	if (!Leader)
	{
		return false;
	}
	switch (Line)
	{
	case EInventoryDrawerSlot::Turret: return DrawerSquadCount(GetWorld(), EDeployableType::Turret) > 0;
	case EInventoryDrawerSlot::Barricade: return DrawerSquadCount(GetWorld(), EDeployableType::Barricade) > 0;
	case EInventoryDrawerSlot::Mine: return DrawerSquadCount(GetWorld(), EDeployableType::Mine) > 0;
	case EInventoryDrawerSlot::Medkit: return Leader->MedkitsCount > 0;
	case EInventoryDrawerSlot::CannedFood: return Leader->CannedFoodCount > 0;
	case EInventoryDrawerSlot::Bread: return Leader->BreadCount > 0;
	case EInventoryDrawerSlot::Chocolate: return Leader->ChocolateCount > 0;
	case EInventoryDrawerSlot::Tripwire:
	{
		const URelocationSubsystem* Relocation = GetWorld() ? GetWorld()->GetSubsystem<URelocationSubsystem>() : nullptr;
		return Relocation && Relocation->GetSquadGrenades() >= 2;
	}
	default:
	{
		// Matches and ammo: information lines, enabled while there is something to drag over.
		ETransferItem Item = ETransferItem::Medkit;
		return GetTransferItem(Line, Item) && TransferRules::GetAvailable(*Leader, Item) > 0;
	}
	}
}

bool UInventoryDrawerWidget::IsSlotVisible(EInventoryDrawerSlot Line) const
{
	switch (Line)
	{
	case EInventoryDrawerSlot::ShotgunAmmo:
	case EInventoryDrawerSlot::FlameFuel:
	case EInventoryDrawerSlot::CryoAmmo:
	case EInventoryDrawerSlot::PlasmaAmmo:
		return IsSlotEnabled(Line); // Godot transfer_dialog.gd hid the special ammo the operative does not carry
	default:
		return true;
	}
}

bool UInventoryDrawerWidget::GetTransferItem(EInventoryDrawerSlot Line, ETransferItem& OutItem)
{
	switch (Line)
	{
	case EInventoryDrawerSlot::Turret: OutItem = ETransferItem::Turret; return true;
	case EInventoryDrawerSlot::Barricade: OutItem = ETransferItem::Barricade; return true;
	case EInventoryDrawerSlot::Mine: OutItem = ETransferItem::Mine; return true;
	case EInventoryDrawerSlot::Medkit: OutItem = ETransferItem::Medkit; return true;
	case EInventoryDrawerSlot::CannedFood: OutItem = ETransferItem::CannedFood; return true;
	case EInventoryDrawerSlot::Bread: OutItem = ETransferItem::Bread; return true;
	case EInventoryDrawerSlot::Chocolate: OutItem = ETransferItem::Chocolate; return true;
	case EInventoryDrawerSlot::Matches: OutItem = ETransferItem::Matches; return true;
	case EInventoryDrawerSlot::RifleAmmo: OutItem = ETransferItem::RifleAmmo; return true;
	case EInventoryDrawerSlot::PistolAmmo: OutItem = ETransferItem::PistolAmmo; return true;
	case EInventoryDrawerSlot::ShotgunAmmo: OutItem = ETransferItem::ShotgunAmmo; return true;
	case EInventoryDrawerSlot::FlameFuel: OutItem = ETransferItem::FlameFuel; return true;
	case EInventoryDrawerSlot::CryoAmmo: OutItem = ETransferItem::CryoAmmo; return true;
	case EInventoryDrawerSlot::PlasmaAmmo: OutItem = ETransferItem::PlasmaAmmo; return true;
	default: return false;
	}
}

UInventoryDragDropOperation* UInventoryDrawerWidget::CreateDragOperation(EInventoryDrawerSlot Line)
{
	ETransferItem Item = ETransferItem::Medkit;
	AOperativeCharacter* Leader = const_cast<AOperativeCharacter*>(DrawerLeader(GetWorld()));
	const int32 Available = Leader && GetTransferItem(Line, Item) ? TransferRules::GetAvailable(*Leader, Item) : 0;
	if (Available <= 0)
	{
		return nullptr;
	}
	UInventoryDragDropOperation* Operation = NewObject<UInventoryDragDropOperation>(this);
	Operation->Item = Item;
	Operation->Sender = Leader;
	Operation->Available = Available;
	Operation->Pivot = EDragPivot::CenterCenter;

	UBorder* Visual = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Visual->SetBrushColor(DragBorder);
	Visual->SetPadding(FMargin(2.f));
	UBorder* Inner = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Inner->SetBrushColor(DragBack);
	Inner->SetPadding(FMargin(8.f, 4.f));
	Visual->SetContent(Inner);
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Label->GetFont();
	Font.Size = 11;
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(DrawerText));
	Label->SetText(DrawerClean(FString::Printf(TEXT("%s x%d"), *TransferRules::GetItemName(Item), Available)));
	Inner->SetContent(Label);
	Operation->DefaultDragVisual = Visual;
	return Operation;
}

int32 UInventoryDrawerWidget::FindSlotAt(const FVector2D& ScreenPosition) const
{
	for (int32 Index = 0; Index < SlotButtons.Num(); ++Index)
	{
		const UWidget* Cell = SlotButtons[Index] ? SlotButtons[Index]->GetParent() : nullptr;
		if (Cell && Cell->GetVisibility() != ESlateVisibility::Collapsed && SlotButtons[Index]->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FReply UInventoryDrawerWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	PressedSlot = INDEX_NONE;
	if (IsOpen() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Index = FindSlotAt(InMouseEvent.GetScreenSpacePosition());
		ETransferItem Item = ETransferItem::Medkit;
		const AOperativeCharacter* Leader = DrawerLeader(GetWorld());
		if (Index != INDEX_NONE && Leader && GetTransferItem(static_cast<EInventoryDrawerSlot>(Index), Item) && TransferRules::GetAvailable(*Leader, Item) > 0)
		{
			// The line takes the press: a drag hands it over, a release without a drag clicks it (NativeOnMouseButtonUp).
			PressedSlot = Index;
			return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
		}
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UInventoryDrawerWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PressedSlot != INDEX_NONE && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Index = PressedSlot;
		PressedSlot = INDEX_NONE;
		if (FindSlotAt(InMouseEvent.GetScreenSpacePosition()) == Index && SlotButtons[Index]->GetIsEnabled())
		{
			Activate(static_cast<EInventoryDrawerSlot>(Index));
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UInventoryDrawerWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (PressedSlot != INDEX_NONE)
	{
		OutOperation = CreateDragOperation(static_cast<EInventoryDrawerSlot>(PressedSlot));
		PressedSlot = INDEX_NONE;
		return;
	}
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
}

void UInventoryDrawerWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
	if (UInventoryDragDropOperation* Operation = Cast<UInventoryDragDropOperation>(InOperation))
	{
		HandleWorldDrop(Operation, InDragDropEvent.GetScreenSpacePosition());
	}
}

void UInventoryDrawerWidget::HandleWorldDrop(UInventoryDragDropOperation* Operation, const FVector2D& ScreenPosition)
{
	APlayerController* PC = GetOwningPlayer();
	ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
	if (!Operation || !Hud || (Panel && Panel->GetCachedGeometry().IsUnderLocation(ScreenPosition)))
	{
		return; // dropped back on the drawer: nothing to do
	}
	Hud->HandleDragReleasedOverWorld(Operation, ScreenPosition);
}

bool UInventoryDrawerWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UInventoryDragDropOperation* Operation = Cast<UInventoryDragDropOperation>(InOperation);
	if (!Operation || !IsOpen())
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}
	if (Operation->Container.IsValid())
	{
		const APlayerController* PC = GetOwningPlayer();
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		AOperativeCharacter* Leader = const_cast<AOperativeCharacter*>(DrawerLeader(GetWorld()));
		if (Hud && Leader)
		{
			Hud->HandleTakeDrop(Operation->Container.Get(), Operation->Item, Leader);
		}
	}
	return true; // an inventory line dropped back on the drawer: nothing to do
}

void UInventoryDrawerWidget::Refresh()
{
	if (TitleText)
	{
		TitleText->SetText(GetTitleText());
	}
	int32 Visible = 0;
	for (int32 Index = 0; Index < SlotTexts.Num(); ++Index)
	{
		const EInventoryDrawerSlot Line = static_cast<EInventoryDrawerSlot>(Index);
		SlotTexts[Index]->SetText(GetSlotText(Line));
		// Matches and ammo are information only (Godot btn_match has no action) but enabled while they can be dragged over.
		SlotButtons[Index]->SetIsEnabled(IsSlotEnabled(Line));
		UWidget* Cell = SlotButtons[Index]->GetParent();
		const bool bVisible = IsSlotVisible(Line);
		Cell->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Cell->Slot); GridSlot && bVisible)
		{
			GridSlot->SetRow(Visible / 2);
			GridSlot->SetColumn(Visible % 2);
			++Visible;
		}
	}
}

void UInventoryDrawerWidget::Activate(EInventoryDrawerSlot Line)
{
	ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(GetOwningPlayer());
	if (!PC)
	{
		return;
	}
	switch (Line)
	{
	case EInventoryDrawerSlot::Turret:
	case EInventoryDrawerSlot::Barricade:
	case EInventoryDrawerSlot::Mine:
		Close(); // Godot hides the drawer before the placement starts
		PC->StartPlacementForType(Line == EInventoryDrawerSlot::Turret ? EDeployableType::Turret
			: (Line == EInventoryDrawerSlot::Barricade ? EDeployableType::Barricade : EDeployableType::Mine));
		break;
	case EInventoryDrawerSlot::Medkit: PC->UseSquadItem(EPersonalItem::Medkit); break;
	case EInventoryDrawerSlot::CannedFood: PC->UseSquadItem(EPersonalItem::CannedFood); break;
	case EInventoryDrawerSlot::Bread: PC->UseSquadItem(EPersonalItem::Bread); break;
	case EInventoryDrawerSlot::Chocolate: PC->UseSquadItem(EPersonalItem::Chocolate); break;
	case EInventoryDrawerSlot::Tripwire:
		Close();
		PC->StartTripwirePlacement();
		break;
	default: break;
	}
	// Godot inventory_drawer.gd: EventBus.item_used(target_soldier, "MEDKIT" / ...).
	const TCHAR* ItemId = Line == EInventoryDrawerSlot::Medkit ? TEXT("MEDKIT") : (Line == EInventoryDrawerSlot::CannedFood ? TEXT("CANNED_FOOD")
		: (Line == EInventoryDrawerSlot::Bread ? TEXT("BREAD") : (Line == EInventoryDrawerSlot::Chocolate ? TEXT("CHOCOLATE") : nullptr)));
	UCodexEventBus* Bus = UCodexEventBus::Get(this);
	const USquadSubsystem* ItemSquad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (ItemId && Bus && ItemSquad && ItemSquad->GetLeader())
	{
		Bus->OnItemUsed.Broadcast(ItemSquad->GetLeader(), ItemId);
	}
	Refresh();
}

void UInventoryDrawerWidget::HandleTurret() { Activate(EInventoryDrawerSlot::Turret); }
void UInventoryDrawerWidget::HandleBarricade() { Activate(EInventoryDrawerSlot::Barricade); }
void UInventoryDrawerWidget::HandleMine() { Activate(EInventoryDrawerSlot::Mine); }
void UInventoryDrawerWidget::HandleMedkit() { Activate(EInventoryDrawerSlot::Medkit); }
void UInventoryDrawerWidget::HandleCannedFood() { Activate(EInventoryDrawerSlot::CannedFood); }
void UInventoryDrawerWidget::HandleBread() { Activate(EInventoryDrawerSlot::Bread); }
void UInventoryDrawerWidget::HandleChocolate() { Activate(EInventoryDrawerSlot::Chocolate); }
void UInventoryDrawerWidget::HandleTripwire() { Activate(EInventoryDrawerSlot::Tripwire); }
void UInventoryDrawerWidget::HandleClose() { Close(); }

#undef LOCTEXT_NAMESPACE
