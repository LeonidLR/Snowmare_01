#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "DropLinkNavigation.generated.h"

/**
 * Nav area of a generated DROP link (UDropLinkSubsystem; game designer items 6 / 10 / 11a, 2026-11-03): a one-way smart link from a navmesh
 * edge down onto a lower walkable surface (a roof to a lower roof, a bridge into the cut, a roof to the ground). Taken only downwards (the
 * link is one-way). PENDING the game designer's confirmation: DefaultCost 3 and an entering cost of 10 m, so a stair / ramp / ladder of a
 * similar length is preferred (a ladder link costs 3 x its length, no entering cost). The operatives' filters allow it; the enemies'
 * (UNavFilter_NoVault and its subclasses) exclude it. Overridable per project: DefaultEngine.ini [/Script/CodexTactics.NavArea_Drop].
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
