// CodexTactics.Combat.Firefight.* — horde #12 (game designer 2026-11-02 / 11-03 / 11-08, UE-only; FirefightRules, UFirefightSubsystem):
// what a firefight is (contact + shots with an aware enemy, or an ambush sprung; one per encounter), when it ends (all down / contact lost),
// the streak to the level's threshold, the slip-past reset, the data keys (horde.json "firefight_*", level "horde_after_firefights"), and
// the horde's composition with the Military Zombie variant (no Spitter). 2026-11-09 (game designer): the default threshold is 10 (the HQ
// hint at 9), the streak is the ONLY horde trigger (the 240 s real-time-fight clock and its keys are gone), the Depot has the horde, the Lake
// and the PMC arenas do not.

#include "Misc/AutomationTest.h"
#include "Combat/FirefightRules.h"
#include "Combat/HordeRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FIREFIGHT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Firefight." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FIREFIGHT_TEST(FFirefightDefinitionTest, "Definition")
bool FFirefightDefinitionTest::RunTest(const FString&)
{
	using namespace FirefightRules;
	// Spotted, but nobody fired: contact, no firefight yet; the first shot makes it one - once.
	FFirefightEncounter Spotted;
	TestFalse(TEXT("Spotted alone: no firefight"), ApplyEvent(Spotted, EFirefightEvent::EnemyAware, 1.0));
	TestTrue(TEXT("... the encounter is on"), Spotted.bActive && !Spotted.bCounted);
	TestTrue(TEXT("The squad's shot at the aware enemy: a firefight"), ApplyEvent(Spotted, EFirefightEvent::SquadShot, 2.0));
	TestFalse(TEXT("More shots in the same encounter: no second firefight"), ApplyEvent(Spotted, EFirefightEvent::SquadShot, 3.0));
	TestFalse(TEXT("... nor its attacks"), ApplyEvent(Spotted, EFirefightEvent::EnemyAttack, 4.0));
	TestEqual(TEXT("Started at the first contact"), Spotted.StartTime, 1.0);
	TestEqual(TEXT("The last contact"), Spotted.LastContactTime, 4.0);

	// The squad fires first and the enemy notices (a hit breaks its patrol): a firefight on the awareness.
	FFirefightEncounter Opened;
	TestFalse(TEXT("A shot at an unaware enemy: no firefight yet"), ApplyEvent(Opened, EFirefightEvent::SquadShot, 0.0));
	TestTrue(TEXT("... it notices: a firefight"), ApplyEvent(Opened, EFirefightEvent::EnemyAware, 0.1));

	// A silent kill: the shot, never an aware enemy - no firefight.
	FFirefightEncounter Silent;
	ApplyEvent(Silent, EFirefightEvent::SquadShot, 0.0);
	TestFalse(TEXT("Silent kill: no firefight"), Qualifies(Silent));

	// An enemy attack is awareness and a shot; an ambush sprung is a firefight at once.
	FFirefightEncounter Attacked;
	TestTrue(TEXT("An enemy attack: a firefight"), ApplyEvent(Attacked, EFirefightEvent::EnemyAttack, 0.0));
	FFirefightEncounter Ambush;
	TestTrue(TEXT("An ambush sprung: a firefight"), ApplyEvent(Ambush, EFirefightEvent::AmbushSprung, 0.0));
	TestTrue(TEXT("... marked as an ambush"), Ambush.bAmbushSprung);
	return true;
}

