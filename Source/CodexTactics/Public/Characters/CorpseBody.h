#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CorpseBody.generated.h"

class UPrimitiveComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCorpseBody : public UInterface
{
	GENERATED_BODY()
};

/**
 * A dead body UCorpseSubsystem keeps on the level (game designer 2026-11-08 / 11-09: "corpses of ALL enemies stay"; UE-only, no Godot
 * reference - Godot freed the body after death_decay_delay). Implemented by AEnemyCharacter (every monster, the C++ placeholder body
 * included) and AOperativeCharacter (a hostile PMC fighter only - the player's own squad's KIA are never registered: they stay for good,
 * they hold the remains). The subsystem freezes the body once it lies settled (CorpseRules::ShouldFreeze) and fades the oldest far body
 * above the level's shared cap (CorpseRules::PickCorpseToFade). Native-only interface (no Blueprint implementation).
 */
class CODEXTACTICS_API ICorpseBody
{
	GENERATED_BODY()

public:
	/** The death pose holds: no clip advancing or blending any more (a body without any death animation: true at once). */
	virtual bool IsDeathPoseSettled() const = 0;
	/** The body is still on its way down (killed in the air / on a ledge / dropped by a ladder): it is not frozen in the air. */
	virtual bool IsCorpseFalling() const = 0;
	/** The body is still on a ladder (its drop not finished). */
	virtual bool IsCorpseOnLadder() const = 0;
	/** Stops everything that costs game-thread time (AI brain, ticks, anims, collision); the body stays rendered where it lies. */
	virtual void FreezeCorpse() = 0;
	/** FreezeCorpse ran. */
	virtual bool IsCorpseFrozen() const = 0;
	/** The visible body: the fade scales it down, the in-view test reads its render time (null: none). */
	virtual UPrimitiveComponent* GetCorpseVisual() const = 0;
	/** The body still holds loot the player has not searched: never faded while it is in the camera view (CorpseRules). */
	virtual bool HoldsUnsearchedLoot() const { return false; }
};
