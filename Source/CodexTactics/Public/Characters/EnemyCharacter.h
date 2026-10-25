#pragma once

#include "CoreMinimal.h"
#include "AI/PatrolRouteRules.h"
#include "AI/PerceptionRules.h"
#include "AI/ScentRules.h"
#include "Characters/EnemyAIRules.h"
#include "GameFramework/Character.h"
#include "Data/CombatTypes.h"
#include "Combat/KnockdownTypes.h"
#include "EnemyCharacter.generated.h"

class AAIController;
class APatrolRouteActor;
class UHealthComponent;
struct FOverheadLabel;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UVisionConeComponent;
enum class ENoiseReaction : uint8;

/** A loud squad action enemies can hear (PerceptionRules gunshot / grenade radius). */
enum class ESquadNoise : uint8
{
	/** An operative fired (at an enemy or an object). */
	Gunshot,
	/** A grenade thrown by the squad went off. */
	Explosion
};

/** Patrol / search progress of one enemy for the save game (AEnemyCharacter::CapturePatrolSnapshot). */
struct CODEXTACTICS_API FEnemyPatrolSnapshot
{
	/** On patrol / escort duty (not engaged). */
	bool bOnPatrol = false;
	int32 WaypointIndex = 0;
	bool bForward = true;
	/** EPatrolPhase as a number (Moving, Waiting, Turning, Finished). */
	int32 Phase = 0;
	float WaitLeft = 0.f;
	bool bSearching = false;
	FVector SearchOrigin = FVector::ZeroVector;
	float SearchElapsed = 0.f;
	float Suspicion = 0.f;
	/** Stealth lure investigation (2026-10-18): the search's own spec (FPatrolSearchSpec). */
	bool bLureSearch = false;
	float SearchDurationSeconds = -1.f;
	float SearchSweepRadiusCm = -1.f;
	float SearchApproachSpeedFraction = 0.f;
	/** Investigating a lost hound scent (2026-10-18, FPatrolSearchSpec::bScent). */
	bool bScentSearch = false;
	/** Audit 2026-10-19: the hound's running scent episode (EHoundScentPhase as a number, its timers, the scent point). */
	int32 ScentPhase = 0;
	float ScentPhaseSeconds = 0.f;
	float ScentSinceSeconds = 0.f;
	FVector ScentPoint = FVector::ZeroVector;
};

/**
 * How a patrol search runs. A trap / a lost leader: the defaults (the level's search time, the 12 m sweep, the search pace).
 * A stealth lure (user decisions 2026-10-18, UStealthLureSubsystem): its own investigation time, a tight sweep round the
 * noise and the walk to it at a fraction of the enemy's own speed (hounds fast, Brutes slow).
 */
struct CODEXTACTICS_API FPatrolSearchSpec
{
	/** Investigating a stealth lure (the HUD's «?» marker, AEnemyCharacter::IsInvestigatingLure). */
	bool bLure = false;
	/** Seconds of the search; < 0 = the level's patrol search time. */
	float DurationSeconds = -1.f;
	/** Sweep points within this radius of the noise, cm; < 0 = enemy_perception.json search.sweep_radius_m. */
	float SweepRadiusCm = -1.f;
	/** Walk to the noise at this fraction of the normal speed (StealthLureRules::GetApproachSpeed); <= 0 = the search pace. */
	float ApproachSpeedFraction = 0.f;
	/** A hound investigating the point where it lost a scent (ScentRules, 2026-10-18; AEnemyCharacter::IsInvestigatingScent). */
	bool bScent = false;
	/** A lure: stand and listen (turned towards the noise) this long before walking there, s (FStealthLureParams::ListenSeconds). */
	float ListenSeconds = 0.f;
};

/** Materials of one look variant, by material SLOT NAME (slot order differs between meshes); slots not listed keep the mesh's. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemyMaterialSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TMap<FName, TObjectPtr<class UMaterialInterface>> SlotMaterials;
};

/**
 * One body of an enemy Blueprint with ITS colour sets (user 2026-10-21, single-slot optimized bodies): every optimized body has
 * one material slot and its own atlas, so a set only fits the body it was baked for (MI_MilitaryZombie_1_Set2 is not body 2's
 * Set2). Sets = the material of slot 0, one per colour set.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemyLookBody
{
	GENERATED_BODY()

	/** The body (same skeleton as the Blueprint's mesh: the ABP keeps running). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TObjectPtr<class USkeletalMesh> Mesh;

	/** Its colour sets: the slot-0 material of each (an MI of M_CharacterMaster*); empty = the mesh's own material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TArray<TObjectPtr<class UMaterialInterface>> Sets;
};

/**
 * Per-instance look variants of one enemy Blueprint (game designer 2026-10-20, Military Zombie: 3 assembled bodies x 3 texture
 * sets): BeginPlay picks one mesh and one material set by the spawn seed (AEnemyCharacter::GetVariantSeed, deterministic per
 * spawn). The meshes must share the Blueprint mesh's skeleton (the ABP keeps running). Empty lists: the Blueprint's look.
 * Bodies (2026-10-21) wins when filled and the Blueprint's mesh is one of the bodies (UsesLookBodiesFor): a body, then one of
 * THAT body's sets (EnemyAnimRules::PickLookBodyAndSet, the same salts as Meshes / MaterialSets); Meshes / MaterialSets stay
 * the path of the Blueprints without Bodies.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FEnemyLookVariants
{
	GENERATED_BODY()

	/** Body meshes to pick from (one per instance); empty = the Blueprint's mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TArray<TObjectPtr<class USkeletalMesh>> Meshes;

	/** Material sets to pick from (one per instance, independent of the mesh); empty = the mesh's own materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TArray<FEnemyMaterialSet> MaterialSets;

	/** Bodies with their own colour sets (single-slot bodies); non-empty = used instead of Meshes / MaterialSets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	TArray<FEnemyLookBody> Bodies;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedDynamic, AEnemyCharacter*, Enemy);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedNative, AEnemyCharacter*);

/**
 * Base enemy character in CodexTactics.
 * Tagged "Enemy" for combat queries and targeting.
 * Configurable via EEnemyArchetype (Hound, Spitter, Brute, Frostbitten).
 * Godot reference: Scenes/movements/enemy_base.gd.
 * Layout (cleanup 2026-10-19, docs/port/REFACTOR_ENEMY_2026-10.md): the members are defined in one EnemyCharacter<Cluster>.cpp
 * per cluster under Private/Characters/ - the main file (ctor, lifecycle, Tick, facing, move requests, holds, label),
 * Archetype (archetype defaults, wave modifiers, balance), Behavior (TickBehavior: fire fear, flank detour, melee close-in;
 * target selection), Combat (attacks, spitter, cutter jump), Health (knockdown hooks, damage, death), Patrol (routes, escorts,
 * guard posts, patrol break), Save (patrol snapshot), Perception (sight / hearing, vision cone, squad noise), Scent (hound
 * scent), Search (patrol search, stealth lure), Performance (variant seed, look variants, alert roar / spawn entrance; 2026-10-20).
 */
