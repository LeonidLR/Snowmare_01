// UE-only (no Godot reference): the enemy-appear hitch gate (004) and the world preload's public state queries.
#include "Misc/AutomationTest.h"
#include "Core/GameplayPreloadSubsystem.h"
#include "Core/HitchGateRules.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitchGateSpawnWindowTest, "CodexTactics.Core.HitchGate.SpawnWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitchGateSpawnWindowTest::RunTest(const FString&)
{
	const TArray<uint64> Spawns = { 100, 200 };
	TestTrue(TEXT("the spawn frame itself"), HitchGateRules::IsInSpawnWindow(100, Spawns, 10));
	TestTrue(TEXT("the last frame of the window"), HitchGateRules::IsInSpawnWindow(110, Spawns, 10));
	TestFalse(TEXT("one frame past the window"), HitchGateRules::IsInSpawnWindow(111, Spawns, 10));
	TestFalse(TEXT("a frame before the spawn"), HitchGateRules::IsInSpawnWindow(99, Spawns, 10));
	TestTrue(TEXT("the second spawn's window"), HitchGateRules::IsInSpawnWindow(205, Spawns, 10));
	TestTrue(TEXT("window 0 = the spawn frame only"), HitchGateRules::IsInSpawnWindow(200, Spawns, 0));
	TestFalse(TEXT("window 0 excludes the next frame"), HitchGateRules::IsInSpawnWindow(201, Spawns, 0));
	TestFalse(TEXT("no spawns, no window"), HitchGateRules::IsInSpawnWindow(100, {}, 10));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitchGateSummaryTest, "CodexTactics.Core.HitchGate.Summary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitchGateSummaryTest::RunTest(const FString&)
{
	FHitchGateLimits Limits; // 250 / 50 / 33 ms, window 10, report-only slow budget
	const TArray<FHitchFrameSample> Samples = {
		{ 90, 120.0 },  // outside every window (before the first spawn): only the "all frames" summary sees it
		{ 100, 40.0 },  // spawn frame: slow, under the spawn gate
		{ 101, 20.0 },
		{ 110, 34.0 },  // last window frame: slow
		{ 111, 60.0 },  // outside: not part of the spawn gate
		{ 200, 16.0 },
	};
	const TArray<uint64> Spawns = { 100, 200 };
	const FHitchWindowStats Window = HitchGateRules::Summarize(Samples, Spawns, Limits);
	TestEqual(TEXT("frames inside the windows"), Window.Frames, 4);
	TestEqual(TEXT("slowest window frame"), Window.MaxMs, 40.0);
	TestTrue(TEXT("its frame number"), Window.MaxFrame == 100);
	TestEqual(TEXT("frames > 33 ms in the windows"), Window.SlowFrames, 2);
	TestTrue(TEXT("40 ms passes the 50 ms spawn gate (slow frames report only)"), HitchGateRules::PassesSpawnGate(Window, Limits));

	const FHitchWindowStats All = HitchGateRules::Summarize(Samples, {}, Limits, true);
	TestEqual(TEXT("all frames"), All.Frames, 6);
	TestEqual(TEXT("slowest of all"), All.MaxMs, 120.0);
	TestEqual(TEXT("slow frames of all"), All.SlowFrames, 4);

	FHitchGateLimits Strict = Limits;
	Strict.MaxSlowSpawnFrames = 1;
	TestFalse(TEXT("a slow-frame budget of 1 fails 2 slow frames"), HitchGateRules::PassesSpawnGate(Window, Strict));
	Strict.MaxSlowSpawnFrames = 2;
	TestTrue(TEXT("a budget of 2 keeps them"), HitchGateRules::PassesSpawnGate(Window, Strict));
	Strict.SpawnMaxFrameMs = 39.0;
	TestFalse(TEXT("a 39 ms gate fails the 40 ms frame"), HitchGateRules::PassesSpawnGate(Window, Strict));
	TestFalse(TEXT("no measured window frame never passes"), HitchGateRules::PassesSpawnGate(FHitchWindowStats(), Limits));

	FHitchWindowStats WaveWindow;
	WaveWindow.Frames = 11;
	WaveWindow.MaxMs = 85.0;
	TestTrue(TEXT("the wave gate is report-only by default"), HitchGateRules::PassesWaveGate(WaveWindow, Limits));
	FHitchGateLimits WaveGated = Limits;
	WaveGated.WaveMaxFrameMs = 50.0;
	TestFalse(TEXT("an 85 ms wave frame fails a 50 ms wave gate"), HitchGateRules::PassesWaveGate(WaveWindow, WaveGated));
	WaveWindow.MaxMs = 44.0;
	TestTrue(TEXT("a 44 ms wave frame keeps it"), HitchGateRules::PassesWaveGate(WaveWindow, WaveGated));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitchGateConsoleTest, "CodexTactics.Core.HitchGate.ConsoleDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHitchGateConsoleTest::RunTest(const FString&)
{
	const FHitchGateLimits Defaults;
	const FHitchGateLimits FromConsole = HitchGateRules::FromConsole();
	TestEqual(TEXT("hard limit default"), FromConsole.HardLimitMs, Defaults.HardLimitMs);
	TestEqual(TEXT("spawn gate default"), FromConsole.SpawnMaxFrameMs, Defaults.SpawnMaxFrameMs);
	TestEqual(TEXT("slow frame default"), FromConsole.SlowFrameMs, Defaults.SlowFrameMs);
	TestEqual(TEXT("window default"), FromConsole.SpawnWindowFrames, Defaults.SpawnWindowFrames);
	TestEqual(TEXT("slow budget default"), FromConsole.MaxSlowSpawnFrames, Defaults.MaxSlowSpawnFrames);
	TestEqual(TEXT("wave gate default"), FromConsole.WaveMaxFrameMs, Defaults.WaveMaxFrameMs);
	IConsoleVariable* Gate = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.HitchProbe.SpawnMaxFrameMs"));
	TestNotNull(TEXT("Codex.HitchProbe.SpawnMaxFrameMs registered"), Gate);
	if (Gate)
	{
		const float Before = Gate->GetFloat();
		Gate->Set(40.f, ECVF_SetByCode);
		TestEqual(TEXT("the console variable drives the limit"), HitchGateRules::FromConsole().SpawnMaxFrameMs, 40.0);
		Gate->Set(Before, ECVF_SetByCode);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameplayPreloadStateTest, "CodexTactics.Core.GameplayPreload.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameplayPreloadStateTest::RunTest(const FString&)
{
	TestTrue(TEXT("no world: not applicable"), UGameplayPreloadSubsystem::GetState(nullptr) == EGameplayPreloadState::NotApplicable);
	TestTrue(TEXT("no world: ready (nothing to wait for)"), UGameplayPreloadSubsystem::IsGameplayReady(nullptr));
	TestTrue(TEXT("no world: released"), UGameplayPreloadSubsystem::IsGameplayReleased(nullptr));
	TestFalse(TEXT("no world: not preparing"), UGameplayPreloadSubsystem::IsPreparing(nullptr));
	TestEqual(TEXT("state names"), UGameplayPreloadSubsystem::StateName(EGameplayPreloadState::Failed), FString(TEXT("failed")));
	TestEqual(TEXT("state names"), UGameplayPreloadSubsystem::StateName(EGameplayPreloadState::Ready), FString(TEXT("ready")));
	IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Preload.Enabled"));
	TestNotNull(TEXT("Codex.Preload.Enabled registered"), Enabled);
	TestTrue(TEXT("the preload is on by default"), Enabled && Enabled->GetInt() == 1);
	TestTrue(TEXT("the world is paused while preparing by default (Codex.Preload.WorldPause 1)"), UGameplayPreloadSubsystem::UsesWorldPause());
	if (Enabled && !FString(FCommandLine::Get()).Contains(TEXT("CodexPreload=")))
	{
		Enabled->Set(0, ECVF_SetByCode);
		TestFalse(TEXT("Codex.Preload.Enabled 0 switches it off"), UGameplayPreloadSubsystem::IsPreloadEnabled());
		Enabled->Set(1, ECVF_SetByCode);
		TestTrue(TEXT("Codex.Preload.Enabled 1 switches it on"), UGameplayPreloadSubsystem::IsPreloadEnabled());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
