#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

class UGodotBalanceAsset;

/** Godot ai_grenade_* (player.gd defaults, game_balance_config.gd). */
struct CODEXTACTICS_API FAIGrenadeConfig
{
	/** Enemies within the blast radius of the aim point needed to throw. */
	int32 MinCluster = 3;
	/** Seconds between autonomous throws. */
	float Cooldown = 3.f;
	/** The aim point stays at least this far from the thrower, cm. */
	float MinThrowerDistance = 500.f;
	/** No squad member / ally within this distance of the aim point, cm. */
	float FriendlyFireRadius = 500.f;
};

/** Result of Godot _evaluate_ai_grenade_opportunity. */
struct CODEXTACTICS_API FAIGrenadeOpportunity
{
	bool bCanThrow = false;
	FVector Target = FVector::ZeroVector;
	int32 EnemyCount = 0;
};

/** Autonomous grenade throws of the squad AI (Godot Scenes/movements/player.gd _evaluate_ai_grenade_opportunity). */
namespace AIGrenadeRules
{
	CODEXTACTICS_API FAIGrenadeConfig ConfigFromBalance(const UGodotBalanceAsset* Balance);

	/**
	 * The enemy position with the most enemies within BlastRadius (at least MinCluster), inside the stance throw range
	 * (100 / 75 / 50 %), at least MinThrowerDistance from the thrower and with no ally within FriendlyFireRadius.
	 * Godot global_position: the thrower / allies at their centre (1 m above the feet), enemies at their feet; distances
	 * are 3D like Godot.
	 */
	CODEXTACTICS_API FAIGrenadeOpportunity Evaluate(const FAIGrenadeConfig& Config, const FVector& Thrower, EOperativeStance Stance,
		float ThrowRange, float BlastRadius, const TArray<FVector>& Enemies, const TArray<FVector>& Allies);
}
