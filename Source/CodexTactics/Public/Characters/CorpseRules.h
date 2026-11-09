#pragma once

#include "CoreMinimal.h"

/**
 * Corpses stay (game designer 2026-11-08: "corpses of ALL enemies, not only PMC, must stay on the ground"; UE-only - Godot freed the body
 * after death_decay_delay). Pure rules (tests CodexTactics.Characters.Corpse.*); UCorpseSubsystem applies them: an enemy body is FROZEN once
 * its death clip has settled and it lies on the ground (a body killed in the air / on a ladder falls first - AEnemyCharacter::Landed, the
 * ladder's drop), then costs no game-thread time (no actor / movement / anim tick, no AI controller, out of the enemy registry, no collision
 * for pawns or clicks - still rendered). At most Cap bodies per level (level JSON "corpse_cap", default 150): above it the oldest one
 * FARTHEST from the squad fades out over FadeSeconds (scaled down), one not in the camera view first.
 * 2026-11-09 (game designer: "every enemy body stays, ALL enemies"): the hostile PMC fighters' bodies share the SAME cap and freeze
 * (UCorpseSubsystem takes any ICorpseBody); the player's own squad's KIA are never under it (they stay for good, they hold the remains);
 * a body without a death pose (the Spitter's C++ placeholder) stays too, laid on the ground; a body still holding unsearched loot is never
 * faded while it is in the camera view.
 */
struct CODEXTACTICS_API FCorpseConfig
{
	/** Bodies stay (Codex.Corpse.Persist 0: the old SetLifeSpan of the death decay delay). */
	bool bPersist = true;
	/** Bodies kept on the level ("corpse_cap" / Codex.Corpse.Cap). */
	int32 Cap = 150;
	/** Seconds a body over the cap takes to fade out (Codex.Corpse.FadeSeconds). */
	float FadeSeconds = 1.f;
	/** The fading body is the farthest from the squad among this many OLDEST bodies (Codex.Corpse.OldestPool). */
	int32 OldestPool = 10;
	/** A body is never frozen sooner after death (the death montage's blend-in) ... */
	float MinSettleSeconds = 0.6f;
	/** ... and its death pose must have held this long (no montage advancing, blends done) ... */
	float SettleHoldSeconds = 0.25f;
	/** ... or, failing that (a clip that never ends, no montage at all), this long after death. */
	float MaxSettleSeconds = 8.f;
};

/** One body as the cap rule sees it. */
struct CODEXTACTICS_API FCorpseCandidate
{
	/** World time of the death, s (smaller = older). */
	double DeathTime = 0.0;
	/** Distance to the nearest living operative, cm. */
	float SquadDistanceCm = 0.f;
	/** Rendered in the camera view lately. */
	bool bInView = false;
	/** Frozen (settled on the ground); a body still settling is never faded. */
	bool bFrozen = false;
	/** Already fading out. */
	bool bFading = false;
	/** It still holds loot the player has not searched (ICorpseBody::HoldsUnsearchedLoot): never faded while in view. */
	bool bUnsearchedLoot = false;
};

/** Whose body it is (CorpseRules::IsKeptUnderCap). */
enum class ECorpseKind : uint8
{
	/** A monster (AEnemyCharacter), with or without a death pose. */
	Enemy,
	/** A hostile PMC fighter (AOperativeCharacter of another team). */
	HostileOperative,
	/** One of the player's own squad killed in action. */
	SquadOperative
};

namespace CorpseRules
{
	/**
	 * The body may be frozen now: it lies (not falling, not on a ladder - unless 2 x MaxSettleSeconds passed, a body stuck in a fall) and
	 * either its death pose has held SettleHoldSeconds (SinceSettledSeconds, < 0 = not settled) after MinSettleSeconds, or MaxSettleSeconds
	 * passed since the death.
	 */
	CODEXTACTICS_API bool ShouldFreeze(float SinceDeathSeconds, float SinceSettledSeconds, bool bFalling, bool bOnLadder, const FCorpseConfig& Config);

	/** Bodies over the cap (the fading ones do not count), >= 0. */
	CODEXTACTICS_API int32 CountOverCap(const TArray<FCorpseCandidate>& Corpses, int32 Cap);

	/**
	 * The body to fade: among the frozen, not fading ones - those out of the camera view when there are any - the OldestPool oldest, of
	 * them the farthest from the squad (a tie: the older). A body with unsearched loot in the camera view is never picked. INDEX_NONE when
	 * no such body is left.
	 */
	CODEXTACTICS_API int32 PickCorpseToFade(const TArray<FCorpseCandidate>& Corpses, const FCorpseConfig& Config);

	/** The fading body's scale 1 -> 0 over FadeSeconds (smooth; 0 at and after the end). */
	CODEXTACTICS_API float FadeScale(float ElapsedSeconds, float FadeSeconds);

	/** The cap in force: the console value when >= 1, else the level's, at least 1. */
	CODEXTACTICS_API int32 ResolveCap(int32 LevelCap, int32 ConsoleCap);

	/**
	 * The body goes under the shared cap and the freeze (UCorpseSubsystem): every monster and every hostile PMC fighter; never the player's
	 * own squad's KIA (2026-11-09).
	 */
	CODEXTACTICS_API bool IsKeptUnderCap(ECorpseKind Kind);

	/**
	 * A body without a death pose laid on the ground (2026-11-09): the height of its centre (the placeholder cylinder's axis / a skeletal
	 * root) relative to the capsule centre - the floor lies HalfHeight below it, the body rests BodyHalfThickness above the floor (at most
	 * the capsule centre).
	 */
	CODEXTACTICS_API float LyingBodyCentreZ(float CapsuleHalfHeightCm, float BodyHalfThicknessCm);
}
