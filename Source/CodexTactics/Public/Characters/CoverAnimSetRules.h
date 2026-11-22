// Copyright CodexTactics. No Godot reference: Unreal-only feature (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md).

#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

/** Where the operative holds his cover: picks the cover animation set (Content/Data/Animation/cover_anim_sets.json "contexts"). */
enum class ECoverAnimContext : uint8
{
	Outdoor,
	Indoor,
	Stealth
};

/** Which fire stance a cover set plays: the side peek round the corner or the over-the-top shot (Aim/2: low cover, away from its corner). */
enum class ECoverFireSet : uint8
{
	Side,
	Over
};

/** One named set: skeleton name -> clip key -> asset path ("default" serves skeletons not listed), plus per-clip timings. */
struct FCoverAnimSetData
{
	TMap<FString, TMap<FString, FString>> Bodies;
	/** Grenade release time per "<stance>_<side>" key, s. */
	TMap<FString, float> GrenadeReleaseSeconds;
	/** The walk loops' ground pace at rate 1 per stance ("stand" / "crouch"), cm/s. */
	TMap<FString, float> ShimmyClipSpeed;
};

/** The whole cover_anim_sets.json. */
struct FCoverAnimSetConfig
{
	bool bEnabled = false;
	/** context name ("outdoor" / "indoor" / "stealth") -> set name; "m4" (or empty / unknown) = the native clips. */
	TMap<FString, FString> Contexts;
	/** operative key (DisplayName / OperativeId) -> context name -> set name. */
	TMap<FString, TMap<FString, FString>> Operatives;
	TMap<FString, FCoverAnimSetData> Sets;
	/** Walks along the wall at least this long play the walk start / stop clips (designer Q5 2026-11-22), cm. */
	float ShimmyTransitionMinCm = 150.f;
};

/**
 * Pure rules of the data-driven cover animation sets (the indoor Riffle set today, the stealth wall set / per-operative wall styles later).
 * Tests: CodexTactics.Animation.CoverAnimSet.*.
 */
namespace CoverAnimSetRules
{
	/** The name the native (M4 Cover Pack) clips go by: choosing it means "no override". */
	inline const TCHAR* NativeSetName = TEXT("m4");

	/** Parses the JSON text; false (and OutError) when it is not an object. Unknown fields are ignored, bad ones keep their defaults. */
	CODEXTACTICS_API bool ParseConfig(const FString& JsonText, FCoverAnimSetConfig& OutConfig, FString& OutError);

	/** "outdoor" / "indoor" / "stealth". */
	CODEXTACTICS_API const TCHAR* ContextName(ECoverAnimContext Context);

	/** Stealth (reserved: the stealth layer owns the wall pose today) over indoor (the storey rule put him in a building) over outdoor. */
	CODEXTACTICS_API ECoverAnimContext DecideContext(bool bInBuilding, bool bStealth);

	/**
	 * The set for Context: the operative's own entry (any of OperativeKeys, first match) over the context's; disabled, unknown or "m4"
	 * -> empty (= the native clips).
	 */
	CODEXTACTICS_API FString ChooseSet(const FCoverAnimSetConfig& Config, ECoverAnimContext Context, const TArray<FString>& OperativeKeys);

	/** The clip table of Set for a skeleton (its own entry, else "default"); nullptr when the set or both entries are missing. */
	CODEXTACTICS_API const TMap<FString, FString>* FindBodyClips(const FCoverAnimSetConfig& Config, const FString& Set, const FString& SkeletonName);

	/** "<base>_right" / "<base>_left" for the side along the wall he faces. */
	CODEXTACTICS_API FString SideKey(const FString& Base, ECoverFacing Facing);

	/** Side peek at a corner / high cover / standing; the over-the-top shot crouched behind a LOW cover away from its corner (a window sill). */
	CODEXTACTICS_API ECoverFireSet DecideFireSet(bool bCrouching, ECoverHeight Height, bool bAtCorner, bool bSetHasOverClips);

	/** A walk along the wall this long plays the start / stop clips (Q5): at least MinCm and the clip exists. */
	CODEXTACTICS_API bool UseShimmyTransitions(float WalkCm, float MinCm, bool bHasClip);

	/** The walk stop clip starts when the distance left is within the clip's own travel at this pace (pace x clip length), cm. */
	CODEXTACTICS_API bool ShouldStartShimmyStop(float RemainingCm, float SpeedCmPerSecond, float StopClipSeconds);
}
