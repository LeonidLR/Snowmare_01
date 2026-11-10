#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcOperatorRules.h"
#include "Combat/LauncherTacticsRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Combat/SuppressionRules.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

// Who fires the RPG-7 and when (the game designer's answers on the RPG-7, 2026-11-09, item 7; LauncherTacticsRules + SuppressionRules
// "launcher.*"; new - no Godot reference): the weapon choice per target (reasons, vetoes, the threshold), the player's pick, the tuning file,
// and the blast's suppression radius / holds.

#define RPGAI_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Weapons.RpgAI." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LauncherTacticsTest
{
	/** A seen target 30 m away, rockets in the tube + 3, nothing round it. */
	FLauncherTargetView Lone()
	{
		FLauncherTargetView View;
		View.DistanceCm = 3000.f;
		View.MinRangeCm = 1000.f;
		View.MaxRangeCm = 15000.f;
		View.RocketsLeft = 4;
		return View;
	}
}

RPGAI_TEST(FRpgAiWeaponChoiceTest, "WeaponChoice")
bool FRpgAiWeaponChoiceTest::RunTest(const FString&)
{
	using namespace LauncherTacticsRules;
	using namespace LauncherTacticsTest;
	const FLauncherTacticsConfig Config;

	// A single exposed man: the rifle (weight_single_exposed 0).
	FLauncherChoice Choice = Evaluate(Config, Lone(), Config.Threshold);
	TestFalse(TEXT("a lone man in the open: no rocket (the rifle)"), Choice.bUse);
	TestEqual(TEXT("... no reason"), Choice.Reason, ELauncherUse::None);

	// Clusters: 2 within the 4 m blast = (2 - 1) / (3 - 1) x 1.2 = 0.6 (>= 0.5: a rocket), 3 = 1.2.
	FLauncherTargetView Two = Lone();
	Two.ClusterCount = 2;
	Choice = Evaluate(Config, Two, Config.Threshold);
	TestTrue(TEXT("two enemies within the blast: a rocket"), Choice.bUse && Choice.Reason == ELauncherUse::Cluster);
	TestEqual(TEXT("... score 0.6"), Choice.Score, 0.6f, 1e-4f);
	FLauncherTargetView Three = Lone();
	Three.ClusterCount = 3;
	TestEqual(TEXT("three: score 1.2 (full)"), Evaluate(Config, Three, Config.Threshold).Score, 1.2f, 1e-4f);
	Three.ClusterCount = 6;
	TestEqual(TEXT("six: still 1.2 (capped)"), Evaluate(Config, Three, Config.Threshold).Score, 1.2f, 1e-4f);
	TestEqual(TEXT("cluster factor of one: 0"), ClusterFactor(Config, 1), 0.f);

	// Cover the rifle cannot beat: 0.8.
	FLauncherTargetView Covered = Lone();
	Covered.bInCover = true;
	Choice = Evaluate(Config, Covered, Config.Threshold);
	TestTrue(TEXT("a man behind cover / a barricade: a rocket"), Choice.bUse && Choice.Reason == ELauncherUse::Cover);
	TestEqual(TEXT("... score 0.8"), Choice.Score, 0.8f, 1e-4f);

	// A heavy target (vehicle / turret / mortar crew): 1.5, the strongest reason.
	FLauncherTargetView Heavy = Covered;
	Heavy.bHeavy = true;
	Choice = Evaluate(Config, Heavy, Config.Threshold);
	TestTrue(TEXT("a heavy target: a rocket, the heavy reason wins over cover"), Choice.bUse && Choice.Reason == ELauncherUse::Heavy && FMath::IsNearlyEqual(Choice.Score, 1.5f));

	// Suppressing a group: 2+ within 9 m that is a threat (fires at us / closes in) - 0.7; the same group idle: nothing.
	FLauncherTargetView Group = Lone();
	Group.SuppressGroup = 3;
	TestFalse(TEXT("an idle group spread over 9 m (none within the blast): no rocket"), Evaluate(Config, Group, Config.Threshold).bUse);
	Group.bGroupThreat = true;
	Choice = Evaluate(Config, Group, Config.Threshold);
	TestTrue(TEXT("the same group firing at us / advancing: a suppressing rocket"), Choice.bUse && Choice.Reason == ELauncherUse::Suppress);
	TestEqual(TEXT("... score 0.7"), Choice.Score, 0.7f, 1e-4f);
	FLauncherTargetView LoneThreat = Lone();
	LoneThreat.bGroupThreat = true;
	TestFalse(TEXT("one man firing at us: no group, the rifle"), Evaluate(Config, LoneThreat, Config.Threshold).bUse);

	// The vetoes, in their order.
	FLauncherTargetView Veto = Three;
	Veto.DistanceCm = 600.f;
	Choice = Evaluate(Config, Veto, Config.Threshold);
	TestTrue(TEXT("a cluster 6 m away (inside the 10 m minimum range): refused"), !Choice.bUse && Choice.Veto == ELauncherVeto::TooClose);
	TestTrue(TEXT("... the score is still reported (the log's decision path)"), Choice.Score > 1.f);
	Veto = Three;
	Veto.DistanceCm = 16000.f;
	TestEqual(TEXT("past the reach"), Evaluate(Config, Veto, Config.Threshold).Veto, ELauncherVeto::OutOfRange);
	Veto = Three;
	Veto.FriendliesNear = 1;
	TestEqual(TEXT("a mate within 6 m of the impact"), Evaluate(Config, Veto, Config.Threshold).Veto, ELauncherVeto::FriendlyNear);
	Veto = Three;
	Veto.bLineBlockedShort = true;
	TestEqual(TEXT("the line blocked short of the target"), Evaluate(Config, Veto, Config.Threshold).Veto, ELauncherVeto::NoLine);
	Veto = Three;
	Veto.RocketsLeft = 0;
	TestEqual(TEXT("no rockets"), Evaluate(Config, Veto, Config.Threshold).Veto, ELauncherVeto::NoRockets);
	Veto.DistanceCm = 500.f;
	TestEqual(TEXT("no rockets comes first"), Evaluate(Config, Veto, Config.Threshold).Veto, ELauncherVeto::NoRockets);

	// The last rocket kept for a heavy target (data: keep_rockets_for_heavy; 0 = none kept by default).
	FLauncherTargetView Last = Three;
	Last.RocketsLeft = 1;
	TestTrue(TEXT("default: the last rocket goes at a cluster"), Evaluate(Config, Last, Config.Threshold).bUse);
	FLauncherTacticsConfig Keep;
	Keep.KeepRocketsForHeavy = 1.f;
	TestEqual(TEXT("keep 1: the last rocket is not spent on a cluster"), Evaluate(Keep, Last, Keep.Threshold).Veto, ELauncherVeto::SavedForHeavy);
	Last.bHeavy = true;
	TestTrue(TEXT("... but on a heavy target"), Evaluate(Keep, Last, Keep.Threshold).bUse);
	Last.bHeavy = false;
	Last.RocketsLeft = 2;
	TestTrue(TEXT("keep 1 with two left: the cluster gets one"), Evaluate(Keep, Last, Keep.Threshold).bUse);

	// The ordered strike's looser threshold; a raised threshold drops the two-man cluster.
	TestTrue(TEXT("ordered threshold 0.3: the two-man cluster"), Evaluate(Config, Two, Config.OrderedThreshold).bUse);
	TestFalse(TEXT("threshold 0.7: not the two-man cluster"), Evaluate(Config, Two, 0.7f).bUse);
	FLauncherTacticsConfig Eager;
	Eager.WeightSingleExposed = 0.6f;
	Choice = Evaluate(Eager, Lone(), Eager.Threshold);
	TestTrue(TEXT("weight_single_exposed 0.6: a rocket at a lone man too"), Choice.bUse && Choice.Reason == ELauncherUse::Single);

	// Picking the best usable target.
	TArray<FLauncherChoice> Choices;
	Choices.Add(Evaluate(Config, Lone(), Config.Threshold));
	Choices.Add(Evaluate(Config, Two, Config.Threshold));
	Choices.Add(Evaluate(Config, Veto, Config.Threshold));
	Choices.Add(Evaluate(Config, Covered, Config.Threshold));
	TestEqual(TEXT("the best usable: the man in cover (0.8 > 0.6; the vetoed cluster skipped)"), PickBest(Choices), 3);
	TestEqual(TEXT("none usable"), PickBest({ Evaluate(Config, Lone(), Config.Threshold) }), INDEX_NONE);

	// Counting round the aim point.
	const TArray<FVector> Points = { FVector(0.f), FVector(300.f, 0.f, 0.f), FVector(0.f, 399.f, 0.f), FVector(500.f, 0.f, 0.f), FVector(0.f, 0.f, 800.f) };
	TestEqual(TEXT("three within 4 m"), CountWithin(Points, FVector::ZeroVector, 400.f), 3);
	TestEqual(TEXT("five within 9 m"), CountWithin(Points, FVector::ZeroVector, 900.f), 5);

	// The player's pick holds player_choice_hold_s.
	TestFalse(TEXT("never picked"), IsPlayerPickHeld(Config, -1.f));
	TestTrue(TEXT("picked 10 s ago: held"), IsPlayerPickHeld(Config, 10.f));
	TestFalse(TEXT("picked 50 s ago: the rule may switch again"), IsPlayerPickHeld(Config, 50.f));
	return true;
}

