#pragma once

// UE-only (no Godot reference; Godot used the global randf()): the seeded gameplay random service (ai/arena-determinism, 2026-11-21).
// The PMC arena (CodexTacticsDev PmcArenaCommand.cpp) seeds it at the start of every round so a round replays the same way for the
// same seed; unseeded (normal game, PIE, every other smoke) every draw is the global FMath one, exactly as before - no gameplay change.
// Draws come from separate streams per consumer category so a cosmetic pick (an idle variant, a callout line) cannot shift the combat
// sequence. Game thread only: a draw from another thread (or without a world) always falls back to FMath and never touches a stream.
// Rule: never draw from an animation proxy's Update / Evaluate or any worker-thread task - those run on worker threads, fall back to FMath
// and so break the replay silently. Draw on the game thread (NativeUpdateAnimation, Tick) and hand the result to the proxy.
// Codex.Random.Trace (1 = every seeded draw, 2 = + every operative's state per tick) diffs two runs of one seed to their first fork.

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "CodexGameplayRandom.generated.h"

/** The independent random streams of the seeded gameplay random service. */
UENUM()
enum class ECodexRandomStream : uint8
{
	/** Hits, crits, dodges, misfires, spread, miss offsets, suppression scatter, knockdown, panic / rage rolls. */
	Combat,
	/** Squad / commander decisions that draw at runtime (the PMC rules mostly use their own FRandomStream(Seed)). */
	AI,
	/** Animation variant picks, idle loop counts, callout / chatter lines. */
	Cosmetic,
	Count UMETA(Hidden)
};

/**
 * The stream set (pure logic, no world): unseeded every draw is FMath's global one; Seed() gives every category its own
 * FRandomStream derived from (seed, category).
 */
struct CODEXTACTICS_API FCodexRandomStreams
{
	/** Seeds every category stream from InSeed (StreamSeed). */
	void Seed(uint32 InSeed);

	/** Back to the global FMath draws. */
	void Unseed();

	/** Seed() was called since the last Unseed(). */
	bool IsSeeded() const { return bSeeded; }

	/** The seed passed to the last Seed() (0 when unseeded). */
	uint32 GetSeed() const { return BaseSeed; }

	/** The FRandomStream seed of Category for InSeed: a mixed (murmur3 finaliser) hash so neighbouring seeds / categories decorrelate. */
	static uint32 StreamSeed(uint32 InSeed, ECodexRandomStream Category);

	/** [0, 1) (FMath::FRand). */
	float FRand(ECodexRandomStream Category) const;

	/** [Min, Max) (FMath::FRandRange). */
	float FRandRange(ECodexRandomStream Category, float Min, float Max) const;

	/** Min..Max inclusive (FMath::RandRange). */
	int32 RandRange(ECodexRandomStream Category, int32 Min, int32 Max) const;

	/** 0..Count-1, 0 when Count <= 0 (FMath::RandHelper). */
	int32 RandHelper(ECodexRandomStream Category, int32 Count) const;

	/** A coin flip (FMath::RandBool). */
	bool RandBool(ECodexRandomStream Category) const;

	/** A random unit vector (FMath::VRand). */
	FVector VRand(ECodexRandomStream Category) const;

	/** A random unit vector inside the cone around Dir (FMath::VRandCone). */
	FVector VRandCone(ECodexRandomStream Category, const FVector& Dir, float ConeHalfAngleRad) const;

	/** A uniform point inside a circle of Radius (FMath::RandPointInCircle, the same rejection sampling). */
	FVector2D RandPointInCircle(ECodexRandomStream Category, float Radius) const;

private:
	const FRandomStream& StreamOf(ECodexRandomStream Category) const;

	FRandomStream Streams[static_cast<int32>(ECodexRandomStream::Count)];
	uint32 BaseSeed = 0;
	bool bSeeded = false;
};

/**
 * Per-world owner of the gameplay random streams. Unseeded by default (FMath draws); the PMC arena calls Seed(round seed) at the
 * start of every round and Unseed() when it finishes. Gameplay code draws through the CodexRandom functions below.
 */
UCLASS()
class CODEXTACTICS_API UCodexGameplayRandom : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of WorldContext's world (null without one). */
	static UCodexGameplayRandom* Get(const UObject* WorldContext);

	/** Seeds every category stream from InSeed: the same seed gives the same draw sequence per category. */
	void Seed(uint32 InSeed);

	/** Back to the global FMath draws (normal play). */
	void Unseed();

	/** Seeded since the last Unseed(). */
	bool IsSeeded() const { return Streams.IsSeeded(); }

	/** The stream set (draws). */
	const FCodexRandomStreams& GetStreams() const { return Streams; }

private:
	FCodexRandomStreams Streams;
};

/**
 * Gameplay random draws. WorldContext is any object with a world (actor, component, anim instance); a null context, a world without
 * the subsystem, an unseeded service or a call off the game thread all draw from FMath (the old behaviour).
 */
namespace CodexRandom
{
	/** [0, 1). */
	CODEXTACTICS_API float FRand(ECodexRandomStream Category, const UObject* WorldContext);

	/** [Min, Max). */
	CODEXTACTICS_API float FRandRange(ECodexRandomStream Category, const UObject* WorldContext, float Min, float Max);

	/** Min..Max inclusive. */
	CODEXTACTICS_API int32 RandRange(ECodexRandomStream Category, const UObject* WorldContext, int32 Min, int32 Max);

	/** 0..Count-1 (0 when Count <= 0). */
	CODEXTACTICS_API int32 RandHelper(ECodexRandomStream Category, const UObject* WorldContext, int32 Count);

	/** A coin flip. */
	CODEXTACTICS_API bool RandBool(ECodexRandomStream Category, const UObject* WorldContext);

	/** A random unit vector. */
	CODEXTACTICS_API FVector VRand(ECodexRandomStream Category, const UObject* WorldContext);

	/** A random unit vector inside the cone around Dir. */
	CODEXTACTICS_API FVector VRandCone(ECodexRandomStream Category, const UObject* WorldContext, const FVector& Dir, float ConeHalfAngleRad);

	/** A uniform point inside a circle of Radius. */
	CODEXTACTICS_API FVector2D RandPointInCircle(ECodexRandomStream Category, const UObject* WorldContext, float Radius);
}