FIREFIGHT_TEST(FFirefightEndTest, "EndsWhenDownOrContactLost")
bool FFirefightEndTest::RunTest(const FString&)
{
	using namespace FirefightRules;
	FFirefightConfig Config;
	Config.LostContactSeconds = 20.f;
	FFirefightEncounter Encounter;
	TestTrue(TEXT("No encounter: nothing ends"), CheckEnd(Encounter, 0, 100.0, Config) == EFirefightEnd::None);
	ApplyEvent(Encounter, EFirefightEvent::EnemyAttack, 10.0);
	TestTrue(TEXT("Engaged enemies alive, contact fresh: on"), CheckEnd(Encounter, 2, 15.0, Config) == EFirefightEnd::None);
	TestTrue(TEXT("Every engaged enemy down: over"), CheckEnd(Encounter, 0, 15.0, Config) == EFirefightEnd::AllDown);
	TestTrue(TEXT("19.9 s without contact: still on"), CheckEnd(Encounter, 1, 29.9, Config) == EFirefightEnd::None);
	TestTrue(TEXT("20 s without contact: lost"), CheckEnd(Encounter, 1, 30.0, Config) == EFirefightEnd::LostContact);
	KeepContact(Encounter, 29.0); // an engaged enemy still hunts the squad nearby
	TestTrue(TEXT("Kept contact moves the clock"), CheckEnd(Encounter, 1, 30.0, Config) == EFirefightEnd::None);
	// A new contact after the end starts a fresh encounter (counted again).
	FFirefightEncounter Ended;
	ApplyEvent(Ended, EFirefightEvent::EnemyAttack, 0.0);
	Ended = FFirefightEncounter(); // the subsystem resets it at the end
	TestTrue(TEXT("Next encounter counts again"), ApplyEvent(Ended, EFirefightEvent::EnemyAttack, 50.0));
	return true;
}

FIREFIGHT_TEST(FFirefightStreakTest, "StreakToTheThreshold")
bool FFirefightStreakTest::RunTest(const FString&)
{
	FFirefightStreak Streak;
	FFirefightConfig Config; // hint 1 firefight before
	for (int32 Index = 1; Index < 6; ++Index)
	{
		TestFalse(*FString::Printf(TEXT("Firefight %d of 6: no horde"), Index), Streak.AddFirefight(6));
	}
	TestEqual(TEXT("5 in a row"), Streak.Count, 5);
	TestTrue(TEXT("Hint at 1 left"), FirefightRules::ShouldHint(Streak.Remaining(6), Config));
	TestFalse(TEXT("No hint at 2 left"), FirefightRules::ShouldHint(2, Config));
	TestTrue(TEXT("The 6th: the horde"), Streak.AddFirefight(6));
	TestEqual(TEXT("... the streak starts again"), Streak.Count, 0);
	TestEqual(TEXT("... 6 on the level"), Streak.Total, 6);
	TestEqual(TEXT("... one horde"), Streak.Hordes, 1);
	TestFalse(TEXT("No hint after the horde"), FirefightRules::ShouldHint(0, Config));

	// Slip-past resets a running streak (and says so), a zero streak has nothing to reset.
	Streak.AddFirefight(6);
	Streak.AddFirefight(6);
	TestTrue(TEXT("Slip-past resets 2"), Streak.SlipPast());
	TestEqual(TEXT("... to 0"), Streak.Count, 0);
	TestFalse(TEXT("Nothing to reset"), Streak.SlipPast());
	TestEqual(TEXT("One reset counted"), Streak.SlipResets, 1);
	TestEqual(TEXT("Total kept"), Streak.Total, 8);

	// Threshold 1 (min): every firefight brings it; never 0 or below.
	FFirefightStreak One;
	TestTrue(TEXT("Threshold 1"), One.AddFirefight(1));
	TestTrue(TEXT("Threshold 0 acts as 1"), One.AddFirefight(0));
	FFirefightConfig NoHint;
	NoHint.HintFirefightsBefore = 0;
	TestFalse(TEXT("Hint off"), FirefightRules::ShouldHint(1, NoHint));
	TestEqual(TEXT("Console threshold wins"), FirefightRules::ResolveThreshold(6, 2), 2);
	TestEqual(TEXT("Console -1: the level's"), FirefightRules::ResolveThreshold(4, -1), 4);
	TestEqual(TEXT("Level value at least 1"), FirefightRules::ResolveThreshold(0, -1), 1);
	return true;
}

