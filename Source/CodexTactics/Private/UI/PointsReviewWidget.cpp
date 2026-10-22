#include "UI/PointsReviewWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/OperativePortraitCapture.h"
#include "UI/ProfileDialogWidget.h"

#define LOCTEXT_NAMESPACE "PointsReviewWidget"

namespace PointsReviewLocal
{
	const FLinearColor PointsScreenBack(0.01f, 0.012f, 0.015f, 1.f);
	const FLinearColor PointsTitleColor = ACodexTacticsHUD::GodotColor(1.f, 0.88f, 0.5f);
	const FLinearColor PointsHintColor = ACodexTacticsHUD::GodotColor(0.7f, 0.75f, 0.8f);
	const FLinearColor PointsFrameColor = ACodexTacticsHUD::GodotColor(0.82f, 0.65f, 0.28f);
	const FLinearColor PointsPortraitBack(0.02f, 0.025f, 0.03f, 1.f);

	FString RoleLetter(const AOperativeCharacter& Operative)
	{
		switch (Operative.SquadRole)
		{
		case EOperativeRole::Engineer: return TEXT("E");
		case EOperativeRole::MedicSapper: return TEXT("M");
		case EOperativeRole::Recruit: return TEXT("R");
		default: return TEXT("C");
		}
	}
}

void UPointsReviewWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UPointsReviewWidget::BuildDefaultLayout()
{
	using namespace PointsReviewLocal; // names prefixed "Points": unity builds merge this file with ProfileDialogWidget.cpp
	auto MakeText = [this](const FName& Name, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		return Text;
	};

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PointsRoot"));
	WidgetTree->RootWidget = Root;
	// The black exit screen stays: a full-screen backdrop that also eats the clicks meant for the world.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PointsBackdrop"));
	Backdrop->SetBrushColor(PointsScreenBack);
	UCanvasPanelSlot* BackSlot = Root->AddChildToCanvas(Backdrop);
	BackSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackSlot->SetOffsets(FMargin(0.f));

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PointsColumn"));
	UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column);
	ColumnSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	ColumnSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	ColumnSlot->SetAutoSize(true);

	TitleText = MakeText(TEXT("PointsTitle"), 26, PointsTitleColor);
	Column->AddChildToVerticalBox(TitleText)->SetHorizontalAlignment(HAlign_Center);
	UTextBlock* Sub = MakeText(TEXT("PointsSubtitle"), 14, PointsHintColor);
	Sub->SetText(LOCTEXT("Subtitle", "Unspent attribute points: spend them now with + / -, or keep them for later."));
	Column->AddChildToVerticalBox(Sub)->SetPadding(FMargin(0.f, 6.f, 0.f, 18.f));

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PointsRow"));
	Column->AddChildToVerticalBox(Row)->SetHorizontalAlignment(HAlign_Center);

	// The character image: a live portrait (render target) over a dark card with the role letter as the fallback.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitCardFrame"));
	Frame->SetBrushColor(PointsFrameColor);
	Frame->SetPadding(FMargin(3.f));
	UOverlay* Card = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PortraitCard"));
	UBorder* CardBack = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitCardBack"));
	CardBack->SetBrushColor(PointsPortraitBack);
	UOverlaySlot* BackOverlay = Card->AddChildToOverlay(CardBack);
	BackOverlay->SetHorizontalAlignment(HAlign_Fill);
	BackOverlay->SetVerticalAlignment(VAlign_Fill);
	PortraitLetter = MakeText(TEXT("PortraitLetter"), 96, PointsTitleColor);
	UOverlaySlot* LetterSlot = Card->AddChildToOverlay(PortraitLetter);
	LetterSlot->SetHorizontalAlignment(HAlign_Center);
	LetterSlot->SetVerticalAlignment(VAlign_Center);
	PortraitImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PortraitImage"));
	PortraitImage->SetVisibility(ESlateVisibility::Collapsed);
	UOverlaySlot* ImageSlot = Card->AddChildToOverlay(PortraitImage);
	ImageSlot->SetHorizontalAlignment(HAlign_Fill);
	ImageSlot->SetVerticalAlignment(VAlign_Fill);
	PortraitName = MakeText(TEXT("PortraitName"), 15, PointsTitleColor);
	UOverlaySlot* NameSlot = Card->AddChildToOverlay(PortraitName);
	NameSlot->SetHorizontalAlignment(HAlign_Center);
	NameSlot->SetVerticalAlignment(VAlign_Bottom);
	NameSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	Frame->SetContent(Card);
	USizeBox* PortraitSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PortraitSize"));
	PortraitSize->SetWidthOverride(360.f);
	PortraitSize->SetHeightOverride(450.f);
	PortraitSize->AddChild(Frame);
	UHorizontalBoxSlot* PortraitHSlot = Row->AddChildToHorizontalBox(PortraitSize);
	PortraitHSlot->SetVerticalAlignment(VAlign_Center);
	PortraitHSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));

	// The regular character sheet goes here (Init).
	ProfileSlot = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ProfileSlot"));
	ProfileSlot->SetWidthOverride(430.f);
	ProfileSlot->SetHeightOverride(530.f);
	Row->AddChildToHorizontalBox(ProfileSlot)->SetVerticalAlignment(VAlign_Center);

	HintText = MakeText(TEXT("PointsHint"), 14, PointsHintColor);
	Column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
}

