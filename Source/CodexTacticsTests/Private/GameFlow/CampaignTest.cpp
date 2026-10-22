#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "GameFlow/CampaignCarry.h"
#include "GameFlow/CampaignRules.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Quests/GateExitRules.h"
#include "Survival/ColdSurvivalComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 2026-10-22 b (game designer: "if the operatives have unspent points, after the black screen a menu with the character's image and the
// points ... after that the new level opens"; "the next level is Frozen Factory, the current one 0.0 Frozen Dam"): the campaign level
// order, the decision after the exit's black screen (points screen / next level / main menu), the level-start fade and the squad carried
// to the next level. UE-only (the Godot archive had no level-to-level flow).

#define CAMPAIGN_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.GameFlow.Campaign." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CampaignTestLocal
{
	const TCHAR* TwoLevels = TEXT(R"({"campaign_id":"demo","levels":[
		{"id":"0.0","map":"/Game/Maps/L_Demo01_A_FrozenLake","title":"0.0 Frozen Dam"},
		{"id":"0.1","map":"/Game/Maps/L_Demo01_B_FrozenFactory","title":"0.1 Frozen Factory"}]})");

	FCampaignConfig Parse(FAutomationTestBase& Test, const TCHAR* Json)
	{
		FCampaignConfig Config;
		FString Error;
		Test.TestTrue(FString::Printf(TEXT("parses (%s)"), *Error), CampaignRules::ParseCampaign(Json, Config, Error));
		return Config;
	}
}

CAMPAIGN_TEST(FCampaignParseTest, "Parse")
bool FCampaignParseTest::RunTest(const FString&)
{
	const FCampaignConfig Config = CampaignTestLocal::Parse(*this, CampaignTestLocal::TwoLevels);
	TestEqual(TEXT("two levels"), Config.Levels.Num(), 2);
	TestEqual(TEXT("id"), Config.CampaignId, FString(TEXT("demo")));
	TestEqual(TEXT("first title"), Config.Levels[0].Title, FString(TEXT("0.0 Frozen Dam")));

	FCampaignConfig Out;
	FString Error;
	TestFalse(TEXT("not JSON"), CampaignRules::ParseCampaign(TEXT("{oops"), Out, Error));
	TestFalse(TEXT("no levels"), CampaignRules::ParseCampaign(TEXT(R"({"levels":[]})"), Out, Error));
	TestFalse(TEXT("a level without a map"), CampaignRules::ParseCampaign(TEXT(R"({"levels":[{"title":"x"}]})"), Out, Error));
	TestTrue(TEXT("... named"), Error.Contains(TEXT("map")));
	TestFalse(TEXT("a map listed twice"), CampaignRules::ParseCampaign(TEXT(R"({"levels":[{"map":"/Game/Maps/L_A"},{"map":"/Game/Maps/L_A.L_A"}]})"), Out, Error));
	TestTrue(TEXT("no title: the map's short name"), CampaignRules::ParseCampaign(TEXT(R"({"levels":[{"map":"/Game/Maps/L_A"}]})"), Out, Error)
		&& Out.Levels[0].Title == TEXT("L_A"));
	return true;
}

CAMPAIGN_TEST(FCampaignSequenceTest, "Sequence")
bool FCampaignSequenceTest::RunTest(const FString&)
{
	const FCampaignConfig Config = CampaignTestLocal::Parse(*this, CampaignTestLocal::TwoLevels);
	TestEqual(TEXT("short name of a path"), CampaignRules::ShortMapName(TEXT("/Game/Maps/L_X.L_X")), FString(TEXT("L_X")));
	TestEqual(TEXT("short name of a PIE world"), CampaignRules::ShortMapName(TEXT("UEDPIE_0_L_Demo01_A_FrozenLake")), FString(TEXT("L_Demo01_A_FrozenLake")));
	const FCampaignLevel* Next = CampaignRules::FindNextLevel(Config, TEXT("L_Demo01_A_FrozenLake"));
	TestTrue(TEXT("the lake (0.0) -> Frozen Factory (0.1)"), Next && Next->Map == TEXT("/Game/Maps/L_Demo01_B_FrozenFactory") && Next->Title == TEXT("0.1 Frozen Factory"));
	TestTrue(TEXT("any name form, any case"), CampaignRules::FindNextLevel(Config, TEXT("UEDPIE_2_l_demo01_a_frozenlake")) == Next
		&& CampaignRules::FindNextLevel(Config, TEXT("/Game/Maps/L_Demo01_A_FrozenLake.L_Demo01_A_FrozenLake")) == Next);
	TestNull(TEXT("the last level has no next"), CampaignRules::FindNextLevel(Config, TEXT("L_Demo01_B_FrozenFactory")));
	TestNull(TEXT("a map outside the campaign (L_MovementTest) has none"), CampaignRules::FindNextLevel(Config, TEXT("L_MovementTest")));
	TestEqual(TEXT("title"), CampaignRules::GetLevelTitle(Config, TEXT("L_Demo01_B_FrozenFactory"), TEXT("?")), FString(TEXT("0.1 Frozen Factory")));
	TestEqual(TEXT("title fallback"), CampaignRules::GetLevelTitle(Config, TEXT("L_MovementTest"), TEXT("L_MovementTest")), FString(TEXT("L_MovementTest")));
	return true;
}

