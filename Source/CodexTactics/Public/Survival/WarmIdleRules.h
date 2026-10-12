#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "WarmIdleRules.generated.h"

struct FTacticalHeatConfig;

/**
 * Weather idle pose of one operative (user request 2026-10-12; clips: /Game/90+_Weather_Animations NEW_version/Cold, retargeted onto
 * the operative skeletons by Scripts/Editor/setup_operative_weather_animation.py). Cosmetic: never blocks an order.
 */
UENUM(BlueprintType)
enum class EWarmIdlePose : uint8
{
	None,
	/** Shivering on the spot (Cold_1): cold, idle, no heat around. */
	ColdIdle,
	/** Standing, hands held out to the fire (Cold_Fire_1): a burning barrel, the outer ring of a campfire / stove. */
	HandWarm,
	/** Sitting on the ground at the fire (Cold_Fire_2): the inner ring of a campfire / stove. */
	SitWarm
};

/** What kind of heat source warms the operative (decides the pose). */
UENUM(BlueprintType)
enum class EWarmHeatKind : uint8
{
	None,
	Barrel,
	Campfire,
	Stove,
	/** Generator, flare, fire-puddle ring: warms, no pose. */
	Other
};

/** Step of a weather clip set: start -> loop A (x n) -> transit -> loop B (x n) -> transit -> loop A ... -> end. */
UENUM(BlueprintType)
enum class EWarmClipStep : uint8
{
	None,
	Start,
	LoopA,
	TransitAB,
	LoopB,
	TransitBA,
	End
};

/** Role of an operative at a campfire / stove. */
enum class EWarmFireRole : uint8
{
	Unassigned,
	Sit,
	Stand
};

/** Why a weather pose ends. */
enum class EWarmExitReason : uint8
{
	/** An order / any movement: orders take priority at once. */
	Moved,
	/** A threat spotted / under fire. */
	Threat,
	/** Knocked down, dead, in cover, vaulting: another layer owns the body now. */
	BodyTaken,
	/** The fight started (no weather poses in combat). */
	CombatStarted,
	/** Warm enough / the fire went out / the pose is no longer wanted while standing still. */
	NoLongerWanted
};

/** How a weather pose ends. */
enum class EWarmExitStyle : uint8
{
	/** The set's End clip (stands up / lowers the arms), when the set has one. */
	PlayEnd,
	/** A short blend-out (WarmIdleInterruptBlendSeconds). */
	QuickBlend,
	/** Cut at once (the next layer starts its own clip). */
	Cut
};

/** Everything the pose decision reads about one operative (gathered by UWarmIdleSubsystem). */
struct CODEXTACTICS_API FWarmIdleInput
{
	/** Exploration (or the post-combat return): see WarmIdleRules::IsExplorationPhase. */
	bool bExploration = true;
	bool bAlive = true;
	bool bKnockedDown = false;
	bool bFrostbitten = false;
	bool bInCover = false;
	bool bCarrying = false;
	bool bVaulting = false;
	/** Interaction / relocation / deploy task of his own, guard post, panic, rage. */
	bool bBusy = false;
	/** Moving or ordered to move (the walk to his own warm spot does not count). */
	bool bMoving = false;
	/** An enemy spotted / a combat target / hit recently. */
	bool bThreat = false;
	EOperativeStance Stance = EOperativeStance::Standing;
	float Cold = 0.f;
	/** Seconds standing idle (reset by any order or move). */
	float IdleSeconds = 0.f;
	/** The heat source covering him. */
	EWarmHeatKind Heat = EWarmHeatKind::None;
	/** Already in the cold idle (hysteresis). */
	bool bInColdIdle = false;
};

/** What the squad's "gather at the fire" pull reads about one follower (USquadSubsystem / UWarmIdleSubsystem::GetFollowerPullTarget). */
struct CODEXTACTICS_API FWarmPullInput
{
	/** Exploration (WarmIdleRules::IsExplorationPhase): never in a fight / ambush / pause / turn-based / cutscene. */
	bool bExploration = true;
	/** In the formation: not solo mode, not a guard (T), the followers are not held. */
	bool bInFormation = true;
	/** He was given an explicit order since the pull began (or while this fire burned): he leaves the pull until the leader leaves the fire. */
	bool bOrdered = false;
	/** The leader is at the fire: holds a warm spot there (a new pull) or still stands in its warm zone (a pull in progress). */
	bool bLeaderAtFire = false;
	/** The pull is already running (the distance limit applies only to its start, so a detour does not drop it). */
	bool bPulling = false;
	/** The follower can walk and pose: alive, up, not frostbitten / in cover / carrying / vaulting / busy / threatened / prone. */
	bool bFollowerFree = true;
	/** Follower to the heat source, 2D, cm. */
	float FollowerToSource = 0.f;
};

