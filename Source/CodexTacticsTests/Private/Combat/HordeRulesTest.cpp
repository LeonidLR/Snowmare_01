// CodexTactics.Combat.Horde.* — the horde (UE-only; HordeRules, UHordeSubsystem): the spawn point inside the 25-45 m band (hidden first),
// the mix, the per-level JSON. The real-time-fight clock tests (TimerCountsOnlyRunningRealTime, TriggersOnceAt240, RepeatOption) went with
// the clock on 2026-11-09 (game designer: only the firefight streak summons the horde - CodexTactics.Combat.Firefight.NoTimerHordeOnlyTheStreak).
// Game designer play-test 2026-11-09 (feat/horde-300m-waves): the firefight horde ~300 m away somewhere new each time (FarSpawn*), the level's
// waves feed the hordes in order (WavesFeed*, WavePlan*), the "horde" block stays the fallback (PlanFromConfigFallback).

#include "Misc/AutomationTest.h"
#include "Combat/HordeRules.h"
#include "Combat/CombatPressureRules.h"
#include "Data/LevelJsonRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define HORDE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Horde." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HORDE_TEST(FHordeSpawnPointDistanceBandTest, "SpawnPointDistanceBand")
bool FHordeSpawnPointDistanceBandTest::RunTest(const FString&)
{
	using namespace HordeRules;
	const FHordeConfig Config;
	TestTrue(TEXT("25 m in"), IsInDistanceBand(2500.f, Config));
	TestTrue(TEXT("45 m in"), IsInDistanceBand(4500.f, Config));
	TestFalse(TEXT("24 m out"), IsInDistanceBand(2400.f, Config));
	TestFalse(TEXT("46 m out"), IsInDistanceBand(4600.f, Config));

	TArray<FHordeSpawnCandidate> Candidates;
	auto Add = [&Candidates](float Distance, bool bReachable, bool bVisible)
	{
		FHordeSpawnCandidate Candidate;
		Candidate.DistanceCm = Distance;
		Candidate.Location = FVector(Distance, 0.f, 0.f);
		Candidate.bReachable = bReachable;
		Candidate.bVisibleToSquad = bVisible;
		Candidates.Add(Candidate);
	};
	Add(1500.f, true, false);  // 0: too close
	Add(6000.f, true, false);  // 1: too far
	Add(3500.f, false, false); // 2: no path
	Add(3500.f, true, true);   // 3: in the band, seen
	Add(4400.f, true, false);  // 4: in the band, hidden
	TestEqual(TEXT("The hidden reachable point in the band wins"), PickSpawnPoint(Candidates, Config), 4);
	FHordeConfig Visible = Config;
	Visible.bPreferOutOfSight = false;
	TestEqual(TEXT("Without the sight preference: the band's middle"), PickSpawnPoint(Candidates, Visible), 3);
	Candidates.RemoveAt(4);
	TestEqual(TEXT("Only a seen one left: taken"), PickSpawnPoint(Candidates, Config), 3);
	Candidates.RemoveAt(3);
	TestEqual(TEXT("Nothing valid: none"), PickSpawnPoint(Candidates, Config), INDEX_NONE);

	// The cluster stays within its radius.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		TestTrue(FString::Printf(TEXT("Member %d inside 4 m"), Index), ClusterOffset(Index, 8, Config.ClusterRadiusCm).Size2D() <= Config.ClusterRadiusCm + 0.1f);
	}
	TestTrue(TEXT("Distinct spots"), !ClusterOffset(1, 8, 400.f).Equals(ClusterOffset(2, 8, 400.f), 10.f));
	return true;
}

