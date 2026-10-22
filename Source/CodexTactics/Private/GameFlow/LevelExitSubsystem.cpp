#include "GameFlow/LevelExitSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/CampaignSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interactables/GateActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/PointsReviewWidget.h"
#include "UI/ProfileDialogWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelExitSubsystem"

namespace LevelExitSubsystemLocal
{
	/** A load hitch must not jump the fade (same cap as the game flow's timers). */
	constexpr float MaxExitStepSeconds = 0.25f;

	/** The points screen sits above the black exit overlay (9100). */
	constexpr int32 PointsScreenZOrder = 9200;

	bool IsLiving(const AOperativeCharacter* Operative)
	{
		return Operative && !Operative->IsKilledInAction() && (!Operative->HealthComponent || Operative->HealthComponent->GetCurrentHealth() > 0.f);
	}

	/** The squad in roster order. */
	TArray<AOperativeCharacter*> SortedMembers(const UWorld* World)
	{
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		return Members;
	}
}

void ULevelExitSubsystem::Deinitialize()
{
	RemoveOverlay();
	RemoveStartOverlay();
	Super::Deinitialize();
}

bool ULevelExitSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId ULevelExitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULevelExitSubsystem, STATGROUP_Tickables);
}

FGateExitConfig ULevelExitSubsystem::GetConfig() const
{
	if (bHasOverride)
	{
		return OverrideConfig;
	}
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	return Level ? Level->Config.GateExit : FGateExitConfig();
}

void ULevelExitSubsystem::SetConfigOverride(const FGateExitConfig& InConfig)
{
	bHasOverride = true;
	OverrideConfig = InConfig;
}

float ULevelExitSubsystem::GetFadeAlpha() const
{
	return bExiting ? GateExitRules::FadeAlpha(ExitElapsed, ArmedConfig) : 0.f;
}

float ULevelExitSubsystem::GetTitleAlpha() const
{
	return bExiting ? GateExitRules::TitleAlpha(ExitElapsed, ArmedConfig) : 0.f;
}

bool ULevelExitSubsystem::HasCrossed(const AOperativeCharacter* Operative) const
{
	const bool* Found = Crossed.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative)));
	return Found && *Found;
}

bool ULevelExitSubsystem::GetExitLine(FGateExitLine& OutLine) const
{
	if (!bArmed)
	{
		return false;
	}
	OutLine = Line;
	return true;
}

bool ULevelExitSubsystem::IsPointsScreenShown() const
{
	return PointsScreen && PointsScreen->IsInViewport() && !PointsScreen->HasContinued();
}

float ULevelExitSubsystem::GetStartFadeAlpha() const
{
	return bStartFadeResolved ? CampaignRules::StartFadeAlpha(StartElapsed, StartFadeSeconds) : 0.f;
}

float ULevelExitSubsystem::GetStartTitleAlpha() const
{
	return bStartFadeResolved && !StartTitle.IsEmpty() ? CampaignRules::StartTitleAlpha(StartElapsed, StartFadeSeconds) : 0.f;
}

FString ULevelExitSubsystem::GetCurrentLevelTitle() const
{
	const UWorld* World = GetWorld();
	UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World);
	return World && Campaign ? Campaign->GetLevelTitle(World->GetMapName(), FString()) : FString();
}

void ULevelExitSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	TickStartFade(*World);
	if (bExiting)
	{
		ExitElapsed += FMath::Min(static_cast<float>(FApp::GetDeltaTime()), LevelExitSubsystemLocal::MaxExitStepSeconds);
		const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		const bool bHeadless = (!Mission || Mission->IsHeadlessStart()) && !bLeaveInHeadless;
		if (!bLeaveDecided && GateExitRules::ShouldLeaveLevel(ExitElapsed, ArmedConfig, !NextLevelMap.IsEmpty(), bHeadless))
		{
			Leave();
		}
		return;
	}
	const FGateExitConfig Config = GetConfig();
	if (!Config.bEnabled)
	{
		return;
	}
	const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
	AGateActor* LevelGate = Gate.IsValid() ? Gate.Get() : AGateActor::FindLevelGate(World);
	const bool bArmedNow = LevelGate && GateExitRules::IsArmed(Config.bEnabled, LevelGate->IsOpen(),
		Mission && Mission->IsMissionFailed(), Mission && Mission->IsMissionComplete());
	if (!bArmedNow)
	{
		return;
	}
	if (!bArmed)
	{
		Arm(LevelGate, Config);
	}
	UpdateCrossings(Config);
}

