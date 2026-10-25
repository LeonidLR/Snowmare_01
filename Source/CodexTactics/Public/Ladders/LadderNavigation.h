#pragma once

#include "CoreMinimal.h"
#include "Interactables/VaultNavigation.h"
#include "NavAreas/NavArea.h"
#include "LadderNavigation.generated.h"

/**
 * Nav area of a ladder's smart link (docs/design/LADDERS.md 3.3 / 7.4). The operatives' filters allow it; the enemies' default
 * filter UNavFilter_NoVault excludes it; a climbing enemy (AEnemyCharacter::bCanClimbLadders, 7.5) paths with
 * UNavFilter_NoVaultLadder, which allows it. The cost makes a path take the ladder only when it saves a long way round (the climb
 * is slow: ~41 cm/s up the loop).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Ladder : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Ladder();
};

/**
 * The link area of a ladder with ALadderActor::bAllowEnemies off: the squad only. Every enemy filter excludes it (the climbing
 * enemies' one too).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_LadderSquadOnly : public UNavArea_Ladder
{
	GENERATED_BODY()
};

/**
 * Query filter of a climbing enemy (LADDERS.md 7.4 / 7.5): what UNavFilter_NoVault closes (vault areas, crawl strips, squad-only
 * ladders) stays closed, the ladder links (UNavArea_Ladder) are open.
 */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoVaultLadder : public UNavFilter_NoVault
{
	GENERATED_BODY()

public:
	UNavFilter_NoVaultLadder();
};
