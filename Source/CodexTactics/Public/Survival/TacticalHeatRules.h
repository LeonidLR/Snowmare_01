#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "TacticalHeatRules.generated.h"

/** What one squad member starts the mission with (Sprint 15; tactical_heat.json "starting_loadout"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTacticalHeatLoadout
{
	GENERATED_BODY()

	FTacticalHeatLoadout() = default;
	FTacticalHeatLoadout(int32 InHeatPacks, int32 InFirewood, int32 InFlares, int32 InStoves)
		: HeatPacks(InHeatPacks), Firewood(InFirewood), Flares(InFlares), Stoves(InStoves)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0"))
	int32 HeatPacks = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0"))
	int32 Firewood = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0"))
	int32 Flares = 0;

	/** Portable gas stoves (at most StoveMaxCarried); each starts with a full canister. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Heat", meta = (ClampMin = "0"))
	int32 Stoves = 0;
};

/**
 * Mobile & tactical heat (Sprint 15, TANDEM request #14, UE-only — no Godot reference). The defaults are Gemini's spec values;
 * the game reads Content/Data/Survival/tactical_heat.json over them (Data/TacticalHeatTuning, Unreal is the master copy).
 * Distances in cm, times in seconds, cold in percentage points (the operative's ColdLevel 0..100).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTacticalHeatConfig
{
	GENERATED_BODY()

	// --- Chemical heat pack (EPersonalItem::HeatPack) ---

	/** Cold taken off at once, points (35 % of the 0..100 cold scale; like the food items' "25 % cold off"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeatPack", meta = (ClampMin = "0", ClampMax = "100"))
	float HeatPackColdReduction = 35.f;

	/** "Heat buffer" after a pack: the open-air cold accumulation is multiplied by HeatPackAccumulationMultiplier this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeatPack", meta = (ClampMin = "0"))
	float HeatPackBufferSeconds = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeatPack", meta = (ClampMin = "0", ClampMax = "1"))
	float HeatPackAccumulationMultiplier = 0.5f;

	/** Turn-based cost of using a pack (on oneself or on an adjacent mate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeatPack", meta = (ClampMin = "0"))
	int32 HeatPackActionPoints = 1;

	/** One inventory slot holds this many. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeatPack", meta = (ClampMin = "1"))
	int32 HeatPackMaxStack = 3;

	// --- Campfire (ACampfireActor, EDeployableType::Campfire) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0"))
	float CampfireHeatRadius = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0.1"))
	float CampfireBurnSeconds = 75.f;

	/** Turn-based: the fire lasts this many squad rounds (one round = CampfireBurnSeconds / rounds of the burn time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "1"))
	int32 CampfireTurnRounds = 4;

	/** Arctic wind on high ground: the burn time is multiplied by this (-20 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0.05", ClampMax = "1"))
	float CampfireWindBurnMultiplier = 0.8f;

	/** Ground height (world Z of the fire's base) from which the wind blows (the cold system's ElevatedHeight, 140). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire")
	float CampfireWindHeight = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0"))
	int32 CampfireMatchesCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0"))
	int32 CampfireFirewoodCost = 1;

	/** Instead of firewood: this many units of flamethrower fuel (the "Fuel" alternative of the spec). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "1"))
	int32 CampfireFlameFuelCost = 10;

	/** Crackle + smoke column: patrols within this radius start a search at the fire (repeated every interval). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0"))
	float CampfireNoiseRadius = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0.5"))
	float CampfireNoiseIntervalSeconds = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campfire", meta = (ClampMin = "0"))
	int32 CampfireBuildActionPoints = 3;

	// --- Portable gas stove (APortableStoveActor, EDeployableType::PortableStove) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "0"))
	float StoveHeatRadius = 220.f;

	/** A full gas canister burns this long (the rest is kept with the packed stove). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "1"))
	float StoveGasCapacitySeconds = 180.f;

	/** Turn-based: gas burnt per squad round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "0"))
	float StoveGasSecondsPerRound = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "0"))
	int32 StoveDeployActionPoints = 2;

	/** Crouched work of «Pack up» (the hold of the interaction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "0"))
	float StovePackUpSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stove", meta = (ClampMin = "1"))
	int32 StoveMaxCarried = 1;

	// --- Signal flare (ASignalFlareActor) ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0"))
	float FlareHeatRadius = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0.1"))
	float FlareBurnSeconds = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "1"))
	int32 FlareTurnRounds = 2;

	/** Standing throw range (stance scales it like a grenade: crouched 75 % = 15 m, prone 50 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0"))
	float FlareThrowRange = 2000.f;

	/** A click this close to the thrower lights the flare at his feet instead of throwing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0"))
	float FlareAtFeetDistance = 200.f;

	/** The magnesium light shows every enemy within this radius to the squad (line of sight rules). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0"))
	float FlareRevealRadius = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flare", meta = (ClampMin = "0"))
	int32 FlareMaxCarried = 3;

	// --- Residual fire puddle (AFirePuddleActor) ---

	/** Core: fire damage + Burning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleCoreRadius = 120.f;

	/** Radiant ring up to here: no damage, warming at PuddleWarmthRate, frozen weapons thaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleHeatRadius = 350.f;

	/** Cold loss in the ring, %/s (the strongest warming of all sources; a plain source warms at FColdConfig::WarmthRecoveryRate 8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleWarmthRate = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0.1"))
	float PuddleLifetimeMinSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0.1"))
	float PuddleLifetimeMaxSeconds = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "1"))
	int32 PuddleTurnRounds = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleCoreDamagePerSecond = 10.f;

	/** The core hurts in pulses this far apart (one pulse = PuddleCoreDamagePerSecond * interval). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0.1"))
	float PuddleDamageIntervalSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleBurningSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle", meta = (ClampMin = "0"))
	float PuddleBurningTickDamage = 5.f;

	/** An exploding fuel barrel leaves a puddle at its foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirePuddle")
	bool bBarrelLeavesPuddle = true;

	// --- Warming at a heat source and the weather idle poses (user request 2026-10-12, Survival/WarmIdleRules) ---

	/** Warming at a heat source (barrel, campfire, stove, generator, flare, puddle ring) outside a wave fight: x this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	float ExplorationWarmthMultiplier = 1.f;

	/** Warming at a heat source during a wave fight / ambush (any combat mode): x this - the squad has to manoeuvre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	float CombatWarmthMultiplier = 0.5f;

	/** Exploration, standing still out of any warm zone: the cold idle (shivering) starts from this cold (the Chills tier, 40). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0", ClampMax = "100"))
	float WarmIdleColdThreshold = 40.f;

	/** The cold idle ends this far below the threshold (no flicker at the edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	float WarmIdleColdHysteresis = 5.f;

	/** Seconds an operative must stand idle (no order, no move, no task) before a weather pose starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	float WarmIdleDelaySeconds = 2.f;

	/** At a campfire / stove at least this many of the operatives present sit (the coldest), the rest stand and warm their hands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	int32 WarmMinSitters = 2;

	/** ... and every further operative at least this cold sits too (inner seats permitting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0", ClampMax = "100"))
	float WarmExtraSitterCold = 50.f;

	/** Distance of the inner (sitting) ring / outer (standing) ring from a campfire's centre, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float CampfireSeatDistance = 115.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float CampfireStandDistance = 175.f;

	/** The same rings around a gas stove (a smaller flame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float StoveSeatDistance = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float StoveStandDistance = 150.f;

	/** Hand-warming spots around a burning barrel (hands over the rim of the 1.2 m drum), cm from its centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float BarrelStandDistance = 125.f;

	/** Least distance between two spots on a ring (seats / standing places), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "30"))
	float WarmSpotSpacing = 85.f;

	/** The operative counts as at his spot within this distance, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "5"))
	float WarmSpotArriveTolerance = 35.f;

	/** Walk speed to a spot, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "10"))
	float WarmSpotWalkSpeed = 160.f;

	/**
	 * Squad gathers at the fire (user decision 2026-10-12): in exploration formation, while the leader sits / stands at a warm spot of a
	 * barrel / campfire / stove, the followers walk to that source's ring instead of their formation slots (WarmIdleRules::ShouldPullFollower).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle")
	bool bWarmPullFollowers = true;

	/** A follower farther than this from the source (2D) is not pulled in, cm (15 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmIdle", meta = (ClampMin = "0"))
	float WarmPullMaxDistance = 1500.f;

	// --- Common ---

	/** Extra distance counted as inside a new source's warm zone (the old sources keep Godot's 50 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (ClampMin = "0"))
	float HeatTolerance = 0.f;

	/** Per role: Commander, Engineer, MedicSapper, Recruit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loadout")
	TArray<FTacticalHeatLoadout> StartingLoadout = {
		FTacticalHeatLoadout(1, 1, 1, 0), // Commander
		FTacticalHeatLoadout(1, 1, 0, 1), // Engineer: carries the stove
		FTacticalHeatLoadout(2, 0, 1, 0), // Medic-Sapper
		FTacticalHeatLoadout(0, 0, 0, 0) }; // Recruit
};

/** Burn timer of a heat source: lit -> burning (TimeLeft) -> burnt out. Gas of a stove uses the same state. */
struct CODEXTACTICS_API FHeatBurnState
{
	bool bBurning = false;
	/** Burnt out / out of gas (a campfire turns to ashes, a flare is spent). */
	bool bBurntOut = false;
	float TimeLeft = 0.f;