void ULevelExitSubsystem::Arm(AGateActor* InGate, const FGateExitConfig& InConfig)
{
	UWorld* World = GetWorld();
	Gate = InGate;
	ArmedConfig = InConfig;
	// "auto": out is the side away from the CombatStart point (the defended yard), else away from the squad now.
	const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
	const FName InsideTag = Mission ? Mission->CombatStartTag : FName(TEXT("CombatStart"));
	TOptional<FVector> Inside;
	for (TActorIterator<AActor> It(World); It && !Inside.IsSet(); ++It)
	{
		if (It->ActorHasTag(InsideTag))
		{
			Inside = It->GetActorLocation();
		}
	}
	if (!Inside.IsSet())
	{
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Sum += Member->GetActorLocation();
			++Count;
		}
		if (Count > 0)
		{
			Inside = Sum / Count;
		}
	}
	const FTransform GateTransform = InGate->GetActorTransform();
	const FVector* InsidePoint = Inside.IsSet() ? &Inside.GetValue() : nullptr;
	const int32 Sign = GateExitRules::ResolveOutwardSign(InConfig.Side, GateTransform, InsidePoint);
	Line = GateExitRules::MakeLine(GateTransform, InGate->DoorSize.Y * GateTransform.GetScale3D().Y, Sign);
	bArmed = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] armed at %s: out = %s (%s), doorway half width %.0f cm"), *InGate->GetActorNameOrLabel(),
		*Line.Outward.ToCompactString(), *GateExitRules::SideToString(true, InConfig.Side), Line.HalfWidthCm);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("Commander", "Commander"), MissionRules::GetGateExitOpenRadio());
	}
}

void ULevelExitSubsystem::UpdateCrossings(const FGateExitConfig& InConfig)
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	TArray<FGateExitMember> Members;
	for (AOperativeCharacter* Operative : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		bool& bCrossed = Crossed.FindOrAdd(Operative);
		const bool bWas = bCrossed;
		bCrossed = GateExitRules::UpdateCrossed(bCrossed, Line, InConfig, Operative->GetActorLocation());
		if (bCrossed != bWas)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] %s %s (%.0f cm beyond the door)"), *Operative->DisplayName.ToString(),
				bCrossed ? TEXT("is out") : TEXT("went back inside"), GateExitRules::SignedDistance(Line, Operative->GetActorLocation()));
		}
		FGateExitMember& Member = Members.AddDefaulted_GetRef();
		Member.bAlive = LevelExitSubsystemLocal::IsLiving(Operative);
		Member.bCrossed = bCrossed;
	}
	CrossedCount = GateExitRules::CountCrossed(Members);
	RequiredCount = GateExitRules::CountRequired(Members);
	// The exit objective stays up (the victory dialogue's "after victory" objective arrives a few seconds after the gate opened).
	if (UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>())
	{
		const FText ExitObjective = MissionRules::GetGateExitObjective(CrossedCount, RequiredCount);
		if (CrossedCount != ReportedCrossed || RequiredCount != ReportedRequired || !Mission->GetObjective().EqualTo(ExitObjective))
		{
			ReportedCrossed = CrossedCount;
			ReportedRequired = RequiredCount;
			Mission->SetObjective(ExitObjective);
		}
	}
	if (GateExitRules::ShouldEndLevel(Members))
	{
		BeginExit();
	}
}

void ULevelExitSubsystem::BeginExit()
{
	if (bExiting)
	{
		return;
	}
	bExiting = true;
	ExitElapsed = 0.f;
	// The campaign's next level (2026-10-22 b) decides how long the black screen lasts and what follows it.
	UWorld* World = GetWorld();
	UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World);
	if (const FCampaignLevel* Next = Campaign ? Campaign->FindNextLevel(World) : nullptr)
	{
		NextLevelMap = Next->Map;
		NextLevelTitle = Next->Title;
	}
	ExitLevelTitle = GetCurrentLevelTitle();
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] all %d living operatives are out: fade to black (%.1f s), mission complete; next: %s"), RequiredCount,
		ArmedConfig.FadeSeconds, NextLevelMap.IsEmpty() ? TEXT("the main menu") : *NextLevelMap);
	if (UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>())
	{
		Mission->CompleteMission();
	}
	ShowOverlay();
}

