#include "Misc/AutomationTest.h"
#include "AI/PmcEncounterRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// PMC squads as level encounters (user request 2026-11-03, docs/systems/pmc-squad-ai.md 7): pure rules.

namespace PmcEncounterTestLocal
{
	TArray<TSharedPtr<FJsonValue>> ParseArray(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcEncounterParseTest, "CodexTactics.AI.PmcEncounter.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcEncounterParseTest::RunTest(const FString&)
{
	using namespace PmcEncounterTestLocal;
	TArray<FString> Warnings;
	const TArray<FPmcEncounterSpec> Squads = PmcEncounterRules::ParseSquads(ParseArray(TEXT(R"([
		{ "count": 3, "weights": "pmc_commander.json", "spawn": "random", "min_m": 40, "max_m": 90, "delay_s": 30, "posture": "defend" },
		{ "posture": "HUNT", "delay_s": 120 },
		{ "count": 2, "spawn": [100, -200], "posture": "patrol", "seed": 9 },
		{ "count": 0 },
		{ "posture": "dance" },
		{ "spawn": "nearby" },
		{ "count": 12, "min_m": 1, "max_m": 2 }
	])")), &Warnings);
	if (!TestEqual(TEXT("valid entries kept"), Squads.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("bad entries reported"), Warnings.Num(), 3);
	const FPmcEncounterSpec& Defend = Squads[0];
	TestEqual(TEXT("count"), Defend.Count, 3);
	TestEqual(TEXT("posture defend"), static_cast<int32>(Defend.Posture), static_cast<int32>(EPmcPosture::Defend));
	TestTrue(TEXT("random spawn"), Defend.bRandomSpawn);
	TestEqual(TEXT("min 40 m"), Defend.MinCm, 4000.f);
	TestEqual(TEXT("max 90 m"), Defend.MaxCm, 9000.f);
	TestEqual(TEXT("delay"), Defend.DelaySeconds, 30.f);
	TestEqual(TEXT("weights"), Defend.Weights, FString(TEXT("pmc_commander.json")));
	// Defaults: three fighters, hunt, 40..90 m, the default weights.
	const FPmcEncounterSpec& Hunt = Squads[1];
	TestEqual(TEXT("default count"), Hunt.Count, 3);
	TestEqual(TEXT("posture case-insensitive"), static_cast<int32>(Hunt.Posture), static_cast<int32>(EPmcPosture::Hunt));
	TestEqual(TEXT("default min"), Hunt.MinCm, 4000.f);
	TestEqual(TEXT("default weights"), Hunt.Weights, FString(TEXT("pmc_commander.json")));
	const FPmcEncounterSpec& Patrol = Squads[2];
	TestFalse(TEXT("fixed spawn"), Patrol.bRandomSpawn);
	TestEqual(TEXT("fixed spot"), Patrol.FixedSpot, FVector(100.f, -200.f, 0.f));
	TestEqual(TEXT("seed"), Patrol.Seed, 9);
	// Clamps: at most six men, at least 5 m away, max >= min + 5 m.
	const FPmcEncounterSpec& Clamped = Squads[3];
	TestEqual(TEXT("count clamped"), Clamped.Count, 6);
	TestEqual(TEXT("min clamped to 5 m"), Clamped.MinCm, 500.f);
	TestEqual(TEXT("max >= min + 5 m"), Clamped.MaxCm, 1000.f);
	// Posture words.
	EPmcPosture Posture = EPmcPosture::Hunt;
	TestTrue(TEXT("ambush = defend"), PmcEncounterRules::ParsePosture(TEXT(" Ambush "), Posture) && Posture == EPmcPosture::Defend);
	TestFalse(TEXT("unknown word"), PmcEncounterRules::ParsePosture(TEXT("sleep"), Posture));
	TestEqual(TEXT("name"), FString(PmcEncounterRules::PostureName(EPmcPosture::Patrol)), FString(TEXT("patrol")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcEncounterSpotTest, "CodexTactics.AI.PmcEncounter.Spot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcEncounterSpotTest::RunTest(const FString&)
{
	// Spots every 10 m along a line from the squad at 0, alternating levels: a pick is in range, accepted, the same per seed.
	TArray<FPmcSpawnCandidate> Spots;
	for (int32 Index = 0; Index < 15; ++Index)
	{
		Spots.Add({ FVector(Index * 1000.f, 0.f, (Index % 2) * 300.f), Index % 2 });
	}
	const FVector Squad = FVector::ZeroVector;
	auto All = [](int32) { return true; };
	TSet<int32> Distinct;
	int32 Elevated = 0;
	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		const int32 Pick = PmcEncounterRules::PickEncounterSpot(Spots, Squad, 4000.f, 9000.f, Seed, All);
		if (!TestTrue(TEXT("a spot found"), Pick != INDEX_NONE))
		{
			return false;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Spots[Pick].Location, Squad));
		TestTrue(TEXT("40..90 m from the squad"), Distance >= 4000.f && Distance <= 9000.f);
		TestEqual(TEXT("deterministic"), PmcEncounterRules::PickEncounterSpot(Spots, Squad, 4000.f, 9000.f, Seed, All), Pick);
		Distinct.Add(Pick);
		Elevated += Spots[Pick].Level > 0 ? 1 : 0;
	}
	TestTrue(TEXT("seeds vary the spot"), Distinct.Num() >= 3);
	TestTrue(TEXT("roofs favoured a little"), Elevated >= 6);
	// Seen / unreachable spots are skipped: only spot 7 is acceptable.
	TestEqual(TEXT("the acceptable one"), PmcEncounterRules::PickEncounterSpot(Spots, Squad, 4000.f, 9000.f, 3, [](int32 Index) { return Index == 7; }), 7);
	TestEqual(TEXT("nothing acceptable"), PmcEncounterRules::PickEncounterSpot(Spots, Squad, 4000.f, 9000.f, 3, [](int32) { return false; }), INDEX_NONE);
	TestEqual(TEXT("nothing in range"), PmcEncounterRules::PickEncounterSpot(Spots, Squad, 20000.f, 30000.f, 3, All), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcEncounterSearchTest, "CodexTactics.AI.PmcEncounter.Search",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcEncounterSearchTest::RunTest(const FString&)
{
	const FVector Start(1000.f, 0.f, 0.f);
	const FVector Squad(5000.f, 5000.f, 0.f);
	// A known position wins; else the start first, then radio intel near the squad (never on it).
	TestEqual(TEXT("last known"), PmcEncounterRules::HuntSearchPoint(true, FVector(7.f, 8.f, 9.f), 3, Start, Squad, 1500.f, 1), FVector(7.f, 8.f, 9.f));
	TestEqual(TEXT("leg 0: the player start"), PmcEncounterRules::HuntSearchPoint(false, FVector::ZeroVector, 0, Start, Squad, 1500.f, 1), Start);
	for (int32 Leg = 1; Leg <= 10; ++Leg)
	{
		const FVector Point = PmcEncounterRules::HuntSearchPoint(false, FVector::ZeroVector, Leg, Start, Squad, 1500.f, 4);
		const float Off = static_cast<float>(FVector::Dist2D(Point, Squad));
		TestTrue(TEXT("radio intel 7.5..15 m off the squad"), Off >= 749.f && Off <= 1501.f);
		TestEqual(TEXT("deterministic"), PmcEncounterRules::HuntSearchPoint(false, FVector::ZeroVector, Leg, Start, Squad, 1500.f, 4), Point);
	}
	// Patrol points: inside the ring round the anchor, varying per leg.
	TSet<FIntPoint> Distinct;
	for (int32 Leg = 0; Leg < 8; ++Leg)
	{
		const FVector Point = PmcEncounterRules::PatrolPoint(Start, 1500.f, 2, Leg);
		const float Off = static_cast<float>(FVector::Dist2D(Point, Start));
		TestTrue(TEXT("patrol 6..15 m round the anchor"), Off >= 599.f && Off <= 1501.f);
		Distinct.Add(FIntPoint(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y)));
	}
	TestTrue(TEXT("patrol legs differ"), Distinct.Num() >= 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcEncounterResumeHuntTest, "CodexTactics.AI.PmcEncounter.ResumeHunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcEncounterResumeHuntTest::RunTest(const FString&)
{
	// fix/pmc-hunt-honest-sight (2026-11-09): hunters that lost sight of the player's men hunt again instead of holding for good.
	TestTrue(TEXT("hunters, after contact, nobody seen for 15 s: the hunt resumes"), PmcEncounterRules::ShouldResumeHunt(true, true, 15.f, false, 15.f));
	TestFalse(TEXT("... not before 15 s"), PmcEncounterRules::ShouldResumeHunt(true, true, 14.f, false, 15.f));
	TestFalse(TEXT("... not while one of them sees an enemy"), PmcEncounterRules::ShouldResumeHunt(true, true, 60.f, true, 15.f));
	TestFalse(TEXT("... not before any contact (the first hunt still runs)"), PmcEncounterRules::ShouldResumeHunt(true, false, 60.f, false, 15.f));
	TestFalse(TEXT("a defending / patrolling squad holds its ground"), PmcEncounterRules::ShouldResumeHunt(false, true, 60.f, false, 15.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
