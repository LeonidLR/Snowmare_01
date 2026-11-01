#pragma once

#include "CoreMinimal.h"

/**
 * The combat lab's ambushers (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md "Ambushers"; UE-only, no Godot reference):
 * an ambusher waits concealed and strikes when the squad is least ready - when nobody in the squad looks its way (the squad has turned
 * its back to it). Pure rules, tested in CodexTactics.AI.AmbushWatch.*; UCombatPressureSubsystem feeds them the squad's real looks (each
 * living member's body yaw and his formation sector look, docs/design/FORMATION_SECTORS.md).
 */

/** One look of a squad member: where he stands and where he looks. A member may give two (his body and his formation sector look). */
struct CODEXTACTICS_API FAmbushWatcher
{
	FVector2D Position = FVector2D::ZeroVector;
	/** Where he looks, degrees (UE yaw: atan2(dy, dx)). */
	float LookYawDeg = 0.f;
};

/** Tuning (level JSON "pressure"."ambush", LevelJsonRules::ParsePressureSettings). */
struct CODEXTACTICS_API FAmbushWatchParams
{
	/** A watcher covers the bearings within this of his look, degrees (his sector is twice as wide). */
	float WatchHalfAngleDeg = 45.f;
	/** A watcher farther than this from the ambusher does not count, cm (planar). */
	float WatchRangeCm = 5000.f;
	/** The ambusher strikes only when the nearest operative is this close, cm (planar). */
	float StrikeRangeCm = 1800.f;
};

/** What a hidden ambusher does this tick. */
enum class EAmbushDecision : uint8
{
	/** The squad is beyond the strike range: stay hidden. */
	Wait,
	/** In strike range, but someone looks its way: hold. */
	Hold,
	/** In strike range and nobody looks its way: strike now. */
	Spring
};

namespace AmbushWatchRules
{
	/** Bearing From -> To, degrees (UE yaw, -180..180]. */
	CODEXTACTICS_API float BearingDeg(const FVector2D& From, const FVector2D& To);

	/** |Look - Bearing| folded to 0..180, degrees. */
	CODEXTACTICS_API float AngleOffDeg(float LookYawDeg, float BearingDeg);

	/**
	 * Index of the first watcher within Params.WatchRangeCm of Ambusher whose sector (look +- WatchHalfAngleDeg) holds the bearing from him
	 * to Ambusher; INDEX_NONE = the side is unwatched.
	 */
	CODEXTACTICS_API int32 FindWatcher(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params);

	/** FindWatcher != INDEX_NONE. */
	CODEXTACTICS_API bool IsWatched(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params);

	/**
	 * How far the closest look (of the watchers in range) is from the bearing to Ambusher minus the half angle, degrees: < 0 = watched,
	 * > 0 = the margin by which the side is unwatched; 180 when nobody is in range.
	 */
	CODEXTACTICS_API float WatchMarginDeg(const TArray<FAmbushWatcher>& Watchers, const FVector2D& Ambusher, const FAmbushWatchParams& Params);

	/** Planar distance from Point to the nearest of Positions (MAX_flt when empty), cm. */
	CODEXTACTICS_API float NearestDistance(const TArray<FVector2D>& Positions, const FVector2D& Point);

	/**
	 * Wait while the nearest living operative (Squad) is beyond the strike range; in range: Hold while watched, Spring when unwatched.
	 * An empty squad: Wait.
	 */
	CODEXTACTICS_API EAmbushDecision Decide(const TArray<FAmbushWatcher>& Watchers, const TArray<FVector2D>& Squad, const FVector2D& Ambusher,
		const FAmbushWatchParams& Params);

	/**
	 * The middle of the widest arc of directions no look covers (each look +- WatchHalfAngleDeg; positions ignored - the looks as seen from
	 * the squad's middle), degrees; unset when the looks cover the whole ring. No watchers: 0 (everything is unwatched).
	 */
	CODEXTACTICS_API TOptional<float> WidestUnwatchedBearing(const TArray<FAmbushWatcher>& Watchers, const FAmbushWatchParams& Params);
}
