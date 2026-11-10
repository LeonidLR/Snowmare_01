#include "Misc/AutomationTest.h"
#include "Core/LoadoutRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/LevelLoadoutRules.h"
#include "Data/WaveConfigTypes.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// Training Ground pass 1 (2026-11-09): level JSON "loadouts" / "kit" / environment.override_carry and the once-per-level stage loadout.
// UE-only (no Godot reference).

namespace
{
	bool ParseTgLevel(const TCHAR* Json, FLevelCombatConfig& OutConfig)
	{
		FString Error;
		return LevelJsonRules::ParseLevel(Json, TEXT("tg_test"), OutConfig, Error);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonTgLoadoutsTest, "CodexTactics.Data.LevelJson.TgLoadouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonTgLoadoutsTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	if (!TestTrue(TEXT("level parses"), ParseTgLevel(
		TEXT("{\"loadouts\":{\" Commander \":[\"M16\",\"pistol\",\"m16\"],\"slot_1\":[\"shotgun\"],\"engineer\":[\"pistol\"],\"recruit\":[],\"medic_sapper\":\"m16\"}}"),
		Config)))
	{
		return false;
	}
	const FLevelWeaponLoadout* Commander = Config.Loadouts.Find(TEXT("commander"));
	if (TestNotNull(TEXT("key trimmed + lower-cased"), Commander))
	{
		TestTrue(TEXT("ids lower-cased, duplicates dropped, order kept"), Commander->WeaponIds == TArray<FString>({ TEXT("m16"), TEXT("pistol") }));
	}
	TestFalse(TEXT("an empty list counts as absent"), Config.Loadouts.Contains(TEXT("recruit")));
	TestFalse(TEXT("a non-array value is skipped"), Config.Loadouts.Contains(TEXT("medic_sapper")));
	TestEqual(TEXT("three valid entries"), Config.Loadouts.Num(), 3);

	const FLevelWeaponLoadout* ById = LevelLoadoutRules::FindPerOperative(Config.Loadouts, TEXT("slot_1"), TEXT("engineer"));
	if (TestNotNull(TEXT("operative id found"), ById))
	{
		TestEqual(TEXT("the operative id wins over the role key"), ById->WeaponIds[0], FString(TEXT("shotgun")));
	}
	const FLevelWeaponLoadout* ByRole = LevelLoadoutRules::FindPerOperative(Config.Loadouts, TEXT("slot_0"), TEXT("commander"));
	TestTrue(TEXT("falls back to the role key"), ByRole == Commander);
	TestTrue(TEXT("lookup keys are normalized"), LevelLoadoutRules::FindPerOperative(Config.Loadouts, TEXT(" SLOT_1 "), FString()) == ById);
	TestNull(TEXT("no entry = today's behaviour"), LevelLoadoutRules::FindPerOperative(Config.Loadouts, TEXT("slot_2"), TEXT("medic_sapper")));
	TestEqual(TEXT("slot id"), LevelLoadoutRules::MakeOperativeId(2), FString(TEXT("slot_2")));
	TestTrue(TEXT("no id for a negative index"), LevelLoadoutRules::MakeOperativeId(-1).IsEmpty());

	TArray<FString> Known;
	TArray<FString> Unknown;
	const auto IsKnown = [](const FString& Id) { return Id == TEXT("m16") || Id == TEXT("pistol"); };
	TestTrue(TEXT("at least one known"), LevelLoadoutRules::FilterKnownWeapons({ TEXT("Laser"), TEXT("M16") }, IsKnown, Known, Unknown));
	TestTrue(TEXT("known / unknown split"), Known == TArray<FString>({ TEXT("m16") }) && Unknown == TArray<FString>({ TEXT("laser") }));
	TestFalse(TEXT("none known = global fallback"), LevelLoadoutRules::FilterKnownWeapons({ TEXT("laser") }, IsKnown, Known, Unknown));
	TestTrue(TEXT("none known: no known ids left"), Known.IsEmpty());

	FLevelCombatConfig Empty;
	TestTrue(TEXT("no key = empty maps"), ParseTgLevel(TEXT("{}"), Empty) && Empty.Loadouts.IsEmpty() && Empty.Kits.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonTgKitTest, "CodexTactics.Data.LevelJson.TgKit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonTgKitTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	if (!TestTrue(TEXT("level parses"), ParseTgLevel(
		TEXT("{\"kit\":{\"Commander\":{\"grenades\":3,\"medkits\":0,\"ammo\":{\"M16\":90,\"pistol\":-5,\"x\":\"a\"}},\"slot_2\":{\"heat_packs\":2},\"engineer\":5}}"),
		Config)))
	{
		return false;
	}
	const FLevelOperativeKit* Kit = Config.Kits.Find(TEXT("commander"));
	if (TestNotNull(TEXT("kit key normalized"), Kit))
	{
		TestTrue(TEXT("set fields"), Kit->Grenades == 3 && Kit->Medkits == 0);
		TestEqual(TEXT("an absent field stays -1 (keep)"), Kit->HeatPacks, -1);
		TestEqual(TEXT("ammo id lower-cased"), Kit->AmmoReserve.FindRef(TEXT("m16")), 90);
		TestEqual(TEXT("a negative / non-number ammo value is skipped"), Kit->AmmoReserve.Num(), 1);
	}
	const FLevelOperativeKit* Slot = Config.Kits.Find(TEXT("slot_2"));
	TestTrue(TEXT("slot kit: only heat packs set"), Slot && Slot->HeatPacks == 2 && Slot->Grenades == -1 && Slot->Medkits == -1 && Slot->AmmoReserve.IsEmpty());
	TestFalse(TEXT("a non-object entry is skipped"), Config.Kits.Contains(TEXT("engineer")));

	TestEqual(TEXT("-1 keeps the current value"), LevelLoadoutRules::ApplyKitCount(2, -1, 4), 2);
	TestEqual(TEXT("clamped to the max"), LevelLoadoutRules::ApplyKitCount(2, 9, 4), 4);
	TestEqual(TEXT("0 is a real value"), LevelLoadoutRules::ApplyKitCount(2, 0, 4), 0);
	// "Kit wins" over squad_loadout.
	TestEqual(TEXT("kit field set: the stage value is dropped"), LoadoutRules::StageOrKit(2, 5, true), 5);
	TestEqual(TEXT("no kit: the stage value applies"), LoadoutRules::StageOrKit(2, 5, false), 2);
	TestEqual(TEXT("no kit, stage -1: keep"), LoadoutRules::StageOrKit(-1, 5, false), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelJsonTgOverrideCarryTest, "CodexTactics.Data.LevelJson.TgOverrideCarry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelJsonTgOverrideCarryTest::RunTest(const FString&)
{
	FLevelCombatConfig Config;
	if (!TestTrue(TEXT("level parses"), ParseTgLevel(TEXT("{\"environment\":{\"override_carry\":true,\"start_hour\":6,\"weather\":\"Clear\"}}"), Config)))
	{
		return false;
	}
	TestTrue(TEXT("override_carry parsed"), Config.Environment.bOverrideCarry);
	FLevelCombatConfig Plain;
	ParseTgLevel(TEXT("{\"environment\":{\"start_hour\":6}}"), Plain);
	TestFalse(TEXT("default false"), Plain.Environment.bOverrideCarry);

	const FEnvironmentConfig EnvConfig;
	FEnvironmentState Carry = EnvironmentRules::MakeInitialState(EnvConfig, 20.f, EWeatherState::Snowfall, 5);
	Carry.SnowAmount = 0.4f;
	const TOptional<FEnvironmentState> Carried(Carry);

	const FEnvironmentState Won = UEnvironmentSubsystem::ResolveInitialState(EnvConfig, Config.Environment, Carried, 0);
	TestTrue(TEXT("the level's hour and weather win"), FMath::IsNearlyEqual(Won.Hour, 6.f) && Won.Weather == EWeatherState::Clear);
	TestTrue(TEXT("a fresh weather dwell"), FMath::IsNearlyZero(Won.WeatherElapsedSeconds));
	TestTrue(TEXT("unset snow continues from the carry"), FMath::IsNearlyEqual(Won.SnowAmount, 0.4f));
	TestFalse(TEXT("not locked"), Won.bLocked);

	// Only the hour set: the carried weather continues.
	FEnvironmentLevelSettings HourOnly;
	HourOnly.bOverrideCarry = true;
	HourOnly.bHasStartHour = true;
	HourOnly.StartHour = 9.f;
	const FEnvironmentState HourWon = UEnvironmentSubsystem::ResolveInitialState(EnvConfig, HourOnly, Carried, 0);
	TestTrue(TEXT("hour only: the hour wins, the weather continues"), FMath::IsNearlyEqual(HourWon.Hour, 9.f) && HourWon.Weather == EWeatherState::Snowfall);

	FEnvironmentLevelSettings NoOverride = Config.Environment;
	NoOverride.bOverrideCarry = false;
	const FEnvironmentState Kept = UEnvironmentSubsystem::ResolveInitialState(EnvConfig, NoOverride, Carried, 0);
	TestTrue(TEXT("without override_carry the carry continues"), FMath::IsNearlyEqual(Kept.Hour, 20.f) && Kept.Weather == EWeatherState::Snowfall);

	FEnvironmentLevelSettings Locked = Config.Environment;
	Locked.bLocked = true;
	const FEnvironmentState LockedState = UEnvironmentSubsystem::ResolveInitialState(EnvConfig, Locked, Carried, 0);
	TestTrue(TEXT("a locked level is unchanged"), LockedState.bLocked && FMath::IsNearlyEqual(LockedState.Hour, 6.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMissionStageLoadoutOnceTest, "CodexTactics.Mission.StageLoadoutOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMissionStageLoadoutOnceTest::RunTest(const FString&)
{
	using namespace LoadoutRules;
	TestTrue(TEXT("the cutscene end runs it"), ShouldRunStageLoadout(false, true, ECodexGamePhase::Preparation));
	TestTrue(TEXT("a sudden first wave runs it"), ShouldRunStageLoadout(false, true, ECodexGamePhase::WaveCombat));
	TestTrue(TEXT("first Preparation without a cutscene end"), ShouldRunStageLoadout(false, false, ECodexGamePhase::Preparation));
	TestTrue(TEXT("first WaveCombat of an ambush / firefight level"), ShouldRunStageLoadout(false, false, ECodexGamePhase::WaveCombat));
	TestFalse(TEXT("Exploration alone does not"), ShouldRunStageLoadout(false, false, ECodexGamePhase::Exploration));
	TestFalse(TEXT("the cutscene start does not"), ShouldRunStageLoadout(false, false, ECodexGamePhase::Cutscene));
	TestFalse(TEXT("PostCombat alone does not"), ShouldRunStageLoadout(false, false, ECodexGamePhase::PostCombat));
	TestFalse(TEXT("never twice (cutscene end)"), ShouldRunStageLoadout(true, true, ECodexGamePhase::Preparation));
	TestFalse(TEXT("never twice (next fight)"), ShouldRunStageLoadout(true, false, ECodexGamePhase::WaveCombat));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
