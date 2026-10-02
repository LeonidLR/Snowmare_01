#include "Misc/AutomationTest.h"
#include "Bot/PlaytestBotRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Playtest bot decisions (Godot archive tools/bot/bot_driver.gd, smart_tactical_bot.gd).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestBotProfilesTest, "CodexTactics.Bot.Profiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlaytestBotProfilesTest::RunTest(const FString&)
{
	TestTrue(TEXT("veteran"), PlaytestBotRules::ParseProfile(TEXT("veteran")) == EBotProfile::Veteran);
	TestTrue(TEXT("REGULAR = normal"), PlaytestBotRules::ParseProfile(TEXT("REGULAR")) == EBotProfile::Normal);
	TestTrue(TEXT("unknown = normal"), PlaytestBotRules::ParseProfile(TEXT("???")) == EBotProfile::Normal);
	TestEqual(TEXT("Name"), PlaytestBotRules::ProfileName(EBotProfile::Casual), FString(TEXT("CASUAL")));
	const FBotProfileConfig Veteran = PlaytestBotRules::GetProfileConfig(EBotProfile::Veteran);
	TestTrue(TEXT("Veteran: turret, 2 barricades, mine, guards"), Veteran.Turrets == 1 && Veteran.Barricades == 2 && Veteran.Mines == 1 && Veteran.bGuardAfterDeploy);
	TestEqual(TEXT("Veteran heals below 45 %"), Veteran.HealBelowHealthFraction, 0.45f);
	TestEqual(TEXT("Veteran warms above 70 %"), Veteran.WarmAboveCold, 70.f);
	const FBotProfileConfig Normal = PlaytestBotRules::GetProfileConfig(EBotProfile::Normal);
	TestTrue(TEXT("Normal: one of each, no guard"), Normal.Turrets == 1 && Normal.Barricades == 1 && Normal.Mines == 1 && !Normal.bGuardAfterDeploy);
	TestEqual(TEXT("Normal heals below 38 %"), Normal.HealBelowHealthFraction, 0.38f);
	const FBotProfileConfig Casual = PlaytestBotRules::GetProfileConfig(EBotProfile::Casual);
	TestTrue(TEXT("Casual: nothing"), Casual.Turrets == 0 && Casual.Barricades == 0 && Casual.HealBelowHealthFraction == 0.f && Casual.WarmAboveCold > 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestBotTacticsTest, "CodexTactics.Bot.Tactics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlaytestBotTacticsTest::RunTest(const FString&)
{
	// Three hounds within 4 m of each other, one far away.
	const TArray<FVector> Enemies = { FVector(1000, 0, 0), FVector(1200, 100, 0), FVector(1100, -150, 0), FVector(5000, 0, 0) };
	FVector Center;
	TestEqual(TEXT("Cluster of 3"), PlaytestBotRules::FindCluster(Enemies, 400.f, Center), 3);
	TestEqual(TEXT("Cluster centre X"), static_cast<float>(Center.X), 1100.f, 0.1f);
	TestTrue(TEXT("Throw at 11 m, range 12 m"), PlaytestBotRules::ShouldThrowGrenade(3, 1100.f, 1200.f));
	TestFalse(TEXT("Too close (2.9 m)"), PlaytestBotRules::ShouldThrowGrenade(3, 290.f, 1200.f));
	TestFalse(TEXT("Out of range"), PlaytestBotRules::ShouldThrowGrenade(5, 1300.f, 1200.f));
	TestFalse(TEXT("Two are not a cluster"), PlaytestBotRules::ShouldThrowGrenade(2, 800.f, 1200.f));

	// Fallback: 5 m, away from the threat and towards the rear.
	const FVector Back = PlaytestBotRules::FallbackPosition(FVector::ZeroVector, FVector(300, 0, 0), FVector(-1, 0, 0));
	TestEqual(TEXT("5 m"), static_cast<float>(Back.Size2D()), 500.f, 0.5f);
	TestTrue(TEXT("Away from the threat"), Back.X < 0.f);

	// Cover: elevated and intact beats far; stand point 1.5 m behind, away from the threat.
	TestTrue(TEXT("Near intact cover scores higher"), PlaytestBotRules::CoverScore(200.f, 1.f, false) > PlaytestBotRules::CoverScore(1200.f, 1.f, false));
	TestEqual(TEXT("Score formula"), PlaytestBotRules::CoverScore(400.f, 0.5f, true), 100.f + 25.f + 15.f - 10.f, 0.01f);
	const FVector Stand = PlaytestBotRules::CoverStandPoint(FVector(1000, 0, 0), FVector(3000, 0, 0));
	TestEqual(TEXT("Stand point behind"), static_cast<float>(Stand.X), 850.f, 0.1f);
	return true;
}

#endif