void ULevelExitSubsystem::Leave()
{
	bLeaveDecided = true;
	UWorld* World = GetWorld();
	const TArray<AOperativeCharacter*> Members = LevelExitSubsystemLocal::SortedMembers(World);
	TArray<FPointsReviewMember> Review;
	for (const AOperativeCharacter* Member : Members)
	{
		Review.Add({ LevelExitSubsystemLocal::IsLiving(Member), Member->UnspentStatPoints });
	}
	Decision = CampaignRules::DecideAfterExit(!NextLevelMap.IsEmpty(), CampaignRules::AnyUnspentPoints(Review));
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] after the black screen: %s"), *UEnum::GetValueAsString(Decision));
	switch (Decision)
	{
	case ELevelExitNext::PointsScreen:
	{
		TArray<AOperativeCharacter*> WithPoints;
		for (const int32 Index : CampaignRules::GetReviewIndices(Review))
		{
			WithPoints.Add(Members[Index]);
		}
		ShowPointsScreen(WithPoints);
		break;
	}
	case ELevelExitNext::NextLevel:
		TravelToNextLevel();
		break;
	default:
		if (UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(this))
		{
			Frontend->QuitToMainMenu();
		}
		break;
	}
}

void ULevelExitSubsystem::ShowPointsScreen(const TArray<AOperativeCharacter*>& Members)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		TravelToNextLevel(); // nobody to show it to
		return;
	}
	PointsScreen = CreateWidget<UPointsReviewWidget>(PC, UPointsReviewWidget::StaticClass());
	if (!PointsScreen)
	{
		TravelToNextLevel();
		return;
	}
	const ACodexTacticsHUD* Hud = Cast<ACodexTacticsHUD>(PC->GetHUD());
	PointsScreen->Init(Members, ExitLevelTitle, NextLevelTitle, Hud ? Hud->ProfileDialogWidgetClass : nullptr);
	PointsScreen->OnContinue.AddUObject(this, &ULevelExitSubsystem::HandlePointsContinue);
	PointsScreen->AddToViewport(LevelExitSubsystemLocal::PointsScreenZOrder);
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);
	FString Names;
	for (const AOperativeCharacter* Member : Members)
	{
		Names += FString::Printf(TEXT("%s%s (%d)"), Names.IsEmpty() ? TEXT("") : TEXT(", "), *Member->DisplayName.ToString(), Member->UnspentStatPoints);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] stat-points screen: %s"), *Names);
}

void ULevelExitSubsystem::HandlePointsContinue()
{
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] points screen: Continue"));
	TravelToNextLevel();
}

void ULevelExitSubsystem::TravelToNextLevel()
{
	UWorld* World = GetWorld();
	if (bTravelRequested || !World || NextLevelMap.IsEmpty())
	{
		return;
	}
	bTravelRequested = true;
	if (UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(World))
	{
		Campaign->CaptureSquad(World, NextLevelMap);
	}
	if (UMissionSessionSubsystem* Session = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>() : nullptr)
	{
		Session->LastMode = EMissionStartMode::Game;
		Session->bQuickRestart = false;
		Session->bFrontendStart = false;
		Session->PendingLoadSlot.Reset();
		Session->PendingLoadDirectory.Reset();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] opening the next level %s (%s)"), *NextLevelMap, *NextLevelTitle);
	UGameplayStatics::OpenLevel(World, FName(*NextLevelMap));
}

void ULevelExitSubsystem::TickStartFade(UWorld& World)
{
	if (!bStartFadeResolved)
	{
		bStartFadeResolved = true;
		const ACodexTacticsGameMode* GameMode = World.GetAuthGameMode<ACodexTacticsGameMode>();
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		UCampaignSubsystem* Campaign = UCampaignSubsystem::Get(&World);
		const bool bArrival = Campaign && Campaign->ConsumeArrivalFade(&World);
		StartFadeSeconds = CampaignRules::ResolveStartFadeSeconds(Level ? Level->Config.StartFadeSeconds : 0.f, bArrival);
		StartTitle = GetCurrentLevelTitle();
		if (StartFadeSeconds > 0.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] level start: fade in over %.1f s (%s%s)"), StartFadeSeconds,
				StartTitle.IsEmpty() ? TEXT("no title") : *StartTitle, bArrival ? TEXT(", campaign arrival") : TEXT(""));
			bStartFadeRunning = true;
			ShowStartOverlay();
		}
		return;
	}
	if (!bStartFadeRunning)
	{
		return;
	}
	StartElapsed += FMath::Min(static_cast<float>(FApp::GetDeltaTime()), LevelExitSubsystemLocal::MaxExitStepSeconds);
	if (GetStartFadeAlpha() <= 0.f && GetStartTitleAlpha() <= 0.f)
	{
		bStartFadeRunning = false;
		RemoveStartOverlay();
	}
}

