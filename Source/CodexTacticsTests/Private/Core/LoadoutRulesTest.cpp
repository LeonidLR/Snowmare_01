#include "Misc/AutomationTest.h"
#include "Core/LoadoutRules.h"
#include "Data/WaveConfigTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/main.gd _apply_stage_exploration_resources parity; the level_01_outpost squad_loadout import.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadoutRulesTest, "CodexTactics.Core.Loadout.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoadoutRulesTest::RunTest(const FString&)
{
	using namespace LoadoutRules;
	TestTrue(TEXT("'Start game' collects"), ResolveMode(TEXT("STARTING_UNIQUE"), EMissionStartMode::Game) == ELoadoutMode::ExploreAndCollect);
	TestTrue(TEXT("'Start combat': collecting becomes the starting set"), ResolveMode(TEXT("EXPLORE_AND_COLLECT"), EMissionStartMode::Combat) == ELoadoutMode::StartingUnique);
	TestTrue(TEXT("'Start combat' keeps a preset"), ResolveMode(TEXT("EDITOR_PRESET"), EMissionStartMode::Combat) == ELoadoutMode::EditorPreset);
	TestTrue(TEXT("No start mode: the level's"), ResolveMode(TEXT("STARTING_UNIQUE"), EMissionStartMode::None) == ELoadoutMode::StartingUnique);

	const FSquadLoadout Loadout;
	int32 Rifle = 0;
	int32 Pistol = 0;
	{
		FLoadoutSupply C, E, M;
		Apply(ELoadoutMode::StartingUnique, Loadout, C, E, M, Rifle, Pistol);
		TestTrue(TEXT("Starting set from nothing: 1 / 2 / 2"), C.Turrets == 1 && E.Barricades == 2 && M.Mines == 2 && Rifle == -1);
	}
	{
		FLoadoutSupply C{ 1, 1, 0 }, E{ 1, 2, 1 }, M{ 0, 0, 3 };
		Apply(ELoadoutMode::StartingUnique, Loadout, C, E, M, Rifle, Pistol);
		TestTrue(TEXT("Starting set keeps a bigger collection (totals 2 / 3 / 4)"), C.Turrets == 2 && E.Barricades == 3 && M.Mines == 4);
	}
	{
		FLoadoutSupply C{ 1, 1, 0 }, E{ 1, 2, 1 }, M{ 1, 0, 5 };
		Apply(ELoadoutMode::ExploreAndCollect, Loadout, C, E, M, Rifle, Pistol);
		TestTrue(TEXT("Collected: totals capped 2 / 3 / 5 (turrets 3 -> 2)"), C.Turrets == 2 && E.Barricades == 3 && M.Mines == 5);
		TestTrue(TEXT("Turrets over the cap stay with the others"), E.Turrets == 1 && M.Turrets == 1);
		TestTrue(TEXT("Godot quirk: the commander keeps his barricade"), C.Barricades == 1);
	}
	{
		FLoadoutSupply C{ 1, 0, 0 }, E{ 1, 0, 0 }, M;
		Apply(ELoadoutMode::ExploreAndCollect, Loadout, C, E, M, Rifle, Pistol);
		TestTrue(TEXT("All turrets to the commander"), C.Turrets == 2 && E.Turrets == 0);
	}
	{
		FSquadLoadout Maximal;
		Maximal.PresetTier = TEXT("MAXIMAL");
		FLoadoutSupply C, E, M;
		Apply(ELoadoutMode::EditorPreset, Maximal, C, E, M, Rifle, Pistol);
		TestTrue(TEXT("MAXIMAL preset 2 / 4 / 5, medkits 4, reserves 240 / 96"), C.Turrets == 2 && E.Barricades == 4 && M.Mines == 5
			&& C.Medkits == 4 && Rifle == 240 && Pistol == 96);
	}

	{
		// Grenades per operative (Wave Editor): preset tiers, CUSTOM / other modes take grenades_count, -1 keeps, clamp.
		FSquadLoadout Custom;
		Custom.PresetTier = TEXT("CUSTOM");
		Custom.GrenadesCount = 3;
		TestEqual(TEXT("CUSTOM preset: grenades_count"), GrenadesFor(ELoadoutMode::EditorPreset, Custom, 4), 3);
		Custom.PresetTier = TEXT("MAXIMAL");
		TestEqual(TEXT("MAXIMAL preset: 4"), GrenadesFor(ELoadoutMode::EditorPreset, Custom, 4), 4);
		Custom.PresetTier = TEXT("MINIMAL");
		TestEqual(TEXT("MINIMAL preset: 1"), GrenadesFor(ELoadoutMode::EditorPreset, Custom, 4), 1);
		Custom.GrenadesCount = 9;
		TestEqual(TEXT("explore mode: grenades_count clamped to the carry limit"), GrenadesFor(ELoadoutMode::ExploreAndCollect, Custom, 4), 4);
		Custom.GrenadesCount = -1;
		TestEqual(TEXT("not set: keep"), GrenadesFor(ELoadoutMode::StartingUnique, Custom, 4), -1);
	}

	const ULevelConfigAsset* Level = LoadObject<ULevelConfigAsset>(nullptr, TEXT("/Game/Data/Levels/DA_Level_level_01_outpost.DA_Level_level_01_outpost"));
	if (!TestNotNull(TEXT("DA_Level_level_01_outpost"), Level))
	{
		return false;
	}
	const FSquadLoadout& Imported = Level->Config.SquadLoadout;
	TestTrue(TEXT("Imported squad_loadout: STARTING_UNIQUE / CUSTOM, 1 / 2 / 2, medkits 2, 30 / 24"), Imported.SimulationMode == TEXT("STARTING_UNIQUE")
		&& Imported.PresetTier == TEXT("CUSTOM") && Imported.TurretsCount == 1 && Imported.BarricadesCount == 2 && Imported.MinesCount == 2
		&& Imported.MedkitsCount == 2 && Imported.M16Ammo == 30 && Imported.PistolAmmo == 24);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
