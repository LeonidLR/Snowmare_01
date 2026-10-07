#pragma once

#include "CoreMinimal.h"
#include "AI/PerceptionRules.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeMovementRules.h"

/**
 * Stealth decisions of the playtest bot on ambush / patrol levels (user plan 2026-10-07, UE-only, no Godot reference —
 * the Godot bot never sneaked). Heuristic, deterministic for a seed, deliberately not perfect: the bot is a stealth-ish
 * opponent for tuning the patrol perception (Content/Data/AI/enemy_perception.json, Codex.Perception.* knobs) with the
 * Jev AI coach. It estimates the patrols' detection risk with the same PerceptionRules the enemies use, picks the
 * fastest gait that stays under it, sneaks up on a patrol and decides when to strike first (from cover / unseen) rather
 * than be detected; during a trap search it keeps still. Pure rules, tested in CodexTactics.Bot.Stealth.*;
 * UPlaytestBotSubsystem applies them. Distances in cm.
 */

/** One patrolling enemy as the bot judges it. */
struct CODEXTACTICS_API FBotPatrolView
{
	FVector Location = FVector::ZeroVector;
	/** Planar facing of the body. */
	FVector Forward = FVector::ForwardVector;
	/** Its perception in force (x the search multiplier while searching). */
	FEnemyPerceptionParams Params;
	/** Suspicion meter 0..1 (1 = detected). */
	float Suspicion = 0.f;
	bool bSearching = false;
};

/** Tunables of the stealth bot; the seeded ones vary per run (MakeSeededConfig). */
struct CODEXTACTICS_API FBotStealthConfig
{
	/** Sight is judged with the range x this and the half-angle + FovMarginDeg (patrols turn). */
	float SightMargin = 1.15f;
	float FovMarginDeg = 20.f;
	/** Footsteps are judged with the hearing radius x this. */
	float HearingMargin = 1.35f;
	/** Strike when the target is within this x the rifle range. Seeded 0.55..0.9. */
	float AmbushRangeFraction = 0.75f;
	/** A patrol's suspicion at / above this: strike now when in rifle range, else hide. Seeded 0.35..0.65. */
	float AlarmSuspicion = 0.5f;
	/** World seconds of sneaking before the bot strikes from wherever it is (no endless stalking). */
	float MaxStealthSeconds = 240.f;
	/**
	 * Patience: before this many world seconds the bot does not spring a planned ambush (it explores, holds in cover and
	 * dodges noses — the patrols get their chance to notice it); only a pre-emptive strike or a search contact. Seeded
	 * 20..120 s; 0 = strike as soon as in position.
	 */
	float MinSneakSeconds = 0.f;
	/** The approach point is turned this far round the target (seeded -60..60 deg: runs come in from different sides). */
	float ApproachAngleDeg = 0.f;
	/** Lay a mine on the patrol route first and wait for the search (when the squad carries one). Seeded. */
	bool bUseTrap = false;
	/** Seconds the bot waits for a laid trap to go off before it goes on. */
	float TrapWaitSeconds = 60.f;
};

/** What the stealth bot does this decision. */
enum class EBotStealthAction : uint8
{
	/** Move on (explore / approach the target patrol) at the chosen gait. */
	Sneak,
	/** In ambush range but exposed: take cover / go prone and wait for the moment. */
	Hold,
	/** Keep still (prone / in cover): a patrol grows suspicious out of reach, or a search is on. */
	Hide,
	/** Back away from a hound about to smell the squad (no stance hides from a nose) while still patient. */
	Evade,
	/** Strike first: attack order on the target (starts the ambush fight). */
	Ambush
};

/** Why the bot strikes (logs, coach telemetry). */
enum class EBotAmbushReason : uint8
{
	None,
	/** In range, in cover or unseen: the planned ambush. */
	InPosition,
	/** A patrol is about to detect the squad and it is within rifle range: strike before it does. */
	PreEmptive,
	/** A searching patrol came within range of the hidden squad. */
	SearchContact,
	/** Sneaking took too long (MaxStealthSeconds). */
	Forced
};

