#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Hold-B radial formation menu - pure rules (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md "Selection",
 * contract docs/design/FORMATION_CONTRACT.md 9; reference implementation Tools/TrainingGround/src/movement/menu.ts)). Tests:
 * CodexTactics.Formation.Menu.*, parity CodexTactics.Formation.Parity.Menu.
 *
 * 8 items on the clock, 45 deg sectors, clockwise from 12: 12 line, 1:30 diamond (the wedge with 3, the pair with 2), 3 echelon right,
 * 4:30 blizzard, 6 column, 7:30 hedgehog, 9 echelon left, 10:30 all forward; the centre (the dead zone) = Auto. Hold B (G until 2026-10-26; G is the grenade): the menu opens,
 * the cursor highlights an item, releasing B picks it; a manual choice holds until Auto is picked; Esc / right mouse button closes it
 * and keeps the previous choice; a disabled item is refused (the previous choice stays).
 *
 * No display text here (in-game text is English and lives in the widget as FText); ToString / Parse give the contract ids.
 */

/** Menu items, clockwise from 12 o'clock; the value is the sector index (sector i centred on i * 45 deg). */
enum class EFormationMenuItem : uint8
{
	/** 12 [line]. */
	Line,
	/** 1:30 [diamond]: the diamond with 4, the wedge with 3, the pair with 2. */
	Diamond,
	/** 3 [echelon_right]. */
	EchelonRight,
	/** 4:30 [blizzard]: the blizzard tight diamond. */
	Blizzard,
	/** 6 [column]. */
	Column,
	/** 7:30 [hedgehog]. */
	Hedgehog,
	/** 9 [echelon_left]. */
	EchelonLeft,
	/** 10:30 [all_forward]: the terrain rules' formation, everybody looks forward (LookYaw mode Forward). */
	AllForward
};

/** What an input did (the UI reacts: e.g. Disabled = flash the item / play the refusal sound). */
enum class EFormationMenuEffect : uint8
{
	None,
	Opened,
	Picked,
	Auto,
	Cancelled,
	Disabled
};

/** The menu's state (contract { open, choice, highlighted }; start: closed, Auto, nothing highlighted). */
struct FFormationMenuState
{
	/** The menu is shown (B held). */
	bool bOpen = false;
	/** The squad's formation choice; unset = Auto (the terrain rules). Holds while the menu is closed. */
	TOptional<EFormationMenuItem> Choice;
	/** The item under the cursor while open; unset = Auto (the centre). */
	TOptional<EFormationMenuItem> Highlighted;
};

/** What a choice means for the formation logic. */
struct FFormationMenuResolved
{
	/** The fixed formation of a manual choice; unset = the terrain rules decide (FormationTerrainRules::FormationFor). */
	TOptional<EFormationKind> ManualFormation;
	/** "All forward": the terrain rules' formation with LookYaw mode Forward. */
	bool bAllForward = false;
};

namespace FormationMenuRules
{
	/** Number of items (sectors). */
	inline constexpr int32 ItemCount = 8;
	/** Width of one sector, deg. */
	inline constexpr float SectorDeg = 45.f;

	/** Contract id of an item ("line", "diamond", ..., "all_forward"); not display text. */
	CODEXTACTICS_API const TCHAR* ToString(EFormationMenuItem Item);
	/** Parses a contract id (case-insensitive); false when unknown ("auto" is not an item: see ParseChoice). */
	CODEXTACTICS_API bool Parse(const FString& Id, EFormationMenuItem& Out);
	/** Parses a choice: "auto" -> unset, an item id -> the item; false when unknown. */
	CODEXTACTICS_API bool ParseChoice(const FString& Id, TOptional<EFormationMenuItem>& Out);

	/** Clock position of the item's sector centre (12, 1.5, 3, ..., 10.5). */
	CODEXTACTICS_API float ItemClock(EFormationMenuItem Item);
	/** The fewest members the item is enabled for (every item: 2 - one operative has no formation). */
	CODEXTACTICS_API int32 MinMembers(EFormationMenuItem Item);

	/**
	 * The item under the cursor: (Dx, Dy) = its offset from the menu centre in screen px, x right, y DOWN (UMG / Slate), 12 = up. Within
	 * DeadZonePx (inclusive) -> unset (Auto); else angle a = atan2(Dx, -Dy) in deg 0..360 clockwise from up, item = floor(a / 45 + 0.5)
	 * mod 8 (each item owns +-22.5 deg; a border goes to the next item clockwise).
	 */
	CODEXTACTICS_API TOptional<EFormationMenuItem> ItemAt(float Dx, float Dy, float DeadZonePx);

	/** Enabled for MemberCount members (>= MinMembers). Auto is always available. */
	CODEXTACTICS_API bool IsEnabled(EFormationMenuItem Item, int32 MemberCount);

	/** B held: opens with Auto highlighted (the cursor at the centre); the choice is untouched. Returns Opened. */
	CODEXTACTICS_API EFormationMenuEffect Open(FFormationMenuState& State);

	/** Cursor moved while open: the highlight follows ItemAt; nothing is chosen yet (closed: no change). */
	CODEXTACTICS_API void Move(FFormationMenuState& State, float Dx, float Dy, float DeadZonePx);

	/**
	 * B released: on an enabled item -> it becomes the choice (Picked, holds until Auto); in the centre -> Auto (Auto); on a disabled
	 * item -> the previous choice stays (Disabled). The menu closes in every case. Closed: None, no change.
	 */
	CODEXTACTICS_API EFormationMenuEffect Release(FFormationMenuState& State, int32 MemberCount);

	/** Esc / right mouse button while open: closes, the previous choice stays (Cancelled). Closed: None. */
	CODEXTACTICS_API EFormationMenuEffect Cancel(FFormationMenuState& State);

	/**
	 * What a choice means: Auto (unset) -> the terrain rules; AllForward -> the terrain rules + LookYaw mode Forward; Diamond -> the
	 * diamond with 4 members, else the wedge (the pair with 2); any other item -> its formation. A manual choice the member count no
	 * longer allows (members died) -> Auto.
	 */
	CODEXTACTICS_API FFormationMenuResolved Resolve(const TOptional<EFormationMenuItem>& Choice, int32 MemberCount);
}
