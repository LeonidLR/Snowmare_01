#include "UI/InventoryDrawerWidget.h"
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
}

void UInventoryDrawerWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DrawerRoot"));
	WidgetTree->RootWidget = Root;

	// Godot: centre bottom, 540 x 276, 94 px above the edge.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryDrawer"));
	Frame->SetBrushColor(DrawerBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 1.f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 1.f));
	FrameSlot->SetPosition(FVector2D(0.f, -94.f));
	FrameSlot->SetSize(FVector2D(540.f, 276.f));
	Panel = Frame;

	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DrawerBody"));
	Body->SetBrushColor(DrawerBack);
	Body->SetPadding(FMargin(14.f, 10.f));
	Frame->SetContent(Body);
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
		TEXT("BtnInvCan"), TEXT("BtnInvBread"), TEXT("BtnInvChoco"), TEXT("BtnInvMatch") };
	for (int32 Index = 0; Index < 8; ++Index)
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
	default: return Leader->MatchesCount > 0; // Godot: a disabled label-like button
	}
}

void UInventoryDrawerWidget::Refresh()
{
	if (TitleText)
	{
		TitleText->SetText(GetTitleText());
	}
	for (int32 Index = 0; Index < SlotTexts.Num(); ++Index)
	{
		const EInventoryDrawerSlot Line = static_cast<EInventoryDrawerSlot>(Index);
		SlotTexts[Index]->SetText(GetSlotText(Line));
		// Matches are information only (Godot btn_match has no action).
		SlotButtons[Index]->SetIsEnabled(Line != EInventoryDrawerSlot::Matches && IsSlotEnabled(Line));
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
void UInventoryDrawerWidget::HandleClose() { Close(); }

#undef LOCTEXT_NAMESPACE
