#pragma once

#include "CoreMinimal.h"
#include "GameFlow/CampaignCarry.h"
#include "GameFlow/CampaignRules.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampaignSubsystem.generated.h"

class AOperativeCharacter;
class UWorld;

/**
 * The campaign across levels (game designer 2026-10-22: "0.0 Frozen Dam" -> "0.1 Frozen Factory"; UE-only). Reads the level order
 * and titles from Content/Data/Campaign/campaign.json (CampaignRules); holds the squad handed from the level just left to the next
 * one (CampaignCarry). The carry stays for its arrival map until NEW GAME or the next level transition, so a restart of the arrival
 * level (Ctrl + X, "Restart") brings the squad back as it arrived; a loaded save overwrites it with the saved values anyway.
 * Written by ULevelExitSubsystem right before it opens the next level; read by ACodexTacticsGameMode::SpawnSquad.
 */
UCLASS()
class CODEXTACTICS_API UCampaignSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The campaign of Context's game instance (null without one, e.g. in unit tests). */
	static UCampaignSubsystem* Get(const UObject* Context);

	/** The campaign data (loaded on first use; empty when the file is missing / invalid - logged once). */
	const FCampaignConfig& GetConfig();

	/** The level after World's map, nullptr at the end of the campaign / outside it. */
	const FCampaignLevel* FindNextLevel(const UWorld* World);

	/** The campaign title of a map ("0.1 Frozen Factory"); Fallback outside the campaign. */
	FString GetLevelTitle(const FString& Map, const FString& Fallback);

	/** The first level of the campaign (NEW GAME), empty without a campaign. */
	FString GetFirstLevelMap();

	/** Captures World's squad (every operative of the roster, the killed in action marked) for the trip to ToMap. */
	void CaptureSquad(const UWorld* World, const FString& ToMap);

	/** The carry for this world's map (nullptr when the squad did not arrive through the campaign). */
	const FCampaignSquadCarry* GetCarryFor(const UWorld* World) const;

	/** The carry, whatever its map (smokes / tests). */
	const FCampaignSquadCarry& GetCarry() const { return Carry; }

	/** Smokes / tests: replace the carry. */
	void SetCarry(const FCampaignSquadCarry& InCarry) { Carry = InCarry; bArrivalFadePending = InCarry.IsSet(); }

	/** A fresh arrival on World's map: true once (the start fade of the arrival), false afterwards / without a carry for it. */
	bool ConsumeArrivalFade(const UWorld* World);

	/** Forgets the carry (NEW GAME: a fresh run starts with the roster's squad). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Campaign")
	void Clear();

	/** Re-reads campaign.json on the next GetConfig (tests). */
	void ReloadConfig() { bConfigLoaded = false; }

private:
	FCampaignConfig Config;
	bool bConfigLoaded = false;
	FCampaignSquadCarry Carry;
	bool bArrivalFadePending = false;
};
