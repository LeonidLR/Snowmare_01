#include "UI/SaveLoadDialogWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/SaveGameRules.h"
#include "Core/SaveGameSubsystem.h"
#include "Engine/World.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "SaveLoadDialogWidget"

namespace
{
	// Godot save_load_dialog.gd colours.
	const FLinearColor SaveBack = ACodexTacticsHUD::GodotColor(0.06f, 0.08f, 0.12f, 0.98f);
	const FLinearColor SaveBorder = ACodexTacticsHUD::GodotColor(0.2f, 0.75f, 0.95f, 0.9f);
	const FLinearColor SaveTitle = ACodexTacticsHUD::GodotColor(0.3f, 0.9f, 1.f);
	const FLinearColor SaveSubtitle = ACodexTacticsHUD::GodotColor(0.65f, 0.75f, 0.85f, 0.9f);
	const FLinearColor SaveLabel = ACodexTacticsHUD::GodotColor(0.8f, 0.9f, 1.f);
	const FLinearColor SaveHeader = ACodexTacticsHUD::GodotColor(0.7f, 0.8f, 0.9f);
	const FLinearColor SaveStatusOk = ACodexTacticsHUD::GodotColor(0.4f, 0.95f, 0.65f);
	const FLinearColor SaveStatusError = ACodexTacticsHUD::GodotColor(1.f, 0.4f, 0.4f);
	const FLinearColor SaveEmpty = ACodexTacticsHUD::GodotColor(0.6f, 0.65f, 0.75f, 0.8f);
	const FLinearColor CardBack = ACodexTacticsHUD::GodotColor(0.11f, 0.15f, 0.22f, 0.9f);
	const FLinearColor CardBorder = ACodexTacticsHUD::GodotColor(0.25f, 0.45f, 0.65f, 0.7f);
	const FLinearColor CardSelectedBack = ACodexTacticsHUD::GodotColor(0.14f, 0.22f, 0.34f, 0.95f);
	const FLinearColor CardSelectedBorder = ACodexTacticsHUD::GodotColor(0.3f, 0.9f, 1.f);
	const FLinearColor CardName = FLinearColor::White;
	const FLinearColor CardStage = ACodexTacticsHUD::GodotColor(1.f, 0.85f, 0.3f);
	const FLinearColor CardDetails = ACodexTacticsHUD::GodotColor(0.65f, 0.72f, 0.8f);
	const FLinearColor BadgeAuto = ACodexTacticsHUD::GodotColor(1.f, 0.65f, 0.2f);
	const FLinearColor BadgeQuick = ACodexTacticsHUD::GodotColor(0.88f, 0.45f, 1.f);
	const FLinearColor BadgeManual = ACodexTacticsHUD::GodotColor(0.3f, 0.85f, 1.f);
	const FLinearColor ConfirmDim = ACodexTacticsHUD::GodotColor(0.02f, 0.04f, 0.07f, 0.82f);
	const FLinearColor ConfirmBack = ACodexTacticsHUD::GodotColor(0.09f, 0.12f, 0.18f, 0.98f);
	const FLinearColor ConfirmBorder = ACodexTacticsHUD::GodotColor(1.f, 0.7f, 0.2f, 0.95f);
	const FLinearColor ConfirmTitle = ACodexTacticsHUD::GodotColor(1.f, 0.75f, 0.2f);
	const FLinearColor DialogButton = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);

	FText SaveClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}
}

void USaveCardProxy::HandleClicked()
{
	USaveLoadDialogWidget* Owner = Dialog.Get();
	if (!Owner)
	{
		return;
	}
	switch (Action)
	{
	case EAction::Load: Owner->LoadSlot(Slot); break;
	case EAction::Delete: Owner->DeleteSlot(Slot); break;
	default: Owner->ClickCard(Slot); break;
	}
}

void USaveLoadDialogWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SaveRoot"));
	WidgetTree->RootWidget = Root;

	auto MakeText = [this](int32 Size, const FLinearColor& Color, const FString& Text, ETextJustify::Type Justify = ETextJustify::Center)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetJustification(Justify);
		Block->SetAutoWrapText(true);
		Block->SetText(SaveClean(Text));
		return Block;
	};
	auto MakeButton = [this, &MakeText](const FString& Label, float Width, float Height, UTextBlock** OutText = nullptr)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Button->SetBackgroundColor(DialogButton);
		UTextBlock* Text = MakeText(12, FLinearColor(0.95f, 0.95f, 0.95f), Label);
		Button->AddChild(Text);
		if (OutText)
		{
			*OutText = Text;
		}
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(Width);
		Size->SetHeightOverride(Height);
		Size->AddChild(Button);
		return TPair<USizeBox*, UButton*>(Size, Button);
	};

	// Godot: centred, 680 x 520.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SaveLoadDialog"));
	Frame->SetBrushColor(SaveBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetSize(FVector2D(680.f, 520.f));
	Panel = Frame;
	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Body->SetBrushColor(SaveBack);
	Body->SetPadding(FMargin(18.f, 14.f));
	Frame->SetContent(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Body->SetContent(Column);

	TitleText = MakeText(18, SaveTitle, TEXT("💾 SAVE MANAGER"));
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
	Column->AddChildToVerticalBox(MakeText(11, SaveSubtitle, TEXT("Click to select, double-click to quickly overwrite / load")))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(NameRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UHorizontalBoxSlot* LabelSlot = NameRow->AddChildToHorizontalBox(MakeText(13, SaveLabel, TEXT("Slot name:"), ETextJustify::Left));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	SlotNameEdit = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SlotNameEdit"));
	SlotNameEdit->SetHintText(LOCTEXT("Hint", "e.g. Leonid_01"));
	{
		// Godot input style: dark field (0.1, 0.14, 0.2, 0.8), light text.
		FEditableTextBoxStyle Style = SlotNameEdit->GetWidgetStyle();
		const FLinearColor Field = ACodexTacticsHUD::GodotColor(0.1f, 0.14f, 0.2f, 0.8f);
		Style.BackgroundImageNormal.TintColor = FSlateColor(Field);
		Style.BackgroundImageHovered.TintColor = FSlateColor(Field);
		Style.BackgroundImageFocused.TintColor = FSlateColor(Field);
		Style.ForegroundColor = FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f));
		Style.FocusedForegroundColor = FSlateColor(FLinearColor::White);
		Style.TextStyle.Font.Size = 13;
		SlotNameEdit->SetWidgetStyle(Style);
	}
	SlotNameEdit->OnTextChanged.AddDynamic(this, &USaveLoadDialogWidget::HandleSlotNameChanged);
	UHorizontalBoxSlot* EditSlot = NameRow->AddChildToHorizontalBox(SlotNameEdit);
	EditSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	EditSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	UTextBlock* SaveLabelText = nullptr;
	const TPair<USizeBox*, UButton*> Save = MakeButton(TEXT("💾 Save"), 150.f, 34.f, &SaveLabelText);
	SaveButton = Save.Value;
	SaveButtonText = SaveLabelText;
	SaveButton->OnClicked.AddDynamic(this, &USaveLoadDialogWidget::HandleSave);
	NameRow->AddChildToHorizontalBox(Save.Key);

	Column->AddChildToVerticalBox(MakeText(12, SaveHeader, TEXT("Available saves (single click: select, double click: action):"), ETextJustify::Left))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SavesScroll"));
	USizeBox* ScrollSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ScrollSize->SetHeightOverride(260.f);
	ScrollSize->AddChild(Scroll);
	Column->AddChildToVerticalBox(ScrollSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	SavesList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SavesList"));
	Scroll->AddChild(SavesList);

	StatusText = MakeText(12, SaveStatusOk, TEXT("Select a slot to load, or enter a name to save."));
	Column->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	const TPair<USizeBox*, UButton*> CloseButton = MakeButton(TEXT("✖ Back to menu"), 160.f, 34.f);
	CloseButton.Value->OnClicked.AddDynamic(this, &USaveLoadDialogWidget::HandleClose);
	Column->AddChildToVerticalBox(CloseButton.Key)->SetHorizontalAlignment(HAlign_Center);

	// Overwrite confirmation (Godot _setup_confirmation_modal): dim over the screen, 460 x 170 panel.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConfirmOverlay"));
	Dim->SetBrushColor(ConfirmDim);
	Dim->SetVisibility(ESlateVisibility::Collapsed);
	Dim->SetHorizontalAlignment(HAlign_Center);
	Dim->SetVerticalAlignment(VAlign_Center);
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	DimSlot->SetOffsets(FMargin(0.f));
	ConfirmOverlay = Dim;
	UBorder* ConfirmFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ConfirmFrame->SetBrushColor(ConfirmBorder);
	ConfirmFrame->SetPadding(FMargin(2.f));
	USizeBox* ConfirmSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ConfirmSize->SetWidthOverride(460.f);
	ConfirmSize->SetHeightOverride(170.f);
	ConfirmSize->AddChild(ConfirmFrame);
	Dim->SetContent(ConfirmSize);
	UBorder* ConfirmBody = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ConfirmBody->SetBrushColor(ConfirmBack);
	ConfirmBody->SetPadding(FMargin(16.f, 12.f));
	ConfirmFrame->SetContent(ConfirmBody);
	UVerticalBox* ConfirmColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	ConfirmBody->SetContent(ConfirmColumn);
	ConfirmColumn->AddChildToVerticalBox(MakeText(16, ConfirmTitle, TEXT("⚠️ OVERWRITE SAVE")))->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	ConfirmMessage = MakeText(12, FLinearColor(0.9f, 0.9f, 0.9f), TEXT("Overwrite the save?\nThe previous data will be replaced."));
	ConfirmColumn->AddChildToVerticalBox(ConfirmMessage)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	UHorizontalBox* ConfirmRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	ConfirmColumn->AddChildToVerticalBox(ConfirmRow)->SetHorizontalAlignment(HAlign_Center);
	const TPair<USizeBox*, UButton*> Yes = MakeButton(TEXT("✅ Yes, overwrite"), 170.f, 32.f);
	Yes.Value->OnClicked.AddDynamic(this, &USaveLoadDialogWidget::HandleConfirmYes);
	ConfirmRow->AddChildToHorizontalBox(Yes.Key)->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	const TPair<USizeBox*, UButton*> No = MakeButton(TEXT("❌ No, cancel"), 140.f, 32.f);
	No.Value->OnClicked.AddDynamic(this, &USaveLoadDialogWidget::HandleConfirmNo);
	ConfirmRow->AddChildToHorizontalBox(No.Key);
}

void USaveLoadDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

USaveGameSubsystem* USaveLoadDialogWidget::GetSaves() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
}

