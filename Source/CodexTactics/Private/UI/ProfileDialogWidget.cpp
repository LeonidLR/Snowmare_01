#include "UI/ProfileDialogWidget.h"
#include "Subsystems/CodexEventBus.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/CodexTacticsHUD.h"

#define LOCTEXT_NAMESPACE "ProfileDialogWidget"

namespace
{
	// Godot profile_dialog.gd colours.
	const FLinearColor ProfileBack = ACodexTacticsHUD::GodotColor(0.11f, 0.12f, 0.13f, 0.98f);
	const FLinearColor ProfileFrame = ACodexTacticsHUD::GodotColor(0.82f, 0.65f, 0.28f);
	const FLinearColor PortraitBack = ACodexTacticsHUD::GodotColor(0.06f, 0.07f, 0.08f);
	const FLinearColor PortraitFrame = ACodexTacticsHUD::GodotColor(0.6f, 0.5f, 0.25f);
	const FLinearColor RowBack = ACodexTacticsHUD::GodotColor(0.07f, 0.08f, 0.09f, 0.95f);
	const FLinearColor RowFrame = ACodexTacticsHUD::GodotColor(0.3f, 0.32f, 0.35f);
	const FLinearColor RowTextColor = ACodexTacticsHUD::GodotColor(0.88f, 0.9f, 0.92f);
	const FLinearColor NameColor = ACodexTacticsHUD::GodotColor(1.f, 0.88f, 0.5f);
	const FLinearColor RoleColor = ACodexTacticsHUD::GodotColor(0.7f, 0.75f, 0.8f);
	const FLinearColor LevelColor = ACodexTacticsHUD::GodotColor(0.3f, 0.9f, 1.f);
	const FLinearColor PointsColor = ACodexTacticsHUD::GodotColor(1.f, 0.85f, 0.2f);
	const FLinearColor NoPointsColor = ACodexTacticsHUD::GodotColor(0.6f, 0.6f, 0.6f);
	const FLinearColor ButtonText(0.05f, 0.05f, 0.05f);
	constexpr int32 RowCount = 5;
	constexpr int32 StatCount = 4;

	/** Bar fill per row: EXP, HP, luck, accuracy, fortitude. */
	FLinearColor RowFill(int32 Row)
	{
		switch (Row)
		{
		case 0: return ACodexTacticsHUD::GodotColor(0.35f, 0.75f, 1.f);
		case 1: return ACodexTacticsHUD::GodotColor(0.25f, 0.88f, 0.45f);
		case 2: return ACodexTacticsHUD::GodotColor(1.f, 0.85f, 0.2f);
		case 3: return ACodexTacticsHUD::GodotColor(0.45f, 0.7f, 1.f);
		default: return ACodexTacticsHUD::GodotColor(1.f, 0.5f, 0.25f);
		}
	}

	FText ProfileClean(const FString& Text)
	{
		return FText::FromString(ACodexTacticsHUD::StripUnsupportedGlyphs(Text));
	}

	/** Godot _populate_profile_dialog names by character; the recruit keeps his own name. */
	void Identity(const AOperativeCharacter& Member, FString& OutName, FString& OutRole, FString& OutIcon)
	{
		switch (Member.SquadRole)
		{
		case EOperativeRole::Engineer:
			OutName = TEXT("Сержант Кузнецов");
			OutRole = TEXT("Инженер / Саппорт");
			OutIcon = TEXT("И");
			break;
		case EOperativeRole::MedicSapper:
			OutName = TEXT("Лейтенант Морозова");
			OutRole = TEXT("Медик-сапёр / Разведчик");
			OutIcon = TEXT("М");
			break;
		case EOperativeRole::Recruit:
			OutName = Member.DisplayName.ToString();
			OutRole = TEXT("Рекрут / Проводник");
			OutIcon = TEXT("Р");
			break;
		default:
			OutName = TEXT("Полковник Васин");
			OutRole = TEXT("Командир / Снайпер");
			OutIcon = TEXT("К");
			break;
		}
	}
}

UTextBlock* UProfileDialogWidget::MakeText(const FName& Name, int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	return Text;
}