	/** Starts burning for Seconds (relights a burnt-out source). False for Seconds <= 0. */
	bool Ignite(float Seconds);

	/** Real-time step; true when the fire just went out. */
	bool Step(float DeltaSeconds);

	/** One turn-based round burns SecondsPerRound of the time; true when the fire just went out. */
	bool AdvanceRound(float SecondsPerRound);

	/** Puts it out without burning out (a stove packed up: the rest stays). */
	void Stop();
};

/** Zones of a residual fire puddle. */
enum class EFirePuddleZone : uint8
{
	Core,
	Radiant,
	Outside
};

/** Outcome of a heat pack on one operative. */
struct CODEXTACTICS_API FHeatPackResult
{
	float NewCold = 0.f;
	bool bClearsFrostbite = false;
	float BufferSeconds = 0.f;
};

/** Pure rules of the Sprint 15 heat sources (tests: CodexTactics.Survival.TacticalHeat.*). */
namespace TacticalHeatRules
{
	/** A heat pack helps when the operative is cold or frostbitten. */
	CODEXTACTICS_API bool CanUseHeatPack(float Cold, bool bFrostbitten);

	/** Cold - reduction (>= 0), frostbite cleared, the buffer restarts at its full length. */
	CODEXTACTICS_API FHeatPackResult ApplyHeatPack(const FTacticalHeatConfig& Config, float Cold, bool bFrostbitten);

