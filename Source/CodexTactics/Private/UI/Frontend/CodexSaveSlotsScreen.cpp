#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

#define LOCTEXT_NAMESPACE "CodexSaveSlotsScreen"

// ---------------------------------------------------------------------------------------------------------------- entry

void UCodexSaveSlotEntry::SetSlot(const FCodexSaveSlotView& InSlot)
{
	SlotData = InSlot;
	ButtonText = FText::FromString(InSlot.Title);
	DescriptionText = FText::Format(LOCTEXT("EntryDesc", "{0} - {1} - play time {2}"), FText::FromString(InSlot.Title),
		FText::FromString(InSlot.Level), FText::FromString(InSlot.PlayTime));
	ApplySlot();
	OnSlotSet(InSlot.SlotName, InSlot.Title, InSlot.DateTime, InSlot.Level, InSlot.PlayTime);
}

void UCodexSaveSlotEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized(); // builds the default tree when empty
	if (!SlotTitleText) { SlotTitleText = Cast<UTextBlock>(GetWidgetFromName(TEXT("SlotTitleText"))); }
	if (!SlotDateText) { SlotDateText = Cast<UTextBlock>(GetWidgetFromName(TEXT("SlotDateText"))); }
	if (!SlotLevelText) { SlotLevelText = Cast<UTextBlock>(GetWidgetFromName(TEXT("SlotLevelText"))); }
	if (!SlotPlayTimeText) { SlotPlayTimeText = Cast<UTextBlock>(GetWidgetFromName(TEXT("SlotPlayTimeText"))); }
	ApplySlot();
}

void UCodexSaveSlotEntry::ApplySlot()
{
	if (SlotTitleText) { SlotTitleText->SetText(FText::FromString(SlotData.Title)); }
	if (SlotDateText) { SlotDateText->SetText(FText::FromString(SlotData.DateTime)); }
	if (SlotLevelText) { SlotLevelText->SetText(FText::FromString(SlotData.Level)); }
	if (SlotPlayTimeText) { SlotPlayTimeText->SetText(FText::FromString(SlotData.PlayTime)); }
}

void UCodexSaveSlotEntry::BuildDefaultTree(UWidgetTree& Tree) const
{
	USizeBox* Size = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("EntrySize"));
	Size->SetHeightOverride(44.f);
	Size->SetMinDesiredWidth(760.f);
	Tree.RootWidget = Size;
	UHorizontalBox* Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("EntryRow"));
	Size->AddChild(Row);
	const struct { const TCHAR* Name; float Fill; FText Placeholder; } Columns[] = {
		{ TEXT("SlotTitleText"), 0.4f, LOCTEXT("TitleCol", "Slot title") },
		{ TEXT("SlotDateText"), 0.25f, LOCTEXT("DateCol", "2026-01-01 12:00") },
		{ TEXT("SlotLevelText"), 0.23f, LOCTEXT("LevelCol", "Level") },
		{ TEXT("SlotPlayTimeText"), 0.12f, LOCTEXT("TimeCol", "--:--") },
	};
	for (const auto& Column : Columns)
	{
		UTextBlock* Text = CodexDefaultTree::MakeText(Tree, Column.Name, Column.Placeholder, 17, CodexDefaultTree::BodyColor);
		UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Text);
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
		ColumnSlot->SetPadding(FMargin(14.f, 0.f));
	}
}

// --------------------------------------------------------------------------------------------------------------- screen

UCodexSaveSlotsScreen::UCodexSaveSlotsScreen()
{
	bIsBackHandler = true;
	SlotEntryClass = UCodexSaveSlotEntry::StaticClass();
}

void UCodexSaveSlotsScreen::BindScreenWidgets()
{
	BindNamed(TitleText, TEXT("TitleText"));
	BindNamed(SlotList, TEXT("SlotList"));
	BindNamed(EmptyText, TEXT("EmptyText"));
	BindNamed(SlotNameBox, TEXT("SlotNameBox"));
	BindNamed(StatusText, TEXT("StatusText"));
	if (SlotNameBox)
	{
		SlotNameBox->OnTextChanged.AddDynamic(this, &UCodexSaveSlotsScreen::HandleSlotNameChanged);
	}
}