RPGAI_TEST(FRpgAiTuningTest, "TuningFileMirrorsDefaults")
bool FRpgAiTuningTest::RunTest(const FString&)
{
	using namespace LauncherTacticsRules;
	FString Text;
	if (!TestTrue(TEXT("launcher_tactics.json readable"), FFileHelper::LoadFileToString(Text, *GetDefaultPath())))
	{
		return false;
	}
	FLauncherTacticsConfig FromFile;
	TArray<FString> Unknown;
	TestTrue(TEXT("the file parses"), ApplyJson(Text, FromFile, &Unknown));
	TestEqual(TEXT("no unknown keys in the file"), Unknown.Num(), 0);
	const FLauncherTacticsConfig Defaults;
	for (const FString& Key : KnownKeys())
	{
		float File = 0.f;
		float Code = 0.f;
		GetValue(FromFile, Key, File);
		GetValue(Defaults, Key, Code);
		TestEqual(FString::Printf(TEXT("%s: file = code default"), *Key), File, Code, 1e-4f);
	}
	FLauncherTacticsConfig Patched;
	TArray<FString> PatchUnknown;
	TestTrue(TEXT("a partial file applies"), ApplyJson(TEXT("{\"params\": {\"cluster.radius_m\": 5, \"nope\": 1}}"), Patched, &PatchUnknown));
	TestEqual(TEXT("... metres -> cm"), Patched.ClusterRadiusCm, 500.f);
	TestEqual(TEXT("... the unknown key reported"), PatchUnknown.Num(), 1);
	TestFalse(TEXT("no params object: refused"), ApplyJson(TEXT("{}"), Patched));
	return true;
}