/** Pure rules of the weather idle poses (tests: CodexTactics.Survival.WarmIdle.*). */
namespace WarmIdleRules
{
	/** Weather poses happen only here: Exploration and the post-combat return, never in a wave fight / ambush / cutscene / preparation. */
	CODEXTACTICS_API bool IsExplorationPhase(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	/** A wave fight / ambush runs (any combat mode): warming at a heat source is slower. */
	CODEXTACTICS_API bool IsCombatForWarming(ECodexGamePhase Phase);

	/** Warming multiplier of a heat source: CombatWarmthMultiplier in a fight, else ExplorationWarmthMultiplier. */
	CODEXTACTICS_API float GetHeatSourceWarmthMultiplier(const FTacticalHeatConfig& Config, bool bCombat);

	/** No reason to keep him out of a weather pose (phase, body, task, order, threat, idle time). */
	CODEXTACTICS_API bool CanHoldWarmPose(const FWarmIdleInput& Input, const FTacticalHeatConfig& Config);

	/**
	 * Pose of an operative on his own (the campfire / stove roles come from AssignFireRoles): barrel -> HandWarm (standing),
	 * campfire / stove -> HandWarm until the group assigns a role, other heat -> None, no heat and cold -> ColdIdle (standing).
	 */
	CODEXTACTICS_API EWarmIdlePose ChooseSoloPose(const FWarmIdleInput& Input, const FTacticalHeatConfig& Config);

	/** The pose of a campfire / stove role. */
	CODEXTACTICS_API EWarmIdlePose PoseForFireRole(EWarmFireRole Role);

	/** A heat kind whose operatives take spots around it (barrel, campfire, stove). */
	CODEXTACTICS_API bool UsesWarmSpots(EWarmHeatKind Kind);

	/**
	 * Roles at a campfire / stove, incremental (user design 2026-10-12): roles already given are kept (no flicker as the cold
	 * drops); each Unassigned one (newcomers, or all of them after the fire state changed) sits while fewer than
	 * min(MinSitters, N) sit or when at least ExtraSitterCold cold, else stands - the coldest are seated first; afterwards the
	 * coldest standers are promoted until min(MinSitters, N) sit (someone left). Sitters never exceed MaxSitters (inner seats).
	 */
	CODEXTACTICS_API TArray<EWarmFireRole> AssignFireRoles(const TArray<EWarmFireRole>& Current, const TArray<float>& Colds,
		int32 MinSitters, float ExtraSitterCold, int32 MaxSitters);

	/**
	 * Squad gathers at the fire (user decision 2026-10-12): the follower's formation follow target switches to the source's ring while
	 * the leader is at the fire, in exploration formation only; an explicit order, solo / guard, a threat or a body state ends it.
	 */
	CODEXTACTICS_API bool ShouldPullFollower(const FWarmPullInput& Input, const FTacticalHeatConfig& Config);

	/** How many spots fit on a ring of Radius with Spacing between them (>= 1). */
	CODEXTACTICS_API int32 RingCapacity(float Radius, float Spacing);

	/** Count spots evenly on a ring around Center (Z kept), the first at StartYawDeg. */
	CODEXTACTICS_API TArray<FVector> ComputeRing(const FVector& Center, float Radius, int32 Count, float StartYawDeg);

	/**
	 * The free slot for an operative standing at From: the one farthest (in angle) from the taken slots, the walk breaking ties
	 * (WalkWeightPerCm degrees per cm); with no slot taken simply the nearest. INDEX_NONE when none is free.
	 */
	CODEXTACTICS_API int32 PickSlot(const TArray<FVector>& Slots, const TArray<bool>& Taken, const FVector& Center, const FVector& From,
		float WalkWeightPerCm = 0.05f);

	/** Yaw (degrees) from From to To. */
	CODEXTACTICS_API float FaceYaw(const FVector& From, const FVector& To);

	/** The female clip set (AS_She_*) for a body whose mesh or skeleton name has "Female" in it; else the male one (recruits too). */
	CODEXTACTICS_API bool UsesFemaleClips(const FString& MeshName, const FString& SkeletonName);

	/** Next step of a clip set after Current ended (LoopsDone = loops of the current loop clip played so far). */
	CODEXTACTICS_API EWarmClipStep NextClipStep(EWarmClipStep Current, int32 LoopsDone, int32 LoopsBeforeSwitch, bool bHasLoopB);

	/** How a pose ends for Reason (PlayEnd only when the set has an End clip). */
	CODEXTACTICS_API EWarmExitStyle GetExitStyle(EWarmExitReason Reason, bool bHasEndClip);
}
