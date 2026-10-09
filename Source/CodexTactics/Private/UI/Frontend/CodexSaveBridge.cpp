#include "UI/Frontend/CodexSaveBridge.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexSaveBridge"

USaveGameSubsystem* CodexSaveBridge::GetSaves(const UWorld* World)
{
	USaveGameSubsystem* Saves = World ? World->GetSubsystem<USaveGameSubsystem>() : nullptr;
	const UCodexFrontendSubsystem* Frontend = World ? UCodexFrontendSubsystem::Get(World) : nullptr;
	if (Saves && Frontend && !Frontend->SaveDirectoryOverride.IsEmpty())
	{
		Saves->SaveDirectoryOverride = Frontend->SaveDirectoryOverride;
	}
	return Saves;
}

TArray<FCodexSaveSlotView> CodexSaveBridge::ListSlots(const UWorld* World)
{
	TArray<FCodexSaveSlotView> Views;
	if (const USaveGameSubsystem* Saves = GetSaves(World))
	{
		for (const FSaveSlotInfo& Info : Saves->GetAllSaves())
		{
			FCodexSaveSlotView View;
			View.SlotName = Info.SlotName;
			View.Title = Info.Title.IsEmpty() ? Info.SlotName : Info.Title;
			View.DateTime = Info.DateTime;
			const FString Map = Info.MapName.IsEmpty() ? FString() : FPackageName::GetShortName(Info.MapName);
			View.Level = Map.IsEmpty() ? Info.StageName : FString::Printf(TEXT("%s - %s"), *Map, *Info.StageName);
			// The save metadata has no play time yet (TANDEM request «play time in the save metadata»).
			View.PlayTime = CodexFrontendRules::FormatPlayTime(-1.f);
			View.Timestamp = Info.Timestamp;
			Views.Add(View);
		}
	}
	return Views;
}

bool CodexSaveBridge::HasAnySave(const UWorld* World)
{
	return !GetLatestSlot(World).IsEmpty();
}

FString CodexSaveBridge::GetLatestSlot(const UWorld* World)
{
	const USaveGameSubsystem* Saves = GetSaves(World);
	return Saves ? Saves->GetContinueSlot() : FString();
}

bool CodexSaveBridge::SlotExists(const UWorld* World, const FString& SlotName)
{
	const USaveGameSubsystem* Saves = GetSaves(World);
	return Saves && !SlotName.IsEmpty() && Saves->HasSave(SlotName);
}

FString CodexSaveBridge::SuggestSlotName(const UWorld* World)
{
	const USaveGameSubsystem* Saves = GetSaves(World);
	return Saves ? Saves->SuggestNextSlotName() : FString();
}

bool CodexSaveBridge::SaveToSlot(UWorld* World, const FString& SlotName)
{
	USaveGameSubsystem* Saves = GetSaves(World);
	return Saves && GetSaveGate(World).bAllowed && Saves->SaveToSlotWithMessage(SlotName);
}

bool CodexSaveBridge::LoadSlot(UWorld* World, const FString& SlotName, bool& bOutTravelled)
{
	bOutTravelled = false;
	USaveGameSubsystem* Saves = GetSaves(World);
	FSaveSlotInfo Info;
	if (!Saves || !Saves->GetSaveInfo(SlotName, Info))
	{
		return false;
	}
	const FString CurrentMap = World->GetOutermost() ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
	const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
	const bool bSameLevel = Mission && Mission->IsMissionWorld() && (Info.MapName.IsEmpty() || Info.MapName == CurrentMap);
	if (bSameLevel)
	{
		return Saves->LoadFromSlotWithMessage(SlotName);
	}
	bOutTravelled = true;
	return LoadWithTravel(World, SlotName);
}

bool CodexSaveBridge::LoadWithTravel(UWorld* World, const FString& SlotName)
{
	USaveGameSubsystem* Saves = GetSaves(World);
	FSaveSlotInfo Info;
	if (!Saves || !Saves->GetSaveInfo(SlotName, Info))
	{
		return false;
	}
	const UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
	UMissionSessionSubsystem* Session = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	if (Info.MapName.IsEmpty() && Session && !(Mission && Mission->IsMissionWorld()))
	{
		// An old (version 1) save has no map and "the current level" is the menu map here: the campaign level instead,
		// with the same hand-over LoadGameWithTravel uses (the save subsystem applies the pending slot there).
		Session->PendingLoadSlot = SaveGameRules::SanitizeSlotName(SlotName);
		Session->PendingLoadDirectory = Saves->SaveDirectoryOverride;
		UGameplayStatics::OpenLevel(World, FName(*UCodexFrontendSettings::Get().NewGameLevel.ToSoftObjectPath().GetLongPackageName()));
		return true;
	}
	return Saves->LoadGameWithTravel(SlotName);
}

bool CodexSaveBridge::DeleteSlot(const UWorld* World, const FString& SlotName)
{
	const USaveGameSubsystem* Saves = GetSaves(World);
	return Saves && Saves->DeleteSave(SlotName);
}

FCodexSaveGate CodexSaveBridge::GetSaveGate(const UWorld* World)
{
	FCodexSaveGate Gate;
	const UMissionSubsystem* Mission = World ? World->GetSubsystem<UMissionSubsystem>() : nullptr;
	const USaveGameSubsystem* Saves = GetSaves(World);
	if (!Saves || !Mission || !Mission->IsMissionWorld() || UCodexFrontendSubsystem::IsFrontendWorld(World))
	{
		Gate.bAllowed = false;
		Gate.Reason = LOCTEXT("NoMission", "Nothing to save here");
		return Gate;
	}
	Gate.bAllowed = Saves->CanSaveNow(&Gate.Reason);
	return Gate;
}

#undef LOCTEXT_NAMESPACE
