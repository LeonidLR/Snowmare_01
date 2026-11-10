#include "LevelGen/PropsRng.h"

#include "Containers/StringConv.h"

uint32 FPropsRng::NextU32()
{
	// uint32 arithmetic wraps exactly like the JS "| 0" / Math.imul / ">>> 0" chain
	State += PropsRng::Mulberry32Increment;
	uint32 T = (State ^ (State >> 15)) * (State | 1u);
	T = (T + (T ^ (T >> 7)) * (T | 61u)) ^ T;
	return T ^ (T >> 14);
}

uint32 FPropsRng::NextBelow(uint32 Bound)
{
	if (Bound == 0)
	{
		return 0;
	}
	return static_cast<uint32>((static_cast<uint64>(NextU32()) * static_cast<uint64>(Bound)) >> 32);
}

void FPropsRng::Shuffle(TArray<int32>& Items)
{
	for (int32 I = Items.Num() - 1; I > 0; --I)
	{
		const int32 J = static_cast<int32>(NextBelow(static_cast<uint32>(I + 1)));
		Items.Swap(I, J);
	}
}

uint32 PropsRng::Fnv1a(const FString& Text)
{
	const FTCHARToUTF8 Utf8(*Text, Text.Len());
	const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
	uint32 H = Fnv1aOffsetBasis;
	for (int32 I = 0; I < Utf8.Length(); ++I)
	{
		H ^= Bytes[I];
		H *= Fnv1aPrime;
	}
	return H;
}

uint32 PropsRng::StreamSeed(uint32 Seed, const FString& LayerId)
{
	return Seed ^ Fnv1a(LayerId);
}
