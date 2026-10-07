#pragma once

#include "CoreMinimal.h"
#include "Bot/BotStealthRules.h"
#include "Bot/PlaytestBotRules.h"
#include "Bot/SpatialTelemetryRecorder.h"
#include "Data/CombatTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/CoverTypes.h"
#include "PlaytestBotSubsystem.generated.h"

class AActor;
class AEnemyCharacter;
class AOperativeCharacter;

/**
 * Autonomous playtest bot (Godot archive tools/bot/bot_driver.gd + smart_tactical_bot.gd): plays the level through —
 * exploration (collects loot crates and loose deployables), the combat zone, the profile's defences in the first
 * preparation, every wave with the tactical decisions (grenade at clusters, leader fallback, barricade or wall cover
 * (Sprint 12 cover slots), stances, the profile's medkit / warming food) — and the run telemetry
 * (URunTelemetrySubsystem, tester_profile = the profile) is written at the end. The fights run in real time with the
 * squad's fire posture set explicitly (Aggressive); the bot drives the operatives directly, no tactical pause.
 * Stealth (user plan 2026-10-07): on an ambush level (ULevelEncounterSubsystem: patrols, combat_start ambush / auto) it
 * never presses «Начать бой»: it explores and sneaks with the Passive posture, picks crouch / prone by the patrols'
 * estimated sight / hearing (BotStealthRules over PerceptionRules), takes wall cover, may lay a mine on a patrol route
 * and hide through the search, and strikes first when in position — or is detected. «[Stealth] bot ...» log lines and
 * the «[Stealth] outcome» line feed Scripts/Tools/jev_ai_coach.py --stealth.
 * Started by the command line
 *   -CodexBot [-BotProfile=CASUAL|NORMAL|VETERAN] [-BotTimeout=<real s, 600>] [-BotLoadout=COLLECT|UNIQUE|PRESET]
 *   [-BotSeed=<n, 1>] [-BotStealth=0 (press the button even on ambush levels)] [-BotSpawnPatrols (runtime patrols on a
 *   map without any, see SpawnRuntimePatrols)]
 * (Scripts/bot_run.ps1 runs it fixed-step without rendering and quits at the end) or the console command
 * CodexTactics.Bot <profile> [nocollect] [seed=<n>] (no exit).
 */
UCLASS()
class CODEXTACTICS_API UPlaytestBotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Starts playing (bQuitAtEnd: exit the game when the run is over / timed out); Seed varies the stealth heuristics. */
	void StartBot(EBotProfile InProfile, bool bQuitAtEnd, bool bCollectLoot = true, int32 InSeed = 1);
	bool IsActive() const { return bActive; }
	EBotProfile GetProfile() const { return Profile; }

	/** The phase the bot is in (logs / smokes). Stealth: an ambush level after the exploration, sneaking up on a patrol. */
	enum class EBotStage : uint8 { Explore, Stealth, EnterCombat, Fight, Done };
	EBotStage GetStage() const { return Stage; }
	int32 GetGrenadesThrown() const { return GrenadesThrown; }
	int32 GetDeploysOrdered() const { return DeploysOrdered; }
	int32 GetItemsUsed() const { return ItemsUsed; }
	int32 GetStallHunts() const { return StallHunts; }
	int32 GetMarksmanReactions() const { return MarksmanReactions; }
	int32 GetMarksmanAssaults() const { return MarksmanAssaults; }
	int32 GetGeneratorRepairs() const { return GeneratorRepairs; }

	/** Sprint 05-D: a marksman aiming at a squad member -> crouch, the leader into cover at once. True when it reacted. */
	bool ReactToMarksman();
	/**
	 * A marksman out of the rifles' reach with no other enemy near the squad: the squad sprints at him (from the sides)
	 * and stops inside rifle range — he can no longer kite away (user decision 2026-10-04). Codex.Bot.* tunables.
	 */
	bool AssaultMarksman();
	const FSpatialTelemetryRecorder& GetSpatialRecorder() const { return Spatial; }

	// --- Stealth (ambush levels) ---
	/** The level starts its fight by ambush and the bot sneaks (no «Начать бой»). */
	bool IsStealthLevel() const { return bStealthLevel; }
	/** The bot pressed «Начать бой» (UMissionSubsystem::StartMission Combat) in this run. */
	bool HasPressedCombatStart() const { return bPressedCombatStart; }
	int32 GetStealthDecisions() const { return StealthDecisions; }
	EBotStealthAction GetStealthAction() const { return StealthAction; }
	EOperativeStance GetStealthStance() const { return StealthStance; }
	/** "" while sneaking; then ambush_<reason> / detected / auto_fire / damage / no_patrols. */
	const FString& GetStealthOutcome() const { return StealthOutcome; }
	const FBotStealthConfig& GetStealthConfig() const { return StealthConfig; }
	int32 GetSeed() const { return Seed; }

	/**
	 * The best Sprint 12 wall-cover slot within RadiusCm of Operative with the wall between it and Threat (16 knee-high
	 * traces, CoverTraceRules::FindCoverSlotAt on every wall face that looks away from the threat; near and high first).
	 */
	bool FindWallCover(const AOperativeCharacter* Operative, const FVector& Threat, float RadiusCm, FCoverSlot& OutSlot) const;

	/**
	 * A map without patrols (-BotSpawnPatrols): lays two routes ahead of the squad at fixed offsets and puts a marksman
	 * with a hound escort on the first, a hound with a hound and a frostbitten escort on the second; makes the level an
	 * ambush level. Returns the number of enemies spawned. Never saves the map.
	 */
	int32 SpawnRuntimePatrols();

