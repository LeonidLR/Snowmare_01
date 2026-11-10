// CodexTactics.UI.HudLabels.* - the HUD's label switches (the game designer 2026-11-10, feat/injured-tiers-fullbody; HudLabelRules,
// Content/Data/UI/hud_labels.json): one switch per label kind, every default = the HUD as it was, the wound tag override.

#include "Misc/AutomationTest.h"
#include "UI/HudLabelRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLabelPlateTest, "CodexTactics.UI.HudLabels.OperativePlate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudLabelPlateTest::RunTest(const FString&)
{
	using namespace HudLabelRules;
	using CriticalWoundRules::EWoundTier;
	const FHudLabelConfig Defaults;
	// The defaults draw today's plates, character for character.
	TestEqual(TEXT("healthy"), ComposeOperativePlate(Defaults, TEXT("Commander"), TEXT("Standing"), false, EWoundTier::None), FString(TEXT("Commander · Standing")));
	TestEqual(TEXT("light wound"), ComposeOperativePlate(Defaults, TEXT("Commander"), TEXT("Standing"), false, EWoundTier::Light), FString(TEXT("Commander · Standing · WOUNDED")));
	TestEqual(TEXT("heavy wound"), ComposeOperativePlate(Defaults, TEXT("Medic"), TEXT("Crouching"), false, EWoundTier::Heavy), FString(TEXT("Medic · Crouching · BADLY WOUNDED")));
	TestEqual(TEXT("critical + frostbite"), ComposeOperativePlate(Defaults, TEXT("Engineer"), TEXT("Prone"), true, EWoundTier::Critical),
		FString(TEXT("Engineer · Prone · FROSTBITTEN · CRITICAL")));
	FHudLabelConfig NoWound = Defaults;
	NoWound.SetShown(EHudLabel::WoundTag, false);
	TestEqual(TEXT("wound_tag off: no WOUNDED"), ComposeOperativePlate(NoWound, TEXT("A"), TEXT("Standing"), false, EWoundTier::Light), FString(TEXT("A · Standing")));
	TestEqual(TEXT("wound_tag off: no BADLY WOUNDED"), ComposeOperativePlate(NoWound, TEXT("A"), TEXT("Standing"), false, EWoundTier::Heavy), FString(TEXT("A · Standing")));
	TestEqual(TEXT("wound_tag off: CRITICAL keeps (its own switch)"), ComposeOperativePlate(NoWound, TEXT("A"), TEXT("Prone"), false, EWoundTier::Critical),
		FString(TEXT("A · Prone · CRITICAL")));
	FHudLabelConfig TagsOnly = Defaults;
	TagsOnly.SetShown(EHudLabel::OperativeName, false);
	TagsOnly.SetShown(EHudLabel::OperativeStance, false);
	TestEqual(TEXT("name and stance off: the tag alone"), ComposeOperativePlate(TagsOnly, TEXT("A"), TEXT("Standing"), false, EWoundTier::Light), FString(TEXT("WOUNDED")));
	TestTrue(TEXT("nothing to show: empty (no plate drawn)"), ComposeOperativePlate(TagsOnly, TEXT("A"), TEXT("Standing"), false, EWoundTier::None).IsEmpty());
	// The console override Codex.HUD.WoundTags.
	FHudLabelConfig Off = ApplyWoundTagOverride(Defaults, 0);
	TestFalse(TEXT("override 0: wound tag off"), Off.IsShown(EHudLabel::WoundTag));
	TestFalse(TEXT("override 0: critical tag off"), Off.IsShown(EHudLabel::CriticalTag));
	TestTrue(TEXT("override 0: the rest untouched"), Off.IsShown(EHudLabel::OperativeName) && Off.IsShown(EHudLabel::EnemyHealth));
	const FHudLabelConfig On = ApplyWoundTagOverride(Off, 1);
	TestTrue(TEXT("override 1: both on"), On.IsShown(EHudLabel::WoundTag) && On.IsShown(EHudLabel::CriticalTag));
	const FHudLabelConfig Data = ApplyWoundTagOverride(NoWound, -1);
	TestTrue(TEXT("override -1: the data's"), !Data.IsShown(EHudLabel::WoundTag) && Data.IsShown(EHudLabel::CriticalTag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLabelEnemyTest, "CodexTactics.UI.HudLabels.EnemyPlate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudLabelEnemyTest::RunTest(const FString&)
{
	using namespace HudLabelRules;
	const FHudLabelConfig Defaults;
	TestTrue(TEXT("the defaults are the full plate"), Defaults.IsEnemyPlateDefault());
	// The defaults = AEnemyCharacter's plate before the switches: "%s%s\n%d/%d".
	TestEqual(TEXT("default plate"), ComposeEnemyLabel(Defaults, TEXT("Frost Hound"), TEXT(" BURNING ICED"), 40, 120), FString(TEXT("Frost Hound BURNING ICED\n40/120")));
	TestEqual(TEXT("no status"), ComposeEnemyLabel(Defaults, TEXT("Brute"), TEXT(""), 300, 300), FString(TEXT("Brute\n300/300")));
	FHudLabelConfig NoHealth = Defaults;
	NoHealth.SetShown(EHudLabel::EnemyHealth, false);
	TestFalse(TEXT("a switch off: not the default plate"), NoHealth.IsEnemyPlateDefault());
	TestEqual(TEXT("enemy_health off"), ComposeEnemyLabel(NoHealth, TEXT("Brute"), TEXT(" STUNNED"), 10, 300), FString(TEXT("Brute STUNNED")));
	FHudLabelConfig HealthOnly = Defaults;
	HealthOnly.SetShown(EHudLabel::EnemyNames, false);
	HealthOnly.SetShown(EHudLabel::EnemyStatus, false);
	TestEqual(TEXT("names + status off: the health line alone"), ComposeEnemyLabel(HealthOnly, TEXT("Brute"), TEXT(" STUNNED"), 10, 300), FString(TEXT("10/300")));
	FHudLabelConfig StatusOnly = HealthOnly;
	StatusOnly.SetShown(EHudLabel::EnemyStatus, true);
	StatusOnly.SetShown(EHudLabel::EnemyHealth, false);
	TestEqual(TEXT("the status alone, trimmed"), ComposeEnemyLabel(StatusOnly, TEXT("Brute"), TEXT(" STUNNED"), 10, 300), FString(TEXT("STUNNED")));
	TestTrue(TEXT("status alone with none: empty"), ComposeEnemyLabel(StatusOnly, TEXT("Brute"), TEXT(""), 10, 300).IsEmpty());
	TestTrue(TEXT("the cover entry text"), IsCoverText(TEXT("🛡️ IN COVER (-35% damage)")));
	TestFalse(TEXT("another floating text"), IsCoverText(TEXT("MISS")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLabelParseTest, "CodexTactics.UI.HudLabels.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudLabelParseTest::RunTest(const FString&)
{
	using namespace HudLabelRules;
	TestEqual(TEXT("one key per label kind"), GetKeys().Num(), LabelCount);
	for (int32 Index = 0; Index < GetKeys().Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("key %s in EHudLabel order"), GetKeys()[Index].Key), static_cast<int32>(GetKeys()[Index].Label), Index);
		TestTrue(FString::Printf(TEXT("key %s defaults on (today's HUD)"), GetKeys()[Index].Key), GetKeys()[Index].bDefaultShow);
	}
	TestEqual(TEXT("GetKey"), FString(GetKey(EHudLabel::WoundTag)), FString(TEXT("wound_tag")));
	FHudLabelConfig Config;
	TArray<FString> Unknown;
	TestTrue(TEXT("parses"), ParseConfig(TEXT("{\"schema\": \"codex.hud_labels/1\", \"labels\": {\"wound_tag\": {\"show\": false, \"label\": \"x\"}, \"enemy_health\": false, \"pmc_hats\": {\"show\": true}}}"),
		Config, &Unknown));
	TestFalse(TEXT("wound_tag off"), Config.IsShown(EHudLabel::WoundTag));
	TestFalse(TEXT("the short form: enemy_health off"), Config.IsShown(EHudLabel::EnemyHealth));
	TestTrue(TEXT("a key not given keeps its value"), Config.IsShown(EHudLabel::CriticalTag));
	TestEqual(TEXT("the unknown key is reported"), Unknown.Num() == 1 && Unknown[0] == TEXT("pmc_hats"), true);
	FHudLabelConfig Kept;
	Kept.SetShown(EHudLabel::HordeWarning, false);
	TestFalse(TEXT("a wrong schema: refused"), ParseConfig(TEXT("{\"schema\": \"codex.hud_labels/2\", \"labels\": {}}"), Kept));
	TestFalse(TEXT("no schema: refused"), ParseConfig(TEXT("{\"labels\": {}}"), Kept));
	TestFalse(TEXT("not JSON: refused"), ParseConfig(TEXT("nope"), Kept));
	TestFalse(TEXT("a refused file leaves the switches"), Kept.IsShown(EHudLabel::HordeWarning));
	TestTrue(TEXT("a show that is no bool keeps the value"), ParseConfig(TEXT("{\"schema\": \"codex.hud_labels/1\", \"labels\": {\"horde_warning\": {\"show\": 3}}}"), Kept)
		&& !Kept.IsShown(EHudLabel::HordeWarning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudLabelDataFileTest, "CodexTactics.UI.HudLabels.DataFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudLabelDataFileTest::RunTest(const FString&)
{
	using namespace HudLabelRules;
	FString Text;
	if (!TestTrue(TEXT("hud_labels.json exists"), FFileHelper::LoadFileToString(Text, *GetConfigPath())))
	{
		return false;
	}
	FHudLabelConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("hud_labels.json parses (schema codex.hud_labels/1)"), ParseConfig(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys"), Unknown.Num(), 0);
	const FHudLabelConfig Defaults;
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
	const TSharedPtr<FJsonObject>* Labels = nullptr;
	if (!TestTrue(TEXT("a labels object"), Root.IsValid() && Root->TryGetObjectField(TEXT("labels"), Labels) && Labels))
	{
		return false;
	}
	for (const FLabelKey& Key : GetKeys())
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!TestTrue(FString::Printf(TEXT("%s is in the file"), Key.Key), (*Labels)->TryGetObjectField(Key.Key, Entry) && Entry))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: the file = the default (today's HUD)"), Key.Key), FromFile.IsShown(Key.Label), Defaults.IsShown(Key.Label));
		FString Label;
		FString Doc;
		bool bCyrillic = false;
		if ((*Entry)->TryGetStringField(TEXT("label"), Label))
		{
			for (const TCHAR C : Label)
			{
				bCyrillic |= C >= 0x0400 && C <= 0x04FF;
			}
		}
		TestTrue(FString::Printf(TEXT("%s has a Russian label (%s)"), Key.Key, *Label), bCyrillic);
		TestTrue(FString::Printf(TEXT("%s has an English doc"), Key.Key), (*Entry)->TryGetStringField(TEXT("doc"), Doc) && Doc.Len() > 10);
	}
	return true;
}

#endif
