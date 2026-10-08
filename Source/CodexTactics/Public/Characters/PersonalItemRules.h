#pragma once

#include "CoreMinimal.h"
#include "PersonalItemRules.generated.h"

/** Provisions an operative can use (Godot use_squad_item "MEDKIT" / "CANNED_FOOD" / "BREAD" / "CHOCOLATE"). */
UENUM(BlueprintType)
enum class EPersonalItem : uint8
{
	Medkit,
	CannedFood,
	Bread,
	Chocolate
};

/** Health / warmth one item gives. */
struct FPersonalItemEffect
{
	float Heal = 0.f;
	float Warmth = 0.f;
};

/**
 * Pure provision rules. Godot reference: Scenes/movements/player.gd heal_with_item (medkit 80 HP; canned food 45 HP and
 * 25 % cold off; bread 30 / 15; chocolate 20 / 10; refused at full health without cold) and main.gd use_squad_item names.
 */
namespace PersonalItemRules
{
	CODEXTACTICS_API FPersonalItemEffect GetEffect(EPersonalItem Item);

	/** Godot heal_with_item: nothing to do at full health and zero cold. */
	CODEXTACTICS_API bool CanUse(float Health, float MaxHealth, float Cold);

	/** "Medkit", "Canned food", "Bread", "Chocolate" (Godot item_names). */
	CODEXTACTICS_API FText GetName(EPersonalItem Item);

	/** "medkits", "canned food", "bread", "chocolate" (Godot "%s has no ... in personal inventory!"). */
	CODEXTACTICS_API FText GetMissingName(EPersonalItem Item);
}
