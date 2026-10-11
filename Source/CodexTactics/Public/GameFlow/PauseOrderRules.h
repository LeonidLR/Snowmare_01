#pragma once

#include "CoreMinimal.h"

/**
 * Order radius of the tactical pause. Godot reference: main.gd `radius_ring` / `_get_relocate_radius_for_worker` (12 m in
 * the pause). User decision 2026-10-11: the FREE pause that opens a sudden horde wave is the unrestricted preparation - no
 * radius ring and no distance limit on move orders / barricade / deployable placement; the paid pause (Space during a
 * fight) keeps the 12 m radius and its ring.
 */
namespace PauseOrderRules
{
	/** Radius used where the pause has no limit, cm (same as RelocationRules::UnlimitedRadius: Godot 9999 m). */
	constexpr float UnlimitedRadius = 999900.f;

	/** The distance limit and the radius ring apply only in a paid tactical pause. */
	CODEXTACTICS_API bool IsRadiusLimited(bool bFreePause);

	/** Radius of the pause's move / placement orders, cm: PauseRadius in a paid pause, UnlimitedRadius in the free one. */
	CODEXTACTICS_API float GetOrderRadius(bool bFreePause, float PauseRadius);
}
