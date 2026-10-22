#include "GameFlow/CampaignRules.h"

#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString CampaignRules::GetCampaignFilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/Campaign/campaign.json"));
}

bool CampaignRules::ParseCampaign(const FString& Json, FCampaignConfig& OutConfig, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("invalid JSON");
		return false;
	}
	FCampaignConfig Config;
	Root->TryGetStringField(TEXT("campaign_id"), Config.CampaignId);
	const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
	if (!Root->TryGetArrayField(TEXT("levels"), Levels) || Levels->IsEmpty())
	{
		OutError = TEXT("no \"levels\"");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Levels)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		FCampaignLevel Level;
		if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("map"), Level.Map) || Level.Map.IsEmpty())
		{
			OutError = FString::Printf(TEXT("level %d has no \"map\""), Config.Levels.Num());
			return false;
		}
		if (FindLevelIndex(Config, Level.Map) != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("map %s is listed twice"), *Level.Map);
			return false;
		}
		Entry->TryGetStringField(TEXT("id"), Level.Id);
		if (!Entry->TryGetStringField(TEXT("title"), Level.Title) || Level.Title.IsEmpty())
		{
			Level.Title = ShortMapName(Level.Map);
		}
		Config.Levels.Add(MoveTemp(Level));
	}
	OutConfig = MoveTemp(Config);
	return true;
}

FString CampaignRules::ShortMapName(const FString& MapOrPath)
{
	FString Name = FPackageName::GetShortName(MapOrPath); // "/Game/Maps/L_X.L_X" -> "L_X.L_X"
	int32 Dot = INDEX_NONE;
	if (Name.FindChar(TEXT('.'), Dot))
	{
		Name.LeftInline(Dot);
	}
	// PIE worlds: "UEDPIE_<n>_L_X".
	if (Name.StartsWith(TEXT("UEDPIE_")))
	{
		const int32 Underscore = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (Underscore != INDEX_NONE)
		{
			Name.RightChopInline(Underscore + 1);
		}
	}
	return Name;
}

int32 CampaignRules::FindLevelIndex(const FCampaignConfig& Config, const FString& Map)
{
	const FString Short = ShortMapName(Map);
	if (Short.IsEmpty())
	{
		return INDEX_NONE;
	}
	return Config.Levels.IndexOfByPredicate([&Short](const FCampaignLevel& Level)
	{
		return ShortMapName(Level.Map).Equals(Short, ESearchCase::IgnoreCase);
	});
}

const FCampaignLevel* CampaignRules::FindNextLevel(const FCampaignConfig& Config, const FString& Map)
{
	const int32 Index = FindLevelIndex(Config, Map);
	return Index != INDEX_NONE && Config.Levels.IsValidIndex(Index + 1) ? &Config.Levels[Index + 1] : nullptr;
}

FString CampaignRules::GetLevelTitle(const FCampaignConfig& Config, const FString& Map, const FString& Fallback)
{
	const int32 Index = FindLevelIndex(Config, Map);
	return Index != INDEX_NONE ? Config.Levels[Index].Title : Fallback;
}

bool CampaignRules::AnyUnspentPoints(TConstArrayView<FPointsReviewMember> Members)
{
	return !GetReviewIndices(Members).IsEmpty();
}

TArray<int32> CampaignRules::GetReviewIndices(TConstArrayView<FPointsReviewMember> Members)
{
	TArray<int32> Indices;
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		if (Members[Index].bAlive && Members[Index].UnspentPoints > 0)
		{
			Indices.Add(Index);
		}
	}
	return Indices;
}

ELevelExitNext CampaignRules::DecideAfterExit(bool bHasNextLevel, bool bAnyUnspentPoints)
{
	if (!bHasNextLevel)
	{
		return ELevelExitNext::MainMenu;
	}
	return bAnyUnspentPoints ? ELevelExitNext::PointsScreen : ELevelExitNext::NextLevel;
}

float CampaignRules::ResolveStartFadeSeconds(float LevelStartFadeSeconds, bool bCampaignArrival)
{
	if (LevelStartFadeSeconds > 0.f)
	{
		return LevelStartFadeSeconds;
	}
	return bCampaignArrival ? ArrivalFadeSeconds : 0.f;
}

float CampaignRules::StartFadeAlpha(float ElapsedSeconds, float FadeSeconds)
{
	if (FadeSeconds <= 0.f)
	{
		return 0.f;
	}
	const float T = FMath::Clamp((ElapsedSeconds - StartHoldSeconds) / FadeSeconds, 0.f, 1.f);
	return 1.f - FMath::SmoothStep(0.f, 1.f, T);
}

float CampaignRules::StartTitleAlpha(float ElapsedSeconds, float FadeSeconds)
{
	if (FadeSeconds <= 0.f)
	{
		return 0.f;
	}
	const float T = FMath::Clamp((ElapsedSeconds - StartHoldSeconds) / (FadeSeconds * 0.5f), 0.f, 1.f);
	return 1.f - T;
}
