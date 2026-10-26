#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationMenuRules.h"

/**
 * The B key of the formation radial menu (stage 7, user decision 2026-10-26: G is the grenade, so the menu moved to B) - pure
 * tap / hold timing and the menu's layout / display helpers. Godot reference: none (UE-only; design docs/design/FORMATION_SECTORS.md
 * "Selection", contract docs/design/FORMATION_CONTRACT.md 9). Tests: CodexTactics.UI.FormationMenu.*.
 *
 * TAP B (released before the hold time) = the solo / squad toggle as before; HOLD B = the radial menu opens (only while it is
 * available: squad mode, >= 2 members, not turn-based, not tutorial-locked), the release picks (FormationMenuRules::Release). A hold
 * while the menu is NOT available behaves as a tap (the toggle on the release) - B never does nothing. Esc / RMB close the open menu
 * (FormationMenuRules::Cancel); the key's later release then does nothing.
 */

/** What the B key asks for. */
enum class EFormationMenuKeyAction : uint8
{
	None,
	/** Released before the menu opened: the solo / squad toggle (the old B). */
	Toggle,
	/** Held for the hold time while the menu is available: open it. Once per press. */
	Open,
	/** Released while the menu is open: pick the highlighted item (FormationMenuRules::Release). */
	Pick
};

/** Tap / hold tracker of the formation menu key on real time (the tactical pause slows the world, not the key). */
class CODEXTACTICS_API FFormationMenuKeyTracker
{
public:
	/** Key down: a new press (any previous one is forgotten). */
	void Press();

	/**
	 * Advances real time while held; returns Open once when the held time reaches HoldSeconds and bMenuAvailable (a hold that
	 * reaches the time while the menu is unavailable keeps waiting: the menu opens as soon as it becomes available).
	 */
	EFormationMenuKeyAction Tick(float RealDeltaSeconds, float HoldSeconds, bool bMenuAvailable);

	/** Key up: Pick when the menu is open, Toggle when it never opened (however long the hold), None when closed by Close / not held. */
	EFormationMenuKeyAction Release();

	/** The open menu was closed without the key (Esc / RMB / it became unavailable): the key's release then does nothing. */
	void Close();

	bool IsPressed() const { return bPressed; }
	bool IsMenuOpen() const { return bMenuOpen; }
	/** Real seconds the key has been held in the current press. */
	float GetHeldTime() const { return HeldTime; }

private:
	bool bPressed = false;
	bool bMenuOpen = false;
	/** The menu of this press was closed by Close: the release is spent. */
	bool bSpent = false;
	float HeldTime = 0.f;
};

namespace FormationMenuInput
{
	/** Default tap / hold threshold, real seconds (a release before it is a tap). */
	inline constexpr float DefaultHoldSeconds = 0.25f;

	/**
	 * The menu's centre: the cursor, pulled in so the box Centre +- HalfExtent stays inside View (all widget units, y down) with
	 * ScreenMargin to spare; a view smaller than the box -> the view's centre on that axis.
	 */
	CODEXTACTICS_API FVector2D ClampCentre(const FVector2D& Cursor, const FVector2D& View, const FVector2D& HalfExtent, float ScreenMargin);

	/** Unit direction from the menu centre to the item's sector centre (x right, y DOWN; 12 o'clock = (0, -1), clockwise). */
	CODEXTACTICS_API FVector2D ItemDirection(EFormationMenuItem Item);

	/**
	 * In-game label (English): Auto (unset), Line, Diamond / Wedge / Pair (4+ / 3 / 2 members: what the diamond item gives),
	 * Echelon right, Blizzard diamond, Column, Hedgehog, Echelon left, All forward.
	 */
	CODEXTACTICS_API FText Label(const TOptional<EFormationMenuItem>& Item, int32 MemberCount);

	/** Icon asset name of an item / Auto (unset): T_FormationMenu_<contract id> ("auto" for Auto), Content/UI/FormationMenu. */
	CODEXTACTICS_API FString IconAssetName(const TOptional<EFormationMenuItem>& Item);
}
