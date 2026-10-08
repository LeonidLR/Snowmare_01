#include "UI/Frontend/CodexMainMenuScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/Frontend/CodexFrontendPlayerController.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexSaveBridge.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

#define LOCTEXT_NAMESPACE "CodexMainMenuScreen"

UCodexMainMenuScreen::UCodexMainMenuScreen()
{
	bIsBackHandler = true;
}

bool UCodexMainMenuScreen::IsEntryEnabled(ECodexMainMenuEntry Entry) const
{
	const UCodexMenuButton* Button = FindEntryButton(CodexFrontendRules::GetEntryId(Entry));
	return Button && Button->GetIsEnabled();
}

void UCodexMainMenuScreen::RefreshScreen()
{
	const bool bHasSaves = CodexSaveBridge::HasAnySave(GetWorld());
	if (UCodexMenuButton* Continue = FindEntryButton(CodexFrontendRules::GetEntryId(ECodexMainMenuEntry::Continue)))
	{
		Continue->SetEntryEnabled(CodexFrontendRules::IsEntryEnabled(ECodexMainMenuEntry::Continue, bHasSaves), LOCTEXT("NoSave", "No saved game yet."));
		const TArray<FCodexSaveSlotView> Slots = bHasSaves ? CodexSaveBridge::ListSlots(GetWorld()) : TArray<FCodexSaveSlotView>();
		if (!Slots.IsEmpty())
		{
			Continue->SetDescriptionText(FText::Format(LOCTEXT("ContinueLatest", "Resume from your latest save: {0} ({1})."),
				FText::FromString(Slots[0].Title), FText::FromString(Slots[0].DateTime)));
		}
	}
}

void UCodexMainMenuScreen::OnEntrySelected(UCodexMenuButton& Button)
{
	Super::OnEntrySelected(Button);
	if (ACodexFrontendPlayerController* PC = Cast<ACodexFrontendPlayerController>(GetOwningPlayer()))
	{
		PC->FocusCameraAnchor(Button.GetCameraAnchorId());
	}
}

void UCodexMainMenuScreen::OnEntryActivated(FName EntryId)
{
	ECodexMainMenuEntry Entry;
	if (!CodexFrontendRules::ParseEntryId(EntryId, Entry))
	{
		return;
	}
	SelectEntry(EntryId);
	UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(this);
	UCodexUISubsystem* UI = UCodexUISubsystem::Get(this);
	switch (Entry)
	{
	case ECodexMainMenuEntry::Continue:
		if (Frontend)
		{
			Frontend->ContinueLatest();
		}
		break;
	case ECodexMainMenuEntry::NewGame:
		if (Frontend)
		{
			Frontend->StartNewGame();
		}
		break;
	case ECodexMainMenuEntry::LoadGame:
		if (UI)
		{
			UI->PushScreen(CodexUITags::Layer_Menu, CodexUITags::Screen_SaveSlots, [](UCodexActivatableScreen& Screen)
			{
				if (UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(&Screen))
				{
					Slots->SetMode(ECodexSaveScreenMode::Load);
				}
			});
		}
		break;
	case ECodexMainMenuEntry::Options:
		if (UI)
		{
			UI->PushScreen(CodexUITags::Layer_Menu, CodexUITags::Screen_Options);
		}
		break;
	case ECodexMainMenuEntry::Credits:
		if (UI)
		{
			UI->PushScreen(CodexUITags::Layer_Menu, CodexUITags::Screen_Credits);
		}
		break;
	case ECodexMainMenuEntry::Quit:
		if (UI)
		{
			TWeakObjectPtr<UCodexFrontendSubsystem> WeakFrontend(Frontend);
			UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("QuitTitle", "QUIT GAME"), LOCTEXT("QuitMessage", "Quit to the desktop?"),
				[WeakFrontend](ECodexConfirmResult Result)
				{
					if (Result == ECodexConfirmResult::Confirmed && WeakFrontend.IsValid())
					{
						WeakFrontend->QuitGame();
					}
				});
		}
		break;
	}
}

