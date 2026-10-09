#include "UI/Frontend/CodexTitleScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/Frontend/CodexFrontendPlayerController.h"

#define LOCTEXT_NAMESPACE "CodexTitleScreen"

UCodexTitleScreen::UCodexTitleScreen()
{
	bIsBackHandler = false;
	SetIsFocusable(true);
}

void UCodexTitleScreen::ContinueFromTitle()
{
	if (bLeaving)
	{
		return;
	}
	bLeaving = true;
	if (ACodexFrontendPlayerController* PC = Cast<ACodexFrontendPlayerController>(GetOwningPlayer()))
	{
		PC->ShowMainMenu();
	}
	else
	{
		CloseScreen();
	}
}

void UCodexTitleScreen::NativeOnActivated()
{
	bLeaving = false; // pooled instance shown again (back from the main menu)
	Super::NativeOnActivated();
}

UWidget* UCodexTitleScreen::NativeGetDesiredFocusTarget() const
{
	// The screen itself takes the keyboard / gamepad focus so any key reaches NativeOnKeyDown.
	return const_cast<UCodexTitleScreen*>(this);
}

FReply UCodexTitleScreen::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	ContinueFromTitle();
	return FReply::Handled();
}

FReply UCodexTitleScreen::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	ContinueFromTitle();
	return FReply::Handled();
}

void UCodexTitleScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	// A transparent, hit-testable border over the whole screen (catches the click); texts at the bottom centre.
	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TitleRoot"));
	Root->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	Root->SetHorizontalAlignment(HAlign_Center);
	Root->SetVerticalAlignment(VAlign_Bottom);
	Root->SetPadding(FMargin(0.f, 0.f, 0.f, 120.f));
	Tree.RootWidget = Root;
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TitleColumn"));
	Root->SetContent(Column);
	UTextBlock* Title = CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), LOCTEXT("Title", "OPERATION: COLD SILENCE"), 48, CodexDefaultTree::TitleColor, true);
	Title->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.f, 0.f, 0.f, 36.f));
	UTextBlock* Press = CodexDefaultTree::MakeText(Tree, TEXT("PressAnyKeyText"), LOCTEXT("PressAnyKey", "PRESS ANY KEY"), 22, CodexDefaultTree::BodyColor);
	Press->SetJustification(ETextJustify::Center);
	Column->AddChildToVerticalBox(Press)->SetHorizontalAlignment(HAlign_Center);
}

#undef LOCTEXT_NAMESPACE
