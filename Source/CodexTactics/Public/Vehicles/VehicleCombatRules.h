#pragma once

#include "CoreMinimal.h"
#include "Characters/FirePostureRules.h"
#include "Data/CombatTypes.h"
#include "VehicleCombatRules.generated.h"

/**
 * Pure combat rules of the squad vehicle (APC BTR-82, phase P2 + the 2026-10-12 user requests; docs/vehicles/APC_02_DESIGN.md
 * §1.4): the turret (30 mm autocannon + coaxial MG: target choice by the gunner's fire posture, dead zone round the hull,
 * aim slew, shells, MG heat), running over small enemies / ramming the Brute, and the boarding fade timeline.
 * UE-only (Godot had no vehicles): design-number tests in CodexTactics.Vehicle.*.
 */

/** The turret's two guns. */
UENUM(BlueprintType)
enum class EVehicleTurretWeapon : uint8
{
	None,
	/** 30 mm 2A72 autocannon (HE): big damage + splash, limited shells per map. */
	Cannon,
	/** Coaxial 7.62 mm MG: unlimited, overheats. */
	MachineGun
};

/** Turret tuning (vehicle Blueprint, Unreal is master; the level JSON "vehicle.shells" overrides the shells per map). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleTurretConfig
{
	GENERATED_BODY()

	// --- Autocannon ----------------------------------------------------------------------------------------------

	/** Direct hit damage of one HE shell (design 70). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonDamage = 70.f;

	/** Splash radius round the impact (design 1.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonSplashRadiusCm = 150.f;

	/** Splash damage to the others inside the radius (design 35). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonSplashDamage = 35.f;

	/** Seconds between the shells of a burst (design 1 shot / 0.5 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0.05"))
	float CannonShotIntervalSeconds = 0.5f;

	/** Shells per burst (design 3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "1"))
	int32 CannonBurstShots = 3;

	/** Pause after a burst. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonBurstPauseSeconds = 1.2f;

	/** Range (design 40 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonRangeCm = 4000.f;

	/** Shells per map (design 60; the level JSON "vehicle": {"shells": N} overrides it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	int32 CannonShellsPerMap = 60;

	/** Hit chance of a shell at any range inside CannonRangeCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0", ClampMax = "1"))
	float CannonHitChance = 0.9f;

	/** Multiplier on the target's armour for a shell (0 = the HE shell ignores the Brute's hide, 1 = full armour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0", ClampMax = "1"))
	float CannonArmourFactor = 0.25f;

	/** Enemies hear the gun (design 60 m, explosion class). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "0"))
	float CannonNoiseRadiusCm = 6000.f;

	/** The cannon is used on a small target when this many enemies stand within 2 x the splash radius of it (groups). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannon", meta = (ClampMin = "2"))
	int32 CannonGroupCount = 3;

	// --- Coaxial machine gun -------------------------------------------------------------------------------------

	/** Damage per round (design 18). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0"))
	float MgDamage = 18.f;

	/** Rounds per second (design 8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0.5"))
	float MgRoundsPerSecond = 8.f;

	/** Range (design 35 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0"))
	float MgRangeCm = 3500.f;

	/** Hit chance of a round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0", ClampMax = "1"))
	float MgHitChance = 0.8f;

	/** Seconds of continuous fire from cold to overheated (design 4 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0.1"))
	float MgOverheatSeconds = 4.f;

	/** Seconds to cool down from full heat to 0 (design 3 s); an overheated MG stays silent until it is cold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0.1"))
	float MgCooldownSeconds = 3.f;

	/** Enemies hear the MG (design 45 m, gunshot class). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Machine Gun", meta = (ClampMin = "0"))
	float MgNoiseRadiusCm = 4500.f;

	// --- Mount -----------------------------------------------------------------------------------------------------

	/** Turret traverse (design 60 deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "1"))
	float TraverseDegPerSec = 60.f;

	/** Gun elevation speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "1"))
	float ElevationDegPerSec = 35.f;

	/** Gun depression limit (design -4 deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "-30", ClampMax = "0"))
	float MinElevationDeg = -4.f;

	/** Gun elevation limit (design +60 deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0", ClampMax = "85"))
	float MaxElevationDeg = 60.f;

	/** The gun fires once it points within this of the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0.1"))
	float AimToleranceDeg = 4.f;

	/**
	 * Dead zone (design 4 m): the gun cannot depress onto anything closer than this to the hull side — hounds at the wheels
	 * must be handled on foot or by driving off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0"))
	float DeadZoneCm = 400.f;

	/** Defensive posture: the gunner answers only enemies within this distance (design 20 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0"))
	float DefensiveRadiusCm = 2000.f;

	/** How often the gunner re-picks his target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mount", meta = (ClampMin = "0.05"))
	float RetargetSeconds = 0.25f;
};

/** What the hull does to a body in its path (crush rules). */
UENUM(BlueprintType)
enum class EVehicleContact : uint8
{
	/** Nothing to do (dead / hidden / not a pawn we care about). */
	None,
	/** Brake and wait (operatives, recruits / NPCs: never run over). */
	Brake,
	/** A small enemy too slow to crush: shoved aside, no braking. */
	Push,
	/** A small enemy at speed: dies under the wheels. */
	Crush,
	/** A large enemy (Brute) hit fast: knocked down, the hull loses its speed and stops against him. */
	KnockDown,
	/** A large enemy (Brute) hit slowly or already down: the hull stops against him. */
	Block
};

