#include "UI/CodexTacticsHUD.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractionSubsystem.h"
#include "UI/ActionMenuWidget.h"
#include "UI/LootDialogWidget.h"
#include "UI/MissionFailedWidget.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/GameMessageSubsystem.h"

namespace
{
	TAutoConsoleVariable<bool> CVarShowStatus(
		TEXT("CodexTactics.HUD.ShowStatus"), true,
		TEXT("Show the squad status panel and the labels above operatives."));

	constexpr float Margin = 16.f;
	constexpr float LinePadding = 3.f;
	const FLinearColor PanelColor(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor SpeakerColor(1.f, 0.85f, 0.35f);
	const FLinearColor TextColor(0.92f, 0.94f, 0.96f);
	const FLinearColor WarningColor(1.f, 0.45f, 0.35f);
	// Godot StyleBoxFlat_obj + ObjectiveLabel.
	const FLinearColor ObjectivePanelColor = ACodexTacticsHUD::GodotColor(0.05f, 0.06f, 0.08f, 0.8f);
	const FLinearColor ObjectiveFrameColor = ACodexTacticsHUD::GodotColor(0.8f, 0.65f, 0.2f, 1.f);
	const FLinearColor ObjectiveTextColor = ACodexTacticsHUD::GodotColor(1.f, 0.9f, 0.5f);

	const TCHAR* PhaseName(ECodexGamePhase Phase)
	{
		switch (Phase)
		{
		case ECodexGamePhase::Exploration: return TEXT("ИССЛЕДОВАНИЕ");
		case ECodexGamePhase::Cutscene: return TEXT("КАТСЦЕНА");
		case ECodexGamePhase::Preparation: return TEXT("ПОДГОТОВКА");
		case ECodexGamePhase::WaveCombat: return TEXT("БОЙ");
		case ECodexGamePhase::WaveCleared: return TEXT("ВОЛНА ОТБИТА");
		case ECodexGamePhase::PostCombat: return TEXT("ПОСЛЕ БОЯ");
		case ECodexGamePhase::GameOver: return TEXT("ПРОВАЛ");
		default: return TEXT("?");
		}
	}

	const TCHAR* ModeName(ECodexCombatMode Mode)
	{
		switch (Mode)
		{
		case ECodexCombatMode::RealTime: return TEXT("реальное время");
		case ECodexCombatMode::TacticalPause: return TEXT("ТАКТИЧЕСКАЯ ПАУЗА");
		case ECodexCombatMode::TurnBased: return TEXT("ПОШАГОВЫЙ");
		default: return TEXT("");
		}
	}

	const TCHAR* TierName(EColdTier Tier)
	{
		switch (Tier)
		{
		case EColdTier::Chills: return TEXT("озноб");
		case EColdTier::Freezing: return TEXT("замерзает");
		case EColdTier::Hypothermia: return TEXT("гипотермия");
		case EColdTier::Frostbite: return TEXT("ОБМОРОЖЕНИЕ");
		default: return TEXT("норма");
		}
	}
}

FString ACodexTacticsHUD::StripUnsupportedGlyphs(const FString& Text)
{
	FString Result;
	Result.Reserve(Text.Len());
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		const uint32 Code = static_cast<uint32>(Char);
		const bool bSurrogate = Code >= 0xD800 && Code <= 0xDFFF; // emoji outside the BMP
		const bool bSymbol = (Code >= 0x2190 && Code <= 0x2BFF) || Code == 0xFE0F || Code == 0x200D || Code == 0x20E3; // 0x20E3: keycap «2️⃣»
		if (!bSurrogate && !bSymbol)
		{
			Result.AppendChar(Char);
		}
	}
	Result.TrimStartAndEndInline();
	return Result;
}

ACodexTacticsHUD::ACodexTacticsHUD()
{
	ActionMenuWidgetClass = UActionMenuWidget::StaticClass();
	LootDialogWidgetClass = ULootDialogWidget::StaticClass();
	MissionFailedWidgetClass = UMissionFailedWidget::StaticClass();
}

void ACodexTacticsHUD::BeginPlay()
{
	Super::BeginPlay();
	if (ActionMenuWidgetClass && GetOwningPlayerController())
	{
		ActionMenu = CreateWidget<UActionMenuWidget>(GetOwningPlayerController(), ActionMenuWidgetClass);
		if (ActionMenu)
		{
			ActionMenu->AddToViewport(10);
			ActionMenu->HideMenu();
		}
	}
	if (LootDialogWidgetClass && GetOwningPlayerController())
	{
		LootDialog = CreateWidget<ULootDialogWidget>(GetOwningPlayerController(), LootDialogWidgetClass);
		if (LootDialog)
		{
			LootDialog->AddToViewport(11);
			LootDialog->HideDialog();
		}
	}
	if (MissionFailedWidgetClass && GetOwningPlayerController())
	{
		MissionFailed = CreateWidget<UMissionFailedWidget>(GetOwningPlayerController(), MissionFailedWidgetClass);
		if (MissionFailed)
		{
			MissionFailed->AddToViewport(20);
			MissionFailed->HideScreen();
		}
	}
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->OnMissionFailed.AddDynamic(this, &ACodexTacticsHUD::HandleMissionFailed);
	}
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->OnActionMenuChanged.AddDynamic(this, &ACodexTacticsHUD::HandleActionMenuChanged);
		Interactions->OnLootDialogChanged.AddDynamic(this, &ACodexTacticsHUD::HandleLootDialogChanged);
	}
}

