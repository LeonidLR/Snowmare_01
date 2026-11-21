#include "Core/CodexGameplayRandom.h"

#include "Engine/World.h"
#include "CoreGlobals.h"

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
	}

	float FRand(ECodexRandomStream Category, const UObject* WorldContext)
	{
		return StreamsFor(WorldContext).FRand(Category);
	}

	float FRandRange(ECodexRandomStream Category, const UObject* WorldContext, float Min, float Max)
	{
		return StreamsFor(WorldContext).FRandRange(Category, Min, Max);
	}

	int32 RandRange(ECodexRandomStream Category, const UObject* WorldContext, int32 Min, int32 Max)
	{
		return StreamsFor(WorldContext).RandRange(Category, Min, Max);
	}

	int32 RandHelper(ECodexRandomStream Category, const UObject* WorldContext, int32 Count)
	{
		return StreamsFor(WorldContext).RandHelper(Category, Count);
	}

	bool RandBool(ECodexRandomStream Category, const UObject* WorldContext)
	{
		return StreamsFor(WorldContext).RandBool(Category);
	}

	FVector VRand(ECodexRandomStream Category, const UObject* WorldContext)
	{
		return StreamsFor(WorldContext).VRand(Category);
	}

	FVector VRandCone(ECodexRandomStream Category, const UObject* WorldContext, const FVector& Dir, float ConeHalfAngleRad)
	{
		return StreamsFor(WorldContext).VRandCone(Category, Dir, ConeHalfAngleRad);
	}

	FVector2D RandPointInCircle(ECodexRandomStream Category, const UObject* WorldContext, float Radius)
	{
		return StreamsFor(WorldContext).RandPointInCircle(Category, Radius);
	}
}