/** Inputs of one stealth decision. */
struct CODEXTACTICS_API FBotStealthInput
{
	/** A living patrol target exists. */
	bool bHasTarget = false;
	/** Leader -> nearest (target) patrol enemy, cm. */
	float TargetDistanceCm = TNumericLimits<float>::Max();
	/** Leader -> nearest searching patrol enemy, cm (Max when none searches). */
	float SearcherDistanceCm = TNumericLimits<float>::Max();
	/** Rifle reach, cm. */
	float RifleRangeCm = 1400.f;
	/** Highest suspicion of any patrol enemy. */
	float MaxSuspicion = 0.f;
	/** Distance to the patrol enemy with that suspicion, cm. */
	float SuspiciousDistanceCm = TNumericLimits<float>::Max();
	/** No patrol could see any operative now (sight risk < 1 for every pair). */
	bool bSquadUnseen = true;
	/** The leader is in a cover slot (or behind a barricade). */
	bool bLeaderInCover = false;
	/** World seconds since the sneaking began. */
	float ElapsedSeconds = 0.f;
	/** A laid trap is still pending (the bot waits for it). */
	bool bTrapPending = false;
	/**
	 * Strike from at least this far (cm): a frost hound smells everyone within its smell radius whatever the stance, so
	 * the ambush range grows to its smell reach (capped at the rifle range). 0: no smelling patrol.
	 */
	float MinAmbushRangeCm = 0.f;
	/** A smelling patrol enemy is about to smell an operative (within its smell reach): strike now or back off. */
	bool bSmellImminent = false;
};

namespace BotStealthRules
{
	/**
	 * Sight risk of a target in Stance to Observer: (stance range x SightMargin) / distance when it lies in the widened
	 * field of view (or within the proximity radius) and the line is clear, else 0. >= 1: inside the danger zone.
	 */
	CODEXTACTICS_API float SightRisk(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target,
		EOperativeStance Stance, bool bLineClear, const FBotStealthConfig& Config);

	/** Hearing risk of a gait: (hearing radius x HearingMargin) / distance (0 for standing still). >= 1: it would be heard. */
	CODEXTACTICS_API float HearingRisk(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm, const FBotStealthConfig& Config);

	/** The moving gait of a stance (Standing walks, Crouching crouch-walks, Prone crawls). */
	CODEXTACTICS_API ESquadMovementNoise MovingNoise(EOperativeStance Stance);

	/**
	 * Highest risk (sight or hearing) of an operative moving at Position in Stance over all patrols (bLineClear[i]: the
	 * eye line of patrol i is clear; a missing entry counts as clear).
	 */
	CODEXTACTICS_API float MovementRisk(const TArray<FBotPatrolView>& Patrols, const TArray<bool>& LineClear, const FVector& Position,
		EOperativeStance Stance, const FBotStealthConfig& Config);

	/** The fastest stance whose movement risk stays below 1 (Standing, then Crouching, then Prone; Prone when none does). */
	CODEXTACTICS_API EOperativeStance ChooseSneakStance(const TArray<FBotPatrolView>& Patrols, const TArray<bool>& LineClear, const FVector& Position,
		const FBotStealthConfig& Config);

	/** The decision (see EBotStealthAction); OutReason says why when it is Ambush. */
	CODEXTACTICS_API EBotStealthAction Decide(const FBotStealthInput& In, const FBotStealthConfig& Config, EBotAmbushReason& OutReason);

	/** Range the bot strikes from: rifle range x AmbushRangeFraction, at least MinAmbushRangeCm, at most the rifle range. */
	CODEXTACTICS_API float AmbushRange(const FBotStealthInput& In, const FBotStealthConfig& Config);

	/** Smell reach of the patrols (largest smell radius x HearingMargin, cm; 0 when none smells). */
	CODEXTACTICS_API float SmellReach(const TArray<FBotPatrolView>& Patrols, const FBotStealthConfig& Config);

	/**
	 * Where to sneak to strike Patrol: StandoffCm from it on the side away from its facing (behind it), turned towards the
	 * squad's side so the approach does not cross its view. Planar; Z of the patrol.
	 */
	CODEXTACTICS_API FVector ApproachPoint(const FVector& Patrol, const FVector& PatrolForward, const FVector& Squad, float StandoffCm);

	/** Fire posture while sneaking (Passive: nobody opens fire and gives the squad away) or once the fight is on (Aggressive). */
	CODEXTACTICS_API ESquadFirePosture PostureFor(bool bFightStarted);

	/** Per-run variation from the seed (the same seed gives the same config). */
	CODEXTACTICS_API FBotStealthConfig MakeSeededConfig(int32 Seed, bool bHasTrap);

	CODEXTACTICS_API const TCHAR* ActionName(EBotStealthAction Action);
	CODEXTACTICS_API const TCHAR* ReasonName(EBotAmbushReason Reason);
	CODEXTACTICS_API const TCHAR* StanceName(EOperativeStance Stance);
}
