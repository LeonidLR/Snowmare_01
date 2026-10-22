#pragma once

#include "CoreMinimal.h"
#include "CampaignRules.generated.h"

/** One level of the campaign (Content/Data/Campaign/campaign.json "levels"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCampaignLevel
{
	GENERATED_BODY()

	/** Short id, e.g. "0.0". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString Id;

	/** The map's long package name, e.g. "/Game/Maps/L_Demo01_A_FrozenLake". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString Map;

	/** Player-facing title (loading card, mission complete, save slots), e.g. "0.0 Frozen Dam". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString Title;
};

/** The ordered level list of the campaign. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCampaignConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString CampaignId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<FCampaignLevel> Levels;
};

/** What follows the black screen of a level exit. */
UENUM(BlueprintType)
enum class ELevelExitNext : uint8
{
	/** No campaign level after this one: back to the main menu (the 2026-10-22 c behaviour). */
	MainMenu,
	/** A next level and somebody alive has unspent stat points: the points screen first. */
	PointsScreen,
	/** A next level, nobody has points left: load it at once. */
	NextLevel
};

/** One squad member for the points-screen gate. */
struct CODEXTACTICS_API FPointsReviewMember
{
	/** Alive and in the squad (the killed in action never get the screen). */
	bool bAlive = true;
	int32 UnspentPoints = 0;
};

/**
 * Pure rules of the campaign (game designer 2026-10-22: "make the next level now ... it's our first tutorial level; the second level is
 * Frozen Factory"; "if the operatives have unspent points, after the black screen a menu with the character's image and the points,
 * then the new level opens"). The level order lives in Content/Data/Campaign/campaign.json; ULevelExitSubsystem decides after the
 * gate exit's black screen, UCampaignSubsystem carries the squad. UE-only (the Godot archive had no level-to-level flow).
 */
namespace CampaignRules
{
	/** The level start's black hold before the fade-in begins (the camera settles on the squad behind it), real seconds. */
	constexpr float StartHoldSeconds = 0.6f;

	/** The fade-in of a campaign arrival on a level whose JSON sets no "start_fade_seconds". */
	constexpr float ArrivalFadeSeconds = 1.5f;

	/** Content/Data/Campaign/campaign.json (absolute). */
	CODEXTACTICS_API FString GetCampaignFilePath();

	/**
	 * Parses the campaign JSON: {"campaign_id": "...", "levels": [{"id", "map", "title"}, ...]}. A level needs a "map"; a missing title
	 * falls back to the map's short name. False (with OutError) on invalid JSON, no levels or a duplicate map.
	 */
	CODEXTACTICS_API bool ParseCampaign(const FString& Json, FCampaignConfig& OutConfig, FString& OutError);

	/** "L_X" from "/Game/Maps/L_X", "/Game/Maps/L_X.L_X", "UEDPIE_0_L_X" or "L_X". */
	CODEXTACTICS_API FString ShortMapName(const FString& MapOrPath);

	/** Index of the level whose map is Map (any form ShortMapName takes, case-insensitive), INDEX_NONE when it is not in the campaign. */
	CODEXTACTICS_API int32 FindLevelIndex(const FCampaignConfig& Config, const FString& Map);

	/** The level after Map, nullptr at the end of the campaign / for a map outside it. */
	CODEXTACTICS_API const FCampaignLevel* FindNextLevel(const FCampaignConfig& Config, const FString& Map);

	/** The campaign title of Map ("0.0 Frozen Dam"), Fallback for a map outside the campaign. */
	CODEXTACTICS_API FString GetLevelTitle(const FCampaignConfig& Config, const FString& Map, const FString& Fallback);

	/** Somebody alive has unspent stat points. */
	CODEXTACTICS_API bool AnyUnspentPoints(TConstArrayView<FPointsReviewMember> Members);

	/** Indices of the members the points screen shows (alive, points > 0), in squad order. */
	CODEXTACTICS_API TArray<int32> GetReviewIndices(TConstArrayView<FPointsReviewMember> Members);

	/** After the exit's black screen: the main menu (no next level), the points screen (points left) or the next level. */
	CODEXTACTICS_API ELevelExitNext DecideAfterExit(bool bHasNextLevel, bool bAnyUnspentPoints);

	/** The fade-in length of a level start: the level JSON's "start_fade_seconds", else ArrivalFadeSeconds on a campaign arrival, else 0. */
	CODEXTACTICS_API float ResolveStartFadeSeconds(float LevelStartFadeSeconds, bool bCampaignArrival);

	/** Black overlay alpha of the level start: 1 during the hold, then down to 0 over FadeSeconds (smooth); 0 when FadeSeconds <= 0. */
	CODEXTACTICS_API float StartFadeAlpha(float ElapsedSeconds, float FadeSeconds);

	/** The level title card's alpha on the start fade: shown in the hold, gone by the middle of the fade. */
	CODEXTACTICS_API float StartTitleAlpha(float ElapsedSeconds, float FadeSeconds);
}
