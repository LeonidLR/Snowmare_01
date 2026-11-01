#include "Misc/AutomationTest.h"
#include "Combat/AmbushSpotRules.h"
#include "Combat/CombatPressureRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Where a combat-lab ambush hides (game designer answer 1, 2026-11-01, docs/design/COMBAT_LAB.md): wall ends, doorways, authored points.

namespace AmbushSpotRulesTestLocal
{
	/** A wall from A to B, Thickness thick (as the formation lab layout writes it). */
	FPressureBox SpotWall(const FVector2D& A, const FVector2D& B, float Thickness = 40.f)
	{
		FPressureBox Box;
		Box.Center = (A + B) * 0.5;
		Box.AxisX = (B - A).GetSafeNormal();
		Box.HalfSize = FVector2D((B - A).Size() * 0.5, Thickness * 0.5);
		return Box;
	}

	int32 CountKind(const TArray<FAmbushSpot>& Spots, EAmbushSpotKind Kind)
	{
		int32 Count = 0;
		for (const FAmbushSpot& Spot : Spots)
		{
			Count += Spot.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	const FAmbushSpot* NearSpot(const TArray<FAmbushSpot>& Spots, const FVector2D& Point, float Within = 5.f)
	{
		return Spots.FindByPredicate([&Point, Within](const FAmbushSpot& Spot) { return FVector2D::Distance(Spot.Position, Point) <= Within; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushSpotWallEndsTest, "CodexTactics.Combat.AmbushSpots.WallEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushSpotWallEndsTest::RunTest(const FString&)
{
	using namespace AmbushSpotRulesTestLocal;
	FAmbushSpotParams Params;
	// One free wall along X from (0,0) to (1000,0): 2 ends x 2 faces.
	const TArray<FPressureBox> One = { SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)) };
	const TArray<FAmbushSpot> Spots = AmbushSpotRules::WallSpots(One, Params);
	TestEqual(TEXT("A free wall: 4 spots (2 ends x 2 faces)"), Spots.Num(), 4);
	TestEqual(TEXT("... all wall ends"), CountKind(Spots, EAmbushSpotKind::WallEnd), 4);
	// 100 cm inside the end, 260 cm off the 20 cm half thickness.
	TestNotNull(TEXT("East end, +Y face"), NearSpot(Spots, FVector2D(900.0, 280.0)));
	TestNotNull(TEXT("East end, -Y face"), NearSpot(Spots, FVector2D(900.0, -280.0)));
	TestNotNull(TEXT("West end, +Y face"), NearSpot(Spots, FVector2D(100.0, 280.0)));
	if (const FAmbushSpot* Spot = NearSpot(Spots, FVector2D(900.0, 280.0)))
	{
		TestEqual(TEXT("Its normal points off the face"), Spot->Normal.Y, 1.0, 0.001);
		TestEqual(TEXT("It hides behind box 0"), Spot->Box, 0);
		TestEqual(TEXT("Exactly the standoff from the wall"), CombatPressureRules::DistanceToBox(Spot->Position, One[0]), 260.f, 0.5f);
	}
	// A wall written along Y (the long side is the box's local Y) works the same.
	FPressureBox AlongY;
	AlongY.Center = FVector2D(0.0, 0.0);
	AlongY.HalfSize = FVector2D(20.0, 500.0);
	TestNotNull(TEXT("A Y-long box: spot beside its +Y end"), NearSpot(AmbushSpotRules::WallSpots({ AlongY }, Params), FVector2D(280.0, 400.0)));
	// Two walls meeting at a corner (W_E_13 / W_E_14): the joined ends give no spots.
	const TArray<FPressureBox> Corner = { SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)), SpotWall(FVector2D(1000.0, 0.0), FVector2D(1000.0, 1000.0)) };
	const TArray<FAmbushWallEnd> Ends = AmbushSpotRules::FreeEnds(Corner, Params);
	TestEqual(TEXT("A corner: only the two outer ends are free"), Ends.Num(), 2);
	TestNull(TEXT("... no spot at the corner"), NearSpot(AmbushSpotRules::WallSpots(Corner, Params), FVector2D(900.0, 280.0)));
	// A post / ladder rail shorter than the minimum is no wall to hide behind.
	TestEqual(TEXT("A 1 m post: no spots"), AmbushSpotRules::WallSpots({ SpotWall(FVector2D(0.0, 0.0), FVector2D(100.0, 0.0)) }, Params).Num(), 0);
	// The hiding clearance never exceeds the level's clearance and keeps 10 cm under the standoff.
	TestEqual(TEXT("Clearance: min(level 250, standoff 260 - 10)"), AmbushSpotRules::HideClearanceCm(250.f, Params), 250.f);
	Params.WallStandoffCm = 150.f;
	TestEqual(TEXT("A closer standoff lowers the hiding clearance"), AmbushSpotRules::HideClearanceCm(250.f, Params), 140.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushSpotDoorwaysTest, "CodexTactics.Combat.AmbushSpots.Doorways",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushSpotDoorwaysTest::RunTest(const FString&)
{
	using namespace AmbushSpotRulesTestLocal;
	FAmbushSpotParams Params;
	// Two collinear walls with a 200 cm doorway between x 1000 and 1200 (lane C of the lab).
	const TArray<FPressureBox> Gap = { SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)), SpotWall(FVector2D(1200.0, 0.0), FVector2D(2200.0, 0.0)) };
	const TArray<FAmbushWallEnd> Ends = AmbushSpotRules::FreeEnds(Gap, Params);
	TestEqual(TEXT("Four free ends"), Ends.Num(), 4);
	const TArray<FAmbushSpot> Spots = AmbushSpotRules::WallSpots(Gap, Params);
	TestEqual(TEXT("The two jambs: 4 doorway spots"), CountKind(Spots, EAmbushSpotKind::Doorway), 4);
	TestEqual(TEXT("The outer ends: 4 wall-end spots"), CountKind(Spots, EAmbushSpotKind::WallEnd), 4);
	const FAmbushSpot* Jamb = NearSpot(Spots, FVector2D(900.0, -280.0));
	TestTrue(TEXT("Beside the doorway behind the west jamb"), Jamb && Jamb->Kind == EAmbushSpotKind::Doorway);
	const FAmbushSpot* Outer = NearSpot(Spots, FVector2D(100.0, 280.0));
	TestTrue(TEXT("The far end is a plain wall end"), Outer && Outer->Kind == EAmbushSpotKind::WallEnd);
	float GapCm = 0.f;
	const FAmbushWallEnd* East = Ends.FindByPredicate([](const FAmbushWallEnd& End) { return End.Box == 0 && End.Sign == 1; });
	const FAmbushWallEnd* West = Ends.FindByPredicate([](const FAmbushWallEnd& End) { return End.Box == 1 && End.Sign == -1; });
	TestTrue(TEXT("The facing ends pair up"), East && West && AmbushSpotRules::IsDoorwayPair(*East, *West, Params, &GapCm));
	TestEqual(TEXT("... the gap is 200 cm"), GapCm, 200.f, 0.5f);
	// Too wide (the lab's 24 m "big gap"), too narrow, or side by side (parallel corridor walls) is no doorway.
	TestEqual(TEXT("A 24 m gap is open ground"), CountKind(AmbushSpotRules::WallSpots({ SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)),
		SpotWall(FVector2D(3400.0, 0.0), FVector2D(4400.0, 0.0)) }, Params), EAmbushSpotKind::Doorway), 0);
	TestEqual(TEXT("A 50 cm slit is no doorway"), CountKind(AmbushSpotRules::WallSpots({ SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)),
		SpotWall(FVector2D(1050.0, 0.0), FVector2D(2050.0, 0.0)) }, Params), EAmbushSpotKind::Doorway), 0);
	TestEqual(TEXT("Parallel walls side by side (a corridor) are no doorway"), CountKind(AmbushSpotRules::WallSpots({ SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)),
		SpotWall(FVector2D(0.0, 500.0), FVector2D(1000.0, 500.0)) }, Params), EAmbushSpotKind::Doorway), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushSpotCandidatesTest, "CodexTactics.Combat.AmbushSpots.Candidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushSpotCandidatesTest::RunTest(const FString&)
{
	using namespace AmbushSpotRulesTestLocal;
	FAmbushSpotParams Params;
	const TArray<FPressureBox> One = { SpotWall(FVector2D(0.0, 0.0), FVector2D(1000.0, 0.0)) };
	// An authored point on top of a wall-end spot wins it (one spot, authored); another authored point stands alone.
	const TArray<FAmbushSpot> Spots = AmbushSpotRules::Candidates(One, { FVector2D(930.0, 300.0), FVector2D(500.0, -2000.0) }, Params);
	TestEqual(TEXT("4 wall spots + 2 authored - 1 merged = 5"), Spots.Num(), 5);
	TestEqual(TEXT("Two authored"), CountKind(Spots, EAmbushSpotKind::Authored), 2);
	TestEqual(TEXT("Three wall ends left"), CountKind(Spots, EAmbushSpotKind::WallEnd), 3);
	TestNull(TEXT("The merged wall-end spot is gone"), NearSpot(Spots, FVector2D(900.0, 280.0)));
	// Bonuses rank the kinds (the selector's score: CombatPressureRules::ScoreSpot + KindBonus).
	TestTrue(TEXT("Authored > doorway > wall end"), AmbushSpotRules::KindBonus(EAmbushSpotKind::Authored, Params)
		> AmbushSpotRules::KindBonus(EAmbushSpotKind::Doorway, Params) && AmbushSpotRules::KindBonus(EAmbushSpotKind::Doorway, Params)
		> AmbushSpotRules::KindBonus(EAmbushSpotKind::WallEnd, Params));
	FPressureSpotInput Input;
	Input.Kind = EPressureSpawnKind::Ambush;
	Input.DistanceCm = 2200.f;
	Input.MinDistanceCm = 1400.f;
	Input.MaxDistanceCm = 3000.f;
	Input.AheadCos = 1.f;
	Input.bConcealed = true;
	const float Plain = CombatPressureRules::ScoreSpot(Input);
	Input.KindBonus = AmbushSpotRules::KindBonus(EAmbushSpotKind::Authored, Params);
	TestEqual(TEXT("The kind bonus adds to the ambush score"), CombatPressureRules::ScoreSpot(Input) - Plain, Params.AuthoredBonus, 0.001f);
	Input.bConcealed = false;
	TestTrue(TEXT("... but an unconcealed spot stays refused"), CombatPressureRules::ScoreSpot(Input) < -100.f);
	TestEqual(TEXT("Kind names"), FString(AmbushSpotRules::KindName(EAmbushSpotKind::Doorway)), FString(TEXT("doorway")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushSpotLabTest, "CodexTactics.Combat.AmbushSpots.LabLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushSpotLabTest::RunTest(const FString&)
{
	using namespace AmbushSpotRulesTestLocal;
	// The combat lab's walls (formation_lab_01.json) + towers (formation_lab_combat_01.json) as the subsystem reads them.
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Data/FormationLab");
	FString LabText;
	FString TowerText;
	TSharedPtr<FJsonObject> Lab;
	TSharedPtr<FJsonObject> Combat;
	if (!TestTrue(TEXT("The lab layouts load"), FFileHelper::LoadFileToString(LabText, *(Dir / TEXT("formation_lab_01.json")))
		&& FFileHelper::LoadFileToString(TowerText, *(Dir / TEXT("Combat/formation_lab_combat_01.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LabText), Lab) && Lab.IsValid()
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TowerText), Combat) && Combat.IsValid()))
	{
		return false;
	}
	TArray<FPressureBox> Boxes;
	for (const TSharedPtr<FJsonValue>& Value : Lab->GetArrayField(TEXT("walls")))
	{
		const TSharedPtr<FJsonObject> W = Value->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& A = W->GetArrayField(TEXT("a"));
		const TArray<TSharedPtr<FJsonValue>>& B = W->GetArrayField(TEXT("b"));
		Boxes.Add(SpotWall(FVector2D(A[0]->AsNumber(), A[1]->AsNumber()), FVector2D(B[0]->AsNumber(), B[1]->AsNumber()), static_cast<float>(W->GetNumberField(TEXT("thickness")))));
	}
	for (const TSharedPtr<FJsonValue>& Value : Combat->GetArrayField(TEXT("towers")))
	{
		const TSharedPtr<FJsonObject> T = Value->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& C = T->GetArrayField(TEXT("center"));
		const TArray<TSharedPtr<FJsonValue>>& S = T->GetArrayField(TEXT("size"));
		FPressureBox Box;
		Box.Center = FVector2D(C[0]->AsNumber(), C[1]->AsNumber());
		Box.HalfSize = FVector2D(S[0]->AsNumber() * 0.5, S[1]->AsNumber() * 0.5);
		Boxes.Add(Box);
	}
	TestEqual(TEXT("20 walls + 4 towers"), Boxes.Num(), 24);
	FLevelCombatConfig Level;
	FString Error;
	if (!TestTrue(TEXT("L_FormationLabCombat.json parses"), LevelJsonRules::LoadLevel(TEXT("L_FormationLabCombat.json"), Level, Error)))
	{
		return false;
	}
	const FCombatPressureConfig& Pressure = Level.Pressure;
	const TArray<FAmbushSpot> Spots = AmbushSpotRules::Candidates(Boxes, Pressure.AuthoredPoints, Pressure.Spots);
	const int32 Doorways = CountKind(Spots, EAmbushSpotKind::Doorway);
	const int32 Ends = CountKind(Spots, EAmbushSpotKind::WallEnd);
	AddInfo(FString::Printf(TEXT("lab hiding spots: %d wall ends, %d doorways, %d authored"), Ends, Doorways, CountKind(Spots, EAmbushSpotKind::Authored)));
	TestTrue(TEXT("The lab's gap runs give doorways (C 1-2-3 m, V2 4.5 / 6 m gaps: >= 16 spots)"), Doorways >= 16);
	TestTrue(TEXT("... and plenty of wall ends"), Ends >= 40);
	const FAmbushSpot* LaneC = NearSpot(Spots, FVector2D(-1600.0, -1220.0));
	TestTrue(TEXT("Beside lane C's 2 m doorway (W_C_6 west jamb, north face)"), LaneC && LaneC->Kind == EAmbushSpotKind::Doorway);
	TestNull(TEXT("No spot at the W_E_13 / W_E_14 corner"), NearSpot(Spots, FVector2D(200.0, 2780.0), 30.f));
	// Every authored point is a legal spawn: clear of every wall / tower by the level's clearance, inside the lab.
	TestEqual(TEXT("8 authored ambush points"), Pressure.AuthoredPoints.Num(), 8);
	for (const FVector2D& Point : Pressure.AuthoredPoints)
	{
		const float Clear = CombatPressureRules::Clearance(Point, Boxes);
		TestTrue(*FString::Printf(TEXT("Authored point %s: %.0f cm from the nearest wall (>= %.0f), inside the lab"), *Point.ToString(), Clear,
			Pressure.WallClearanceCm), Clear >= Pressure.WallClearanceCm && FMath::Abs(Point.X) <= 5750.0 && FMath::Abs(Point.Y) <= 5750.0);
		const FAmbushSpot* Spot = NearSpot(Spots, Point, 1.f);
		TestTrue(*FString::Printf(TEXT("Authored point %s is a candidate"), *Point.ToString()), Spot && Spot->Kind == EAmbushSpotKind::Authored);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
