#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "CodexTactics.h"
#include "Core/MissionSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFlow/CampaignSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/Frontend/CodexFrontendPlayerController.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexSaveBridge.h"

UCodexFrontendSubsystem* UCodexFrontendSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCodexFrontendSubsystem>() : nullptr;
}

bool UCodexFrontendSubsystem::IsFrontendWorld(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	if (const AGameModeBase* GameMode = World->GetAuthGameMode(); GameMode && GameMode->IsA<ACodexFrontendGameMode>())
	{
		return true;
	}
	const FSoftObjectPath& Frontend = UCodexFrontendSettings::Get().FrontendLevel.ToSoftObjectPath();
	return !Frontend.IsNull() && UGameplayStatics::GetCurrentLevelName(World, true) == FPackageName::GetShortName(Frontend.GetLongPackageName());
}

void UCodexFrontendSubsystem::StartNewGame()
{
	if (UMissionSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>())
	{
		Session->bFrontendStart = true;
		Session->PendingLoadSlot.Reset();
		Session->PendingLoadDirectory.Reset();
		Session->bQuickRestart = false;
		Session->LastMode = EMissionStartMode::None;
	}
	// A fresh run starts at the default hour with a random weather: forget the previous run's end state (environment-weather.md, decision 17).
	if (UEnvironmentCarrySubsystem* Carry = GetGameInstance()->GetSubsystem<UEnvironmentCarrySubsystem>())
	{
		Carry->Clear();
	}
	// 2026-10-22 b: and the squad carried between campaign levels (a fresh run starts with the roster's squad).
	if (UCampaignSubsystem* Campaign = GetGameInstance()->GetSubsystem<UCampaignSubsystem>())
	{
		Campaign->Clear();
	}
	const FSoftObjectPath Level = GetNewGameLevel();
	UE_LOG(LogCodexTactics, Display, TEXT("Frontend: NEW GAME -> %s"), *Level.ToString());
	OpenLevel(Level);
}

FSoftObjectPath UCodexFrontendSubsystem::GetNewGameLevel() const
{
	if (!NewGameLevelOverride.IsNull())
	{
		return NewGameLevelOverride;
	}
	// User decision 2026-10-22: a new game starts the campaign at its first level ("0.0 Frozen Dam").
	if (UCampaignSubsystem* Campaign = GetGameInstance()->GetSubsystem<UCampaignSubsystem>())
	{
		const FString First = Campaign->GetFirstLevelMap();
		if (!First.IsEmpty())
		{
			return FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *First, *FPackageName::GetShortName(First)));
		}
	}
	return UCodexFrontendSettings::Get().NewGameLevel.ToSoftObjectPath();
}

bool UCodexFrontendSubsystem::ContinueLatest()
{
	const FString Latest = CodexSaveBridge::GetLatestSlot(GetGameInstance()->GetWorld());
	return !Latest.IsEmpty() && LoadSlotFromFrontend(Latest);
}

bool UCodexFrontendSubsystem::LoadSlotFromFrontend(const FString& SlotName)
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (SlotName.IsEmpty() || !CodexSaveBridge::SlotExists(World, SlotName))
	{
		return false;
	}
	UMissionSessionSubsystem* Session = GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>();
	if (Session)
	{
		Session->bFrontendStart = true;
		Session->bQuickRestart = false;
		Session->LastMode = EMissionStartMode::None;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Frontend: load '%s'"), *SlotName);
	UGameplayStatics::SetGamePaused(World, false);
	const bool bStarted = CodexSaveBridge::LoadWithTravel(World, SlotName);
	if (!bStarted && Session)
	{
		Session->bFrontendStart = false;
	}
	return bStarted;
}

void UCodexFrontendSubsystem::QuitToMainMenu()
{
	bSkipTitleOnce = true;
	UE_LOG(LogCodexTactics, Display, TEXT("Frontend: QUIT TO MAIN MENU"));
	OpenLevel(UCodexFrontendSettings::Get().FrontendLevel.ToSoftObjectPath());
}

void UCodexFrontendSubsystem::QuitGame()
{
	++QuitRequests;
	UE_LOG(LogCodexTactics, Display, TEXT("Frontend: QUIT GAME%s"), bQuitDisabled ? TEXT(" (disabled by a check)") : TEXT(""));
	if (bQuitDisabled)
	{
		return;
	}
	UWorld* World = GetGameInstance()->GetWorld();
	UKismetSystemLibrary::QuitGame(World, World ? World->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
}

bool UCodexFrontendSubsystem::ConsumeSkipTitle()
{
	const bool bSkip = bSkipTitleOnce;
	bSkipTitleOnce = false;
	return bSkip;
}

void UCodexFrontendSubsystem::OpenLevel(const FSoftObjectPath& Level)
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (!World || Level.IsNull())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Frontend: no level to open"));
		return;
	}
	UGameplayStatics::SetGamePaused(World, false);
	UGameplayStatics::OpenLevel(World, FName(*Level.GetLongPackageName()));
}