/** Run-over / ram tuning (vehicle Blueprint, Unreal is master; user decision 2026-10-12 replaces "never runs anyone over"). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleCrushConfig
{
	GENERATED_BODY()

	/** Small enemies the wheels kill (the Brute is never on this list: he is rammed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush")
	TArray<EEnemyArchetype> CrushableArchetypes = { EEnemyArchetype::Base, EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten,
		EEnemyArchetype::Cutter, EEnemyArchetype::Spitter, EEnemyArchetype::Marksman };

	/** Archetypes that are rammed (knocked down when hit fast, else they stop the hull). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush")
	TArray<EEnemyArchetype> RammedArchetypes = { EEnemyArchetype::Brute };

	/** A small enemy dies under the hull at or above this speed; slower, he is shoved aside (task: > 150 cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush", meta = (ClampMin = "0"))
	float MinCrushSpeedCmS = 150.f;

	/** Speed kept after each body crushed (the hull slows slightly on impact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush", meta = (ClampMin = "0", ClampMax = "1"))
	float CrushSpeedFactor = 0.9f;

	/** Sideways shove of a small enemy too slow to crush (cm/s launch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush", meta = (ClampMin = "0"))
	float PushLaunchCmS = 350.f;

	/** Impulse on a crushed body's ragdoll / death (cm/s along the hull's travel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crush", meta = (ClampMin = "0"))
	float CrushImpulseCmS = 600.f;

	/** A rammed Brute is knocked down at or above this speed (user: "at high speed", >= 4 m/s); slower he just stops the hull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0"))
	float RamKnockdownSpeedCmS = 400.f;

	/** Damage a fast ram deals to the Brute (data, small). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0"))
	float RamDamage = 40.f;

	/** Speed kept after ramming a Brute (the hull loses its speed, then brakes to a stop against him). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0", ClampMax = "1"))
	float RamSpeedFactor = 0.25f;

	/** The rammed Brute's fall and get-up clips play at this absolute rate (heavier, slower fall and get-up; 1 = the clip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0.1", ClampMax = "2"))
	float RamKnockdownPlayRate = 0.65f;

	/** The rammed Brute stays down this many times his normal downed time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0.5", ClampMax = "5"))
	float RamDownedTimeScale = 1.5f;

	/** A downed Brute is run over too (default no: he stops the hull like a standing one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram")
	bool bCrushDownedRammed = false;

	/** Damage the hull takes from a ram (P4 damage: no hull health yet, kept for the damage phase). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ram", meta = (ClampMin = "0"))
	float HullRamDamage = 0.f;
};

/** Boarding / disembarking fade (user 2026-10-12: no boarding animations, a quick fade to black and back). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleFadeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade")
	bool bEnabled = true;

	/** To black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float FadeOutSeconds = 0.25f;

	/** Minimum time on black (the operatives are moved in / out at its start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float HoldSeconds = 0.15f;

	/** Back from black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float FadeInSeconds = 0.25f;

	/** Never stuck black: the longest a hold may be stretched by late joiners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0.1"))
	float MaxHoldSeconds = 1.f;

	/**
	 * Group boarding: the first one at a door waits at most this long for the others still walking to the same vehicle
	 * (those who will arrive within the time left), so one fade covers the whole group; a straggler gets his own fade.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float GroupWaitSeconds = 8.f;
};

/** Phase of the boarding fade. */
enum class EVehicleFadePhase : uint8
{
	Idle,
	FadeOut,
	Hold,
	FadeIn
};

/**
 * The fade timeline (real time, pure): Request() starts a fade (or joins the running one while it is still going to /
 * holding black), Advance() moves it on and says when the screen just turned black (the moment to move the operatives).
 * Joining during the fade-in starts a new cycle once it ends (never a second overlapping fade).
 */
