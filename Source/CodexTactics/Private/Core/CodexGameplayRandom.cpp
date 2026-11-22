#include "Core/CodexGameplayRandom.h"

#include "Engine/World.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "CodexTactics.h"

namespace CodexRandomTrace
{
	/** Determinism hunt (2026-11-22): 1 logs every seeded gameplay draw - frame, stream, caller, value - so two runs of one seed can be diffed;
	 *  2 also logs every operative's exact state per tick (AOperativeCharacter::Tick, non-shipping). */
	static TAutoConsoleVariable<int32> CVarTrace(TEXT("Codex.Random.Trace"), 0,
		TEXT("Determinism diffs: 1 = log every seeded gameplay random draw ([Rng] frame stream caller value); 2 = also every operative's exact state per tick ([RngPos], non-shipping)."), ECVF_Default);
}

void FCodexRandomStreams::Seed(uint32 InSeed)
{
	BaseSeed = InSeed;
	bSeeded = true;
	for (int32 Index = 0; Index < static_cast<int32>(ECodexRandomStream::Count); ++Index)
	{
		Streams[Index].Initialize(static_cast<int32>(StreamSeed(InSeed, static_cast<ECodexRandomStream>(Index))));
	}
}

void FCodexRandomStreams::Unseed()
{
	BaseSeed = 0;
	bSeeded = false;
}

uint32 FCodexRandomStreams::StreamSeed(uint32 InSeed, ECodexRandomStream Category)
{
	uint32 Hash = InSeed * 0x9E3779B1u;
	Hash ^= (static_cast<uint32>(Category) + 1u) * 0x85EBCA77u + 0x165667B1u;
	// murmur3 fmix32
	Hash ^= Hash >> 16;
	Hash *= 0x85EBCA6Bu;
	Hash ^= Hash >> 13;
	Hash *= 0xC2B2AE35u;
	Hash ^= Hash >> 16;
	return Hash;
}

const FRandomStream& FCodexRandomStreams::StreamOf(ECodexRandomStream Category) const
{
	const int32 Index = FMath::Clamp(static_cast<int32>(Category), 0, static_cast<int32>(ECodexRandomStream::Count) - 1);
	return Streams[Index];
}

float FCodexRandomStreams::FRand(ECodexRandomStream Category) const
{
	return bSeeded ? StreamOf(Category).FRand() : FMath::FRand();
}

float FCodexRandomStreams::FRandRange(ECodexRandomStream Category, float Min, float Max) const
{
	return bSeeded ? static_cast<float>(StreamOf(Category).FRandRange(Min, Max)) : FMath::FRandRange(Min, Max);
}

int32 FCodexRandomStreams::RandRange(ECodexRandomStream Category, int32 Min, int32 Max) const
{
	return bSeeded ? StreamOf(Category).RandRange(Min, Max) : FMath::RandRange(Min, Max);
}

int32 FCodexRandomStreams::RandHelper(ECodexRandomStream Category, int32 Count) const
{
	return bSeeded ? StreamOf(Category).RandHelper(Count) : FMath::RandHelper(Count);
}

bool FCodexRandomStreams::RandBool(ECodexRandomStream Category) const
{
	return bSeeded ? StreamOf(Category).RandBool() : FMath::RandBool();
}

FVector FCodexRandomStreams::VRand(ECodexRandomStream Category) const
{
	return bSeeded ? StreamOf(Category).VRand() : FMath::VRand();
}

FVector FCodexRandomStreams::VRandCone(ECodexRandomStream Category, const FVector& Dir, float ConeHalfAngleRad) const
{
	return bSeeded ? StreamOf(Category).VRandCone(Dir, ConeHalfAngleRad) : FMath::VRandCone(Dir, ConeHalfAngleRad);
}

FVector2D FCodexRandomStreams::RandPointInCircle(ECodexRandomStream Category, float Radius) const
{
	if (!bSeeded)
	{
		return FMath::RandPointInCircle(Radius);
	}
	const FRandomStream& Stream = StreamOf(Category);
	FVector2D Point;
	do
	{
		Point.X = Stream.FRand() * 2.f - 1.f;
		Point.Y = Stream.FRand() * 2.f - 1.f;
	}
	while (Point.SizeSquared() > 1.f);
	return Point * Radius;
}