HORDE_TEST(FHordeCompositionAndDataTest, "CompositionAndLevelData")
bool FHordeCompositionAndDataTest::RunTest(const FString&)
{
	using namespace HordeRules;
	const FHordeConfig Config;
	const TArray<EEnemyArchetype> Mix = BuildComposition(Config, 8);
	TestEqual(TEXT("8 members"), Mix.Num(), 8);
	TestEqual(TEXT("5 hounds"), Mix.FilterByPredicate([](EEnemyArchetype T) { return T == EEnemyArchetype::FrostHound; }).Num(), 5);
	TestEqual(TEXT("3 frostbitten"), Mix.FilterByPredicate([](EEnemyArchetype T) { return T == EEnemyArchetype::Frostbitten; }).Num(), 3);
	TestEqual(TEXT("Rounding keeps the total"), BuildComposition(Config, 11).Num(), 11);

	FHordeConfig FromJson;
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"count\": 10, \"min_distance_m\": 30, \"max_distance_m\": 50, \"composition\": {\"BRUTE\": 1, \"NOPE\": 3}}"), FromJson));
	TestEqual(TEXT("Count"), FromJson.Count, 10);
	TestEqual(TEXT("Band in cm"), FromJson.MinDistanceCm, 3000.f);
	TestEqual(TEXT("Only known types"), FromJson.Composition.Num(), 1);
	TestFalse(TEXT("Bad JSON changes nothing"), ApplyJsonText(TEXT("not json"), FromJson));

	// Level JSON: "horde_enabled" and the "horde" overrides.
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("Level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"t\", \"horde_enabled\": false, \"horde\": {\"count\": 12}, \"waves\": []}"),
		TEXT("t"), Level, Error));
	TestFalse(TEXT("Level switches it off"), Level.bHordeEnabled);
	const FHordeConfig Resolved = ResolveForLevel(FHordeConfig(), Level.bHordeEnabled, Level.HordeOverrideJson);
	TestFalse(TEXT("Off for that level"), Resolved.bEnabled);
	TestEqual(TEXT("Level override kept"), Resolved.Count, 12);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("Plain level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"p\", \"waves\": []}"), TEXT("p"), Plain, Error));
	TestTrue(TEXT("Default: on"), Plain.bHordeEnabled && ResolveForLevel(FHordeConfig(), Plain.bHordeEnabled, Plain.HordeOverrideJson).bEnabled);
	return true;
}

HORDE_TEST(FHordeWavesFeedTheHordeTest, "WavesFeedTheHordeInOrder")
bool FHordeWavesFeedTheHordeTest::RunTest(const FString&)
{
	using namespace HordeRules;
	// Horde N = wave N, then the last wave repeats.
	TestEqual(TEXT("Horde 1 = wave 1"), WaveForHorde(1, 3), 0);
	TestEqual(TEXT("Horde 2 = wave 2"), WaveForHorde(2, 3), 1);
	TestEqual(TEXT("Horde 3 = wave 3"), WaveForHorde(3, 3), 2);
	TestEqual(TEXT("Horde 4 = the last wave again"), WaveForHorde(4, 3), 2);
	TestEqual(TEXT("Horde 9 = the last wave again"), WaveForHorde(9, 3), 2);
	TestEqual(TEXT("One wave: always it"), WaveForHorde(5, 1), 0);
	TestEqual(TEXT("No waves: none"), WaveForHorde(1, 0), static_cast<int32>(INDEX_NONE));

	// Who feeds: an ambush-start level with waves that never played them as scripted waves.
	const FHordeConfig Config;
	TestTrue(TEXT("Default: waves feed the horde"), Config.bWavesFeedHorde);
	TestTrue(TEXT("Ambush level with waves: the waves"), ShouldWavesFeedHorde(Config, 3, true, false));
	TestFalse(TEXT("No waves: the horde block"), ShouldWavesFeedHorde(Config, 0, true, false));
	TestFalse(TEXT("A button level (its waves are its scripted fights): the horde block"), ShouldWavesFeedHorde(Config, 3, false, false));
	TestFalse(TEXT("Scripted waves already played here: the horde block"), ShouldWavesFeedHorde(Config, 3, true, true));
	FHordeConfig Off;
	TestTrue(TEXT("waves_feed_horde false parsed"), ApplyJsonText(TEXT("{\"waves_feed_horde\": false}"), Off));
	TestFalse(TEXT("... the horde block always"), ShouldWavesFeedHorde(Off, 3, true, false));

	// The level parser drops "is_active": false waves - the hordes walk the ACTIVE waves.
	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("Level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"t\", \"combat_start\": \"ambush\", \"waves\": ["
		"{\"wave_index\": 1, \"max_simultaneous_enemies\": 2, \"spawns\": [{\"enemy_type\": \"FROSTBITTEN\", \"count\": 4}, {\"enemy_type\": \"HOUND\", \"count\": 30}]},"
		"{\"wave_index\": 2, \"is_active\": false, \"spawns\": [{\"enemy_type\": \"BRUTE\", \"count\": 9}]},"
		"{\"wave_index\": 3, \"max_simultaneous_enemies\": 12, \"spawns\": [{\"enemy_type\": \"CUTTER\", \"count\": 5}],"
		" \"wave_modifiers\": {\"enemy_hp_mult\": 1.1, \"enemy_damage_mult\": 1.08, \"enemy_speed_mult\": 1.2}}]}"), TEXT("t"), Level, Error));
	TestEqual(TEXT("The inactive wave skipped"), Level.Waves.Num(), 2);
	if (Level.Waves.Num() != 2)
	{
		return false;
	}
	const FHordePlan First = PlanFromWave(Level.Waves[WaveForHorde(1, Level.Waves.Num())], 1);
	const FHordePlan Second = PlanFromWave(Level.Waves[WaveForHorde(2, Level.Waves.Num())], 2);
	TestEqual(TEXT("Horde 1: 34 members"), First.Members.Num(), 34);
	TestEqual(TEXT("... at most 2 alive"), First.MaxAlive, 2);
	TestEqual(TEXT("Horde 2 = the 2nd ACTIVE wave: 5 cutters"), Second.Members.Num(), 5);
	TestTrue(TEXT("... cutters"), Second.Members.Num() > 0 && Second.Members[0].Enemy.Type == EEnemyArchetype::Cutter);
	TestEqual(TEXT("... its hp modifier"), Second.Modifiers.EnemyHpMult, 1.1f);
	TestEqual(TEXT("... its speed modifier"), Second.Modifiers.EnemySpeedMult, 1.2f);
	TestTrue(TEXT("... modifiers on"), Second.bHasModifiers);
	return true;
}