RPGAI_TEST(FRpgAiSuppressionRadiusTest, "SuppressionRadius")
bool FRpgAiSuppressionRadiusTest::RunTest(const FString&)
{
	using namespace SuppressionRules;
	const FSuppressionConfig Config;
	TestEqual(TEXT("the radius: 9 m (the designer: 8-10 m)"), Config.LauncherRadiusCm, 900.f);
	float Pinned = 0.f;
	float Suppressed = 0.f;
	LauncherHold(Config, 0.f, Pinned, Suppressed);
	TestTrue(TEXT("at the burst: pinned 3 s, suppressed 5 s"), FMath::IsNearlyEqual(Pinned, 3.f) && FMath::IsNearlyEqual(Suppressed, 5.f));
	LauncherHold(Config, 450.f, Pinned, Suppressed);
	TestTrue(TEXT("4.5 m (half the radius): x0.75 - pinned 2.25 s, suppressed 3.75 s"), FMath::IsNearlyEqual(Pinned, 2.25f) && FMath::IsNearlyEqual(Suppressed, 3.75f));
	LauncherHold(Config, 700.f, Pinned, Suppressed);
	TestTrue(TEXT("7 m (past the 6 m pin radius): suppressed only"), Pinned == 0.f && Suppressed > 2.5f);
	LauncherHold(Config, 900.f, Pinned, Suppressed);
	TestTrue(TEXT("9 m (the edge): suppressed x0.5 = 2.5 s"), Pinned == 0.f && FMath::IsNearlyEqual(Suppressed, 2.5f));
	LauncherHold(Config, 950.f, Pinned, Suppressed);
	TestTrue(TEXT("9.5 m: nothing"), Pinned == 0.f && Suppressed == 0.f);

	// The holds drive the tier whatever the level does.
	FSuppressionState State;
	LauncherHold(Config, 200.f, Pinned, Suppressed);
	AddHold(Config, State, Pinned, Suppressed);
	TestEqual(TEXT("2 m from the burst: PINNED at once (no fire, he ducks)"), State.Tier, ESuppressionTier::Pinned);
	TestTrue(TEXT("... the enemy may not fire"), !EnemyMayFire(State.Tier) && !OperativeFire(Config, State.Tier).bMayFire);
	TestEqual(TEXT("... the level itself untouched (no gunfire)"), State.Level, 0.f);
	const float PinFor = Pinned;
	Step(Config, State, PinFor - 0.1f);
	TestEqual(TEXT("still pinned just before the pin runs out"), State.Tier, ESuppressionTier::Pinned);
	Step(Config, State, 0.2f);
	TestEqual(TEXT("then suppressed (blind fire, no peek)"), State.Tier, ESuppressionTier::Suppressed);
	TestFalse(TEXT("... he may not expose himself"), MayExpose(State.Tier));
	Step(Config, State, Suppressed - PinFor);
	TestEqual(TEXT("free once the suppressed hold ran out"), State.Tier, ESuppressionTier::None);

	// A second, weaker burst never shortens a running hold; gunfire on top keeps the level's own tier.
	FSuppressionState Twice;
	AddHold(Config, Twice, 3.f, 5.f);
	AddHold(Config, Twice, 1.f, 2.f);
	TestTrue(TEXT("the longer hold stays"), FMath::IsNearlyEqual(Twice.HoldPinned, 3.f) && FMath::IsNearlyEqual(Twice.HoldSuppressed, 5.f));
	FSuppressionState Gunfire;
	AddPressure(Config, Gunfire, 0.9f);
	AddHold(Config, Gunfire, 0.f, 1.f);
	TestEqual(TEXT("a pinning level stays pinned under a suppressed hold"), Gunfire.Tier, ESuppressionTier::Pinned);
	FSuppressionConfig Wide = Config;
	Wide.LauncherRadiusCm = 1000.f;
	LauncherHold(Wide, 950.f, Pinned, Suppressed);
	TestTrue(TEXT("launcher.radius_cm 1000: 9.5 m suppressed"), Suppressed > 0.f);
	return true;
}