struct CODEXTACTICS_API FVehicleFadeTimeline
{
	EVehicleFadePhase Phase = EVehicleFadePhase::Idle;
	float PhaseTime = 0.f;
	/** Total time spent on black in this cycle. */
	float HoldTime = 0.f;
	/** A request arrived during the fade-in: go black again once it ends. */
	bool bQueued = false;
	/** Requests joined since the screen went black (the hold is stretched for them, within MaxHoldSeconds). */
	bool bLateJoin = false;

	/** A new board / exit wants the black. True when it joined the current cycle (it will be executed in it). */
	bool Request(const FVehicleFadeConfig& Config);

	/** Moves on by DeltaSeconds (real time). Returns true on the tick the screen became fully black. */
	bool Advance(float DeltaSeconds, const FVehicleFadeConfig& Config);

	/** Screen darkness 0..1. */
	float GetAlpha(const FVehicleFadeConfig& Config) const;

	bool IsBlack() const { return Phase == EVehicleFadePhase::Hold; }
	bool IsActive() const { return Phase != EVehicleFadePhase::Idle; }
};

namespace VehicleCombatRules
{
	// --- Turret ----------------------------------------------------------------------------------------------------

	/** One enemy as the gunner sees it. */
	struct FTurretCandidate
	{
		bool bAlive = true;
		/** Line of sight from the muzzle. */
		bool bVisible = true;
		/** Brute-class (armoured, cannon target). */
		bool bLarge = false;
		/** Marksman (a cannon target too: a sniper behind cover). */
		bool bMarksman = false;
		/** Distance from the muzzle. */
		float DistanceCm = 0.f;
		/** 2D distance from the hull box side (0 inside / touching). */
		float HullDistanceCm = 0.f;
		/** Other enemies within 2 x the splash radius of it. */
		int32 NeighbourCount = 0;
	};

	/** The gunner's situation. */
	struct FTurretContext
	{
		ESquadFirePosture Posture = ESquadFirePosture::Aggressive;
		/** Defensive: the APC / squad has been attacked in this fight (FirePostureRules provocation). */
		bool bProvoked = false;
		/** Index of the Ctrl+click priority target in the candidates, INDEX_NONE without one. */
		int32 PriorityIndex = INDEX_NONE;
		int32 ShellsLeft = 0;
		bool bMgOverheated = false;
	};

	/** The dead zone: closer than DeadZoneCm to the hull side, the gun cannot depress onto it. */
	CODEXTACTICS_API bool IsInDeadZone(float HullDistanceCm, const FVehicleTurretConfig& Config);

	/** Longest reach of the guns that can fire now (the cannon only with shells, the MG only when not overheated). */
	CODEXTACTICS_API float GetMaxRangeCm(int32 ShellsLeft, bool bMgOverheated, const FVehicleTurretConfig& Config);

	/**
	 * Can this candidate be engaged at all (alive, visible, outside the dead zone, within the cannon / MG range)?
	 * Postures are not applied here.
	 */
	CODEXTACTICS_API bool IsEngageable(const FTurretCandidate& Candidate, const FVehicleTurretConfig& Config);

	/**
	 * The gunner's target: the priority target (Ctrl+click) whenever it is engageable, in every posture; otherwise by
	 * posture — Passive holds fire, Defensive answers only once provoked and only within DefensiveRadiusCm, Aggressive
	 * takes anything engageable. Among the eligible: a large enemy within the defensive radius first (the threat to the
	 * hull), else the nearest. INDEX_NONE = hold fire.
	 */
	CODEXTACTICS_API int32 ChooseTarget(const TArray<FTurretCandidate>& Candidates, const FTurretContext& Context, const FVehicleTurretConfig& Config);

	/**
	 * Which gun for the target: the cannon for a Brute / a marksman / a group (NeighbourCount + 1 >= CannonGroupCount) or a
	 * priority target, while shells last and within its range; the MG for the rest (and as the fallback for those when
	 * the shells are gone). An overheated MG means None for small targets (the shells are kept for the big threats).
	 */
	CODEXTACTICS_API EVehicleTurretWeapon ChooseWeapon(const FTurretCandidate& Target, bool bPriority, int32 ShellsLeft, bool bMgOverheated,
		const FVehicleTurretConfig& Config);

	/** Turns Current towards Desired by at most RateDegPerSec * DeltaSeconds (degrees, wraps at +-180). */
	CODEXTACTICS_API float StepAngle(float CurrentDeg, float DesiredDeg, float RateDegPerSec, float DeltaSeconds);