UCodexGameplayRandom* UCodexGameplayRandom::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCodexGameplayRandom>() : nullptr;
}

void UCodexGameplayRandom::Seed(uint32 InSeed)
{
	Streams.Seed(InSeed);
}

void UCodexGameplayRandom::Unseed()
{
	Streams.Unseed();
}

namespace CodexRandom
{
	namespace
	{
		/** The seeded streams of WorldContext's world on the game thread, else an always-unseeded set (FMath draws). */
		const FCodexRandomStreams& StreamsFor(const UObject* WorldContext)
		{
			static const FCodexRandomStreams Unseeded;
			if (!IsInGameThread())
			{
				return Unseeded;
			}
			const UCodexGameplayRandom* Service = UCodexGameplayRandom::Get(WorldContext);
			return Service && Service->IsSeeded() ? Service->GetStreams() : Unseeded;
		}

		/** Whether the trace is on: checked before any string is built, so a normal (unseeded or untraced) draw pays one branch. */
		bool TraceOn(const FCodexRandomStreams& S)
		{
			return S.IsSeeded() && CodexRandomTrace::CVarTrace.GetValueOnGameThread() > 0;
		}

		void NoteText(ECodexRandomStream Category, const UObject* WorldContext, const FString& Value)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Rng] f=%llu s=%d ctx=%s v=%s"), static_cast<unsigned long long>(GFrameCounter),
				static_cast<int32>(Category), WorldContext ? *WorldContext->GetName() : TEXT("-"), *Value);
		}
		void Note(const FCodexRandomStreams& S, ECodexRandomStream C, const UObject* W, float V) { if (TraceOn(S)) { NoteText(C, W, FString::Printf(TEXT("%.6f"), V)); } }
		void Note(const FCodexRandomStreams& S, ECodexRandomStream C, const UObject* W, int32 V) { if (TraceOn(S)) { NoteText(C, W, FString::FromInt(V)); } }
		void Note(const FCodexRandomStreams& S, ECodexRandomStream C, const UObject* W, bool V) { if (TraceOn(S)) { NoteText(C, W, V ? TEXT("1") : TEXT("0")); } }
		void Note(const FCodexRandomStreams& S, ECodexRandomStream C, const UObject* W, const FVector& V) { if (TraceOn(S)) { NoteText(C, W, FString::Printf(TEXT("%.5f,%.5f,%.5f"), V.X, V.Y, V.Z)); } }
		void Note(const FCodexRandomStreams& S, ECodexRandomStream C, const UObject* W, const FVector2D& V) { if (TraceOn(S)) { NoteText(C, W, FString::Printf(TEXT("%.4f,%.4f"), V.X, V.Y)); } }
	}

	float FRand(ECodexRandomStream Category, const UObject* WorldContext)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.FRand(Category);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	float FRandRange(ECodexRandomStream Category, const UObject* WorldContext, float Min, float Max)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.FRandRange(Category, Min, Max);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	int32 RandRange(ECodexRandomStream Category, const UObject* WorldContext, int32 Min, int32 Max)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.RandRange(Category, Min, Max);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	int32 RandHelper(ECodexRandomStream Category, const UObject* WorldContext, int32 Count)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.RandHelper(Category, Count);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	bool RandBool(ECodexRandomStream Category, const UObject* WorldContext)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.RandBool(Category);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	FVector VRand(ECodexRandomStream Category, const UObject* WorldContext)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.VRand(Category);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	FVector VRandCone(ECodexRandomStream Category, const UObject* WorldContext, const FVector& Dir, float ConeHalfAngleRad)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.VRandCone(Category, Dir, ConeHalfAngleRad);
		Note(S, Category, WorldContext, Value);
		return Value;
	}

	FVector2D RandPointInCircle(ECodexRandomStream Category, const UObject* WorldContext, float Radius)
	{
		const FCodexRandomStreams& S = StreamsFor(WorldContext);
		const auto Value = S.RandPointInCircle(Category, Radius);
		Note(S, Category, WorldContext, Value);
		return Value;
	}
}