void UProfileDialogWidget::BuildDefaultLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ProfileRoot"));
	WidgetTree->RootWidget = Root;

	// Godot: centred, 430 x 530, gold border 3 px.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CharacterProfileDialog"));
	Frame->SetBrushColor(ProfileFrame);
	Frame->SetPadding(FMargin(3.f));
	Frame->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetSize(FVector2D(430.f, 530.f));
	Panel = Frame;

	UBorder* Body = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ProfileBody"));
	Body->SetBrushColor(ProfileBack);
	Body->SetPadding(FMargin(14.f, 12.f));
	Frame->SetContent(Body);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ProfileColumn"));
	Body->SetContent(Column);

	// 1. Header: portrait + name / role / level / free points.
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfileHeader"));
	Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UBorder* PortraitBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitFrame"));
	PortraitBorder->SetBrushColor(PortraitFrame);
	PortraitBorder->SetPadding(FMargin(2.f));
	UBorder* PortraitInner = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PortraitBack"));
	PortraitInner->SetBrushColor(PortraitBack);
	PortraitInner->SetHorizontalAlignment(HAlign_Center);
	PortraitInner->SetVerticalAlignment(VAlign_Center);
	PortraitBorder->SetContent(PortraitInner);
	PortraitText = MakeText(TEXT("ProfilePortraitIcon"), 30, NameColor);
	PortraitInner->SetContent(PortraitText);
	USizeBox* PortraitSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PortraitSize"));
	PortraitSize->SetWidthOverride(70.f);
	PortraitSize->SetHeightOverride(75.f);
	PortraitSize->AddChild(PortraitBorder);
	Header->AddChildToHorizontalBox(PortraitSize)->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ProfileInfo"));
	UHorizontalBoxSlot* InfoSlot = Header->AddChildToHorizontalBox(Info);
	InfoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	InfoSlot->SetVerticalAlignment(VAlign_Center);
	NameText = MakeText(TEXT("ProfileNameLabel"), 16, NameColor);
	Info->AddChildToVerticalBox(NameText);
	RoleText = MakeText(TEXT("ProfileRoleLabel"), 12, RoleColor);
	Info->AddChildToVerticalBox(RoleText);
	LevelText = MakeText(TEXT("ProfileLevelLabel"), 13, LevelColor);
	Info->AddChildToVerticalBox(LevelText);
	UnspentText = MakeText(TEXT("ProfileUnspentLabel"), 12, PointsColor);
	Info->AddChildToVerticalBox(UnspentText);

	// 2. Stat rows: text (+ - / + for the four stats) over a bar.
	auto MakeButton = [this](const FName& Name, const FString& Label, float Width)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = MakeText(NAME_None, 11, ButtonText);
		Text->SetText(FText::FromString(Label));
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetMinDesiredWidth(Width);
		Size->SetHeightOverride(24.f);
		Size->AddChild(Button);
		return TPair<UButton*, UWidget*>(Button, Size);
	};
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		UBorder* RowFrameBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowFrameBorder->SetBrushColor(RowFrame);
		RowFrameBorder->SetPadding(FMargin(1.f));
		UBorder* RowBody = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBody->SetBrushColor(RowBack);
		RowBody->SetPadding(FMargin(8.f, 4.f));
		RowFrameBorder->SetContent(RowBody);
		UVerticalBox* RowColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		RowBody->SetContent(RowColumn);
		UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowColumn->AddChildToVerticalBox(Top)->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		UTextBlock* Text = MakeText(NAME_None, 12, RowTextColor);
		UHorizontalBoxSlot* TextSlot = Top->AddChildToHorizontalBox(Text);
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		RowTexts.Add(Text);
		if (Row > 0)
		{
			const TPair<UButton*, UWidget*> Minus = MakeButton(NAME_None, TEXT("-"), 28.f);
			Top->AddChildToHorizontalBox(Minus.Value)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			MinusButtons.Add(Minus.Key);
			const TPair<UButton*, UWidget*> Plus = MakeButton(NAME_None, TEXT("+"), 28.f);
			Top->AddChildToHorizontalBox(Plus.Value)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));
			PlusButtons.Add(Plus.Key);
		}
		UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Bar->SetFillColorAndOpacity(RowFill(Row));
		USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BarSize->SetHeightOverride(8.f);
		BarSize->AddChild(Bar);
		RowColumn->AddChildToVerticalBox(BarSize);
		RowBars.Add(Bar);
		Column->AddChildToVerticalBox(RowFrameBorder)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	// 3. Navigation.
	UHorizontalBox* Nav = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProfileNav"));
	UVerticalBoxSlot* NavSlot = Column->AddChildToVerticalBox(Nav);
	NavSlot->SetHorizontalAlignment(HAlign_Center);
	NavSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	const TPair<UButton*, UWidget*> Prev = MakeButton(TEXT("BtnPrev"), TEXT("< Пред."), 80.f);
	const TPair<UButton*, UWidget*> CloseButton = MakeButton(TEXT("BtnClose"), TEXT("Закрыть [P]"), 120.f);
	const TPair<UButton*, UWidget*> Next = MakeButton(TEXT("BtnNext"), TEXT("След. >"), 80.f);
	Nav->AddChildToHorizontalBox(Prev.Value)->SetPadding(FMargin(4.f, 0.f));
	Nav->AddChildToHorizontalBox(CloseButton.Value)->SetPadding(FMargin(4.f, 0.f));
	Nav->AddChildToHorizontalBox(Next.Value)->SetPadding(FMargin(4.f, 0.f));
	Prev.Key->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandlePrev);
	CloseButton.Key->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleClose);
	Next.Key->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleNext);

	MinusButtons[0]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleMinusHealth);
	PlusButtons[0]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandlePlusHealth);
	MinusButtons[1]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleMinusLuck);
	PlusButtons[1]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandlePlusLuck);
	MinusButtons[2]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleMinusAccuracy);
	PlusButtons[2]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandlePlusAccuracy);
	MinusButtons[3]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandleMinusFortitude);
	PlusButtons[3]->OnClicked.AddDynamic(this, &UProfileDialogWidget::HandlePlusFortitude);
}

void UProfileDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UProfileDialogWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsOpen())
	{
		Refresh();
	}
}

bool UProfileDialogWidget::IsOpen() const
{
	return Panel && Panel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UProfileDialogWidget::Open(AOperativeCharacter* InMember)
{
	if (!InMember)
	{
		const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		InMember = Squad ? Squad->GetLeader() : nullptr;
	}
	if (!InMember || !Panel)
	{
		return;
	}
	Member = InMember;
	Panel->SetVisibility(ESlateVisibility::Visible);
	Refresh();
}

void UProfileDialogWidget::Close()
{
	Member.Reset();
	if (Panel)
	{
		Panel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UProfileDialogWidget::SwitchMember(int32 Direction)
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
	if (Members.IsEmpty())
	{
		return;
	}
	int32 Index = Members.IndexOfByKey(Member.Get());
	Index = Index == INDEX_NONE ? 0 : (Index + Direction + Members.Num()) % Members.Num();
	Member = Members[Index];
	Refresh();
}

void UProfileDialogWidget::ClickStat(EProgressStat Stat, int32 Direction)
{
	AOperativeCharacter* Operative = Member.Get();
	if (!Operative)
	{
		return;
	}
	if (Direction > 0)
	{
		Operative->IncreaseStat(Stat);
	}
	else
	{
		Operative->DecreaseStat(Stat);
	}
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSoldierStatsUpdated.Broadcast(Operative);
	}
	Refresh();
}

bool UProfileDialogWidget::IsStatButtonEnabled(EProgressStat Stat, int32 Direction) const
{
	const AOperativeCharacter* Operative = Member.Get();
	return Operative && (Direction > 0 ? Operative->CanIncreaseStat(Stat) : Operative->CanDecreaseStat(Stat));
}

FString UProfileDialogWidget::GetHeaderText() const
{
	const AOperativeCharacter* Operative = Member.Get();
	if (!Operative)
	{
		return FString();
	}
	FString Name;
	FString Role;
	FString Icon;
	Identity(*Operative, Name, Role, Icon);
	return FString::Printf(TEXT("%s\n%s\nУровень: %d\n⭐ Свободных очков: %d"), *Name, *Role, Operative->Level, Operative->UnspentStatPoints);
}

FString UProfileDialogWidget::GetRowText(int32 Row) const
{
	const AOperativeCharacter* Operative = Member.Get();
	if (!Operative)
	{
		return FString();
	}
	switch (Row)
	{
	case 0:
		return FString::Printf(TEXT("📈 Опыт: %d / %d XP"), Operative->CurrentExp, Operative->GetNextLevelExp());
	case 1:
	{
		const UHealthComponent* Health = Operative->HealthComponent;
		return FString::Printf(TEXT("❤️ HP: %d / %d (Макс: 200)"), Health ? static_cast<int32>(Health->GetCurrentHealth()) : 100,
			Health ? static_cast<int32>(Health->GetMaxHealth()) : 100);
	}
	case 2:
		return FString::Printf(TEXT("🍀 Удача: %d%% (Крит x2.0, Уклон)"), static_cast<int32>(Operative->Luck));
	case 3:
	{
		const EOperativeStance Stance = Operative->GetStance();
		const TCHAR* StanceText = Stance == EOperativeStance::Crouching ? TEXT("Сидя (x1.15)")
			: (Stance == EOperativeStance::Prone ? TEXT("Лёжа (x1.35)") : TEXT("Стоя"));
		return FString::Printf(TEXT("🎯 Меткость: %d%% | %s"), static_cast<int32>(Operative->Accuracy), StanceText);
	}
	default:
	{
		const float Fortitude = Operative->GetStatValue(EProgressStat::Fortitude);
		return FString::Printf(TEXT("🛡️ Стойкость: %d (Срез: -%d%%)"), static_cast<int32>(Fortitude),
			ProgressionRules::FortitudeCutPercent(Fortitude));
	}
	}
}

void UProfileDialogWidget::Refresh()
{
	const AOperativeCharacter* Operative = Member.Get();
	if (!Operative)
	{
		if (IsOpen())
		{
			Close();
		}
		return;
	}
	FString Name;
	FString Role;
	FString Icon;
	Identity(*Operative, Name, Role, Icon);
	if (PortraitText)
	{
		PortraitText->SetText(FText::FromString(Icon));
	}
	if (NameText)
	{
		NameText->SetText(FText::FromString(Name));
	}
	if (RoleText)
	{
		RoleText->SetText(FText::FromString(Role));
	}
	if (LevelText)
	{
		LevelText->SetText(FText::FromString(FString::Printf(TEXT("Уровень: %d"), Operative->Level)));
	}
	if (UnspentText)
	{
		UnspentText->SetText(ProfileClean(FString::Printf(TEXT("⭐ Свободных очков: %d"), Operative->UnspentStatPoints)));
		UnspentText->SetColorAndOpacity(FSlateColor(Operative->UnspentStatPoints > 0 ? PointsColor : NoPointsColor));
	}
	// Bars: EXP of the next level, max HP of the 200 cap, the stat of its cap.
	const float Values[RowCount] = {
		static_cast<float>(Operative->CurrentExp),
		Operative->GetStatValue(EProgressStat::Health),
		Operative->GetStatValue(EProgressStat::Luck),
		Operative->GetStatValue(EProgressStat::Accuracy),
		Operative->GetStatValue(EProgressStat::Fortitude) };
	const float Caps[RowCount] = {
		static_cast<float>(Operative->GetNextLevelExp()),
		ProgressionRules::StatCap(EProgressStat::Health),
		ProgressionRules::StatCap(EProgressStat::Luck),
		ProgressionRules::StatCap(EProgressStat::Accuracy),
		ProgressionRules::StatCap(EProgressStat::Fortitude) };
	for (int32 Row = 0; Row < RowCount && Row < RowTexts.Num(); ++Row)
	{
		RowTexts[Row]->SetText(ProfileClean(GetRowText(Row)));
		if (RowBars.IsValidIndex(Row))
		{
			RowBars[Row]->SetPercent(Caps[Row] > 0.f ? FMath::Clamp(Values[Row] / Caps[Row], 0.f, 1.f) : 0.f);
		}
	}
	for (int32 Stat = 0; Stat < StatCount && Stat < PlusButtons.Num(); ++Stat)
	{
		PlusButtons[Stat]->SetIsEnabled(IsStatButtonEnabled(static_cast<EProgressStat>(Stat), 1));
		MinusButtons[Stat]->SetIsEnabled(IsStatButtonEnabled(static_cast<EProgressStat>(Stat), -1));
	}
}

void UProfileDialogWidget::HandleMinusHealth() { ClickStat(EProgressStat::Health, -1); }
void UProfileDialogWidget::HandlePlusHealth() { ClickStat(EProgressStat::Health, 1); }
void UProfileDialogWidget::HandleMinusLuck() { ClickStat(EProgressStat::Luck, -1); }
void UProfileDialogWidget::HandlePlusLuck() { ClickStat(EProgressStat::Luck, 1); }
void UProfileDialogWidget::HandleMinusAccuracy() { ClickStat(EProgressStat::Accuracy, -1); }
void UProfileDialogWidget::HandlePlusAccuracy() { ClickStat(EProgressStat::Accuracy, 1); }
void UProfileDialogWidget::HandleMinusFortitude() { ClickStat(EProgressStat::Fortitude, -1); }
void UProfileDialogWidget::HandlePlusFortitude() { ClickStat(EProgressStat::Fortitude, 1); }
void UProfileDialogWidget::HandlePrev() { SwitchMember(-1); }
void UProfileDialogWidget::HandleNext() { SwitchMember(1); }
void UProfileDialogWidget::HandleClose() { Close(); }

#undef LOCTEXT_NAMESPACE
