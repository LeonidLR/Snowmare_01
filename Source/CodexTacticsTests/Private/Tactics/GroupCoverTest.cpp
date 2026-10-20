// CodexTactics.Tactics.GroupCover.* - the group cover order (game designer 2026-10-20, Tactics/GroupCoverRules): the others take the
// nearest free slots along the same wall shoulder to shoulder, who does not fit goes prone behind the cover, the assignment never
// crosses paths. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Tactics/GroupCoverRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define GROUP_COVER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.GroupCover." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroupCoverTestUtil
{
	TArray<EGroupCoverProbe> FreeProbes(int32 Count)
	{
		TArray<EGroupCoverProbe> Probes;
		Probes.Init(EGroupCoverProbe::Free, Count);
		return Probes;
	}
}

GROUP_COVER_TEST(FGroupCoverLongWallTest, "LongWallEnoughSlots")
bool FGroupCoverLongWallTest::RunTest(const FString&)
{
	const FGroupCoverConfig Config;
	TestEqual(TEXT("Probes per side: the others + 2"), GroupCoverRules::ProbesPerSide(2, Config), 4);
	TestEqual(TEXT("Probe 2 on the left: two spacings left"), GroupCoverRules::ProbeOffsetCm(-1, 2, Config), -2.f * Config.SlotSpacingCm);
	TestEqual(TEXT("Probe 1 on the right"), GroupCoverRules::ProbeOffsetCm(1, 1, Config), Config.SlotSpacingCm);

	// A long wall: every probe finds a free slot on both sides.
	const TArray<float> Free = GroupCoverRules::CollectFreeOffsets(GroupCoverTestUtil::FreeProbes(4), GroupCoverTestUtil::FreeProbes(4), Config);
	TestEqual(TEXT("8 free slots"), Free.Num(), 8);
	// Two others: the two nearest, one on each side of the clicked slot (shoulder to shoulder).
	const TArray<float> Two = GroupCoverRules::ChooseNearestOffsets(Free, 2, 1.f);
	TestTrue(TEXT("Two others: -100 / +100"), Two.Num() == 2 && FMath::IsNearlyEqual(Two[0], -100.f) && FMath::IsNearlyEqual(Two[1], 100.f));
	// Three others coming from the left: the tie at 200 goes to the left.
	const TArray<float> Three = GroupCoverRules::ChooseNearestOffsets(Free, 3, -1.f);
	TestTrue(TEXT("Three from the left: -200 / -100 / +100"), Three.Num() == 3 && FMath::IsNearlyEqual(Three[0], -200.f)
		&& FMath::IsNearlyEqual(Three[1], -100.f) && FMath::IsNearlyEqual(Three[2], 100.f));
	// Slots never overlap: neighbours a full spacing apart, none on the clicked slot.
	for (int32 Index = 0; Index < Three.Num(); ++Index)
	{
		TestTrue(TEXT("Not on the direct order's slot"), FMath::Abs(Three[Index]) >= Config.SlotSpacingCm - 0.5f);
		if (Index > 0)
		{
			TestTrue(TEXT("A spacing apart"), Three[Index] - Three[Index - 1] >= Config.SlotSpacingCm - 0.5f);
		}
	}
	// Plan: both get a slot, nobody prone.
	const TArray<FGroupCoverAssignment> Plan = GroupCoverRules::PlanAssignments({ FVector2D(-300.f, 400.f), FVector2D(300.f, 400.f) },
		{ FVector2D(-100.f, 45.f), FVector2D(100.f, 45.f) }, { FVector2D(0.f, 170.f) });
	TestTrue(TEXT("Left one -> left slot, right one -> right slot"), Plan.Num() == 2
		&& Plan[0].Kind == EGroupCoverSpotKind::Slot && Plan[0].Index == 0 && Plan[1].Kind == EGroupCoverSpotKind::Slot && Plan[1].Index == 1);
	return true;
}

