#include "Core/GameplayPreloadSubsystem.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayPreloadProgressTest, "CodexTactics.Core.GameplayPreload.Progress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayPreloadProgressTest::RunTest(const FString&)
{
	FGameplayPreloadProgress Progress;
	Progress.State = EGameplayPreloadState::Preparing;
	TestTrue(TEXT("preparing is visible before requests exist"), Progress.IsVisible());
	TestEqual(TEXT("zero total is safe"), Progress.GetPercent(), 0);
	Progress.CompletedPaths = 11; Progress.TotalPaths = 22;
	TestEqual(TEXT("initial batch half complete"), Progress.GetPercent(), 50);
	Progress.CompletedPaths = 22;
	TestEqual(TEXT("initial paths complete before dependency discovery"), Progress.GetPercent(), 100);
	Progress.TotalPaths += 624;
	TestEqual(TEXT("discovered dependencies increase denominator, no invented progress"), Progress.GetPercent(), 3);
	Progress.CompletedPaths = 323;
	TestEqual(TEXT("combined batches half complete"), Progress.GetPercent(), 50);
	Progress.CompletedPaths = 646;
	TestEqual(TEXT("all paths complete"), Progress.GetPercent(), 100);
	TestTrue(TEXT("path completion alone does not dismiss final preparation"), Progress.IsVisible());
	Progress.CompletedPaths = -1;
	TestEqual(TEXT("negative completed count bounded"), Progress.GetPercent(), 0);
	Progress.CompletedPaths = MAX_int64; Progress.TotalPaths = 100;
	TestEqual(TEXT("excess completed count bounded"), Progress.GetPercent(), 100);
	Progress.TotalPaths = MAX_int64;
	TestEqual(TEXT("percent does not overflow at maximum counts"), Progress.GetPercent(), 100);
	Progress.CompletedPaths = MAX_int64 / 2;
	TestEqual(TEXT("large counts retain ratio"), Progress.GetPercent(), 50);
	Progress.TotalPaths = -1;
	TestEqual(TEXT("negative total is safe"), Progress.GetPercent(), 0);
	for (EGameplayPreloadState State : { EGameplayPreloadState::Ready, EGameplayPreloadState::Failed,
		EGameplayPreloadState::Disabled, EGameplayPreloadState::NotApplicable })
	{
		Progress.State = State;
		TestFalse(FString::Printf(TEXT("%s hides overlay regardless of counts"), *UGameplayPreloadSubsystem::StateName(State)), Progress.IsVisible());
	}
	TestFalse(TEXT("null world never shows overlay"), UGameplayPreloadSubsystem::GetProgress(nullptr).IsVisible());
	return true;
}
#endif
