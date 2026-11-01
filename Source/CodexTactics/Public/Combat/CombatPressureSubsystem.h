#pragma once

#include "CoreMinimal.h"
#include "AI/AmbushWatchRules.h"
#include "Combat/CombatPressureRules.h"
#include "Data/CombatTypes.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatPressureSubsystem.generated.h"

class AEnemyCharacter;
class APatrolRouteActor;

/** A hidden ambusher's state (UCombatPressureSubsystem; AmbushWatchRules::Decide). */
enum class EPressureAmbushState : uint8
{
	/** Waiting concealed, the squad beyond the strike range. */
	Hidden,
	/** The squad in strike range, but someone looks its way: holding. */
	Holding,
	/** Struck (its side unwatched) or engaged otherwise (shot at, spotted): an ordinary enemy in the fight now. */
	Sprung
};

/** One spawned pressure enemy as it appeared (smokes, logs). */
struct CODEXTACTICS_API FPressureSpawnRecord
{
	TWeakObjectPtr<AEnemyCharacter> Enemy;
	EPressureSpawnKind Kind = EPressureSpawnKind::Patrol;
	EEnemyArchetype Type = EEnemyArchetype::Frostbitten;
	int32 Group = 0;
	/** Feet on the navmesh where it was spawned. */
	FVector Location = FVector::ZeroVector;
	/** CombatPressureRules::Clearance of Location from the obstacle boxes, cm. */
	float ClearanceCm = 0.f;
	/** Planar distance to the nearest operative at the spawn, cm. */
	float SquadDistanceCm = 0.f;
	/** No operative had a line of sight to it at the spawn. */
	bool bConcealed = false;
	/** Its side was watched at the spawn (AmbushWatchRules::IsWatched). */
	bool bWatched = false;
	/** Spawn*GroupAt: the caller chose the spot (no validity check); false = the pressure's own seeded, checked pick (timer / TriggerEvent). */
	bool bPlacedByCaller = false;
};

/**
 * The combat lab's endless enemy pressure (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md; UE-only, no Godot reference).
 * Active when the level JSON has a "pressure" block (L_FormationLabCombat.json; Codex.Pressure.Enabled overrides). Every interval a spawn
 * event brings a group of 1-3 enemies (Hound / Frostbitten / Spitter / Cutter / Marksman by the level's weights; at most max_alive alive,
 * an event waits while the cap is full; turn-based combat stops the clock):
 *  - PATROLS: a random (seeded) spot 25-50 m from the squad, clear of every wall / obstacle box, on the navmesh, preferably out of the
 *    squad's sight and looks; the leader walks a loop (a runtime APatrolRouteActor), the others escort him (AEnemyCharacter escort);
 *  - AMBUSHERS: a concealed spot (no operative's line of sight) 14-30 m from the squad, ahead of its way; their own perception is muted (they
 *    guard the spot) and they strike when the squad is in strike range and nobody looks their way (AmbushWatchRules on the squad's real looks:
 *    each living member's body yaw and formation sector look); watched, they hold. Shot at / spotted first, they fight at once.
 * Both are level enemies (ULevelEncounterSubsystem::MarkLevelEnemy, UWaveSubsystem::SpawnLevelEnemy): the fight starts naturally on contact
 * (the level's "combat_start": "ambush", "ambush_fight": "skirmish") and every later one joins it. An unengaged enemy left far behind,
 * unseen, for a while is removed (CombatPressureRules::ShouldRecycle). Logs "[Pressure] ...".
 */
