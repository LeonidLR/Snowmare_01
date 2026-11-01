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
	/** Ambushers: the hiding spot's kind (AmbushSpotRules; None = placed by the caller). */
	EAmbushSpotKind SpotKind = EAmbushSpotKind::None;
	/** A coordinated plan's id (0 = a single-spot group or a patrol) and the member's party. */
	int32 Plan = 0;
	EAmbushParty Party = EAmbushParty::Single;
};

/** One relocation of a bypassed ambush group (AmbushRelocateRules; smokes, logs). */
struct CODEXTACTICS_API FPressureRelocationRecord
{
	int32 Group = 0;
	int32 Plan = 0;
	/** The group's first member before / after. */
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	EAmbushSpotKind Kind = EAmbushSpotKind::None;
	/** cos(the squad's heading, leader -> the new spot). */
	float AheadCos = 0.f;
	/** An operative had a line of sight to a member when it moved (never by the rule). */
	bool bSeenAtMove = false;
	/** No operative had a line of sight to the new spot. */
	bool bDestinationConcealed = false;
	/** The pressure clock at the move, s. */
	float Clock = 0.f;
	/** Members moved. */
	int32 Members = 0;
};

/** A coordinated ambush plan as it stands (AmbushPlanRules; smokes, Codex.Pressure.Status). */
struct CODEXTACTICS_API FPressurePlanInfo
{
	int32 Id = 0;
	FVector2D Anchor = FVector2D::ZeroVector;
	float HeadingYawDeg = 0.f;
	EAmbushPlanPhase Phase = EAmbushPlanPhase::Waiting;
	/** The pressure clock when the flank block / the assault began (-1 = not yet), s. */
	float FlankAtClock = -1.f;
	float AssaultAtClock = -1.f;
	/** Seconds it held (squad in range, both flanks watched). */
	float HoldSeconds = 0.f;
	/** Per party (AmbushPlanRules::PlanParty order): its group, its spot (feet), the spot's kind. */
	TArray<int32> PartyGroups;
	TArray<FVector> PartySpots;
	TArray<EAmbushSpotKind> PartyKinds;
	/** Relocated (re-planned round the squad) this often. */
	int32 Relocations = 0;
	/** Could not be re-planned: its parties wait / relocate as single groups now. */
	bool bDissolved = false;
};

