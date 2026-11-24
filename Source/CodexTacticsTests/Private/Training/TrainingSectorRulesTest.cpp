#include "Training/TrainingSectorRules.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TrainingSectorTest
{
	const TCHAR* Sectors = TEXT(R"json({
		"map":"L_Gen_Depot_1_X6",
		"sectors":[{
			"id":5, "box_cm":{"min":[-1000,-2000],"max":[8000,5500]}, "z_cm":{"min":-100,"max":900},
			"starts":[{"pos":[0,0,0],"yaw":0},{"pos":[1000,1000,0],"yaw":25}],
			"pmc_posts":[{"id":"north","kind":"street","pos":[5000,0,0],"yaw":180},{"id":"east","kind":"yard","pos":[6000,1500,0],"yaw":200}],
			"pmc_squad":{"count":3,"weights":"pmc_commander.json","posture":"patrol"}
		}]
	})json");

	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingSectorDataTest, "CodexTactics.Training.Sector.Data",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrainingSectorDataTest::RunTest(const FString&)
{
	using namespace TrainingSectorTest;
	const TSharedPtr<FJsonObject> Json = Read(Sectors);
	if (!TestTrue(TEXT("fixture parsed"), Json.IsValid())) { return false; }
	TArray<FString> Errors;
	const TArray<FTrainingSector> Parsed = TrainingSectorRules::ParseSectors(*Json, Errors);
	if (!TestEqual(TEXT("one valid sector"), Parsed.Num(), 1)) { return false; }
	TestEqual(TEXT("no schema errors"), Errors.Num(), 0);
	const FTrainingSector& Sector = Parsed[0];
	TestEqual(TEXT("id preserved"), Sector.Id, 5);
	TestEqual(TEXT("the frozen box is in cm"), Sector.Area.GetSize(), FVector2D(9000, 7500));

	FTrainingSectorStart A;
	FTrainingSectorStart B;
	FPmcEncounterSpec SquadA;
	FPmcEncounterSpec SquadB;
	TrainingSectorRules::Pick(Sector, 91, A, SquadA);
	TrainingSectorRules::Pick(Sector, 91, B, SquadB);
	TestTrue(TEXT("seed repeats the start and the post"), A.Position == B.Position && SquadA.Post.Id == SquadB.Post.Id);
	TSet<FString> Posts;
	TArray<FVector> Starts;
	for (int32 Seed = 1; Seed <= 16; ++Seed)
	{
		TrainingSectorRules::Pick(Sector, Seed, B, SquadB);
		Posts.Add(SquadB.Post.Id);
		Starts.AddUnique(B.Position);
	}
	TestEqual(TEXT("seeds vary starts"), Starts.Num(), 2);
	TestEqual(TEXT("seeds vary posts"), Posts.Num(), 2);
	TestTrue(TEXT("the chosen post is the only scheduled squad spawn"), SquadA.bHasPost && !SquadA.bRandomSpawn
		&& SquadA.FixedSpot == SquadA.Post.Pos && SquadA.Count == 3 && SquadA.Posture == EPmcPosture::Patrol);

	FString Bad(Sectors);
	Bad.ReplaceInline(TEXT("[8000,5500]"), TEXT("[4000,5500]"));
	const TSharedPtr<FJsonObject> BadJson = Read(Bad);
	Errors.Reset();
	TestTrue(TEXT("reject a sector narrower than 60 m"), BadJson && TrainingSectorRules::ParseSectors(*BadJson, Errors).IsEmpty());
	TestEqual(TEXT("invalid sector is reported"), Errors.Num(), 1);

	TestEqual(TEXT("saved selection"), TrainingSectorRules::SavedId(*Read(TEXT(R"({"map":"L_Gen_Depot_1_X6","sector":5})"))), 5);
	TestEqual(TEXT("wrong map does not activate X6"), TrainingSectorRules::SavedId(*Read(TEXT(R"({"map":"Other","sector":5})"))), 0);
	TestEqual(TEXT("a console zero disables the sector"), TrainingSectorRules::ParseId(TEXT("0")).Get(-1), 0);
	TestFalse(TEXT("a console id cannot be a numeric prefix"), TrainingSectorRules::ParseId(TEXT("5x")).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrainingSectorClampTest, "CodexTactics.Training.Sector.Clamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrainingSectorClampTest::RunTest(const FString&)
{
	const FBox2D Box(FVector2D(-3000, -3000), FVector2D(3000, 3000));
	const FVector Goal(5200, -9900, 250);
	TestTrue(TEXT("outside goal is clamped in XY with pawn clearance, Z is unchanged"),
		TrainingSectorRules::Clamp(Box, Goal, 100.0).Equals(FVector(2900, -2900, 250)));
	TestTrue(TEXT("inside goal is unchanged"), TrainingSectorRules::Clamp(Box, FVector(400, 600, 250), 100.0).Equals(FVector(400, 600, 250)));
	TestFalse(TEXT("outside goal stays outside until clamped"), TrainingSectorRules::Contains(Box, Goal));
	return true;
}

#endif