UCLASS()
class CODEXTACTICS_API UCombatPressureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The level's pressure is on (Codex.Pressure.Enabled overrides the level JSON). */
	bool IsActive() const;
	/** The level's settings in force (Codex.Pressure.IntervalSec applied; the smoke's watch override applied). */
	const FCombatPressureConfig& GetConfig() const { return Config; }

	/** Smokes: the timer's events off / on (forced events still work). */
	void SetAutoSpawn(bool bOn) { bAutoSpawn = bOn; }
	bool IsAutoSpawn() const { return bAutoSpawn; }
	/** Smokes: replaces the ambush watch half angle, degrees (< 0 = the level's value). */
	void SetWatchHalfAngleOverride(float Degrees);

	/**
	 * A spawn event now (console, smokes): Kind unset = the seeded pick; Count <= 0 = the seeded group size. bIgnoreCap spawns even past
	 * max_alive. Returns the enemies spawned (an ambush with no concealed spot falls back to a patrol; none when no valid spot was found).
	 */
	TArray<AEnemyCharacter*> TriggerEvent(TOptional<EPressureSpawnKind> Kind, int32 Count = 0, bool bIgnoreCap = false);

	/** A patrol group with its leader at Feet (Types[0] leads; the loop round Feet). No validity check: the caller picked the spot. */
	TArray<AEnemyCharacter*> SpawnPatrolGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types);
	/** An ambush group at Feet facing FaceTowards (muted perception, holding until its side is unwatched). No validity check. */
	TArray<AEnemyCharacter*> SpawnAmbushGroupAt(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards);

	/** Removes every pressure enemy and its patrol route (smokes). */
	void DespawnAll();

	/** Living pressure enemies (the cap counts these). */
	int32 GetAliveCount() const;
	/** Every spawn since the level began. */
	const TArray<FPressureSpawnRecord>& GetSpawnRecords() const { return Records; }
	/** Timer events fired / that had to wait for room under the cap. */
	int32 GetEventsFired() const { return EventsFired; }
	int32 GetEventsDelayed() const { return EventsDelayed; }
	/** Seconds until the next timer event (0 = due). */
	float GetSecondsToNextEvent() const { return FMath::Max(0.f, DueAt - Clock); }

	/** Enemy is a pressure ambusher; OutState its state, OutHoldSeconds how long it has held while watched. */
	bool GetAmbushState(const AEnemyCharacter* Enemy, EPressureAmbushState& OutState, float& OutHoldSeconds) const;
	/** Ambush groups that sprung because their side was unwatched (not those engaged otherwise). */
	int32 GetSprungByWatchCount() const { return SprungByWatch; }

	/** The squad's looks now: per living member his body yaw and, when it differs, his formation sector look. */
	TArray<FAmbushWatcher> GatherWatchers() const;
	/** The living operatives' planar positions. */
	TArray<FVector2D> GatherSquadPositions() const;
	/** Some living operative has a line of sight (world static only) to a body standing at Feet. */
	bool IsVisibleToSquad(const FVector& Feet) const;
	/** The level's walls / obstacles as boxes (blocking static meshes >= 50 cm tall; read once). */
	const TArray<FPressureBox>& GetObstacleBoxes();

private:
	struct FManaged
	{
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		EPressureSpawnKind Kind = EPressureSpawnKind::Patrol;
		int32 Group = 0;
		float FarSeconds = 0.f;
		EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
		float HoldSeconds = 0.f;
	};

	void ReadConfig();
	void GatherObstacles();
	/** A spawn spot of Kind round the leader (the best seeded candidate); unset when none is valid. */
	TOptional<FVector> FindSpot(EPressureSpawnKind Kind);
	/** Valid feet points for Count members round Feet (the first is Feet); bConcealed: only spots out of the squad's sight (ambushers). */
	TArray<FVector> GroupSpots(const FVector& Feet, int32 Count, bool bConcealed) const;
	TArray<AEnemyCharacter*> SpawnPatrolGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, bool bPlacedByCaller);
	TArray<AEnemyCharacter*> SpawnAmbushGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards, bool bPlacedByCaller);
	/** Feet projected onto the navmesh at ground level near Point2D; unset when off the navmesh. */
	TOptional<FVector> ProjectFeet(const FVector2D& Point, float GroundZ) const;
	AEnemyCharacter* SpawnOne(EEnemyArchetype Type, const FVector& Feet, const FVector& FaceTowards, EPressureSpawnKind Kind, int32 Group, bool bPlacedByCaller);
	APatrolRouteActor* MakeRoute(const FVector& Feet);
	void TickAmbushers(float Step);
	void TickRecycle(float Step);
	void Spring(int32 Group, const TCHAR* Why);
	void CleanUp();
	FVector LeaderFeet() const;
	FVector SquadHeading() const;

	FCombatPressureConfig Config;
	FCombatPressureConfig LevelConfig;
	bool bConfigRead = false;
	bool bAutoSpawn = true;
	float WatchHalfAngleOverride = -1.f;
	FRandomStream Stream;
	float Clock = 0.f;
	float DueAt = 0.f;
	bool bDelayLogged = false;
	float AmbushTimer = 0.f;
	float RecycleTimer = 0.f;
	int32 NextGroup = 1;
	int32 EventsFired = 0;
	int32 EventsDelayed = 0;
	int32 SprungByWatch = 0;
	bool bObstaclesRead = false;
	TArray<FPressureBox> Obstacles;
	FBox2D Bounds = FBox2D(ForceInit);
	TArray<FManaged> Managed;
	TArray<FPressureSpawnRecord> Records;
	/** Patrol routes per group (destroyed with the group). */
	TMap<int32, TWeakObjectPtr<APatrolRouteActor>> Routes;
};
