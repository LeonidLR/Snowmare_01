// CodexTactics.UI.HordeArrows.* - the red "the horde comes from there" edge arrows (game designer 2026-10-25; UE-only): grouping the
// wave's enemies by bearing from the squad, the engaged (close) cut-off and keeping the arrow tips apart.

#include "Misc/AutomationTest.h"
#include "UI/HordeArrowRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define HORDE_ARROW_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.UI.HordeArrows." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HORDE_ARROW_TEST(FHordeArrowGroupsTest, "Groups")
bool FHordeArrowGroupsTest::RunTest(const FString&)
{
	using namespace HordeArrowRules;
	const FVector Squad(1000.f, 1000.f, 0.f);
	TestEqual(TEXT("nobody: no group"), GroupByBearing(Squad, {}).Num(), 0);

	// Four to the north (+Y, bearing 90) spread +-3 m, two to the east (+X, bearing 0): two approach directions.
	TArray<FVector> Enemies;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Enemies.Add(Squad + FVector(-300.f + 200.f * Index, 6000.f + 150.f * Index, 0.f));
	}
	Enemies.Add(Squad + FVector(5000.f, 100.f, 0.f));
	Enemies.Add(Squad + FVector(5400.f, -200.f, 0.f));
	const TArray<FGroup> Groups = GroupByBearing(Squad, Enemies);
	if (!TestEqual(TEXT("two lanes: two groups"), Groups.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("ordered by the nearest member: the east pair first"), Groups[0].Count, 2);
	TestTrue(FString::Printf(TEXT("east bearing ~0 (%.1f)"), Groups[0].BearingDeg), FMath::Abs(Groups[0].BearingDeg) < 5.f);
	TestTrue(FString::Printf(TEXT("east nearest ~50 m (%.0f)"), Groups[0].NearestCm), FMath::Abs(Groups[0].NearestCm - 5000.f) < 20.f);
	TestEqual(TEXT("the north four"), Groups[1].Count, 4);
	TestTrue(FString::Printf(TEXT("north bearing ~90 (%.1f)"), Groups[1].BearingDeg), FMath::Abs(Groups[1].BearingDeg - 90.f) < 5.f);
	TestTrue(TEXT("the centroid lies among the members"), Groups[1].Centroid.Y > Squad.Y + 6000.f && Groups[1].Centroid.Y < Squad.Y + 6500.f);

	// One lane in a chain (each 20 deg apart from the next) is ONE group; a gap of 35+ deg splits.
	TArray<FVector> Chain;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(20.f * Index);
		Chain.Add(Squad + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 4000.f);
	}
	TestEqual(TEXT("a chain of close bearings: one group"), GroupByBearing(Squad, Chain).Num(), 1);
	Chain.Add(Squad + FVector(-4000.f, 0.f, 0.f));
	TestEqual(TEXT("... plus the opposite side: two"), GroupByBearing(Squad, Chain).Num(), 2);

	// The seam at +-180 deg: bearings 170 and -170 are 20 deg apart = one group, mean ~180.
	TArray<FVector> Seam = { Squad + FVector(-4000.f * FMath::Cos(FMath::DegreesToRadians(10.f)), 4000.f * FMath::Sin(FMath::DegreesToRadians(10.f)), 0.f),
		Squad + FVector(-4000.f * FMath::Cos(FMath::DegreesToRadians(10.f)), -4000.f * FMath::Sin(FMath::DegreesToRadians(10.f)), 0.f) };
	const TArray<FGroup> SeamGroups = GroupByBearing(Squad, Seam);
	TestEqual(TEXT("across the +-180 seam: one group"), SeamGroups.Num(), 1);
	TestTrue(TEXT("... facing west"), SeamGroups.Num() == 1 && FMath::Abs(FMath::Abs(SeamGroups[0].BearingDeg) - 180.f) < 1.f);

	// Engaged enemies (within the hide range) give no arrow; the far ones still do.
	TArray<FVector> Mixed = { Squad + FVector(500.f, 0.f, 0.f), Squad + FVector(0.f, 1700.f, 0.f), Squad + FVector(0.f, -4000.f, 0.f) };
	const TArray<FGroup> Far = GroupByBearing(Squad, Mixed);
	TestEqual(TEXT("two engaged ones dropped, one far group left"), Far.Num(), 1);
	TestTrue(TEXT("... to the south"), Far.Num() == 1 && FMath::Abs(Far[0].BearingDeg + 90.f) < 1.f && Far[0].Count == 1);
	TestEqual(TEXT("all engaged: none"), GroupByBearing(Squad, { Squad + FVector(300.f, 0.f, 0.f) }).Num(), 0);

	TestTrue(TEXT("angle delta wraps"), FMath::IsNearlyEqual(AngleDeltaDeg(170.f, -170.f), 20.f) && FMath::IsNearlyEqual(AngleDeltaDeg(-170.f, 170.f), -20.f));
	return true;
}

HORDE_ARROW_TEST(FHordeArrowSpreadTest, "SpreadTips")
bool FHordeArrowSpreadTest::RunTest(const FString&)
{
	using namespace HordeArrowRules;
	const FVector2D View(1920.f, 1080.f);
	// Two arrows on the same right-edge spot, plus a guide arrow already there: all three end up >= the gap apart, inside the margin.
	TArray<FVector2D> Tips = { FVector2D(1872.f, 540.f), FVector2D(1872.f, 540.f) };
	const TArray<FVector2D> Directions = { FVector2D(1.f, 0.f), FVector2D(1.f, 0.f) };
	SpreadTips(View, 48.f, 56.f, { FVector2D(1872.f, 540.f) }, Directions, Tips);
	TestTrue(TEXT("clear of the guide arrow"), FVector2D::Distance(Tips[0], FVector2D(1872.f, 540.f)) >= 55.f);
	TestTrue(TEXT("clear of each other"), FVector2D::Distance(Tips[0], Tips[1]) >= 55.f);
	TestTrue(TEXT("on the edge, in the view"), Tips[0].X == 1872.f && Tips[1].X == 1872.f && Tips[0].Y >= 48.f && Tips[1].Y <= 1032.f);
	// Apart already: untouched.
	TArray<FVector2D> Apart = { FVector2D(1872.f, 200.f), FVector2D(48.f, 800.f) };
	SpreadTips(View, 48.f, 56.f, {}, { FVector2D(1.f, 0.f), FVector2D(-1.f, 0.f) }, Apart);
	TestTrue(TEXT("separate arrows stay put"), Apart[0].Equals(FVector2D(1872.f, 200.f)) && Apart[1].Equals(FVector2D(48.f, 800.f)));
	return true;
}

#undef HORDE_ARROW_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
