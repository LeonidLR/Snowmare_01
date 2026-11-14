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

#endif
