// CodexTactics.Core.GameplayRandom.* - the seeded gameplay random service (ai/arena-determinism, 2026-11-21): the same seed replays the
// same draws, the category streams are independent, unseeded every draw is FMath's global one. UE-only (no Godot reference).

#include "Core/CodexGameplayRandom.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CodexGameplayRandomTestLocal
{
	/** A mixed draw sequence of one category (every API the gameplay code uses), flattened to floats. */
	TArray<float> Draws(const FCodexRandomStreams& Streams, ECodexRandomStream Category, int32 Count)
	{
		TArray<float> Out;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.Add(Streams.FRand(Category));
			Out.Add(Streams.FRandRange(Category, -140.f, 140.f));
			Out.Add(static_cast<float>(Streams.RandRange(Category, 0, 9)));
			Out.Add(static_cast<float>(Streams.RandHelper(Category, 4)));
			Out.Add(Streams.RandBool(Category) ? 1.f : 0.f);
			const FVector Cone = Streams.VRandCone(Category, FVector::ForwardVector, 0.2f);
			Out.Add(static_cast<float>(Cone.Y));
			const FVector2D Circle = Streams.RandPointInCircle(Category, 120.f);
			Out.Add(static_cast<float>(Circle.X));
		}
		return Out;
	}

	/** Bitwise-equal sequences (a replay must be exact, not within a tolerance). */
	bool SameSequence(const TArray<float>& A, const TArray<float>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index] != B[Index])
			{
				return false;
			}
		}
		return true;
	}

	struct FScopedRandomTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		explicit FScopedRandomTestWorld(const TCHAR* WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
			if (World && GEngine)
			{
				Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
				Context->SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedRandomTestWorld()
		{
			if (World && GEngine)
			{
				if (Context)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
			}
		}
	};

	/** The global FMath generator back on a time seed after a parity check pinned it. */
	void ReseedGlobal()
	{
		FMath::RandInit(static_cast<int32>(FPlatformTime::Cycles()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodexGameplayRandomSameSeedTest, "CodexTactics.Core.GameplayRandom.SameSeedSameSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexGameplayRandomSameSeedTest::RunTest(const FString&)
{
	using namespace CodexGameplayRandomTestLocal;
	FCodexRandomStreams A;
	FCodexRandomStreams B;
	A.Seed(1234u);
	B.Seed(1234u);
	TestTrue(TEXT("seeded"), A.IsSeeded() && A.GetSeed() == 1234u);
	for (int32 Category = 0; Category < static_cast<int32>(ECodexRandomStream::Count); ++Category)
	{
		const ECodexRandomStream Stream = static_cast<ECodexRandomStream>(Category);
		TestTrue(FString::Printf(TEXT("category %d: the same seed draws the same sequence"), Category), SameSequence(Draws(A, Stream, 32), Draws(B, Stream, 32)));
	}

	// Seed() again restarts every stream (a new round with the replayed seed).
	FCodexRandomStreams Fresh;
	Fresh.Seed(1234u);
	const TArray<float> First = Draws(Fresh, ECodexRandomStream::Combat, 16);
	Fresh.Seed(1234u);
	TestTrue(TEXT("reseeding restarts the sequence"), SameSequence(First, Draws(Fresh, ECodexRandomStream::Combat, 16)));

	FCodexRandomStreams Other;
	Other.Seed(1235u);
	TestFalse(TEXT("the neighbouring seed draws another sequence"), SameSequence(First, Draws(Other, ECodexRandomStream::Combat, 16)));

	// Ranges hold on the seeded path.
	bool bInRange = true;
	for (int32 Index = 0; Index < 256; ++Index)
	{
		const float Roll = A.FRand(ECodexRandomStream::Combat);
		const int32 Pick = A.RandRange(ECodexRandomStream::Combat, 2, 5);
		const float Range = A.FRandRange(ECodexRandomStream::Combat, 6.f, 12.f);
		const FVector2D Point = A.RandPointInCircle(ECodexRandomStream::Combat, 50.f);
		bInRange &= Roll >= 0.f && Roll < 1.f && Pick >= 2 && Pick <= 5 && Range >= 6.f && Range <= 12.f && Point.Size() <= 50.f + UE_KINDA_SMALL_NUMBER;
	}
	TestTrue(TEXT("seeded draws stay in their ranges"), bInRange);
	TestEqual(TEXT("RandHelper(0) = 0"), A.RandHelper(ECodexRandomStream::Combat, 0), 0);
	TestEqual(TEXT("RandRange(5, 5) = 5"), A.RandRange(ECodexRandomStream::Combat, 5, 5), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodexGameplayRandomIndependentTest, "CodexTactics.Core.GameplayRandom.StreamsIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexGameplayRandomIndependentTest::RunTest(const FString&)
{
	using namespace CodexGameplayRandomTestLocal;
	FCodexRandomStreams Drained;
	FCodexRandomStreams Clean;
	Drained.Seed(77u);
	Clean.Seed(77u);
	// An idle variant / callout spree and some AI draws in between must not move the next combat roll.
	Draws(Drained, ECodexRandomStream::Cosmetic, 100);
	Draws(Drained, ECodexRandomStream::AI, 40);
	TestTrue(TEXT("draining Cosmetic + AI leaves the Combat sequence unchanged"),
		SameSequence(Draws(Drained, ECodexRandomStream::Combat, 32), Draws(Clean, ECodexRandomStream::Combat, 32)));
	// ... and the other way round.
	FCodexRandomStreams Reference;
	Reference.Seed(77u);
	Draws(Reference, ECodexRandomStream::Cosmetic, 100);
	TestTrue(TEXT("draining Combat leaves the Cosmetic sequence unchanged"),
		SameSequence(Draws(Drained, ECodexRandomStream::Cosmetic, 16), Draws(Reference, ECodexRandomStream::Cosmetic, 16)));

	// The categories are different sequences, from different stream seeds.
	FCodexRandomStreams Split;
	Split.Seed(77u);
	TestFalse(TEXT("Combat and Cosmetic differ"), SameSequence(Draws(Split, ECodexRandomStream::Combat, 8), Draws(Split, ECodexRandomStream::Cosmetic, 8)));
	const uint32 CombatSeed = FCodexRandomStreams::StreamSeed(77u, ECodexRandomStream::Combat);
	const uint32 AISeed = FCodexRandomStreams::StreamSeed(77u, ECodexRandomStream::AI);
	const uint32 CosmeticSeed = FCodexRandomStreams::StreamSeed(77u, ECodexRandomStream::Cosmetic);
	TestTrue(TEXT("three distinct stream seeds"), CombatSeed != AISeed && AISeed != CosmeticSeed && CombatSeed != CosmeticSeed);
	TestTrue(TEXT("the stream seed is a pure function"), FCodexRandomStreams::StreamSeed(77u, ECodexRandomStream::AI) == AISeed);
	TestTrue(TEXT("round seeds n / n+1 get unrelated combat seeds"),
		FCodexRandomStreams::StreamSeed(78u, ECodexRandomStream::Combat) != CombatSeed + 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodexGameplayRandomUnseededTest, "CodexTactics.Core.GameplayRandom.UnseededFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexGameplayRandomUnseededTest::RunTest(const FString&)
{
	using namespace CodexGameplayRandomTestLocal;
	FCodexRandomStreams Streams;
	TestFalse(TEXT("unseeded by default"), Streams.IsSeeded());

	// Unseeded = exactly the global FMath draws (normal play keeps its old behaviour).
	FMath::RandInit(4242);
	TArray<float> Expected;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Expected.Add(FMath::FRand());
		Expected.Add(static_cast<float>(FMath::RandRange(0, 9)));
	}
	FMath::RandInit(4242);
	TArray<float> Actual;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Actual.Add(Streams.FRand(ECodexRandomStream::Combat));
		Actual.Add(static_cast<float>(Streams.RandRange(ECodexRandomStream::Cosmetic, 0, 9)));
	}
	TestTrue(TEXT("unseeded draws are the global FMath sequence"), SameSequence(Expected, Actual));

	// Seed, then Unseed: back on the global draws.
	Streams.Seed(5u);
	Streams.Unseed();
	TestFalse(TEXT("Unseed clears the seeded flag"), Streams.IsSeeded());
	FMath::RandInit(99);
	const float Global = FMath::FRand();
	FMath::RandInit(99);
	TestTrue(TEXT("after Unseed the draw is FMath's again"), Streams.FRand(ECodexRandomStream::Combat) == Global);

	// No world context: the free functions fall back to FMath too.
	FMath::RandInit(7);
	const float GlobalA = FMath::FRand();
	const int32 GlobalB = FMath::RandRange(1, 6);
	FMath::RandInit(7);
	TestTrue(TEXT("a null context draws FMath::FRand"), CodexRandom::FRand(ECodexRandomStream::Combat, nullptr) == GlobalA);
	TestEqual(TEXT("a null context draws FMath::RandRange"), CodexRandom::RandRange(ECodexRandomStream::Combat, nullptr, 1, 6), GlobalB);
	ReseedGlobal();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodexGameplayRandomWorldTest, "CodexTactics.Core.GameplayRandom.WorldService",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexGameplayRandomWorldTest::RunTest(const FString&)
{
	using namespace CodexGameplayRandomTestLocal;
	FScopedRandomTestWorld TestWorld(TEXT("CodexGameplayRandomTestWorld"));
	if (!TestNotNull(TEXT("test world"), TestWorld.World))
	{
		return false;
	}
	UWorld* World = TestWorld.World;
	UCodexGameplayRandom* Random = UCodexGameplayRandom::Get(World);
	if (!TestNotNull(TEXT("the world has the gameplay random subsystem"), Random))
	{
		return false;
	}
	TestFalse(TEXT("a fresh world is unseeded"), Random->IsSeeded());
	FMath::RandInit(31);
	const float Global = FMath::FRand();
	FMath::RandInit(31);
	TestTrue(TEXT("unseeded world draws are FMath's"), CodexRandom::FRand(ECodexRandomStream::Combat, World) == Global);

	// Seeded: the world's draws are the pure stream set's, and cosmetic draws in between do not shift combat.
	Random->Seed(2026u);
	FCodexRandomStreams Reference;
	Reference.Seed(2026u);
	bool bSame = true;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		CodexRandom::RandRange(ECodexRandomStream::Cosmetic, World, 0, 3); // an anim variant pick between the shots
		CodexRandom::RandBool(ECodexRandomStream::Cosmetic, World);
		bSame &= CodexRandom::FRand(ECodexRandomStream::Combat, World) == Reference.FRand(ECodexRandomStream::Combat);
		bSame &= CodexRandom::FRandRange(ECodexRandomStream::Combat, World, -140.f, 140.f) == Reference.FRandRange(ECodexRandomStream::Combat, -140.f, 140.f);
		bSame &= CodexRandom::VRandCone(ECodexRandomStream::Combat, World, FVector::ForwardVector, 0.1f) == Reference.VRandCone(ECodexRandomStream::Combat, FVector::ForwardVector, 0.1f);
		bSame &= CodexRandom::RandPointInCircle(ECodexRandomStream::Combat, World, 120.f) == Reference.RandPointInCircle(ECodexRandomStream::Combat, 120.f);
	}
	TestTrue(TEXT("seeded world combat draws replay the reference sequence, cosmetic picks in between"), bSame);

	Random->Unseed();
	TestFalse(TEXT("Unseed"), Random->IsSeeded());
	FMath::RandInit(32);
	const float GlobalAfter = FMath::FRand();
	FMath::RandInit(32);
	TestTrue(TEXT("after Unseed the world draws FMath again"), CodexRandom::FRand(ECodexRandomStream::Combat, World) == GlobalAfter);
	ReseedGlobal();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