FIREFIGHT_TEST(FFirefightSlipPastTest, "SlipPastResetsTheStreak")
bool FFirefightSlipPastTest::RunTest(const FString&)
{
	using namespace FirefightRules;
	FFirefightConfig Config;
	Config.SlipExitFactor = 1.25f;
	const float Reach = GroupReach(2000.f, Config);
	TestEqual(TEXT("Reach = the vision range inside the clamp"), Reach, 2000.f);
	TestEqual(TEXT("Muted ambusher: the minimum reach"), GroupReach(300.f, Config), Config.SlipMinReachCm);
	TestEqual(TEXT("Armed human's long sight: capped"), GroupReach(12000.f, Config), Config.SlipMaxReachCm);

	// Approach, pass by unseen, leave: slipped past.
	FSlipGroupState Group;
	TestTrue(TEXT("Far: nothing"), UpdateGroup(Group, 5000.f, Reach, false, false, Config) == ESlipVerdict::None);
	TestTrue(TEXT("Within the reach"), UpdateGroup(Group, 1800.f, Reach, false, false, Config) == ESlipVerdict::EnteredReach);
	TestTrue(TEXT("Still near: nothing"), UpdateGroup(Group, 1500.f, Reach, false, false, Config) == ESlipVerdict::None);
	TestTrue(TEXT("Just out of the reach (hysteresis): nothing"), UpdateGroup(Group, 2400.f, Reach, false, false, Config) == ESlipVerdict::None);
	TestTrue(TEXT("Beyond reach x 1.25: slipped past"), UpdateGroup(Group, 2600.f, Reach, false, false, Config) == ESlipVerdict::SlippedPast);
	TestTrue(TEXT("... once"), UpdateGroup(Group, 3000.f, Reach, false, false, Config) == ESlipVerdict::None);

	// A group that spotted the squad never counts, even when the squad leaves it later.
	FSlipGroupState Spotted;
	UpdateGroup(Spotted, 1000.f, Reach, false, false, Config);
	TestTrue(TEXT("It spots the squad: nothing"), UpdateGroup(Spotted, 1000.f, Reach, true, false, Config) == ESlipVerdict::None);
	TestTrue(TEXT("... leaving it later: no slip-past"), UpdateGroup(Spotted, 6000.f, Reach, false, false, Config) == ESlipVerdict::None);
	TestTrue(TEXT("... latched"), Spotted.bEngaged);

	// A group never within the reach that goes away: nothing; one that goes away while the squad is in its reach (recycled): slipped past.
	FSlipGroupState Distant;
	UpdateGroup(Distant, 9000.f, Reach, false, false, Config);
	TestTrue(TEXT("Gone, never near: nothing"), UpdateGroup(Distant, TNumericLimits<float>::Max(), 0.f, false, true, Config) == ESlipVerdict::None);
	FSlipGroupState Recycled;
	UpdateGroup(Recycled, 1000.f, Reach, false, false, Config);
	TestTrue(TEXT("Gone while in the reach: slipped past"), UpdateGroup(Recycled, TNumericLimits<float>::Max(), 0.f, false, true, Config) == ESlipVerdict::SlippedPast);
	// Killed by the squad: engaged (its contact) - no slip-past.
	FSlipGroupState Killed;
	UpdateGroup(Killed, 1000.f, Reach, false, false, Config);
	TestTrue(TEXT("Shot dead: no slip-past"), UpdateGroup(Killed, TNumericLimits<float>::Max(), 0.f, true, true, Config) == ESlipVerdict::None);

	// The whole sequence the designer asked for: 2 firefights, a slip-past, then the count starts again.
	FFirefightStreak Streak;
	Streak.AddFirefight(3);
	Streak.AddFirefight(3);
	FSlipGroupState Patrol;
	UpdateGroup(Patrol, 1000.f, Reach, false, false, Config);
	if (UpdateGroup(Patrol, 9000.f, Reach, false, false, Config) == ESlipVerdict::SlippedPast && Config.bSlipPastResets)
	{
		Streak.SlipPast();
	}
	TestEqual(TEXT("Reset by the slip-past"), Streak.Count, 0);
	TestFalse(TEXT("The next firefight is the 1st of 3 again"), Streak.AddFirefight(3));
	return true;
}

