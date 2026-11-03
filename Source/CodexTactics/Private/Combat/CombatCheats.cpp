#include "Combat/CombatCheats.h"

#include "Characters/OperativeCharacter.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace CombatCheatsLocal
{
#if !UE_BUILD_SHIPPING
	static TAutoConsoleVariable<int32> CVarCheatImmortal(TEXT("Codex.Cheat.Immortal"), -1,
		TEXT("Squad immortality (combat lab, docs/design/COMBAT_LAB.md): -1 = the level JSON \"cheats\".\"immortal\" decides, 0 = off, 1 = on. Never in Shipping."));
	static TAutoConsoleVariable<int32> CVarCheatInfiniteAmmo(TEXT("Codex.Cheat.InfiniteAmmo"), -1,
		TEXT("Squad infinite ammo (combat lab): -1 = the level JSON \"cheats\".\"infinite_ammo\" decides, 0 = off, 1 = on. Never in Shipping."));
#endif

	const FLevelCombatConfig* LevelOf(const UWorld* World)
	{
		const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
		const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
		return Level ? &Level->Config : nullptr;
	}
}

bool CombatCheats::Resolve(int32 ConsoleValue, bool bLevelFlag, bool bShippingBuild)
{
	if (bShippingBuild)
	{
		return false;
	}
	return ConsoleValue >= 1 || (ConsoleValue < 0 && bLevelFlag);
}

bool CombatCheats::IsImmortal(const UWorld* World)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const FLevelCombatConfig* Level = CombatCheatsLocal::LevelOf(World);
	return Resolve(CombatCheatsLocal::CVarCheatImmortal.GetValueOnGameThread(), Level && Level->bCheatImmortal, false);
#endif
}

bool CombatCheats::IsInfiniteAmmo(const UWorld* World)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const FLevelCombatConfig* Level = CombatCheatsLocal::LevelOf(World);
	return Resolve(CombatCheatsLocal::CVarCheatInfiniteAmmo.GetValueOnGameThread(), Level && Level->bCheatInfiniteAmmo, false);
#endif
}

bool CombatCheats::IsImmortalActor(const AActor* Actor)
{
	// Teams (2026-11-03): the cheat is the player squad's; a hostile PMC fighter dies like any enemy.
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
	return Operative && Operative->IsPlayerTeam() && IsImmortal(Actor->GetWorld());
}