private:
	/** First tick with a squad: runtime patrols, stealth level or not, posture, the exploration targets. */
	void SetupRun();
	void TickExplore(float DeltaTime);
	void TickFight(float DeltaTime);
	/**
	 * One stealth decision step (Explore on an ambush level, Stealth stage). True when stealth owns the squad this tick
	 * (the exploration walk must wait).
	 */
	bool TickStealth(float DeltaTime, bool bExploring);
	/** Patrolling enemies (alive, on patrol) and how the bot judges them. */
	void GatherPatrols(TArray<AEnemyCharacter*>& OutEnemies, TArray<FBotPatrolView>& OutViews) const;
	/** Eye line from the patrol enemy to the operative is clear (the Sprint 12 full-wall rule included). */
	bool IsEyeLineClear(const AEnemyCharacter* Enemy, const AOperativeCharacter* Operative) const;
	/** Applies the sneaking stance to the squad (quieter at once, louder only after a second). */
	void ApplyStealthStance(EOperativeStance Stance, float DeltaTime);
	/** Attack order on Target: Aggressive posture, the ambush fight starts (AttackOrder). */
	void StrikeFirst(AEnemyCharacter* Target, EBotAmbushReason Reason, float DistanceCm);
	/** The fight started (by the bot or a detection): outcome line, Aggressive posture, Fight stage. */
	void OnStealthCombatStarted();
	/** The trap opener (StealthConfig.bUseTrap): the medic lays a mine on the target's route. True while it owns the squad. */
	bool TickTrap(AEnemyCharacter* Target, const TArray<AEnemyCharacter*>& Patrols);
	void DeployDefences();
	void CombatAssist(bool bAllowWarmMoves = true);
	void SmartTactics(float DeltaTime);
	void UpdateStances();
	/** The best barricade cover for Leader against Threat within 20 m (false: none). */
	bool FindCover(const AOperativeCharacter* Leader, const FVector& Threat, FVector& OutStand, FString& OutId, float& OutScore) const;

	/** Point on the navigation mesh near Point (false: none within 2 m). */
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	/**
	 * The broken diesel generator (its heat and the turrets' power): the engineer (else anyone) walks up and repairs it —
	 * in the preparation, or in a wave with no enemy within 12 m of it. True while the repair is under way.
	 */
	bool TickGeneratorRepair(float DeltaTime, bool bInWave);
	class AInteractableActor* FindBrokenGenerator() const;
	void Finish(const TCHAR* Result, int32 ExitCode);
	FVector GetFrontDirection() const;
	TArray<AActor*> LiveEnemies() const;
	AOperativeCharacter* Member(int32 Index) const;
	void HandleEnemySpawned(class AEnemyCharacter* Enemy, EEnemyArchetype Archetype);
	void HandleEnemyDied(AActor* Victim, const FString& Source);

	/** Godot spatial_telemetry_recorder: frames / events of the fight for the editor's replay player. */
	FSpatialTelemetryRecorder Spatial;
	bool bSpatialStarted = false;

	EBotProfile Profile = EBotProfile::Normal;
	FBotProfileConfig Config;
	EBotStage Stage = EBotStage::Explore;
	bool bActive = false;
	bool bQuit = false;
	bool bCollect = true;
	double StartRealTime = 0.0;
	float TimeoutSeconds = 600.f;
	float DecisionTimer = 0.f;
	float StatusTimer = 0.f;

	// Exploration
	TArray<TWeakObjectPtr<AActor>> ExploreTargets;
	TWeakObjectPtr<AActor> ExploreTarget;
	float ExploreTime = 0.f;
	float TargetTime = 0.f;

	// Preparation / fight
	bool bDeployed = false;
	/** The veteran's second barricade, ordered once the first one stands (one deploy task per worker at a time). */
	TOptional<FVector> PendingBarricade;
	float PendingYaw = 0.f;
	float DeployWait = 0.f;
	float GrenadeCooldown = 0.f;
	float MoveCooldown = 0.f;
	/** Wave stall: the few last enemies did not die for a while -> the squad goes after them. */
	int32 LastEnemyCount = 0;
	float StallTimer = 0.f;
	int32 StallHunts = 0;
	int32 GrenadesThrown = 0;
	int32 DeploysOrdered = 0;
	int32 ItemsUsed = 0;
	float WarmMoveCooldown = 0.f;
	int32 MarksmanReactions = 0;
	int32 MarksmanAssaults = 0;
	int32 GeneratorRepairs = 0;
	TWeakObjectPtr<class AInteractableActor> RepairTarget;
	TWeakObjectPtr<AOperativeCharacter> RepairWorker;
	float RepairTime = 0.f;
	float RepairCooldown = 0.f;
	/** Heat sources a warm-up trip did not help (cold not dropping): skipped until the world time. */
	TMap<TWeakObjectPtr<const UObject>, double> ColdHeatUntil;
	TWeakObjectPtr<const UObject> WarmTarget;
	float WarmStartCold = 0.f;
	int32 WarmMoves = 0;
	/** ReactToMarksman log throttle (one line per marksman aim, not per tick). */
	TWeakObjectPtr<const AActor> LastReactedMarksman;
	double LastReactLogTime = -100.0;
	int32 WallCoverMoves = 0;

	bool bRunSetUp = false;
	/** The stealth step held the squad during the loot walk: the walk is requested again once it lets go. */
	bool bExploreWalkPaused = false;

	// Stealth
	int32 Seed = 1;
	FBotStealthConfig StealthConfig;
	bool bStealthLevel = false;
	bool bPressedCombatStart = false;
	bool bStealthStarted = false;
	bool bAmbushIssued = false;
	EBotAmbushReason AmbushReason = EBotAmbushReason::None;
	double StealthStartTime = 0.0;
	float StealthDecisionTimer = 0.f;
	float StealthMoveCooldown = 0.f;
	float StanceLouderTime = 0.f;
	int32 StealthDecisions = 0;
	int32 StanceChanges = 0;
	EBotStealthAction StealthAction = EBotStealthAction::Sneak;
	EOperativeStance StealthStance = EOperativeStance::Standing;
	FString StealthOutcome;
	TWeakObjectPtr<AEnemyCharacter> StealthTarget;
	/** Patrol enemies seen on patrol (a break not caused by the bot = a detection). */
	TSet<TWeakObjectPtr<AEnemyCharacter>> KnownPatrols;
	double FirstDetectionTime = -1.0;
	int32 SearchesSeen = 0;
	bool bSearchWasOn = false;
	bool bFollowersHeld = false;
	// Trap opener: 0 none, 1 walking to lay it, 2 retreating, 3 waiting for it, 4 done.
	int32 TrapState = 0;
	double TrapStateTime = 0.0;
	FVector TrapRetreatPoint = FVector::ZeroVector;
	int32 TrapMinesBefore = 0;
};
