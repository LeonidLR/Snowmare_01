#pragma once

#include "CoreMinimal.h"

/**
 * Under fire (bug #43, user PIE on Depot 2026-11-07, UE-only; Godot had no shooting enemies of this kind): "the squad in a column
 * in a narrow passage is fired at by a PMC from a corner at the passage end; the squad (AGGRESSIVE) does not react until it is out
 * of the passage". The shooter WAS known to the squad (his muzzle flash demasks him within earshot, UPmcSquadSubsystem::UpdateSight),
 * but his body stood behind the corner: no clear line to it, so no combat target - no turn (a moving man faced his path) and no
 * shot back.
 *
 * Every round fired at an operative by a hostile operative now puts the shooter's muzzle flash point on the target and on every mate
 * of his team within ShareRadiusCm of him (they hear the rounds come in) for MemorySeconds:
 *  - the target's team learns the shooter (UPmcSquadSubsystem::NotifyShotAt: seen for SightRules::DemaskSeconds whatever the range);
 *  - each of them without a combat target turns to the flash - standing or walking (the 8-direction locomotion strafes), not in
 *    cover, not sprinting / carrying / panicking, never against a facing the player ordered, never on the turn-based grid;
 *  - with no line to the shooter's body he returns fire at the flash itself (the part of the shooter round the corner) - a
 *    suppressive, blind-factor round (SightRules::BlindFireAccuracyMultiplier, a hit only while the shooter stays at the spot),
 *    when his fire posture lets him open fire on his own and a line from his muzzle to the flash is clear (a rear man of the
 *    column without one only turns).
 *
 * The manoeuvre under fire (#43 rest, 2026-11-07; AOperativeCharacter::UpdateUnderFireManoeuvre, every DecisionSeconds), for the squad
 * and the hostile PMC fighters alike - an operative under fire, not in cover, with no order of his own running:
 *  - a cover slot within CoverSeekRadiusCm whose wall shields him from the flash (PmcCommanderRules::CoverQualityAgainst >=
 *    CoverMinQuality) -> he takes the nearest one (a sprint into it);
 *  - else, with no clear line to the flash (a wall corner) or a mate in it, a short step off the line (SidestepOffsets: up to
 *    SidestepMaxCm either side, on the navmesh - inside the passage) to the nearest spot with a clear line and no mate in it;
 *  - else prone (ProneWhenNoCover), crouching again once the threat is over.
 * Never over a player's explicit move order (any own move order / pending cover / fire order / facing order); a formation follower waits
 * ReactionSeconds (the leader's slot logic first) and does nothing while the formation walks; the formation then leaves him on his
 * new spot until the threat is over or the leader gets a new order (USquadSubsystem::IsOnOwnOrder). Tuning: Content/Data/Combat/under_fire.json.
 */
struct FUnderFireConfig
{
	/** How long a round at him keeps the flash point as his threat, s. "memory_seconds" */
	float MemorySeconds = 3.f;
	/** Mates within this distance of the target share the threat, cm. "share_radius_cm" */
	float ShareRadiusCm = 2500.f;
	/** 1 = the manoeuvre under fire runs, 0 = only the turn / the return fire (A/B). "manoeuvre.enabled" */
	float ManoeuvreEnabled = 1.f;
	/** Seconds between his manoeuvre decisions. "manoeuvre.decision_seconds" */
	float DecisionSeconds = 0.3f;
	/** A formation follower waits this long after the first round before he acts (the leader's slot logic first), s. "manoeuvre.reaction_seconds" */
	float ReactionSeconds = 0.6f;
	/** He holds the spot he manoeuvred to (the formation leaves him there) this long after the last round, s. "manoeuvre.hold_seconds" */
	float HoldSeconds = 4.f;
	/** Cover slots this far around him are tried, cm. "cover.radius_cm" */
	float CoverSeekRadiusCm = 800.f;
	/** A slot shields him enough when its quality against the flash is at least this (0..1). "cover.min_quality" */
	float CoverMinQuality = 0.35f;
	/** A cover search that found nothing is not repeated sooner, s. "cover.retry_seconds" */
	float CoverRetrySeconds = 2.f;
	/** The longest step off the line, cm. "sidestep.max_cm" */
	float SidestepMaxCm = 200.f;
	/** The step candidates' spacing, cm (both sides, nearest first). "sidestep.step_cm" */
	float SidestepStepCm = 50.f;
	/** A mate this close to his line to the flash blocks it, cm. "sidestep.mate_block_radius_cm" */
	float MateBlockRadiusCm = 60.f;
	/** After a step he does not step again for this long, s. "sidestep.cooldown_seconds" */
	float SidestepCooldownSeconds = 2.5f;
	/** 1 = with no cover nearby he goes prone (crouching again when the threat is over), 0 = he stays in his stance. "prone.when_no_cover" */
	float ProneWhenNoCover = 1.f;
};

/** What the manoeuvre under fire does now (UnderFireRules::ChooseManoeuvre). */
enum class EUnderFireManoeuvre : uint8
{
	None,
	/** A sprint into the nearest cover slot that shields him from the flash. */
	TakeCover,
	/** A short step off the line to a spot with a clear line to the flash. */
	Sidestep,
	/** No cover nearby: down on the ground. */
	GoProne
};