bool UCodexMainMenuScreen::NativeOnHandleBackAction()
{
	// Back: return to the title screen («PRESS ANY KEY»).
	if (ACodexFrontendPlayerController* PC = Cast<ACodexFrontendPlayerController>(GetOwningPlayer()))
	{
		PC->ShowTitle();
		return true;
	}
	return false;
}

void UCodexMainMenuScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UOverlay* Root = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MenuRoot"));
	Tree.RootWidget = Root;

	// Left column: game title + entries.
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EntryColumn"));
	UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column);
	ColumnSlot->SetHorizontalAlignment(HAlign_Left);
	ColumnSlot->SetVerticalAlignment(VAlign_Center);
	ColumnSlot->SetPadding(FMargin(110.f, 0.f, 0.f, 0.f));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("GameTitleText"), LOCTEXT("GameTitle", "COLD SILENCE"), 40,
		CodexDefaultTree::TitleColor, true))->SetPadding(FMargin(18.f, 0.f, 0.f, 40.f));
	struct FEntry
	{
		const TCHAR* WidgetName;
		ECodexMainMenuEntry Entry;
		FText Label;
		FText Description;
	};
	const FEntry Entries[] = {
		{ TEXT("ContinueButton"), ECodexMainMenuEntry::Continue, LOCTEXT("Continue", "CONTINUE"), LOCTEXT("ContinueDesc", "Resume from your latest save.") },
		{ TEXT("NewGameButton"), ECodexMainMenuEntry::NewGame, LOCTEXT("NewGame", "NEW GAME"), LOCTEXT("NewGameDesc", "Start the operation from the beginning.") },
		{ TEXT("LoadGameButton"), ECodexMainMenuEntry::LoadGame, LOCTEXT("LoadGame", "LOAD GAME"), LOCTEXT("LoadGameDesc", "Pick a saved game to load.") },
		{ TEXT("OptionsButton"), ECodexMainMenuEntry::Options, LOCTEXT("Options", "OPTIONS"), LOCTEXT("OptionsDesc", "Graphics, audio and controls.") },
		{ TEXT("CreditsButton"), ECodexMainMenuEntry::Credits, LOCTEXT("Credits", "CREDITS"), LOCTEXT("CreditsDesc", "The people behind Cold Silence.") },
		{ TEXT("QuitButton"), ECodexMainMenuEntry::Quit, LOCTEXT("Quit", "QUIT"), LOCTEXT("QuitDesc", "Leave the game.") },
	};
	for (const FEntry& Entry : Entries)
	{
		UCodexMenuButton* Button = MakeEntryButton(Tree, Entry.WidgetName, CodexFrontendRules::GetEntryId(Entry.Entry), Entry.Label, Entry.Description);
		Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	// Bottom-left description line, bottom-right back hint.
	UTextBlock* Description = CodexDefaultTree::MakeText(Tree, TEXT("DescriptionText"), FText::GetEmpty(), 18, CodexDefaultTree::BodyColor);
	UOverlaySlot* DescriptionSlot = Root->AddChildToOverlay(Description);
	DescriptionSlot->SetHorizontalAlignment(HAlign_Left);
	DescriptionSlot->SetVerticalAlignment(VAlign_Bottom);
	DescriptionSlot->SetPadding(FMargin(128.f, 0.f, 0.f, 64.f));
	UTextBlock* BackHint = CodexDefaultTree::MakeText(Tree, TEXT("BackHintText"), LOCTEXT("BackHint", "[Esc]  BACK"), 18, CodexDefaultTree::HintColor);
	UOverlaySlot* HintSlot = Root->AddChildToOverlay(BackHint);
	HintSlot->SetHorizontalAlignment(HAlign_Right);
	HintSlot->SetVerticalAlignment(VAlign_Bottom);
	HintSlot->SetPadding(FMargin(0.f, 0.f, 96.f, 64.f));
}

#undef LOCTEXT_NAMESPACE
