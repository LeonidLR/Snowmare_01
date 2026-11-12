// Designer task 003: stealth selection, break conditions and detection/noise tuning. UE-only.
#include "Misc/AutomationTest.h"
#include "Characters/StealthModeRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthSelectionTest, "CodexTactics.Characters.StealthMode.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthSelectionTest::RunTest(const FString&)
{
	using StealthModeRules::EnableForSelection;
	TestFalse(TEXT("Empty selection"), EnableForSelection(0, 0));
	TestTrue(TEXT("Single normal operative enters"), EnableForSelection(1, 0));
	TestFalse(TEXT("Single stealthed operative exits"), EnableForSelection(1, 1));
	TestTrue(TEXT("Mixed group enters together"), EnableForSelection(3, 1));
	TestTrue(TEXT("Normal group enters together"), EnableForSelection(3, 0));
	TestFalse(TEXT("Entire stealthed group exits"), EnableForSelection(3, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthBreakTest, "CodexTactics.Characters.StealthMode.Break",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthBreakTest::RunTest(const FString&)
{
	using StealthModeRules::ShouldBreak;
	FStealthModeConfig Config;
	TestFalse(TEXT("Inactive mode stays inactive"), ShouldBreak(false, true, true, Config));
	TestFalse(TEXT("Walking does not break mode"), ShouldBreak(true, false, false, Config));
	TestTrue(TEXT("Firing breaks mode by default"), ShouldBreak(true, true, false, Config));
	TestTrue(TEXT("Sprint breaks mode"), ShouldBreak(true, false, true, Config));
	Config.bBreakOnFire = false;
	TestFalse(TEXT("Data can keep stealth on fire"), ShouldBreak(true, true, false, Config));
	TestTrue(TEXT("Sprint still breaks when fire does not"), ShouldBreak(true, true, true, Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthDetectionTest, "CodexTactics.Characters.StealthMode.Detection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthDetectionTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	FStealthModeConfig Config;
	TestEqual(TEXT("2000 cm sight becomes 800 cm"), 2000.f * DetectionRangeScale(true, Config), 800.f);
	TestEqual(TEXT("Suspicion buildup halves"), 0.2f * DetectionBuildupScale(true, Config), 0.1f);
	TestEqual(TEXT("Crouched footstep radius halves"), 300.f * FootstepNoiseScale(true, Config), 150.f);
	TestEqual(TEXT("Inactive range unchanged"), DetectionRangeScale(false, Config), 1.f);
	TestEqual(TEXT("Inactive buildup unchanged"), DetectionBuildupScale(false, Config), 1.f);
	TestEqual(TEXT("Inactive noise unchanged"), FootstepNoiseScale(false, Config), 1.f);
	Config.bEnabled = false;
	TestEqual(TEXT("Disabled range unchanged"), DetectionRangeScale(true, Config), 1.f);
	TestEqual(TEXT("Disabled buildup unchanged"), DetectionBuildupScale(true, Config), 1.f);
	TestEqual(TEXT("Disabled noise unchanged"), FootstepNoiseScale(true, Config), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthConfigTest, "CodexTactics.Characters.StealthMode.Config",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthConfigTest::RunTest(const FString&)
{
	using namespace StealthModeRules;
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetBoolField(TEXT("enabled"), false);
	Json->SetBoolField(TEXT("stealth_break_on_fire"), false);
	Json->SetNumberField(TEXT("detection_range_multiplier"), -2.0);
	Json->SetNumberField(TEXT("detection_buildup_multiplier"), 3.0);
	Json->SetStringField(TEXT("footstep_noise_multiplier"), TEXT("bad"));
	Json->SetNumberField(TEXT("visual_opacity"), 5.0);
	const FStealthModeConfig Read = FromJson(*Json);
	TestFalse(TEXT("Enabled parsed"), Read.bEnabled);
	TestFalse(TEXT("Break on fire parsed"), Read.bBreakOnFire);
	TestEqual(TEXT("Range clamps low"), Read.DetectionRangeMultiplier, 0.f);
	TestEqual(TEXT("Buildup clamps high"), Read.DetectionBuildupMultiplier, 1.f);
	TestEqual(TEXT("Malformed noise keeps default"), Read.FootstepNoiseMultiplier, 0.5f);
	TestEqual(TEXT("Opacity clamps high"), Read.VisualOpacity, 1.f);
	Json->SetBoolField(TEXT("enabled"), true);
	Json->SetNumberField(TEXT("fast_speed"), 320.0);
	Json->SetNumberField(TEXT("fast_detection_range_mult"), 0.6);
	Json->SetNumberField(TEXT("fast_buildup_mult"), 0.75);
	Json->SetNumberField(TEXT("look_lock_keep_s"), 2.0);
	Json->SetNumberField(TEXT("look_lock_fov_deg"), 100.0);
	Json->SetNumberField(TEXT("look_lock_switch_ratio"), 0.7);
	const FStealthModeConfig ReadV2 = FromJson(*Json);
	TestEqual(TEXT("Fast speed parsed"), ReadV2.FastSpeedCm, 320.f);
	TestEqual(TEXT("Fast range scale"), DetectionRangeScale(true, true, ReadV2), 0.6f);
	TestEqual(TEXT("Fast buildup scale"), DetectionBuildupScale(true, true, ReadV2), 0.75f);
	TestEqual(TEXT("Slow noise scale matches 2-arg"), FootstepNoiseScale(true, false, ReadV2), FootstepNoiseScale(true, ReadV2));
	TestEqual(TEXT("Fast noise scale unattenuated"), FootstepNoiseScale(true, true, ReadV2), 1.f);
	TestEqual(TEXT("Inactive 3-arg range scale"), DetectionRangeScale(false, true, ReadV2), 1.f);
	TestEqual(TEXT("Inactive 3-arg buildup scale"), DetectionBuildupScale(false, true, ReadV2), 1.f);
	FStealthModeConfig Disabled = ReadV2;
	Disabled.bEnabled = false;
	TestEqual(TEXT("Disabled 3-arg range scale"), DetectionRangeScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("Disabled 3-arg buildup scale"), DetectionBuildupScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("Disabled 3-arg noise scale"), FootstepNoiseScale(true, true, Disabled), 1.f);
	TestEqual(TEXT("2-arg range equals slow"), DetectionRangeScale(true, ReadV2), DetectionRangeScale(true, false, ReadV2));
	TestEqual(TEXT("2-arg buildup equals slow"), DetectionBuildupScale(true, ReadV2), DetectionBuildupScale(true, false, ReadV2));
	TestEqual(TEXT("Look lock keep parsed"), ReadV2.LookLockKeepSeconds, 2.0f);
	TestEqual(TEXT("Look lock fov parsed"), ReadV2.LookLockFovDeg, 100.0f);
	TestEqual(TEXT("Look lock switch ratio parsed"), ReadV2.LookLockSwitchRatio, 0.7f);
	TSharedRef<FJsonObject> ClampedJson = MakeShared<FJsonObject>();
	ClampedJson->SetNumberField(TEXT("fast_speed"), -50.0);
	ClampedJson->SetNumberField(TEXT("fast_detection_range_mult"), 2.5);
	ClampedJson->SetNumberField(TEXT("fast_buildup_mult"), -0.5);
	ClampedJson->SetNumberField(TEXT("look_lock_keep_s"), -3.0);
	ClampedJson->SetNumberField(TEXT("look_lock_fov_deg"), 360.0);
	ClampedJson->SetNumberField(TEXT("look_lock_switch_ratio"), 4.0);
	const FStealthModeConfig ClampedV2 = FromJson(*ClampedJson);
	TestEqual(TEXT("Fast speed clamps low"), ClampedV2.FastSpeedCm, 0.f);
	TestEqual(TEXT("Fast range clamps high"), ClampedV2.FastDetectionRangeMultiplier, 1.f);
	TestEqual(TEXT("Fast buildup clamps low"), ClampedV2.FastDetectionBuildupMultiplier, 0.f);
	TestEqual(TEXT("Look lock keep clamps low"), ClampedV2.LookLockKeepSeconds, 0.f);
	TestEqual(TEXT("Look lock fov clamps high"), ClampedV2.LookLockFovDeg, 180.f);
	TestEqual(TEXT("Look lock switch ratio clamps high"), ClampedV2.LookLockSwitchRatio, 1.f);
	FStealthModeConfig Base;
	Base.DetectionRangeMultiplier = 0.25f;
	TestEqual(TEXT("Missing keys preserve base"), FromJson(*MakeShared<FJsonObject>(), Base).DetectionRangeMultiplier, 0.25f);
	// The editable shipped data must parse; do not pin the designer's tuning to the defaults.
	FString Text;
	TSharedPtr<FJsonObject> File;
	TestTrue(TEXT("Shipped stealth data parses"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), File) && File.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthFormationRuleTest, "CodexTactics.Characters.StealthMode.FormationRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStealthFormationRuleTest::RunTest(const FString&)
{
	using StealthModeRules::IsBlockedByFormation;
	TestTrue(TEXT("Formation mode blocks stealth for non-guarding member"), IsBlockedByFormation(false, true, false));
	TestFalse(TEXT("Guarding member outside formation may enter stealth"), IsBlockedByFormation(false, true, true));
	TestFalse(TEXT("Solo mode allows stealth"), IsBlockedByFormation(true, true, false));
	TestFalse(TEXT("Solo/holding while guarding allows stealth"), IsBlockedByFormation(true, true, true));
	TestFalse(TEXT("Solo/holding without formation allows stealth"), IsBlockedByFormation(true, false, false));
	TestFalse(TEXT("Inactive formation does not block stealth"), IsBlockedByFormation(false, false, false));
	return true;
}

#endif