void UPointsReviewWidget::Init(const TArray<AOperativeCharacter*>& Members, const FString& LevelTitle, const FString& NextLevelTitle,
	TSubclassOf<UProfileDialogWidget> ProfileClass)
{
	if (!WidgetTree || !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (TitleText)
	{
		TitleText->SetText(FText::Format(LOCTEXT("Title", "{0} - MISSION COMPLETE"), FText::FromString(LevelTitle.ToUpper())));
	}
	if (HintText)
	{
		HintText->SetText(NextLevelTitle.IsEmpty() ? LOCTEXT("HintNoNext", "Continue > when the squad is ready.")
			: FText::Format(LOCTEXT("Hint", "Continue > opens the next level: {0}"), FText::FromString(NextLevelTitle)));
	}
	if (!Profile && ProfileSlot)
	{
		Profile = CreateWidget<UProfileDialogWidget>(this, ProfileClass ? *ProfileClass : UProfileDialogWidget::StaticClass());
		if (Profile)
		{
			ProfileSlot->AddChild(Profile);
			Profile->OnReviewContinue.AddUObject(this, &UPointsReviewWidget::HandleProfileContinue);
		}
	}
	if (Profile)
	{
		Profile->OpenReview(Members);
	}
	// The live portrait only where the game renders (headless checks: the role letter).
	UWorld* World = GetWorld();
	if (!Capture && World && FApp::CanEverRender())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Capture = World->SpawnActor<AOperativePortraitCapture>(Params);
		if (Capture && PortraitImage)
		{
			PortraitImage->SetBrushResourceObject(Capture->GetRenderTarget());
			PortraitImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	FollowShownMember();
}

void UPointsReviewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	FollowShownMember();
}

void UPointsReviewWidget::FollowShownMember()
{
	AOperativeCharacter* Shown = GetShownMember();
	if (Shown == PortraitMember.Get())
	{
		return;
	}
	PortraitMember = Shown;
	if (PortraitLetter)
	{
		PortraitLetter->SetText(Shown ? FText::FromString(PointsReviewLocal::RoleLetter(*Shown)) : FText::GetEmpty());
		PortraitLetter->SetVisibility(Capture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (PortraitName)
	{
		PortraitName->SetText(Shown ? Shown->DisplayName : FText::GetEmpty());
	}
	if (Capture)
	{
		Capture->SetTarget(Shown);
	}
}

AOperativeCharacter* UPointsReviewWidget::GetShownMember() const
{
	return Profile ? Profile->GetMember() : nullptr;
}

bool UPointsReviewWidget::HasPortraitImage() const
{
	return Capture != nullptr && PortraitImage && PortraitImage->GetVisibility() != ESlateVisibility::Collapsed;
}

AOperativeCharacter* UPointsReviewWidget::GetPortraitTarget() const
{
	return Capture ? Capture->GetTarget() : nullptr;
}

FString UPointsReviewWidget::GetTitleText() const
{
	return TitleText ? TitleText->GetText().ToString() : FString();
}

FString UPointsReviewWidget::GetHintText() const
{
	return HintText ? HintText->GetText().ToString() : FString();
}

void UPointsReviewWidget::HandleProfileContinue()
{
	Continue();
}

void UPointsReviewWidget::Continue()
{
	if (bContinued)
	{
		return;
	}
	bContinued = true;
	OnContinue.Broadcast();
}

void UPointsReviewWidget::NativeDestruct()
{
	if (Capture)
	{
		Capture->Destroy();
		Capture = nullptr;
	}
	Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