void ACodexTacticsHUD::HandleLootDialogChanged(bool bOpen, ALootCrateActor* Crate)
{
	if (!LootDialog)
	{
		return;
	}
	if (bOpen && Crate)
	{
		LootDialog->ShowCrate(Crate);
	}
	else
	{
		LootDialog->HideDialog();
	}
}

void ACodexTacticsHUD::HandleMissionFailed(const FText& Reason)
{
	if (MissionFailed)
	{
		MissionFailed->ShowFailure(Reason);
	}
}

void ACodexTacticsHUD::HandleActionMenuChanged(bool bOpen, const FActionMenuSpec& Menu)
{
	if (!ActionMenu)
	{
		return;
	}
	if (bOpen)
	{
		ActionMenu->ShowMenu(Menu);
	}
	else
	{
		ActionMenu->HideMenu();
	}
}

void ACodexTacticsHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine)
	{
		return;
	}
	DrawMessageFeed();
	const float ObjectiveBottom = DrawObjectiveBanner();
	if (CVarShowStatus.GetValueOnGameThread())
	{
		DrawSquadPanel(ObjectiveBottom + Margin * 0.5f);
		DrawOperativeLabels();
	}
}

TArray<FString> ACodexTacticsHUD::WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth) const
{
	TArray<FString> Lines;
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);
	FString Current;
	for (const FString& Word : Words)
	{
		const FString Candidate = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
		float Width = 0.f;
		float Height = 0.f;
		Canvas->StrLen(Font, Candidate, Width, Height);
		if (Width * Scale > MaxWidth && !Current.IsEmpty())
		{
			Lines.Add(Current);
			Current = Word;
		}
		else
		{
			Current = Candidate;
		}
	}
	if (!Current.IsEmpty())
	{
		Lines.Add(Current);
	}
	return Lines;
}

void ACodexTacticsHUD::DrawMessageFeed()
{
	const UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Messages || Messages->GetHistory().IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = 1.1f;
	const float Now = GetWorld()->GetRealTimeSeconds();
	const float Width = Canvas->SizeX * FeedWidthFraction;
	const float Left = Canvas->SizeX - Width - Margin;
	const float LineHeight = Font->GetMaxCharHeight() * Scale + LinePadding;

	// Newest last; collect the visible tail first so the panel can be sized.
	struct FFeedLine
	{
		FString Text;
		FLinearColor Color;
	};
	TArray<FFeedLine> Lines;
	const TArray<FGameMessage>& History = Messages->GetHistory();
	int32 Shown = 0;
	for (int32 Index = History.Num() - 1; Index >= 0 && Shown < MaxFeedMessages; --Index)
	{
		const FGameMessage& Message = History[Index];
		if (Now - Message.PostedAt > FeedMessageLifetime)
		{
			break;
		}
		TArray<FFeedLine> Block;
		Block.Add({ StripUnsupportedGlyphs(Message.Speaker.ToString()).ToUpper(), SpeakerColor });
		for (const FString& Line : WrapText(StripUnsupportedGlyphs(Message.Text.ToString()), Font, Scale, Width - 2.f * Margin))
		{
			Block.Add({ Line, TextColor });
		}
		Lines.Insert(Block, 0);
		++Shown;
	}
	if (Lines.IsEmpty())
	{
		return;
	}

	const float Height = Lines.Num() * LineHeight + Shown * LinePadding * 2.f + Margin;
	DrawRect(PanelColor, Left, Margin, Width, Height);
	float Y = Margin + Margin * 0.5f;
	for (const FFeedLine& Line : Lines)
	{
		if (Line.Color == SpeakerColor && Y > Margin * 1.5f)
		{
			Y += LinePadding * 2.f;
		}
		DrawText(Line.Text, Line.Color, Left + Margin, Y, Font, Scale);
		Y += LineHeight;
	}
}