/**
 * The combat lab's endless enemy pressure (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md; UE-only, no Godot reference).
 * Active when the level JSON has a "pressure" block (L_FormationLabCombat.json; Codex.Pressure.Enabled overrides). The first event at
 * first_event_sec (30 s), then every interval a spawn event brings a group of 1-3 enemies (Hound / Frostbitten / Spitter / Cutter / Marksman by
 * the level's weights; at most max_alive alive, an event waits while the cap is full; turn-based combat stops the clock):
 *  - PATROLS: a random (seeded) spot 25-50 m from the squad, clear of every wall / obstacle box, on the navmesh, preferably out of the
 *    squad's sight and looks; the leader walks a loop (a runtime APatrolRouteActor), the others escort him (AEnemyCharacter escort);
 *  - AMBUSHERS: a concealed hiding spot (no operative's line of sight) 14-30 m from the squad, ahead of its way - at a wall end, a doorway
 *    or an authored ambush point (AmbushSpotRules; designer answer 1, 2026-11-01); their own perception is muted (they guard the spot) and
 *    they strike when the squad is in strike range and nobody looks their way (AmbushWatchRules on the squad's real looks: each living
 *    member's body yaw, his aim and his formation sector look); watched, they hold. Shot at / spotted first, they fight at once. A ranged
 *    ambusher that strikes suppresses (USuppressionSubsystem deliberate intent on its target). A bypassed group relocates ahead unseen
 *    (AmbushRelocateRules, answer 3);
 *  - COORDINATED plans (answer 2): four parties (flank left / right, front, rear) at hiding spots round the squad; when a flank side is
 *    unwatched the flanks strike with every ranged member suppressing, then the rear + front (AmbushPlanRules); their own alive cap.
 * All are level enemies (ULevelEncounterSubsystem::MarkLevelEnemy, UWaveSubsystem::SpawnLevelEnemy): the fight starts naturally on contact
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
	/** The level's settings in force (Codex.Pressure.IntervalSec applied; the smoke's overrides applied). */
	const FCombatPressureConfig& GetConfig() const { return Config; }

	/** Smokes: the timer's events off / on (forced events still work). */
	void SetAutoSpawn(bool bOn) { bAutoSpawn = bOn; }
	bool IsAutoSpawn() const { return bAutoSpawn; }
	/** Smokes: replaces the ambush watch half angle, degrees (< 0 = the level's value). */
	void SetWatchHalfAngleOverride(float Degrees);
	/** Smokes: replaces the share of coordinated ambushes 0..1 (< 0 = the level's value). */
	void SetCoordinatedShareOverride(float Share);

	/**
	 * A spawn event now (console, smokes): Kind unset = the seeded pick; Count <= 0 = the seeded group size. bIgnoreCap spawns even past
	 * max_alive. Returns the enemies spawned (a coordinated plan with no room / layout falls back to a single ambush, an ambush with no
	 * concealed hiding spot to a patrol; none when no valid spot was found).
	 */
	TArray<AEnemyCharacter*> TriggerEvent(TOptional<EPressureSpawnKind> Kind, int32 Count = 0, bool bIgnoreCap = false);

	/**
	 * A coordinated ambush plan round the squad now: Types (one per body, AmbushPlanRules::PartyOf order: flank left, flank right, front,
	 * rear, then front / rear) or, empty, the seeded bodies / types. bIgnoreCap ignores the plans' alive cap. Empty when there is no room or
	 * no complete layout (four parties at concealed hiding spots).
	 */
	TArray<AEnemyCharacter*> TriggerCoordinated(const TArray<EEnemyArchetype>& Types, bool bIgnoreCap = false);

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
	/** The pressure's clock (s since the level's pressure began; stops in turn-based combat) / when the first timer event fired (-1 = not yet). */
	float GetClock() const { return Clock; }
	float GetFirstEventClock() const { return FirstEventClock; }

	/** Enemy is a pressure ambusher; OutState its state, OutHoldSeconds how long it has held while watched. */
	bool GetAmbushState(const AEnemyCharacter* Enemy, EPressureAmbushState& OutState, float& OutHoldSeconds) const;
	/** Ambush groups that sprung because their side was unwatched (not those engaged otherwise). */
	int32 GetSprungByWatchCount() const { return SprungByWatch; }

	/** Enemy's group, plan id (0 = none), party, and whether it suppresses (a sprung ranged ambusher). False when it is no pressure enemy. */
	bool GetMemberInfo(const AEnemyCharacter* Enemy, int32& OutGroup, int32& OutPlan, EAmbushParty& OutParty, bool& bOutSuppressor) const;
	/** The newest coordinated plan's id (0 = none yet). */
	int32 GetLastPlanId() const { return LastPlanId; }
	bool GetPlanInfo(int32 PlanId, FPressurePlanInfo& OutInfo) const;
	/** Every relocation of a bypassed ambush since the level began. */
	const TArray<FPressureRelocationRecord>& GetRelocations() const { return Relocations; }
	/** Every hiding-spot candidate of the level (wall ends, doorways, authored points; AmbushSpotRules::Candidates; read once). */
	const TArray<FAmbushSpot>& GetHideSpots();

	/** The squad's looks now: per living member his body yaw, his aim (the enemy he aims at / fights) and his formation sector look. */
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
		EEnemyArchetype Type = EEnemyArchetype::Frostbitten;
		int32 Group = 0;
		int32 Plan = 0;
		EAmbushParty Party = EAmbushParty::Single;
		float FarSeconds = 0.f;
		EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
		float HoldSeconds = 0.f;
		/** Sprung ranged ambusher: its rounds suppress deliberately (USuppressionSubsystem intent kept on its target). */
		bool bSuppressor = false;
	};

	/** What a spawn is (records, managed entries). */
	struct FSpawnTags
	{
		EAmbushSpotKind SpotKind = EAmbushSpotKind::None;
		int32 Plan = 0;
		EAmbushParty Party = EAmbushParty::Single;
	};

	/** A chosen hiding spot. */
	struct FHideChoice
	{
		FVector Feet = FVector::ZeroVector;
		EAmbushSpotKind Kind = EAmbushSpotKind::None;
		float AheadCos = 0.f;
	};

	/** A running coordinated plan. */
	struct FPlan
	{
		FPressurePlanInfo Info;
		float PhaseSeconds = 0.f;
		float BypassedSeconds = 0.f;
	};

	void ReadConfig();
	void GatherObstacles();
	/** A patrol spawn spot round the leader (the best seeded candidate); unset when none is valid. */
	TOptional<FVector> FindSpot(EPressureSpawnKind Kind);
	/**
	 * The best concealed hiding spot (AmbushSpotRules candidates) MinDistanceCm..MaxDistanceCm from the leader / every operative, at least
	 * MinAheadCos ahead of the squad's heading, AvoidCm away from Avoid; scored by CombatPressureRules::ScoreSpot + the kind's bonus; reachable
	 * on foot. Unset when none.
	 */
	TOptional<FHideChoice> FindHideSpot(float MinDistanceCm, float MaxDistanceCm, float MinAheadCos, const TArray<FVector2D>& Avoid, float AvoidCm);
	/** The hiding spots' wall clearance (AmbushSpotRules::HideClearanceCm). */
	float HideClearance() const;
	/** A coordinated layout round the squad: four concealed hiding spots, one per party (AmbushPlanRules::AssignSpots). */
	bool PlanLayout(FPressurePlanInfo& OutInfo);
	/** Valid feet points for Count members round Feet (the first is Feet); bConcealed: only spots out of the squad's sight (ambushers). */
	TArray<FVector> GroupSpots(const FVector& Feet, int32 Count, bool bConcealed) const;
	TArray<AEnemyCharacter*> SpawnPatrolGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, bool bPlacedByCaller);
	TArray<AEnemyCharacter*> SpawnAmbushGroup(const FVector& Feet, const TArray<EEnemyArchetype>& Types, const FVector& FaceTowards, bool bPlacedByCaller,
		const FSpawnTags& Tags);
	/** Feet projected onto the navmesh at ground level near Point2D; unset when off the navmesh. */
	TOptional<FVector> ProjectFeet(const FVector2D& Point, float GroundZ) const;
	AEnemyCharacter* SpawnOne(EEnemyArchetype Type, const FVector& Feet, const FVector& FaceTowards, EPressureSpawnKind Kind, int32 Group, bool bPlacedByCaller,
		const FSpawnTags& Tags);
	APatrolRouteActor* MakeRoute(const FVector& Feet);
	void TickAmbushers(float Step);
	/** One coordinated plan's step (phases, springs, bypass / re-plan). */
	void TickPlan(FPlan& Plan, float Step, const TArray<FVector2D>& Squad, const TArray<FAmbushWatcher>& Watchers);
	/** A single group's bypass clock and relocation (AmbushRelocateRules). */
	void TickRelocation(int32 Group, float Step, const TArray<FVector2D>& Squad);
	/** Moves a hidden group's members to Feet (unseen; their guard post goes along). Returns the record. */
	FPressureRelocationRecord MoveGroup(int32 Group, const FVector& Feet, EAmbushSpotKind Kind, float AheadCos, const FVector& FaceTowards);
	/** Keeps every suppressor's deliberate intent on its current target (or the nearest operative). */
	void TickSuppressors();
	void TickRecycle(float Step);
	void Spring(int32 Group, const TCHAR* Why);
	void SpringMember(FManaged& Each, const TCHAR* Why);
	/** Any member of Group an operative has a line of sight to. */
	bool IsGroupSeen(int32 Group) const;
	void CleanUp();
	FVector LeaderFeet() const;
	FVector SquadHeading() const;

	FCombatPressureConfig Config;
	FCombatPressureConfig LevelConfig;
	bool bConfigRead = false;
	bool bAutoSpawn = true;
	float WatchHalfAngleOverride = -1.f;
	float CoordinatedShareOverride = -1.f;
	FRandomStream Stream;
	float Clock = 0.f;
	float DueAt = 0.f;
	float FirstEventClock = -1.f;
	bool bDelayLogged = false;
	float AmbushTimer = 0.f;
	float RecycleTimer = 0.f;
	int32 NextGroup = 1;
	int32 NextPlan = 1;
	int32 LastPlanId = 0;
	int32 EventsFired = 0;
	int32 EventsDelayed = 0;
	int32 SprungByWatch = 0;
	bool bObstaclesRead = false;
	TArray<FPressureBox> Obstacles;
	FBox2D Bounds = FBox2D(ForceInit);
	bool bHideSpotsRead = false;
	TArray<FAmbushSpot> HideSpots;
	TArray<FManaged> Managed;
	TArray<FPressureSpawnRecord> Records;
	TArray<FPlan> Plans;
	TArray<FPressureRelocationRecord> Relocations;
	/** Single groups' bypass clocks (AmbushRelocateRules). */
	TMap<int32, float> GroupBypassSeconds;
	/** Patrol routes per group (destroyed with the group). */
	TMap<int32, TWeakObjectPtr<APatrolRouteActor>> Routes;
};