bool USaveLoadDialogWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

bool USaveLoadDialogWidget::IsConfirmOpen() const
{
	return ConfirmOverlay && ConfirmOverlay->GetVisibility() != ESlateVisibility::Collapsed;
}

void USaveLoadDialogWidget::CancelConfirmation()
{
	PendingOverwrite.Reset();
	if (ConfirmOverlay)
	{
		ConfirmOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USaveLoadDialogWidget::Open(ESaveDialogMode Mode)
{
	CurrentMode = Mode;
	if (!Panel)
	{
		return;
	}
	Panel->SetVisibility(ESlateVisibility::Visible);
	CancelConfirmation();
	TitleText->SetText(SaveClean(Mode == ESaveDialogMode::Save ? TEXT("💾 SAVE AND OVERWRITE")
		: (Mode == ESaveDialogMode::Load ? TEXT("📂 LOAD GAME") : TEXT("💾 SAVE MANAGER"))));
	SelectedSlot.Reset();
	if (const USaveGameSubsystem* Saves = GetSaves())
	{
		SlotNameEdit->SetText(FText::FromString(Saves->SuggestNextSlotName()));
	}
	RefreshSavesList();
	UpdateSaveButton();
}

void USaveLoadDialogWidget::Close()
{
	CancelConfirmation();
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

FText USaveLoadDialogWidget::GetTitleText() const { return TitleText ? TitleText->GetText() : FText::GetEmpty(); }
FText USaveLoadDialogWidget::GetStatusText() const { return StatusText ? StatusText->GetText() : FText::GetEmpty(); }
FString USaveLoadDialogWidget::GetSlotNameText() const { return SlotNameEdit ? SlotNameEdit->GetText().ToString() : FString(); }
FText USaveLoadDialogWidget::GetSaveButtonText() const { return SaveButtonText ? SaveButtonText->GetText() : FText::GetEmpty(); }

void USaveLoadDialogWidget::SetSlotNameText(const FString& Name)
{
	if (SlotNameEdit)
	{
		SlotNameEdit->SetText(FText::FromString(Name));
	}
	UpdateSaveButton();
}

void USaveLoadDialogWidget::SetStatus(const FString& Text, bool bError)
{
	if (StatusText)
	{
		StatusText->SetText(SaveClean(Text));
		StatusText->SetColorAndOpacity(FSlateColor(bError ? SaveStatusError : SaveStatusOk));
	}
}

void USaveLoadDialogWidget::UpdateSaveButton()
{
	const USaveGameSubsystem* Saves = GetSaves();
	const FString Raw = GetSlotNameText().TrimStartAndEnd();
	const bool bExists = Saves && !Raw.IsEmpty() && Saves->HasSave(SaveGameRules::SanitizeSlotName(Raw));
	if (SaveButtonText)
	{
		SaveButtonText->SetText(SaveClean(bExists ? TEXT("💾 Overwrite") : TEXT("💾 Save")));
	}
}

void USaveLoadDialogWidget::RefreshSavesList()
{
	if (!SavesList)
	{
		return;
	}
	SavesList->ClearChildren();
	CardProxies.Reset();
	CardFrames.Reset();
	const USaveGameSubsystem* Saves = GetSaves();
	const TArray<FSaveSlotInfo> All = Saves ? Saves->GetAllSaves() : TArray<FSaveSlotInfo>();
	auto MakeText = [this](int32 Size, const FLinearColor& Color, const FString& Text)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Block->GetFont();
		Font.Size = Size;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetText(SaveClean(Text));
		return Block;
	};
	if (All.IsEmpty())
	{
		UTextBlock* Empty = MakeText(13, SaveEmpty, TEXT("No saved games.\nEnter a save name above and press \"Save\"."));
		Empty->SetJustification(ETextJustify::Center);
		SavesList->AddChildToVerticalBox(Empty)->SetPadding(FMargin(0.f, 40.f));
		UpdateSaveButton();
		return;
	}
	auto Bind = [this](UButton* Button, const FString& SlotId, USaveCardProxy::EAction Action)
	{
		USaveCardProxy* Proxy = NewObject<USaveCardProxy>(this);
		Proxy->Slot = SlotId;
		Proxy->Action = Action;
		Proxy->Dialog = this;
		Button->OnClicked.AddDynamic(Proxy, &USaveCardProxy::HandleClicked);
		CardProxies.Add(Proxy);
	};
	for (const FSaveSlotInfo& Info : All)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrushColor(CardBorder);
		Card->SetPadding(FMargin(1.f));
		UBorder* CardBody = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		CardBody->SetBrushColor(CardBack);
		CardBody->SetPadding(FMargin(8.f, 4.f));
		Card->SetContent(CardBody);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		CardBody->SetContent(Row);

		UButton* Select = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Select->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Select->AddChild(Texts);
		UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Texts->AddChildToVerticalBox(Top);
		const bool bAuto = Info.SaveType == TEXT("autosave");
		const bool bQuick = Info.SaveType == TEXT("quicksave");
		Top->AddChildToHorizontalBox(MakeText(11, bAuto ? BadgeAuto : (bQuick ? BadgeQuick : BadgeManual),
			bAuto ? TEXT("[AUTO]") : (bQuick ? TEXT("[QUICK]") : TEXT("[MANUAL]"))))->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		Top->AddChildToHorizontalBox(MakeText(13, CardName, Info.SlotName))->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		Top->AddChildToHorizontalBox(MakeText(11, CardStage, TEXT("📍 ") + Info.StageName));
		FString Details = FString::Printf(TEXT("Author: %s   %s"), *Info.Author, *Info.DateTime);
		if (!Info.SquadSummary.IsEmpty())
		{
			Details += TEXT("   ") + Info.SquadSummary;
		}
		Texts->AddChildToVerticalBox(MakeText(10, CardDetails, Details));
		UHorizontalBoxSlot* SelectSlot = Row->AddChildToHorizontalBox(Select);
		SelectSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Bind(Select, Info.SlotName, USaveCardProxy::EAction::Select);

		UButton* Load = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Load->SetBackgroundColor(DialogButton);
		Load->AddChild(MakeText(11, FLinearColor(0.95f, 0.95f, 0.95f), TEXT("📂 Load")));
		UHorizontalBoxSlot* LoadSlot = Row->AddChildToHorizontalBox(Load);
		LoadSlot->SetVerticalAlignment(VAlign_Center);
		LoadSlot->SetPadding(FMargin(6.f, 0.f));
		Bind(Load, Info.SlotName, USaveCardProxy::EAction::Load);

		UButton* Delete = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Delete->SetBackgroundColor(DialogButton);
		Delete->SetToolTipText(LOCTEXT("DeleteTip", "Delete save"));
		Delete->AddChild(MakeText(11, FLinearColor(0.95f, 0.95f, 0.95f), TEXT("X")));
		Row->AddChildToHorizontalBox(Delete)->SetVerticalAlignment(VAlign_Center);
		Bind(Delete, Info.SlotName, USaveCardProxy::EAction::Delete);

		SavesList->AddChildToVerticalBox(Card)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		CardFrames.Add(Info.SlotName, Card);
	}
	if (TObjectPtr<UBorder>* Selected = CardFrames.Find(SelectedSlot))
	{
		(*Selected)->SetBrushColor(CardSelectedBorder);
		Cast<UBorder>((*Selected)->GetContent())->SetBrushColor(CardSelectedBack);
	}
	UpdateSaveButton();
}

void USaveLoadDialogWidget::ClickCard(const FString& SlotName)
{
	if (SelectedSlot == SlotName)
	{
		// Godot double click: load in the load mode, otherwise save (with the overwrite confirmation).
		if (CurrentMode == ESaveDialogMode::Load)
		{
			SetStatus(FString::Printf(TEXT("📂 Loading slot \"%s\"..."), *SlotName));
			LoadSlot(SlotName);
		}
		else
		{
			RequestOverwrite(SlotName);
		}
		return;
	}
	if (TObjectPtr<UBorder>* Previous = CardFrames.Find(SelectedSlot))
	{
		(*Previous)->SetBrushColor(CardBorder);
		Cast<UBorder>((*Previous)->GetContent())->SetBrushColor(CardBack);
	}
	SelectedSlot = SlotName;
	if (TObjectPtr<UBorder>* Card = CardFrames.Find(SlotName))
	{
		(*Card)->SetBrushColor(CardSelectedBorder);
		Cast<UBorder>((*Card)->GetContent())->SetBrushColor(CardSelectedBack);
	}
	SetSlotNameText(SlotName);
	SetStatus(FString::Printf(TEXT("Slot \"%s\" selected. Press \"Overwrite\" or double-click the slot."), *SlotName));
}

void USaveLoadDialogWidget::PressSave()
{
	const USaveGameSubsystem* Saves = GetSaves();
	FString Raw = GetSlotNameText().TrimStartAndEnd();
	if (Raw.IsEmpty() && Saves)
	{
		Raw = Saves->SuggestNextSlotName();
		SetSlotNameText(Raw);
	}
	const FString SlotId = SaveGameRules::SanitizeSlotName(Raw);
	if (Saves && Saves->HasSave(SlotId))
	{
		RequestOverwrite(SlotId);
	}
	else
	{
		SaveSlot(SlotId);
	}
}

void USaveLoadDialogWidget::RequestOverwrite(const FString& SlotName)
{
	const USaveGameSubsystem* Saves = GetSaves();
	if (!Saves || !Saves->HasSave(SlotName))
	{
		SaveSlot(SlotName);
		return;
	}
	PendingOverwrite = SlotName;
	ConfirmMessage->SetText(FText::FromString(FString::Printf(TEXT("Overwrite the existing save \"%s\"?\nThe previous data will be replaced."), *SlotName)));
	ConfirmOverlay->SetVisibility(ESlateVisibility::Visible);
}

void USaveLoadDialogWidget::ConfirmOverwrite()
{
	const FString SlotId = PendingOverwrite;
	CancelConfirmation();
	if (!SlotId.IsEmpty())
	{
		SaveSlot(SlotId);
	}
}

void USaveLoadDialogWidget::SaveSlot(const FString& SlotName)
{
	USaveGameSubsystem* Saves = GetSaves();
	if (!Saves)
	{
		return;
	}
	const bool bOverwrite = Saves->HasSave(SlotName);
	if (Saves->SaveToSlotWithMessage(SlotName))
	{
		SetStatus(bOverwrite ? FString::Printf(TEXT("✅ Save \"%s\" overwritten!"), *SlotName)
			: FString::Printf(TEXT("✅ Game saved to slot \"%s\"!"), *SlotName));
		SelectedSlot = SlotName;
		RefreshSavesList();
	}
	else
	{
		SetStatus(FString::Printf(TEXT("❌ Failed to save to \"%s\"!"), *SlotName), true);
	}
}

void USaveLoadDialogWidget::LoadSlot(const FString& SlotName)
{
	USaveGameSubsystem* Saves = GetSaves();
	if (Saves && Saves->LoadFromSlotWithMessage(SlotName))
	{
		Close();
		const APlayerController* PC = GetOwningPlayer();
		if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
		{
			Hud->ClosePauseMenus(); // Godot: both windows close, time runs again
		}
		return;
	}
	SetStatus(FString::Printf(TEXT("❌ Failed to load slot \"%s\"!"), *SlotName), true);
}

void USaveLoadDialogWidget::DeleteSlot(const FString& SlotName)
{
	USaveGameSubsystem* Saves = GetSaves();
	if (Saves && Saves->DeleteSave(SlotName))
	{
		if (SelectedSlot == SlotName)
		{
			SelectedSlot.Reset();
		}
		SetStatus(FString::Printf(TEXT("Save \"%s\" deleted."), *SlotName));
		RefreshSavesList();
	}
	else
	{
		SetStatus(FString::Printf(TEXT("Could not delete \"%s\"."), *SlotName), true);
	}
}

void USaveLoadDialogWidget::HandleSave() { PressSave(); }
void USaveLoadDialogWidget::HandleConfirmYes() { ConfirmOverwrite(); }
void USaveLoadDialogWidget::HandleConfirmNo() { CancelConfirmation(); }
void USaveLoadDialogWidget::HandleSlotNameChanged(const FText& Text) { UpdateSaveButton(); }

void USaveLoadDialogWidget::HandleClose()
{
	// «Back to menu»: back to the pause menu.
	const APlayerController* PC = GetOwningPlayer();
	if (ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr)
	{
		Hud->CloseSaveLoadDialog();
	}
	else
	{
		Close();
	}
}

#undef LOCTEXT_NAMESPACE