FString ACodexTacticsHUD::DescribeOperative(const AOperativeCharacter& Operative, bool bLeader) const
{
	FString Line = FString::Printf(TEXT("[%d] %s%s  %s"), Operative.SquadIndex + 1, *Operative.DisplayName.ToString(),
		bLeader ? TEXT(" <ЛИДЕР>") : TEXT(""), *AOperativeCharacter::GetStanceDisplayName(Operative.GetStance()).ToString());
	if (Operative.IsSprinting())
	{
		Line += TEXT(" бег");
	}
	else if (Operative.IsMoving())
	{
		Line += TEXT(" идёт");
	}
	if (const UHealthComponent* Health = Operative.HealthComponent)
	{
		Line += FString::Printf(TEXT("  HP %.0f/%.0f"), Health->GetCurrentHealth(), Health->GetMaxHealth());
	}
	if (const UColdSurvivalComponent* Cold = Operative.ColdSurvival)
	{
		Line += FString::Printf(TEXT("  холод %.0f%% (%s)"), Operative.ColdLevel, TierName(Cold->GetTier()));
		if (Cold->IsNearHeatSource())
		{
			Line += TEXT(" греется");
		}
		if (Cold->IsWeaponFrozen())
		{
			Line += TEXT("  ОРУЖИЕ ЗАМЁРЗЛО");
		}
	}
	Line += FString::Printf(TEXT("  патроны %d/%d%s  спички %d  гранаты %d"), Operative.CurrentClip, Operative.ReserveAmmo,
		Operative.bIsReloading ? TEXT(" перезарядка") : TEXT(""), Operative.MatchesCount, Operative.GrenadesCount);
	if (Operative.TurretsCount + Operative.BarricadesCount + Operative.MinesCount > 0)
	{
		Line += FString::Printf(TEXT("  [турели %d, баррикады %d, мины %d]"), Operative.TurretsCount, Operative.BarricadesCount,
			Operative.MinesCount);
	}
	return Line;
}

float ACodexTacticsHUD::DrawObjectiveBanner()
{
	const UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!Mission || Mission->GetObjective().IsEmpty())
	{
		return Margin;
	}
	// Godot ObjectivePanel: at (20, 20), gold 2 px frame, 12 x 8 padding, gold text.
	UFont* Font = GEngine->GetMediumFont();
	const float Scale = 1.f;
	const FString Text = TEXT("ЦЕЛЬ: ") + StripUnsupportedGlyphs(Mission->GetObjective().ToString());
	float W = 0.f;
	float H = 0.f;
	Canvas->StrLen(Font, Text, W, H);
	const float Left = 20.f;
	const float Top = 20.f;
	const float Width = FMath::Max(430.f, W * Scale + 24.f);
	const float Height = H * Scale + 16.f;
	DrawRect(ObjectiveFrameColor, Left - 2.f, Top - 2.f, Width + 4.f, Height + 4.f);
	DrawRect(ObjectivePanelColor, Left, Top, Width, Height);
	DrawText(Text, ObjectiveTextColor, Left + 12.f, Top + 8.f, Font, Scale);
	return Top + Height + 2.f;
}

void ACodexTacticsHUD::DrawSquadPanel(float Top)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = 1.1f;
	const float LineHeight = Font->GetMaxCharHeight() * Scale + LinePadding;

	TArray<TPair<FString, FLinearColor>> Lines;
	if (Flow)
	{
		FString Header = FString::Printf(TEXT("ФАЗА: %s"), PhaseName(Flow->GetPhase()));
		if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
		{
			Header += FString::Printf(TEXT("  |  %s  |  пауз %d/%d"), ModeName(Flow->GetCombatMode()),
				Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges);
		}
		Lines.Emplace(Header, SpeakerColor);
	}
	if (Squad)
	{
		Lines.Emplace(Squad->IsSoloMode() ? TEXT("Режим: ОДИНОЧНЫЙ [B]") : TEXT("Режим: отряд [B]"), TextColor);
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		for (const AOperativeCharacter* Member : Members)
		{
			const bool bFrozen = Member->ColdSurvival && (Member->ColdSurvival->IsWeaponFrozen() || Member->ColdSurvival->IsFrostbitten());
			Lines.Emplace(DescribeOperative(*Member, Member == Squad->GetLeader()), bFrozen ? WarningColor : Member->BodyColor * 1.3f);
		}
	}
	if (Lines.IsEmpty())
	{
		return;
	}

	float Width = 0.f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Line.Key, W, H);
		Width = FMath::Max(Width, W * Scale);
	}
	DrawRect(PanelColor, Margin, Top, Width + 2.f * Margin, Lines.Num() * LineHeight + Margin);
	float Y = Top + Margin * 0.5f;
	for (const TPair<FString, FLinearColor>& Line : Lines)
	{
		DrawText(Line.Key, Line.Value, Margin * 2.f, Y, Font, Scale);
		Y += LineHeight;
	}
}

void ACodexTacticsHUD::DrawOperativeLabels()
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		const float Top = Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.f;
		const FVector Screen = Project(Member->GetActorLocation() + FVector(0.f, 0.f, Top), true);
		if (Screen.Z <= 0.f)
		{
			continue; // behind the camera
		}
		FString Label = FString::Printf(TEXT("%s · %s"), *Member->DisplayName.ToString(),
			*AOperativeCharacter::GetStanceDisplayName(Member->GetStance()).ToString());
		if (Member->ColdSurvival && Member->ColdSurvival->IsFrostbitten())
		{
			Label += TEXT(" · ОБМОРОЖЕН");
		}
		float W = 0.f;
		float H = 0.f;
		Canvas->StrLen(Font, Label, W, H);
		const bool bLeader = Member == Squad->GetLeader();
		DrawRect(PanelColor, Screen.X - W * 0.5f - 4.f, Screen.Y - 2.f, W + 8.f, H + 4.f);
		DrawText(Label, bLeader ? SpeakerColor : TextColor, Screen.X - W * 0.5f, Screen.Y, Font);
	}
}
