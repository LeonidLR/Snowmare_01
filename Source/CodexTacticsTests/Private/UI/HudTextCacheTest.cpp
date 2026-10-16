#include "Misc/AutomationTest.h"
#include "UI/HudTextCache.h"

#if WITH_DEV_AUTOMATION_TESTS

// Audit 2026-10-11 P6 / P7: the canvas HUD lays its world labels and feed messages out once per text instead of every
// frame. UE-only (no Godot reference). A fake measurement: 10 px per character, 12 px high.

namespace HudTextCacheTest
{
	FVector2D FakeMeasure(const FString& Text, int32& Calls)
	{
		++Calls;
		return FVector2D(10.0 * Text.Len(), Text.IsEmpty() ? 0.0 : 12.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextCacheWrapTest, "CodexTactics.UI.HudTextCache.WrapWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudTextCacheWrapTest::RunTest(const FString&)
{
	int32 Calls = 0;
	auto Measure = [&Calls](const FString& Text) { return HudTextCacheTest::FakeMeasure(Text, Calls); };
	// 100 px at scale 1 = 10 characters per line.
	const TArray<FString> Lines = HudTextCache::WrapWords(TEXT("Hold the line until   the blast door opens"), 1.f, 100.f, Measure);
	TestTrue(TEXT("greedy wrap at 10 characters"), Lines == TArray<FString>({ TEXT("Hold the"), TEXT("line until"), TEXT("the blast"),
		TEXT("door opens") }));
	TestEqual(TEXT("one measurement per word"), Calls, 8);
	// Scale 2 halves the room; a word wider than the line keeps its own line.
	const TArray<FString> Scaled = HudTextCache::WrapWords(TEXT("go supercalifragilistic go"), 2.f, 100.f, Measure);
	TestTrue(TEXT("long word alone"), Scaled == TArray<FString>({ TEXT("go"), TEXT("supercalifragilistic"), TEXT("go") }));
	TestEqual(TEXT("empty text: no lines"), HudTextCache::WrapWords(TEXT("   "), 1.f, 100.f, Measure).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextCacheLabelTest, "CodexTactics.UI.HudTextCache.LayoutLabel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudTextCacheLabelTest::RunTest(const FString&)
{
	int32 Calls = 0;
	auto Measure = [&Calls](const FString& Text) { return HudTextCacheTest::FakeMeasure(Text, Calls); };
	const HudTextCache::FLabelLayout Layout = HudTextCache::LayoutLabel(TEXT("  Hound 40/40 \n\n   FROZEN  \n"), Measure);
	TestTrue(TEXT("trimmed lines, empty ones dropped"), Layout.Lines == TArray<FString>({ TEXT("Hound 40/40"), TEXT("FROZEN") }));
	TestEqual(TEXT("one text per line"), Layout.LineTexts.Num(), 2);
	TestEqual(TEXT("line text"), Layout.LineTexts[1].ToString(), FString(TEXT("FROZEN")));
	TestTrue(TEXT("widths per line"), Layout.LineWidths == TArray<float>({ 110.f, 60.f }));
	TestEqual(TEXT("widest line"), Layout.Width, 110.f);
	TestEqual(TEXT("tallest line"), Layout.LineHeight, 12.f);
	TestEqual(TEXT("each line measured once"), Calls, 2);
	const HudTextCache::FLabelLayout Empty = HudTextCache::LayoutLabel(TEXT("  "), Measure);
	TestEqual(TEXT("empty label: no lines"), Empty.Lines.Num(), 0);
	TestEqual(TEXT("empty label: no height (the caller uses the font)"), Empty.LineHeight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudTextCacheBoundedTest, "CodexTactics.UI.HudTextCache.BoundedCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHudTextCacheBoundedTest::RunTest(const FString&)
{
	HudTextCache::TBoundedStringCache<int32> Cache(2);
	int32 Made = 0;
	auto Make = [&Made](const FString& Key) { ++Made; return Key.Len(); };
	TestEqual(TEXT("miss makes"), Cache.FindOrMake(TEXT("Hound"), Make), 5);
	TestEqual(TEXT("hit returns the cached value"), Cache.FindOrMake(TEXT("Hound"), Make), 5);
	TestEqual(TEXT("made once"), Made, 1);
	// Case-sensitive keys: FString's own == would hand «HOUND» the «Hound» entry.
	Cache.FindOrMake(TEXT("HOUND"), Make);
	TestEqual(TEXT("another case is another entry"), Made, 2);
	TestEqual(TEXT("two entries"), Cache.Num(), 2);
	// Past the capacity the cache is dropped and refilled.
	Cache.FindOrMake(TEXT("Brute"), Make);
	TestEqual(TEXT("flushed once"), Cache.GetFlushes(), 1);
	TestEqual(TEXT("only the new entry"), Cache.Num(), 1);
	Cache.FindOrMake(TEXT("Hound"), Make);
	TestEqual(TEXT("a flushed key is made again"), Made, 4);
	TestEqual(TEXT("hits"), Cache.GetHits(), 1);
	TestEqual(TEXT("misses"), Cache.GetMisses(), 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
