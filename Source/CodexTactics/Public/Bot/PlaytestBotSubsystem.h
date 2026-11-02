#pragma once

#include "CoreMinimal.h"
#include "Bot/BotRouteRules.h"
#include "Bot/BotStealthRules.h"
#include "Bot/CombatLabRunCollector.h"
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
 * The playtest bot exists in development builds only (audit 2026-10-11 #3, "bot gate"): in Test / Shipping the subsystem
 * is never created, -CodexBot / the CodexTactics.Bot command / the Codex.Bot.* console variables do not exist, so a
 * shipped game cannot be driven, quit (RequestExitWithStatus) or made to write Saved/Telemetry by it.
 */
#ifndef CODEX_WITH_PLAYTEST_BOT
#define CODEX_WITH_PLAYTEST_BOT (!(UE_BUILD_SHIPPING || UE_BUILD_TEST))
#endif

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
 * ROUTE mode (Training Ground "Combat lab", game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7; PlaytestBotRouteMode.cpp): on an
 * ENDLESS level (the level JSON has an active "pressure" block and a "bot_route"; -BotRoute=1 / 0 forces it on / off) the bot leads the squad
 * in formation along the level's planned route (clicks through the player controller: moves, a wall click = the formation wall layout, a
 * tower climbed by the leader alone), fights what it meets with the Aggressive posture - on contact it takes COVER AT A WALL facing the
 * threat first (the group cover click on the nearest suitable wall's near face, BotCoverRules; game designer decision 5, 2026-11-01 night),
 * fights from there and walks on when the fight is over or after "fight_cover"."hold_sec" (no wall: it halts for "fight_hold_sec") -, and
 * stops by TIME: -BotDuration=<game s, 600> -> RESULT COMPLETE. The run report (FCombatLabRunCollector + FBotRouteTracker,
 * schema codex.combatlab_run/1) goes to -CombatLabReport=<file.json> (a live snapshot to <file>.live.json every 5 s).
 */
UCLASS()
class CODEXTACTICS_API UPlaytestBotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Never in Test / Shipping builds (CODEX_WITH_PLAYTEST_BOT); otherwise as any world subsystem. */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	/** Game and PIE worlds only: no bot object in editor / preview / inactive worlds. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	/** The bot gate as a pure rule (tests): the world types and builds the bot may exist in. */
	static bool IsBotAllowed(EWorldType::Type WorldType);

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
	enum class EBotStage : uint8 { Explore, Stealth, EnterCombat, Fight, Route, Done };
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

	// --- Route mode (combat lab) ---
	/** The bot walks the level's planned route (an endless pressure level). */
	bool IsRouteMode() const { return bRouteMode; }
	const FBotRouteTracker& GetRouteTracker() const { return RouteTracker; }
	const FCombatLabRunCollector& GetCombatLabCollector() const { return LabCollector; }
	/** The route's drops through roof / floor holes so far (generated maps; the one under way included). */
	TArray<FBotRouteDrop> GetRouteDrops() const;
	/**
	 * Starts the ROUTE mode from the console / a smoke (Codex.Bot.Route; generated maps 2026-11-02): the level's "bot_route" whether the
	 * level has an endless pressure or not, for DurationSeconds of game time (<= 0: the -BotDuration / 600 s default), bQuitAtEnd exits the
	 * game at the end (false: the caller reads GetRouteTracker() and ends the game itself).
	 */
	void StartRouteRun(float DurationSeconds, bool bQuitAtEnd, int32 InSeed = 1);

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

	// Route mode (PlaytestBotRouteMode.cpp)
	/** The level has a route and an endless pressure (or -BotRoute=1): route mode is set up. */
	bool TrySetupRouteMode();
	void TickRoute(float DeltaTime);
	/** A click on the ground at the current waypoint (the player's path, ACodexTacticsPlayerController::HandleWorldHit). */
	void IssueRouteMove(const TCHAR* Why);
	/** Feeds a tracker command back (moves, actions, the end). */
	void HandleRouteCommand(EBotRouteCommand Command);
	void BeginRouteAction();
	void TickRouteAction(float DeltaTime);
	void EndRouteAction(bool bOk, const FString& Why);
	/**
	 * Decision 5 (2026-11-01 night): the squad takes cover at the nearest suitable wall between it and the threat (BotCoverRules on the level's
	 * obstacle boxes) - a click on that wall's near face, the cover ghost confirmed. False when there is no threat / no wall / the click missed.
	 */
	bool TryTakeFightCover();
	/** A click (a Visibility trace) handed to the controller; false when nothing was hit or no controller. */
	bool RouteClick(const FVector& Start, const FVector& End, const TCHAR* What);
	FBotRouteInput RouteInput() const;
	FCombatLabSample SampleCombatLab() const;
	void WriteCombatLabLive() const;
	void WriteCombatLabReport(const TCHAR* Result) const;
	/** The drop bookkeeping (a waypoint with "drop"): opens / closes a record as the route reaches / leaves it, notes the leader falling. */
	void TrackRouteDrop();
	/** A drop leg that got stuck: the bot puts the living squad down at the drop's waypoint itself (marked bScripted in the record). */
	bool ScriptedRouteDrop();

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

	// Route mode
	enum class ERouteActionPhase : uint8 { None, WallWait, WallHold, ClimbUp, ClimbHold, ClimbDown };
	bool bRouteMode = false;
	FBotRouteTracker RouteTracker;
	FCombatLabRunCollector LabCollector;
	float RouteDurationSeconds = 600.f;
	double RouteStartTime = 0.0;
	float LabSampleTimer = 0.f;
	float LabLiveTimer = 0.f;
	float RouteClickCooldown = 0.f;
	bool bRouteReorder = false;
	bool bRouteFightHold = false;
	bool bRouteWasInFight = false;
	double RouteFightHoldStart = 0.0;
	/** The fight hold is a cover at a wall (TryTakeFightCover), not a halt in place. */
	bool bRouteFightCover = false;
	ERouteActionPhase RoutePhase = ERouteActionPhase::None;
	float RoutePhaseTime = 0.f;
	TWeakObjectPtr<class ALadderActor> RouteLadder;
	FString CombatLabReportFile;
	int32 RoutePressureSeed = 0;
	int32 RouteLoggedStucks = 0;
	int32 RouteLoggedIndex = 0;
	/** -1 = the command line decides (-BotRoute=), 0 off, 1 on (StartRouteRun). */
	int32 RouteForce = -1;
	/** StartRouteRun's duration (> 0 overrides -BotDuration). */
	float RouteDurationOverride = 0.f;
	TArray<FBotRouteDrop> RouteDrops;
	/** The drop under way (an index into RouteDrops), INDEX_NONE when the current leg is no drop. */
	int32 RouteDropActive = INDEX_NONE;
	int32 RouteDropReachedBase = 0;
	float RouteDropStart = 0.f;
};