void UCodexSaveSlotsScreen::RefreshScreen()
{
	const bool bSave = Mode == ECodexSaveScreenMode::Save;
	if (TitleText)
	{
		TitleText->SetText(bSave ? LOCTEXT("SaveTitle", "SAVE GAME") : LOCTEXT("LoadTitle", "LOAD GAME"));
	}
	if (SlotNameBox)
	{
		SlotNameBox->SetVisibility(bSave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (UCodexMenuButton* SaveButton = FindEntryButton(TEXT("Save")))
	{
		SaveButton->SetVisibility(bSave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	RefreshSlots();
	if (bSave && GetSlotName().IsEmpty())
	{
		SetSlotName(CodexSaveBridge::SuggestSlotName(GetWorld()));
	}
	UpdateSaveButton();
}

void UCodexSaveSlotsScreen::RefreshSlots()
{
	Slots = CodexSaveBridge::ListSlots(GetWorld());
	if (SlotList)
	{
		SlotList->ClearChildren();
		const TSubclassOf<UCodexSaveSlotEntry> EntryClass = SlotEntryClass ? SlotEntryClass : TSubclassOf<UCodexSaveSlotEntry>(UCodexSaveSlotEntry::StaticClass());
		for (const FCodexSaveSlotView& View : Slots)
		{
			UCodexSaveSlotEntry* Entry = CreateWidget<UCodexSaveSlotEntry>(this, EntryClass);
			if (!Entry)
			{
				continue;
			}
			Entry->SetSlot(View);
			const FString SlotName = View.SlotName;
			Entry->OnClicked().AddWeakLambda(this, [this, SlotName]() { ChooseSlot(SlotName); });
			TWeakObjectPtr<UCodexSaveSlotEntry> WeakEntry(Entry);
			auto ShowDescription = [this, WeakEntry]()
			{
				if (DescriptionText && WeakEntry.IsValid())
				{
					DescriptionText->SetText(WeakEntry->GetDisplayedDescription());
				}
			};
			Entry->OnHovered().AddWeakLambda(this, ShowDescription);
			Entry->OnFocusReceived().AddWeakLambda(this, ShowDescription);
			SlotList->AddChild(Entry);
		}
	}
	if (EmptyText)
	{
		EmptyText->SetVisibility(Slots.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UCodexMenuButton* DeleteButton = FindEntryButton(TEXT("Delete")))
	{
		DeleteButton->SetEntryEnabled(!Slots.IsEmpty(), LOCTEXT("NothingToDelete", "No saved games."));
	}
}

void UCodexSaveSlotsScreen::ChooseSlot(const FString& SlotName)
{
	SelectedSlot = SlotName;
	if (Mode == ECodexSaveScreenMode::Save)
	{
		SetSlotName(SlotName);
		return;
	}
	UWorld* World = GetWorld();
	if (UCodexFrontendSubsystem::IsFrontendWorld(World))
	{
		if (UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(this))
		{
			Frontend->LoadSlotFromFrontend(SlotName);
		}
		return;
	}
	// In a mission: load here, close the pause menus, the world runs again (old pause flow: load closes everything).
	bool bTravelled = false;
	if (!CodexSaveBridge::LoadSlot(World, SlotName, bTravelled))
	{
		SetStatus(FText::Format(LOCTEXT("LoadFailed", "Could not load \"{0}\"."), FText::FromString(SlotName)));
		return;
	}
	if (bTravelled)
	{
		return; // the save's map is reopening
	}
	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		UI->ClearLayer(CodexUITags::Layer_GameMenu);
	}
	UGameplayStatics::SetGamePaused(World, false);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UCodexSaveSlotsScreen::PressSave()
{
	const FString Name = GetSlotName().TrimStartAndEnd();
	if (Mode != ECodexSaveScreenMode::Save || Name.IsEmpty())
	{
		return;
	}
	if (!CodexSaveBridge::GetSaveGate(GetWorld()).bAllowed)
	{
		SetStatus(CodexSaveBridge::GetSaveGate(GetWorld()).Reason);
		return;
	}
	if (!CodexSaveBridge::SlotExists(GetWorld(), Name))
	{
		SaveNow(Name);
		return;
	}
	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		TWeakObjectPtr<UCodexSaveSlotsScreen> WeakThis(this);
		UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("OverwriteTitle", "OVERWRITE SAVE"),
			FText::Format(LOCTEXT("OverwriteMessage", "Overwrite the save \"{0}\"?"), FText::FromString(Name)), [WeakThis, Name](ECodexConfirmResult Result)
			{
				if (Result == ECodexConfirmResult::Confirmed && WeakThis.IsValid())
				{
					WeakThis->SaveNow(Name);
				}
			});
	}
}

void UCodexSaveSlotsScreen::SaveNow(const FString& SlotName)
{
	const bool bExisted = CodexSaveBridge::SlotExists(GetWorld(), SlotName);
	const bool bSaved = CodexSaveBridge::SaveToSlot(GetWorld(), SlotName);
	SetStatus(!bSaved ? FText::Format(LOCTEXT("SaveFailed", "Could not save \"{0}\"."), FText::FromString(SlotName))
		: FText::Format(bExisted ? LOCTEXT("Overwritten", "Save \"{0}\" overwritten.") : LOCTEXT("Saved", "Game saved to slot \"{0}\"."), FText::FromString(SlotName)));
	RefreshSlots();
	UpdateSaveButton();
}

void UCodexSaveSlotsScreen::PressDelete()
{
	const FString Target = !SelectedSlot.IsEmpty() ? SelectedSlot : (Mode == ECodexSaveScreenMode::Save ? GetSlotName() : FString());
	if (Target.IsEmpty() || !CodexSaveBridge::SlotExists(GetWorld(), Target))
	{
		SetStatus(LOCTEXT("PickSlot", "Pick a saved game first."));
		return;
	}
	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		TWeakObjectPtr<UCodexSaveSlotsScreen> WeakThis(this);
		UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("DeleteTitle", "DELETE SAVE"),
			FText::Format(LOCTEXT("DeleteMessage", "Delete the save \"{0}\"?"), FText::FromString(Target)), [WeakThis, Target](ECodexConfirmResult Result)
			{
				if (Result != ECodexConfirmResult::Confirmed || !WeakThis.IsValid())
				{
					return;
				}
				CodexSaveBridge::DeleteSlot(WeakThis->GetWorld(), Target);
				WeakThis->SelectedSlot.Reset();
				WeakThis->SetStatus(FText::Format(LOCTEXT("Deleted", "Save \"{0}\" deleted."), FText::FromString(Target)));
				WeakThis->RefreshSlots();
				WeakThis->UpdateSaveButton();
			});
	}
}

void UCodexSaveSlotsScreen::SetSlotName(const FString& Name)
{
	if (SlotNameBox)
	{
		SlotNameBox->SetText(FText::FromString(Name));
	}
	PendingName = Name;
	UpdateSaveButton();
}

FString UCodexSaveSlotsScreen::GetSlotName() const
{
	return SlotNameBox ? SlotNameBox->GetText().ToString() : PendingName;
}

FText UCodexSaveSlotsScreen::GetSaveButtonLabel() const
{
	return CodexFrontendRules::GetSaveButtonLabel(CodexSaveBridge::SlotExists(GetWorld(), GetSlotName().TrimStartAndEnd()));
}

void UCodexSaveSlotsScreen::HandleSlotNameChanged(const FText& Text)
{
	PendingName = Text.ToString();
	UpdateSaveButton();
}

void UCodexSaveSlotsScreen::UpdateSaveButton()
{
	if (UCodexMenuButton* SaveButton = FindEntryButton(TEXT("Save")))
	{
		SaveButton->SetButtonText(GetSaveButtonLabel());
	}
}

void UCodexSaveSlotsScreen::SetStatus(const FText& Text)
{
	StatusValue = Text;
	if (StatusText)
	{
		StatusText->SetText(Text);
	}
}

void UCodexSaveSlotsScreen::OnEntryActivated(FName EntryId)
{
	if (EntryId == TEXT("Save"))
	{
		PressSave();
	}
	else if (EntryId == TEXT("Delete"))
	{
		PressDelete();
	}
}

void UCodexSaveSlotsScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotsRoot"));
	Root->SetBrushColor(CodexDefaultTree::PanelColor);
	Root->SetPadding(FMargin(110.f, 90.f));
	Root->SetHorizontalAlignment(HAlign_Left);
	Root->SetVerticalAlignment(VAlign_Fill);
	Tree.RootWidget = Root;
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SlotsColumn"));
	Root->SetContent(Column);
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), LOCTEXT("TitleDefault", "LOAD GAME"), 36, CodexDefaultTree::TitleColor, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));

	UEditableTextBox* NameBox = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SlotNameBox"));
	NameBox->SetHintText(LOCTEXT("NameHint", "Save name"));
	CodexDefaultTree::MarkVariable(NameBox);
	Column->AddChildToVerticalBox(NameBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));

	UScrollBox* List = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("SlotList"));
	CodexDefaultTree::MarkVariable(List);
	UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(List);
	ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("EmptyText"), LOCTEXT("Empty", "No saved games"), 20, CodexDefaultTree::HintColor))
		->SetPadding(FMargin(14.f, 8.f));

	UHorizontalBox* Buttons = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotButtons"));
	Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
	const struct { const TCHAR* Name; const TCHAR* Id; FText Label; FText Description; } Entries[] = {
		{ TEXT("SaveButton"), TEXT("Save"), LOCTEXT("Save", "SAVE"), LOCTEXT("SaveDesc", "Save to the named slot.") },
		{ TEXT("DeleteButton"), TEXT("Delete"), LOCTEXT("Delete", "DELETE"), LOCTEXT("DeleteDesc", "Delete the selected save.") },
		{ TEXT("BackButton"), TEXT("Back"), LOCTEXT("Back", "BACK"), LOCTEXT("BackDesc", "Return to the previous menu.") },
	};
	for (const auto& Entry : Entries)
	{
		Buttons->AddChildToHorizontalBox(MakeEntryButton(Tree, Entry.Name, Entry.Id, Entry.Label, Entry.Description))->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	}
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("DescriptionText"), FText::GetEmpty(), 18, CodexDefaultTree::BodyColor))
		->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("StatusText"), FText::GetEmpty(), 16, CodexDefaultTree::HintColor))
		->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
}

#undef LOCTEXT_NAMESPACE