CAMPAIGN_TEST(FCampaignDataFileTest, "DataFile")
bool FCampaignDataFileTest::RunTest(const FString&)
{
	FString Json;
	TestTrue(TEXT("Content/Data/Campaign/campaign.json loads"), FFileHelper::LoadFileToString(Json, *CampaignRules::GetCampaignFilePath()));
	FCampaignConfig Config;
	FString Error;
	TestTrue(FString::Printf(TEXT("... parses (%s)"), *Error), CampaignRules::ParseCampaign(Json, Config, Error));
	if (!TestEqual(TEXT("two levels"), Config.Levels.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("0.0 = the lake, \"0.0 Frozen Dam\""), Config.Levels[0].Id == TEXT("0.0") && Config.Levels[0].Map == TEXT("/Game/Maps/L_Demo01_A_FrozenLake")
		&& Config.Levels[0].Title == TEXT("0.0 Frozen Dam"));
	TestTrue(TEXT("0.1 = the factory copy, \"0.1 Frozen Factory\""), Config.Levels[1].Id == TEXT("0.1")
		&& Config.Levels[1].Map == TEXT("/Game/Maps/L_Demo01_B_FrozenFactory") && Config.Levels[1].Title == TEXT("0.1 Frozen Factory"));
	for (const FCampaignLevel& Level : Config.Levels)
	{
		TestTrue(FString::Printf(TEXT("%s exists"), *Level.Map), FPackageName::DoesPackageExist(Level.Map));
		FLevelCombatConfig Parsed;
		TestTrue(FString::Printf(TEXT("%s has its level JSON"), *Level.Map),
			LevelJsonRules::LoadLevel(CampaignRules::ShortMapName(Level.Map) + TEXT(".json"), Parsed, Error));
		TestEqual(TEXT("level_name = the campaign title"), Parsed.LevelName.ToString(), Level.Title);
	}
	FLevelCombatConfig Factory;
	LevelJsonRules::LoadLevel(TEXT("L_Demo01_B_FrozenFactory.json"), Factory, Error);
	TestTrue(TEXT("the factory: no tutorial, no gate exit, a 2 s start fade, one wave"), !Factory.Demo.bDemo && !Factory.Demo.bTutorial
		&& !Factory.GateExit.bEnabled && FMath::IsNearlyEqual(Factory.StartFadeSeconds, 2.f) && Factory.Waves.Num() == 1);
	return true;
}

CAMPAIGN_TEST(FCampaignPointsGateTest, "PointsGate")
bool FCampaignPointsGateTest::RunTest(const FString&)
{
	TestEqual(TEXT("next level + points: the points screen"), CampaignRules::DecideAfterExit(true, true), ELevelExitNext::PointsScreen);
	TestEqual(TEXT("next level, no points: straight on"), CampaignRules::DecideAfterExit(true, false), ELevelExitNext::NextLevel);
	TestEqual(TEXT("no next level: the main menu (points or not)"), CampaignRules::DecideAfterExit(false, true), ELevelExitNext::MainMenu);
	TestEqual(TEXT("... the same"), CampaignRules::DecideAfterExit(false, false), ELevelExitNext::MainMenu);

	const TArray<FPointsReviewMember> Squad = { { true, 3 }, { true, 0 }, { true, 2 } };
	TestTrue(TEXT("somebody has points"), CampaignRules::AnyUnspentPoints(Squad));
	TestTrue(TEXT("the screen shows those with points, in squad order"), CampaignRules::GetReviewIndices(Squad) == TArray<int32>({ 0, 2 }));
	const TArray<FPointsReviewMember> FallenWithPoints = { { false, 3 }, { true, 0 } };
	TestFalse(TEXT("the killed in action never count"), CampaignRules::AnyUnspentPoints(FallenWithPoints));
	TestTrue(TEXT("nobody: no screen"), CampaignRules::GetReviewIndices(TArray<FPointsReviewMember>()).IsEmpty());
	return true;
}

CAMPAIGN_TEST(FCampaignTimingTest, "Timing")
bool FCampaignTimingTest::RunTest(const FString&)
{
	FGateExitConfig Config;
	Config.FadeSeconds = 2.f;
	Config.ReturnToMenuSeconds = 6.f;
	Config.NextLevelSeconds = 3.f;
	TestFalse(TEXT("next level: 3 s on the black screen first"), GateExitRules::ShouldLeaveLevel(4.9f, Config, true, false));
	TestTrue(TEXT("... then the points screen / the next level"), GateExitRules::ShouldLeaveLevel(5.f, Config, true, false));
	TestFalse(TEXT("never in a headless check"), GateExitRules::ShouldLeaveLevel(100.f, Config, true, true));
	TestFalse(TEXT("no next level: the menu's 6 s"), GateExitRules::ShouldLeaveLevel(7.9f, Config, false, false));
	TestTrue(TEXT("... then the menu"), GateExitRules::ShouldLeaveLevel(8.f, Config, false, false));
	Config.NextLevelSeconds = 0.f;
	TestFalse(TEXT("0 = stay"), GateExitRules::ShouldLeaveLevel(100.f, Config, true, false));

	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("level JSON keys"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a","start_fade_seconds":2.5,"gate_exit":{"next_level_seconds":4}})"),
		TEXT("a"), Level, Error));
	TestTrue(TEXT("... read"), FMath::IsNearlyEqual(Level.StartFadeSeconds, 2.5f) && FMath::IsNearlyEqual(Level.GateExit.NextLevelSeconds, 4.f));
	TestTrue(TEXT("defaults"), LevelJsonRules::ParseLevel(TEXT(R"({"level_id":"a"})"), TEXT("a"), Level, Error) && Level.StartFadeSeconds == 0.f
		&& FMath::IsNearlyEqual(Level.GateExit.NextLevelSeconds, 3.f));

	TestEqual(TEXT("the level's start fade"), CampaignRules::ResolveStartFadeSeconds(2.f, false), 2.f);
	TestEqual(TEXT("a campaign arrival fades in anyway"), CampaignRules::ResolveStartFadeSeconds(0.f, true), CampaignRules::ArrivalFadeSeconds);
	TestEqual(TEXT("no fade otherwise"), CampaignRules::ResolveStartFadeSeconds(0.f, false), 0.f);
	const float Hold = CampaignRules::StartHoldSeconds;
	TestEqual(TEXT("black during the hold"), CampaignRules::StartFadeAlpha(Hold - 0.01f, 2.f), 1.f);
	TestTrue(TEXT("half way: half"), FMath::IsNearlyEqual(CampaignRules::StartFadeAlpha(Hold + 1.f, 2.f), 0.5f, 0.01f));
	TestEqual(TEXT("clear after hold + fade"), CampaignRules::StartFadeAlpha(Hold + 2.f, 2.f), 0.f);
	TestEqual(TEXT("no fade: clear"), CampaignRules::StartFadeAlpha(0.f, 0.f), 0.f);
	TestEqual(TEXT("the title shows in the hold"), CampaignRules::StartTitleAlpha(0.f, 2.f), 1.f);
	TestEqual(TEXT("... gone by the middle of the fade"), CampaignRules::StartTitleAlpha(Hold + 1.f, 2.f), 0.f);
	return true;
}

