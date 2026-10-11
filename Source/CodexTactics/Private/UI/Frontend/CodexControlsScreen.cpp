#include "UI/Frontend/CodexControlsScreen.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Frontend/CodexControlsRules.h"
#include "UI/Frontend/CodexMenuButton.h"

#define LOCTEXT_NAMESPACE "CodexControlsScreen"

namespace CodexControlsTree
{
	const FLinearColor KeyColor(0.55f, 0.8f, 1.f);
	constexpr float KeyColumnWidth = 260.f;
}

UCodexControlsScreen::UCodexControlsScreen()
{
	bIsBackHandler = true;
	bHandlesControlsHotkey = true; // F1 closes it even while the BACK button owns the keyboard focus
}

void UCodexControlsScreen::BindScreenWidgets()
{
	BindNamed(ControlsList, TEXT("ControlsList"));
}

void UCodexControlsScreen::NativeOnActivated()
{
	// The Esc pause menu has paused the world already; opened with F1 from the game this screen pauses it.
	UWorld* World = GetWorld();
	if (World && !UGameplayStatics::IsGamePaused(World))
	{
		UGameplayStatics::SetGamePaused(World, true);
		bPausedWorld = true;
	}
	Super::NativeOnActivated();
}

void UCodexControlsScreen::NativeOnDeactivated()
{
	UWorld* World = GetWorld();
	if (bPausedWorld && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
		// A focused menu widget would swallow the game keys otherwise.
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	}
	bPausedWorld = false;
	Super::NativeOnDeactivated();
}

void UCodexControlsScreen::RefreshScreen()
{
	PopulateList();
}

bool UCodexControlsScreen::NativeOnHandleBackAction()
{
	CloseScreen();
	return true;
}

void UCodexControlsScreen::PopulateList()
{
	const TArray<FCodexControlsSection> Sections = CodexControlsRules::BuildSections();
	FString Signature;
	for (const FCodexControlsSection& Section : Sections)
	{
		Signature += Section.Title;
		for (const FCodexControlsRow& Row : Section.Rows)
		{
			Signature += Row.CommandId.ToString() + Row.Keys + Row.Description;
		}
	}
	BuiltSectionTitles.Reset();
	for (const FCodexControlsSection& Section : Sections)
	{
		BuiltSectionTitles.Add(Section.Title);
	}
	BuiltRowCount = CodexControlsRules::CountRows(Sections);
	if (!ControlsList || !WidgetTree || (Signature == BuiltSignature && ControlsList->GetChildrenCount() > 0))
	{
		return;
	}
	BuiltSignature = Signature;
	ControlsList->ClearChildren();
	for (const FCodexControlsSection& Section : Sections)
	{
		UTextBlock* Header = CodexDefaultTree::MakeText(*WidgetTree, NAME_None, FText::FromString(Section.Title.ToUpper()), 24, CodexDefaultTree::TitleColor, true);
		if (UScrollBoxSlot* HeaderSlot = Cast<UScrollBoxSlot>(ControlsList->AddChild(Header)))
		{
			HeaderSlot->SetPadding(FMargin(0.f, 26.f, 0.f, 8.f));
		}
		for (const FCodexControlsRow& Row : Section.Rows)
		{
			UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			USizeBox* KeyBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			KeyBox->SetWidthOverride(CodexControlsTree::KeyColumnWidth);
			KeyBox->AddChild(CodexDefaultTree::MakeText(*WidgetTree, NAME_None, FText::FromString(Row.Keys), 18, CodexControlsTree::KeyColor, true));
			Line->AddChildToHorizontalBox(KeyBox);
			UHorizontalBoxSlot* DescriptionSlot = Line->AddChildToHorizontalBox(
				CodexDefaultTree::MakeText(*WidgetTree, NAME_None, FText::FromString(Row.Description), 18, CodexDefaultTree::BodyColor));
			DescriptionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			if (UScrollBoxSlot* LineSlot = Cast<UScrollBoxSlot>(ControlsList->AddChild(Line)))
			{
				LineSlot->SetPadding(FMargin(0.f, 3.f));
			}
		}
	}
}

void UCodexControlsScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ControlsRoot"));
	Root->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.92f));
	Root->SetPadding(FMargin(110.f, 60.f));
	Root->SetHorizontalAlignment(HAlign_Fill);
	Root->SetVerticalAlignment(VAlign_Fill);
	Tree.RootWidget = Root;
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsColumn"));
	Root->SetContent(Column);
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), LOCTEXT("Title", "CONTROLS"), 36, CodexDefaultTree::TitleColor, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("HintText"),
		LOCTEXT("Hint", "F1 or Esc closes this screen. Scroll with the mouse wheel."), 16, CodexDefaultTree::HintColor));
	UScrollBox* List = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ControlsList"));
	CodexDefaultTree::MarkVariable(List);
	UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(List);
	ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ListSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 24.f));
	Column->AddChildToVerticalBox(MakeEntryButton(Tree, TEXT("BackButton"), TEXT("Back"), LOCTEXT("Back", "BACK"), LOCTEXT("BackDesc", "Close the controls reference.")));
}

#undef LOCTEXT_NAMESPACE
