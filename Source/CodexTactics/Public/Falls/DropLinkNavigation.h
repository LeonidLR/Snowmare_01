#pragma once

#include "CoreMinimal.h"
#include "Falls/FallRules.h"
#include "Interactables/VaultNavigation.h"
#include "Ladders/LadderNavigation.h"
#include "NavAreas/NavArea.h"
#include "DropLinkNavigation.generated.h"

/**
 * Nav area of a generated DROP link (UDropLinkSubsystem; game designer items 6 / 10 / 11a, 2026-11-03): a one-way smart link from a navmesh
 * edge down onto a lower walkable surface (a roof to a lower roof, a bridge into the cut, a roof to the ground). Taken only downwards (the
 * link is one-way). PENDING the game designer's confirmation: DefaultCost 3 and an entering cost of 10 m, so a stair / ramp / ladder of a
 * similar length is preferred (a ladder link costs 3 x its length, no entering cost). The operatives' filters allow it; the enemies' base
 * filters (UNavFilter_NoVault / _NoVaultLadder) exclude it, the drop filters below open it per enemy type (FallRules::EnemyDropUse, user
 * decision 2026-11-03). Overridable per project: DefaultEngine.ini [/Script/CodexTactics.NavArea_Drop].
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Drop : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Drop();
};

/**
 * A drop that hurts (higher than one level, FallRules::IsHurtingDrop): the same link with an entering cost of 25 m - a damaging drop is
 * taken only when the way round is much longer (the order's drop marker shows it in red with the expected damage).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_DropHurt : public UNavArea_Drop
{
	GENERATED_BODY()

public:
	UNavArea_DropHurt();
};

namespace DropLinkNavigation
{
	/** Opens the drop areas the base enemy filter closed: both drops for Free and Jumper (a Free body's hurting drop gets its cost in InitializeFilter). */
	CODEXTACTICS_API void OpenDropAreas(TArray<struct FNavigationFilterArea>& Areas);

	/** A free-drop enemy's filter: a hurting drop costs enemy_drops.hurt_entering_cm to enter (taken only when there is no other way). */
	CODEXTACTICS_API void ApplyFreeDropCosts(const class ANavigationData& NavData, struct FNavigationQueryFilter& Filter);

	/** The enemy's nav filter class for its drop use and ladder climbing (AEnemyAIController::RefreshNavigationFilter). */
	CODEXTACTICS_API TSubclassOf<class UNavigationQueryFilter> EnemyFilterClass(FallRules::EEnemyDropUse Use, bool bClimbsLadders);
}

/**
 * An enemy that takes the free drops (FallRules::EEnemyDropUse::Free: the zombies, the PMC fighters, the Marksman - user decision 2026-11-03):
 * the drop links open, a hurting one costs enemy_drops.hurt_entering_cm extra. Vault / crawl / ladder areas as UNavFilter_NoVault.
 */
UCLASS()
class CODEXTACTICS_API UNavFilter_EnemyDropFree : public UNavFilter_NoVault
{
	GENERATED_BODY()

public:
	UNavFilter_EnemyDropFree();
	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;
};

/** UNavFilter_EnemyDropFree for a ladder climber (the ladder links open too). */
UCLASS()
class CODEXTACTICS_API UNavFilter_EnemyDropFreeLadder : public UNavFilter_NoVaultLadder
{
	GENERATED_BODY()

public:
	UNavFilter_EnemyDropFreeLadder();
	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;
};

/** A jumping enemy (FallRules::EEnemyDropUse::Jumper: the hound, the cutter): every drop link at the areas' own costs. */
UCLASS()
class CODEXTACTICS_API UNavFilter_EnemyDropJumper : public UNavFilter_NoVault
{
	GENERATED_BODY()

public:
	UNavFilter_EnemyDropJumper();
};

/** UNavFilter_EnemyDropJumper for a ladder climber. */
UCLASS()
class CODEXTACTICS_API UNavFilter_EnemyDropJumperLadder : public UNavFilter_NoVaultLadder
{
	GENERATED_BODY()

public:
	UNavFilter_EnemyDropJumperLadder();
};
