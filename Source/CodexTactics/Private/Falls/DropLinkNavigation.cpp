// Nav areas of the generated drop links (game designer items 6 / 10 / 11a, 2026-11-03). UE-only, no Godot reference.

#include "Falls/DropLinkNavigation.h"

UNavArea_Drop::UNavArea_Drop()
{
	// PENDING confirmation: like a ladder per cm (3) plus 10 m on entering - a stair / ramp / ladder of a similar length wins.
	DefaultCost = 3.f;
	FixedAreaEnteringCost = 1000.f;
	DrawColor = FColor(255, 170, 40);
}

UNavArea_DropHurt::UNavArea_DropHurt()
{
	// A drop that hurts: taken only when the way round is some 25 m longer.
	DefaultCost = 3.f;
	FixedAreaEnteringCost = 2500.f;
	DrawColor = FColor(255, 60, 40);
}