CAMPAIGN_TEST(FCampaignCarryRoundTripTest, "CarryRoundTrip")
bool FCampaignCarryRoundTripTest::RunTest(const FString&)
{
	AOperativeCharacter* Leaving = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Arriving = NewObject<AOperativeCharacter>();
	if (!TestNotNull(TEXT("operatives"), Leaving) || !TestNotNull(TEXT("..."), Arriving) || !TestNotNull(TEXT("health"), Leaving->HealthComponent.Get()))
	{
		return false;
	}
	Leaving->SquadRole = EOperativeRole::MedicSapper;
	Leaving->SquadIndex = 2;
	Leaving->Level = 3;
	Leaving->CurrentExp = 120;
	Leaving->UnspentStatPoints = 2;
	Leaving->Accuracy = 88.f;
	Leaving->Luck = 36.f;
	Leaving->ColdSurvival->Fortitude = 22.f;
	Leaving->HealthComponent->RestoreHealth(40.f, 115.f); // wounded, max raised by points
	Leaving->ColdLevel = 70.f;
	Leaving->MedkitsCount = 4;
	Leaving->GrenadesCount = 1;
	Leaving->HeatPacksCount = 3;
	Leaving->MinesCount = 2;
	Leaving->StoveGasSeconds = 33.f;
	Leaving->BonusItems = { TEXT("night_vision") };
	Leaving->ExtraAmmo.Add(TEXT("sniper_rifle"), 7);

	const FCarriedOperative Carried = CampaignCarry::Capture(*Leaving);
	TestTrue(TEXT("captured: role, progression"), Carried.Role == EOperativeRole::MedicSapper && Carried.SquadIndex == 2 && Carried.Level == 3
		&& Carried.CurrentExp == 120 && Carried.UnspentStatPoints == 2);
	TestTrue(TEXT("captured: the stats with the points spent in them"), FMath::IsNearlyEqual(Carried.MaxHealth, 115.f) && Carried.Accuracy == 88.f
		&& Carried.Luck == 36.f && Carried.Fortitude == 22.f);
	TestEqual(TEXT("captured: every item count by its save key"), Carried.Items.Num(), CampaignCarry::ItemKeys().Num());

	CampaignCarry::Apply(*Arriving, Carried);
	TestTrue(TEXT("arrives with his progression"), Arriving->Level == 3 && Arriving->CurrentExp == 120 && Arriving->UnspentStatPoints == 2
		&& Arriving->Accuracy == 88.f && Arriving->Luck == 36.f && Arriving->ColdSurvival->Fortitude == 22.f);
	TestTrue(TEXT("... at FULL health of his carried maximum"), FMath::IsNearlyEqual(Arriving->HealthComponent->GetMaxHealth(), 115.f)
		&& FMath::IsNearlyEqual(Arriving->HealthComponent->GetCurrentHealth(), 115.f));
	TestEqual(TEXT("... warm (cold starts fresh)"), Arriving->ColdLevel, 0.f);
	TestTrue(TEXT("... with his kit"), Arriving->MedkitsCount == 4 && Arriving->GrenadesCount == 1 && Arriving->HeatPacksCount == 3 && Arriving->MinesCount == 2
		&& Arriving->StoveGasSeconds == 33.f && Arriving->BonusItems == Leaving->BonusItems && Arriving->ExtraAmmo.FindRef(TEXT("sniper_rifle")) == 7);
	TestFalse(TEXT("points spent on the previous level stay spent (\"-\" stops at the arrival value)"), Arriving->CanDecreaseStat(EProgressStat::Accuracy));
	TestTrue(TEXT("the carried points can be spent"), Arriving->IncreaseStat(EProgressStat::Accuracy) && Arriving->UnspentStatPoints == 1);
	return true;
}

