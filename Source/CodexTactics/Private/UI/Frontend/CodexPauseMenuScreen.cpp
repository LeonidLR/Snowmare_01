#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexSaveBridge.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

#define LOCTEXT_NAMESPACE "CodexPauseMenuScreen"

UCodexPauseMenuScreen::UCodexPauseMenuScreen()
{
	bIsBackHandler = true;
}

void UCodexPauseMenuScreen::Resume()
{
	UWorld* World = GetWorld();
	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		UI->ClearLayer(CodexUITags::Layer_GameMenu);
	}
	if (World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	// A clicked menu button keeps the keyboard focus otherwise and swallows the game keys (1-4, Z / C / V, Space...).
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

bool UCodexPauseMenuScreen::IsSaveEnabled() const
{
	const UCodexMenuButton* Button = FindEntryButton(TEXT("Save"));
	return Button && Button->GetIsEnabled();
}

FText UCodexPauseMenuScreen::GetSaveHint() const
{
	const UCodexMenuButton* Button = FindEntryButton(TEXT("Save"));
	return Button && !Button->GetIsEnabled() ? Button->GetDisplayedDescription() : FText();
}

bool UCodexPauseMenuScreen::IsLoadEnabled() const
{
	const UCodexMenuButton* Button = FindEntryButton(TEXT("Load"));
	return Button && Button->GetIsEnabled();
}

FText UCodexPauseMenuScreen::GetStatusLine() const
{
	const TArray<FCodexSaveSlotView> Slots = CodexSaveBridge::ListSlots(GetWorld());
	if (Slots.IsEmpty())
	{
		return LOCTEXT("NoSaves", "No saved games");
	}
	return FText::Format(LOCTEXT("Latest", "Latest save: {0} ({1})"), FText::FromString(Slots[0].Title), FText::FromString(Slots[0].DateTime));
}

void UCodexPauseMenuScreen::BindScreenWidgets()
{
	BindNamed(StatusText, TEXT("StatusText"));
}

void UCodexPauseMenuScreen::NativeOnActivated()
{
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, true);
	}
	Super::NativeOnActivated();
}

void UCodexPauseMenuScreen::RefreshScreen()
{
	const FCodexSaveGate Gate = CodexSaveBridge::GetSaveGate(GetWorld());
	if (UCodexMenuButton* Save = FindEntryButton(TEXT("Save")))
	{
		Save->SetEntryEnabled(Gate.bAllowed, Gate.Reason);
	}
	if (UCodexMenuButton* Load = FindEntryButton(TEXT("Load")))
	{
		Load->SetEntryEnabled(CodexSaveBridge::HasAnySave(GetWorld()), LOCTEXT("NoLoad", "No saved games yet."));
	}
	if (StatusText)
	{
		StatusText->SetText(GetStatusLine());
	}
}

void UCodexPauseMenuScreen::OnEntryActivated(FName EntryId)
{
	UCodexUISubsystem* UI = UCodexUISubsystem::Get(this);
	if (EntryId == TEXT("Resume"))
	{
		Resume();
	}
	else if ((EntryId == TEXT("Save") || EntryId == TEXT("Load")) && UI)
	{
		const ECodexSaveScreenMode Mode = EntryId == TEXT("Save") ? ECodexSaveScreenMode::Save : ECodexSaveScreenMode::Load;
		UI->PushScreen(CodexUITags::Layer_GameMenu, CodexUITags::Screen_SaveSlots, [Mode](UCodexActivatableScreen& Screen)
		{
			if (UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(&Screen))
			{
				Slots->SetMode(Mode);
			}
		});
	}
	else if (EntryId == TEXT("Options") && UI)
	{
		UI->PushScreen(CodexUITags::Layer_GameMenu, CodexUITags::Screen_Options);
	}
	else if (EntryId == TEXT("QuitToMenu") && UI)
	{
		TWeakObjectPtr<UCodexFrontendSubsystem> Frontend(UCodexFrontendSubsystem::Get(this));
		UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("ToMenuTitle", "QUIT TO MAIN MENU"),
			LOCTEXT("ToMenuMessage", "Return to the main menu? Progress since your last save will be lost."), [Frontend](ECodexConfirmResult Result)
			{
				if (Result == ECodexConfirmResult::Confirmed && Frontend.IsValid())
				{
					Frontend->QuitToMainMenu();
				}
			});
	}
	else if (EntryId == TEXT("QuitGame") && UI)
	{
		TWeakObjectPtr<UCodexFrontendSubsystem> Frontend(UCodexFrontendSubsystem::Get(this));
		UI->ShowConfirm(ECodexConfirmType::YesNo, LOCTEXT("QuitTitle", "QUIT GAME"),
			LOCTEXT("QuitMessage", "Quit to the desktop? Progress since your last save will be lost."), [Frontend](ECodexConfirmResult Result)
			{
				if (Result == ECodexConfirmResult::Confirmed && Frontend.IsValid())
				{
					Frontend->QuitGame();
				}
			});
	}
}

bool UCodexPauseMenuScreen::NativeOnHandleBackAction()
{
	Resume();
	return true;
}

void UCodexPauseMenuScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseRoot"));
	Root->SetBrushColor(CodexDefaultTree::PanelColor);
	Root->SetPadding(FMargin(110.f, 0.f, 0.f, 0.f));
	Root->SetHorizontalAlignment(HAlign_Left);
	Root->SetVerticalAlignment(VAlign_Center);
	Tree.RootWidget = Root;
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Root->SetContent(Column);
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), LOCTEXT("Title", "PAUSED"), 40, CodexDefaultTree::TitleColor, true))
		->SetPadding(FMargin(18.f, 0.f, 0.f, 36.f));
	const struct { const TCHAR* Name; const TCHAR* Id; FText Label; FText Description; } Entries[] = {
		{ TEXT("ResumeButton"), TEXT("Resume"), LOCTEXT("Resume", "RESUME"), LOCTEXT("ResumeDesc", "Back to the mission.") },
		{ TEXT("SaveButton"), TEXT("Save"), LOCTEXT("Save", "SAVE GAME"), LOCTEXT("SaveDesc", "Save your progress to a slot.") },
		{ TEXT("LoadButton"), TEXT("Load"), LOCTEXT("Load", "LOAD GAME"), LOCTEXT("LoadDesc", "Load a saved game.") },
		{ TEXT("OptionsButton"), TEXT("Options"), LOCTEXT("Options", "OPTIONS"), LOCTEXT("OptionsDesc", "Graphics, audio and controls.") },
		{ TEXT("QuitToMenuButton"), TEXT("QuitToMenu"), LOCTEXT("QuitToMenu", "QUIT TO MAIN MENU"), LOCTEXT("QuitToMenuDesc", "Leave the mission for the main menu.") },
		{ TEXT("QuitGameButton"), TEXT("QuitGame"), LOCTEXT("QuitGame", "QUIT GAME"), LOCTEXT("QuitGameDesc", "Quit to the desktop.") },
	};
	for (const auto& Entry : Entries)
	{
		Column->AddChildToVerticalBox(MakeEntryButton(Tree, Entry.Name, Entry.Id, Entry.Label, Entry.Description))->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("DescriptionText"), FText::GetEmpty(), 18, CodexDefaultTree::BodyColor))
		->SetPadding(FMargin(18.f, 28.f, 0.f, 0.f));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("StatusText"), FText::GetEmpty(), 16, CodexDefaultTree::HintColor))
		->SetPadding(FMargin(18.f, 8.f, 0.f, 0.f));
}

#undef LOCTEXT_NAMESPACE