HORDE_TEST(FHordeWavePlanTest, "WavePlanSpitterToMilitaryZombieAndTheCap")
bool FHordeWavePlanTest::RunTest(const FString&)
{
	using namespace HordeRules;
	// The game designer's wave 3 (Training Ground, 2026-11-09): 55 hounds + 2 spitters + 5 cutters + 6 frostbitten, at most 12 alive.
	FWaveDefinition Wave;
	Wave.MaxSimultaneousEnemies = 12;
	Wave.Modifiers.EnemyHpMult = 1.1f;
	Wave.Modifiers.EnemyDamageMult = 1.08f;
	auto Add = [&Wave](EEnemyArchetype Type, int32 Count, float CustomHealth = 0.f)
	{
		FEnemySpawnEntry& Entry = Wave.Spawns.AddDefaulted_GetRef();
		Entry.EnemyType = Type;
		Entry.Count = Count;
		Entry.CustomHealth = CustomHealth;
	};
	Add(EEnemyArchetype::FrostHound, 55);
	Add(EEnemyArchetype::Spitter, 2);
	Add(EEnemyArchetype::Cutter, 5, 250.f);
	Add(EEnemyArchetype::Frostbitten, 6);
	Add(EEnemyArchetype::Base, 3); // never a horde type
	const FHordePlan Plan = PlanFromWave(Wave, 3);
	TestEqual(TEXT("68 members (the Base entry skipped)"), Plan.Members.Num(), 68);
	if (Plan.Members.Num() != 68)
	{
		return false;
	}
	TestEqual(TEXT("Source wave 3"), Plan.SourceWave, 3);
	TestEqual(TEXT("2 Spitters swapped"), Plan.SpitterSwaps, 2);
	const FPressureEnemyType MilitaryZombie(EEnemyArchetype::Frostbitten, CombatPressureRules::MilitaryZombieVariant());
	int32 Zombies = 0;
	int32 Spitters = 0;
	int32 PlainFrostbitten = 0;
	for (const FHordeMember& Member : Plan.Members)
	{
		Zombies += Member.Enemy.Type == MilitaryZombie.Type && Member.Enemy.Variant == MilitaryZombie.Variant ? 1 : 0;
		Spitters += Member.Enemy.Type == EEnemyArchetype::Spitter ? 1 : 0;
		PlainFrostbitten += Member.Enemy.Type == EEnemyArchetype::Frostbitten && Member.Enemy.Variant.IsNone() ? 1 : 0;
	}
	TestEqual(TEXT("The Spitters come as 2 Military Zombies"), Zombies, 2);
	TestEqual(TEXT("No Spitter in the horde"), Spitters, 0);
	TestEqual(TEXT("6 plain frostbitten"), PlainFrostbitten, 6);
	TestTrue(TEXT("Entry order kept: the hounds first"), Plan.Members[0].Enemy.Type == EEnemyArchetype::FrostHound);
	TestEqual(TEXT("A cutter keeps its entry (custom_stats)"), Plan.Members[57].EntryIndex, 2);
	TestEqual(TEXT("... the entry's custom health"), Plan.Entries[Plan.Members[57].EntryIndex].CustomHealth, 250.f);
	TestEqual(TEXT("At most 12 alive"), Plan.MaxAlive, 12);
	TestEqual(TEXT("Modifiers"), Plan.Modifiers.EnemyHpMult, 1.1f);
	TestTrue(FString::Printf(TEXT("Describe names the kinds (%s)"), *Plan.Describe()),
		Plan.Describe().Contains(TEXT("55 FrostHound")) && Plan.Describe().Contains(TEXT("2 MILITARY_ZOMBIE")));

	// The cap: 12 at the start, then one per fallen member, never more than are left.
	TestEqual(TEXT("12 at the start"), MembersToSpawnNow(Plan, 0, 0), 12);
	TestEqual(TEXT("None while 12 live"), MembersToSpawnNow(Plan, 12, 12), 0);
	TestEqual(TEXT("3 fell: 3 more"), MembersToSpawnNow(Plan, 12, 9), 3);
	TestEqual(TEXT("Never more than are left"), MembersToSpawnNow(Plan, 66, 0), 2);
	TestEqual(TEXT("All came: none"), MembersToSpawnNow(Plan, 68, 0), 0);
	FWaveDefinition Zero = Wave;
	Zero.MaxSimultaneousEnemies = 0;
	TestEqual(TEXT("max_simultaneous_enemies 0: at least 1 alive"), PlanFromWave(Zero, 1).MaxAlive, 1);
	return true;
}

