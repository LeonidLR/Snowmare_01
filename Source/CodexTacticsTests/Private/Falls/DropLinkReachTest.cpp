#include "Misc/AutomationTest.h"
#include "Falls/DropLinkReach.h"
#include "Falls/FallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// The soft-lock after a roof-hole drop (user bug 2026-11-03, L_Gen_Depot_1; UE-only, no Godot reference; docs/systems/stances-and-movement.md
// "Falls and drops", "The soft-lock"): FReachGraph - which navmesh nodes still reach the main area, and which one-way drop links are worth keeping
// (never one into a dead-end pocket; a chain of drops whose last one reaches the main area is kept).

#define DROP_REACH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Falls.Reach." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace DropReachTestLocal
{
	/** Two-way walkable edge (neighbouring polys). */
	void Walk(FReachGraph& Graph, int32 A, int32 B)
	{
		Graph.AddEdge(A, B);
		Graph.AddEdge(B, A);
	}
}

DROP_REACH_TEST(FDropReachSolveTest, "Solve")
bool FDropReachSolveTest::RunTest(const FString&)
{
	using namespace DropReachTestLocal;
	// 0-1-2 the main area (anchor 0); 3-4 a roof reached by a one-way drop from it (1 -> 3: walking off a ledge INTO the pocket) and left by
	// nothing; 5 an island with no link at all.
	FReachGraph Graph;
	Graph.Reset(6);
	Walk(Graph, 0, 1);
	Walk(Graph, 1, 2);
	Walk(Graph, 3, 4);
	Graph.AddEdge(1, 3);
	Graph.Solve({ 0 });
	TestTrue(TEXT("the anchor reaches"), Graph.Reaches(0));
	TestTrue(TEXT("the main area reaches the anchor"), Graph.Reaches(1) && Graph.Reaches(2));
	TestFalse(TEXT("the pocket reached by a one-way drop does not"), Graph.Reaches(3) || Graph.Reaches(4));
	TestFalse(TEXT("an island does not"), Graph.Reaches(5));
	TestEqual(TEXT("3 reaching nodes"), Graph.NumReaching(), 3);
	TestFalse(TEXT("an unknown node does not"), Graph.Reaches(17) || Graph.Reaches(INDEX_NONE));
	// A stair built later (4 <-> 2): the pocket joins the main area at once.
	Walk(Graph, 4, 2);
	TestTrue(TEXT("an edge added after Solve spreads"), Graph.Reaches(3) && Graph.Reaches(4));
	return true;
}

DROP_REACH_TEST(FDropReachSelectLinksTest, "SelectLinks")
bool FDropReachSelectLinksTest::RunTest(const FString&)
{
	using namespace DropReachTestLocal;
	// The user's case in small: 0-1 the ground (anchor 0); 2-3 the roof (+2, reached from the ground by a stair 1 <-> 2); 4-5 a room under the
	// roof whose only way out is a doorless wall - a pocket; 6 a lower roof (+1) left only by a drop onto the ground; 7 an upper roof left only
	// by a drop onto that lower roof (a chain).
	FReachGraph Graph;
	Graph.Reset(8);
	Walk(Graph, 0, 1);
	Walk(Graph, 1, 2);
	Walk(Graph, 2, 3);
	Walk(Graph, 4, 5);
	Graph.Solve({ 0 });
	TestFalse(TEXT("the room is a pocket before the links"), Graph.Reaches(4));
	TestFalse(TEXT("the lower roof does not reach yet"), Graph.Reaches(6));
	const TArray<FReachGraph::FCandidate> Candidates = {
		{ 3, 4 },            // 0: roof -> through a hole into the pocket room: REJECTED
		{ 7, 6 },            // 1: upper roof -> lower roof: kept once 2 is (listed first on purpose)
		{ 6, 0 },            // 2: lower roof -> ground: kept
		{ 3, 6 },            // 3: roof -> lower roof: kept
		{ 3, INDEX_NONE },   // 4: a landing off the navmesh: rejected
		{ 5, 1 },            // 5: out of the pocket room onto the ground: kept (it lands in the main area)
		{ INDEX_NONE, 0 } }; // 6: its start poly not found, it lands in the main area: kept (judged by its end)
	const TArray<bool> Kept = Graph.SelectLinks(Candidates);
	TestEqual(TEXT("a verdict per candidate"), Kept.Num(), Candidates.Num());
	// Candidate 5 (a way out of the room) makes the room reach - and only then the hole drop into it is safe: kept too.
	TestTrue(TEXT("the drop out of the room is kept"), Kept[5]);
	TestTrue(TEXT("the hole drop into the room is kept once the room has a way out"), Kept[0]);
	TestTrue(TEXT("the chain's first drop is kept"), Kept[1]);
	TestTrue(TEXT("the chain's last drop is kept"), Kept[2]);
	TestTrue(TEXT("roof -> lower roof is kept"), Kept[3]);
	TestFalse(TEXT("a landing off the navmesh is never kept"), Kept[4]);
	TestTrue(TEXT("an unknown start is judged by its end"), Kept[6]);
	TestTrue(TEXT("the upper roof reaches over the chain"), Graph.Reaches(7));

	// Without the way out the hole drop is rejected (the soft-lock).
	FReachGraph Pocket;
	Pocket.Reset(6);
	Walk(Pocket, 0, 1);
	Walk(Pocket, 1, 2);
	Walk(Pocket, 2, 3);
	Walk(Pocket, 4, 5);
	Pocket.Solve({ 0 });
	const TArray<bool> PocketKept = Pocket.SelectLinks({ { 3, 4 }, { 2, 5 } });
	TestFalse(TEXT("no drop into a dead-end room"), PocketKept[0] || PocketKept[1]);
	TestFalse(TEXT("the room still does not reach"), Pocket.Reaches(4) || Pocket.Reaches(5));
	return true;
}

