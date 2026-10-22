#include "GameFlow/CampaignSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"

UCampaignSubsystem* UCampaignSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : Cast<UGameInstance>(Context);
	return GameInstance ? GameInstance->GetSubsystem<UCampaignSubsystem>() : nullptr;
}

const FCampaignConfig& UCampaignSubsystem::GetConfig()
{
	if (!bConfigLoaded)
	{
		bConfigLoaded = true;
		Config = FCampaignConfig();
		const FString Path = CampaignRules::GetCampaignFilePath();
		FString Json;
		FString Error;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			UE_LOG(LogCodexTactics, Log, TEXT("[Campaign] no campaign file at %s: every level exit returns to the main menu"), *Path);
		}
		else if (!CampaignRules::ParseCampaign(Json, Config, Error))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Campaign] %s: %s"), *Path, *Error);
			Config = FCampaignConfig();
		}
		else
		{
			UE_LOG(LogCodexTactics, Log, TEXT("[Campaign] %s: %d levels"), *Config.CampaignId, Config.Levels.Num());
		}
	}
	return Config;
}

const FCampaignLevel* UCampaignSubsystem::FindNextLevel(const UWorld* World)
{
	return World ? CampaignRules::FindNextLevel(GetConfig(), World->GetMapName()) : nullptr;
}

FString UCampaignSubsystem::GetLevelTitle(const FString& Map, const FString& Fallback)
{
	return CampaignRules::GetLevelTitle(GetConfig(), Map, Fallback);
}

FString UCampaignSubsystem::GetFirstLevelMap()
{
	const FCampaignConfig& Campaign = GetConfig();
	return Campaign.Levels.IsEmpty() ? FString() : Campaign.Levels[0].Map;
}

void UCampaignSubsystem::CaptureSquad(const UWorld* World, const FString& ToMap)
{
	Carry = FCampaignSquadCarry();
	if (!World)
	{
		return;
	}
	Carry.FromMap = CampaignRules::ShortMapName(World->GetMapName());
	Carry.ToMap = CampaignRules::ShortMapName(ToMap);
	for (TActorIterator<AOperativeCharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->bRecruited || It->IsKilledInAction())
		{
			Carry.Members.Add(CampaignCarry::Capture(**It));
		}
	}
	Carry.Members.Sort([](const FCarriedOperative& A, const FCarriedOperative& B) { return A.SquadIndex < B.SquadIndex; });
	bArrivalFadePending = true;
	for (const FCarriedOperative& Member : Carry.Members)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Campaign] %s -> %s: %s level %d, EXP %d, %d unspent points%s"), *Carry.FromMap, *Carry.ToMap,
			*Member.DisplayName, Member.Level, Member.CurrentExp, Member.UnspentStatPoints, Member.bKilledInAction ? TEXT(" (killed in action - stays behind)") : TEXT(""));
	}
}

const FCampaignSquadCarry* UCampaignSubsystem::GetCarryFor(const UWorld* World) const
{
	return World && Carry.IsSet() && CampaignRules::ShortMapName(World->GetMapName()).Equals(Carry.ToMap, ESearchCase::IgnoreCase) ? &Carry : nullptr;
}

bool UCampaignSubsystem::ConsumeArrivalFade(const UWorld* World)
{
	if (!bArrivalFadePending || !GetCarryFor(World))
	{
		return false;
	}
	bArrivalFadePending = false;
	return true;
}

void UCampaignSubsystem::Clear()
{
	Carry = FCampaignSquadCarry();
	bArrivalFadePending = false;
}
