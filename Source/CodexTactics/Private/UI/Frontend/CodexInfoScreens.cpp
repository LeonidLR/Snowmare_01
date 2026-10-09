#include "UI/Frontend/CodexInfoScreens.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/Frontend/CodexMenuButton.h"

#define LOCTEXT_NAMESPACE "CodexInfoScreens"

namespace CodexInfoTree
{
	/** Full-screen dim border with a left-aligned column (the menu scene stays visible on the right). */
	UVerticalBox* MakePanel(UWidgetTree& Tree, const FText& Title)
	{
		UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelRoot"));
		Root->SetBrushColor(CodexDefaultTree::PanelColor);
		Root->SetPadding(FMargin(110.f, 90.f));
		Root->SetHorizontalAlignment(HAlign_Left);
		Root->SetVerticalAlignment(VAlign_Fill);
		Tree.RootWidget = Root;
		UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelColumn"));
		Root->SetContent(Column);
		Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), Title, 36, CodexDefaultTree::TitleColor, true))
			->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
		return Column;
	}
}

void UCodexOptionsScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UVerticalBox* Column = CodexInfoTree::MakePanel(Tree, LOCTEXT("OptionsTitle", "OPTIONS"));
	UTextBlock* Body = CodexDefaultTree::MakeText(Tree, TEXT("BodyText"),
		LOCTEXT("OptionsBody", "Graphics, audio and control settings arrive in a later update."), 20, CodexDefaultTree::BodyColor);
	Column->AddChildToVerticalBox(Body)->SetPadding(FMargin(0.f, 0.f, 0.f, 40.f));
	Column->AddChildToVerticalBox(MakeEntryButton(Tree, TEXT("BackButton"), TEXT("Back"), LOCTEXT("Back", "BACK"), LOCTEXT("BackDesc", "Return to the previous menu.")));
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("DescriptionText"), FText::GetEmpty(), 18, CodexDefaultTree::HintColor))
		->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
}

void UCodexOptionsScreen::OnEntryActivated(FName EntryId)
{
}

UCodexCreditsScreen::UCodexCreditsScreen()
{
	CreditLines = {
		LOCTEXT("Credit1", "OPERATION: COLD SILENCE"),
		LOCTEXT("Credit2", "A tactical arctic survival RPG - a tribute to Gorky 17"),
		FText::GetEmpty(),
		LOCTEXT("Credit3", "Game design, art and direction - the Cold Silence team"),
		LOCTEXT("Credit4", "Gameplay programming - with Claude (Anthropic)"),
		LOCTEXT("Credit5", "Technical direction, rendering and VFX - with Gemini (Google)"),
		FText::GetEmpty(),
		LOCTEXT("Credit6", "Made with Unreal Engine"),
	};
}

FText UCodexCreditsScreen::GetCreditsText() const
{
	TArray<FString> Lines;
	for (const FText& Line : CreditLines)
	{
		Lines.Add(Line.ToString());
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void UCodexCreditsScreen::BindScreenWidgets()
{
	BindNamed(CreditsText, TEXT("CreditsText"));
}

void UCodexCreditsScreen::RefreshScreen()
{
	if (CreditsText && !CreditLines.IsEmpty())
	{
		CreditsText->SetText(GetCreditsText());
	}
}

void UCodexCreditsScreen::OnEntryActivated(FName EntryId)
{
}

void UCodexCreditsScreen::BuildDefaultTree(UWidgetTree& Tree) const
{
	UVerticalBox* Column = CodexInfoTree::MakePanel(Tree, LOCTEXT("CreditsTitle", "CREDITS"));
	UScrollBox* Scroll = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CreditsScroll"));
	UTextBlock* Lines = CodexDefaultTree::MakeText(Tree, TEXT("CreditsText"), GetCreditsText(), 20, CodexDefaultTree::BodyColor);
	Scroll->AddChild(Lines);
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(Scroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
	Column->AddChildToVerticalBox(MakeEntryButton(Tree, TEXT("BackButton"), TEXT("Back"), LOCTEXT("Back", "BACK"), LOCTEXT("BackDesc", "Return to the previous menu.")));
}

#undef LOCTEXT_NAMESPACE
