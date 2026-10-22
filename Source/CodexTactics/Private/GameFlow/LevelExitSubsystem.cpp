#include "GameFlow/LevelExitSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/GateActor.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelExitSubsystem"

namespace LevelExitSubsystemLocal
{
	/** A load hitch must not jump the fade (same cap as the game flow's timers). */
	constexpr float MaxExitStepSeconds = 0.25f;

	bool IsLiving(const AOperativeCharacter* Operative)
	{
		return Operative && !Operative->IsKilledInAction() && (!Operative->HealthComponent || Operative->HealthComponent->GetCurrentHealth() > 0.f);
	}
}

void ULevelExitSubsystem::Deinitialize()
{
	RemoveOverlay();
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

void ULevelExitSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	if (bExiting)
	{
		ExitElapsed += FMath::Min(static_cast<float>(FApp::GetDeltaTime()), LevelExitSubsystemLocal::MaxExitStepSeconds);
		const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		if (!bReturnRequested && GateExitRules::ShouldReturnToMenu(ExitElapsed, ArmedConfig, !Mission || Mission->IsHeadlessStart()))
		{
			bReturnRequested = true;
			if (UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(this))
			{
				Frontend->QuitToMainMenu();
			}
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
	UE_LOG(LogCodexTactics, Display, TEXT("[LevelExit] all %d living operatives are out: fade to black (%.1f s), mission complete"), RequiredCount,
		ArmedConfig.FadeSeconds);
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->CompleteMission();
	}
	ShowOverlay();
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