void ULevelExitSubsystem::ShowStartOverlay()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport || StartOverlay.IsValid())
	{
		return; // headless (-nullrhi without a viewport): the timeline still runs (checks read GetStartFadeAlpha)
	}
	TWeakObjectPtr<ULevelExitSubsystem> WeakThis(this);
	FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 40);
	TitleFont.LetterSpacing = 180;
	StartOverlay = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor_Lambda([WeakThis]()
			{
				const ULevelExitSubsystem* Self = WeakThis.Get();
				return FSlateColor(FLinearColor(0.f, 0.f, 0.f, Self ? Self->GetStartFadeAlpha() : 0.f));
			})
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(StartTitle.ToUpper()))
			.Font(TitleFont)
			.ColorAndOpacity_Lambda([WeakThis]()
			{
				const ULevelExitSubsystem* Self = WeakThis.Get();
				return FSlateColor(FLinearColor(0.85f, 0.92f, 1.f, Self ? Self->GetStartTitleAlpha() : 0.f));
			})
		];
	StartOverlay->SetVisibility(EVisibility::HitTestInvisible); // the player may already click under the fading black
	Viewport->AddViewportWidgetContent(StartOverlay.ToSharedRef(), 9100);
}

void ULevelExitSubsystem::RemoveStartOverlay()
{
	if (!StartOverlay.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(StartOverlay.ToSharedRef());
		}
	}
	StartOverlay.Reset();
}

void ULevelExitSubsystem::ShowOverlay()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport || Overlay.IsValid())
	{
		return; // headless (-nullrhi): the timeline still runs (checks read GetFadeAlpha)
	}
	TWeakObjectPtr<ULevelExitSubsystem> WeakThis(this);
	FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 44);
	TitleFont.LetterSpacing = 200;
	FSlateFontInfo LevelFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	LevelFont.LetterSpacing = 160;
	Overlay = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor_Lambda([WeakThis]()
			{
				const ULevelExitSubsystem* Self = WeakThis.Get();
				return FSlateColor(FLinearColor(0.f, 0.f, 0.f, Self ? Self->GetFadeAlpha() : 1.f));
			})
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 0.f, 0.f, 12.f)
			[
				// 2026-10-22 b: the campaign title of the level ("0.0 FROZEN DAM"), empty outside the campaign.
				SNew(STextBlock)
				.Text(FText::FromString(ExitLevelTitle.ToUpper()))
				.Font(LevelFont)
				.ColorAndOpacity_Lambda([WeakThis]()
				{
					const ULevelExitSubsystem* Self = WeakThis.Get();
					return FSlateColor(FLinearColor(0.7f, 0.75f, 0.8f, Self ? Self->GetTitleAlpha() * 0.9f : 0.f));
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(MissionRules::GetMissionCompleteTitle())
				.Font(TitleFont)
				.ColorAndOpacity_Lambda([WeakThis]()
				{
					const ULevelExitSubsystem* Self = WeakThis.Get();
					return FSlateColor(FLinearColor(0.85f, 0.92f, 1.f, Self ? Self->GetTitleAlpha() : 0.f));
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.f, 14.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExitSubtitle", "The squad made it out."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
				.ColorAndOpacity_Lambda([WeakThis]()
				{
					const ULevelExitSubsystem* Self = WeakThis.Get();
					return FSlateColor(FLinearColor(0.7f, 0.75f, 0.8f, Self ? Self->GetTitleAlpha() * 0.9f : 0.f));
				})
			]
		];
	Overlay->SetVisibility(EVisibility::Visible); // above the HUD (z 9100) and eats the clicks: no more orders
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 9100);
}

void ULevelExitSubsystem::RemoveOverlay()
{
	if (!Overlay.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
	}
	Overlay.Reset();
}

#undef LOCTEXT_NAMESPACE
