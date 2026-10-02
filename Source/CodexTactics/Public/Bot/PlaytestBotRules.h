#pragma once

#include "CoreMinimal.h"

/**
 * Playtest bot decisions (Godot archive tools/bot/bot_driver.gd + smart_tactical_bot.gd; Unreal is the reference
 * since 2026-10-02). Distances in cm.
 */
enum class EBotProfile : uint8
{
	Casual,
	Normal,
	Veteran
};

/** What a profile does on its own (bot_driver.gd _veteran_* / _normal_*). */
struct CODEXTACTICS_API FBotProfileConfig
{
	/** Deploys at the first preparation: turrets / barricades / mines (0 = none). */
	int32 Turrets = 0;
	int32 Barricades = 0;
	int32 Mines = 0;
	/** The engineer and the medic crouch and guard after deploying (veteran). */
	bool bGuardAfterDeploy = false;
	/** A member below this health fraction uses a medkit (0 = never). **Deviation**: Godot spent a pause charge to heal +35 HP. */
	float HealBelowHealthFraction = 0.f;
	/** A member above this cold uses warming food (101 = never). **Deviation**: Godot just lowered the cold by 40 / 35. */
	float WarmAboveCold = 101.f;
};

namespace PlaytestBotRules
{
	/** "CASUAL" / "NORMAL" / "REGULAR" / "VETERAN" (any case); unknown -> Normal. */
	CODEXTACTICS_API EBotProfile ParseProfile(const FString& Name);
	CODEXTACTICS_API FString ProfileName(EBotProfile Profile);
	CODEXTACTICS_API FBotProfileConfig GetProfileConfig(EBotProfile Profile);

	/** smart_tactical_bot _find_enemy_cluster: the enemy with most others within Radius; count and their centre. */
	CODEXTACTICS_API int32 FindCluster(const TArray<FVector>& Enemies, float Radius, FVector& OutCenter);

	/** A grenade at a cluster of >= 3 between 3 m and the throw range (smart_tactical_bot step 1). */
	CODEXTACTICS_API bool ShouldThrowGrenade(int32 ClusterCount, float DistanceCm, float ThrowRangeCm);

	/** _find_fallback_position: 5 m away from the threat, pulled towards the rear (RearDirection, planar). */
	CODEXTACTICS_API FVector FallbackPosition(const FVector& Position, const FVector& Threat, const FVector& RearDirection);

	/** _evaluate_best_cover score: 100 + 25 elevated + 30 x cover health - 2.5 per metre to the stand point. */
	CODEXTACTICS_API float CoverScore(float DistanceToStandCm, float CoverHealthFraction, bool bElevated);

	/** The stand point 1.5 m behind a cover, away from the threat centre. */
	CODEXTACTICS_API FVector CoverStandPoint(const FVector& Cover, const FVector& ThreatCenter);
}
