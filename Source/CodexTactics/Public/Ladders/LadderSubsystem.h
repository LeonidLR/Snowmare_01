#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LadderSubsystem.generated.h"

class ALadderActor;

/**
 * The world's ladders (docs/design/LADDERS.md): ALadderActor registers itself on BeginPlay, so the climbers' link polling and the
 * player's ladder click never iterate actors. UE-only system (no Godot reference).
 */
UCLASS()
class CODEXTACTICS_API ULadderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(ALadderActor* Ladder);
	void Unregister(ALadderActor* Ladder);

	/** Every live ladder of the world. */
	TArray<ALadderActor*> GetLadders() const;

	/**
	 * The climbable ladder whose bottom / top link point lies within RadiusCm (2D, and 80 cm in height) of Location; bOutTop tells
	 * which end. Null when none.
	 */
	ALadderActor* FindLinkEndNear(const FVector& Location, float RadiusCm, bool& bOutTop) const;

private:
	TArray<TWeakObjectPtr<ALadderActor>> Ladders;
};