DROP_REACH_TEST(FDropReachRescueTest, "Rescue")
bool FDropReachRescueTest::RunTest(const FString&)
{
	using namespace FallRules;
	const FFallConfig Config;
	// The watch: a bad standing for rescue.after_s inside rescue.watch_s of the landing.
	TestFalse(TEXT("fine: never"), ShouldRescue(ENavStanding::Fine, 99.f, 1.f, Config));
	TestFalse(TEXT("off the navmesh, not long enough"), ShouldRescue(ENavStanding::OffNavmesh, Config.RescueAfterSeconds - 0.1f, 3.f, Config));
	TestTrue(TEXT("off the navmesh long enough"), ShouldRescue(ENavStanding::OffNavmesh, Config.RescueAfterSeconds, 3.f, Config));
	TestTrue(TEXT("a dead-end pocket long enough"), ShouldRescue(ENavStanding::DeadEnd, Config.RescueAfterSeconds + 1.f, 5.f, Config));
	TestFalse(TEXT("after the watch window: no"), ShouldRescue(ENavStanding::OffNavmesh, 99.f, Config.RescueWatchSeconds + 1.f, Config));

	// The point: his level first (the nearest reaching one), else the nearest reaching one at all, never past rescue.max_cm.
	const FVector Feet(0.f, 0.f, 300.f);
	TArray<FRescueCandidate> Candidates = {
		{ FVector(100.f, 0.f, 300.f), false },    // 0: nearest, his level, but a dead end
		{ FVector(400.f, 0.f, 310.f), true },     // 1: his level, reaches
		{ FVector(0.f, 50.f, 600.f), true },      // 2: nearer in 2D, the roof above - not his level
		{ FVector(250.f, 0.f, 360.f), true } };   // 3: his level (60 cm up: the tolerance is 80), reaches, nearer than 1
	TestEqual(TEXT("the nearest reaching point of his level"), PickRescuePoint(Feet, Candidates, Config), 3);
	Candidates.RemoveAt(3);
	TestEqual(TEXT("... the next one"), PickRescuePoint(Feet, Candidates, Config), 1);
	Candidates.RemoveAt(1);
	TestEqual(TEXT("no reaching point on his level: none (never a teleport up / down a floor)"), PickRescuePoint(Feet, Candidates, Config), static_cast<int32>(INDEX_NONE));
	TArray<FRescueCandidate> Far = { { FVector(Config.RescueMaxCm + 100.f, 0.f, 300.f), true }, { FVector(50.f, 0.f, 300.f), false } };
	TestEqual(TEXT("too far / dead ends only: none"), PickRescuePoint(Feet, Far, Config), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no candidates: none"), PickRescuePoint(Feet, {}, Config), static_cast<int32>(INDEX_NONE));
	return true;
}

DROP_REACH_TEST(FDropReachCommitTest, "CommittedToDrop")
bool FDropReachCommitTest::RunTest(const FString&)
{
	using namespace FallRules;
	const FFallConfig Config;
	// The generator's hole link: from 70 cm outside a 2.4 m roof hole (z 600) down to its centre on the floor below (z 300).
	const FVector Start(-190.f, 0.f, 600.f);
	const FVector End(0.f, 0.f, 300.f);
	const FVector Below(400.f, 300.f, 310.f); // his path's goal on the floor below
	TestTrue(TEXT("at its start"), IsCommittedToDrop(FVector(-200.f, 10.f, 602.f), Below, Start, End, Config));
	TestTrue(TEXT("either order of the end points"), IsCommittedToDrop(FVector(-200.f, 10.f, 602.f), Below, End, Start, Config));
	TestTrue(TEXT("on the rim, past the start (the soft-lock spot)"), IsCommittedToDrop(FVector(-98.f, 11.f, 596.f), Below, Start, End, Config));
	TestTrue(TEXT("within link.commit_radius_cm of the start"), IsCommittedToDrop(FVector(-190.f - Config.LinkCommitRadiusCm + 5.f, 0.f, 600.f), Below, Start, End, Config));
	TestFalse(TEXT("farther off"), IsCommittedToDrop(FVector(-190.f - Config.LinkCommitRadiusCm - 50.f, 0.f, 600.f), Below, Start, End, Config));
	TestFalse(TEXT("already down on the lower level"), IsCommittedToDrop(FVector(-20.f, 0.f, 302.f), Below, Start, End, Config));
	TestFalse(TEXT("a link that hardly drops (a step / a ramp link)"), IsCommittedToDrop(FVector(-190.f, 0.f, 600.f), Below, Start, FVector(0.f, 0.f, 520.f), Config));
	TestFalse(TEXT("his path's goal on his own level: the path only brushes the hole"), IsCommittedToDrop(FVector(-200.f, 10.f, 602.f), FVector(900.f, 0.f, 605.f), Start, End, Config));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
