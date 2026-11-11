// The PMC self-play arena (user request 2026-11-03, docs/systems/pmc-squad-ai.md "Arena"): N rounds of squad vs squad in one process,
// a machine-readable result JSON (schema codex.pmc_arena/1) in Saved/Arena/<timestamp>.json and one summary line. The hook the
// overnight trainer uses. Headless:
//   "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<project>/CodexTactics.uproject" /Game/Maps/L_FormationLabCombat
//     -game -nullrhi -nosound -unattended -LevelJson=L_PmcArena.json -UseFixedTimeStep -FPS=60 -FixedSeed
//     -ExecCmds="CodexTactics.PmcArena rounds=3 seed=7 weightsA=pmc_commander.json weightsB=pmc_commander.json opponent=pmc time=120"
// Screenshots: -RenderOffscreen -ResX=1280 -ResY=720 instead of -nullrhi and shot=1 -> Saved/Screenshots/PmcArena/.
// Per round a track Saved/Arena/<run>/round_<n>.track.json (codex.pmc_arena_track/1: every 0.2 s each fighter's side / team / id / position /
// yaw / stance / HP / state, and the events: shots from -> to hit / miss, kills, the commander's decisions with the chosen order + top score) and a
// highlight_score in the result (the formula: HighlightScoreOf). Replay / capture of a round: the same seed + weights with record=1 on a RENDERED
// run (-RenderOffscreen, -UseFixedTimeStep -FPS=60 -FixedSeed) and the clip recorder in front:
//   -ExecCmds="CodexTactics.ClipRecorder pmcarena nodirector,CodexTactics.PmcArena seed=7 rounds=1 record=1 ..." -> Saved/Screenshots/Clips/pmcarena/f_NNNNN.png
//   (Scripts/Tools/clip_assemble.py turns the frames into an mp4 as Scripts/levelgen_record_run.ps1 does). The arena's overview camera
//   (fix/arena-clip-readability 2026-11-10) frames the box of all LIVE fighters + the objective every rendered frame, eased, 65 deg down,
//   its yaw across the round's spawn axis (CamPitch=<deg> CamMin= / CamMax=<m>, 20 / 110; a lone flanker 30 m from everybody is left out of the box); a rendered run colours the teams with a
//   see-through overlay - A red, B green, dim on the fallen (Teams=off disables it, TeamGlow=<f> its intensity). Determinism: the same weights + seed + fixed step reproduce the spawns, looks, weapons and orders; the crowd avoidance,
//   async navmesh work and physics hits can still drift a long round (known limit: check the track's winner / duration against the original).
// Args (keys case-insensitive, the night trainer's template "Map=.. Rounds=.. Seed=.. WeightsA=.. Opponent=Bot|Pmc WeightsB=..|None TimeLimit=.. Out=.."
// fits, docs/tools/night-trainer.md 6): rounds=N seed=S weightsA=<file> weightsB=<file>|None opponent=pmc|player|bot time|timelimit=<s> out=<project-relative
// result path> map=<informational> squad=<men per side> distance=<cm> shot=0|1 smoke=0|1 record=0|1. Round n's seed = seed + n - 1 (Rounds=1 Seed=<it> replays it).
//   spawns=random|fixed (default: random off L_FormationLabCombat, fixed on it) spawnmin=<m> spawnmax=<m> (35 / 80): random = per round a seeded pair of
//   navmesh spots on any level (ground, roofs, the cut), reachable from each other, spawnmin..spawnmax apart (PmcCommanderRules::PickSpawnPair).
//   opponent=pmc: team 1 (A) vs team 2 (B), the player's squad parked (any number of rounds); player: A vs the player's squad commanded with
//   weights B (one round: the fallen cannot be revived); bot: A vs the player's squad driven by the playtest bot (one round, development builds).
//   objective=0|1 (task #53, default 1): each round has an objective point near the defender's spawn (on the navmesh, reachable by both);
//   the defenders hold it, the attackers win by holding it (an attacker within the radius, no live defender within it) for hold_s seconds
//   or by elimination; the defenders win by elimination or at the time limit with the point not held by the attackers (held by them at
//   the limit = a contested draw). Radius / hold seconds / placement: the level JSON's "pmc_arena" object (PmcArena::LoadConfig).
//   playarea=on|off (fix/arena-no-outer-strip 2026-11-10, default on): the level's arena play area ("pmc_arena" -> "play_area": an
//   inclusion box + exclusion boxes in world cm, AI/PmcArenaPlayArea.h) - no spawn, squad slot or objective outside it (the Depot's
//   outer strip between the long perimeter wall and the map edge: hidden from the camera); off = the whole navmesh as before.
//   scenario=random|skirmish|chokepoint|street|roofs|blizzard (default random = per round from its seed, PmcCommanderRules::PickScenario;
//   needs spawns=random): skirmish = the random spawn pair; chokepoint = the objective + the defender spawn at a narrow passage (1-3 m
//   between walls >= 3 m); street = the objective on open ground; roofs = the objective on a roof level; blizzard = a skirmish in a
//   forced Blizzard (the sight halved). A scenario without a site on the map falls back to skirmish (the result names the real one).
//   Per round per side: role (attacker / defender), damage_dealt, damage_from_concealment (dealt while the shooter was not seen by the
//   target's team), first_strike (1 = this side's hit came first), objective_hold_s (seconds of sole control of the point),
//   median_engagement_m (its shots' median distance).
// CodexTactics.PmcArenaSmoke = one pmc round + the JSON validated (Smoke RESULT: PASS / FAIL).
// CodexTactics.PmcStalemateSmoke (fix/pmc-arena-stalemate 2026-11-10) = one real 3v3 objective round on the Depot: contact happens and resumes
//   within 45 s after every quiet spell, the attackers reach the point area, no running order is replaced inside its minimum hold, the
//   decision churn per fighter is measured (the round's "flow" object).
// CodexTactics.PmcArenaObjectiveSmoke = one round where the defenders are parked out of the fight: the attackers must walk onto the point
//   and capture it (end_reason objective, the attacker wins, his objective_hold_s >= hold_s).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/AmbushWatchRules.h"
#include "AI/PmcArenaPlayArea.h"
#include "AI/PmcArenaRoundRules.h"
#include "AI/PmcArenaViewRules.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "AI/SquadOrderCatalog.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Bot/PlaytestBotRules.h"
#include "Bot/PlaytestBotSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/MeshComponent.h"
#include "Combat/HordeSubsystem.h"
#include "Combat/ProjectileSubsystem.h"
#include "Combat/SightRules.h"
#include "Combat/SuppressionRules.h"
#include "Containers/Ticker.h"
#include "Data/LevelJsonRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace PmcArena
{
	constexpr float StepSeconds = 0.1f;
	constexpr int32 TeamA = 1;
	constexpr int32 TeamB = 2;

	enum class EOpponent : uint8 { Pmc, Player, Bot };

	struct FOptions
	{
		int32 Rounds = 1;
		int32 Seed = 1;
		FString WeightsA;
		FString WeightsB;
		EOpponent Opponent = EOpponent::Pmc;
		float TimeLimit = 120.f;
		int32 Squad = 3;
		float DistanceCm = 3000.f;
		bool bShots = false;
		bool bSmoke = false;
		/** record=1: the replay / capture mode - the overview camera follows the fight every tick (rendered run + CodexTactics.ClipRecorder). */
		bool bRecord = false;
		/** Out=<project-relative path> of the result JSON (the night trainer, docs/tools/night-trainer.md 6); empty = Saved/Arena/<run>.json. */
		FString OutPath;
		/** Map=<path> as the trainer passes it (informational: the launcher picks the map). */
		FString MapArg;
		/** Spawns=Random|Fixed: 0 = not given (random off L_FormationLabCombat), 1 = fixed (start + far spot), 2 = random (seeded pairs). */
		int32 SpawnMode = 0;
		/** The random spawn pair's planar distance range, cm (SpawnMin= / SpawnMax= in metres). */
		float SpawnMinCm = 3500.f;
		float SpawnMaxCm = 8000.f;
		/** Objective=0|1 (task #53, default on): the round's objective point - the attackers take it, the defenders hold it. */
		bool bObjective = true;
		/** Scenario=random|skirmish|chokepoint|street|roofs|blizzard: -1 = random (per round from its seed), else an EPmcScenario. */
		int32 Scenario = -1;
		/** PmcArenaObjectiveSmoke: the defenders are parked out of the fight after the spawn - the attackers must take the point. */
		bool bSmokeCapture = false;
		/** PmcStalemateSmoke: a real objective round, the fight flow checked (quiet spells, the point reached, order commitment). */
		bool bSmokeStalemate = false;
		/**
		 * Ammo=resupply|real (fix/pmc-arena-stalemate, default resupply): a fighter whose reserve runs dry gets his weapon's reserve back - an
		 * arena-only training rule (the Depot tracks: empty rifles stood on the point next to each other for 3 minutes, unable to fight).
		 */
		bool bResupply = true;
		/**
		 * Teams=on|off (fix/arena-clip-readability 2026-11-10, default on): a rendered run colours every live fighter with a see-through team
		 * overlay (A red, B green, dim on the fallen; M_TargetFresnel, no depth test) instead of the operatives' red occlusion silhouette. TeamGlow=<f> = its
		 * Intensity. Rendering only (headless runs never load it).
		 */
		bool bTeams = true;
		float TeamGlow = 0.6f;
		/** The record camera (record=1): CamPitch=<deg below the horizon> CamMin= / CamMax=<m>. */
		float CamPitchDeg = 65.f;
		float CamMinCm = 2000.f;
		float CamMaxCm = 11000.f;
		/** PlayArea=on|off (fix/arena-no-outer-strip, default on): off ignores the level's "play_area" (the before / after measurement). */
		bool bPlayArea = true;
		/**
		 * RocketContact=on|off (fix/arena-rpg-contact-roof 2026-11-10, default on): an RPG-7 rocket's burst within launcher.radius_cm
		 * (suppression.json) of a live enemy of the shooter is a contact moment (the round's contact + flow); off = only the shots (the
		 * before / after measurement). The rocket's launch at an enemy is a shot either way (OnWeaponFiredNative).
		 */
		bool bRocketContact = true;
		/**
		 * ObjectiveReach=on|off (fix/arena-rpg-contact-roof, default on): the objective must be reached by a full navmesh path from BOTH
		 * sides' spawns (PmcArenaRoundRules::IsReachableFromBoth), else another point; off = the old placement (the measurement).
		 */
		bool bObjectiveReach = true;
	};

	/** PmcStalemateSmoke's limits (the designer 2026-11-08): the fight resumes within this after a quiet spell, s. */
	constexpr float MaxQuietSeconds = 45.f;
	/** The same fighter's next order sooner than this counts as a quick re-decision (pmc-v3: a flank re-decided twice a second), s. */
	constexpr float QuickRedecisionSeconds = 2.f;

	/**
	 * The arena's own data (task #53): the level JSON's "pmc_arena" object (L_PmcArenaDepot.json / L_PmcArena.json), the code defaults
	 * below without it: objective_radius_m, objective_hold_s, objective_offset_frac (the point from the defender's spawn towards the
	 * attacker's, x 0.75-1.25 seeded), defender_offset_m (a scenario site -> the defender's spawn), passage_min_m / passage_max_m /
	 * wall_min_height_m (the chokepoint search), objective_level_m (fix/pmc-onpoint-3d-detour: a man is on the point only within this
	 * height of it, feet vs the navmesh point - one floor is 3 m).
	 */
	struct FArenaConfig
	{
		float ObjectiveRadiusCm = 600.f;
		float ObjectiveLevelCm = 150.f;
		float HoldSeconds = 15.f;
		float ObjectiveOffsetFrac = 0.2f;
		float DefenderOffsetCm = 800.f;
		float PassageMinCm = 100.f;
		float PassageMaxCm = 300.f;
		float WallHeightCm = 300.f;
		/**
		 * fix/arena-no-outer-strip 2026-11-10 (the game designer): "play_area" - where spawns / squad slots / objectives may stand
		 * (PmcArenaPlayAreaRules::IsInside); unrestricted without it or with PlayArea=off.
		 */
		FPmcArenaPlayArea PlayArea;
		FString Source = TEXT("defaults");
	};

	struct FSide
	{
		int32 TeamId = 0;
		FString Label;
		FVector Spawn = FVector::ZeroVector;
		FRotator Facing = FRotator::ZeroRotator;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		float StartHp = 0.f;
	};

	struct FRoundResult
	{
		int32 Index = 0;
		/** The round's own seed (base seed + round - 1): Rounds=1 Seed=<this> replays it. */
		int32 Seed = 0;
		FString EndReason;
		int32 RetreatsSurvived[2] = { 0, 0 };
		float HpMax[2] = { 0.f, 0.f };
		FString Attacker;
		FString Winner;
		float Duration = 0.f;
		int32 Survivors[2] = { 0, 0 };
		float HpLost[2] = { 0.f, 0.f };
		int32 Kills[2] = { 0, 0 };
		FPmcTeamStats Stats[2];
		bool bFightStarted = false;
		/** How many times the lead (alive A - alive B) changed sign in the round (the track's samples). */
		int32 LeadSwings = 0;
		float HighlightScore = 0.f;
		FString TrackFile;
		/** Task #53: the scenario played (a site-less one falls back to skirmish) and the one asked for. */
		FString Scenario = TEXT("skirmish");
		FString ScenarioRequested = TEXT("random");
		FString Weather;
		bool bObjective = false;
		FVector ObjectiveLocation = FVector::ZeroVector;
		/** captured / held / contested / eliminated / none (PmcCommanderRules::ResolveRound). */
		FString ObjectiveResult = TEXT("none");
		/** The attackers' capture clock at the end, s. */
		float AttackerHeld = 0.f;
		float DamageDealt[2] = { 0.f, 0.f };
		float DamageFromConcealment[2] = { 0.f, 0.f };
		int32 FirstStrike[2] = { 0, 0 };
		float ObjectiveHoldS[2] = { 0.f, 0.f };
		float MedianEngagementM[2] = { 0.f, 0.f };
		int32 Shots[2] = { 0, 0 };
		/** First hits on a target that was not looking towards the shooter (his facing > 45 deg off; user 2026-11-07). */
		int32 StrikesUnobserved[2] = { 0, 0 };
		/**
		 * The fight's flow (fix/pmc-arena-stalemate 2026-11-10): a shot between the sides at all, shots per minute, the first shot, the longest
		 * quiet spell after it (gaps between shots + the quiet tail to the end), the attackers' mean / closest distance to the point, the
		 * orders per fighter per minute, quick re-decisions (same fighter < 2 s; of the same order), running orders replaced inside their
		 * minimum hold (must be 0), orders failed, detours.
		 */
		bool bContact = false;
		float ShotsPerMin = 0.f;
		float FirstContactS = -1.f;
		float LongestQuietS = 0.f;
		float AttackerMeanDistM = -1.f;
		float AttackerClosestM = -1.f;
		/** fix/pmc-onpoint-3d-detour: man-seconds attackers stood within the radius (planar) but off the point's level (under a roof point). */
		float AttackerUnderPointS = 0.f;
		float OrdersPerFighterMin = 0.f;
		int32 QuickRedecisions = 0;
		int32 QuickSameOrder = 0;
		int32 EarlyPreempts = 0;
		int32 OrdersFailed = 0;
		int32 Detours = 0;
		/** Arena resupplies (Ammo=resupply): reserves refilled after running dry. */
		int32 Resupplies = 0;
		/**
		 * fix/arena-rpg-contact-roof: per side the RPG-7 rockets launched (part of Shots) and their bursts within launcher.radius_cm of a
		 * live enemy; the round's contact moments (shots + those bursts with RocketContact=on).
		 */
		int32 Rockets[2] = { 0, 0 };
		int32 RocketBurstsNear[2] = { 0, 0 };
		int32 ContactEvents = 0;
		/** The objective's reach at placement: a full navmesh path from the attackers' / the defenders' spawn to the point. */
		bool bReachAttacker = true;
		bool bReachDefender = true;
	};

	/** The last hit a fighter took (the HP drops of the next samples are that shooter's damage). */
	struct FHitNote
	{
		int32 Side = -1;
		bool bConcealed = false;
		float Time = 0.f;
	};

	/**
	 * The round's lightweight track (schema codex.pmc_arena_track/1, the trainer's viewer contract): fighters[] {id, side, team}, frames[] = per
	 * TrackInterval one [x, y] (null when down) per fighter, states[] = the same grid of [yaw, stance, hp, state] (the arena's extra), events[].
	 */
	struct FTrack
	{
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TArray<TSharedPtr<FJsonValue>> Frames;
		TArray<TSharedPtr<FJsonValue>> States;
		TArray<TSharedPtr<FJsonValue>> Events;
		/** Fighters that fell back at least once (retreats_survived = those alive at the end). */
		TSet<TWeakObjectPtr<AOperativeCharacter>> Retreated;
		int32 HealsSeen[2] = { 0, 0 };
		float SampleTimer = 0.f;
		int32 LastLeadSign = 0;
		int32 LeadSwings = 0;
		TArray<FDelegateHandle> FireHandles;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Listened;
		FDelegateHandle OrderHandle;
		/** Task #53 ambush stats: each fighter's HP at the last sample and his last hit; per side damage, damage from concealment, shot distances. */
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> LastHp;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FHitNote> LastHit;
		float Damage[2] = { 0.f, 0.f };
		float Concealed[2] = { 0.f, 0.f };
		int32 FirstStrikeSide = -1;
		TArray<float> ShotDistM[2];
		/** Fighters already hit this round, and per side the first hits on a target looking away (strikes_unobserved). */
		TSet<TWeakObjectPtr<AOperativeCharacter>> StruckOnce;
		int32 StrikesUnobserved[2] = { 0, 0 };
		/** The fight flow (fix/pmc-arena-stalemate): shot times between the sides, each fighter's last order (time, order), the counters. */
		TArray<float> ShotTimes;
		TMap<TWeakObjectPtr<AOperativeCharacter>, TPair<float, EPmcOrder>> LastOrder;
		int32 Orders = 0;
		int32 QuickRedecisions = 0;
		int32 QuickSameOrder = 0;
		int32 EarlyPreempts = 0;
		double AttackerDistSum = 0.0;
		int32 AttackerDistSamples = 0;
		float AttackerClosestCm = -1.f;
		float AttackerUnderPointS = 0.f;
		int32 Resupplies = 0;
		/**
		 * fix/arena-rpg-contact-roof: per side the rockets launched and their bursts near a live enemy (PmcArenaRoundRules::IsLauncherContact),
		 * the times of those bursts (contact moments next to ShotTimes), the projectile layer's listener (bound once).
		 */
		int32 Rockets[2] = { 0, 0 };
		int32 RocketBurstsNear[2] = { 0, 0 };
		TArray<float> RocketBurstTimes;
		FDelegateHandle ProjectileHandle;
	};
	constexpr float TrackInterval = 0.2f;

	struct FState
	{
		FOptions Options;
		/** Spawns=Random resolved (seeded spawn pairs per round) and the ground Z under the start (the height levels' reference). */
		bool bRandomSpawns = false;
		float GroundZ = 0.f;
		FPmcWeights WeightsA;
		FPmcWeights WeightsB;
		bool bWeightsALoaded = false;
		bool bWeightsBLoaded = false;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Round = 0;
		int32 Failures = 0;
		FSide Sides[2];
		TArray<FRoundResult> Results;
		TArray<TWeakObjectPtr<AOperativeCharacter>> ParkedSquad;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 ShotsTaken = 0;
		FString OutputPath;
		FDateTime Started;
		FDateTime StartedUtc;
		FString RunName;
		bool bRoundsLimited = false;
		FTrack Track;
		/** Task #53: the arena data, this round's objective / capture clock / attacker, the weather before a blizzard round. */
		FArenaConfig Config;
		FPmcObjectiveClock Clock;
		bool bRoundObjective = false;
		FVector Objective = FVector::ZeroVector;
		/** fix/arena-rpg-contact-roof: this round's objective reached by a full path from the attackers' / the defenders' spawn. */
		bool bReachAttacker = true;
		bool bReachDefender = true;
		bool bAAttacks = false;
		bool bWeatherForced = false;
		bool bInitialWeatherKnown = false;
		EWeatherState InitialWeather = EWeatherState::Clear;
		/** The record camera (fix/arena-clip-readability): the framing (yaw per round across the spawns) and the smoothed view. */
		FPmcArenaFraming Framing;
		FPmcArenaView View;
		bool bHasView = false;
		/** The team overlays (one MID per side; the fighters' meshes hold them) and the fighters already switched off their silhouette. */
		TWeakObjectPtr<UMaterialInstanceDynamic> TeamOverlay[2];
		TWeakObjectPtr<UMaterialInstanceDynamic> TeamOverlayDim[2];
		TSet<TWeakObjectPtr<AOperativeCharacter>> TeamColoured;
		TSet<TWeakObjectPtr<AOperativeCharacter>> TeamDimmed;
		/** Points the play area turned down this round (samples, offsets, squad slots; logged at the round start). */
		mutable int32 AreaRejects = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FOptions Parse(const TArray<FString>& Args)
	{
		FOptions Options;
		for (const FString& Arg : Args)
		{
			FString Key;
			FString Value;
			if (!Arg.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}
			Key = Key.ToLower();
			const bool bNone = Value.Equals(TEXT("none"), ESearchCase::IgnoreCase);
			if (Key == TEXT("rounds")) { Options.Rounds = FMath::Max(1, FCString::Atoi(*Value)); }
			else if (Key == TEXT("seed")) { Options.Seed = FCString::Atoi(*Value); }
			else if (Key == TEXT("weightsa")) { Options.WeightsA = bNone ? FString() : Value; }
			else if (Key == TEXT("weightsb")) { Options.WeightsB = bNone ? FString() : Value; }
			else if (Key == TEXT("time") || Key == TEXT("timelimit")) { Options.TimeLimit = FMath::Max(10.f, FCString::Atof(*Value)); }
			else if (Key == TEXT("out")) { Options.OutPath = bNone ? FString() : Value; }
			else if (Key == TEXT("map")) { Options.MapArg = Value; }
			else if (Key == TEXT("squad")) { Options.Squad = FMath::Clamp(FCString::Atoi(*Value), 1, 6); }
			else if (Key == TEXT("distance")) { Options.DistanceCm = FMath::Clamp(FCString::Atof(*Value), 1000.f, 8000.f); }
			else if (Key == TEXT("shot")) { Options.bShots = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("smoke")) { Options.bSmoke = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("record")) { Options.bRecord = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("spawns")) { Options.SpawnMode = Value.Equals(TEXT("random"), ESearchCase::IgnoreCase) ? 2 : 1; }
			else if (Key == TEXT("spawnmin")) { Options.SpawnMinCm = FMath::Clamp(FCString::Atof(*Value), 10.f, 200.f) * 100.f; }
			else if (Key == TEXT("spawnmax")) { Options.SpawnMaxCm = FMath::Clamp(FCString::Atof(*Value), 10.f, 300.f) * 100.f; }
			else if (Key == TEXT("objective")) { Options.bObjective = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("smokecapture")) { Options.bSmokeCapture = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("ammo")) { Options.bResupply = !Value.Equals(TEXT("real"), ESearchCase::IgnoreCase); }
			else if (Key == TEXT("teams")) { Options.bTeams = !(Value.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Value == TEXT("0")); }
			else if (Key == TEXT("teamglow")) { Options.TeamGlow = FMath::Clamp(FCString::Atof(*Value), 0.05f, 5.f); }
			else if (Key == TEXT("campitch")) { Options.CamPitchDeg = FMath::Clamp(FCString::Atof(*Value), 30.f, 89.f); }
			else if (Key == TEXT("cammin")) { Options.CamMinCm = FMath::Clamp(FCString::Atof(*Value), 5.f, 200.f) * 100.f; }
			else if (Key == TEXT("cammax")) { Options.CamMaxCm = FMath::Clamp(FCString::Atof(*Value), 10.f, 300.f) * 100.f; }
			else if (Key == TEXT("playarea")) { Options.bPlayArea = !(Value.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Value == TEXT("0")); }
			else if (Key == TEXT("rocketcontact")) { Options.bRocketContact = !(Value.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Value == TEXT("0")); }
			else if (Key == TEXT("objectivereach")) { Options.bObjectiveReach = !(Value.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Value == TEXT("0")); }
			else if (Key == TEXT("scenario"))
			{
				EPmcScenario Scenario = EPmcScenario::Skirmish;
				Options.Scenario = PmcCommanderRules::ParseScenario(Value, Scenario) ? static_cast<int32>(Scenario) : -1;
			}
			else if (Key == TEXT("opponent"))
			{
				const FString Lower = Value.ToLower();
				Options.Opponent = Lower == TEXT("player") ? EOpponent::Player : (Lower == TEXT("bot") ? EOpponent::Bot : EOpponent::Pmc);
			}
		}
		return Options;
	}

	const TCHAR* OpponentName(EOpponent Opponent)
	{
		switch (Opponent)
		{
		case EOpponent::Player: return TEXT("Player");
		case EOpponent::Bot: return TEXT("Bot");
		default: return TEXT("Pmc");
		}
	}

	/** A project-relative path (forward slashes, the trainer's convention) made absolute; an absolute one kept. */
	FString ResolveProjectPath(const FString& Path)
	{
		if (Path.IsEmpty() || FPaths::IsRelative(Path) == false)
		{
			return Path;
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Path);
	}

	/** Where the result JSON goes (Out=, else Saved/Arena/<run>.json) and the folder of the round tracks next to it. */
	FString ResultPath(const FState& State)
	{
		if (!State.Options.OutPath.IsEmpty())
		{
			return ResolveProjectPath(State.Options.OutPath);
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Arena") / FString::Printf(TEXT("%s.json"), *State.RunName));
	}

	FString TrackDir(const FState& State)
	{
		const FString Result = ResultPath(State);
		return FPaths::GetPath(Result) / FPaths::GetBaseFilename(Result);
	}

	float SumHp(const TArray<TWeakObjectPtr<AOperativeCharacter>>& Members)
	{
		float Total = 0.f;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			Total += Operative && Operative->HealthComponent ? FMath::Max(0.f, Operative->HealthComponent->GetCurrentHealth()) : 0.f;
		}
		return Total;
	}

	int32 CountAlive(const TArray<TWeakObjectPtr<AOperativeCharacter>>& Members)
	{
		int32 Alive = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			Alive += Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive() ? 1 : 0;
		}
		return Alive;
	}

	/** The player's start spot and facing (the squad spawned there). */
	bool FindStart(UWorld* World, FVector& OutLocation, FRotator& OutFacing)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (Squad && Squad->GetLeader())
		{
			OutLocation = Squad->GetLeader()->GetActorLocation();
			OutFacing = FRotator(0.f, Squad->GetLeader()->GetActorRotation().Yaw, 0.f);
			return true;
		}
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			OutFacing = FRotator(0.f, It->GetActorRotation().Yaw, 0.f);
			return true;
		}
		return false;
	}

	/**
	 * A spot DistanceCm from Origin with a navmesh path from it: the start facing first, then the bearings round it. Area = the play area
	 * (nullptr = anywhere): a spot outside it is skipped.
	 */
	bool FindFarSpot(UWorld* World, const FVector& Origin, float Yaw, float DistanceCm, FVector& OutSpot, const FPmcArenaPlayArea* Area = nullptr)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		for (const float Bearing : { 0.f, 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f })
		{
			for (const float Scale : { 1.f, 0.8f, 0.6f })
			{
				const FVector Candidate = Origin + FRotator(0.f, Yaw + Bearing, 0.f).Vector() * DistanceCm * Scale;
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(600.f, 600.f, 400.f)) || (Area && !PmcArenaPlayAreaRules::IsInside(*Area, OnNav.Location)))
				{
					continue;
				}
				const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Origin, OnNav.Location);
				if (Path && Path->IsValid() && !Path->IsPartial() && FVector::Dist2D(Origin, OnNav.Location) >= DistanceCm * 0.5f)
				{
					OutSpot = OnNav.Location;
					return true;
				}
			}
		}
		return false;
	}

	/** The level JSON's "pmc_arena" object (the -LevelJson= file, else the map's own); the defaults without one. */
	FArenaConfig LoadConfig(UWorld* World)
	{
		FArenaConfig Config;
		FString CommandLineJson;
		FParse::Value(FCommandLine::Get(), TEXT("LevelJson="), CommandLineJson);
		const FString File = LevelJsonRules::ResolveLevelJsonFile(World->GetMapName(), FString(), CommandLineJson);
		if (File.IsEmpty())
		{
			return Config;
		}
		const FString Path = FPaths::IsRelative(File) ? LevelJsonRules::GetLevelsDirectory() / File : File;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		const TSharedPtr<FJsonObject>* Arena = nullptr;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
			|| !Json->TryGetObjectField(TEXT("pmc_arena"), Arena) || !Arena || !Arena->IsValid())
		{
			return Config;
		}
		auto Read = [Arena](const TCHAR* Key, float& Out, float Scale, float Min, float Max)
		{
			double Value = 0.0;
			if ((*Arena)->TryGetNumberField(Key, Value))
			{
				Out = FMath::Clamp(static_cast<float>(Value) * Scale, Min, Max);
			}
		};
		Read(TEXT("objective_radius_m"), Config.ObjectiveRadiusCm, 100.f, 100.f, 3000.f);
		Read(TEXT("objective_level_m"), Config.ObjectiveLevelCm, 100.f, 0.f, 1000.f);
		Read(TEXT("objective_hold_s"), Config.HoldSeconds, 1.f, 1.f, 600.f);
		Read(TEXT("objective_offset_frac"), Config.ObjectiveOffsetFrac, 1.f, 0.f, 0.5f);
		Read(TEXT("defender_offset_m"), Config.DefenderOffsetCm, 100.f, 0.f, 3000.f);
		Read(TEXT("passage_min_m"), Config.PassageMinCm, 100.f, 50.f, 1000.f);
		Read(TEXT("passage_max_m"), Config.PassageMaxCm, 100.f, 50.f, 1000.f);
		Read(TEXT("wall_min_height_m"), Config.WallHeightCm, 100.f, 100.f, 2000.f);
		Config.PassageMaxCm = FMath::Max(Config.PassageMaxCm, Config.PassageMinCm);
		Config.Source = FPaths::GetCleanFilename(Path);
		TArray<FString> AreaErrors;
		PmcArenaPlayAreaRules::ReadJson(**Arena, Config.PlayArea, AreaErrors);
		for (const FString& Error : AreaErrors)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] %s: %s"), *Config.Source, *Error);
		}
		return Config;
	}

	/** A fighter's feet (the capsule centre minus its half height): the level tests compare them with navmesh points. */
	FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	bool HasNavPath(UWorld* World, UNavigationSystemV1* Nav, const FVector& From, const FVector& To)
	{
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
		return Path && Path->IsValid() && !Path->IsPartial();
	}

	/** Point may hold a spawn / squad slot / objective (the level's play area, PmcArenaPlayAreaRules::IsInside); a rejection is counted. */
	bool InArea(const FState& State, const FVector& Point)
	{
		if (PmcArenaPlayAreaRules::IsInside(State.Config.PlayArea, Point))
		{
			return true;
		}
		++State.AreaRejects;
		return false;
	}

	/** The box the random samplers draw from: the navmesh bounds clipped to the play area's inclusion box (invalid without a navmesh). */
	FBox SampleBox(const FState& State, UNavigationSystemV1* Nav)
	{
		const ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		return NavData ? PmcArenaPlayAreaRules::SampleBounds(State.Config.PlayArea, NavData->GetBounds()) : FBox(ForceInit);
	}

	/**
	 * A seeded navmesh point anywhere in Bounds at about Level x 3 m over the ground (the random spawns' sampling); false off the navmesh
	 * or outside the play area (fix/arena-no-outer-strip).
	 */
	bool SampleNav(const FState& State, UNavigationSystemV1* Nav, const FBox& Bounds, FRandomStream& Stream, int32 Level, FVector& Out)
	{
		const float X = Stream.FRandRange(static_cast<float>(Bounds.Min.X), static_cast<float>(Bounds.Max.X));
		const float Y = Stream.FRandRange(static_cast<float>(Bounds.Min.Y), static_cast<float>(Bounds.Max.Y));
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(FVector(X, Y, State.GroundZ + Level * 300.f + 100.f), OnNav, FVector(300.f, 300.f, 200.f))
			|| !InArea(State, OnNav.Location))
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	}

	/** The distance to the first non-pawn blocker along Direction within Reach (0 = free). */
	float RayHit(UWorld* World, const FVector& From, const FVector& Direction, float Reach)
	{
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(PmcArenaSite), false);
		if (!World->LineTraceSingleByChannel(Hit, From, From + Direction * Reach, ECC_Visibility, Params) || Cast<APawn>(Hit.GetActor()))
		{
			return 0.f;
		}
		return FMath::Max(1.f, static_cast<float>(Hit.Distance));
	}

	int32 LevelOf(const FState& State, const FVector& NavPoint)
	{
		return SightRules::HeightLevel(static_cast<float>(NavPoint.Z) + SightRules::EyeHeight(EOperativeStance::Standing) - State.GroundZ);
	}

	/**
	 * A seeded scenario site (task #53): chokepoint = a ground spot between two tall walls 1-3 m apart with the passage free along its
	 * axis (PmcCommanderRules::IsPassage; OutAxis = along it); street = a ground spot free 10 m round in at least 7 of 8 directions;
	 * roofs = a navmesh spot one height level or more up. Up to 12 sites from 400 samples, one picked by the seed (Skip = how many
	 * of the found ones to pass over: the next site when the first has no attacker spawn).
	 */
	bool FindSite(const FState& State, UWorld* World, EPmcScenario Scenario, int32 Seed, int32 Skip, FVector& OutSite, FVector& OutAxis)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FBox Bounds = SampleBox(State, Nav);
		if (!Bounds.IsValid)
		{
			return false;
		}
		FRandomStream Stream(Seed * 4099 + static_cast<int32>(Scenario) * 31 + 7);
		TArray<TPair<FVector, FVector>> Sites;
		const FArenaConfig& Config = State.Config;
		for (int32 Try = 0; Try < 400 && Sites.Num() < 12; ++Try)
		{
			const int32 Level = Scenario == EPmcScenario::Roofs ? Stream.RandRange(1, 3) : 0;
			FVector Point;
			if (!SampleNav(State, Nav, Bounds, Stream, Level, Point))
			{
				continue;
			}
			const int32 Height = LevelOf(State, Point);
			if (Scenario == EPmcScenario::Roofs)
			{
				if (Height >= 1)
				{
					Sites.Add({ Point, FVector::ForwardVector });
				}
				continue;
			}
			if (Height != 0)
			{
				continue;
			}
			const FVector Chest = Point + FVector(0.f, 0.f, 100.f);
			if (Scenario == EPmcScenario::Street)
			{
				int32 Free = 0;
				for (int32 Index = 0; Index < 8; ++Index)
				{
					Free += RayHit(World, Chest, FRotator(0.f, Index * 45.f, 0.f).Vector(), 1000.f) <= 0.f ? 1 : 0;
				}
				if (Free >= 7)
				{
					Sites.Add({ Point, FVector::ForwardVector });
				}
				continue;
			}
			// Chokepoint: chest rays for the walls, rays just under the wall height for "tall".
			float Hits[8];
			bool Tall[8];
			const FVector High = Point + FVector(0.f, 0.f, Config.WallHeightCm - 10.f);
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const FVector Direction = FRotator(0.f, Index * 45.f, 0.f).Vector();
				Hits[Index] = RayHit(World, Chest, Direction, Config.PassageMaxCm);
				Tall[Index] = Hits[Index] > 0.f && RayHit(World, High, Direction, Config.PassageMaxCm + 50.f) > 0.f;
			}
			int32 Axis = 0;
			float Width = 0.f;
			if (PmcCommanderRules::IsPassage(Hits, Tall, Config.PassageMinCm, Config.PassageMaxCm, Axis, Width))
			{
				Sites.Add({ Point, FRotator(0.f, Axis * 45.f, 0.f).Vector() });
			}
		}
		if (Sites.IsEmpty())
		{
			return false;
		}
		const int32 Pick = (Stream.RandRange(0, Sites.Num() - 1) + Skip) % Sites.Num();
		if (Skip >= Sites.Num())
		{
			return false;
		}
		OutSite = Sites[Pick].Key;
		OutAxis = Sites[Pick].Value;
		return true;
	}

	/**
	 * The attackers' spawn for a scenario site: a seeded ground navmesh spot SpawnMin..SpawnMax from it with a path both ways (16 path
	 * checks at most); a chokepoint wants it along the passage (|cos| >= 0.5 to the axis) so they come through it.
	 */
	bool PickFarSpawn(const FState& State, UWorld* World, const FVector& Site, const FVector* Axis, int32 Seed, FVector& Out)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FBox Bounds = SampleBox(State, Nav);
		if (!Bounds.IsValid)
		{
			return false;
		}
		FRandomStream Stream(Seed * 6151 + 3);
		int32 PathChecks = 0;
		for (int32 Try = 0; Try < 300 && PathChecks < 16; ++Try)
		{
			FVector Point;
			if (!SampleNav(State, Nav, Bounds, Stream, 0, Point) || LevelOf(State, Point) != 0)
			{
				continue;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(Point, Site));
			if (Distance < State.Options.SpawnMinCm || Distance > FMath::Max(State.Options.SpawnMinCm, State.Options.SpawnMaxCm))
			{
				continue;
			}
			if (Axis && FMath::Abs(FVector::DotProduct((Point - Site).GetSafeNormal2D(), *Axis)) < 0.5f)
			{
				continue;
			}
			++PathChecks;
			if (HasNavPath(World, Nav, Point, Site) && HasNavPath(World, Nav, Site, Point))
			{
				Out = Point;
				return true;
			}
		}
		return false;
	}

	/**
	 * The defenders' spawn DefenderOffsetCm behind Site (away from the attackers), on its level with a path from it, inside the play area;
	 * Site itself without one (a Warning when the play area turned an offset down).
	 */
	FVector PickNearSpawn(const FState& State, UWorld* World, const FVector& Site, const FVector& Attacker)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FVector Away = (Site - Attacker).GetSafeNormal2D();
		if (!Nav || Away.IsNearlyZero() || State.Config.DefenderOffsetCm <= 0.f)
		{
			return Site;
		}
		const int32 RejectsBefore = State.AreaRejects;
		for (const float Bearing : { 0.f, 45.f, -45.f, 90.f, -90.f })
		{
			FNavLocation OnNav;
			const FVector Candidate = Site + Away.RotateAngleAxis(Bearing, FVector::UpVector) * State.Config.DefenderOffsetCm;
			if (Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(300.f, 300.f, 150.f)) && FMath::Abs(OnNav.Location.Z - Site.Z) < 250.f
				&& InArea(State, OnNav.Location) && HasNavPath(World, Nav, Site, OnNav.Location)
				&& (!State.Options.bObjectiveReach || HasNavPath(World, Nav, OnNav.Location, Site)))
			{
				// fix/arena-rpg-contact-roof: the defenders walk FROM their spawn TO the site (a one-way drop link off a roof is no way up).
				return OnNav.Location;
			}
		}
		if (State.AreaRejects > RejectsBefore)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] no defender spawn behind the site %s inside the play area - the site itself"), *Site.ToCompactString());
		}
		return Site;
	}

	/** A full navmesh path From -> To (PmcArenaRoundRules::IsFullPath); a point is always reachable from itself. */
	bool HasFullNavPath(UWorld* World, UNavigationSystemV1* Nav, const FVector& From, const FVector& To)
	{
		return FVector::DistSquared(From, To) < 50.f * 50.f || HasNavPath(World, Nav, From, To);
	}

	/**
	 * What a pawn would stand on at a navmesh point: a Pawn-channel trace from 1 m above to 3 m below (pawns ignored). OutFloorZ = the hit's
	 * Z (the trace's end without a hit), OutWhat = the actor / component hit for the log.
	 */
	bool TracePawnFloor(UWorld* World, const FVector& NavPoint, float& OutFloorZ, FString& OutWhat)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PmcArenaFloor), false);
		const FVector From = NavPoint + FVector(0.f, 0.f, 100.f);
		const FVector To = NavPoint - FVector(0.f, 0.f, 300.f);
		bool bHit = false;
		for (int32 Try = 0; Try < 4; ++Try)
		{
			bHit = World->LineTraceSingleByChannel(Hit, From, To, ECC_Pawn, Params);
			if (!bHit || !Cast<APawn>(Hit.GetActor()))
			{
				break;
			}
			Params.AddIgnoredActor(Hit.GetActor());
		}
		OutFloorZ = bHit ? static_cast<float>(Hit.ImpactPoint.Z) : static_cast<float>(To.Z);
		OutWhat = bHit ? FString::Printf(TEXT("%s/%s"), Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("?"))
			: FString(TEXT("nothing"));
		return bHit;
	}

	/** The path From -> To for the log: points, length, partial, where it ends (3D) and its highest / lowest point. */
	FString DescribeNavPath(UWorld* World, UNavigationSystemV1* Nav, const FVector& From, const FVector& To)
	{
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
		if (!Path || !Path->IsValid() || Path->PathPoints.IsEmpty())
		{
			return TEXT("no path");
		}
		float MinZ = TNumericLimits<float>::Max();
		float MaxZ = TNumericLimits<float>::Lowest();
		for (const FVector& Point : Path->PathPoints)
		{
			MinZ = FMath::Min(MinZ, static_cast<float>(Point.Z));
			MaxZ = FMath::Max(MaxZ, static_cast<float>(Point.Z));
		}
		const FVector End = Path->PathPoints.Last();
		return FString::Printf(TEXT("%d points, %.0f cm, partial %d, ends at %s (%.0f cm from the goal, dZ %.0f), z %.0f..%.0f"), Path->PathPoints.Num(),
			Path->GetPathLength(), Path->IsPartial() ? 1 : 0, *End.ToCompactString(), FVector::Dist(End, To), End.Z - To.Z, MinZ, MaxZ);
	}

	/**
	 * The objective of a skirmish / fixed-spawn round: from the defender's spawn towards the attacker's, ObjectiveOffsetFrac x (0.75-1.25,
	 * seeded) of the way, on the navmesh at the defender's level with a path from both spawns, inside the play area; half that, then the
	 * defender's spawn itself (a Warning when the play area turned a candidate down, or the defender's spawn itself lies outside it).
	 * fix/arena-rpg-contact-roof (ObjectiveReach=on): when neither of the two is reached by a FULL path from both spawns, more points -
	 * 1 / 0.75 / 0.5 / 0.25 of the way at 25 / 50 deg off the spawn line - then the defender's spawn only when the attackers reach it
	 * (PmcArenaRoundRules::PickReachable); a Warning when no point is reachable from both.
	 */
	FVector PlaceObjectiveNear(const FState& State, UWorld* World, const FVector& Defender, const FVector& Attacker, int32 Seed)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return Defender;
		}
		FRandomStream Stream(Seed * 1543 + 11);
		const float Frac = State.Config.ObjectiveOffsetFrac * Stream.FRandRange(0.75f, 1.25f);
		const int32 RejectsBefore = State.AreaRejects;
		for (const float Scale : { 1.f, 0.5f })
		{
			FVector Candidate = FMath::Lerp(Defender, Attacker, Frac * Scale);
			Candidate.Z = Defender.Z;
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(300.f, 300.f, 150.f)) && FMath::Abs(OnNav.Location.Z - Defender.Z) < 250.f
				&& InArea(State, OnNav.Location) && HasNavPath(World, Nav, Defender, OnNav.Location) && HasNavPath(World, Nav, Attacker, OnNav.Location))
			{
				return OnNav.Location;
			}
		}
		if (State.Options.bObjectiveReach)
		{
			// More points off the spawn line, nearer the defender; the defender's spawn last (the attackers must reach it too).
			TArray<FVector> Candidates;
			const FVector Offset = (Attacker - Defender) * Frac;
			for (const float Scale : { 1.f, 0.75f, 0.5f, 0.25f })
			{
				for (const float Bearing : { 25.f, -25.f, 50.f, -50.f })
				{
					FVector Candidate = Defender + Offset.RotateAngleAxis(Bearing, FVector::UpVector) * Scale;
					Candidate.Z = Defender.Z;
					FNavLocation OnNav;
					if (Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(300.f, 300.f, 150.f)) && FMath::Abs(OnNav.Location.Z - Defender.Z) < 250.f
						&& InArea(State, OnNav.Location))
					{
						Candidates.Add(OnNav.Location);
					}
				}
			}
			Candidates.Add(Defender);
			const int32 Pick = PmcArenaRoundRules::PickReachable(Candidates, Attacker, Defender,
				[World, Nav](const FVector& From, const FVector& To) { return HasFullNavPath(World, Nav, From, To); });
			if (Pick != INDEX_NONE)
			{
				return Candidates[Pick];
			}
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] seed %d: no objective point reachable by a full path from both spawns (%d candidates) - on the defender's spawn %s, the attackers at %s cannot reach it"),
				Seed, Candidates.Num(), *Defender.ToCompactString(), *Attacker.ToCompactString());
		}
		if (State.AreaRejects > RejectsBefore || !PmcArenaPlayAreaRules::IsInside(State.Config.PlayArea, Defender))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] seed %d: no objective inside the play area between the spawns - on the defender's spawn %s%s"), Seed,
				*Defender.ToCompactString(), PmcArenaPlayAreaRules::IsInside(State.Config.PlayArea, Defender) ? TEXT("") : TEXT(" (itself OUTSIDE the play area)"));
		}
		return Defender;
	}

	/** A chokepoint / street / roofs round: the site = the objective, the attackers far off, the defenders behind it. False = no site / spawn. */
	bool PlaceScenarioSite(FState& State, UWorld* World, EPmcScenario Scenario, int32 Seed, int32 AttackerIndex)
	{
		for (int32 Skip = 0; Skip < 4; ++Skip)
		{
			FVector Site;
			FVector Axis;
			if (!FindSite(State, World, Scenario, Seed, Skip, Site, Axis))
			{
				return false;
			}
			FVector Attacker;
			if (!PickFarSpawn(State, World, Site, Scenario == EPmcScenario::Chokepoint ? &Axis : nullptr, Seed + Skip * 101, Attacker))
			{
				continue;
			}
			const FVector Defender = PickNearSpawn(State, World, Site, Attacker);
			// fix/arena-rpg-contact-roof: the site must be reached by a full path from both spawns, else the next site.
			if (State.Options.bObjectiveReach)
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (!PmcArenaRoundRules::IsReachableFromBoth(Site, Attacker, Defender,
					[World, Nav](const FVector& From, const FVector& To) { return HasFullNavPath(World, Nav, From, To); }))
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] scenario %s seed %d: site %s not reached by a full path from both spawns - the next site"),
						PmcCommanderRules::ScenarioName(Scenario), Seed, *Site.ToCompactString());
					continue;
				}
			}
			FSide& Atk = State.Sides[AttackerIndex];
			FSide& Def = State.Sides[1 - AttackerIndex];
			Atk.Spawn = Attacker;
			Def.Spawn = Defender;
			Atk.Facing = (Defender - Attacker).GetSafeNormal2D().Rotation();
			Def.Facing = (Attacker - Defender).GetSafeNormal2D().Rotation();
			State.Objective = Site;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] scenario %s seed %d: objective %s (level %d), attackers %s, defenders %s, %.0f m apart"),
				PmcCommanderRules::ScenarioName(Scenario), Seed, *Site.ToCompactString(), LevelOf(State, Site), *Attacker.ToCompactString(),
				*Defender.ToCompactString(), FVector::Dist2D(Attacker, Defender) / 100.f);
			return true;
		}
		return false;
	}

	const TCHAR* WeatherName(EWeatherState Weather)
	{
		switch (Weather)
		{
		case EWeatherState::Overcast: return TEXT("overcast");
		case EWeatherState::Snowfall: return TEXT("snowfall");
		case EWeatherState::Blizzard: return TEXT("blizzard");
		default: return TEXT("clear");
		}
	}

	/** The blizzard scenario forces a Blizzard (the sight x 0.5, SightRules weather multipliers); the next round gets the level's weather back. */
	FString ApplyWeather(FState& State, UWorld* World, bool bBlizzard)
	{
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		if (!Env || !Env->IsInitialised())
		{
			return TEXT("none");
		}
		if (!State.bInitialWeatherKnown)
		{
			State.bInitialWeatherKnown = true;
			State.InitialWeather = Env->GetWeather();
		}
		if (bBlizzard)
		{
			Env->ForceWeather(EWeatherState::Blizzard, true);
			State.bWeatherForced = true;
		}
		else if (State.bWeatherForced)
		{
			Env->ForceWeather(State.InitialWeather, true);
			State.bWeatherForced = false;
		}
		return WeatherName(Env->GetWeather());
	}

	/**
	 * Spawns=Random: from the round seed, up to 48 navmesh spots anywhere in the navmesh bounds on a random level (ground, +3 / +6 /
	 * +9 m: roofs, walkways, the cut), then PmcCommanderRules::PickSpawnPair - SpawnMin..SpawnMax apart, a path both ways (ladders /
	 * drop links included), variety favoured. Same seed + map -> the same pair. Sets both sides' spawn and facing (towards each other).
	 */
	bool PickRandomSpawns(FState& State, UWorld* World, int32 Seed)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FBox Bounds = SampleBox(State, Nav);
		if (!Bounds.IsValid)
		{
			return false;
		}
		const float EyeCm = SightRules::EyeHeight(EOperativeStance::Standing);
		FRandomStream Stream(Seed * 7919 + 101);
		TArray<FPmcSpawnCandidate> Candidates;
		for (int32 Try = 0; Try < 200 && Candidates.Num() < 48; ++Try)
		{
			const float X = Stream.FRandRange(static_cast<float>(Bounds.Min.X), static_cast<float>(Bounds.Max.X));
			const float Y = Stream.FRandRange(static_cast<float>(Bounds.Min.Y), static_cast<float>(Bounds.Max.Y));
			const int32 Level = Stream.RandRange(0, 3);
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(FVector(X, Y, State.GroundZ + Level * 300.f + 100.f), OnNav, FVector(300.f, 300.f, 200.f))
				|| !InArea(State, OnNav.Location))
			{
				continue;
			}
			Candidates.Add({ OnNav.Location, SightRules::HeightLevel(static_cast<float>(OnNav.Location.Z) + EyeCm - State.GroundZ) });
		}
		auto HasPath = [World, Nav](const FVector& From, const FVector& To)
		{
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, From, To);
			return Path && Path->IsValid() && !Path->IsPartial();
		};
		int32 IndexA = INDEX_NONE;
		int32 IndexB = INDEX_NONE;
		const bool bPicked = PmcCommanderRules::PickSpawnPair(Candidates, State.Options.SpawnMinCm, FMath::Max(State.Options.SpawnMinCm, State.Options.SpawnMaxCm), Seed,
			[&Candidates, &HasPath](int32 I, int32 J) { return HasPath(Candidates[I].Location, Candidates[J].Location) && HasPath(Candidates[J].Location, Candidates[I].Location); },
			IndexA, IndexB, 24);
		if (!bPicked)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] seed %d: no random spawn pair (%d candidates) - the previous spawns stay"), Seed, Candidates.Num());
			return false;
		}
		const FPmcSpawnCandidate& SpotA = Candidates[IndexA];
		const FPmcSpawnCandidate& SpotB = Candidates[IndexB];
		State.Sides[0].Spawn = SpotA.Location;
		State.Sides[1].Spawn = SpotB.Location;
		State.Sides[0].Facing = (SpotB.Location - SpotA.Location).GetSafeNormal2D().Rotation();
		State.Sides[1].Facing = (SpotA.Location - SpotB.Location).GetSafeNormal2D().Rotation();
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] spawns seed %d: A %s level %d, B %s level %d, %.0f m apart (%d candidates)"), Seed,
			*SpotA.Location.ToCompactString(), SpotA.Level, *SpotB.Location.ToCompactString(), SpotB.Level, FVector::Dist2D(SpotA.Location, SpotB.Location) / 100.f,
			Candidates.Num());
		return true;
	}

	/** The navmesh path length From -> To, cm; -1 without a full path. */
	float NavPathLength(UWorld* World, UNavigationSystemV1* Nav, const FVector& From, const FVector& To)
	{
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
		return Path && Path->IsValid() && !Path->IsPartial() ? static_cast<float>(Path->GetPathLength()) : -1.f;
	}

	/**
	 * Spawns=Random: the squad round its spawn spot on free navmesh (SmokeUtils::FreeSpot keeps a roof squad on its roof), facing the other
	 * side. fix/arena-clip-readability 2026-11-10 (the designer's clips: part of a squad stood behind the Depot street's fence): FreeSpot
	 * searches up to 6 m round a slot, through fences and walls - every spot must now reach Goal (the objective, else the other side's
	 * spawn) by a full path and lie close to the spawn by the navmesh too (PmcArenaViewRules::IsSpawnSpotUsable); the next slot (the
	 * line's other places, then rows behind it) otherwise, the spawn spot itself as the last resort.
	 */
	void PlaceAround(const FState& State, UWorld* World, const FSide& Side, const FVector& Goal)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FVector Right = Side.Facing.RotateVector(FVector::RightVector);
		const FVector Back = -Side.Facing.Vector();
		const int32 Count = Side.Members.Num();
		const int32 Wide = FMath::Max(Count, 3);
		TArray<FVector> Slots;
		for (const int32 Row : { 0, 1, 2 })
		{
			for (int32 Index = 0; Index < Wide; ++Index)
			{
				Slots.Add(Side.Spawn + Right * ((Index - (Wide - 1) * 0.5f) * 150.f) + Back * (Row * 150.f));
			}
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			AOperativeCharacter* Member = Side.Members[Index].Get();
			if (!Member)
			{
				continue;
			}
			FVector Chosen = SmokeUtils::FreeSpot(World, Side.Spawn + FVector(0.f, 0.f, 50.f), Member);
			bool bFound = false;
			for (int32 Try = 0; Try < Slots.Num() && !bFound; ++Try)
			{
				// Each man's own slot first, then the others in turn.
				const FVector Spot = SmokeUtils::FreeSpot(World, Slots[(Index + Try) % Slots.Num()] + FVector(0.f, 0.f, 50.f), Member);
				const float Straight = static_cast<float>(FVector::Dist(Side.Spawn, Spot));
				// The spawn spot itself (FreeSpot's capsule centre right over it): the path query finds no path between one point and itself.
				const float FromSpawn = !Nav || Straight <= 120.f ? Straight : NavPathLength(World, Nav, Side.Spawn, Spot);
				const bool bReachesGoal = !Nav || HasNavPath(World, Nav, Spot, Goal);
				// fix/arena-no-outer-strip: a slot over the play area's edge (the Depot's outer strip behind the perimeter wall) is no slot.
				if (!InArea(State, Spot))
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] side %s man %d: spawn spot %s rejected (outside the play area)"), *Side.Label, Index, *Spot.ToCompactString());
				}
				else if (PmcArenaViewRules::IsSpawnSpotUsable(bReachesGoal, FromSpawn, Straight))
				{
					Chosen = Spot;
					bFound = true;
				}
				else
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] side %s man %d: spawn spot %s rejected (path from the spawn %.0f cm for %.0f cm straight, reaches the goal %d)"),
						*Side.Label, Index, *Spot.ToCompactString(), FromSpawn, Straight, bReachesGoal ? 1 : 0);
				}
			}
			if (!bFound)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] side %s man %d: no usable spawn slot - placed at the spawn spot %s"), *Side.Label, Index, *Chosen.ToCompactString());
			}
			Member->TeleportTo(Chosen, Side.Facing, false, true);
		}
	}

	void ParkPlayerSquad(FState& State, UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad)
		{
			return;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->StopOperative();
			Member->SetActorHiddenInGame(true);
			Member->SetActorEnableCollision(false);
			Member->CustomTimeDilation = 0.f;
			Member->SetActorLocation(Member->GetActorLocation() + FVector(100000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			State.ParkedSquad.Add(Member);
		}
	}

	bool IsLive(const AOperativeCharacter* Operative)
	{
		return Operative && !Operative->IsKilledInAction() && (!Operative->HealthComponent || Operative->HealthComponent->IsAlive());
	}

	/** The overview camera (shots / record): spawned once, the canvas HUD and the widgets off, the player's view on it. */
	ACameraActor* EnsureCamera(FState& State, UWorld* World)
	{
		ACameraActor* Camera = State.Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			State.Camera = Camera;
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(State.Framing.HorizontalFovDeg);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			}
			// The fight, not the HUD: the canvas HUD and the widgets off (the capture's bShowUI only covers Slate).
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetHUD())
			{
				PC->GetHUD()->bShowHUD = false;
			}
		}
		if (Camera)
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
		return Camera;
	}

	/**
	 * The overview camera (fix/arena-clip-readability 2026-11-10): frames the box of every LIVE fighter + the objective point
	 * (PmcArenaViewRules::FrameBox; the spawns before anybody stands), steep (CamPitch, 65 deg) so the fences hide less, the yaw across the
	 * round's spawn axis, CamMin..CamMax away; eased (bSnap = jump, a new round / a still shot) - record=1 calls it every rendered frame.
	 */
	void AimCamera(FState& State, UWorld* World, float DeltaSeconds = 0.f, bool bSnap = true)
	{
		if (!State.Options.bShots && !State.Options.bRecord)
		{
			return;
		}
		ACameraActor* Camera = EnsureCamera(State, World);
		if (!Camera)
		{
			return;
		}
		TArray<FVector> Fighters;
		TArray<int32> FighterSides;
		for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[SideIndex].Members)
			{
				if (const AOperativeCharacter* Operative = Member.Get(); IsLive(Operative))
				{
					Fighters.Add(Operative->GetActorLocation());
					FighterSides.Add(SideIndex);
				}
			}
		}
		TArray<FVector> Points = PmcArenaViewRules::FramedFighters(Fighters, FighterSides, State.bRoundObjective, State.Objective);
		if (Points.IsEmpty())
		{
			Points.Add(State.Sides[0].Spawn);
			Points.Add(State.Sides[1].Spawn);
		}
		if (State.bRoundObjective)
		{
			Points.Add(State.Objective);
		}
		State.Framing.PitchDeg = State.Options.CamPitchDeg;
		State.Framing.MinDistanceCm = State.Options.CamMinCm;
		State.Framing.MaxDistanceCm = FMath::Max(State.Options.CamMinCm, State.Options.CamMaxCm);
		if (const UGameViewportClient* Viewport = World->GetGameViewport())
		{
			FVector2D Size;
			Viewport->GetViewportSize(Size);
			if (Size.X > 1.0 && Size.Y > 1.0)
			{
				State.Framing.Aspect = static_cast<float>(Size.X / Size.Y);
			}
		}
		const FPmcArenaView Target = PmcArenaViewRules::FrameBox(Points, State.Framing);
		State.View = (bSnap || !State.bHasView) ? Target : PmcArenaViewRules::Smooth(State.View, Target, DeltaSeconds);
		State.bHasView = true;
		if (FMath::FloorToInt(State.StageTime / 2.f) != FMath::FloorToInt((State.StageTime - DeltaSeconds) / 2.f) || bSnap)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] camera t=%.1f: %d points (of %d fighters), look %s, distance %.0f m (target %.0f m), yaw %.0f, pitch %.0f"), State.StageTime,
				Points.Num(), Fighters.Num(), *State.View.Look.ToCompactString(), State.View.DistanceCm / 100.f, Target.DistanceCm / 100.f, State.Framing.YawDeg, State.Framing.PitchDeg);
		}
		Camera->SetActorLocationAndRotation(PmcArenaViewRules::EyeOf(State.View, State.Framing), FRotator(-State.Framing.PitchDeg, State.Framing.YawDeg, 0.f));
	}

	/**
	 * The team overlay MID of a side (M_TargetFresnel: unlit, translucent, no depth test - seen through fences and walls), made once;
	 * bDim = the corpse's (a third of the glow, half the colour: who fell where stays readable, without looking alive).
	 */
	UMaterialInstanceDynamic* TeamOverlayOf(FState& State, int32 SideIndex, bool bDim = false)
	{
		TWeakObjectPtr<UMaterialInstanceDynamic>& Slot = bDim ? State.TeamOverlayDim[SideIndex] : State.TeamOverlay[SideIndex];
		if (UMaterialInstanceDynamic* Existing = Slot.Get())
		{
			return Existing;
		}
		UMaterialInterface* Fresnel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_TargetFresnel.M_TargetFresnel"));
		if (!Fresnel)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Overlay = UMaterialInstanceDynamic::Create(Fresnel, GetTransientPackage());
		FLinearColor Color = PmcArenaViewRules::TeamColor(SideIndex);
		if (bDim)
		{
			Color = FLinearColor(Color.R * 0.5f, Color.G * 0.5f, Color.B * 0.5f, Color.A * 0.6f);
		}
		Overlay->SetVectorParameterValue(TEXT("Color"), Color);
		Overlay->SetScalarParameterValue(TEXT("Intensity"), State.Options.TeamGlow * (bDim ? 0.35f : 1.f));
		Slot = Overlay;
		return Overlay;
	}

	/** Every mesh of the operative and of his attached actors (the weapon / launcher actors). */
	void ForEachBodyMesh(AOperativeCharacter* Operative, TFunctionRef<void(UMeshComponent*)> Visit)
	{
		TArray<AActor*> Actors;
		Actors.Add(Operative);
		Operative->GetAttachedActors(Actors, false, true);
		for (AActor* Actor : Actors)
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Actor);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (Mesh)
				{
					Visit(Mesh);
				}
			}
		}
	}

	/**
	 * Teams=on on a rendered run: every live fighter's visible meshes (the body, the weapon, attached actors) carry his side's overlay; his
	 * own occlusion silhouette (red for every PMC fighter) is switched off. Run every step: a mesh shown later (a weapon switch) or one
	 * whose overlay a hit flash took and gave back empty gets it again; a mesh with another overlay on (a hit flash) is left alone. Killed
	 * in action: once, the team overlay turns dim (TeamOverlayOf bDim) - the fallen stay readable by team (the operative's own corpse rule
	 * clears overlays only once the health reaches 0, not on KIA).
	 */
	void ApplyTeamOverlays(FState& State)
	{
		if (!State.Options.bTeams || !FApp::CanEverRender())
		{
			return;
		}
		for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
		{
			UMaterialInstanceDynamic* Overlay = TeamOverlayOf(State, SideIndex);
			if (!Overlay)
			{
				continue;
			}
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[SideIndex].Members)
			{
				AOperativeCharacter* Operative = Member.Get();
				if (!Operative)
				{
					continue;
				}
				if (!IsLive(Operative))
				{
					UMaterialInstanceDynamic* Dim = TeamOverlayOf(State, SideIndex, true);
					if (Dim && State.TeamColoured.Contains(Member) && !State.TeamDimmed.Contains(Member))
					{
						// A hit flash still on a mesh gives our bright overlay back when it ends: dimmed only once no flash is left.
						bool bFlashOn = false;
						ForEachBodyMesh(Operative, [Overlay, Dim, &bFlashOn](UMeshComponent* Mesh)
						{
							UMaterialInterface* Current = Mesh->GetOverlayMaterial();
							if (Current == Overlay || (Mesh->IsVisible() && !Current))
							{
								Mesh->SetOverlayMaterial(Dim);
							}
							else if (Current && Current != Dim)
							{
								bFlashOn = true;
							}
						});
						if (!bFlashOn)
						{
							State.TeamDimmed.Add(Member);
						}
					}
					continue;
				}
				if (!State.TeamColoured.Contains(Member))
				{
					State.TeamColoured.Add(Member);
					Operative->bEnableSilhouette = false;
				}
				ForEachBodyMesh(Operative, [Overlay](UMeshComponent* Mesh)
				{
					if (Mesh->IsVisible() && !Mesh->GetOverlayMaterial())
					{
						Mesh->SetOverlayMaterial(Overlay);
					}
				});
			}
		}
	}

	void Shot(FState& State, UWorld* World, const TCHAR* Name)
	{
		if (!State.Options.bShots)
		{
			return;
		}
		AimCamera(State, World);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("PmcArena");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString File = Dir / FString::Printf(TEXT("round%d_%s.png"), State.Round, Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		++State.ShotsTaken;
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] shot %s"), *FPaths::ConvertRelativePathToFull(File));
	}

	// --- The track (codex.pmc_arena_track/1) ---------------------------------------------------------------------------------------

	const TCHAR* SideOf(const FState& State, const AOperativeCharacter* Operative)
	{
		for (const FSide& Side : State.Sides)
		{
			if (Side.Members.Contains(Operative))
			{
				return *Side.Label;
			}
		}
		return TEXT("-");
	}

	int32 SideIndexOf(const FState& State, const AOperativeCharacter* Operative)
	{
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (Operative && State.Sides[Index].Members.Contains(Operative))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	TSharedRef<FJsonObject> EventJson(const FState& State, const TCHAR* Type)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("t"), FMath::RoundToFloat(State.StageTime * 100.f) / 100.f);
		Json->SetStringField(TEXT("type"), Type);
		return Json;
	}

	/**
	 * The round's contact moments (fix/arena-rpg-contact-roof): the shots between the sides (the RPG-7's launches at an enemy among them)
	 * and, with RocketContact=on, the rocket bursts near a live enemy. ResolveRound's contact and the flow stats both read these.
	 */
	TArray<float> ContactTimesOf(const FState& State)
	{
		TArray<float> Times = State.Track.ShotTimes;
		if (State.Options.bRocketContact)
		{
			Times.Append(State.Track.RocketBurstTimes);
		}
		return Times;
	}

	void StartTrack(FState& State, UWorld* World)
	{
		FTrack& Track = State.Track;
		Track.Fighters.Reset();
		Track.Frames.Reset();
		Track.States.Reset();
		Track.Events.Reset();
		Track.Retreated.Reset();
		Track.HealsSeen[0] = Track.HealsSeen[1] = 0;
		Track.SampleTimer = 0.f;
		Track.LastLeadSign = 0;
		Track.LeadSwings = 0;
		Track.LastHp.Reset();
		Track.LastHit.Reset();
		Track.Damage[0] = Track.Damage[1] = 0.f;
		Track.Concealed[0] = Track.Concealed[1] = 0.f;
		Track.FirstStrikeSide = INDEX_NONE;
		Track.ShotDistM[0].Reset();
		Track.ShotDistM[1].Reset();
		Track.StruckOnce.Reset();
		Track.StrikesUnobserved[0] = Track.StrikesUnobserved[1] = 0;
		Track.ShotTimes.Reset();
		Track.LastOrder.Reset();
		Track.Orders = Track.QuickRedecisions = Track.QuickSameOrder = Track.EarlyPreempts = 0;
		Track.AttackerDistSum = 0.0;
		Track.AttackerDistSamples = 0;
		Track.AttackerClosestCm = -1.f;
		Track.AttackerUnderPointS = 0.f;
		Track.Resupplies = 0;
		Track.Rockets[0] = Track.Rockets[1] = 0;
		Track.RocketBurstsNear[0] = Track.RocketBurstsNear[1] = 0;
		Track.RocketBurstTimes.Reset();
		FState* StatePtr = &State;
		// fix/arena-rpg-contact-roof: an RPG-7 rocket's burst within launcher.radius_cm (suppression.json) of a live enemy of its shooter is
		// contact (a round where only rockets flew was a no_contact draw, -3 in the trainer). Bound once for the run.
		if (UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>(); Projectiles && !Track.ProjectileHandle.IsValid())
		{
			Track.ProjectileHandle = Projectiles->OnProjectileFinished.AddLambda([StatePtr](const FProjectileImpact& Impact)
			{
				if (!Impact.bLauncher || !Impact.bExploded || StatePtr->Stage != 2)
				{
					return;
				}
				const AOperativeCharacter* Shooter = Cast<AOperativeCharacter>(Impact.Instigator.Get());
				const int32 ShooterSide = SideIndexOf(*StatePtr, Shooter);
				if (ShooterSide == INDEX_NONE)
				{
					return;
				}
				TArray<FVector> Enemies;
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : StatePtr->Sides[1 - ShooterSide].Members)
				{
					const AOperativeCharacter* Operative = Member.Get();
					if (Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive())
					{
						Enemies.Add(Operative->GetActorLocation());
					}
				}
				const float RadiusCm = SuppressionRules::GetConfig().LauncherRadiusCm;
				const bool bNear = PmcArenaRoundRules::IsLauncherContact(Impact.Location, Enemies, RadiusCm);
				FTrack& BurstTrack = StatePtr->Track;
				TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("rocket_burst"));
				Json->SetStringField(TEXT("from"), Shooter->DisplayName.ToString());
				Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Shooter));
				Json->SetNumberField(TEXT("x"), FMath::RoundToFloat(Impact.Location.X));
				Json->SetNumberField(TEXT("y"), FMath::RoundToFloat(Impact.Location.Y));
				Json->SetBoolField(TEXT("near_enemy"), bNear);
				BurstTrack.Events.Add(MakeShared<FJsonValueObject>(Json));
				if (bNear)
				{
					++BurstTrack.RocketBurstsNear[ShooterSide];
					BurstTrack.RocketBurstTimes.Add(StatePtr->StageTime);
				}
			});
		}
		for (FSide& Side : State.Sides)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
			{
				AOperativeCharacter* Operative = Member.Get();
				if (!Operative)
				{
					continue;
				}
				Track.Fighters.Add(Operative);
				Track.LastHp.Add(Operative, Operative->HealthComponent ? FMath::Max(0.f, Operative->HealthComponent->GetCurrentHealth()) : 0.f);
				if (Track.Listened.Contains(Operative))
				{
					continue;
				}
				Track.Listened.Add(Operative);
				// Shots: from -> to, hit / miss. Kills: the health component's death.
				Track.FireHandles.Add(Operative->OnWeaponFiredNative.AddLambda([StatePtr](AOperativeCharacter* Shooter, AActor* Target, bool bHit)
				{
					TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("shot"));
					Json->SetStringField(TEXT("from"), Shooter ? Shooter->DisplayName.ToString() : TEXT("?"));
					Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Shooter));
					Json->SetStringField(TEXT("to"), Target ? Target->GetName() : TEXT("?"));
					Json->SetBoolField(TEXT("hit"), bHit);
					// Task #53 ambush stats: a shot at the other side - its distance, whether the target's team saw the shooter (checked
					// before this shot's own demask lands: the team sight updates on its interval), the first hit of the round.
					const AOperativeCharacter* Victim = Cast<AOperativeCharacter>(Target);
					const int32 ShooterSide = SideIndexOf(*StatePtr, Shooter);
					const int32 VictimSide = SideIndexOf(*StatePtr, Victim);
					// The RPG-7's launch (fix/arena-rpg-contact-roof): counted per side whatever its target; at an enemy it is a shot below, and its
					// burst near an enemy is another contact moment (the projectile listener).
					if (Shooter && ShooterSide != INDEX_NONE && Shooter->IsLauncherEquipped())
					{
						++StatePtr->Track.Rockets[ShooterSide];
						Json->SetBoolField(TEXT("rocket"), true);
					}
					if (Shooter && Victim && ShooterSide != INDEX_NONE && VictimSide != INDEX_NONE && ShooterSide != VictimSide)
					{
						FTrack& ShotTrack = StatePtr->Track;
						ShotTrack.ShotTimes.Add(StatePtr->StageTime);
						const float DistM = static_cast<float>(FVector::Dist2D(Shooter->GetActorLocation(), Victim->GetActorLocation())) / 100.f;
						ShotTrack.ShotDistM[ShooterSide].Add(DistM);
						const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(Shooter->GetWorld());
						const bool bConcealed = Pmc && !Pmc->IsVisibleToTeam(Victim->TeamId, Shooter);
						Json->SetBoolField(TEXT("concealed"), bConcealed);
						Json->SetNumberField(TEXT("dist_m"), FMath::RoundToFloat(DistM * 10.f) / 10.f);
						if (bHit)
						{
							if (ShotTrack.FirstStrikeSide == INDEX_NONE)
							{
								ShotTrack.FirstStrikeSide = ShooterSide;
							}
							ShotTrack.LastHit.Add(const_cast<AOperativeCharacter*>(Victim), { ShooterSide, bConcealed, StatePtr->StageTime });
							// The first hit on him: was he looking away from the shooter (his facing > 45 deg off the bearing to him)?
							if (!ShotTrack.StruckOnce.Contains(Victim))
							{
								ShotTrack.StruckOnce.Add(const_cast<AOperativeCharacter*>(Victim));
								const float Off = AmbushWatchRules::AngleOffDeg(static_cast<float>(Victim->GetActorRotation().Yaw),
									AmbushWatchRules::BearingDeg(FVector2D(Victim->GetActorLocation()), FVector2D(Shooter->GetActorLocation())));
								ShotTrack.StrikesUnobserved[ShooterSide] += Off > 45.f ? 1 : 0;
								Json->SetBoolField(TEXT("unobserved"), Off > 45.f);
							}
						}
					}
					StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
				}));
				if (Operative->HealthComponent)
				{
					Operative->HealthComponent->OnDiedNative.AddLambda([StatePtr](AActor* Victim, const FString& AttackerSource)
					{
						TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("kill"));
						const AOperativeCharacter* Fallen = Cast<AOperativeCharacter>(Victim);
						Json->SetStringField(TEXT("target"), Fallen ? Fallen->DisplayName.ToString() : (Victim ? Victim->GetName() : TEXT("?")));
						Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Fallen));
						Json->SetStringField(TEXT("by"), AttackerSource);
						StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
					});
				}
			}
		}
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World); Pmc && !Track.OrderHandle.IsValid())
		{
			// The commander's decisions: the chosen option with its (top) score; heals / flanks / suppressions / ambushes / retreats among them.
			Track.OrderHandle = Pmc->OnFighterOrder.AddLambda([StatePtr](int32 TeamId, AOperativeCharacter* Fighter, EPmcOrder Order, float Score, EPmcPickReason Reason)
			{
				TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("tactic"));
				// The fight flow (fix/pmc-arena-stalemate): why it was picked, quick re-decisions, a running order replaced inside its minimum hold.
				FTrack& FlowTrack = StatePtr->Track;
				++FlowTrack.Orders;
				const TCHAR* ReasonName = Reason == EPmcPickReason::Preempt ? TEXT("preempt") : (Reason == EPmcPickReason::Invalid ? TEXT("invalid")
					: (Reason == EPmcPickReason::Keep ? TEXT("keep") : TEXT("best")));
				Json->SetStringField(TEXT("why"), ReasonName);
				// Ages on the world clock (the commander's own; the arena's stage clock steps in 0.1 s ticks).
				const float WorldNow = Fighter && Fighter->GetWorld() ? static_cast<float>(Fighter->GetWorld()->GetTimeSeconds()) : StatePtr->StageTime;
				if (const TPair<float, EPmcOrder>* Last = Fighter ? FlowTrack.LastOrder.Find(Fighter) : nullptr)
				{
					const float Age = WorldNow - Last->Key;
					FlowTrack.QuickRedecisions += Age < QuickRedecisionSeconds ? 1 : 0;
					FlowTrack.QuickSameOrder += Age < QuickRedecisionSeconds && Last->Value == Order ? 1 : 0;
					const FPmcWeights& Weights = Fighter->TeamId == TeamA ? StatePtr->WeightsA : StatePtr->WeightsB;
					FlowTrack.EarlyPreempts += Reason == EPmcPickReason::Preempt && Age < Weights.OrderMinHoldSeconds - 0.05f ? 1 : 0;
				}
				if (Fighter)
				{
					FlowTrack.LastOrder.Add(Fighter, TPair<float, EPmcOrder>(WorldNow, Order));
				}
				Json->SetStringField(TEXT("who"), Fighter ? Fighter->DisplayName.ToString() : TEXT("?"));
				Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Fighter));
				Json->SetNumberField(TEXT("team"), TeamId);
				Json->SetStringField(TEXT("decision"), PmcCommanderRules::OrderName(Order));
				Json->SetNumberField(TEXT("score"), FMath::RoundToFloat(Score * 100.f) / 100.f);
				StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
				if (Order == EPmcOrder::FallBack && Fighter)
				{
					StatePtr->Track.Retreated.Add(Fighter);
				}
			});
		}
	}

	const TCHAR* StanceName(EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching: return TEXT("crouch");
		case EOperativeStance::Prone: return TEXT("prone");
		default: return TEXT("stand");
		}
	}

	void SampleTrack(FState& State, UWorld* World, float DeltaTime)
	{
		FTrack& Track = State.Track;
		Track.SampleTimer -= DeltaTime;
		if (Track.SampleTimer > 0.f)
		{
			return;
		}
		Track.SampleTimer = TrackInterval;
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		TArray<TSharedPtr<FJsonValue>> Frame;
		TArray<TSharedPtr<FJsonValue>> Detail;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : Track.Fighters)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			const bool bAlive = Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
			if (!bAlive)
			{
				Frame.Add(MakeShared<FJsonValueNull>());
				Detail.Add(MakeShared<FJsonValueNull>());
				continue;
			}
			const FVector Location = Operative->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> Point = { MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Location.X)), MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Location.Y)) };
			Frame.Add(MakeShared<FJsonValueArray>(Point));
			FString StateName = Operative->IsPlayerTeam() && !(Pmc && Pmc->IsTeamCommanded(PmcTeam::Player)) ? TEXT("player")
				: PmcCommanderRules::OrderName(Pmc ? Pmc->GetFighterOrder(Operative) : EPmcOrder::Hold);
			if (Operative->bInCover)
			{
				StateName += TEXT("/cover");
			}
			TArray<TSharedPtr<FJsonValue>> Extra = { MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Operative->GetActorRotation().Yaw)),
				MakeShared<FJsonValueString>(StanceName(Operative->GetStance())), MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Operative->HealthComponent->GetCurrentHealth())),
				MakeShared<FJsonValueString>(StateName) };
			Detail.Add(MakeShared<FJsonValueArray>(Extra));
		}
		Track.Frames.Add(MakeShared<FJsonValueArray>(Frame));
		Track.States.Add(MakeShared<FJsonValueArray>(Detail));
		// The attackers' distance to the point (fix/pmc-arena-stalemate: do they walk onto it?).
		if (State.bRoundObjective)
		{
			double Sum = 0.0;
			int32 Count = 0;
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[State.bAAttacks ? 0 : 1].Members)
			{
				const AOperativeCharacter* Operative = Member.Get();
				if (Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive())
				{
					// fix/pmc-onpoint-3d-detour: level-aware (a man under a roof point is not "at" it).
					const FVector Feet = FeetOf(*Operative);
					const float Distance = PmcCommanderRules::LevelAwareDistanceCm(Feet, State.Objective, State.Config.ObjectiveLevelCm,
						State.Config.ObjectiveRadiusCm + 1.f);
					if (FVector::Dist2D(Feet, State.Objective) <= State.Config.ObjectiveRadiusCm && !PmcCommanderRules::IsOnPoint(Feet, State.Objective,
						State.Config.ObjectiveRadiusCm, State.Config.ObjectiveLevelCm))
					{
						Track.AttackerUnderPointS += TrackInterval;
					}
					Sum += Distance;
					++Count;
					Track.AttackerClosestCm = Track.AttackerClosestCm < 0.f ? Distance : FMath::Min(Track.AttackerClosestCm, Distance);
				}
			}
			if (Count > 0)
			{
				Track.AttackerDistSum += Sum / Count;
				++Track.AttackerDistSamples;
			}
		}
		// Heals: the team statistics' counter moving between two samples.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FPmcTeamStats* Stats = Pmc ? Pmc->GetTeamStats(State.Sides[Index].TeamId) : nullptr;
			if (Stats && Stats->Heals > Track.HealsSeen[Index])
			{
				Track.HealsSeen[Index] = Stats->Heals;
				TSharedRef<FJsonObject> Json = EventJson(State, TEXT("heal"));
				Json->SetStringField(TEXT("side"), State.Sides[Index].Label);
				Track.Events.Add(MakeShared<FJsonValueObject>(Json));
			}
		}
		// Lead swings: the sign of (alive A - alive B) changing.
		const int32 Lead = CountAlive(State.Sides[0].Members) - CountAlive(State.Sides[1].Members);
		const int32 Sign = Lead > 0 ? 1 : (Lead < 0 ? -1 : 0);
		if (Sign != 0 && Track.LastLeadSign != 0 && Sign != Track.LastLeadSign)
		{
			++Track.LeadSwings;
		}
		if (Sign != 0)
		{
			Track.LastLeadSign = Sign;
		}
	}

	/**
	 * Damage per side (task #53): each fighter's HP drop since the last sample goes to the side of his last hit (within 3 s) - and to its
	 * damage from concealment when that shooter was not seen by the target's team.
	 */
	void UpdateDamage(FState& State)
	{
		FTrack& Track = State.Track;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : Track.Fighters)
		{
			float* Last = Track.LastHp.Find(Fighter);
			if (!Last)
			{
				continue;
			}
			const AOperativeCharacter* Operative = Fighter.Get();
			const float Hp = Operative && !Operative->IsKilledInAction() && Operative->HealthComponent ? FMath::Max(0.f, Operative->HealthComponent->GetCurrentHealth()) : 0.f;
			const float Drop = *Last - Hp;
			*Last = Hp;
			const FHitNote* Note = Drop > 0.01f ? Track.LastHit.Find(Fighter) : nullptr;
			if (Note && Note->Side != INDEX_NONE && State.StageTime - Note->Time <= 3.f)
			{
				Track.Damage[Note->Side] += Drop;
				Track.Concealed[Note->Side] += Note->bConcealed ? Drop : 0.f;
			}
		}
	}

	/**
	 * Live men of Side on the point: within the objective's radius (planar) AND on its level (fix/pmc-onpoint-3d-detour: feet vs the navmesh
	 * point within objective_level_m, 1.5 m; was the capsule centre within 3 m, which let a man one floor below count).
	 */
	int32 CountOnPoint(const FState& State, const FSide& Side)
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			if (Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive()
				&& PmcCommanderRules::IsOnPoint(FeetOf(*Operative), State.Objective, State.Config.ObjectiveRadiusCm, State.Config.ObjectiveLevelCm))
			{
				++Count;
			}
		}
		return Count;
	}

	/** Ammo=resupply: a live fighter whose current weapon's reserve ran dry (the clip too low to fight on) gets the weapon's reserve back. */
	void ResupplyAmmo(FState& State)
	{
		if (!State.Options.bResupply)
		{
			return;
		}
		for (FSide& Side : State.Sides)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
			{
				AOperativeCharacter* Operative = Member.Get();
				if (!Operative || Operative->IsKilledInAction() || !Operative->UsesAmmo() || !Operative->CurrentWeapon || Operative->IsLauncherEquipped()
					|| Operative->ReserveAmmo > 0)
				{
					continue;
				}
				const int32 Magazine = FMath::Max(1, Operative->CurrentWeapon->Stats.Ammo.MagazineSize);
				if (Operative->CurrentClip * 3 > Magazine)
				{
					continue;
				}
				Operative->ReserveAmmo = FMath::Max(Magazine, Operative->CurrentWeapon->Stats.Ammo.ReserveAmmo);
				++State.Track.Resupplies;
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] resupply: %s's %s reserve back to %d (Ammo=resupply)"), *Operative->DisplayName.ToString(),
					*Operative->CurrentWeapon->WeaponId, Operative->ReserveAmmo);
			}
		}
	}

	/** One step of the capture clock and the commanders' objective inputs (the time fraction, the clock, an enemy on the point). */
	void UpdateObjective(FState& State, UWorld* World)
	{
		if (!State.bRoundObjective)
		{
			return;
		}
		const int32 Atk = State.bAAttacks ? 0 : 1;
		const int32 AttackersOn = CountOnPoint(State, State.Sides[Atk]);
		const int32 DefendersOn = CountOnPoint(State, State.Sides[1 - Atk]);
		if (State.Clock.Step(AttackersOn, DefendersOn, StepSeconds))
		{
			TSharedRef<FJsonObject> Json = EventJson(State, TEXT("objective"));
			Json->SetStringField(TEXT("event"), TEXT("captured"));
			Json->SetStringField(TEXT("side"), State.Sides[Atk].Label);
			State.Track.Events.Add(MakeShared<FJsonValueObject>(Json));
		}
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
		{
			const float TimeFrac = FMath::Clamp(State.StageTime / FMath::Max(State.Options.TimeLimit, 1.f), 0.f, 1.f);
			Pmc->UpdateTeamObjective(State.Sides[Atk].TeamId, TimeFrac, State.Clock.HoldFrac(), DefendersOn > 0);
			Pmc->UpdateTeamObjective(State.Sides[1 - Atk].TeamId, TimeFrac, State.Clock.HoldFrac(), AttackersOn > 0);
		}
	}

	float Median(TArray<float> Values)
	{
		if (Values.IsEmpty())
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Mid = Values.Num() / 2;
		return Values.Num() % 2 == 1 ? Values[Mid] : 0.5f * (Values[Mid - 1] + Values[Mid]);
	}

	FString WriteTrack(FState& State, UWorld* World, const FRoundResult& Result)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("codex.pmc_arena_track/1"));
		Root->SetStringField(TEXT("run"), State.RunName);
		Root->SetNumberField(TEXT("round"), Result.Index);
		Root->SetStringField(TEXT("map"), World->GetMapName());
		Root->SetNumberField(TEXT("seed"), State.Options.Seed);
		Root->SetNumberField(TEXT("round_seed"), Result.Seed);
		Root->SetNumberField(TEXT("dt"), TrackInterval);
		Root->SetStringField(TEXT("units"), TEXT("cm"));
		Root->SetStringField(TEXT("winner"), Result.Winner);
		Root->SetNumberField(TEXT("duration_s"), FMath::RoundToFloat(Result.Duration * 10.f) / 10.f);
		Root->SetNumberField(TEXT("highlight_score"), Result.HighlightScore);
		Root->SetStringField(TEXT("scenario"), Result.Scenario);
		if (Result.bObjective)
		{
			TSharedRef<FJsonObject> Objective = MakeShared<FJsonObject>();
			Objective->SetNumberField(TEXT("x"), FMath::RoundToFloat(Result.ObjectiveLocation.X));
			Objective->SetNumberField(TEXT("y"), FMath::RoundToFloat(Result.ObjectiveLocation.Y));
			Objective->SetNumberField(TEXT("radius"), State.Config.ObjectiveRadiusCm);
			Objective->SetStringField(TEXT("attacker"), Result.Attacker);
			Root->SetObjectField(TEXT("objective"), Objective);
		}
		TArray<TSharedPtr<FJsonValue>> Fighters;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Track.Fighters)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Operative ? Operative->DisplayName.ToString() : TEXT("?"));
			Json->SetStringField(TEXT("side"), SideOf(State, Operative));
			Json->SetNumberField(TEXT("team"), Operative ? Operative->TeamId : -1);
			Fighters.Add(MakeShared<FJsonValueObject>(Json));
		}
		Root->SetArrayField(TEXT("fighters"), Fighters);
		Root->SetArrayField(TEXT("frames"), State.Track.Frames);
		Root->SetStringField(TEXT("states_comment"), TEXT("per frame per fighter [yaw, stance, hp, state] (null when down) - the arena's extra beside the contract"));
		Root->SetArrayField(TEXT("states"), State.Track.States);
		Root->SetArrayField(TEXT("events"), State.Track.Events);
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		const FString Dir = TrackDir(State);
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = FPaths::ConvertRelativePathToFull(Dir / FString::Printf(TEXT("round_%d.track.json"), Result.Index));
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return Path;
	}

	/**
	 * How much a round is worth watching (the user wants the juiciest fights): kills x1 + a decided round +1 + a close finish (the winner's
	 * last man standing) +2 + lead swings x1.5 + successful flanks x1 + ambushes sprung x0.5 + heals x0.5, and the duration sanity: a 20-150 s
	 * round +1, shorter or longer -1 (nothing happened, or a stalemate).
	 */
	float HighlightScoreOf(const FRoundResult& Result)
	{
		float Score = static_cast<float>(Result.Kills[0] + Result.Kills[1]);
		const bool bDecided = Result.Winner == TEXT("A") || Result.Winner == TEXT("B");
		Score += bDecided ? 1.f : 0.f;
		const int32 WinnerSurvivors = Result.Winner == TEXT("A") ? Result.Survivors[0] : (Result.Winner == TEXT("B") ? Result.Survivors[1] : 0);
		Score += bDecided && WinnerSurvivors == 1 ? 2.f : 0.f;
		Score += Result.LeadSwings * 1.5f;
		Score += static_cast<float>(Result.Stats[0].FlanksSucceeded + Result.Stats[1].FlanksSucceeded);
		Score += 0.5f * static_cast<float>(Result.Stats[0].Ambushes + Result.Stats[1].Ambushes);
		Score += 0.5f * static_cast<float>(Result.Stats[0].Heals + Result.Stats[1].Heals);
		Score += Result.Duration >= 20.f && Result.Duration <= 150.f ? 1.f : -1.f;
		return FMath::RoundToFloat(Score * 10.f) / 10.f;
	}

	TSharedRef<FJsonObject> StatsJson(const FPmcTeamStats& Stats)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("flanks_attempted"), Stats.FlanksAttempted);
		Json->SetNumberField(TEXT("flanks_succeeded"), Stats.FlanksSucceeded);
		Json->SetNumberField(TEXT("suppressions"), Stats.Suppressions);
		Json->SetNumberField(TEXT("suppression_s"), FMath::RoundToFloat(Stats.SuppressionSeconds * 10.f) / 10.f);
		Json->SetNumberField(TEXT("ambushes"), Stats.Ambushes);
		Json->SetNumberField(TEXT("retreats"), Stats.Retreats);
		Json->SetNumberField(TEXT("standoffs"), Stats.Standoffs);
		Json->SetNumberField(TEXT("heals"), Stats.Heals);
		Json->SetNumberField(TEXT("relocations"), Stats.Relocations);
		Json->SetNumberField(TEXT("advances"), Stats.Advances);
		Json->SetNumberField(TEXT("covers"), Stats.Covers);
		Json->SetNumberField(TEXT("friendly_fire_hits"), Stats.FriendlyFireHits);
		Json->SetNumberField(TEXT("stuck_events"), Stats.StuckEvents);
		Json->SetNumberField(TEXT("orders"), Stats.OrdersIssued);
		Json->SetNumberField(TEXT("objective_moves"), Stats.ObjectiveMoves);
		Json->SetNumberField(TEXT("rpg_strikes"), Stats.RpgStrikes);
		Json->SetNumberField(TEXT("orders_failed"), Stats.OrdersFailed);
		Json->SetNumberField(TEXT("detours"), Stats.Detours);
		Json->SetNumberField(TEXT("preempts"), Stats.Preempts);
		Json->SetNumberField(TEXT("push_decisions"), Stats.PushDecisions);
		return Json;
	}

	FString WriteJson(FState& State, UWorld* World)
	{
		const FString Path = ResultPath(State);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("codex.pmc_arena/1"));
		Root->SetStringField(TEXT("map"), State.Options.MapArg.IsEmpty() ? World->GetMapName() : State.Options.MapArg);
		Root->SetStringField(TEXT("opponent"), OpponentName(State.Options.Opponent));
		Root->SetNumberField(TEXT("seed"), State.Options.Seed);
		Root->SetNumberField(TEXT("rounds_requested"), State.Options.Rounds);
		Root->SetBoolField(TEXT("rounds_limited"), State.bRoundsLimited);
		Root->SetNumberField(TEXT("time_limit_s"), State.Options.TimeLimit);
		Root->SetNumberField(TEXT("squad_size"), State.Options.Squad);
		Root->SetStringField(TEXT("run"), State.RunName);
		Root->SetBoolField(TEXT("record"), State.Options.bRecord);
		Root->SetStringField(TEXT("weights_a"), State.Options.WeightsA.IsEmpty() ? TEXT("Content/Data/AI/pmc_commander.json") : State.Options.WeightsA);
		Root->SetStringField(TEXT("weights_b"), State.Options.Opponent == EOpponent::Bot ? TEXT("None") : (State.Options.WeightsB.IsEmpty() ? TEXT("Content/Data/AI/pmc_commander.json") : State.Options.WeightsB));
		Root->SetBoolField(TEXT("objective"), State.Options.bObjective);
		Root->SetStringField(TEXT("scenario"), State.Options.Scenario < 0 ? TEXT("random") : PmcCommanderRules::ScenarioName(static_cast<EPmcScenario>(State.Options.Scenario)));
		{
			TSharedRef<FJsonObject> Config = MakeShared<FJsonObject>();
			Config->SetNumberField(TEXT("objective_radius_m"), State.Config.ObjectiveRadiusCm / 100.f);
			Config->SetNumberField(TEXT("objective_level_m"), State.Config.ObjectiveLevelCm / 100.f);
			Config->SetNumberField(TEXT("objective_hold_s"), State.Config.HoldSeconds);
			Config->SetNumberField(TEXT("objective_offset_frac"), State.Config.ObjectiveOffsetFrac);
			Config->SetStringField(TEXT("source"), State.Config.Source);
			Root->SetObjectField(TEXT("arena_config"), Config);
		}
		Root->SetStringField(TEXT("highlight_formula"), TEXT("kills + decided(1) + last_man_standing(2) + lead_swings*1.5 + flanks_succeeded + 0.5*ambushes + 0.5*heals + duration_20_150s(+1 else -1)"));
		Root->SetStringField(TEXT("started"), State.Started.ToIso8601());
		Root->SetStringField(TEXT("finished"), FDateTime::Now().ToIso8601());
		Root->SetStringField(TEXT("started_utc"), State.StartedUtc.ToIso8601());
		Root->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
		TSharedRef<FJsonObject> Weights = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> WeightsA = MakeShared<FJsonObject>();
		WeightsA->SetStringField(TEXT("file"), State.Options.WeightsA.IsEmpty() ? TEXT("pmc_commander.json") : State.Options.WeightsA);
		WeightsA->SetStringField(TEXT("name"), State.WeightsA.Name);
		WeightsA->SetBoolField(TEXT("loaded"), State.bWeightsALoaded);
		TSharedRef<FJsonObject> WeightsB = MakeShared<FJsonObject>();
		WeightsB->SetStringField(TEXT("file"), State.Options.WeightsB.IsEmpty() ? TEXT("pmc_commander.json") : State.Options.WeightsB);
		WeightsB->SetStringField(TEXT("name"), State.WeightsB.Name);
		WeightsB->SetBoolField(TEXT("loaded"), State.bWeightsBLoaded);
		Weights->SetObjectField(TEXT("A"), WeightsA);
		Weights->SetObjectField(TEXT("B"), WeightsB);
		Root->SetObjectField(TEXT("weights"), Weights);

		TArray<TSharedPtr<FJsonValue>> Rounds;
		int32 WinsA = 0;
		int32 WinsB = 0;
		int32 Draws = 0;
		float Duration = 0.f;
		for (const FRoundResult& Result : State.Results)
		{
			TSharedRef<FJsonObject> Round = MakeShared<FJsonObject>();
			Round->SetNumberField(TEXT("index"), Result.Index);
			Round->SetNumberField(TEXT("seed"), Result.Seed);
			Round->SetStringField(TEXT("attacker"), Result.Attacker);
			Round->SetStringField(TEXT("scenario"), Result.Scenario);
			Round->SetStringField(TEXT("scenario_requested"), Result.ScenarioRequested);
			Round->SetStringField(TEXT("weather"), Result.Weather);
			{
				TSharedRef<FJsonObject> Objective = MakeShared<FJsonObject>();
				Objective->SetBoolField(TEXT("enabled"), Result.bObjective);
				Objective->SetStringField(TEXT("result"), Result.ObjectiveResult);
				if (Result.bObjective)
				{
					Objective->SetNumberField(TEXT("x"), FMath::RoundToFloat(Result.ObjectiveLocation.X));
					Objective->SetNumberField(TEXT("y"), FMath::RoundToFloat(Result.ObjectiveLocation.Y));
					Objective->SetNumberField(TEXT("z"), FMath::RoundToFloat(Result.ObjectiveLocation.Z));
					Objective->SetNumberField(TEXT("radius_m"), State.Config.ObjectiveRadiusCm / 100.f);
					Objective->SetNumberField(TEXT("level_m"), State.Config.ObjectiveLevelCm / 100.f);
					Objective->SetNumberField(TEXT("hold_s"), State.Config.HoldSeconds);
					Objective->SetNumberField(TEXT("attacker_clock_s"), FMath::RoundToFloat(Result.AttackerHeld * 10.f) / 10.f);
					// fix/arena-rpg-contact-roof: a full navmesh path from each side's spawn to the point at placement.
					Objective->SetBoolField(TEXT("reach_attacker"), Result.bReachAttacker);
					Objective->SetBoolField(TEXT("reach_defender"), Result.bReachDefender);
				}
				Round->SetObjectField(TEXT("objective"), Objective);
			}
			Round->SetStringField(TEXT("winner"), Result.Winner);
			Round->SetNumberField(TEXT("duration_s"), FMath::RoundToFloat(Result.Duration * 10.f) / 10.f);
			Round->SetStringField(TEXT("end_reason"), Result.EndReason);
			Round->SetBoolField(TEXT("fight_started"), Result.bFightStarted);
			// fix/pmc-arena-stalemate: a round without a shot between the sides is a no-reward draw for the trainer; the fight flow numbers.
			Round->SetBoolField(TEXT("contact"), Result.bContact);
			{
				TSharedRef<FJsonObject> Flow = MakeShared<FJsonObject>();
				auto Round1 = [](float Value) { return FMath::RoundToFloat(Value * 10.f) / 10.f; };
				Flow->SetNumberField(TEXT("shots_per_min"), Round1(Result.ShotsPerMin));
				Flow->SetNumberField(TEXT("first_contact_s"), Round1(Result.FirstContactS));
				Flow->SetNumberField(TEXT("longest_quiet_s"), Round1(Result.LongestQuietS));
				Flow->SetNumberField(TEXT("attacker_mean_dist_m"), Round1(Result.AttackerMeanDistM));
				Flow->SetNumberField(TEXT("attacker_closest_m"), Round1(Result.AttackerClosestM));
				Flow->SetNumberField(TEXT("attacker_under_point_s"), Round1(Result.AttackerUnderPointS));
				Flow->SetNumberField(TEXT("orders_per_fighter_min"), FMath::RoundToFloat(Result.OrdersPerFighterMin * 100.f) / 100.f);
				Flow->SetNumberField(TEXT("quick_redecisions"), Result.QuickRedecisions);
				Flow->SetNumberField(TEXT("quick_same_order"), Result.QuickSameOrder);
				Flow->SetNumberField(TEXT("early_preempts"), Result.EarlyPreempts);
				Flow->SetNumberField(TEXT("orders_failed"), Result.OrdersFailed);
				Flow->SetNumberField(TEXT("detours"), Result.Detours);
				Flow->SetNumberField(TEXT("resupplies"), Result.Resupplies);
				Flow->SetNumberField(TEXT("contact_events"), Result.ContactEvents);
				Round->SetObjectField(TEXT("flow"), Flow);
			}
			Round->SetNumberField(TEXT("lead_swings"), Result.LeadSwings);
			Round->SetNumberField(TEXT("highlight_score"), Result.HighlightScore);
			// The track relative to this result file (the trainer's convention).
			FString TrackRelative = Result.TrackFile;
			FPaths::MakePathRelativeTo(TrackRelative, *Path);
			Round->SetStringField(TEXT("track"), TrackRelative);
			TSharedRef<FJsonObject> Sides = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				TSharedRef<FJsonObject> Side = MakeShared<FJsonObject>();
				Side->SetStringField(TEXT("team"), Index == 0 || State.Options.Opponent == EOpponent::Pmc ? TEXT("pmc") : (State.Options.Opponent == EOpponent::Bot ? TEXT("bot") : TEXT("player")));
				Side->SetNumberField(TEXT("survivors"), Result.Survivors[Index]);
				Side->SetNumberField(TEXT("squad_size"), State.Options.Squad);
				Side->SetNumberField(TEXT("hp_lost"), FMath::RoundToFloat(Result.HpLost[Index]));
				Side->SetNumberField(TEXT("hp_max"), FMath::RoundToFloat(Result.HpMax[Index]));
				Side->SetNumberField(TEXT("kills"), Result.Kills[Index]);
				Side->SetNumberField(TEXT("flanks_attempted"), Result.Stats[Index].FlanksAttempted);
				Side->SetNumberField(TEXT("flanks_succeeded"), Result.Stats[Index].FlanksSucceeded);
				Side->SetNumberField(TEXT("suppression_s"), FMath::RoundToFloat(Result.Stats[Index].SuppressionSeconds * 10.f) / 10.f);
				Side->SetNumberField(TEXT("ambushes"), Result.Stats[Index].Ambushes);
				Side->SetNumberField(TEXT("retreats"), Result.Stats[Index].Retreats);
				Side->SetNumberField(TEXT("retreats_survived"), Result.RetreatsSurvived[Index]);
				Side->SetNumberField(TEXT("heals"), Result.Stats[Index].Heals);
				Side->SetNumberField(TEXT("friendly_fire"), Result.Stats[Index].FriendlyFireHits);
				Side->SetNumberField(TEXT("stuck_events"), Result.Stats[Index].StuckEvents);
				// Task #53: the role and the ambush stats.
				const bool bAttacker = Result.Attacker == (Index == 0 ? TEXT("A") : TEXT("B"));
				Side->SetStringField(TEXT("role"), bAttacker ? TEXT("attacker") : TEXT("defender"));
				Side->SetNumberField(TEXT("damage_dealt"), FMath::RoundToFloat(Result.DamageDealt[Index]));
				Side->SetNumberField(TEXT("damage_from_concealment"), FMath::RoundToFloat(Result.DamageFromConcealment[Index]));
				Side->SetNumberField(TEXT("first_strike"), Result.FirstStrike[Index]);
				Side->SetNumberField(TEXT("objective_hold_s"), FMath::RoundToFloat(Result.ObjectiveHoldS[Index] * 10.f) / 10.f);
				Side->SetNumberField(TEXT("median_engagement_m"), FMath::RoundToFloat(Result.MedianEngagementM[Index] * 10.f) / 10.f);
				Side->SetNumberField(TEXT("shots"), Result.Shots[Index]);
				Side->SetNumberField(TEXT("rockets"), Result.Rockets[Index]);
				Side->SetNumberField(TEXT("rocket_bursts_near"), Result.RocketBurstsNear[Index]);
				Side->SetNumberField(TEXT("strikes_unobserved"), Result.StrikesUnobserved[Index]);
				Sides->SetObjectField(Index == 0 ? TEXT("A") : TEXT("B"), Side);
			}
			Round->SetObjectField(TEXT("sides"), Sides);
			auto Pair = [&Result](auto Getter)
			{
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetNumberField(TEXT("A"), Getter(0));
				Json->SetNumberField(TEXT("B"), Getter(1));
				return Json;
			};
			Round->SetObjectField(TEXT("survivors"), Pair([&Result](int32 Index) { return Result.Survivors[Index]; }));
			Round->SetObjectField(TEXT("hp_lost"), Pair([&Result](int32 Index) { return FMath::RoundToFloat(Result.HpLost[Index]); }));
			Round->SetObjectField(TEXT("kills"), Pair([&Result](int32 Index) { return Result.Kills[Index]; }));
			TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
			Stats->SetObjectField(TEXT("A"), StatsJson(Result.Stats[0]));
			Stats->SetObjectField(TEXT("B"), StatsJson(Result.Stats[1]));
			Round->SetObjectField(TEXT("stats"), Stats);
			Rounds.Add(MakeShared<FJsonValueObject>(Round));
			WinsA += Result.Winner == TEXT("A") ? 1 : 0;
			WinsB += Result.Winner == TEXT("B") ? 1 : 0;
			Draws += Result.Winner == TEXT("draw") ? 1 : 0;
			Duration += Result.Duration;
		}
		Root->SetArrayField(TEXT("rounds"), Rounds);
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("rounds"), State.Results.Num());
		Summary->SetNumberField(TEXT("A_wins"), WinsA);
		Summary->SetNumberField(TEXT("B_wins"), WinsB);
		Summary->SetNumberField(TEXT("draws"), Draws);
		Summary->SetNumberField(TEXT("avg_duration_s"), State.Results.IsEmpty() ? 0.f : FMath::RoundToFloat(Duration / State.Results.Num() * 10.f) / 10.f);
		Root->SetObjectField(TEXT("summary"), Summary);

		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] rounds=%d A_wins=%d B_wins=%d draws=%d avg_duration=%.1fs opponent=%s -> %s"), State.Results.Num(), WinsA, WinsB, Draws,
			State.Results.IsEmpty() ? 0.f : Duration / State.Results.Num(), OpponentName(State.Options.Opponent), *FPaths::ConvertRelativePathToFull(Path));
		return Path;
	}

	/** The smoke's validation of the file it wrote (the schema the trainer reads). */
	void ValidateJson(FState& State, const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		const bool bParsed = FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
		Check(State, bParsed, TEXT("arena JSON written and parsed"));
		if (!bParsed)
		{
			return;
		}
		Check(State, Json->GetStringField(TEXT("schema")) == TEXT("codex.pmc_arena/1"), TEXT("schema codex.pmc_arena/1"));
		const TArray<TSharedPtr<FJsonValue>>* Rounds = nullptr;
		Check(State, Json->TryGetArrayField(TEXT("rounds"), Rounds) && Rounds->Num() == State.Options.Rounds, TEXT("one round recorded"));
		if (Rounds && Rounds->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* Round = nullptr;
			if ((*Rounds)[0]->TryGetObject(Round))
			{
				const FString Winner = (*Round)->GetStringField(TEXT("winner"));
				Check(State, Winner == TEXT("A") || Winner == TEXT("B") || Winner == TEXT("draw"), FString::Printf(TEXT("winner is A / B / draw (%s)"), *Winner));
				const FString Attacker = (*Round)->GetStringField(TEXT("attacker"));
				const FString Reason = (*Round)->GetStringField(TEXT("end_reason"));
				const TSharedPtr<FJsonObject>* RoundSides = nullptr;
				const bool bRoles = (*Round)->TryGetObjectField(TEXT("sides"), RoundSides)
					&& (*RoundSides)->GetObjectField(Attacker)->GetStringField(TEXT("role")) == TEXT("attacker")
					&& (*RoundSides)->GetObjectField(Attacker == TEXT("A") ? TEXT("B") : TEXT("A"))->GetStringField(TEXT("role")) == TEXT("defender");
				Check(State, bRoles, FString::Printf(TEXT("sides.<X>.role: %s attacks, the other defends"), *Attacker));
				bool bStats = bRoles;
				for (const TCHAR* Field : { TEXT("damage_from_concealment"), TEXT("first_strike"), TEXT("objective_hold_s"), TEXT("median_engagement_m"), TEXT("strikes_unobserved") })
				{
					bStats &= (*RoundSides)->GetObjectField(TEXT("A"))->HasField(Field) && (*RoundSides)->GetObjectField(TEXT("B"))->HasField(Field);
				}
				Check(State, bStats, TEXT("the ambush stats per side (damage_from_concealment, first_strike, objective_hold_s, median_engagement_m)"));
				Check(State, (*Round)->HasField(TEXT("scenario")) && (*Round)->HasField(TEXT("objective")), FString::Printf(TEXT("scenario '%s' + objective recorded"),
					*(*Round)->GetStringField(TEXT("scenario"))));
				Check(State, Reason == TEXT("elimination") || Reason == TEXT("objective") || Reason == TEXT("time_limit"), FString::Printf(TEXT("end_reason %s"), *Reason));
				// fix/arena-rpg-contact-roof: the objective is reached by a full navmesh path from both sides' spawns; the rocket counters recorded.
				if (!State.Results.IsEmpty() && State.Results.Last().bObjective && State.Options.bObjectiveReach)
				{
					const FRoundResult& Placed = State.Results.Last();
					Check(State, Placed.bReachAttacker && Placed.bReachDefender, FString::Printf(TEXT("the objective is reached by a full path from both spawns (attackers %s, defenders %s)"),
						Placed.bReachAttacker ? TEXT("yes") : TEXT("no"), Placed.bReachDefender ? TEXT("yes") : TEXT("no")));
				}
				Check(State, bRoles && (*RoundSides)->GetObjectField(TEXT("A"))->HasField(TEXT("rockets")) && (*RoundSides)->GetObjectField(TEXT("B"))->HasField(TEXT("rocket_bursts_near")),
					TEXT("sides.<X>.rockets / rocket_bursts_near recorded"));
				if (State.Options.bSmokeStalemate && !State.Results.IsEmpty())
				{
					// PmcStalemateSmoke (the designer 2026-11-08): the fight does not freeze.
					const FRoundResult& Flow = State.Results.Last();
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] stalemate smoke flow: %s by %s, contact %s, shots %.1f/min, first contact %.1f s, longest quiet %.1f s, attackers mean %.1f m / closest %.1f m from the point (level-aware), under the point %.1f man-s, orders %.2f per fighter-min, quick re-decisions %d (same order %d), early preempts %d, failed %d, detours %d"),
						*Flow.Winner, *Flow.EndReason, Flow.bContact ? TEXT("yes") : TEXT("no"), Flow.ShotsPerMin, Flow.FirstContactS, Flow.LongestQuietS, Flow.AttackerMeanDistM,
						Flow.AttackerClosestM, Flow.AttackerUnderPointS, Flow.OrdersPerFighterMin, Flow.QuickRedecisions, Flow.QuickSameOrder, Flow.EarlyPreempts, Flow.OrdersFailed, Flow.Detours);
					Check(State, Flow.bContact, TEXT("the sides made contact (shots between them)"));
					Check(State, Flow.LongestQuietS <= MaxQuietSeconds, FString::Printf(TEXT("the fight resumes within %.0f s after every quiet spell (longest %.1f s)"),
						MaxQuietSeconds, Flow.LongestQuietS));
					const float Radius = State.Config.ObjectiveRadiusCm / 100.f;
					const bool bAttackersDown = Flow.EndReason == TEXT("elimination") && Flow.Winner != Flow.Attacker;
					Check(State, bAttackersDown || (Flow.AttackerClosestM >= 0.f && Flow.AttackerClosestM <= 1.5f * Radius),
						FString::Printf(TEXT("the attackers reached the point area (closest %.1f m <= %.1f m, or they were wiped out)"), Flow.AttackerClosestM, 1.5f * Radius));
					Check(State, Flow.EarlyPreempts == 0, FString::Printf(TEXT("no running order replaced inside its minimum hold (%d)"), Flow.EarlyPreempts));
					// One order per minimum hold at most (3 s -> 20 a minute); the pmc-v3 flank flapped at ~120.
					const float MaxChurn = 60.f / FMath::Max(1.f, State.WeightsA.OrderMinHoldSeconds);
					Check(State, Flow.OrdersPerFighterMin <= MaxChurn, FString::Printf(TEXT("decision churn %.2f orders per fighter-minute <= %.0f (one per minimum hold)"),
						Flow.OrdersPerFighterMin, MaxChurn));
					// A re-take after a finished walk / cover is fine now and then; the pmc-v3 loop did it ~120 times a minute per fighter.
					const int32 MaxQuickSame = FMath::Max(2 * State.Options.Squad, FMath::CeilToInt(2 * State.Options.Squad * Flow.Duration / 60.f));
					Check(State, Flow.QuickSameOrder <= MaxQuickSame, FString::Printf(TEXT("the same order re-decided within %.0f s at most %d times (once per fighter-minute; %d)"),
						QuickRedecisionSeconds, MaxQuickSame, Flow.QuickSameOrder));
				}
				if (State.Options.bSmokeCapture)
				{
					// PmcArenaObjectiveSmoke: the defenders were parked - the attackers walked onto the point and held it.
					const TSharedPtr<FJsonObject> Objective = (*Round)->GetObjectField(TEXT("objective"));
					Check(State, Reason == TEXT("objective") && Winner == Attacker && Objective->GetStringField(TEXT("result")) == TEXT("captured"),
						FString::Printf(TEXT("the attackers (%s) captured the point (end_reason %s, winner %s, result %s)"), *Attacker, *Reason, *Winner,
							*Objective->GetStringField(TEXT("result"))));
					const double Hold = bRoles ? (*RoundSides)->GetObjectField(Attacker)->GetNumberField(TEXT("objective_hold_s")) : 0.0;
					Check(State, Hold >= State.Config.HoldSeconds - 0.3f, FString::Printf(TEXT("attacker objective_hold_s %.1f >= hold %.0f s"), Hold, State.Config.HoldSeconds));
					const TSharedPtr<FJsonObject>* RoundStats = nullptr;
					Check(State, (*Round)->TryGetObjectField(TEXT("stats"), RoundStats) && (*RoundStats)->GetObjectField(Attacker)->GetNumberField(TEXT("objective_moves")) >= 1.0,
						TEXT("the attackers' commander gave Objective orders"));
					return;
				}
				Check(State, (*Round)->GetBoolField(TEXT("fight_started")), TEXT("the fight started (the squads met)"));
				Check(State, (*Round)->GetNumberField(TEXT("duration_s")) > 0.0, TEXT("duration recorded"));
				Check(State, (*Round)->HasField(TEXT("highlight_score")), TEXT("highlight_score present"));
				Check(State, (*Round)->HasField(TEXT("seed")) && (*Round)->HasField(TEXT("end_reason")), TEXT("round seed + end_reason (the trainer contract)"));
				const TSharedPtr<FJsonObject>* Sides = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("sides"), Sides) && (*Sides)->HasField(TEXT("A")) && (*Sides)->HasField(TEXT("B"))
					&& (*Sides)->GetObjectField(TEXT("A"))->HasField(TEXT("retreats_survived")) && (*Sides)->GetObjectField(TEXT("B"))->HasField(TEXT("hp_max")),
					TEXT("sides.A / sides.B with the contract's metrics"));
				FString Track;
				TSharedPtr<FJsonObject> TrackJson;
				FString TrackText;
				const FString TrackPath = (*Round)->TryGetStringField(TEXT("track"), Track) ? FPaths::ConvertRelativePathToFull(FPaths::GetPath(Path) / Track) : FString();
				const bool bTrack = !TrackPath.IsEmpty() && FFileHelper::LoadFileToString(TrackText, *TrackPath)
					&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TrackText), TrackJson) && TrackJson.IsValid()
					&& TrackJson->GetStringField(TEXT("schema")) == TEXT("codex.pmc_arena_track/1");
				Check(State, bTrack, FString::Printf(TEXT("the round's track file written (codex.pmc_arena_track/1, relative '%s')"), *Track));
				const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Fighters = nullptr;
				const bool bSampled = bTrack && TrackJson->TryGetArrayField(TEXT("frames"), Frames) && Frames->Num() >= 10
					&& TrackJson->TryGetArrayField(TEXT("fighters"), Fighters) && Fighters->Num() == 2 * State.Options.Squad
					&& TrackJson->TryGetArrayField(TEXT("events"), Events) && Events->Num() >= 1 && TrackJson->HasField(TEXT("dt"));
				Check(State, bSampled, FString::Printf(TEXT("track frames (%d) x fighters (%d), events (%d), dt"), Frames ? Frames->Num() : 0, Fighters ? Fighters->Num() : 0, Events ? Events->Num() : 0));
				const TSharedPtr<FJsonObject>* Stats = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("stats"), Stats) && (*Stats)->HasField(TEXT("A")) && (*Stats)->HasField(TEXT("B")), TEXT("per-side stats present"));
				const TSharedPtr<FJsonObject>* HpLost = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("hp_lost"), HpLost) && ((*HpLost)->GetNumberField(TEXT("A")) + (*HpLost)->GetNumberField(TEXT("B")) > 0.0),
					TEXT("somebody lost health (they fought)"));
			}
		}
		const TSharedPtr<FJsonObject>* Summary = nullptr;
		Check(State, Json->TryGetObjectField(TEXT("summary"), Summary) && (*Summary)->HasField(TEXT("A_wins")), TEXT("summary present"));
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		if (World)
		{
			if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
			{
				Pmc->DestroyTeam(TeamA);
				Pmc->DestroyTeam(TeamB);
			}
			if (UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>(); Projectiles && State.Track.ProjectileHandle.IsValid())
			{
				Projectiles->OnProjectileFinished.Remove(State.Track.ProjectileHandle);
				State.Track.ProjectileHandle.Reset();
			}
			State.OutputPath = WriteJson(State, World);
			if (State.Options.bSmoke)
			{
				ValidateJson(State, State.OutputPath);
			}
		}
		if (State.Options.bSmoke)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		}
		FPlatformMisc::RequestExit(false, TEXT("PmcArena"));
		return false;
	}

	/**
	 * The round's spawn evidence (fix/arena-clip-readability): each fighter's spot, its navmesh path to the objective (or the other side's
	 * spawn) and the straight distance - a path much longer than the straight line = he stands behind a fence / wall from it.
	 */
	void LogSpawnReach(FState& State, UWorld* World)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
		{
			const FVector Goal = State.bRoundObjective ? State.Objective : State.Sides[1 - SideIndex].Spawn;
			for (int32 Index = 0; Index < State.Sides[SideIndex].Members.Num(); ++Index)
			{
				const AOperativeCharacter* Operative = State.Sides[SideIndex].Members[Index].Get();
				if (!Operative)
				{
					continue;
				}
				const FVector Spot = Operative->GetActorLocation();
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d side %s man %d at %s: path to the %s %.0f cm, straight %.0f cm"), State.Round,
					*State.Sides[SideIndex].Label, Index, *Spot.ToCompactString(), State.bRoundObjective ? TEXT("objective") : TEXT("enemy spawn"),
					NavPathLength(World, Nav, Spot, Goal), FVector::Dist(Spot, Goal));
			}
		}
	}

	void StartRound(FState& State, UWorld* World)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Pmc || !Squad)
		{
			return;
		}
		++State.Round;
		// The round's own seed (base + round - 1, the trainer's convention): Rounds=1 Seed=<it> replays this round - its spawns, looks,
		// weapons AND its attacker (by the seed's parity, not the round index).
		const int32 Seed = State.Options.Seed + State.Round - 1;
		State.AreaRejects = 0;
		for (FSide& Side : State.Sides)
		{
			Side.Members.Reset();
		}
		Pmc->ResetTeam(TeamA);
		Pmc->ResetTeam(TeamB);
		// The attacker alternates with the round seed's parity (even = A); task #53: the round's scenario and objective.
		const bool bAAttacks = (Seed % 2) == 0;
		State.bAAttacks = bAAttacks;
		const int32 AttackerIndex = bAAttacks ? 0 : 1;
		const FString ScenarioRequested = State.Options.Scenario < 0 ? FString(TEXT("random")) : FString(PmcCommanderRules::ScenarioName(static_cast<EPmcScenario>(State.Options.Scenario)));
		EPmcScenario Scenario = EPmcScenario::Skirmish;
		if (State.bRandomSpawns)
		{
			Scenario = State.Options.Scenario >= 0 ? static_cast<EPmcScenario>(State.Options.Scenario) : PmcCommanderRules::PickScenario(Seed);
		}
		else if (State.Options.Scenario == static_cast<int32>(EPmcScenario::Blizzard))
		{
			Scenario = EPmcScenario::Blizzard;
		}
		bool bSitePlaced = false;
		if (Scenario == EPmcScenario::Chokepoint || Scenario == EPmcScenario::Street || Scenario == EPmcScenario::Roofs)
		{
			bSitePlaced = PlaceScenarioSite(State, World, Scenario, Seed, AttackerIndex);
			if (!bSitePlaced)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] seed %d: no %s site on this map - a skirmish instead"), Seed, PmcCommanderRules::ScenarioName(Scenario));
				Scenario = EPmcScenario::Skirmish;
			}
		}
		const bool bRandomPair = bSitePlaced || (State.bRandomSpawns && PickRandomSpawns(State, World, Seed));
		State.bRoundObjective = State.Options.bObjective;
		if (State.bRoundObjective && !bSitePlaced)
		{
			State.Objective = PlaceObjectiveNear(State, World, State.Sides[1 - AttackerIndex].Spawn, State.Sides[AttackerIndex].Spawn, Seed);
		}
		// fix/arena-rpg-contact-roof: the objective's reach as placed (always measured; ObjectiveReach=on makes both true when any point is).
		State.bReachAttacker = State.bReachDefender = true;
		if (State.bRoundObjective)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			State.bReachAttacker = HasFullNavPath(World, Nav, State.Sides[AttackerIndex].Spawn, State.Objective);
			State.bReachDefender = HasFullNavPath(World, Nav, State.Sides[1 - AttackerIndex].Spawn, State.Objective);
			const FString Line = FString::Printf(TEXT("[PmcArena] seed %d objective reach: %s at level %d - from the attackers' spawn %s, from the defenders' spawn %s"), Seed,
				*State.Objective.ToCompactString(), LevelOf(State, State.Objective), State.bReachAttacker ? TEXT("full path") : TEXT("NO full path"),
				State.bReachDefender ? TEXT("full path") : TEXT("NO full path"));
			float FloorZ = 0.f;
			FString FloorWhat;
			const bool bFloor = TracePawnFloor(World, State.Objective, FloorZ, FloorWhat);
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] seed %d objective paths: attackers %s; defenders %s; pawn floor %s at z %.0f (%s)"), Seed,
				*DescribeNavPath(World, Nav, State.Sides[AttackerIndex].Spawn, State.Objective), *DescribeNavPath(World, Nav, State.Sides[1 - AttackerIndex].Spawn, State.Objective),
				bFloor ? TEXT("hit") : TEXT("NONE"), FloorZ, *FloorWhat);
			if (State.bReachAttacker && State.bReachDefender)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("%s"), *Line);
			}
			else
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("%s"), *Line);
			}
		}
		State.Clock = FPmcObjectiveClock();
		State.Clock.HoldSeconds = State.Config.HoldSeconds;
		const FString Weather = ApplyWeather(State, World, Scenario == EPmcScenario::Blizzard);
		// Side A: a PMC squad at the start (or at the round's random spot).
		FSide& A = State.Sides[0];
		// The personality before the spawn: its rpg_gunner_share decides the squad's RPG gunner (a trainer gene).
		Pmc->SetTeamWeights(TeamA, State.WeightsA);
		for (AOperativeCharacter* Fighter : Pmc->SpawnSquad(TeamA, A.Spawn, A.Facing, State.Options.Squad, Seed))
		{
			A.Members.Add(Fighter);
		}
		if (bRandomPair)
		{
			PlaceAround(State, World, A, State.bRoundObjective ? State.Objective : State.Sides[1].Spawn);
		}
		Pmc->SetTeamCommanded(TeamA, true, &State.WeightsA);
		// Side B.
		FSide& B = State.Sides[1];
		if (State.Options.Opponent == EOpponent::Pmc)
		{
			Pmc->SetTeamWeights(TeamB, State.WeightsB);
			for (AOperativeCharacter* Fighter : Pmc->SpawnSquad(TeamB, B.Spawn, B.Facing, State.Options.Squad, Seed + 1000))
			{
				B.Members.Add(Fighter);
			}
			if (bRandomPair)
			{
				PlaceAround(State, World, B, State.bRoundObjective ? State.Objective : State.Sides[0].Spawn);
			}
			Pmc->SetTeamCommanded(TeamB, true, &State.WeightsB);
		}
		else
		{
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			const FVector Right = B.Facing.RotateVector(FVector::RightVector);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				Member->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, B.Spawn + Right * ((Index - (Members.Num() - 1) * 0.5f) * 150.f), Member);
				Member->TeleportTo(Spot, B.Facing, false, true);
				B.Members.Add(Member);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			if (State.Options.Opponent == EOpponent::Player)
			{
				Pmc->SetTeamCommanded(PmcTeam::Player, true, &State.WeightsB);
			}
			else if (UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>())
			{
				Bot->StartBot(EBotProfile::Veteran, false, false, Seed);
			}
			else
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] no playtest bot in this build: the player's squad fights on its own fire alone"));
			}
		}
		// fix/arena-no-outer-strip: the round's placement against the play area (every fighter and the objective must be inside it).
		if (State.Config.PlayArea.IsRestricted())
		{
			int32 Outside = 0;
			int32 Placed = 0;
			for (const FSide& Side : State.Sides)
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
				{
					if (const AOperativeCharacter* Operative = Member.Get())
					{
						++Placed;
						Outside += PmcArenaPlayAreaRules::IsInside(State.Config.PlayArea, Operative->GetActorLocation()) ? 0 : 1;
					}
				}
			}
			const bool bObjectiveInside = !State.bRoundObjective || PmcArenaPlayAreaRules::IsInside(State.Config.PlayArea, State.Objective);
			const FString Line = FString::Printf(TEXT("[PmcArena] play area seed %d: %d / %d fighters outside, objective %s, %d points turned down"), Seed, Outside, Placed,
				bObjectiveInside ? TEXT("inside") : TEXT("OUTSIDE"), State.AreaRejects);
			if (Outside > 0 || !bObjectiveInside)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("%s"), *Line);
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("%s"), *Line);
			}
		}
		// The defender waits in ambush, the attacker advances on the objective (task #53; without one: on the defender's spawn).
		const FVector AssaultA = State.bRoundObjective ? State.Objective : B.Spawn;
		const FVector AssaultB = State.bRoundObjective ? State.Objective : A.Spawn;
		Pmc->SetTeamAssault(TeamA, bAAttacks ? &AssaultA : nullptr);
		if (State.Options.Opponent == EOpponent::Pmc)
		{
			Pmc->SetTeamAssault(TeamB, bAAttacks ? nullptr : &AssaultB);
		}
		else if (State.Options.Opponent == EOpponent::Player)
		{
			Pmc->SetTeamAssault(PmcTeam::Player, bAAttacks ? nullptr : &AssaultB);
		}
		if (State.bRoundObjective)
		{
			Pmc->SetTeamObjective(State.Sides[AttackerIndex].TeamId, EPmcRole::Attacker, State.Objective, State.Config.ObjectiveRadiusCm, State.Config.ObjectiveLevelCm);
			Pmc->SetTeamObjective(State.Sides[1 - AttackerIndex].TeamId, EPmcRole::Defender, State.Objective, State.Config.ObjectiveRadiusCm, State.Config.ObjectiveLevelCm);
		}
		// PmcArenaObjectiveSmoke: the defenders out of the fight (parked far away, alive, not commanded) - only the capture can end it.
		if (State.Options.bSmokeCapture)
		{
			FSide& Defenders = State.Sides[1 - AttackerIndex];
			Pmc->SetTeamCommanded(Defenders.TeamId, false);
			for (int32 Index = 0; Index < Defenders.Members.Num(); ++Index)
			{
				if (AOperativeCharacter* Member = Defenders.Members[Index].Get())
				{
					Member->StopOperative();
					Member->SetActorHiddenInGame(true);
					Member->SetActorEnableCollision(false);
					Member->CustomTimeDilation = 0.f;
					Member->SetActorLocation(Member->GetActorLocation() + FVector(100000.f + Index * 500.f, 50000.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
		}
		for (FSide& Side : State.Sides)
		{
			Side.StartHp = SumHp(Side.Members);
		}
		FRoundResult& Result = State.Results.AddDefaulted_GetRef();
		Result.Index = State.Round;
		Result.Seed = Seed;
		Result.Attacker = bAAttacks ? TEXT("A") : TEXT("B");
		Result.Scenario = PmcCommanderRules::ScenarioName(Scenario);
		Result.ScenarioRequested = ScenarioRequested;
		Result.Weather = Weather;
		Result.bObjective = State.bRoundObjective;
		Result.ObjectiveLocation = State.Objective;
		Result.bReachAttacker = State.bReachAttacker;
		Result.bReachDefender = State.bReachDefender;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[Index].Members)
			{
				Result.HpMax[Index] += Member.IsValid() && Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() : 0.f;
			}
		}
		StartTrack(State, World);
		// The record camera: across this round's spawn axis, a cut (no easing from the last round); the team colours on at once.
		State.Framing.YawDeg = PmcArenaViewRules::YawAcross(A.Spawn, B.Spawn);
		State.bHasView = false;
		State.TeamColoured.Reset();
		State.TeamDimmed.Reset();
		ApplyTeamOverlays(State);
		LogSpawnReach(State, World);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d: A %d men at %s vs B %d men at %s, attacker %s, scenario %s (asked %s), weather %s, objective %s at %s (r %.0f m, hold %.0f s)"),
			State.Round, A.Members.Num(), *A.Spawn.ToCompactString(), B.Members.Num(), *B.Spawn.ToCompactString(), *Result.Attacker, *Result.Scenario, *ScenarioRequested, *Weather,
			State.bRoundObjective ? TEXT("on") : TEXT("off"), *State.Objective.ToCompactString(), State.Config.ObjectiveRadiusCm / 100.f, State.Config.HoldSeconds);
	}

	void EndRound(FState& State, UWorld* World, const FPmcRoundOutcome& Outcome)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		FRoundResult& Result = State.Results.Last();
		Result.Duration = State.StageTime;
		UpdateDamage(State);
		const TCHAR* Why = *Outcome.Reason;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FSide& Side = State.Sides[Index];
			Result.Survivors[Index] = CountAlive(Side.Members);
			Result.HpLost[Index] = FMath::Max(0.f, Side.StartHp - SumHp(Side.Members));
			if (const FPmcTeamStats* Stats = Pmc ? Pmc->GetTeamStats(Side.TeamId) : nullptr)
			{
				Result.Stats[Index] = *Stats;
				Result.Kills[Index] = Stats->Kills;
			}
		}
		Result.bFightStarted = Pmc && (Pmc->HasTeamContact(TeamA) || Pmc->HasTeamContact(State.Sides[1].TeamId));
		Result.Winner = Outcome.Winner;
		Result.ObjectiveResult = Outcome.ObjectiveResult;
		Result.AttackerHeld = State.Clock.Held;
		SampleTrack(State, World, TrackInterval); // the last sample
		Result.EndReason = Outcome.Reason;
		const int32 AttackerIndex = State.bAAttacks ? 0 : 1;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Result.DamageDealt[Index] = State.Track.Damage[Index];
			Result.DamageFromConcealment[Index] = State.Track.Concealed[Index];
			Result.FirstStrike[Index] = State.Track.FirstStrikeSide == Index ? 1 : 0;
			Result.ObjectiveHoldS[Index] = State.bRoundObjective ? State.Clock.ControlSeconds[Index == AttackerIndex ? 0 : 1] : 0.f;
			Result.MedianEngagementM[Index] = Median(State.Track.ShotDistM[Index]);
			Result.Shots[Index] = State.Track.ShotDistM[Index].Num();
			Result.StrikesUnobserved[Index] = State.Track.StrikesUnobserved[Index];
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Track.Retreated)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			if (Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive())
			{
				++Result.RetreatsSurvived[State.Sides[0].Members.Contains(Operative) ? 0 : 1];
			}
		}
		// The fight flow (fix/pmc-arena-stalemate).
		{
			const FTrack& Track = State.Track;
			const float Minutes = FMath::Max(Result.Duration, 1.f) / 60.f;
			// fix/arena-rpg-contact-roof: contact + quiet spells from every contact moment (shots, rockets, rocket bursts near an enemy).
			const FPmcArenaFlow Flow = PmcArenaRoundRules::ComputeFlow(ContactTimesOf(State), Result.Duration);
			Result.bContact = Flow.bContact;
			Result.ShotsPerMin = Flow.EventsPerMin;
			Result.FirstContactS = Flow.FirstContactS;
			Result.LongestQuietS = Flow.LongestQuietS;
			Result.ContactEvents = Flow.Events;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Result.Rockets[Index] = Track.Rockets[Index];
				Result.RocketBurstsNear[Index] = Track.RocketBurstsNear[Index];
			}
			Result.AttackerMeanDistM = Track.AttackerDistSamples > 0 ? static_cast<float>(Track.AttackerDistSum / Track.AttackerDistSamples) / 100.f : -1.f;
			Result.AttackerClosestM = Track.AttackerClosestCm >= 0.f ? Track.AttackerClosestCm / 100.f : -1.f;
			Result.AttackerUnderPointS = Track.AttackerUnderPointS;
			Result.OrdersPerFighterMin = Track.Orders / FMath::Max(1, Track.Fighters.Num()) / Minutes;
			Result.QuickRedecisions = Track.QuickRedecisions;
			Result.QuickSameOrder = Track.QuickSameOrder;
			Result.EarlyPreempts = Track.EarlyPreempts;
			Result.OrdersFailed = Result.Stats[0].OrdersFailed + Result.Stats[1].OrdersFailed;
			Result.Detours = Result.Stats[0].Detours + Result.Stats[1].Detours;
			Result.Resupplies = Track.Resupplies;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d contact: %s (%d moments: %d shots, rockets A %d / B %d, bursts near an enemy A %d / B %d, rocket contact %s), objective reach attackers %s / defenders %s"),
			State.Round, Result.bContact ? TEXT("yes") : TEXT("no"), Result.ContactEvents, State.Track.ShotTimes.Num(), Result.Rockets[0], Result.Rockets[1],
			Result.RocketBurstsNear[0], Result.RocketBurstsNear[1], State.Options.bRocketContact ? TEXT("on") : TEXT("off"),
			Result.bReachAttacker ? TEXT("yes") : TEXT("NO"), Result.bReachDefender ? TEXT("yes") : TEXT("NO"));
		if (Result.bObjective)
		{
			// Where everybody ended against the point, in 3D (a man under a roof point is 2D-close but a level below).
			FString Ends;
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[SideIndex].Members)
				{
					if (const AOperativeCharacter* Operative = Member.Get())
					{
						const FVector Delta = FeetOf(*Operative) - State.Objective;
						Ends += FString::Printf(TEXT(" %s:%.0fm/dz%.0fm"), *State.Sides[SideIndex].Label, Delta.Size2D() / 100.f, Delta.Z / 100.f);
					}
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d ends vs the objective (2D m / feet dZ m):%s"), State.Round, *Ends);
		}
		Result.LeadSwings = State.Track.LeadSwings;
		Result.HighlightScore = HighlightScoreOf(Result);
		Result.TrackFile = WriteTrack(State, World, Result);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d over (%s): winner %s, survivors A %d / B %d, hp lost A %.0f / B %.0f, kills A %d / B %d, flanks A %d/%d B %d/%d, suppression A %.1fs B %.1fs, heals A %d B %d, retreats A %d B %d, stuck A %d B %d, highlight %.1f -> %s"),
			State.Round, Why, *Result.Winner, Result.Survivors[0], Result.Survivors[1], Result.HpLost[0], Result.HpLost[1], Result.Kills[0], Result.Kills[1],
			Result.Stats[0].FlanksAttempted, Result.Stats[0].FlanksSucceeded, Result.Stats[1].FlanksAttempted, Result.Stats[1].FlanksSucceeded,
			Result.Stats[0].SuppressionSeconds, Result.Stats[1].SuppressionSeconds, Result.Stats[0].Heals, Result.Stats[1].Heals, Result.Stats[0].Retreats, Result.Stats[1].Retreats,
			Result.Stats[0].StuckEvents, Result.Stats[1].StuckEvents, Result.HighlightScore, *Result.TrackFile);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d objective: scenario %s, attacker %s, end %s, objective %s (clock %.1f s), damage A %.0f (concealed %.0f) / B %.0f (concealed %.0f), first strike %s, hold A %.1f s / B %.1f s, median engagement A %.1f m / B %.1f m, unobserved strikes A %d / B %d"),
			State.Round, *Result.Scenario, *Result.Attacker, *Result.EndReason, *Result.ObjectiveResult, Result.AttackerHeld, Result.DamageDealt[0], Result.DamageFromConcealment[0],
			Result.DamageDealt[1], Result.DamageFromConcealment[1], Result.FirstStrike[0] ? TEXT("A") : (Result.FirstStrike[1] ? TEXT("B") : TEXT("-")), Result.ObjectiveHoldS[0],
			Result.ObjectiveHoldS[1], Result.MedianEngagementM[0], Result.MedianEngagementM[1], Result.StrikesUnobserved[0], Result.StrikesUnobserved[1]);
		if (Pmc)
		{
			for (const int32 Team : { TeamA, TeamB, PmcTeam::Player })
			{
				Pmc->SetTeamObjective(Team, EPmcRole::None, FVector::ZeroVector, 0.f);
			}
			Pmc->SetTeamCommanded(TeamA, false);
			Pmc->SetTeamCommanded(TeamB, false);
			Pmc->SetTeamCommanded(PmcTeam::Player, false);
			Pmc->SetTeamAssault(PmcTeam::Player, nullptr);
			Pmc->DestroyTeam(TeamA);
			Pmc->DestroyTeam(TeamB);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		switch (State.Stage)
		{
		case 0: // the level settled
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Pmc)
			{
				Check(State, false, TEXT("UPmcSquadSubsystem present"));
				return Finish(State, World, false);
			}
			FVector Start;
			FRotator Facing;
			if (!FindStart(World, Start, Facing))
			{
				Check(State, false, TEXT("a start spot (the squad / a PlayerStart)"));
				return Finish(State, World, false);
			}
			// The arena data first: the far spot (the fixed spawns' B) keeps to the play area too.
			State.Config = LoadConfig(World);
			if (!State.Options.bPlayArea && State.Config.PlayArea.IsRestricted())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] PlayArea=off: the level's play area (%s) is ignored"), *PmcArenaPlayAreaRules::Describe(State.Config.PlayArea));
				State.Config.PlayArea = FPmcArenaPlayArea();
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] play area: %s (%s)"), *PmcArenaPlayAreaRules::Describe(State.Config.PlayArea), *State.Config.Source);
			FVector Far;
			bool bFar = FindFarSpot(World, Start, Facing.Yaw, State.Options.DistanceCm, Far, &State.Config.PlayArea);
			if (!bFar && State.Config.PlayArea.IsRestricted())
			{
				bFar = FindFarSpot(World, Start, Facing.Yaw, State.Options.DistanceCm, Far);
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] no far spot inside the play area - %s"), bFar ? TEXT("one outside it") : TEXT("none at all"));
			}
			if (!bFar)
			{
				Check(State, false, TEXT("a far spot with a navmesh path from the start"));
				return Finish(State, World, false);
			}
			State.Sides[0].TeamId = TeamA;
			State.Sides[0].Label = TEXT("A");
			State.Sides[0].Spawn = Start;
			State.Sides[0].Facing = (Far - Start).GetSafeNormal2D().Rotation();
			State.Sides[1].TeamId = State.Options.Opponent == EOpponent::Pmc ? TeamB : PmcTeam::Player;
			State.Sides[1].Label = TEXT("B");
			State.Sides[1].Spawn = Far;
			State.Sides[1].Facing = (Start - Far).GetSafeNormal2D().Rotation();
			// Spawns=Random (the default off the combat lab): seeded spawn pairs per round, height levels over the ground under the start.
			State.bRandomSpawns = State.Options.SpawnMode == 2 || (State.Options.SpawnMode == 0 && !World->GetMapName().Contains(TEXT("FormationLabCombat")));
			State.GroundZ = static_cast<float>(Start.Z) - 92.f;
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				FNavLocation OnNav;
				if (Nav->ProjectPointToNavigation(Start, OnNav, FVector(200.f, 200.f, 300.f)))
				{
					State.GroundZ = static_cast<float>(OnNav.Location.Z);
				}
			}
			if (State.Options.Opponent == EOpponent::Pmc)
			{
				ParkPlayerSquad(State, World);
			}
			else if (State.Options.Rounds > 1)
			{
				State.bRoundsLimited = true;
				State.Options.Rounds = 1;
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] opponent=%s: one round only (the player's fallen cannot be revived)"), OpponentName(State.Options.Opponent));
			}
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->SetCombatStartOverride(ECombatStartMode::Ambush);
			}
			// The arena is watched from above: the fog of war on the hostile fighters (2026-11-03) is off (rendering only; the teams'
			// own sight still decides who shoots whom).
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->SetRevealHostileOperatives(true);
			}
			// The lab's immortality / infinite ammo cheats are the player squad's level defaults: off for a real fight.
			for (const TCHAR* Name : { TEXT("Codex.Cheat.Immortal"), TEXT("Codex.Cheat.InfiniteAmmo") })
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Variable->Set(0, ECVF_SetByConsole);
				}
			}
			// Game designer 2026-11-09: the horde is ON at the Depot (L_Gen_Depot_1.json) - never in the arena / the night trainer, even when the
			// arena runs on the Depot map without -LevelJson=L_PmcArenaDepot.json (horde off there too): no firefight horde in self-play.
			if (IConsoleVariable* HordeOn = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Horde.Enabled")))
			{
				HordeOn->Set(0, ECVF_SetByConsole);
			}
			if (UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>())
			{
				Horde->RefreshConfig();
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] the horde is %s in the arena (Codex.Horde.Enabled 0)"), Horde->GetConfig().bEnabled ? TEXT("STILL ON") : TEXT("off"));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] objective %s: radius %.0f m, hold %.0f s, offset %.2f (%s); scenario %s"), State.Options.bObjective ? TEXT("on") : TEXT("off"),
				State.Config.ObjectiveRadiusCm / 100.f, State.Config.HoldSeconds, State.Config.ObjectiveOffsetFrac, *State.Config.Source,
				State.Options.Scenario < 0 ? TEXT("random") : PmcCommanderRules::ScenarioName(static_cast<EPmcScenario>(State.Options.Scenario)));
			State.Started = FDateTime::Now();
			State.StartedUtc = FDateTime::UtcNow();
			State.RunName = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + (State.Options.bRecord ? TEXT("-record") : TEXT(""));
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] %s: %d round(s), seed %d, opponent %s, weights A '%s' (%s) B '%s' (%s), time limit %.0f s, start %s, far spot %s (%.0f cm)"),
				*World->GetMapName(), State.Options.Rounds, State.Options.Seed, OpponentName(State.Options.Opponent), *State.WeightsA.Name,
				State.bWeightsALoaded ? TEXT("file") : TEXT("defaults"), *State.WeightsB.Name, State.bWeightsBLoaded ? TEXT("file") : TEXT("defaults"), State.Options.TimeLimit,
				*Start.ToCompactString(), *Far.ToCompactString(), FVector::Dist2D(Start, Far));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1: // a round starts
			StartRound(State, World);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		case 2: // the fight
		{
			SampleTrack(State, World, StepSeconds);
			ApplyTeamOverlays(State);
			// record=1: the camera follows every rendered frame (RunWith's frame ticker), not this 10 Hz step.
			if (State.Options.bShots && State.Round == 1)
			{
				AimCamera(State, World);
				if ((State.ShotsTaken == 0 && State.StageTime >= 6.f) || (State.ShotsTaken == 1 && State.StageTime >= 11.f) || (State.ShotsTaken == 2 && State.StageTime >= 18.f))
				{
					Shot(State, World, State.ShotsTaken == 0 ? TEXT("contact") : (State.ShotsTaken == 1 ? TEXT("midfight") : TEXT("late")));
				}
			}
			UpdateDamage(State);
			ResupplyAmmo(State);
			UpdateObjective(State, World);
			const FPmcRoundOutcome Outcome = PmcCommanderRules::ResolveRound(CountAlive(State.Sides[0].Members), CountAlive(State.Sides[1].Members),
				State.StageTime >= State.Options.TimeLimit, State.bRoundObjective, State.Clock, State.bAAttacks,
				!State.Track.ShotTimes.IsEmpty() || (State.Options.bRocketContact && !State.Track.RocketBurstTimes.IsEmpty()));
			if (!Outcome.bOver)
			{
				return true;
			}
			EndRound(State, World, Outcome);
			State.Stage = State.Round >= State.Options.Rounds ? 4 : 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3: // a breather between rounds
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		case 4:
		default:
			if (State.StageTime < 0.5f)
			{
				return true; // the last screenshot request lands
			}
			return Finish(State, World, true);
		}
	}

	void RunWith(const TArray<FString>& Args, UWorld* World, bool bSmoke, bool bSmokeCapture = false, bool bSmokeStalemate = false)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Options = Parse(Args);
		State->Options.bSmoke |= bSmoke;
		if (bSmoke)
		{
			State->Options.Rounds = 1;
			State->Options.Opponent = EOpponent::Pmc;
			State->Options.TimeLimit = FMath::Min(State->Options.TimeLimit, 90.f);
		}
		if (bSmokeStalemate)
		{
			// A real objective round (180 s), long enough for quiet spells and the push (30 s) to show.
			State->Options.bSmokeStalemate = true;
			State->Options.bObjective = true;
			State->Options.Squad = 3;
			State->Options.TimeLimit = 180.f;
		}
		if (bSmokeCapture)
		{
			// Side A attacks (an even round seed); the objective on, the defenders parked.
			State->Options.bSmokeCapture = true;
			State->Options.bObjective = true;
			State->Options.Seed += (State->Options.Seed % 2 == 0) ? 0 : 1;
		}
		State->bWeightsALoaded = PmcCommanderRules::LoadWeightsFile(State->Options.WeightsA, State->WeightsA);
		State->bWeightsBLoaded = PmcCommanderRules::LoadWeightsFile(State->Options.WeightsB, State->WeightsB);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
		if (State->Options.bRecord)
		{
			// The record camera eases every frame (the 10 Hz step made it jump in a 30 fps clip); a new round cuts (StartRound).
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaSeconds)
			{
				UWorld* World = WeakWorld.Get();
				if (!World)
				{
					return false;
				}
				if (State->Stage == 2)
				{
					AimCamera(*State, World, DeltaSeconds, false);
				}
				return true;
			}));
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, false);
	}

	void RunSmoke(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true);
	}

	void RunObjectiveSmoke(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true, true);
	}

	void RunStalemateSmoke(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true, false, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcArena"),
		TEXT("PMC self-play arena: rounds=N seed=S weightsA=<file> weightsB=<file> opponent=pmc|player|bot time=<s> squad=<n> distance=<cm> shot=0|1 objective=0|1 scenario=random|skirmish|chokepoint|street|roofs|blizzard; writes Saved/Arena/<timestamp>.json, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	static FAutoConsoleCommandWithWorldAndArgs SmokeCommand(
		TEXT("CodexTactics.PmcArenaSmoke"),
		TEXT("Dev check: one PMC-vs-PMC arena round on this map and the result JSON validated; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmoke));

	static FAutoConsoleCommandWithWorldAndArgs ObjectiveSmokeCommand(
		TEXT("CodexTactics.PmcArenaObjectiveSmoke"),
		TEXT("Dev check (task #53): one arena round with the defenders parked out of the fight - the attackers must walk onto the objective and capture it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunObjectiveSmoke));

	static FAutoConsoleCommandWithWorldAndArgs StalemateSmokeCommand(
		TEXT("CodexTactics.PmcStalemateSmoke"),
		TEXT("Dev check (fix/pmc-arena-stalemate): one real 3v3 objective round (Depot) - contact resumes within 45 s after every quiet spell, the attackers reach the point area, no order replaced inside its minimum hold, decision churn measured; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunStalemateSmoke));
}

#endif // !UE_BUILD_SHIPPING