RPGAI_TEST(FRpgAiPmcGunnerTest, "PmcGunner")
bool FRpgAiPmcGunnerTest::RunTest(const FString&)
{
	using namespace PmcOperatorRules;
	FPmcOperatorConfig Config;
	auto Gunners = [&Config](int32 Seed, int32 Fighters)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Fighters; ++Index)
		{
			Count += IsRpgGunner(Config, Seed, Index) ? 1 : 0;
		}
		return Count;
	};
	Config.RpgGunner.Share = 1.f;
	bool bExactlyOne = true;
	for (int32 Seed = 0; Seed < 100; ++Seed)
	{
		bExactlyOne &= Gunners(Seed, 3) == 1 && Gunners(Seed, 8) == 1;
	}
	TestTrue(TEXT("share 1: every squad of 3+ has exactly one gunner (max_per_squad 1), never one past slot_span"), bExactlyOne);
	Config.RpgGunner.MaxPerSquad = 2;
	TestEqual(TEXT("max_per_squad 2: two"), Gunners(5, 6), 2);
	Config.RpgGunner.MaxPerSquad = 1;
	Config.RpgGunner.Share = 0.f;
	int32 Any = 0;
	for (int32 Seed = 0; Seed < 100; ++Seed)
	{
		Any += Gunners(Seed, 6);
	}
	TestEqual(TEXT("share 0: no gunners"), Any, 0);
	Config.RpgGunner.Share = 0.5f;
	int32 WithGunner = 0;
	for (int32 Seed = 0; Seed < 400; ++Seed)
	{
		WithGunner += Gunners(Seed, 3) > 0 ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("share 0.5: about half the squads (%d of 400)"), WithGunner), WithGunner > 150 && WithGunner < 250);
	TestTrue(TEXT("deterministic: the same seed + index"), IsRpgGunner(Config, 17, 0) == IsRpgGunner(Config, 17, 0) && IsRpgGunner(Config, 17, 1) == IsRpgGunner(Config, 17, 1));
	Config.RpgGunner.WeaponId.Reset();
	TestFalse(TEXT("no weapon id: no gunner"), IsRpgGunner(Config, 1, 0) || IsRpgGunner(Config, 2, 1));

	// The data file's block.
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"rpg_gunner\": {\"weapon\": \"rpg7\", \"share\": 0.25, \"max_per_squad\": 1, \"slot_span\": 4}}")), Json);
	const FPmcOperatorConfig Parsed = Json.IsValid() ? FromJson(*Json) : FPmcOperatorConfig();
	TestTrue(TEXT("rpg_gunner parsed"), Parsed.RpgGunner.WeaponId == TEXT("rpg7") && FMath::IsNearlyEqual(Parsed.RpgGunner.Share, 0.25f) && Parsed.RpgGunner.SlotSpan == 4);
	const FPmcOperatorConfig& Project = Get();
	TestTrue(TEXT("pmc_operators.json: an RPG-7 gunner, at most one per squad"), Project.RpgGunner.WeaponId == TEXT("rpg7") && Project.RpgGunner.MaxPerSquad == 1
		&& Project.RpgGunner.Share > 0.f);
	return true;
}

