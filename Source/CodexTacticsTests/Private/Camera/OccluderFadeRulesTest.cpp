// CodexTactics.Camera.OccluderFade.* - a wall that hides the cursor's target fades (the game designer's play-test 2026-11-01, approved
// fix c; UOccluderFadeSubsystem / OccluderFadeRules): which pieces fade, the hide test, the opacity ease, the restore hysteresis.
// Godot reference: none (UE-only).

#include "Camera/OccluderFadeSubsystem.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define OCCLUDER_FADE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Camera.OccluderFade." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

OCCLUDER_FADE_TEST(FOccluderFadeKindTest, "FadeableKind")
bool FOccluderFadeKindTest::RunTest(const FString&)
{
	using namespace OccluderFadeRules;
	FOccluderTraits Wall;
	Wall.bStaticMesh = true;
	TestTrue(TEXT("a plain static mesh wall fades"), IsFadeableKind(Wall));
	for (const TPair<const TCHAR*, bool FOccluderTraits::*>& Each : TArray<TPair<const TCHAR*, bool FOccluderTraits::*>>{
		{ TEXT("a pawn"), &FOccluderTraits::bPawn }, { TEXT("terrain"), &FOccluderTraits::bTerrain }, { TEXT("foliage"), &FOccluderTraits::bFoliage },
		{ TEXT("an instanced mesh"), &FOccluderTraits::bInstanced }, { TEXT("NoOccluderFade"), &FOccluderTraits::bOptOut },
		{ TEXT("an interactable (barricade / crate)"), &FOccluderTraits::bInteractable } })
	{
		FOccluderTraits Other = Wall;
		Other.*(Each.Value) = true;
		TestFalse(FString::Printf(TEXT("%s never fades"), Each.Key), IsFadeableKind(Other));
	}
	FOccluderTraits Hidden = Wall;
	Hidden.bVisible = false;
	TestFalse(TEXT("an invisible piece (or one a roof cutaway took) is left alone"), IsFadeableKind(Hidden));
	FOccluderTraits Shape;
	TestFalse(TEXT("no static mesh (a trigger box) never fades"), IsFadeableKind(Shape));
	TestEqual(TEXT("the opt-out tag"), OptOutTag(), FName(TEXT("NoOccluderFade")));
	return true;
}

OCCLUDER_FADE_TEST(FOccluderFadeHidesTargetTest, "HidesTarget")
bool FOccluderFadeHidesTargetTest::RunTest(const FString&)
{
	using namespace OccluderFadeRules;
	const FOccluderFadeConfig Config;
	TestEqual(TEXT("~10 Hz"), Config.UpdateIntervalSeconds, 0.1f);
	TestEqual(TEXT("at most 6 at once"), Config.MaxFaded, 6);
	// The camera 2000 cm from a crouched man behind a 3 m wall (feet at z 0): the wall's face hit 1900 cm along the ray, its top at 300.
	TestTrue(TEXT("a 3 m wall in front of him hides him"), HidesTarget(1900.f, 2000.f, 300.f, 0.f, 40.f, Config));
	TestFalse(TEXT("a hit at the target itself (his own wall face / ground) is no occluder"), HidesTarget(1970.f, 2000.f, 300.f, 0.f, 40.f, Config));
	TestFalse(TEXT("a piece behind him is no occluder"), HidesTarget(2100.f, 2000.f, 300.f, 0.f, 40.f, Config));
	TestFalse(TEXT("a 60 cm kerb / low cover never fades"), HidesTarget(1900.f, 2000.f, 60.f, 0.f, 40.f, Config));
	TestTrue(TEXT("a 1 m wall just counts"), HidesTarget(1900.f, 2000.f, 100.f, 0.f, 40.f, Config));
	TestFalse(TEXT("a floor slab (wide both ways) never fades"), HidesTarget(1900.f, 2000.f, 300.f, 0.f, 12000.f, Config));
	TestTrue(TEXT("a 30 m long wall (its short side is its thickness) fades"), HidesTarget(1900.f, 2000.f, 300.f, 0.f, 40.f, Config));
	return true;
}

OCCLUDER_FADE_TEST(FOccluderFadeOpacityTest, "OpacityEaseAndRestore")
bool FOccluderFadeOpacityTest::RunTest(const FString&)
{
	using namespace OccluderFadeRules;
	const FOccluderFadeConfig Config;
	TestEqual(TEXT("fading in starts solid"), Opacity(0.f, true, Config), 1.f);
	TestTrue(TEXT("half way in"), FMath::IsNearlyEqual(Opacity(0.5f * Config.FadeSeconds, true, Config), 0.5f * (1.f + Config.FadedOpacity), 1e-4f));
	TestEqual(TEXT("fully faded after FadeSeconds"), Opacity(Config.FadeSeconds, true, Config), Config.FadedOpacity);
	TestEqual(TEXT("held faded"), Opacity(10.f, true, Config), Config.FadedOpacity);
	TestEqual(TEXT("fading out starts faded"), Opacity(0.f, false, Config), Config.FadedOpacity);
	TestEqual(TEXT("back to solid"), Opacity(Config.FadeSeconds, false, Config), 1.f);
	TestTrue(TEXT("faded is see-through, not invisible"), Config.FadedOpacity > 0.f && Config.FadedOpacity < 0.6f);
	TestFalse(TEXT("hid a target just now: stays"), ShouldRestore(10.0, 10.1, Config));
	TestTrue(TEXT("no target behind it for the delay: back"), ShouldRestore(10.0, 10.0 + Config.RestoreDelaySeconds, Config));
	return true;
}

#undef OCCLUDER_FADE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