FIREFIGHT_TEST(FFirefightDataTest, "DataKeys")
bool FFirefightDataTest::RunTest(const FString&)
{
	using namespace HordeRules;
	// horde.json / the level "horde" override: the firefight_* keys (meters -> cm).
	FHordeConfig Config;
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"firefight_lost_contact_seconds\": 30, \"firefight_contact_range_m\": 25, \"firefight_slip_past_resets\": false,"
		" \"firefight_slip_min_reach_m\": 10, \"firefight_slip_max_reach_m\": 5, \"firefight_slip_exit_factor\": 0.5, \"firefight_warning_seconds\": 4,"
		" \"firefight_hint_before\": 2}"), Config));
	TestEqual(TEXT("Lost contact"), Config.Firefight.LostContactSeconds, 30.f);
	TestEqual(TEXT("Contact range in cm"), Config.Firefight.ContactRangeCm, 2500.f);
	TestFalse(TEXT("Slip-past reset off"), Config.Firefight.bSlipPastResets);
	TestEqual(TEXT("Min reach"), Config.Firefight.SlipMinReachCm, 1000.f);
	TestEqual(TEXT("Max reach never below the min"), Config.Firefight.SlipMaxReachCm, 1000.f);
	TestEqual(TEXT("Exit factor at least 1"), Config.Firefight.SlipExitFactor, 1.f);
	TestEqual(TEXT("Warning lead"), Config.Firefight.WarningLeadSeconds, 4.f);
	TestEqual(TEXT("Hint"), Config.Firefight.HintFirefightsBefore, 2);

	// The shipped horde.json carries the keys with the designer's defaults.
	const FHordeConfig Shipped = LoadDefaults();
	TestEqual(TEXT("Shipped: 20 s lost contact"), Shipped.Firefight.LostContactSeconds, 20.f);
	TestEqual(TEXT("Shipped: 10 s warning"), Shipped.Firefight.WarningLeadSeconds, 10.f);
	TestTrue(TEXT("Shipped: slip-past resets"), Shipped.Firefight.bSlipPastResets);

	// The level JSON root key horde_after_firefights (Training Ground waves tab): default 10 (game designer 2026-11-09), min 1.
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("Level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"t\", \"horde_after_firefights\": 2, \"waves\": []}"), TEXT("t"), Level, Error));
	TestEqual(TEXT("Threshold 2"), Level.HordeAfterFirefights, 2);
	TestEqual(TEXT("... resolved for the level"), ResolveForLevel(FHordeConfig(), Level.bHordeEnabled, Level.HordeOverrideJson, Level.HordeAfterFirefights).AfterFirefights, 2);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("Plain level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"p\", \"waves\": []}"), TEXT("p"), Plain, Error));
	TestEqual(TEXT("Default 10"), Plain.HordeAfterFirefights, 10);
	FLevelCombatConfig Zero;
	TestTrue(TEXT("Zero parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"z\", \"horde_after_firefights\": 0, \"waves\": []}"), TEXT("z"), Zero, Error));
	TestEqual(TEXT("Min 1"), Zero.HordeAfterFirefights, 1);
	FLevelCombatConfig Off;
	TestTrue(TEXT("Off parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"o\", \"horde_enabled\": false, \"horde_after_firefights\": 1, \"waves\": []}"),
		TEXT("o"), Off, Error));
	TestFalse(TEXT("horde_enabled false: never"), ResolveForLevel(FHordeConfig(), Off.bHordeEnabled, Off.HordeOverrideJson, Off.HordeAfterFirefights).bEnabled);
	return true;
}

