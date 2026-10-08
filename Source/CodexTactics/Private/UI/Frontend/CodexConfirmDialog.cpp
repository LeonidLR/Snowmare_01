#include "UI/Frontend/CodexConfirmDialog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/Frontend/CodexMenuButton.h"

#define LOCTEXT_NAMESPACE "CodexConfirmDialog"

UCodexConfirmDialog::UCodexConfirmDialog()
{
	bIsBackHandler = true;
	bIsModal = true;
}

void UCodexConfirmDialog::Setup(ECodexConfirmType InType, const FText& InTitle, const FText& InMessage, TFunction<void(ECodexConfirmResult)> InOnResult)
{
	Type = InType;
	TitleValue = InTitle;
	MessageValue = InMessage;
	OnResult = MoveTemp(InOnResult);
	bFinished = false;
	DesiredFocusName = Type == ECodexConfirmType::Ok ? FName(TEXT("OkButton")) : FName(TEXT("NoButton"));
	RefreshScreen();
}

void UCodexConfirmDialog::BindScreenWidgets()
{
	BindNamed(TitleText, TEXT("TitleText"));
	BindNamed(MessageText, TEXT("MessageText"));
}

void UCodexConfirmDialog::RefreshScreen()
{
	if (TitleText)
	{
		TitleText->SetText(TitleValue);
	}
	if (MessageText)
	{
		MessageText->SetText(MessageValue);
	}
	const bool bOk = Type == ECodexConfirmType::Ok;
	const ESlateVisibility YesNo = bOk ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (UCodexMenuButton* Yes = FindEntryButton(TEXT("Yes"))) { Yes->SetVisibility(YesNo); }
	if (UCodexMenuButton* No = FindEntryButton(TEXT("No"))) { No->SetVisibility(YesNo); }
	if (UCodexMenuButton* Ok = FindEntryButton(TEXT("Ok"))) { Ok->SetVisibility(bOk ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void UCodexConfirmDialog::OnEntryActivated(FName EntryId)
{
	if (EntryId == TEXT("Yes") || EntryId == TEXT("Ok"))
	{
		Confirm();
	}
	else if (EntryId == TEXT("No"))
	{
		Cancel();
	}
}

bool UCodexConfirmDialog::NativeOnHandleBackAction()
{
	Cancel();
	return true;
}

void UCodexConfirmDialog::Finish(ECodexConfirmResult Result)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	// Close first: the callback may push another screen or open a level.
	TFunction<void(ECodexConfirmResult)> Callback = MoveTemp(OnResult);
	CloseScreen();
	if (Callback)
	{
		Callback(Result);
	}
}

void UCodexConfirmDialog::BuildDefaultTree(UWidgetTree& Tree) const
{
	UOverlay* Root = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("DialogRoot"));
	Tree.RootWidget = Root;

	// Blurred, dimmed background over the whole screen (it also swallows clicks outside the panel).
	UBackgroundBlur* Blur = Tree.ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(), TEXT("BackgroundBlur"));
	Blur->SetBlurStrength(8.f);
	CodexDefaultTree::MarkVariable(Blur);
	UOverlaySlot* BlurSlot = Root->AddChildToOverlay(Blur);
	BlurSlot->SetHorizontalAlignment(HAlign_Fill);
	BlurSlot->SetVerticalAlignment(VAlign_Fill);
	UBorder* Dim = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DimBorder"));
	Dim->SetBrushColor(CodexDefaultTree::DimColor);
	CodexDefaultTree::MarkVariable(Dim);
	Blur->SetContent(Dim);

	UBorder* Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogPanel"));
	Panel->SetBrushColor(FLinearColor(0.03f, 0.045f, 0.07f, 0.96f));
	Panel->SetPadding(FMargin(40.f, 32.f));
	UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel);
	PanelSlot->SetHorizontalAlignment(HAlign_Center);
	PanelSlot->SetVerticalAlignment(VAlign_Center);
	USizeBox* Width = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialogWidth"));
	Width->SetWidthOverride(620.f);
	Panel->SetContent(Width);
	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogColumn"));
	Width->AddChild(Column);
	Column->AddChildToVerticalBox(CodexDefaultTree::MakeText(Tree, TEXT("TitleText"), LOCTEXT("Title", "CONFIRM"), 28, CodexDefaultTree::TitleColor, true))
		->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	UTextBlock* Message = CodexDefaultTree::MakeText(Tree, TEXT("MessageText"), LOCTEXT("Message", "Are you sure?"), 20, CodexDefaultTree::BodyColor);
	Message->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(Message)->SetPadding(FMargin(0.f, 0.f, 0.f, 28.f));
	UHorizontalBox* Buttons = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DialogButtons"));
	Column->AddChildToVerticalBox(Buttons)->SetHorizontalAlignment(HAlign_Right);
	const struct { const TCHAR* Name; const TCHAR* Id; FText Label; } Entries[] = {
		{ TEXT("YesButton"), TEXT("Yes"), LOCTEXT("Yes", "YES") },
		{ TEXT("NoButton"), TEXT("No"), LOCTEXT("No", "NO") },
		{ TEXT("OkButton"), TEXT("Ok"), LOCTEXT("Ok", "OK") },
	};
	for (const auto& Entry : Entries)
	{
		UCodexMenuButton* Button = MakeEntryButton(Tree, Entry.Name, Entry.Id, Entry.Label, FText::GetEmpty());
		Buttons->AddChildToHorizontalBox(Button)->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
	}
}

#undef LOCTEXT_NAMESPACE
