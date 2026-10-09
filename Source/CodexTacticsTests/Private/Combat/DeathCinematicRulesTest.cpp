#include "Misc/AutomationTest.h"
#include "Combat/DeathCinematicRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Death cinematic timing (user request 2026-10-08; UE-only): slow motion never stacks with the pause dilation, the
// focus lasts until the death clip has played, the commander's defeat fades to black and shows the line.

#define DEATHCAM_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.DeathCinematic." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DEATHCAM_TEST(FDeathCinematicDilationTest, "SlowMoNoStacking")
bool FDeathCinematicDilationTest::RunTest(const FString&)
{
	const FDeathCinematicConfig Config;
	FDeathFocusTimes Times;
	Times.RealSeconds = 0.5f;
	TestEqual(TEXT("Real time slows to 0.3"), DeathCinematicRules::FocusDilation(Config, Times, 1.f), 0.3f);
	TestEqual(TEXT("Tactical pause (0.02) is not slowed further"), DeathCinematicRules::FocusDilation(Config, Times, 0.02f), 0.02f);
	TestEqual(TEXT("Game over stays stopped"), DeathCinematicRules::FocusDilation(Config, Times, 0.f), 0.f);
	Times.RealSeconds = Config.SlowMoRealSeconds + 0.01f;
	TestEqual(TEXT("After the slow motion time resumes"), DeathCinematicRules::FocusDilation(Config, Times, 1.f), 1.f);
	Times.RealSeconds = 0.1f;
	Times.bSlowMo = false;
	TestEqual(TEXT("A queued death adds no second slow motion"), DeathCinematicRules::FocusDilation(Config, Times, 1.f), 1.f);
	return true;
}

DEATHCAM_TEST(FDeathCinematicFocusTest, "FocusLastsTheClip")
bool FDeathCinematicFocusTest::RunTest(const FString&)
{
	const FDeathCinematicConfig Config;
	FDeathFocusTimes Times;
	Times.ClipSeconds = 0.87f; // Knocked_Back
	Times.RealSeconds = 1.f;
	Times.GameSeconds = 0.3f;
	TestFalse(TEXT("Slow motion still running"), DeathCinematicRules::IsFocusFinished(Config, Times));
	Times.RealSeconds = 2.1f;
	Times.GameSeconds = 0.63f; // 2 s at 0.3x + a frame
	TestFalse(TEXT("Clip not over yet (0.63 < 0.87 + 0.4)"), DeathCinematicRules::IsFocusFinished(Config, Times));
	Times.GameSeconds = 1.3f;
	TestTrue(TEXT("Clip played + hold: the camera returns"), DeathCinematicRules::IsFocusFinished(Config, Times));
	FDeathFocusTimes Long;
	Long.ClipSeconds = 30.f;
	Long.RealSeconds = Config.MaxFocusRealSeconds;
	TestTrue(TEXT("Capped by the real-time maximum"), DeathCinematicRules::IsFocusFinished(Config, Long));
	FDeathFocusTimes Queued;
	Queued.bSlowMo = false;
	Queued.ClipSeconds = 0.5f;
	Queued.RealSeconds = 1.f;
	Queued.GameSeconds = 1.f;
	TestTrue(TEXT("A queued focus needs no slow motion"), DeathCinematicRules::IsFocusFinished(Config, Queued));
	Queued.RealSeconds = 0.2f;
	TestFalse(TEXT("...but the camera has to arrive first"), DeathCinematicRules::IsFocusFinished(Config, Queued));
	return true;
}

DEATHCAM_TEST(FDeathCinematicFadeTest, "DefeatFade")
bool FDeathCinematicFadeTest::RunTest(const FString&)
{
	const FDeathCinematicConfig Config;
	TestEqual(TEXT("No black during a focus"), DeathCinematicRules::FadeAlpha(Config, EDeathCinematicPhase::Focus, 1.f), 0.f);
	TestEqual(TEXT("Half-way through the fade"), DeathCinematicRules::FadeAlpha(Config, EDeathCinematicPhase::DefeatFade, Config.DefeatFadeSeconds * 0.5f), 0.5f);
	TestEqual(TEXT("Black under the text"), DeathCinematicRules::FadeAlpha(Config, EDeathCinematicPhase::DefeatText, 0.f), 1.f);
	TestFalse(TEXT("No line while fading"), DeathCinematicRules::ShowsDefeatText(EDeathCinematicPhase::DefeatFade));
	TestTrue(TEXT("The line on black"), DeathCinematicRules::ShowsDefeatText(EDeathCinematicPhase::DefeatText));
	TestEqual(TEXT("Mission-failed screen: overlay gone"), DeathCinematicRules::FadeAlpha(Config, EDeathCinematicPhase::Done, 5.f), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