FIREFIGHT_TEST(FFirefightHordeVariantTest, "HordeMilitaryZombieNoSpitter")
bool FFirefightHordeVariantTest::RunTest(const FString&)
{
	using namespace HordeRules;
	FHordeConfig Config;
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"composition\": {\"FROST_HOUND\": 2, \"MILITARY_ZOMBIE\": 1, \"SPITTER\": 5}}"), Config));
	TestEqual(TEXT("The Spitter refused, the Military Zombie kept"), Config.Composition.Num(), 2);
	const TArray<FPressureEnemyType> Members = BuildMembers(Config, 6);
	const int32 Zombies = Members.FilterByPredicate([](const FPressureEnemyType& Each)
	{
		return Each.Type == EEnemyArchetype::Frostbitten && Each.Variant == CombatPressureRules::MilitaryZombieVariant();
	}).Num();
	const int32 Hounds = Members.FilterByPredicate([](const FPressureEnemyType& Each) { return Each.Type == EEnemyArchetype::FrostHound && Each.Variant.IsNone(); }).Num();
	TestEqual(TEXT("2 Military Zombies (a Frostbitten variant)"), Zombies, 2);
	TestEqual(TEXT("4 plain hounds"), Hounds, 4);
	TestFalse(TEXT("No Spitter"), Members.ContainsByPredicate([](const FPressureEnemyType& Each) { return Each.Type == EEnemyArchetype::Spitter; }));
	TestEqual(TEXT("BuildComposition keeps the archetypes"), BuildComposition(Config, 6).FilterByPredicate([](EEnemyArchetype T) { return T == EEnemyArchetype::Frostbitten; }).Num(), 2);
	FHordeConfig OnlySpitters;
	ApplyJsonText(TEXT("{\"composition\": {\"SPITTER\": 1}}"), OnlySpitters);
	TestEqual(TEXT("Only Spitters: the default mix stays"), OnlySpitters.Composition.Num(), 2);
	TestFalse(TEXT("Spitter refused"), IsAllowedHordeType(EEnemyArchetype::Spitter));
	TestTrue(TEXT("Brute allowed"), IsAllowedHordeType(EEnemyArchetype::Brute));
	// The shipped horde.json has no Spitter.
	for (const FHordeMixEntry& Entry : LoadDefaults().Composition)
	{
		TestTrue(TEXT("Shipped composition: no Spitter"), Entry.Type != EEnemyArchetype::Spitter);
	}
	return true;
}

FIREFIGHT_TEST(FFirefightThresholdTenTest, "ThresholdDefaultTenHintAtNine")
bool FFirefightThresholdTenTest::RunTest(const FString&)
{
	using namespace FirefightRules;
	// Game designer 2026-11-09: the horde after 10 firefights in a row (was 6) - every place the default lives agrees.
	TestEqual(TEXT("FirefightRules default"), DefaultAfterFirefights, 10);
	TestEqual(TEXT("FHordeConfig default"), FHordeConfig().AfterFirefights, 10);
	TestEqual(TEXT("FLevelCombatConfig default"), FLevelCombatConfig().HordeAfterFirefights, 10);
	TestEqual(TEXT("ResolveForLevel default argument"), HordeRules::ResolveForLevel(FHordeConfig(), true, FString()).AfterFirefights, 10);
	TestEqual(TEXT("No console override: the level's value"), ResolveThreshold(DefaultAfterFirefights, -1), 10);

	// The streak: 9 firefights - the HQ hint (one more and the horde comes), no horde; the 10th brings the horde and starts again.
	const FFirefightConfig Config; // HintFirefightsBefore 1
	FFirefightStreak Streak;
	int32 HintAt = 0;
	for (int32 Index = 1; Index <= 9; ++Index)
	{
		TestFalse(FString::Printf(TEXT("Firefight %d: no horde"), Index), Streak.AddFirefight(DefaultAfterFirefights));
		if (ShouldHint(Streak.Remaining(DefaultAfterFirefights), Config))
		{
			TestEqual(TEXT("Only one hint"), HintAt, 0);
			HintAt = Index;
		}
	}
	TestEqual(TEXT("The HQ hint after the 9th firefight"), HintAt, 9);
	TestTrue(TEXT("The 10th in a row: the horde"), Streak.AddFirefight(DefaultAfterFirefights));
	TestEqual(TEXT("... and the count starts again"), Streak.Count, 0);
	return true;
}

