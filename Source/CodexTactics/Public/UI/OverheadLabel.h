#pragma once

#include "CoreMinimal.h"

/**
 * A world label drawn by the HUD above an actor (Godot Label3D overhead_label of enemies, barricades, turrets and the
 * generator: billboard, no depth test, outlined).
 */
struct CODEXTACTICS_API FOverheadLabel
{
	/** One or two lines; emoji the HUD font lacks are stripped. */
	FString Text;
	FLinearColor Color = FLinearColor::White;
	/** Height above the actor's base (feet / ground), cm. */
	float HeightCm = 180.f;
	/** Small coloured square before the text (enemy armor tier: Godot 🟢 / 🟡 / 🔴). */
	bool bHasMarker = false;
	FLinearColor MarkerColor = FLinearColor::White;
};