UCLASS()
class CODEXTACTICS_API AEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;
	/** Leaves the world's UEnemyRegistrySubsystem (registered in BeginPlay). */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	/** Configures the enemy according to the archetype stats and visuals. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void InitializeArchetype(EEnemyArchetype InArchetype);

	/**
	 * A spawner (waves, save load) spawns an enemy of Archetype within this scope: its BeginPlay takes it as is. Without
	 * one the enemy was placed in the level and BeginPlay resolves its archetype from its class
	 * (EnemyAIRules::ResolvePlacedArchetype).
	 */
	struct CODEXTACTICS_API FScopedSpawnArchetype
	{
		explicit FScopedSpawnArchetype(EEnemyArchetype InArchetype);
		~FScopedSpawnArchetype();
		FScopedSpawnArchetype(const FScopedSpawnArchetype&) = delete;
		FScopedSpawnArchetype& operator=(const FScopedSpawnArchetype&) = delete;

	private:
		TOptional<EEnemyArchetype> Previous;
	};

	/** Placed in the level (not made by a spawner): the archetype came from ResolvePlacedArchetype (smokes). */
	bool WasPlacedInLevel() const { return bPlacedInLevel; }

	// --- Animation extras + look variants (game designer 2026-10-20, Military Zombie; UE-only) ---

	/**
	 * Seed of its per-instance variants (look, walk / run / idle clips, idle-break choices): EnemyAnimRules::MakeVariantSeed of its
	 * name and the place it entered the world (cached on the first call), or VariantSeedOverride when >= 0.
	 */
	uint32 GetVariantSeed() const;

	/**
	 * The spawn / wake-up entrance: its ABP's EntranceAnimations clip (UEnemyAnimInstance::NotifyEntrance), standing still
	 * meanwhile (no AI moves, no attacks). Called by UWaveSubsystem::SpawnEnemy for every wave / horde spawn and at BeginPlay for a
	 * placed enemy with bPlayEntranceWhenPlaced. Returns the seconds it stands (0: no entrance clip - every other enemy).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	float PlaySpawnEntrance();

	/** Standing still for its alert roar / spawn entrance (smokes, Blueprints). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsPerforming() const { return PerformanceLockTimer > 0.f; }

	/** Which look variant BeginPlay applied (-1 = the Blueprint's mesh / materials). With LookVariants.Bodies: the body index
	 * and the index into THAT body's Sets. */
	int32 GetLookMeshIndex() const { return LookMeshIndex; }

	/**
	 * LookVariants.Bodies apply to a body that is one of them: a child Blueprint (or a placed instance) that replaced the mesh
	 * with another body keeps its own look (BP_Enemy_FrozenPMC_Fighter under BP_Enemy_Frostbitten, 2026-10-21).
	 */
	bool UsesLookBodiesFor(const class USkeletalMesh* CandidateMesh) const;
	int32 GetLookMaterialSetIndex() const { return LookMaterialSetIndex; }

	/** Placed in the level: play the entrance clip at BeginPlay (wave / horde spawns always play it when the ABP has one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Animation")
	bool bPlayEntranceWhenPlaced = false;

	/** Pins this enemy's variant seed (look, clip variants); -1 = derived from its name and spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy|Look")
	int32 VariantSeedOverride = -1;

	/** Per-instance look variants (mesh x material set) picked at BeginPlay by the variant seed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy|Look")
	FEnemyLookVariants LookVariants;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	EEnemyArchetype GetArchetype() const { return Archetype; }

	/** Name used in radio lines (Godot enemy_name). */
	const FString& GetEnemyDisplayName() const { return EnemyDisplayName; }

	/**
	 * Godot apply_balance_config (enemy_base.gd + enemy_frost_hound / spitter / brute.gd): crit chance / multiplier and,
	 * for hounds, spitters and brutes, health / speed / damage / range / cooldown from the imported game_balance_config.
	 * Called at the end of ApplyArchetypeDefaults with the game mode's GameBalanceConfig.
	 */
	void ApplyBalance(const class UGodotBalanceAsset& Config);

	/**
	 * Level wave modifiers (Godot main.gd _spawn_custom_json_wave): max health = CustomHealth * HpMult when the spawn
	 * has custom_stats.health, else max health * HpMult (full health); attack damage * DamageMult; speed * SpeedMult.
	 */
	void ApplyWaveModifiers(float HpMult, float DamageMult, float SpeedMult, float CustomHealth = 0.f);

	/** The wave modifiers plus the spawn's custom_stats (health, damage, speed, attack range / cooldown). */
	void ApplySpawnEntry(const struct FWaveModifiers& Mods, const struct FEnemySpawnEntry& Entry);

	/** Avoids burning barrels (Godot fears_fire; turn-based enemies keep out of the 5 x 5 fire-fear area). */
	bool DoesFearFire() const { return bFearsFire; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	/** The pack coordinator may send it at Candidate: no recent dead end there and (fire fearers) not inside a fire zone. */
	bool IsTargetUsableForTactics(const AActor* Candidate) const;
	/** Dev diagnostics: path following, progress timer, fear / braving / fall-back, dead-end targets, status. */
	FString GetDebugState() const;

	/** EXP every squad member gets for this kill. */
	int32 GetKillExpReward() const { return KillExpReward; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsDying() const { return bIsDying; }

	/** Sprint 14 knockdown (Frostbitten / Brute only; the Brute has heavy poise; TANDEM request #12). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Knockdown")
	TObjectPtr<class UKnockdownComponent> KnockdownComponent;

	/** Knocked down (any phase): no AI, no attacks until it is up. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	bool IsKnockedDown() const;

	/**
	 * Turn-based hold (bug fix 2026-10-06, MarksmanCloseShotSmoke): UTurnBasedCombatSubsystem sets it on every enemy it
	 * freezes (grid units and the ones in stasis) and clears it when the fight ends. While held the AI controller refuses
	 * navigation moves and the movement is stopped at once; event-driven reactions (a hit, a timer) must not move it —
	 * the grid alone moves its units. The actor tick being off is not enough: the movement component and the path
	 * following keep running.
	 */
	void SetTurnBasedHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsTurnBasedHeld() const { return bTurnBasedHeld; }

	// --- Ladders (docs/design/LADDERS.md 7.5, user decision 2026-10-22: non-mutant humans climb, mutants never) ---

	/**
	 * This enemy class climbs ladders: the zombified PMC (BP_Enemy_FrozenPMC_Fighter), the Military Zombie, the Marksman (class
	 * default here) and the future PMC mercenaries; never a mutant (Brute, Cutter, Frostbitten, Hound, Spitter, Cryo Drone). A
	 * climber paths with UNavFilter_NoVaultLadder (the ladder links open) and climbs with LadderClimb; a non-climber's filter keeps
	 * the links closed (it paths around or stops short). Treated as false, with a warning, when the clips cannot load.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy|Ladder")
	bool bCanClimbLadders = false;

	/** The climb (the operatives' component and clips; ticks only on a climbing class). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Enemy|Ladder")
	TObjectPtr<class ULadderClimbComponent> LadderClimb;

	/** bCanClimbLadders and the clips are there (the AI controller's nav filter follows it). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy|Ladder")
	bool CanClimbLadders() const;

	/** On a ladder (moving onto / off it, climbing, falling off it). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy|Ladder")
	bool IsClimbingLadder() const;

	/** The climb owns the body: on the ladder or waiting at its foot / head for it (the AI is suspended). */
	bool IsLadderBusy() const;

	/** The climb is over: the AI asks for its move afresh and walks on to Destination (the path's goal before the ladder). */
	void ResumeMoveAfterLadder(const FVector& Destination);

	/** Hook: the climb took the body (the marksman drops his aim). */
	virtual void OnLadderClimbStarted() {}

	/** Where a save taken now puts it: the end of the ladder it heads to while climbing (LADDERS.md 7.8), else its location. */
	FVector GetSaveLocation() const;

	/**
	 * Horde member (UHordeSubsystem, user request 2026-10-06): it knows where every operative is (no sight / hearing
	 * needed — UTacticalSightSubsystem::GetBelief answers «perceived»), goes straight for the squad and never falls back.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "CodexTactics|Enemy")
	bool bKnowsSquadPosition = false;

	/**
	 * Godot enemy_base.gd _update_overhead_ui: armor tier marker, name, statuses, HP (hound / cutter 1.15 m, brute 2.4 m,
	 * others 1.8 m up). The status emoji the HUD font lacks are written as words.
	 */
	bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackDamage() const { return AttackDamage; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackRange() const { return AttackRange; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackCooldown() const { return AttackCooldown; }

	/** Attacks target directly (applies damage spec to target's health component). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void AttackTarget(AActor* Target);

	/** Finds the closest living operative from USquadSubsystem. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	AActor* FindClosestSquadMember() const;

	/**
	 * Godot _find_closest_squad_member: operatives, turrets and the generator weighted by EnemyAIRules::SelectTarget
	 * (small enemies go for the generator / turrets first, large ones for the squad).
	 */
	AActor* FindTarget() const;

	/** Cutter pounce (Godot enemy_cutter.gd): windup, ballistic flight, impact, recovery. */
	enum class ECutterJumpPhase : uint8 { None, Windup, Airborne, Impact };

	/** Godot start_jump_attack: the cutter leaps at Target (3.5-9 m, jump config from DA_EnemyAnim_cutter). */
	bool StartJumpAttack(AActor* Target);
	bool IsJumpAttacking() const { return JumpPhase != ECutterJumpPhase::None; }
	/** Seconds until the next pounce. */
	float GetJumpCooldown() const { return JumpCooldownTimer; }

	/**
	 * It attacks Target (blow, pounce, spit, marksman shot). An operative attacked by an enemy placed in the map while
	 * the squad still explores starts the fight right away (ULevelEncounterSubsystem, EAmbushTrigger::EnemyAttack; user
	 * bug 2026-10-11), so the squad fights back per its fire posture. Call before the hit lands.
	 */
	void NotifySquadAttacked(AActor* Target);

	/** Blueprint hooks for animations / effects (the enemy's AnimBP can also read UEnemyAnimInstance). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnAttackStarted(AActor* Target);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnHitReaction(float Damage);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnDeath();

	/** Blueprint hooks for the jump animation (the cutter Blueprint plays its clips). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackStarted();
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackImpact();

	/** Godot is_fleeing_fire: running from a burning barrel / warm zone. */
	bool IsFleeingFire() const { return bFleeingFire; }

	/**
	 * No usable target (all in fire / heat zones or out of reach): the enemy goes for the nearest operative and ignores
	 * its fear of fire until a usable target appears (user decision 2026-10-02; it used to wait at the zone's edge).
	 */
	bool IsBravingFire() const { return bBravingFire; }

	/** Current victim (operative, turret or generator). */
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** Smokes: forces the victim (the behaviour tick may re-pick it; freeze the enemy with CustomTimeDilation 0 first). */
	void SetCurrentTargetForTesting(AActor* Target) { CurrentTarget = Target; }

	/**
	 * Operatives first (the game designer's rule 2026-10-25): FindTarget's last verdict of EnemyAIRules::MayBeDistractedByObjects -
	 * true when a barricade / turret may take this enemy off the squad (every operative it could go for already has
	 * GetOperativeAttackersBeforeObjects() other enemies on him, or none is left to go for).
	 */
	bool IsObjectDistractionAllowed() const { return bObjectDistractionAllowed; }

	/**
	 * 2026-10-25: can this enemy get within bite reach of Target on foot? A navmesh path query with its own filter (no vaulting
	 * over barricades; partial paths allowed) whose end must pass EnemyAIRules::PathEndReachesTarget. False as well while the
	 * stuck detector has Target on its no-way list (UnreachableUntil). Never a guess against the squad: no navmesh, a target off
	 * the navmesh or a failed query count as reachable (the stuck detector decides then). Cached for 0.5 s per target.
	 */
	bool IsTargetReachableOnFoot(const AActor* Target) const;

	/** Codex.Enemy.OperativeAttackersBeforeObjects (ai_tuning.json "cvars"; code default 2; 0 = the Godot weighting). */
	static int32 GetOperativeAttackersBeforeObjects();

	/** Shared enemy tuning (Godot enemy_* / spitter_preferred_range keys). */
	FEnemyAIConfig AIConfig;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Enemy")
	FOnEnemyDiedDynamic OnEnemyDied;

	FOnEnemyDiedNative OnEnemyDiedNative;

	// --- Sprint 11 outpost stealth patrols (no Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md) ---

	/**
	 * Spline route this enemy patrols from map start (no preparation delay) until it is alerted: it walks waypoint to
	 * waypoint at PatrolWalkSpeed, pauses at each, then turns towards the next segment. Null: no route (wave enemies
	 * behave as before; a marksman falls back to his legacy PatrolRoute points; an enemy placed in the map without any
	 * route guards its post - bGuardPostWhenUnrouted, user request 2026-10-11).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	TObjectPtr<APatrolRouteActor> AssignedPatrolRoute;

	/**
	 * Leader this enemy escorts (e.g. a frost hound with a marksman): it stays 200-350 cm from him while he patrols and
	 * breaks off with him. Not BlueprintReadWrite (UHT does not expose weak pointers to Blueprints): Get/SetEscortLeader.
	 */
	UPROPERTY(EditInstanceOnly, Category = "CodexTactics|Patrol")
	TWeakObjectPtr<AEnemyCharacter> EscortLeader;

	/** Walk speed on the patrol route / while escorting, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0"))
	float PatrolWalkSpeed = 190.f;

	/**
	 * Use PerceptionOverride instead of its archetype's row in Content/Data/AI/enemy_perception.json (a special guard on
	 * one map, smokes). Off: the data file decides (user request 2026-10-06; replaces the fixed 30 m PatrolSightRange).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Perception")
	bool bOverridePerception = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Perception", meta = (EditCondition = "bOverridePerception"))
	FEnemyPerceptionParams PerceptionOverride;

	/** Sight / hearing / smell in force (override or the archetype's data; smell only for hounds). */
	FEnemyPerceptionParams GetPerception() const;

	/** A tripwire / mine detonation this close (planar) breaks its patrol, cm (user decision 2026-10-06: 20 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0"))
	float PatrolTrapAlertRadius = PatrolRouteRules::TrapAlertRadiusCm;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	AEnemyCharacter* GetEscortLeader() const { return EscortLeader.Get(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Patrol")
	void SetEscortLeader(AEnemyCharacter* NewLeader);

	/**
	 * Puts a spawned enemy on patrol at runtime (level-placed ones start from their AssignedPatrolRoute / EscortLeader at
	 * BeginPlay): walks Route, or escorts Leader when one is given. Both null: no patrol.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Patrol")
	virtual void StartPatrol(APatrolRouteActor* Route, AEnemyCharacter* Leader);

	/** Still on its patrol / escort duty (not alerted). Wave enemies without a route or leader: false. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	virtual bool IsOnPatrol() const { return bPatrolActive; }

	/**
	 * Patrol -> Engage: stops the walk, alerts its leader / escorts, remembers AlertLocation (an engaged enemy that knows
	 * of no operative goes there). No-op when not on patrol.
	 */
	virtual void BreakPatrol(EPatrolAlertCause Cause, const FVector& AlertLocation);

	/**
	 * A tripwire / mine / placed charge went off at Location: within PatrolTrapAlertRadius the patrol starts a search
	 * (user amendment 2026-10-06: a trap no longer breaks it into Engage, so it does not start the fight).
	 */
	void NotifyTrapTriggered(const FVector& Location);

	/** Every enemy in World hears a trap that went off at Location (ATripwireActor / AProximityMineActor / ApplyBlast). */
	static void AlertPatrolsNearTrap(UWorld* World, const FVector& Location);

	/**
	 * A squad gunshot / thrown grenade at Location: every patrolling enemy within its hearing radius (x the search
	 * multiplier while searching) breaks into Engage (and the fight starts on an ambush level).
	 */
	static void NotifySquadNoise(UWorld* World, const FVector& Location, ESquadNoise Noise);

	/**
	 * Patrol -> Search: walks to Location (faster than the patrol pace), then sweeps random reachable points around it
	 * with heightened perception for the level's search time (60 s), then returns to the nearest waypoint of its route.
	 * Its leader / escorts search too. A new blast moves the search there and restarts the clock. No-op off patrol.
	 */
	virtual void StartPatrolSearch(const FVector& Location);

	/**
	 * StartPatrolSearch with a spec (the stealth lure's investigation). The re-target rule (StealthLureRules::ReactToNoise, fix
	 * 2026-10-18): a NEWER noise wins over the point it already searches / investigates (lure, trap, scent search) - the origin
	 * moves there, a lure's listening beat plays again, the walk and the clock restart; a hound following a scent closer than
	 * StealthLureRules::ScentHoldsNoiseCm keeps following; a sniffing hound / one on a far scent drops it and its nose is held
	 * (only the close open-air detection works) until it reaches the noise. The whole patrol goes along: its leader and every
	 * escort of the patrol move to the same noise, searching or not. Returns this enemy's reaction (Ignored off patrol duty).
	 */
	ENoiseReaction StartPatrolSearchWith(const FVector& Location, const FPatrolSearchSpec& Spec);

	/**
	 * Its navmesh walking path to Point (projected onto its navmesh), cm: how far a sound travels round walls / through doorways
	 * to it (StealthLureRules::HearsAlongPath). -1 = no navmesh / no full path.
	 */
	float GetHearingPathLengthTo(const FVector& Point) const;

	/**
	 * Investigating a whistle / thrown stone (stealth lure): searching with a lure spec. Hook for a later stealth kill: while it
	 * looks around at the noise (HasReachedSearchOrigin) it faces away from where it came from.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsInvestigatingLure() const { return bSearching && ActiveSearchSpec.bLure; }

	/**
	 * Hound scent (game designer 2026-10-18, ScentRules): caught a scent and sniffs (Sniffing) or follows it slowly (Following);
	 * None otherwise. The HUD's scent marker; full detection only close in open air or by sight.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	EHoundScentPhase GetScentPhase() const { return ScentState.Phase; }

	/** Sniffing or following a scent (GetScentPhase != None). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsOnScent() const { return ScentState.Phase != EHoundScentPhase::None; }

	/** Where it last smelled an operative (the follow goal / the lost-scent search point). */
	FVector GetScentPoint() const { return ScentState.ScentPoint; }

	/** Investigating the point where it lost a scent (a patrol search with a scent spec). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsInvestigatingScent() const { return bSearching && ActiveSearchSpec.bScent; }

	/**
	 * Walls the scent from Target passes on its way to the nose: the clearest of a few offset traces (straight, over a low obstacle,
	 * either side - so a doorway / gap counts as open), each up to PerceptionRules::MaxHearingOccluders.
	 */
	int32 CountScentWallsTo(const AActor& Target) const;

	/** Why its patrol broke last (smokes, the Jev coach log); valid once HasBrokenPatrol. */
	EPatrolAlertCause GetLastPatrolBreakCause() const { return LastPatrolBreakCause; }
	bool HasBrokenPatrol() const { return bHasBrokenPatrol; }

	/** The point it searches round (the trap blast / the lure noise). */
	FVector GetSearchOrigin() const { return SearchOrigin; }

	/** The search walked to its origin already (now sweeping / looking around there). */
	bool HasReachedSearchOrigin() const { return bSearching && bSearchReachedOrigin; }

	/**
	 * Off patrol duty and aware of the squad (a target, an alarm point, the horde's knowledge): "the squad is detected" for the
	 * stealth lure (StealthLureRules::IsHuntingSquad).
	 */
	bool IsHuntingSquad() const;

	/**
	 * World-blocking surfaces (pawns / silhouettes ignored) between its ear and Point, up to PerceptionRules::MaxHearingOccluders
	 * (the footstep hearing trace; the stealth lure uses it for its noise point).
	 */
	int32 CountHearingWallsTo(const FVector& Point) const;

	/** Hunting for the squad after a trap (still on patrol duty, not engaged). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsSearching() const { return bSearching; }

	/** Suspicion meter 0..1 of the patrol sight (1 = detected). */
	float GetSuspicion() const { return PatrolSuspicion; }

	/** True while a trap / placed charge deals its blast damage (AInteractableActor::ApplyBlast): damage = trap event. */
	static bool IsTrapBlastInProgress() { return TrapBlastDepth() > 0; }

	/** Nesting depth of FScopedTrapBlast (game thread only). */
	static int32& TrapBlastDepth();

	/** Marks the damage dealt inside its lifetime as a trap blast (search, not the squad's direct attack). */
	struct FScopedTrapBlast
	{
		FScopedTrapBlast() { ++TrapBlastDepth(); }
		~FScopedTrapBlast() { --TrapBlastDepth(); }
		FScopedTrapBlast(const FScopedTrapBlast&) = delete;
		FScopedTrapBlast& operator=(const FScopedTrapBlast&) = delete;
	};

	/** 2026-10-23: nesting depth of FScopedSquadTrapBlast (game thread only). */
	static int32& SquadTrapBlastDepth();
	/** True while a trap the SQUAD placed (its tripwire, its mine) blasts: a kill raises "EnemyKilled.SquadTrap" too. */
	static bool IsSquadTrapBlastInProgress() { return SquadTrapBlastDepth() > 0; }

	/**
	 * 2026-10-23 (the A4 lesson "take him quietly with a tripwire or a mine"): wraps the blast of a squad-placed trap; an enemy it kills
	 * raises "EnemyKilled.SquadTrap" before "EnemyKilled.Trap" / "EnemyKilled". Inactive (bActive false): nothing.
	 */
	struct FScopedSquadTrapBlast
	{
		explicit FScopedSquadTrapBlast(bool bInActive) : bActive(bInActive) { SquadTrapBlastDepth() += bActive ? 1 : 0; }
		~FScopedSquadTrapBlast() { SquadTrapBlastDepth() -= bActive ? 1 : 0; }
		FScopedSquadTrapBlast(const FScopedSquadTrapBlast&) = delete;
		FScopedSquadTrapBlast& operator=(const FScopedSquadTrapBlast&) = delete;
		bool bActive = false;
	};

	/** Index of the waypoint it walks to / waits at (smokes, debugging). */
	int32 GetPatrolWaypointIndex() const { return PatrolWaypointIndex; }

	/** Save game: where it is on its patrol / search. */
	FEnemyPatrolSnapshot CapturePatrolSnapshot() const;

	/**
	 * Save-game load: assigns Route / Leader and resumes the saved patrol (waypoint, phase, wait, search with its
	 * elapsed time) without alert lines; Snapshot.bOnPatrol false keeps it off patrol (route / leader remembered).
	 */
	virtual void RestorePatrolSnapshot(APatrolRouteActor* Route, AEnemyCharacter* Leader, const FEnemyPatrolSnapshot& Snapshot);

	// --- Guard post (user request 2026-10-11) ---

	/**
	 * Placed in the map without a route or an escort leader: it guards its placement (stands, faces its placement
	 * rotation, looks around) until its perception detects the squad (sight / hearing / smell; a trap sends it searching,
	 * then it walks back), then the normal ambush flow. Off: it hunts the squad from level start (the behaviour before
	 * 2026-10-11). Wave / runtime spawns always hunt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	bool bGuardPostWhenUnrouted = true;

	/** Look-around sweep of a guard to each side of its placement facing, degrees (0 = stares ahead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0", ClampMax = "180"))
	float GuardLookAroundDeg = PatrolRouteRules::GuardLookAroundDeg;

	/** One look-around cycle (ahead, left, ahead, right, ahead), seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "1"))
	float GuardLookPeriodSeconds = PatrolRouteRules::GuardLookPeriodSeconds;

	/** Guarding its placement (on patrol duty without a route; see bGuardPostWhenUnrouted). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsGuardingPost() const { return bGuardingPost && IsOnPatrol(); }

	/** Where it guards (its placement). */
	FVector GetGuardPostLocation() const { return GuardPostLocation; }

	/**
	 * Starts guarding the current spot when PatrolRouteRules::ShouldGuardPost allows it (placed in the map, no route, no
	 * escort leader, bGuardPostWhenUnrouted). Called at BeginPlay and by ULevelEncounterSubsystem::MarkLevelEnemy.
	 * True when it guards.
	 */
	bool TryStartGuardPost();

	// --- Vision cone (user request 2026-10-11; Godot had cones on the operatives only) ---

	/** Ground cone of its sight (range / field of view from GetPerception; colour idle / suspicious / alert). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vision Cone")
	TObjectPtr<UVisionConeComponent> VisionCone;

	/**
	 * Draw this enemy's vision cone. Set it on a BP_Enemy_*'s Class Defaults or on one enemy placed in a level; console
	 * Codex.Debug.VisionCones 0 / 1 overrides every actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone")
	bool bShowVisionCone = false;

	/** Keep the (red) cone once it hunts / fights the squad; off: only while on patrol / guard / search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone")
	bool bShowVisionConeWhenEngaged = true;

	/** Sight range the cone shows: a standing operative's detection range (x the search multiplier while searching), cm. */
	float GetVisionConeRange() const;
	float GetVisionConeHalfAngle() const;

protected:
	/** Legacy marksman PatrolRoute points (a route of their own: no guard post). */
	virtual bool HasLegacyPatrolRoute() const { return false; }

	/** Guard driver: back to the post when away, else face the placement yaw with the look-around sweep. */
	void TickGuardPost(float DeltaTime);

	/** Shape / colour / visibility of the vision cone (every tick). */
	void UpdateVisionCone(float DeltaTime);

	bool bGuardingPost = false;
	bool bGuardReturning = false;
	FVector GuardPostLocation = FVector::ZeroVector;
	float GuardPostYaw = 0.f;
	float GuardClock = 0.f;

	/** Patrol state of the route driver (TickPatrolRoute). */
	enum class EPatrolPhase : uint8 { Moving, Waiting, Turning, Finished };

	/** Set at BeginPlay when it has a route or a leader; cleared by BreakPatrol. */
	bool bPatrolActive = false;
	EPatrolPhase PatrolPhase = EPatrolPhase::Moving;
	int32 PatrolWaypointIndex = 0;
	bool bPatrolForward = true;
	float PatrolWaitLeft = 0.f;
	float PatrolTurnTime = 0.f;
	float PatrolStuckTime = 0.f;
	bool bPatrolMoveIssued = false;
	float PatrolSightTimer = 0.f;
	bool bEscortMoving = false;
	/** Last tether point it was sent to (a new move only when the leader walked on). */
	FVector EscortGoal = FVector::ZeroVector;
	/** Where the alarm came from: an engaged enemy with no known operative investigates it. */
	FVector PatrolAlertLocation = FVector::ZeroVector;
	bool bHasPatrolAlertLocation = false;

	/** Walks AssignedPatrolRoute (move, wait, turn to the next segment). */
	void TickPatrolRoute(float DeltaTime);
	/** Patrol / escort tick of the base enemy: sight check, leader mirror, tether or route. */
	void TickPatrolBehavior(float DeltaTime);
	/** Keeps the 200-350 cm tether to Leader. */
	void TickEscort(float DeltaTime, const AEnemyCharacter& Leader);
	/** Moves towards a patrol waypoint / tether point at PatrolWalkSpeed (the marksman walks his own way). */
	/** Walks to Goal at Speed (patrol / search / escort moves); false when the move request was refused (no path). */
	virtual bool IssuePatrolMove(const FVector& Goal, float Speed);
	/**
	 * Where a search / lure walk to Noise ends for this enemy (StealthLureRules::ChooseInvestigatePoint): Noise on this agent's
	 * navmesh when reachable, else the closest reachable point of its path, else the projected / raw point.
	 */
	FVector ResolveSearchPoint(const FVector& Noise) const;
	/**
	 * The closest living operative it sees now (PerceptionRules::CanSee with the Sprint 08 eye -> profile trace, so a
	 * prone operative behind 60 cm cover stays hidden); OutDistance / OutRange: distance and the stance sight range.
	 */
	AActor* FindVisibleOperative(const FEnemyPerceptionParams& Params, float& OutDistance, float& OutRange) const;

	/**
	 * World-blocking surfaces (walls, buildings; pawns and silhouettes ignored) between its ear and Target's body, up to
	 * PerceptionRules::MaxHearingOccluders: each one cuts the footstep hearing radius (HearingOcclusionPerWall).
	 */
	int32 CountHearingWalls(const AActor& Target) const;
	/**
	 * Sight (suspicion build-up), hearing (squad gait) and smell (hounds) every 0.2 s; a detection breaks the patrol
	 * (BreakPatrol). True when it broke.
	 */
	bool TickPatrolPerception(float DeltaTime);
	/** Perception in force on patrol (x the search multiplier while searching). */
	FEnemyPerceptionParams GetPatrolPerception() const;
	/**
	 * The hound's nose this perception step (ScentRules over every living operative within the smell radius, walls counted by
	 * CountScentWallsTo) -> the scent state machine; true when it detected (the patrol broke).
	 */
	bool TickScentPerception(const FEnemyPerceptionParams& Params, float StepSeconds);
	/** Scent driver while IsOnScent: stand and sniff, or walk slowly to the scent point. True while it drives the hound. */
	bool TickScentBehavior(float DeltaTime);
	/** Back to no scent (patrol break, a new patrol, a load). */
	void ResetScent();
	/** World-blocking surfaces between From and To (pawns / silhouettes ignored), each component once, up to Max. */
	int32 CountOccludersBetween(const FVector& From, const FVector& To, int32 Max) const;
	/** Search driver: blast point, then sweep points, until the search time is over. */
	void TickPatrolSearch(float DeltaTime);
	/** Search over: back to the nearest waypoint (route) / the tether (escort). */
	void EndPatrolSearch();
	/** «[Stealth] detection by <sense> at <t> s» log line for the Jev AI coach. */
	void LogStealthDetection(EPatrolAlertCause Cause, bool bWasSearching) const;
	/** StartPatrolSearchWith for this enemy alone (the re-target rule, no propagation to its patrol). */
	ENoiseReaction ApplySearchNoise(const FVector& Location, const FPatrolSearchSpec& Spec);
	/** World AI held (dialogue, cutscene): stops once and returns true — the behaviour tick does nothing else. */
	bool HoldForWorldAIPause();

	bool bSearching = false;
	FVector SearchOrigin = FVector::ZeroVector;
	FVector SearchGoal = FVector::ZeroVector;
	/** The raw noise / blast point (SearchOrigin = where the walk there ends on the navmesh). */
	FVector SearchNoise = FVector::ZeroVector;
	/** Lure: listening left before the walk to the noise, s. */
	float SearchListenLeft = 0.f;
	/** World time SearchNoise was set (the re-target rule's same-noise check); < 0 = never. */
	double SearchNoiseTime = -1.0;
	/** A noise took it off a scent: its nose only detects close in open air until it reaches the noise (runtime only). */
	bool bScentHeldByNoise = false;
	/** Refused move requests on the current leg (StealthLureRules::RepathAfterFailures / GiveUpAfterFailures). */
	int32 SearchMoveFailures = 0;
	float SearchElapsed = 0.f;
	float SearchLegTime = 0.f;
	float SearchLookLeft = 0.f;
	float SearchStuckTime = 0.f;
	bool bSearchReachedOrigin = false;
	bool bSearchMoving = false;
	/** Spec of the running search (default = a trap search). */
	FPatrolSearchSpec ActiveSearchSpec;
	/** Hound scent episode (ScentRules::Step). */
	FHoundScentState ScentState;
	/** Last follow goal sent to the AI (a new move only when the scent point moved). */
	FVector ScentMoveGoal = FVector::ZeroVector;
	bool bScentMoveIssued = false;
	float ScentStuckTime = 0.f;
	EPatrolAlertCause LastPatrolBreakCause = EPatrolAlertCause::PartnerAlert;
	bool bHasBrokenPatrol = false;
	float PatrolSuspicion = 0.f;
	bool bHeldByAIPause = false;
	/** See SetTurnBasedHeld. */
	bool bTurnBasedHeld = false;
	/** bCanClimbLadders but the ladder clips did not load (BeginPlay): treated as a non-climber. */
	bool bLadderClipsMissing = false;
	/** BeginPlay: the climb ticks on a climbing class with clips; the controller's nav filter follows (EnemyCharacterLadder.cpp). */
	void InitLadderClimb();
	/** Subclass hook of SetTurnBasedHeld (the marksman cancels his get-up / aim). */
	virtual void OnTurnBasedHeldChanged(bool bHeld) {}
	/** Breaks the patrol of its leader and of every enemy escorting it. */
	void PropagatePatrolBreak(const FVector& AlertLocation);
	/** Sets up the patrol from AssignedPatrolRoute / EscortLeader (BeginPlay). */
	void InitPatrol();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Enemy")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/**
	 * The enemy type (stats, name, AI). A placed enemy left at its class default takes the game mode's EnemyClasses mapping
	 * for its Blueprint (BP_Enemy_Brute -> Brute); set it on the instance to override.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	EEnemyArchetype Archetype = EEnemyArchetype::FrostHound;

	/** Set by BeginPlay when no spawner scope was active. */
	bool bPlacedInLevel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	FString EnemyDisplayName = TEXT("Frost Hound");

	/**
	 * Nameplate / feed name of this Blueprint instead of its archetype's (a variant body of an archetype:
	 * BP_Enemy_FrozenPMC_Fighter = a Frostbitten named "Frozen PMC"). Empty = the archetype's name. Stats stay the archetype's.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	FString DisplayNameOverride;

	/** Use KnockdownClipFolder instead of the archetype's knockdown clips (a body on another skeleton than the archetype's). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy|Knockdown", meta = (InlineEditConditionToggle))
	bool bOverrideKnockdownClipFolder = false;

	/**
	 * Folder under /Game/Animations_KnockDown with this body's Knocked_ / Revive_ / Death_ Back / Front sequences; empty = that
	 * folder itself (the UE5 Manny clips, used by BP_Enemy_FrozenPMC_Fighter). Only for archetypes that can be knocked down.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy|Knockdown", meta = (EditCondition = "bOverrideKnockdownClipFolder"))
	FString KnockdownClipFolder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackDamage = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackRange = 180.0f; // 1.8m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackCooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritChance = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritMultiplier = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	bool bFearsFire = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	bool bIsDying = false;

	/** EXP every squad member gets for this kill (Godot exp_reward_<type>; ProgressionRules::KillReward). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	int32 KillExpReward = 9;

	float AttackTimer = 0.0f;

	/** Seconds the attack clip still plays: the enemy stands and faces AttackLockTarget (Godot is_attacking). */
	float AttackLockTimer = 0.0f;
	TWeakObjectPtr<AActor> AttackLockTarget;

	/** Starts the attack clip and the stand-still lock; the cooldown lasts at least the clip (Godot _play_attack_animation). */
	void StartAttackAnimation(AActor* Target);

	/** Seconds it still stands for its alert roar / spawn entrance (TickBehavior holds it; a hit / knockdown that cuts the clip ends it). */
	float PerformanceLockTimer = 0.f;
	/** Where the roar faces (the alarm point); unset = keeps its facing (the entrance). */
	TOptional<FVector> PerformanceFacePoint;
	/** The alert roar when it notices the squad (BreakPatrol): UEnemyAnimInstance::NotifyAlert + the stand-still lock. */
	void StartAlertPerformance(const FVector& AlertLocation);
	/** TickBehavior's hold while performing: stops the moves, turns to the alarm point, ends with the clip. True while it holds. */
	bool TickPerformanceLock(float DeltaTime);
	/** BeginPlay: applies LookVariants (mesh / materials) by the variant seed. */
	void ApplyLookVariants();
	mutable TOptional<uint32> CachedVariantSeed;
	int32 LookMeshIndex = -1;
	int32 LookMaterialSetIndex = -1;

	// --- Cutter jump (Godot enemy_cutter.gd; numbers from /Game/Data/Enemies/DA_EnemyAnim_cutter) ---
	bool bJumpAttackEnabled = false;
	float JumpAttackSpeed = 1.85f;
	float JumpMinDistance = 350.f;
	float JumpMaxDistance = 900.f;
	float JumpCooldown = 6.f;
	float JumpAttackDamage = 28.f;
	float JumpDamageRadius = 220.f;
	ECutterJumpPhase JumpPhase = ECutterJumpPhase::None;
	float JumpPhaseTimer = 0.f;
	float JumpCooldownTimer = 0.f;
	float JumpFlightDuration = 0.7f;
	bool bJumpDamageDealt = false;
	bool bAirborneDeath = false;
	TWeakObjectPtr<AActor> JumpTarget;
	void TickJumpAttack(float DeltaTime);
	void ApplyJumpImpactDamage();
	virtual void Landed(const FHitResult& Hit) override;

	/** Walk speed before frost / fear factors (archetype, balance, wave modifiers). */
	float BaseWalkSpeed = 300.f;
	bool bFleeingFire = false;
	/** Set by FindTarget (see IsBravingFire). */
	mutable bool bBravingFire = false;
	/** Set by FindTarget (see IsObjectDistractionAllowed). */
	mutable bool bObjectDistractionAllowed = true;
	/** IsTargetReachableOnFoot cache: the target, when it was checked (world seconds) and the verdict. */
	mutable TWeakObjectPtr<const AActor> ReachCheckTarget;
	mutable double ReachCheckTime = -1.0;
	mutable bool bReachCheckResult = true;
	/** The last obstacle this enemy walked round for an operative (logged once per obstacle). */
	TWeakObjectPtr<AActor> LoggedObstacleSkip;
	/** Godot last_attacker_source (a turret hit pulls large enemies to turrets). */
	FString LastAttackerSource;
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Targets it could not get closer to (no path): skipped until the time (world seconds). */
	TMap<TWeakObjectPtr<AActor>, double> UnreachableUntil;
	/** Progress towards the current target (stuck detection). */
	TWeakObjectPtr<AActor> ProgressTarget;
	FVector ProgressLocation = FVector::ZeroVector;
	float ProgressTimer = 0.f;
	/** Running a morale fall-back (the floating text once per fall-back). */
	bool bFallingBack = false;
	/** The target this enemy already went round (flank done: straight in from now on). */
	TWeakObjectPtr<AActor> FlankDoneTarget;
	/** The running flank detour (2026-10-18): its target, time, progress probe (EnemyTacticsRules::EvaluateFlankDetour). */
	TWeakObjectPtr<AActor> FlankDetourTarget;
	float FlankDetourElapsed = 0.f;
	float FlankDetourStuck = 0.f;
	FVector FlankDetourProbe = FVector::ZeroVector;
	/** Seconds standing on a barricade / barrel top (VaultNavigation::IsStandingOnObstacle). */
	float ObstacleTopTime = 0.f;

	/** Nearest burning barrel / active heat source within the fear radius (Godot _find_nearest_active_fire_source). */
	bool FindNearestFire(FVector& OutFire) const;
	/** Nearest burning barrel / active heat zone whose fear radius (OutRadius) reaches within Extra of the feet. */
	bool FindNearestFireZone(float Extra, FVector& OutFire, float& OutRadius) const;
	/** Location inside a burning barrel's / active heat zone's fear radius (the heat of Candidate itself excluded). */
	bool IsInFearZone(const FVector& Location, const AActor* Candidate) const;
	/**
	 * 2026-10-22: nothing solid between the heat source at SourceLocation (owned by SourceOwner) and the point Location (EnemyAIRules::
	 * FeelsHeat): a visibility trace at EnemyAIRules::HeatLineHeightCm, pawns and IgnoreActor passed through.
	 */
	bool HasClearHeatLine(const FVector& SourceLocation, const AActor* SourceOwner, const FVector& Location, const AActor* IgnoreActor) const;
	/**
	 * 2026-10-25: where to walk to hit Object (a turret): just outside it on this enemy's side, else the nearest navmesh point round
	 * it - its box cuts a navmesh hole wider than a MoveToActor query reaches. False without a navmesh / any point.
	 */
	bool FindObjectApproachPoint(const AActor* Object, const FVector& Feet, FVector& OutPoint) const;
	/** Barricade / turret in the way within 2.2 m (Godot _find_blocking_barricade). */
	AActor* FindBlockingObstacle(const AActor* Target) const;
	/** Godot _attack_barricade (brutes hit twice as hard) and melee on turrets / the generator. */
	void AttackObject(AActor* Object);
	/** Godot enemy_frost_spitter.gd _process_enemy_behavior. */
	void TickSpitter(float DeltaTime);

	/** APC P4: the manned vehicle this enemy would go for (UVehicleSubsystem::FindEnemyTargetVehicle), nullptr when none. */
	class AVehiclePawn* FindVehicleTarget() const;

	/**
	 * APC P4: a ranged enemy against a manned vehicle (spitter / cryo drone acid, the marksman's rifle): closes to within
	 * RangeCm of the hull with a line from MuzzleHeightCm over the feet to the hull, then hits it every CooldownSeconds
	 * (HitChance) with Damage through the hull's armour for its archetype.
	 */
	void TickRangedVsVehicle(class AVehiclePawn& Vehicle, float DeltaTime, float RangeCm, float Damage, float CooldownSeconds, float MuzzleHeightCm,
		float HitChance);

	/**
	 * The behaviour part of Tick (targets, attacks, movement); Tick then turns the body (UpdateMovementFacing).
	 * Archetypes with their own brain (AMarksmanEnemyCharacter) override it.
	 */
	virtual void TickBehavior(float DeltaTime);

	/**
	 * The per-tick AI's move ask: MoveToLocation(Goal, AcceptanceRadius, no stop on overlap, pathfinding), re-issued only
	 * when the goal changed / the move stopped / the re-path interval passed (AEnemyAIController::RequestMoveToLocation,
	 * audit 2026-10-11 #1). Another controller class moves at once.
	 */
	void RequestAIMoveToLocation(AAIController* AIC, const FVector& Goal, float AcceptanceRadius);
	/** The per-tick AI's chase ask: MoveToActor(Goal, AcceptanceRadius), throttled like RequestAIMoveToLocation. */
	void RequestAIMoveToActor(AAIController* AIC, AActor* Goal, float AcceptanceRadius);
	/** Forgets the last throttled move, so the next ask is issued at once (turn-based hold, world AI pause). */
	void ResetAIMoveThrottle();

	/** Turn towards Yaw this frame (attack lock, aiming, pounce windup): the movement facing then stays out of it. */
	void FaceYaw(float Yaw, float DeltaTime, float InterpSpeed);

	/** Godot enemy_base.gd: face the (smoothed) velocity above 0.2 m/s with lerp_angle(turn_speed), else keep. */
	void UpdateMovementFacing(float DeltaTime);

	/** Godot enemy_base.gd turn_speed (lerp factor, 1/s). */
	float TurnSpeed = 8.f;
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bFacedThisTick = false;
	/** Godot position of a target: operatives at their centre, objects at their base. */
	static FVector GodotPosition(const AActor* Actor);

	UFUNCTION()
	void HandleDamaged(const FDamageSpec& Spec, float FinalDamage);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);

	void ApplyArchetypeDefaults();
	/** A placed enemy: Archetype from the instance / the game mode's class mapping / the class default (logged once). */
	void ResolvePlacedArchetype();

	/** Which archetypes can fall and with which clips (Frostbitten / Brute on their own skeletons). */
	void ConfigureKnockdown();
	/** Knockdown phase change: stops the AI / movement, frees the capsule; restores them when it is up. */
	void HandleKnockdownPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase);
	/** Knocks the operative down after a landed blow (Brute heavy melee, hound pounce after a run-up). */
	void TryKnockDownOperative(class AOperativeCharacter* Operative, float DamageDealt);
	/** Seconds it has been running at full speed (hound pounce run-up). */
	float RunUpSeconds = 0.f;
	/** Seconds since this hound last knocked somebody down. */
	float SecondsSinceHoundPounce = 1000.f;
	TEnumAsByte<ECollisionResponse> KnockdownSavedPawnResponse = ECR_Block;
};