CAMPAIGN_TEST(FCampaignCarryMatchingTest, "CarryMatching")
bool FCampaignCarryMatchingTest::RunTest(const FString&)
{
	FCampaignSquadCarry Carry;
	TestTrue(TEXT("no carry: everybody spawns fresh"), CampaignCarry::ShouldSpawn(Carry, EOperativeRole::Commander, 0));
	Carry.FromMap = TEXT("L_Demo01_A_FrozenLake");
	Carry.ToMap = TEXT("L_Demo01_B_FrozenFactory");
	FCarriedOperative Commander;
	Commander.Role = EOperativeRole::Commander;
	Commander.SquadIndex = 0;
	FCarriedOperative Engineer;
	Engineer.Role = EOperativeRole::Engineer;
	Engineer.SquadIndex = 1;
	Engineer.bKilledInAction = true;
	FCarriedOperative Medic;
	Medic.Role = EOperativeRole::MedicSapper;
	Medic.SquadIndex = 2;
	Carry.Members = { Commander, Engineer, Medic };
	TestTrue(TEXT("matched by role"), CampaignCarry::FindFor(Carry, EOperativeRole::MedicSapper, 5) == &Carry.Members[2]);
	TestTrue(TEXT("else by squad index"), CampaignCarry::FindFor(Carry, EOperativeRole::Recruit, 0) == &Carry.Members[0]);
	TestNull(TEXT("nobody to match: fresh"), CampaignCarry::FindFor(Carry, EOperativeRole::Recruit, 7));
	TestFalse(TEXT("the fallen stay behind"), CampaignCarry::ShouldSpawn(Carry, EOperativeRole::Engineer, 1));
	TestTrue(TEXT("the living come along"), CampaignCarry::ShouldSpawn(Carry, EOperativeRole::Commander, 0) && CampaignCarry::ShouldSpawn(Carry, EOperativeRole::MedicSapper, 2));
	TestTrue(TEXT("a roster row with no record spawns fresh"), CampaignCarry::ShouldSpawn(Carry, EOperativeRole::Recruit, 9));
	return true;
}

#undef CAMPAIGN_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
