#include "Misc/AutomationTest.h"
#include "Data/NarrativeManifest.h"

#if WITH_DEV_AUTOMATION_TESTS

// narrative_manifest.json (Scripts/Narrative/sync_narrative.py output): English text, placeholders for missing lines.

#define NARRATIVE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Dialogue.Manifest." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace NarrativeManifestTest
{
	// Cyrillic is written as \u escapes so the test source stays ASCII.
	const TCHAR* const kJson = TEXT(R"({
	  "sequences": {
	    "DA_Seq": { "sequence_id": "DA_Seq", "lines": [
	      { "speaker_en": "Commander", "speaker_ru": "Командир", "text_en": "Hold the line.", "text_ru": "Держать", "delay": 2.5 },
	      { "speaker_en": "Engineer", "speaker_ru": "", "text_en": "", "text_ru": "Нет перевода", "delay": 0 }
	    ] }
	  },
	  "glossary": { "item_canister": { "name_en": "20L Fuel Canister", "name_ru": "Канистра" } }
	})");

	FDialogueLine MakeLine(const TCHAR* Speaker, const FString& Text)
	{
		FDialogueLine Line;
		Line.SpeakerName = Speaker;
		Line.Text = Text;
		return Line;
	}

	const FString kRussian = FString(TEXT("Держать"));
}

NARRATIVE_TEST(FNarrativeManifestParseTest, "Parse")
bool FNarrativeManifestParseTest::RunTest(const FString&)
{
	FNarrativeManifest Manifest;
	FString Error;
	TestTrue(TEXT("valid manifest parses"), FNarrativeManifest::Parse(NarrativeManifestTest::kJson, Manifest, Error));
	const TArray<FNarrativeLine>* Lines = Manifest.Sequences.Find(TEXT("DA_Seq"));
	if (!TestNotNull(TEXT("sequence found"), Lines))
	{
		return false;
	}
	TestEqual(TEXT("two lines"), Lines->Num(), 2);
	TestEqual(TEXT("EN speaker"), (*Lines)[0].Speaker, FString(TEXT("Commander")));
	TestEqual(TEXT("EN text"), (*Lines)[0].Text, FString(TEXT("Hold the line.")));
	TestEqual(TEXT("delay"), (*Lines)[0].DelayAfter, 2.5f);
	TestEqual(TEXT("zero delay keeps the default"), (*Lines)[1].DelayAfter, 4.f);
	TestEqual(TEXT("glossary"), Manifest.GetGlossaryName(TEXT("item_canister")), FString(TEXT("20L Fuel Canister")));
	TestEqual(TEXT("glossary fallback"), Manifest.GetGlossaryName(TEXT("nope"), TEXT("X")), FString(TEXT("X")));

	FNarrativeManifest Bad;
	TestFalse(TEXT("garbage is rejected"), FNarrativeManifest::Parse(TEXT("not json"), Bad, Error));
	TestFalse(TEXT("no sequences is rejected"), FNarrativeManifest::Parse(TEXT("{}"), Bad, Error));
	return true;
}

