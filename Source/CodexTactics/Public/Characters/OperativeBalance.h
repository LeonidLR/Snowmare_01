#pragma once

#include "CoreMinimal.h"

class AOperativeCharacter;
class UGodotBalanceAsset;

/**
 * Applies the imported Godot GameBalanceConfig (DA_GameBalanceConfig) to an operative: per-role max health and
 * matches, the engineer's barricades / the medic's mines, walk / run / crouch speeds, acceleration, sprint / lift
 * limits, carry and wound speed multipliers, the commander's preparation radius, the cold rules (rates, stance
 * multipliers, misfire / freeze / aim). Metres are converted to cm.
 * Call it on a deferred spawn before BeginPlay. Fortitude, luck and accuracy stay per-role code values, as in Godot.
 * Godot reference: Scenes/movements/player.gd apply_balance_config.
 */
namespace OperativeBalance
{
	CODEXTACTICS_API void Apply(const UGodotBalanceAsset& Config, AOperativeCharacter& Operative);
}
