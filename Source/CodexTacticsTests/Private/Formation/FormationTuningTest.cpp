// CodexTactics.Formation.Tuning.* - formation_config.json (Formation/FormationTuning; contract docs/design/FORMATION_CONTRACT.md "Units").
// The file is owned by the Training Ground simulator session; the FFormationConfig defaults must mirror it. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationTuning.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_TUNING_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Tuning." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FORMATION_TUNING_TEST(FFormationTuningDefaultsMirrorFileTest, "DefaultsMirrorFile")
bool FFormationTuningDefaultsMirrorFileTest::RunTest(const FString&)
{
	const FString Path = FormationTuning::GetDefaultPath();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		AddWarning(FString::Printf(TEXT("%s not found: the defaults cannot be compared yet"), *Path));
		return true;
	}
	TMap<FString, float> Values;
	TMap<FString, FVector2D> Ranges;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("The file parses (%s)"), *Error), FormationTuning::ParseConfigFile(Text, Values, &Ranges, &Error)))
	{
		return false;
	}
	const FFormationConfig Defaults;
	const TArray<FString> Known = FormationTuning::KnownKeys();
	for (const FString& Key : Known)
	{
		const float* FileValue = Values.Find(Key);
		float Default = 0.f;
		FormationTuning::GetValue(Defaults, Key, Default);
		if (!TestNotNull(FString::Printf(TEXT("The file has %s"), *Key), FileValue))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("Default %s = %g mirrors the file's %g"), *Key, Default, *FileValue), FMath::IsNearlyEqual(Default, *FileValue, 1e-4f));
		const FVector2D* Range = Ranges.Find(Key);
		TestTrue(FString::Printf(TEXT("%s has min <= value <= max"), *Key), Range && Range->X <= *FileValue && *FileValue <= Range->Y);
	}
	for (const TPair<FString, float>& Pair : Values)
	{
		TestTrue(FString::Printf(TEXT("The struct knows the file's key %s"), *Pair.Key), Known.Contains(Pair.Key));
	}
	TestEqual(TEXT("51 keys in the struct (contract v1.1)"), Known.Num(), 51);
	return true;
}

FORMATION_TUNING_TEST(FFormationTuningParseTest, "ParseClampAndUnknownKeys")
bool FFormationTuningParseTest::RunTest(const FString&)
{
	const FString Json = TEXT("{\"schema\":\"codex.formation_config/1\",\"params\":{")
		TEXT("\"pace.patrol\":{\"value\":5,\"min\":0.5,\"max\":1.0,\"unit\":\"ratio\",\"doc\":\"\"},")
		TEXT("\"slot.side_offset\":{\"value\":300,\"min\":150,\"max\":400},")
		TEXT("\"nope.key\":{\"value\":1,\"min\":0,\"max\":2},")
		TEXT("\"broken\":{\"min\":0}}}");
	TMap<FString, float> Values;
	TMap<FString, FVector2D> Ranges;
	TestTrue(TEXT("Parses"), FormationTuning::ParseConfigFile(Json, Values, &Ranges));
	TestEqual(TEXT("Three numeric params"), Values.Num(), 3);
	TArray<FString> Unknown;
	const FFormationConfig Config = FormationTuning::FromValues(Values, FFormationConfig(), &Ranges, &Unknown);
	TestEqual(TEXT("pace.patrol clamped to its max"), Config.PacePatrol, 1.f);
	TestEqual(TEXT("slot.side_offset read"), Config.SlotSideOffset, 300.f);
	TestEqual(TEXT("A missing key keeps the default"), Config.SlotBackOffset, FFormationConfig().SlotBackOffset);
	TestTrue(TEXT("The unknown key is reported"), Unknown.Num() == 1 && Unknown[0] == TEXT("nope.key"));

	TMap<FString, float> Ignored;
	FString Error;
	TestFalse(TEXT("Another schema is refused"), FormationTuning::ParseConfigFile(TEXT("{\"schema\":\"x\",\"params\":{}}"), Ignored, nullptr, &Error));
	TestFalse(TEXT("Not JSON is refused"), FormationTuning::ParseConfigFile(TEXT("nope"), Ignored));

	// Round trip through the key table.
	FFormationConfig Changed;
	Changed.WallEndDeg = 80.f;
	Changed.ClassifyHoldTime = 2.f;
	const FFormationConfig Back = FormationTuning::FromValues(FormationTuning::ToValues(Changed));
	TestTrue(TEXT("ToValues / FromValues round trip"), Back.WallEndDeg == 80.f && Back.ClassifyHoldTime == 2.f);
	return true;
}

#undef FORMATION_TUNING_TEST

#endif
