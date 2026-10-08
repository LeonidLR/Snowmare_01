#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "CodexTactics.h"
#include "Core/MissionSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
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
	UE_LOG(LogCodexTactics, Display, TEXT("Frontend: NEW GAME"));
	OpenLevel(UCodexFrontendSettings::Get().NewGameLevel.ToSoftObjectPath());
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