NARRATIVE_TEST(FNarrativeManifestBuildLinesTest, "BuildLines")
bool FNarrativeManifestBuildLinesTest::RunTest(const FString&)
{
	FNarrativeManifest Manifest;
	FString Error;
	Manifest.Parse(NarrativeManifestTest::kJson, Manifest, Error);
	TArray<FString> Warnings;

	// 1. Sequence in the manifest: EN text wins over whatever the asset holds; an empty text_en is a placeholder.
	TArray<FDialogueLine> AssetLines = { NarrativeManifestTest::MakeLine(TEXT("X"), NarrativeManifestTest::kRussian) };
	TArray<FDialogueLine> Lines = Manifest.BuildLines(TEXT("DA_Seq"), AssetLines, Warnings);
	TestEqual(TEXT("manifest line count wins"), Lines.Num(), 2);
	TestEqual(TEXT("line 1 EN"), Lines[0].Text, FString(TEXT("Hold the line.")));
	TestEqual(TEXT("line 1 speaker"), Lines[0].SpeakerName, FString(TEXT("Commander")));
	TestEqual(TEXT("line 2 placeholder"), Lines[1].Text, FString(TEXT("[EN missing: DA_Seq#2]")));
	TestEqual(TEXT("one warning for the empty line"), Warnings.Num(), 1);

	// 2. Sequence absent: English asset text survives, Cyrillic asset text becomes the placeholder.
	Warnings.Reset();
	AssetLines = { NarrativeManifestTest::MakeLine(TEXT("Medic"), TEXT("Already English")), NarrativeManifestTest::MakeLine(TEXT("Medic"), NarrativeManifestTest::kRussian) };
	Lines = Manifest.BuildLines(TEXT("DA_Other"), AssetLines, Warnings);
	TestEqual(TEXT("asset line count"), Lines.Num(), 2);
	TestEqual(TEXT("English asset text is kept"), Lines[0].Text, FString(TEXT("Already English")));
	TestEqual(TEXT("Russian asset text is replaced"), Lines[1].Text, FString(TEXT("[EN missing: DA_Other#2]")));
	TestTrue(TEXT("a missing sequence warns"), Warnings.Num() >= 1);

	// 3. Nothing anywhere: one placeholder, never an empty window.
	Lines = Manifest.BuildLines(TEXT("DA_Empty"), TArray<FDialogueLine>(), Warnings);
	TestEqual(TEXT("single placeholder"), Lines.Num(), 1);
	TestEqual(TEXT("placeholder text"), Lines[0].Text, FString(TEXT("[EN missing: DA_Empty#1]")));

	for (const FDialogueLine& Line : Lines)
	{
		TestFalse(TEXT("no Cyrillic reaches the screen"), FNarrativeManifest::ContainsCyrillic(Line.Text) || FNarrativeManifest::ContainsCyrillic(Line.SpeakerName));
	}
	return true;
}

NARRATIVE_TEST(FNarrativeManifestCyrillicTest, "Cyrillic")
bool FNarrativeManifestCyrillicTest::RunTest(const FString&)
{
	TestTrue(TEXT("Cyrillic detected"), FNarrativeManifest::ContainsCyrillic(NarrativeManifestTest::kRussian));
	TestFalse(TEXT("English is clean"), FNarrativeManifest::ContainsCyrillic(TEXT("Hold the line. [EN missing: A#1]")));
	TestEqual(TEXT("EnglishOr keeps English"), FNarrativeManifest::EnglishOr(TEXT("Fine"), TEXT("Fallback")), FString(TEXT("Fine")));
	TestEqual(TEXT("EnglishOr swaps Russian"), FNarrativeManifest::EnglishOr(NarrativeManifestTest::kRussian, TEXT("Fallback")), FString(TEXT("Fallback")));
	TestEqual(TEXT("EnglishOr swaps empty"), FNarrativeManifest::EnglishOr(FString(), TEXT("Fallback")), FString(TEXT("Fallback")));
	return true;
}

NARRATIVE_TEST(FNarrativeManifestRealFileTest, "RealFile")
bool FNarrativeManifestRealFileTest::RunTest(const FString&)
{
	// The shipped manifest: every sequence the game plays has English lines and no Cyrillic in the EN columns.
	FNarrativeManifest::Reset();
	const FNarrativeManifest& Manifest = FNarrativeManifest::Get();
	for (const TCHAR* Id : { TEXT("DA_DialogueIntro"), TEXT("DA_DialoguePrep"), TEXT("DA_DialogueWaveRest"), TEXT("DA_DialogueVictory") })
	{
		const TArray<FNarrativeLine>* Lines = Manifest.Sequences.Find(Id);
		if (!TestNotNull(*FString::Printf(TEXT("%s in the manifest"), Id), Lines))
		{
			continue;
		}
		for (int32 Index = 0; Index < Lines->Num(); ++Index)
		{
			const FNarrativeLine& Line = (*Lines)[Index];
			TestFalse(*FString::Printf(TEXT("%s#%d has text_en"), Id, Index + 1), Line.Text.IsEmpty());
			TestFalse(*FString::Printf(TEXT("%s#%d EN has no Cyrillic"), Id, Index + 1), FNarrativeManifest::ContainsCyrillic(Line.Text) || FNarrativeManifest::ContainsCyrillic(Line.Speaker));
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
