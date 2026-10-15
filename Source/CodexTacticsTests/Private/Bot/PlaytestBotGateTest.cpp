#include "Misc/AutomationTest.h"
#include "Bot/PlaytestBotSubsystem.h"
#include "Engine/EngineTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Audit 2026-10-11 #3 ("bot gate"): the playtest bot exists only in development builds and only in game / PIE worlds, so
// a shipped game cannot be driven, quit or made to write Saved/Telemetry by -CodexBot. UE-only (Godot ran the bot as a
// separate tool scene).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestBotGateTest, "CodexTactics.Bot.Gate.WorldTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlaytestBotGateTest::RunTest(const FString&)
{
	// This test runs in the editor (Development): the bot is compiled in.
	TestTrue(TEXT("compiled in a development build"), CODEX_WITH_PLAYTEST_BOT != 0);
	TestTrue(TEXT("game world"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::Game));
	TestTrue(TEXT("play in editor"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::PIE));
	TestFalse(TEXT("not the editor world"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::Editor));
	TestFalse(TEXT("not an editor preview"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::EditorPreview));
	TestFalse(TEXT("not a game preview"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::GamePreview));
	TestFalse(TEXT("not an inactive world"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::Inactive));
	TestFalse(TEXT("not a world without a type"), UPlaytestBotSubsystem::IsBotAllowed(EWorldType::None));
	// The world-subsystem hook answers the same (the CDO is what the subsystem collection asks).
	const UPlaytestBotSubsystem* Defaults = GetDefault<UPlaytestBotSubsystem>();
	TestTrue(TEXT("CDO: game world supported"), Defaults->DoesSupportWorldType(EWorldType::Game));
	TestFalse(TEXT("CDO: editor world not supported"), Defaults->DoesSupportWorldType(EWorldType::Editor));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
