#include "Misc/AutomationTest.h"
#include "AI/PmcEncounterRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Smart-enemies T-11 (first half), PMC_DEFENCE D2: generator-placed PMC squads spawn at their post. Pure rules: ParseSquad reads
// "post", the men's places on the post, their facing, the pmc_operators.json "post_spawn" numbers.

namespace PmcPostSpawnTestLocal
{
	TArray<TSharedPtr<FJsonValue>> PostEntriesFromText(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Out);
		return Out;
	}

	TSharedPtr<FJsonObject> PostObjectFromText(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPostSpawnParseTest, "CodexTactics.AI.PmcEncounter.PostSpawnParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPostSpawnParseTest::RunTest(const FString&)
{
	using namespace PmcPostSpawnTestLocal;
	// The entries the generator writes (Scripts/Editor/build_levelgen_map.py pmc_squads_of): a "post" and no "spawn".
	TArray<FString> Warnings;
	const TArray<FPmcEncounterSpec> Squads = PmcEncounterRules::ParseSquads(PostEntriesFromText(TEXT(R"([
		{ "count": 2, "weights": "pmc_commander.json", "posture": "defend", "delay_s": 0,
		  "post": { "id": "PMC_01", "kind": "window", "pos": [5640, -767, 0], "yaw": 90 } },
		{ "count": 3, "posture": "hunt", "delay_s": 120, "post": { "id": "PMC_03", "kind": "ambush_exit", "pos": [475, -3975, 0], "yaw": -125.5 } },
		{ "count": 2, "posture": "defend", "delay_s": 45, "post": { "id": "PMC_04", "kind": "overwatch_roof", "pos": [-2100, 1250, 600] } },
		{ "post": { "pos": [10, 20] } },
		{ "count": 3, "weights": "pmc_commander.json", "spawn": "random", "posture": "defend", "delay_s": 30 },
		{ "post": { "kind": "window" } },
		{ "post": { "pos": [1] } }
	])")), &Warnings);
	if (!TestEqual(TEXT("valid entries kept"), Squads.Num(), 5))
	{
		return false;
	}
	TestEqual(TEXT("a post without pos / with a one-number pos is reported"), Warnings.Num(), 2);

	const FPmcEncounterSpec& Window = Squads[0];
	TestTrue(TEXT("the post is read"), Window.bHasPost);
	TestEqual(TEXT("post id"), Window.Post.Id, FString(TEXT("PMC_01")));
	TestEqual(TEXT("post kind"), Window.Post.Kind, FString(TEXT("window")));
	TestEqual(TEXT("post pos"), Window.Post.Pos, FVector(5640.f, -767.f, 0.f));
	TestEqual(TEXT("post yaw"), Window.Post.YawDeg, 90.f);
	TestFalse(TEXT("a post replaces the random ring"), Window.bRandomSpawn);
	TestEqual(TEXT("the fixed spot is the post"), Window.FixedSpot, Window.Post.Pos);
	TestEqual(TEXT("posture defend"), static_cast<int32>(Window.Posture), static_cast<int32>(EPmcPosture::Defend));
	TestEqual(TEXT("no delay"), Window.DelaySeconds, 0.f);
	TestEqual(TEXT("count"), Window.Count, 2);

	const FPmcEncounterSpec& Hunt = Squads[1];
	TestEqual(TEXT("a hunt post keeps its posture"), static_cast<int32>(Hunt.Posture), static_cast<int32>(EPmcPosture::Hunt));
	TestEqual(TEXT("... and its delay_s"), Hunt.DelaySeconds, 120.f);
	TestEqual(TEXT("fractional yaw"), Hunt.Post.YawDeg, -125.5f);

	const FPmcEncounterSpec& Roof = Squads[2];
	TestEqual(TEXT("roof post z"), Roof.Post.Pos, FVector(-2100.f, 1250.f, 600.f));
	TestEqual(TEXT("no yaw = 0"), Roof.Post.YawDeg, 0.f);
	TestEqual(TEXT("a defend post is there at the start whatever its delay_s says"), Roof.DelaySeconds, 0.f);

	const FPmcEncounterSpec& Bare = Squads[3];
	TestEqual(TEXT("a post without posture is a defence"), static_cast<int32>(Bare.Posture), static_cast<int32>(EPmcPosture::Defend));
	TestEqual(TEXT("a two-number pos = z 0"), Bare.Post.Pos, FVector(10.f, 20.f, 0.f));
	TestEqual(TEXT("no delay_s on a post = 0, not the default"), Bare.DelaySeconds, 0.f);

	// Squads without a post behave exactly as before.
	const FPmcEncounterSpec& Plain = Squads[4];
	TestFalse(TEXT("no post"), Plain.bHasPost);
	TestTrue(TEXT("random spawn"), Plain.bRandomSpawn);
	TestEqual(TEXT("its delay"), Plain.DelaySeconds, 30.f);
	TestEqual(TEXT("... and min"), Plain.MinCm, 4000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPostSpawnPlacementTest, "CodexTactics.AI.PmcEncounter.PostSpawnPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPostSpawnPlacementTest::RunTest(const FString&)
{
	FPmcEncounterPost Post;
	Post.Pos = FVector(1000.f, -500.f, 600.f);
	const FPmcPostSpawnConfig Config; // the defaults mirror pmc_operators.json post_spawn (checked in PostSpawnConfig)

	// The first man is ON the post, whatever the yaw; the last resort is the post; deterministic.
	for (const float Yaw : { -125.f, 0.f, 90.f, 270.f })
	{
		Post.YawDeg = Yaw;
		const TArray<FVector> First = PmcEncounterRules::PostSlots(Post, 0, Config);
		if (!TestTrue(TEXT("slots exist"), First.Num() > 0))
		{
			return false;
		}
		TestEqual(TEXT("man 0 is on the post"), First[0], Post.Pos);
		TestEqual(TEXT("the post itself is the last resort"), PmcEncounterRules::PostSlots(Post, 2, Config).Last(), Post.Pos);
		TestEqual(TEXT("deterministic"), PmcEncounterRules::PostSlots(Post, 2, Config), PmcEncounterRules::PostSlots(Post, 2, Config));
	}

	// A firing line across the facing (facing +Y here: the line runs along X, right hand = -X), alternating right / left.
	Post.YawDeg = 90.f;
	const FVector Man1 = PmcEncounterRules::PostSlots(Post, 1, Config)[0];
	const FVector Man2 = PmcEncounterRules::PostSlots(Post, 2, Config)[0];
	const FVector Man3 = PmcEncounterRules::PostSlots(Post, 3, Config)[0];
	TestTrue(TEXT("man 1 one step to the right"), Man1.Equals(Post.Pos + FVector(-Config.LineSpacingCm, 0.f, 0.f), 0.01f));
	TestTrue(TEXT("man 2 one step to the left"), Man2.Equals(Post.Pos + FVector(Config.LineSpacingCm, 0.f, 0.f), 0.01f));
	TestTrue(TEXT("man 3 two steps to the right"), Man3.Equals(Post.Pos + FVector(-2.f * Config.LineSpacingCm, 0.f, 0.f), 0.01f));
	const TArray<FVector> Slots1 = PmcEncounterRules::PostSlots(Post, 1, Config);
	TestTrue(TEXT("the first rank is behind man 1's slot"), Slots1[1].Equals(Man1 - FVector(0.f, Config.RankSpacingCm, 0.f), 0.01f));
	TestEqual(TEXT("a slot, RankCount ranks, 8 ring points, the post"), Slots1.Num(), 1 + Config.RankCount + 8 + 1);
	for (const FVector& Slot : PmcEncounterRules::PostSlots(Post, 4, Config))
	{
		TestEqual(TEXT("a slot keeps the post's height"), static_cast<float>(Slot.Z), 600.f);
	}

	// A squad of six: six different places.
	TSet<FIntPoint> Firsts;
	for (int32 Man = 0; Man < 6; ++Man)
	{
		const FVector Slot = PmcEncounterRules::PostSlots(Post, Man, Config)[0];
		Firsts.Add(FIntPoint(FMath::RoundToInt(Slot.X), FMath::RoundToInt(Slot.Y)));
	}
	TestEqual(TEXT("six men, six places"), Firsts.Num(), 6);

	// The spacing comes from the config, not the code.
	FPmcPostSpawnConfig Wide = Config;
	Wide.LineSpacingCm = 400.f;
	TestTrue(TEXT("a wider line"), PmcEncounterRules::PostSlots(Post, 2, Wide)[0].Equals(Post.Pos + FVector(400.f, 0.f, 0.f), 0.01f));

	// Facing = the post's yaw (normalised), level.
	for (const float Yaw : { 0.f, 90.f, -125.f, 270.f, 540.f })
	{
		Post.YawDeg = Yaw;
		const FRotator Facing = PmcEncounterRules::PostFacing(Post);
		TestEqual(TEXT("facing yaw = the post yaw"), static_cast<float>(FRotator::NormalizeAxis(static_cast<float>(Facing.Yaw) - Yaw)), 0.f, 0.01f);
		TestEqual(TEXT("no pitch"), static_cast<float>(Facing.Pitch), 0.f);
		TestEqual(TEXT("no roll"), static_cast<float>(Facing.Roll), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPostSpawnConfigTest, "CodexTactics.AI.PmcEncounter.PostSpawnConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPostSpawnConfigTest::RunTest(const FString&)
{
	using namespace PmcPostSpawnTestLocal;
	// A missing block = the defaults; the keys override; a bad value is clamped.
	const FPmcPostSpawnConfig Defaults;
	const TSharedPtr<FJsonObject> None = PostObjectFromText(TEXT(R"({ "stats": {} })"));
	if (!TestTrue(TEXT("parsed"), None.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("no block: the default spacing"), PmcEncounterRules::PostSpawnConfigFromJson(*None).LineSpacingCm, Defaults.LineSpacingCm);

	const TSharedPtr<FJsonObject> Json = PostObjectFromText(
		TEXT(R"({ "post_spawn": { "line_spacing_cm": 220, "rank_spacing_cm": 80, "rank_count": 99, "ring_cm": -5 } })"));
	if (!TestTrue(TEXT("parsed"), Json.IsValid()))
	{
		return false;
	}
	const FPmcPostSpawnConfig Read = PmcEncounterRules::PostSpawnConfigFromJson(*Json);
	TestEqual(TEXT("line spacing"), Read.LineSpacingCm, 220.f);
	TestEqual(TEXT("rank spacing"), Read.RankSpacingCm, 80.f);
	TestEqual(TEXT("rank count clamped to 6"), Read.RankCount, 6);
	TestEqual(TEXT("a negative ring is 0"), Read.RingCm, 0.f);

	// The shipped file carries the block and its values equal the code defaults (one source of truth).
	const FPmcPostSpawnConfig Shipped = PmcEncounterRules::LoadPostSpawnConfig();
	TestEqual(TEXT("pmc_operators.json line spacing"), Shipped.LineSpacingCm, Defaults.LineSpacingCm);
	TestEqual(TEXT("pmc_operators.json rank spacing"), Shipped.RankSpacingCm, Defaults.RankSpacingCm);
	TestEqual(TEXT("pmc_operators.json rank count"), Shipped.RankCount, Defaults.RankCount);
	TestEqual(TEXT("pmc_operators.json ring"), Shipped.RingCm, Defaults.RingCm);
	TestEqual(TEXT("a missing file = the defaults"), PmcEncounterRules::LoadPostSpawnConfig(TEXT("Z:/no/such/file.json")).LineSpacingCm, Defaults.LineSpacingCm);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
