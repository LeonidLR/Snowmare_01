#include "UI/QuantitySplitDialogWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "UI/CodexButtonFocus.h"
#include "UI/CodexTacticsHUD.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "QuantitySplitDialogWidget"

namespace
{
	// The purple of the retired Godot transfer_dialog.gd: bg (0.08, 0.06, 0.12, 0.96), border (0.65, 0.18, 0.75).
	const FLinearColor SplitBack = ACodexTacticsHUD::GodotColor(0.08f, 0.06f, 0.12f, 0.96f);
	const FLinearColor SplitBorder = ACodexTacticsHUD::GodotColor(0.65f, 0.18f, 0.75f);
	const FLinearColor SplitTitle = ACodexTacticsHUD::GodotColor(0.85f, 0.45f, 1.f);
	const FLinearColor SplitButton = ACodexTacticsHUD::GodotColor(0.2f, 0.22f, 0.27f);
	const FLinearColor SplitConfirm = ACodexTacticsHUD::GodotColor(0.12f, 0.55f, 0.28f);
	const FLinearColor SplitCancel = ACodexTacticsHUD::GodotColor(0.6f, 0.18f, 0.18f);
	const FLinearColor SplitText(0.95f, 0.95f, 0.95f);

	/** A focused HUD control would keep the keyboard focus and swallow the game keys (see CodexButtonFocus). */
	void SplitDisableFocus(UWidget* Widget)
	{
		if (const FBoolProperty* Property = Widget ? CastField<FBoolProperty>(Widget->GetClass()->FindPropertyByName(TEXT("IsFocusable"))) : nullptr)
		{
			Property->SetPropertyValue_InContainer(Widget, false);
		}
	}

	FText SplitClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}
}

void UQuantitySplitDialogWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SplitRoot"));
	WidgetTree->RootWidget = Root;

	// Centre of the screen, a little above the middle (clear of the drawer and the action bar).
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("QuantitySplitDialog"));
	Frame->SetBrushColor(SplitBorder);
	Frame->SetPadding(FMargin(2.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetPosition(FVector2D(0.f, -60.f));
	FrameSlot->SetAutoSize(true);
	Panel = Frame;

	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Width->SetWidthOverride(400.f);
	Frame->SetContent(Width);
	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SplitBody"));
	Body->SetBrushColor(SplitBack);
	Body->SetPadding(FMargin(14.f, 10.f));
	Width->AddChild(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Body->SetContent(Column);

	auto MakeText = [this](int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		return Text;
	};
	auto MakeButton = [this, &MakeText](const FName& Name, const FText& Label, const FLinearColor& Color, float ButtonWidth)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		CodexButtonFocus::Disable(Button);
		Button->SetBackgroundColor(Color);
		UTextBlock* Text = MakeText(11, SplitText);
		Text->SetText(Label);
		Button->AddChild(Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(ButtonWidth);
		Size->SetHeightOverride(30.f);
		Size->AddChild(Button);
		return TPair<UButton*, USizeBox*>(Button, Size);
	};

	TitleText = MakeText(13, SplitTitle);
	TitleText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	QuantityText = MakeText(16, SplitText);
	Column->AddChildToVerticalBox(QuantityText)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	UHorizontalBox* SliderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(SliderRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	const TPair<UButton*, USizeBox*> Minus = MakeButton(TEXT("BtnSplitMinus"), LOCTEXT("Minus", "-"), SplitButton, 36.f);
	SliderRow->AddChildToHorizontalBox(Minus.Value)->SetVerticalAlignment(VAlign_Center);
	QuantitySlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("SplitSlider"));
	SplitDisableFocus(QuantitySlider);
	QuantitySlider->SetSliderBarColor(SplitButton);
	QuantitySlider->SetSliderHandleColor(SplitTitle);
	UHorizontalBoxSlot* SliderSlot = SliderRow->AddChildToHorizontalBox(QuantitySlider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	SliderSlot->SetPadding(FMargin(8.f, 0.f));
	const TPair<UButton*, USizeBox*> Plus = MakeButton(TEXT("BtnSplitPlus"), LOCTEXT("Plus", "+"), SplitButton, 36.f);
	SliderRow->AddChildToHorizontalBox(Plus.Value)->SetVerticalAlignment(VAlign_Center);

	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(ButtonRow)->SetHorizontalAlignment(HAlign_Center);
	const TPair<UButton*, USizeBox*> All = MakeButton(TEXT("BtnSplitAll"), LOCTEXT("All", "ALL"), SplitButton, 80.f);
	const TPair<UButton*, USizeBox*> Confirm = MakeButton(TEXT("BtnSplitConfirm"), LOCTEXT("Confirm", "CONFIRM"), SplitConfirm, 130.f);
	const TPair<UButton*, USizeBox*> Cancel = MakeButton(TEXT("BtnSplitCancel"), LOCTEXT("Cancel", "CANCEL"), SplitCancel, 100.f);
	for (USizeBox* Size : { All.Value, Confirm.Value, Cancel.Value })
	{
		ButtonRow->AddChildToHorizontalBox(Size)->SetPadding(FMargin(4.f, 0.f));
	}

	Minus.Key->OnClicked.AddDynamic(this, &UQuantitySplitDialogWidget::HandleMinus);
	Plus.Key->OnClicked.AddDynamic(this, &UQuantitySplitDialogWidget::HandlePlus);
	All.Key->OnClicked.AddDynamic(this, &UQuantitySplitDialogWidget::HandleAll);
	Confirm.Key->OnClicked.AddDynamic(this, &UQuantitySplitDialogWidget::HandleConfirm);
	Cancel.Key->OnClicked.AddDynamic(this, &UQuantitySplitDialogWidget::HandleCancel);
	QuantitySlider->OnValueChanged.AddDynamic(this, &UQuantitySplitDialogWidget::HandleSlider);
}

void UQuantitySplitDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UQuantitySplitDialogWidget::OpenFor(AOperativeCharacter* InSender, AOperativeCharacter* InRecipient, ETransferItem InItem, int32 InMaxQuantity)
{
	OpenForRequest(FTransferRequest::MakeGive(InSender, InRecipient, InItem), InMaxQuantity);
}

void UQuantitySplitDialogWidget::OpenForRequest(const FTransferRequest& InRequest, int32 InMaxQuantity)
{
	Request = InRequest;
	const ETransferItem Item = Request.Item;
	MaxQuantity = FMath::Max(0, InMaxQuantity);
	Quantity = MaxQuantity;
	if (QuantitySlider)
	{
		QuantitySlider->SetMinValue(static_cast<float>(TransferRules::GetMinQuantity(Item, MaxQuantity)));
		QuantitySlider->SetMaxValue(static_cast<float>(FMath::Max(MaxQuantity, 1)));
		QuantitySlider->SetStepSize(static_cast<float>(TransferRules::GetItemQuantityStep(Item)));
	}
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Visible);
	}
	RefreshTexts();
}

void UQuantitySplitDialogWidget::Close()
{
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UQuantitySplitDialogWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UQuantitySplitDialogWidget::SetQuantity(int32 Value)
{
	Quantity = TransferRules::QuantizeQuantity(Request.Item, Value, MaxQuantity);
	RefreshTexts();
}

void UQuantitySplitDialogWidget::Increment()
{
	Quantity = TransferRules::StepQuantity(Request.Item, Quantity, 1, MaxQuantity);
	RefreshTexts();
}

void UQuantitySplitDialogWidget::Decrement()
{
	Quantity = TransferRules::StepQuantity(Request.Item, Quantity, -1, MaxQuantity);
	RefreshTexts();
}

void UQuantitySplitDialogWidget::SelectAll()
{
	Quantity = MaxQuantity;
	RefreshTexts();
}

ETransferRequestOutcome UQuantitySplitDialogWidget::Confirm()
{
	if (!IsOpen())
	{
		return ETransferRequestOutcome::Failed;
	}
	Close();
	USquadTransferSubsystem* Transfer = GetWorld() ? GetWorld()->GetSubsystem<USquadTransferSubsystem>() : nullptr;
	if (!Request.Operative.IsValid() || !Transfer)
	{
		return ETransferRequestOutcome::Failed;
	}
	// The stock may have changed while the dialog was open (fire, another hand-over): never ask for more than there is.
	FTransferRequest Final = Request;
	Final.Quantity = FMath::Min(Quantity, Transfer->GetMaxQuantity(Request));
	return Transfer->Request(Final);
}

void UQuantitySplitDialogWidget::Cancel()
{
	Close();
}

FText UQuantitySplitDialogWidget::GetTitleText() const
{
	const FString Name = TransferRules::GetItemName(Request.Item);
	switch (Request.Action)
	{
	case ETransferAction::DropToGround: return SplitClean(FString::Printf(TEXT("DROP ON GROUND: %s"), *Name));
	case ETransferAction::Store: return SplitClean(FString::Printf(TEXT("STORE IN CRATE: %s"), *Name));
	case ETransferAction::Take:
		return SplitClean(FString::Printf(TEXT("TAKE: %s -> %s"), *Name,
			Request.Operative.IsValid() ? *Request.Operative->DisplayName.ToString() : TEXT("?")));
	default:
		return SplitClean(FString::Printf(TEXT("GIVE: %s -> %s"), *Name,
			Request.Recipient.IsValid() ? *Request.Recipient->DisplayName.ToString() : TEXT("?")));
	}
}

FText UQuantitySplitDialogWidget::GetQuantityText() const
{
	return SplitClean(FString::Printf(TEXT("x%d  (max %d, step %d)"), Quantity, MaxQuantity, TransferRules::GetItemQuantityStep(Request.Item)));
}

void UQuantitySplitDialogWidget::RefreshTexts()
{
	if (TitleText)
	{
		TitleText->SetText(GetTitleText());
	}
	if (QuantityText)
	{
		QuantityText->SetText(GetQuantityText());
	}
	if (QuantitySlider && !FMath::IsNearlyEqual(QuantitySlider->GetValue(), static_cast<float>(Quantity)))
	{
		QuantitySlider->SetValue(static_cast<float>(Quantity));
	}
}

void UQuantitySplitDialogWidget::HandleMinus() { Decrement(); }
void UQuantitySplitDialogWidget::HandlePlus() { Increment(); }
void UQuantitySplitDialogWidget::HandleAll() { SelectAll(); }
void UQuantitySplitDialogWidget::HandleConfirm() { Confirm(); }
void UQuantitySplitDialogWidget::HandleCancel() { Cancel(); }

void UQuantitySplitDialogWidget::HandleSlider(float Value)
{
	SetQuantity(FMath::RoundToInt(Value));
}

#undef LOCTEXT_NAMESPACE
