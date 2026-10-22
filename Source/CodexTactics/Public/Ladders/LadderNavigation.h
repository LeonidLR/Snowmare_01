#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "LadderNavigation.generated.h"

/**
 * Nav area of a ladder's smart link (docs/design/LADDERS.md 3.3 / 7.4). The operatives' filters allow it; the enemies' default
 * filter UNavFilter_NoVault excludes it (no enemy climbs yet: the climbing enemies of 7.5 are a later phase). The cost makes a
 * path take the ladder only when it saves a long way round (the climb is slow: ~41 cm/s up the loop).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Ladder : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Ladder();
};