GROUP_COVER_TEST(FGroupCoverShortCoverTest, "ShortCoverProneOverflow")
bool FGroupCoverShortCoverTest::RunTest(const FString&)
{
	const FGroupCoverConfig Config;
	// A 2 m barricade clicked near its left end: nothing on the left, one slot on the right, then its end.
	const TArray<float> Free = GroupCoverRules::CollectFreeOffsets({ EGroupCoverProbe::NoWall },
		{ EGroupCoverProbe::Free, EGroupCoverProbe::NoWall, EGroupCoverProbe::Free }, Config);
	TestTrue(TEXT("One slot (a gap ends the scan: nothing past it)"), Free.Num() == 1 && FMath::IsNearlyEqual(Free[0], 100.f));
	const TArray<float> Chosen = GroupCoverRules::ChooseNearestOffsets(Free, 2, 1.f);
	TestEqual(TEXT("Two others, one slot"), Chosen.Num(), 1);

	// The barricade spans -30 .. +130 cm from the clicked slot (both corners exposed): one prone spot behind it, inside it.
	const FVector2D Span = GroupCoverRules::ComputeCoverSpan(true, 30.f, true, 130.f, 0.f, 100.f, Config);
	TestTrue(TEXT("Span from the corners"), FMath::IsNearlyEqual(Span.X, -30.f) && FMath::IsNearlyEqual(Span.Y, 130.f));
	const TArray<FVector2D> One = GroupCoverRules::ComputeProneSpots(1, Span, 0, Config);
	TestTrue(TEXT("One prone spot right behind the clicked slot, ProneDepthCm off the wall"), One.Num() == 1
		&& FMath::IsNearlyEqual(One[0].X, 0.f) && FMath::IsNearlyEqual(One[0].Y, Config.ProneDepthCm));

	// The same barricade, nobody fits beside him (a 1.5 m barricade, clicked in its middle): both prone, side by side inside it.
	const FVector2D Small = GroupCoverRules::ComputeCoverSpan(true, 75.f, true, 75.f, 0.f, 0.f, Config);
	const TArray<FVector2D> Two = GroupCoverRules::ComputeProneSpots(2, Small, 2, Config);
	TestEqual(TEXT("Two spots + two spares"), Two.Num(), 4);
	if (Two.Num() == 4)
	{
		TestTrue(TEXT("Side by side, ProneSpacingCm apart, centred behind him"), FMath::IsNearlyEqual(Two[1].X - Two[0].X, Config.ProneSpacingCm)
			&& FMath::IsNearlyEqual(Two[0].X + Two[1].X, 0.f, 0.1f));
		TestTrue(TEXT("Both inside the cover"), Two[0].X > Small.X && Two[1].X < Small.Y);
		TestTrue(TEXT("Spares outside the row"), FMath::Abs(Two[2].X) > FMath::Abs(Two[1].X) && FMath::Abs(Two[3].X) > FMath::Abs(Two[1].X));
		TestTrue(TEXT("All behind the one at the wall (his slot is 45 cm off it)"), Two[0].Y >= 150.f && Two[3].Y >= 150.f);
	}
	// A cover narrower than the row: the row is centred on the cover.
	const TArray<FVector2D> Narrow = GroupCoverRules::ComputeProneSpots(3, FVector2D(-40.f, 80.f), 0, Config);
	TestTrue(TEXT("Narrow cover: the row centred on it"), Narrow.Num() == 3 && FMath::IsNearlyEqual(Narrow[1].X, 20.f));
	// A wide cover clicked near its right end: the row shifts inside the cover.
	const TArray<FVector2D> Shifted = GroupCoverRules::ComputeProneSpots(3, FVector2D(-400.f, 30.f), 0, Config);
	TestTrue(TEXT("Row kept inside the right end"), Shifted.Num() == 3 && Shifted[2].X <= 30.f - Config.ProneEdgeMarginCm + 0.1f);

	// Plan: two others, one slot, one prone spot - the one nearer the slot takes it, the other lies down.
	const TArray<FGroupCoverAssignment> Plan = GroupCoverRules::PlanAssignments({ FVector2D(0.f, 500.f), FVector2D(150.f, 300.f) },
		{ FVector2D(100.f, 45.f) }, { FVector2D(0.f, 170.f) });
	TestTrue(TEXT("Two members: one slot, one prone"), Plan.Num() == 2 && Plan[1].Kind == EGroupCoverSpotKind::Slot && Plan[1].Index == 0
		&& Plan[0].Kind == EGroupCoverSpotKind::Prone && Plan[0].Index == 0);
	// No place at all: prone with no spot -> INDEX_NONE (the controller then tries a spare).
	const TArray<FGroupCoverAssignment> None = GroupCoverRules::PlanAssignments({ FVector2D(0.f, 500.f) }, {}, {});
	TestTrue(TEXT("Nowhere to go"), None.Num() == 1 && None[0].Index == INDEX_NONE);
	return true;
}

