#include "Core/LoadoutRules.h"
#include "Interactables/DeployableRules.h"

ELoadoutMode LoadoutRules::ResolveMode(const FString& LevelMode, EMissionStartMode StartMode)
{
	ELoadoutMode Mode = LevelMode == TEXT("STARTING_UNIQUE") ? ELoadoutMode::StartingUnique
		: (LevelMode == TEXT("EDITOR_PRESET") ? ELoadoutMode::EditorPreset : ELoadoutMode::ExploreAndCollect);
	if (StartMode == EMissionStartMode::Game)
	{
		Mode = ELoadoutMode::ExploreAndCollect;
	}
	else if (StartMode == EMissionStartMode::Combat && Mode == ELoadoutMode::ExploreAndCollect)
	{
		Mode = ELoadoutMode::StartingUnique;
	}
	return Mode;
}

void LoadoutRules::Apply(ELoadoutMode Mode, const FSquadLoadout& Loadout, FLoadoutSupply& Commander, FLoadoutSupply& Engineer,
	FLoadoutSupply& Medic, int32& OutM16Reserve, int32& OutPistolReserve)
{
	OutM16Reserve = -1;
	OutPistolReserve = -1;
	const int32 Turrets = Commander.Turrets + Engineer.Turrets + Medic.Turrets;
	const int32 Barricades = Commander.Barricades + Engineer.Barricades + Medic.Barricades;
	const int32 Mines = Commander.Mines + Engineer.Mines + Medic.Mines;
	switch (Mode)
	{
	case ELoadoutMode::StartingUnique:
		// Godot starting_* exports are 0 on every operative, so the fallbacks 1 / 2 / 2 apply.
		Commander.Turrets = FMath::Max(1, Turrets);
		Engineer.Barricades = FMath::Max(2, Barricades);
		Medic.Mines = FMath::Max(2, Mines);
		break;
	case ELoadoutMode::EditorPreset:
	{
		int32 T = Loadout.TurretsCount;
		int32 B = Loadout.BarricadesCount;
		int32 M = Loadout.MinesCount;
		int32 Med = Loadout.MedkitsCount;
		int32 Rifle = Loadout.M16Ammo;
		int32 Pistol = Loadout.PistolAmmo;
		if (Loadout.PresetTier == TEXT("MINIMAL"))
		{
			T = 0; B = 0; M = 0; Med = 0; Rifle = 60; Pistol = 24;
		}
		else if (Loadout.PresetTier == TEXT("STANDARD"))
		{
			T = 1; B = 2; M = 2; Med = 2; Rifle = 120; Pistol = 48;
		}
		else if (Loadout.PresetTier == TEXT("MAXIMAL"))
		{
			T = 2; B = 4; M = 5; Med = 4; Rifle = 240; Pistol = 96;
		}
		Commander.Turrets = FMath::Max(T, Turrets);
		Commander.Medkits = Med;
		OutM16Reserve = Rifle;
		OutPistolReserve = Pistol;
		Engineer.Barricades = FMath::Max(B, Barricades);
		Medic.Mines = FMath::Max(M, Mines);
		break;
	}
	default:
		Commander.Turrets = FMath::Clamp(Turrets, 0, DeployableRules::GetMaxCarried(EDeployableType::Turret));
		Engineer.Barricades = FMath::Clamp(Barricades, 0, DeployableRules::GetMaxCarried(EDeployableType::Barricade));
		if (Commander.Turrets >= Turrets)
		{
			Engineer.Turrets = 0;
		}
		Medic.Mines = FMath::Clamp(Mines, 0, DeployableRules::GetMaxCarried(EDeployableType::Mine));
		if (Commander.Turrets >= Turrets)
		{
			Medic.Turrets = 0;
		}
		break;
	}
}
