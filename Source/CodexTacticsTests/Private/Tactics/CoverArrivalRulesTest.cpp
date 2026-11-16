// CodexTactics.Tactics.Cover.Arrival* - the cover arrival past a squadmate on the slot (075, CoverSmoke 2026-11-16, CoverArrivalRules).

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tactics/CoverArrivalRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverArrivalPassSquadmateTest, "CodexTactics.Tactics.Cover.ArrivalPassesASquadmateOnTheSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverArrivalPassSquadmateTest::RunTest(const FString&)
{
	using namespace CoverArrivalRules;
	const FCoverArrivalConfig Config;
	// CoverSmoke 2026-11-16: the Engineer's corner-watch spot 11 cm from the corner slot stopped the sweep 43 cm short (72 cm from the wall end).
	TestTrue(TEXT("a walking squadmate stopped the sweep 43 cm short: past him onto the slot"), ShouldPassSquadmate(true, true, false, 43.f, Config));
	TestTrue(TEXT("... 74 cm short on a wall entry too"), ShouldPassSquadmate(true, true, false, 74.f, Config));
	TestFalse(TEXT("not blocked: nothing to repeat"), ShouldPassSquadmate(false, false, false, 0.f, Config));
	TestFalse(TEXT("a wall / an obstacle stopped it: stays (as before)"), ShouldPassSquadmate(true, false, false, 43.f, Config));
	TestFalse(TEXT("a squadmate in cover himself keeps his spot (group cover shoulder to shoulder)"), ShouldPassSquadmate(true, true, true, 43.f, Config));
	TestFalse(TEXT("within the gap: fine as it is"), ShouldPassSquadmate(true, true, false, Config.PassSquadmateMinGapCm, Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverArrivalTuningFileTest, "CodexTactics.Tactics.Cover.ArrivalTuningFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverArrivalTuningFileTest::RunTest(const FString&)
{
	using namespace CoverArrivalRules;
	FString Text;
	if (!TestTrue(TEXT("cover_arrival.json is there"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Tactics/cover_arrival.json")))))
	{
		return false;
	}
	FCoverArrivalConfig FromFile;
	FromFile.PassSquadmateMinGapCm = -1.f;
	TestTrue(TEXT("it parses"), LoadConfigFromJson(Text, FromFile));
	TestEqual(TEXT("pass_squadmate_min_gap_cm mirrors the struct default"), FromFile.PassSquadmateMinGapCm, FCoverArrivalConfig().PassSquadmateMinGapCm);
	TestEqual(TEXT("the game reads the same value"), GetConfig().PassSquadmateMinGapCm, FromFile.PassSquadmateMinGapCm);
	FCoverArrivalConfig Clamped;
	TestTrue(TEXT("an inline override parses"), LoadConfigFromJson(TEXT("{\"params\": {\"pass_squadmate_min_gap_cm\": 500}}"), Clamped));
	TestEqual(TEXT("... clamped to 100 cm"), Clamped.PassSquadmateMinGapCm, 100.f);
	TestFalse(TEXT("no JSON: false"), LoadConfigFromJson(TEXT("nope"), Clamped));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
