// CodexTactics.Ladder.TraversalQueue.* - the ladder's lease + line (TTraversalQueue, LADDERS.md 7.1), shared since R17-1 by the squad's turn-taking
// climb onto / over a mantle obstacle (MANTLING.md 6.4): one holder, the others in the order they joined, stale waiters skipped, the spot rule.

#include "Misc/AutomationTest.h"
#include "GameFramework/Actor.h"
#include "Ladders/TraversalQueue.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TRAVERSAL_QUEUE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Ladder.TraversalQueue." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TraversalQueueTestUtil
{
	AActor* MakeClimber()
	{
		return NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient);
	}
}

TRAVERSAL_QUEUE_TEST(FTraversalQueueFifoTest, "OneAtATimeInOrder")
bool FTraversalQueueFifoTest::RunTest(const FString&)
{
	using namespace TraversalQueueTestUtil;
	AActor* A = MakeClimber();
	AActor* B = MakeClimber();
	AActor* C = MakeClimber();
	TSet<const AActor*> Waiting;
	auto IsWaiting = [&Waiting](const AActor* Who) { return Waiting.Contains(Who); };
	TTraversalQueue<AActor> Queue;
	TestTrue(TEXT("A takes the free traversal"), Queue.TryLease(A, IsWaiting));
	Queue.Join(B);
	Queue.Join(C);
	Queue.Join(B); // once only
	Waiting = { B, C };
	TestEqual(TEXT("two in line"), Queue.Length(IsWaiting), 2);
	TestEqual(TEXT("B is next (joined first)"), Queue.GetIndex(B, IsWaiting), 0);
	TestEqual(TEXT("C after him"), Queue.GetIndex(C, IsWaiting), 1);
	TestTrue(TEXT("at most one at a time: B cannot take it while A holds it"), !Queue.TryLease(B, IsWaiting) && Queue.IsBusyFor(B, IsWaiting));
	Queue.Release(A);
	TestTrue(TEXT("C cannot jump the line"), !Queue.TryLease(C, IsWaiting));
	TestTrue(TEXT("B goes when A is off"), Queue.TryLease(B, IsWaiting));
	Waiting.Remove(B);
	TestEqual(TEXT("B left the line"), Queue.GetIndex(B, IsWaiting), INDEX_NONE);
	TestEqual(TEXT("C is next now"), Queue.GetIndex(C, IsWaiting), 0);
	Queue.Release(C); // not the holder: nothing
	TestTrue(TEXT("a release by another man keeps B's lease"), Queue.GetHolder() == B);
	Queue.Release(B);
	TestTrue(TEXT("then C"), Queue.TryLease(C, IsWaiting));
	return true;
}

TRAVERSAL_QUEUE_TEST(FTraversalQueueStaleTest, "StaleWaitersSkipped")
bool FTraversalQueueStaleTest::RunTest(const FString&)
{
	using namespace TraversalQueueTestUtil;
	AActor* A = MakeClimber();
	AActor* B = MakeClimber();
	AActor* C = MakeClimber();
	TSet<const AActor*> Waiting = { A, B, C };
	auto IsWaiting = [&Waiting](const AActor* Who) { return Waiting.Contains(Who); };
	TTraversalQueue<AActor> Queue;
	Queue.Join(A);
	Queue.Join(B);
	Queue.Join(C);
	Waiting.Remove(A); // re-ordered / killed without leaving the line
	TestEqual(TEXT("A no longer counts"), Queue.Length(IsWaiting), 2);
	TestEqual(TEXT("B is next"), Queue.GetIndex(B, IsWaiting), 0);
	TestFalse(TEXT("A does not block B"), Queue.IsBusyFor(B, IsWaiting));
	Waiting.Add(A);
	TestEqual(TEXT("back in it, A keeps his old place (a man knocked down and up again)"), Queue.GetIndex(A, IsWaiting), 0);
	Queue.Leave(A);
	TestEqual(TEXT("leaving drops him"), Queue.GetIndex(A, IsWaiting), INDEX_NONE);
	Queue.Reset();
	TestTrue(TEXT("reset: nobody holds, nobody waits"), Queue.GetHolder() == nullptr && Queue.Length(IsWaiting) == 0);
	return true;
}

TRAVERSAL_QUEUE_TEST(FTraversalQueueSpotTest, "SpotRule")
bool FTraversalQueueSpotTest::RunTest(const FString&)
{
	// The ladder's numbers (ULadderClimbComponent QueueFirstSpotCm 90 / QueueSpacingCm 75) = mantle.json squad.queue_first_cm / queue_spacing_cm.
	TestEqual(TEXT("the next one 90 cm out"), TTraversalQueue<AActor>::SpotDistanceCm(0, 90.f, 75.f), 90.f);
	TestEqual(TEXT("the third 240 cm out"), TTraversalQueue<AActor>::SpotDistanceCm(2, 90.f, 75.f), 240.f);
	TestEqual(TEXT("no place: the first spot"), TTraversalQueue<AActor>::SpotDistanceCm(INDEX_NONE, 90.f, 75.f), 90.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
