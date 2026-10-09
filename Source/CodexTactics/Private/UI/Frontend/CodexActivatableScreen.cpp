#include "UI/Frontend/CodexActivatableScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexUISubsystem.h"

UCodexActivatableScreen::UCodexActivatableScreen()
{
	bIsBackHandler = true;
	bSupportsActivationFocus = true;
	EntryButtonClass = UCodexMenuButton::StaticClass();
}

void UCodexActivatableScreen::CloseScreen()
{
	if (IsActivated())
	{
		DeactivateWidget(); // the stack removes a deactivated top screen
	}
	else if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		UI->RemoveScreen(*this); // not the top one (e.g. under a dialog): leave the stack directly
	}
}

bool UCodexActivatableScreen::RequestBack()
{
	return bIsBackHandler && NativeOnHandleBackAction();
}

void UCodexActivatableScreen::SelectEntry(FName EntryId)
{
	if (UCodexMenuButton* Button = FindEntryButton(EntryId))
	{
		SelectedEntryId = EntryId;
		// The selected entry keeps the style's "selected" look (mouse moved away / keyboard / gamepad / scripted selection).
		for (UCodexMenuButton* Each : EntryButtons)
		{
			if (Each)
			{
				if (Each == Button)
				{
					Each->SetIsSelected(true, /*bGiveClickFeedback*/ false);
				}
				else if (Each->GetSelected())
				{
					Each->ClearSelection(); // SetIsSelected(false) ignores non-toggleable buttons
				}
			}
		}
		OnEntrySelected(*Button);
		BP_OnEntrySelected(EntryId);
	}
}

void UCodexActivatableScreen::ActivateEntry(FName EntryId)
{
	const UCodexMenuButton* Button = FindEntryButton(EntryId);
	if (!Button || !Button->GetIsEnabled())
	{
		return;
	}
	if (EntryId == TEXT("Back"))
	{
		RequestBack(); // every screen's BACK button = the back action
		return;
	}
	OnEntryActivated(EntryId);
}

UCodexMenuButton* UCodexActivatableScreen::FindEntryButton(FName EntryId) const
{
	for (UCodexMenuButton* Button : EntryButtons)
	{
		if (Button && Button->GetEntryId() == EntryId)
		{
			return Button;
		}
	}
	return nullptr;
}

FText UCodexActivatableScreen::GetDescriptionLine() const
{
	return DescriptionText ? DescriptionText->GetText() : FText();
}

void UCodexActivatableScreen::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree(*WidgetTree);
	}
	Super::NativeOnInitialized();
	BindNamed(DescriptionText, TEXT("DescriptionText"));
	BindScreenWidgets();
	HookEntryButtons();
}

void UCodexActivatableScreen::NativeOnActivated()
{
	Super::NativeOnActivated();
	RefreshScreen();
	if (!SelectedEntryId.IsNone())
	{
		SelectEntry(SelectedEntryId); // back from a sub-screen: description / camera of the last entry again
	}
}

UWidget* UCodexActivatableScreen::NativeGetDesiredFocusTarget() const
{
	if (!DesiredFocusName.IsNone())
	{
		if (UWidget* Named = GetWidgetFromName(DesiredFocusName))
		{
			return Named;
		}
	}
	if (UCodexMenuButton* Selected = FindEntryButton(SelectedEntryId); Selected && Selected->GetIsEnabled())
	{
		return Selected;
	}
	for (UCodexMenuButton* Button : EntryButtons)
	{
		if (Button && Button->GetIsEnabled() && Button->IsVisible())
		{
			return Button;
		}
	}
	return Super::NativeGetDesiredFocusTarget();
}

void UCodexActivatableScreen::OnEntrySelected(UCodexMenuButton& Button)
{
	if (DescriptionText)
	{
		DescriptionText->SetText(Button.GetDisplayedDescription());
	}
}

UCodexMenuButton* UCodexActivatableScreen::MakeEntryButton(UWidgetTree& Tree, FName WidgetName, FName EntryId, const FText& Label, const FText& Description) const
{
	const TSubclassOf<UCodexMenuButton> ButtonClass = EntryButtonClass ? EntryButtonClass : TSubclassOf<UCodexMenuButton>(UCodexMenuButton::StaticClass());
	UCodexMenuButton* Button = Tree.ConstructWidget<UCodexMenuButton>(ButtonClass, WidgetName);
	Button->ButtonText = Label;
	Button->DescriptionText = Description;
	Button->EntryId = EntryId;
	CodexDefaultTree::MarkVariable(Button);
	return Button;
}

void UCodexActivatableScreen::HookEntryButtons()
{
	EntryButtons.Reset();
	if (!WidgetTree)
	{
		return;
	}
	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		UCodexMenuButton* Button = Cast<UCodexMenuButton>(Widget);
		if (!Button || Button->GetEntryId().IsNone())
		{
			return;
		}
		EntryButtons.Add(Button);
		Button->SetIsSelectable(true);
		Button->SetIsInteractableWhenSelected(true); // a selected entry still activates on click / Enter
		const FName Id = Button->GetEntryId();
		Button->OnHovered().AddWeakLambda(this, [this, Id]() { SelectEntry(Id); });
		Button->OnFocusReceived().AddWeakLambda(this, [this, Id]() { SelectEntry(Id); });
		Button->OnClicked().AddWeakLambda(this, [this, Id]() { ActivateEntry(Id); });
	});
}