GROUP_COVER_TEST(FGroupCoverCornerTest, "CornerClick")
bool FGroupCoverCornerTest::RunTest(const FString&)
{
	const FGroupCoverConfig Config;
	// The clicked slot is at the right corner (the direct-order operative takes it): the wall goes on only to the left.
	const TArray<float> Free = GroupCoverRules::CollectFreeOffsets(GroupCoverTestUtil::FreeProbes(4), { EGroupCoverProbe::NoWall }, Config);
	const TArray<float> Chosen = GroupCoverRules::ChooseNearestOffsets(Free, 2, 1.f);
	TestTrue(TEXT("Corner click: both others inward, -200 / -100 (the corner stays his)"), Chosen.Num() == 2
		&& FMath::IsNearlyEqual(Chosen[0], -200.f) && FMath::IsNearlyEqual(Chosen[1], -100.f));
	// An occupied slot (someone outside the order stands there) is skipped, the scan goes on past it.
	const TArray<float> Skip = GroupCoverRules::CollectFreeOffsets({ EGroupCoverProbe::Occupied, EGroupCoverProbe::Free, EGroupCoverProbe::Free },
		{ EGroupCoverProbe::NoWall }, Config);
	TestTrue(TEXT("Occupied -100 skipped"), Skip.Num() == 2 && FMath::IsNearlyEqual(Skip[0], -200.f) && FMath::IsNearlyEqual(Skip[1], -300.f));

	// Probe classification.
	TestTrue(TEXT("Nothing found"), GroupCoverRules::ClassifyProbe(false, true, 0.f, false, 0.f, false, Config) == EGroupCoverProbe::NoWall);
	TestTrue(TEXT("Another object"), GroupCoverRules::ClassifyProbe(true, false, 0.f, false, 0.f, false, Config) == EGroupCoverProbe::NoWall);
	TestTrue(TEXT("Pulled off its place by the navmesh"), GroupCoverRules::ClassifyProbe(true, true, 50.f, false, 0.f, false, Config) == EGroupCoverProbe::NoWall);
	TestTrue(TEXT("Too close to the end (body past it)"), GroupCoverRules::ClassifyProbe(true, true, 0.f, true, 20.f, false, Config) == EGroupCoverProbe::NoWall);
	TestTrue(TEXT("End far enough"), GroupCoverRules::ClassifyProbe(true, true, 10.f, true, 60.f, false, Config) == EGroupCoverProbe::Free);
	TestTrue(TEXT("Taken"), GroupCoverRules::ClassifyProbe(true, true, 0.f, false, 0.f, true, Config) == EGroupCoverProbe::Occupied);

	// Unknown ends: the span reaches the corner probes' range and past the farthest wall probe.
	const FVector2D Span = GroupCoverRules::ComputeCoverSpan(false, 0.f, false, 0.f, -400.f, 100.f, Config);
	TestTrue(TEXT("Unknown ends"), FMath::IsNearlyEqual(Span.X, -450.f) && FMath::IsNearlyEqual(Span.Y, Config.UnknownReachCm));
	return true;
}

GROUP_COVER_TEST(FGroupCoverNoCrossingTest, "MinimalTravelNoCrossing")
bool FGroupCoverNoCrossingTest::RunTest(const FString&)
{
	// Members approach from behind the wall (y > 0), the slots lie along it (y = 45): the left member takes the left slot.
	const TArray<FVector2D> Members{ FVector2D(250.f, 600.f), FVector2D(-250.f, 600.f), FVector2D(0.f, 900.f) };
	const TArray<FVector2D> Targets{ FVector2D(-200.f, 45.f), FVector2D(-100.f, 45.f), FVector2D(100.f, 45.f) };
	const TArray<int32> Result = GroupCoverRules::AssignMinimalTravel(Members, Targets);
	TestEqual(TEXT("Everyone assigned"), Result.Num(), 3);
	if (Result.Num() == 3)
	{
		TestEqual(TEXT("Right member -> the right slot"), Result[0], 2);
		TestTrue(TEXT("Left member -> a left slot"), Result[1] == 0 || Result[1] == 1);
		TestTrue(TEXT("Each target once"), Result[0] != Result[1] && Result[1] != Result[2] && Result[0] != Result[2]);
		// No two straight paths cross (segments member -> target).
		auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
		{
			auto Side = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R) { return FVector2D::CrossProduct(Q - P, R - P); };
			return Side(A, B, C) * Side(A, B, D) < 0.f && Side(C, D, A) * Side(C, D, B) < 0.f;
		};
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = I + 1; J < 3; ++J)
			{
				TestFalse(TEXT("Paths do not cross"), Cross(Members[I], Targets[Result[I]], Members[J], Targets[Result[J]]));
			}
		}
	}
	// More members than targets: the one farthest from all stays without one.
	const TArray<int32> Short = GroupCoverRules::AssignMinimalTravel({ FVector2D(0.f, 100.f), FVector2D(0.f, 5000.f) }, { FVector2D(0.f, 0.f) });
	TestTrue(TEXT("Near one gets it, far one none"), Short.Num() == 2 && Short[0] == 0 && Short[1] == INDEX_NONE);
	// Large groups use the greedy pairing, still one target each.
	TArray<FVector2D> Many;
	TArray<FVector2D> ManyTargets;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Many.Add(FVector2D(Index * 100.f, 500.f));
		ManyTargets.Add(FVector2D(Index * 100.f, 45.f));
	}
	const TArray<int32> Greedy = GroupCoverRules::AssignMinimalTravel(Many, ManyTargets);
	bool bStraight = Greedy.Num() == 10;
	for (int32 Index = 0; bStraight && Index < 10; ++Index)
	{
		bStraight = Greedy[Index] == Index;
	}
	TestTrue(TEXT("10 members: each to the slot in front of him"), bStraight);
	return true;
}

#undef GROUP_COVER_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