HORDE_TEST(FHordePlanFromConfigTest, "PlanFromConfigFallback")
bool FHordePlanFromConfigTest::RunTest(const FString&)
{
	using namespace HordeRules;
	// No waves feeding (no waves / a scripted-wave level / waves_feed_horde false): the "horde" block / horde.json, all at once.
	const FHordePlan Plan = PlanFromConfig(FHordeConfig());
	TestEqual(TEXT("8 members"), Plan.Members.Num(), 8);
	TestEqual(TEXT("All at once (no cap)"), Plan.MaxAlive, 0);
	TestEqual(TEXT("... all 8 now"), MembersToSpawnNow(Plan, 0, 0), 8);
	TestEqual(TEXT("From the horde block"), Plan.SourceWave, 0);
	TestEqual(TEXT("No entries"), Plan.Entries.Num(), 0);
	TestTrue(TEXT("No entry index"), Plan.Members.Num() > 0 && Plan.Members[0].EntryIndex == INDEX_NONE);
	FHordeConfig Level;
	TestTrue(TEXT("Level horde block"), ApplyJsonText(TEXT("{\"count\": 6, \"composition\": {\"FROST_HOUND\": 2, \"MILITARY_ZOMBIE\": 1}}"), Level));
	const FHordePlan Block = PlanFromConfig(Level);
	TestEqual(TEXT("6 members"), Block.Members.Num(), 6);
	TestEqual(TEXT("2 Military Zombies"), Block.Members.FilterByPredicate([](const FHordeMember& Each)
	{
		return Each.Enemy.Variant == CombatPressureRules::MilitaryZombieVariant();
	}).Num(), 2);
	return true;
}