namespace UnderFireRules
{
	/** The values in force (Content/Data/Combat/under_fire.json, loaded on the first call; the struct defaults mirror the file). */
	CODEXTACTICS_API const FUnderFireConfig& GetConfig();
	/** Replaces the values in force (tests / smokes). */
	CODEXTACTICS_API void SetConfig(const FUnderFireConfig& Config);
	/** Reads the file again (the game mode at start); false when missing / unreadable (the values in force are kept). */
	CODEXTACTICS_API bool ReloadConfig();
	CODEXTACTICS_API FString GetDefaultPath();
	/** The file's "params" keys. */
	CODEXTACTICS_API TArray<FString> KnownKeys();
	/** The member of Key (KnownKeys); false for an unknown key. */
	CODEXTACTICS_API bool GetValue(const FUnderFireConfig& Config, const FString& Key, float& OutValue);
	/** Applies the "params" object of JsonText onto InOut (unknown / non-numeric keys listed in OutUnknown); false without one. */
	CODEXTACTICS_API bool ApplyJson(const FString& JsonText, FUnderFireConfig& InOut, TArray<FString>* OutUnknown = nullptr);

	/** The flash point is still his threat. */
	CODEXTACTICS_API bool IsThreatActive(double Now, double LastShotAtTime, float MemorySeconds);

	/** A mate at this distance from the target shares the threat (the target himself always does). */
	CODEXTACTICS_API bool SharesThreat(bool bIsTarget, float DistanceToTargetCm, float ShareRadiusCm);

	struct FFaceFacts
	{
		bool bRealTimeFight = false;
		bool bAlive = false;
		bool bThreatActive = false;
		/** A combat target already turns him. */
		bool bHasCombatTarget = false;
		bool bInCover = false;
		bool bSprinting = false;
		bool bCarrying = false;
		bool bPanicking = false;
		/** A quick fire / SUPPRESS order turns him itself. */
		bool bFireOrder = false;
		/** A facing the player ordered (facing point / order facing). */
		bool bExplicitFacing = false;
		bool bGridOwnsFacing = false;
	};

	/** He turns to the flash point now (moving or not). */
	CODEXTACTICS_API bool ShouldFaceThreat(const FFaceFacts& Facts);

	/** He returns fire at the flash: no visible target to shoot, the posture allows his own fire, a clear line to the flash, in reach. */
	CODEXTACTICS_API bool ShouldReturnFire(bool bThreatActive, bool bHasDirectTarget, bool bMayAutoFire, bool bLineToFlash, bool bInReach);

	/** The gate of the manoeuvre under fire (cheap facts; the searches run only when it passes). */
	struct FManoeuvreFacts
	{
		bool bRealTimeFight = false;
		bool bAlive = false;
		bool bThreatActive = false;
		/** Since the first round of this spell under fire, s. */
		float SecondsUnderFire = 0.f;
		bool bInCover = false;
		/** On his way into a cover slot already. */
		bool bCoverPending = false;
		/** An order of his own runs: a move order (player, autonomy, the PMC commander), a use / object task, a group cover wait. */
		bool bOwnOrder = false;
		/** A quick fire / SUPPRESS order, or a facing the player ordered. */
		bool bFireOrFacingOrder = false;
		bool bCarrying = false;
		bool bPanicking = false;
		bool bGridOwnsFacing = false;
		/** A follower of the player's formation (not Solo, not the leader). */
		bool bFormationFollower = false;
		/** ... and the formation walks (the leader on a move order / moving). */
		bool bFormationWalking = false;
		/** Commander Mode runs the squad's own cover decisions (USquadAutonomySubsystem). */
		bool bAutonomyOwnsCover = false;
	};

	/** He may manoeuvre now (then the cover / sidestep searches run and ChooseManoeuvre picks). */
	CODEXTACTICS_API bool MayManoeuvre(const FUnderFireConfig& Config, const FManoeuvreFacts& Facts);

	/** What the searches found. */
	struct FManoeuvreOptions
	{
		bool bCoverFound = false;
		/** No clear line from his muzzle to the flash, or a mate stands in it. */
		bool bWantsLine = false;
		bool bSidestepFound = false;
		bool bSidestepCoolingDown = false;
		bool bProne = false;
	};

	/** Cover first, then a step for a line, then prone (ProneWhenNoCover); None when nothing helps. */
	CODEXTACTICS_API EUnderFireManoeuvre ChooseManoeuvre(const FUnderFireConfig& Config, const FManoeuvreOptions& Options);

	/** The step offsets to try, cm, nearest first, alternating sides: +Step, -Step, +2 Step, ... up to +-SidestepMaxCm. */
	CODEXTACTICS_API TArray<float> SidestepOffsets(const FUnderFireConfig& Config);

	/** Feet moved OffsetCm sideways off the planar line Feet -> Flash (positive = to the right of the flash direction). */
	CODEXTACTICS_API FVector SidestepPoint(const FVector& Feet, const FVector& Flash, float OffsetCm);

	/** Mate stands in the planar line From -> To: within RadiusCm of it, between its ends (not right at the muzzle, not past the flash). */
	CODEXTACTICS_API bool IsMateInLine(const FVector& From, const FVector& To, const FVector& Mate, float RadiusCm);

	/**
	 * He keeps the spot he manoeuvred to (the formation leaves him there, USquadSubsystem::IsOnOwnOrder): he manoeuvred in this spell, the
	 * last round came in no more than MemorySeconds + HoldSeconds ago, and the leader got no new move order since.
	 */
	CODEXTACTICS_API bool IsHoldingManoeuvreSpot(const FUnderFireConfig& Config, bool bManoeuvred, double Now, double LastShotAtTime, bool bLeaderOrderedSince);

	/** He went prone under fire and lies there still, the threat over: he crouches again (the formation does not crawl after the leader). */
	CODEXTACTICS_API bool ShouldRiseAfterThreat(bool bWentProneUnderFire, bool bStillProne, bool bThreatActive);
}