FIREFIGHT_TEST(FFirefightOnlyTriggerTest, "NoTimerHordeOnlyTheStreak")
bool FFirefightOnlyTriggerTest::RunTest(const FString&)
{
	using namespace HordeRules;
	// Game designer 2026-11-09: no horde after 240 s of real-time fight any more - the old keys change nothing.
	FHordeConfig Old;
	TestTrue(TEXT("An old override with the timer keys still parses"),
		ApplyJsonText(TEXT("{\"trigger_seconds\": 5, \"repeats\": true, \"repeat_seconds\": 5, \"count_increase_per_repeat\": 7}"), Old));
	const FHordeConfig Defaults;
	TestEqual(TEXT("... the horde size unchanged"), GetHordeSize(Old), GetHordeSize(Defaults));
	TestEqual(TEXT("... the horde size is count"), GetHordeSize(Defaults), Defaults.Count);

	// The shipped horde.json no longer carries the timer keys.
	FString Text;
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("horde.json read"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid());
	if (Root.IsValid())
	{
		for (const TCHAR* Key : { TEXT("trigger_seconds"), TEXT("repeats"), TEXT("repeat_seconds"), TEXT("count_increase_per_repeat") })
		{
			TestFalse(FString::Printf(TEXT("horde.json: no '%s'"), Key), Root->HasField(Key));
		}
	}

	// The firefight horde's warning countdown runs in running game time only (exploration or a real-time fight).
	TestTrue(TEXT("Exploration runs"), IsCountdownRunning(ECodexGamePhase::Exploration, ECodexCombatMode::None, false));
	TestTrue(TEXT("Real-time fight runs"), IsCountdownRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, false));
	TestFalse(TEXT("Tactical pause holds it"), IsCountdownRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, false));
	TestFalse(TEXT("Turn-based holds it"), IsCountdownRunning(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, false));
	TestFalse(TEXT("A dialogue / cutscene AI pause holds it"), IsCountdownRunning(ECodexGamePhase::Exploration, ECodexCombatMode::None, true));
	TestFalse(TEXT("Preparation holds it"), IsCountdownRunning(ECodexGamePhase::Preparation, ECodexCombatMode::None, false));

	// The shipped levels (2026-11-09): the horde ON at the Depot (threshold 10), OFF at the Lake and the PMC arenas.
	auto Load = [this](const TCHAR* File, FLevelCombatConfig& Out)
	{
		FString Json;
		FString Error;
		const bool bOk = FFileHelper::LoadFileToString(Json, *(LevelJsonRules::GetLevelsDirectory() / File))
			&& LevelJsonRules::ParseLevel(Json, FPaths::GetBaseFilename(File), Out, Error);
		TestTrue(FString::Printf(TEXT("%s parsed %s"), File, *Error), bOk);
		return bOk;
	};
	FLevelCombatConfig Depot;
	if (Load(TEXT("L_Gen_Depot_1.json"), Depot))
	{
		const FHordeConfig Resolved = ResolveForLevel(FHordeConfig(), Depot.bHordeEnabled, Depot.HordeOverrideJson, Depot.HordeAfterFirefights);
		TestTrue(TEXT("Depot: the horde on"), Resolved.bEnabled);
		TestEqual(TEXT("Depot: after 10 firefights"), Resolved.AfterFirefights, 10);
	}
	for (const TCHAR* File : { TEXT("L_Demo01_A_FrozenLake.json"), TEXT("L_PmcArena.json"), TEXT("L_PmcArenaDepot.json") })
	{
		FLevelCombatConfig Level;
		if (Load(File, Level))
		{
			TestFalse(FString::Printf(TEXT("%s: no horde"), File), Level.bHordeEnabled);
		}
	}
	return true;
}

#undef FIREFIGHT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