HORDE_TEST(FHordeFarSpawnTest, "FarSpawnPointBandBearingFallback")
bool FHordeFarSpawnTest::RunTest(const FString&)
{
	using namespace HordeRules;
	const FHordeConfig Config;
	TestEqual(TEXT("Band 260 m"), FarBandMinCm(Config), 26000.f);
	TestEqual(TEXT("Band 340 m"), FarBandMaxCm(Config), 34000.f);
	const TArray<float> Radii = FarProbeRadii(Config);
	TestTrue(TEXT("Probes: 340, 300, 260 m first"), Radii.Num() > 3 && Radii[0] == 34000.f && Radii[1] == 30000.f && Radii[2] == 26000.f);
	bool bDescending = true;
	for (int32 Index = 1; Index < Radii.Num(); ++Index)
	{
		bDescending &= Radii[Index] < Radii[Index - 1];
	}
	TestTrue(TEXT("... farthest first"), bDescending);
	TestEqual(TEXT("... down to the 25 m floor"), Radii.Last(), 2500.f);

	TestEqual(TEXT("Bearing east"), BearingDeg(FVector::ZeroVector, FVector(100.f, 0.f, 0.f)), 0.f);
	TestEqual(TEXT("Bearing -Y = 270"), BearingDeg(FVector::ZeroVector, FVector(0.f, -100.f, 0.f)), 270.f, 0.01f);
	TestFalse(TEXT("350 vs 20 deg: only 30 apart"), IsBearingFarEnough(350.f, 20.f, 90.f));
	TestTrue(TEXT("0 vs 180: far enough"), IsBearingFarEnough(0.f, 180.f, 90.f));

	TArray<FHordeFarCandidate> Candidates;
	auto Add = [&Candidates](float DistanceM, float Bearing, bool bReachable, bool bVisible)
	{
		FHordeFarCandidate& Each = Candidates.AddDefaulted_GetRef();
		Each.DistanceCm = DistanceM * 100.f;
		Each.BearingDeg = Bearing;
		Each.Location = FVector(Each.DistanceCm, 0.f, 0.f).RotateAngleAxis(Bearing, FVector::UpVector);
		Each.bReachable = bReachable;
		Each.bVisibleToSquad = bVisible;
	};
	Add(300.f, 0.f, true, false);   // 0: in the band, hidden, east
	Add(280.f, 180.f, true, false); // 1: in the band, hidden, west
	Add(310.f, 90.f, false, false); // 2: no path
	Add(300.f, 270.f, true, true);  // 3: in the band, seen
	Add(120.f, 45.f, true, false);  // 4: near, hidden
	Add(15.f, 135.f, true, false);  // 5: under the 25 m floor
	FRandomStream Random(7);
	// Random among the hidden in-band points; never the unreachable / seen / near ones.
	TSet<int32> Seen;
	for (int32 Run = 0; Run < 40; ++Run)
	{
		const FHordeFarPick Pick = PickFarSpawnPoint(Candidates, Config, false, 0.f, Random);
		TestTrue(TEXT("A hidden in-band point"), (Pick.Index == 0 || Pick.Index == 1) && Pick.Kind == EHordeFarPick::InBand && !Pick.bVisible);
		Seen.Add(Pick.Index);
	}
	TestEqual(TEXT("... different places across hordes"), Seen.Num(), 2);
	// The previous horde came from the east: the next keeps 90 deg off it.
	for (int32 Run = 0; Run < 20; ++Run)
	{
		const FHordeFarPick Pick = PickFarSpawnPoint(Candidates, Config, true, 10.f, Random);
		TestEqual(TEXT("Previous bearing 10 deg: the west point"), Pick.Index, 1);
		TestFalse(TEXT("... the bearing rule held"), Pick.bBearingRelaxed);
	}
	// A small map (the Depot is 177 x 124 m): nothing in the band - among the farthest hidden ones.
	TArray<FHordeFarCandidate> Small;
	auto AddSmall = [&Small](float DistanceM, float Bearing, bool bVisible)
	{
		FHordeFarCandidate& Each = Small.AddDefaulted_GetRef();
		Each.DistanceCm = DistanceM * 100.f;
		Each.BearingDeg = Bearing;
		Each.bReachable = true;
		Each.bVisibleToSquad = bVisible;
	};
	AddSmall(160.f, 90.f, false);  // 0: the farthest hidden (north)
	AddSmall(150.f, 100.f, false); // 1: within 85 % of it
	AddSmall(90.f, 270.f, false);  // 2: south, nearer
	AddSmall(200.f, 0.f, true);    // 3: farther but seen
	for (int32 Run = 0; Run < 20; ++Run)
	{
		const FHordeFarPick Pick = PickFarSpawnPoint(Small, Config, false, 0.f, Random);
		TestTrue(TEXT("Small map: one of the farthest hidden points"), (Pick.Index == 0 || Pick.Index == 1) && Pick.Kind == EHordeFarPick::Farthest);
	}
	// The previous horde came from the north: south (90 m) is more than half of 160 m - the new bearing wins.
	TestEqual(TEXT("Small map, previous north: the south point"), PickFarSpawnPoint(Small, Config, true, 95.f, Random).Index, 2);
	// Only near points off the previous bearing (40 m vs 160 m): the bearing rule gives way.
	Small[2].DistanceCm = 4000.f;
	const FHordeFarPick Strict = PickFarSpawnPoint(Small, Config, true, 95.f, Random);
	TestTrue(TEXT("Default (relax share 0): a new side even at 40 m vs 160 m"), Strict.Index == 2 && !Strict.bBearingRelaxed);
	// firefight_bearing_relax_share 0.5: points off the old bearing under half as far - the bearing rule gives way.
	FHordeConfig Relaxing = Config;
	TestTrue(TEXT("Relax share parsed"), ApplyJsonText(TEXT("{\"firefight_bearing_relax_share\": 0.5}"), Relaxing) && Relaxing.FirefightBearingRelaxShare == 0.5f);
	const FHordeFarPick Relaxed = PickFarSpawnPoint(Small, Relaxing, true, 95.f, Random);
	TestTrue(TEXT("Relax share 0.5: the bearing rule dropped"), Relaxed.bBearingRelaxed && (Relaxed.Index == 0 || Relaxed.Index == 1));
	// Nothing off the previous bearing at all: dropped even at 0.
	TArray<FHordeFarCandidate> OneSide = Small;
	OneSide.RemoveAt(2);
	TestTrue(TEXT("No point off the bearing: dropped"), PickFarSpawnPoint(OneSide, Config, true, 95.f, Random).bBearingRelaxed);
	// Everything seen: a seen point (flagged).
	TArray<FHordeFarCandidate> AllSeen = Small;
	for (FHordeFarCandidate& Each : AllSeen)
	{
		Each.bVisibleToSquad = true;
	}
	const FHordeFarPick Visible = PickFarSpawnPoint(AllSeen, Config, false, 0.f, Random);
	TestTrue(TEXT("All seen: the farthest seen, flagged"), Visible.Index == 3 && Visible.bVisible);
	// Nothing reachable: none.
	TArray<FHordeFarCandidate> None = Small;
	for (FHordeFarCandidate& Each : None)
	{
		Each.bReachable = false;
	}
	TestTrue(TEXT("Nothing reachable: none"), PickFarSpawnPoint(None, Config, false, 0.f, Random).Kind == EHordeFarPick::None);
	return true;
}