	/** The gun's elevation clamped to its mount limits. */
	CODEXTACTICS_API float ClampElevation(float PitchDeg, const FVehicleTurretConfig& Config);

	/** The gun points at the target (yaw and pitch errors within AimToleranceDeg; pitch error ignored when clamped). */
	CODEXTACTICS_API bool IsAimed(float YawErrorDeg, float PitchErrorDeg, const FVehicleTurretConfig& Config);

	/**
	 * MG heat 0..1: rises by 1 / MgOverheatSeconds per second of fire, falls by 1 / MgCooldownSeconds per second without.
	 * Reaching 1 latches bInOutOverheated until the heat is back at 0.
	 */
	CODEXTACTICS_API float StepHeat(float Heat, bool bFiring, float DeltaSeconds, bool& bInOutOverheated, const FVehicleTurretConfig& Config);

	/** Damage of a shell to an enemy DistanceFromImpactCm from the impact (direct hit at <= 1 cm; splash inside the radius). */
	CODEXTACTICS_API float GetCannonDamage(float DistanceFromImpactCm, bool bDirectHit, const FVehicleTurretConfig& Config);

	/** Shells for the map: the level JSON override when >= 0, else the Blueprint's CannonShellsPerMap. */
	CODEXTACTICS_API int32 GetShellsForMap(int32 LevelOverride, const FVehicleTurretConfig& Config);

	/** HUD line "30mm 57/60  MG 40%" / "MG OVERHEAT" / "NO GUNNER". */
	CODEXTACTICS_API FString GetHudLine(bool bHasGunner, int32 ShellsLeft, int32 ShellsMax, float MgHeat, bool bMgOverheated,
		ESquadFirePosture Posture);

	// --- Run over / ram --------------------------------------------------------------------------------------------

	/** Who is in the hull's path. */
	enum class EContactKind : uint8
	{
		Operative,
		/** A recruit / NPC / any friendly that is not a squad operative. */
		Friendly,
		Enemy
	};

	struct FContactInput
	{
		EContactKind Kind = EContactKind::Enemy;
		EEnemyArchetype Archetype = EEnemyArchetype::Base;
		bool bAlive = true;
		/** Knocked down (lying in the path). */
		bool bDowned = false;
		/** Cannot be knocked down again yet (knockdown reknock immunity). */
		bool bKnockdownImmune = false;
		/** Flying (cryo drone): the hull passes under it. */
		bool bFlying = false;
	};

	CODEXTACTICS_API bool IsCrushable(EEnemyArchetype Archetype, const FVehicleCrushConfig& Config);
	CODEXTACTICS_API bool IsRammed(EEnemyArchetype Archetype, const FVehicleCrushConfig& Config);

	/**
	 * What the hull does to Contact at SpeedCmS (unsigned): operatives / friendlies -> Brake (never run over); small enemies ->
	 * Crush at or above MinCrushSpeedCmS, else Push; a rammed enemy (Brute) -> KnockDown at or above RamKnockdownSpeedCmS when
	 * standing and not immune, else Block (a downed one -> Block unless bCrushDownedRammed); unknown archetypes -> Block;
	 * dead / flying -> None.
	 */
	CODEXTACTICS_API EVehicleContact EvaluateContact(const FContactInput& Contact, float SpeedCmS, const FVehicleCrushConfig& Config);

	/** Speed left after the contacts of one tick (each crush slows by CrushSpeedFactor, a ram by RamSpeedFactor). */
	CODEXTACTICS_API float GetSpeedAfterImpacts(float SpeedCmS, int32 Crushed, bool bRammed, const FVehicleCrushConfig& Config);

	// --- Group boarding fade -----------------------------------------------------------------------------------------

	/**
	 * Start the boarding fade now? The first one at a door has waited ReadyWaitSeconds; OtherWalkersEtaSeconds = when each
	 * mate still walking to the same vehicle reaches his door. True once nobody will arrive within the wait left
	 * (GroupWaitSeconds - ReadyWaitSeconds), or the wait is used up.
	 */
	CODEXTACTICS_API bool ShouldStartGroupFade(float ReadyWaitSeconds, const TArray<float>& OtherWalkersEtaSeconds, const FVehicleFadeConfig& Config);

	/**
	 * A walker's arrival estimate: straight-line distance x PathFactor (the path goes round the hull / mates) / his speed,
	 * the speed floored at MinSpeedCmS (just set off / blocked).
	 */
	CODEXTACTICS_API float EstimateArrivalSeconds(float DistanceCm, float SpeedCmS, float MinSpeedCmS = 150.f, float PathFactor = 1.8f);
}