	/** Open-air accumulation multiplier while a heat buffer runs (1 without one). */
	CODEXTACTICS_API float GetBufferAccumulationMultiplier(const FTacticalHeatConfig& Config, float BufferSecondsLeft);

	/** Campfire burn time: CampfireBurnSeconds, x CampfireWindBurnMultiplier on high ground. */
	CODEXTACTICS_API float GetCampfireBurnSeconds(const FTacticalHeatConfig& Config, bool bWindExposed);

	/** The fire's base is at or above CampfireWindHeight. */
	CODEXTACTICS_API bool IsWindExposed(const FTacticalHeatConfig& Config, float GroundZ);

	/** Burn time one turn-based round takes off: total / rounds (rounds clamped to >= 1). */
	CODEXTACTICS_API float SecondsPerRound(float TotalSeconds, int32 Rounds);

	/**
	 * Burn timers and gas advance in real time only: not in the tactical pause (Space, also the free horde pause and its slow-motion
	 * flyover), not in turn-based combat (rounds drive them there) and not while a dialogue / cutscene holds the world.
	 */
	CODEXTACTICS_API bool ShouldAdvanceRealTime(ECodexCombatMode CombatMode, bool bWorldAIPaused);

	/** Distance <= Radius + Tolerance (inclusive edge). */
	CODEXTACTICS_API bool IsInHeatRadius(float Distance, float Radius, float Tolerance = 0.f);

	/** Core (<= core radius), radiant ring (<= heat radius), outside. */
	CODEXTACTICS_API EFirePuddleZone ClassifyPuddleZone(const FTacticalHeatConfig& Config, float Distance);

	/** Puddle lifetime for a 0..1 roll between the min and max. */
	CODEXTACTICS_API float GetPuddleLifetime(const FTacticalHeatConfig& Config, float Roll01);

	/** A flare's light shows this enemy (burning and within the reveal radius). */
	CODEXTACTICS_API bool IsRevealedByFlare(const FTacticalHeatConfig& Config, bool bFlareBurning, float Distance);

	/** Throw range of a flare by stance (GrenadeRules::EffectiveRange of FlareThrowRange). */
	CODEXTACTICS_API float GetFlareThrowRange(const FTacticalHeatConfig& Config, EOperativeStance Stance);

	/** How many campfires these materials make (min of matches and firewood + fuel / fuel cost). */
	CODEXTACTICS_API int32 GetCraftableCampfires(const FTacticalHeatConfig& Config, int32 Matches, int32 Firewood, int32 FlameFuel);

	/** Materials spent on one campfire: firewood first, else flamethrower fuel. False (nothing spent) without enough. */
	CODEXTACTICS_API bool ConsumeCampfireMaterials(const FTacticalHeatConfig& Config, int32& Matches, int32& Firewood, int32& FlameFuel);

	/** Loadout of a role index (0 commander, 1 engineer, 2 medic-sapper, 3 recruit); empty when the table is shorter. */
	CODEXTACTICS_API FTacticalHeatLoadout GetLoadout(const FTacticalHeatConfig& Config, int32 RoleIndex);
}