HORDE_TEST(FHordeFarDataTest, "FarSpawnDataKeys")
bool FHordeFarDataTest::RunTest(const FString&)
{
	using namespace HordeRules;
	FHordeConfig Config;
	TestTrue(TEXT("JSON applied"), ApplyJsonText(TEXT("{\"firefight_spawn_distance_m\": 200, \"firefight_spawn_spread_m\": 25, \"firefight_bearing_change_deg\": 400,"
		" \"firefight_spawn_bearings\": 2, \"firefight_fallback_share\": 0.5, \"waves_feed_horde\": false}"), Config));
	TestEqual(TEXT("Distance in cm"), Config.FirefightSpawnDistanceCm, 20000.f);
	TestEqual(TEXT("Spread in cm"), Config.FirefightSpawnSpreadCm, 2500.f);
	TestEqual(TEXT("Bearing change clamped to 180"), Config.FirefightBearingChangeDeg, 180.f);
	TestEqual(TEXT("At least 4 bearings"), Config.FirefightSpawnBearings, 4);
	TestEqual(TEXT("Fallback share"), Config.FirefightFallbackShare, 0.5f);
	TestFalse(TEXT("Waves feed off"), Config.bWavesFeedHorde);
	TestEqual(TEXT("Band 175-225 m"), FarBandMinCm(Config), 17500.f);
	// The shipped horde.json: the designer's 300 m (260-340), 45 deg (2026-11-10, was 90), the waves feed the horde; the dev command keeps 25-45 m.
	const FHordeConfig Shipped = LoadDefaults();
	TestEqual(TEXT("Shipped: 300 m"), Shipped.FirefightSpawnDistanceCm, 30000.f);
	TestEqual(TEXT("Shipped: +- 40 m"), Shipped.FirefightSpawnSpreadCm, 4000.f);
	TestEqual(TEXT("Shipped: 45 deg"), Shipped.FirefightBearingChangeDeg, 45.f);
	TestTrue(TEXT("Shipped: waves feed the horde"), Shipped.bWavesFeedHorde);
	TestEqual(TEXT("Shipped: the dev band stays 25 m"), Shipped.MinDistanceCm, 2500.f);
	TestEqual(TEXT("Shipped: 10 s HQ warning kept"), Shipped.Firefight.WarningLeadSeconds, 10.f);
	return true;
}

#undef HORDE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