RPGAI_TEST(FRpgAiCommanderStrikeTest, "CommanderStrike")
bool FRpgAiCommanderStrikeTest::RunTest(const FString&)
{
	using namespace PmcCommanderRules;
	const FPmcWeights Weights;
	FPmcFighterView Fighter;
	Fighter.bHasThreat = true;
	Fighter.Threat.bSeenNow = true;
	Fighter.Threat.DistanceCm = 2500.f;
	FPmcSquadView Squad;
	Squad.Alive = 3;
	Squad.bContact = true;
	Squad.bEverContact = true;
	TestEqual(TEXT("no launcher: no RpgStrike"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights), 0.f);
	Fighter.bHasLauncher = true;
	TestEqual(TEXT("a launcher but no rocket target (a lone man in the open): 0"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights), 0.f);
	Fighter.LauncherScore = 1.2f;
	TestEqual(TEXT("a cluster: rpg_strike x 1 (capped) x 0.85 (no mate moving)"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights), Weights.RpgStrike * 0.85f, 1e-4f);
	Squad.MatesFlanking = 1;
	TestEqual(TEXT("... a mate flanks: the full weight (the rocket makes them duck)"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights), Weights.RpgStrike, 1e-4f);
	Squad.MatesRpgStriking = 1;
	TestEqual(TEXT("... a mate already fires one: x0.2"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights), Weights.RpgStrike * 0.2f, 1e-4f);
	Squad.MatesRpgStriking = 0;
	Fighter.LauncherScore = 0.8f;
	TestTrue(TEXT("a man in cover in sight: RpgStrike beats Hold / Suppress"), ScoreOrder(EPmcOrder::RpgStrike, Fighter, Squad, Weights)
		> FMath::Max(ScoreOrder(EPmcOrder::Hold, Fighter, Squad, Weights), ScoreOrder(EPmcOrder::Suppress, Fighter, Squad, Weights)));
	TestEqual(TEXT("its name"), FString(OrderName(EPmcOrder::RpgStrike)), FString(TEXT("RpgStrike")));
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"rpg_strike\": 0.4}")), Json);
	TestEqual(TEXT("rpg_strike read from the weights file"), Json.IsValid() ? FromJson(*Json).RpgStrike : 0.f, 0.4f);
	FPmcWeights FromFile;
	TestTrue(TEXT("pmc_commander.json loads"), LoadWeightsFile(DefaultWeightsPath(), FromFile));
	TestEqual(TEXT("pmc_commander.json rpg_strike = the code default"), FromFile.RpgStrike, Weights.RpgStrike);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
