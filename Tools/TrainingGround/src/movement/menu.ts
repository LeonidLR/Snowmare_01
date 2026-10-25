// Training Ground / movement: the hold-G radial formation menu - pure rules (FORMATION_CONTRACT.md 9, C++ FormationMenuRules).
// Layout (docs/design/FORMATION_SECTORS.md "Selection"): 8 items on the clock, 45 deg sectors, the centre = Auto.
// Screen space: the cursor offset from the menu centre in px, x right, y DOWN (as UMG / Slate); 12 o'clock = up.

import type { Formation } from './layout.ts';

export type MenuItemId = 'line' | 'diamond' | 'echelon_right' | 'blizzard' | 'column' | 'hedgehog' | 'echelon_left' | 'all_forward';

export interface MenuItem {
  id: MenuItemId;
  /** Clock position of the sector centre (12 = up, 1.5 = 45 deg right of up, ...). */
  clock: number;
  /** Fewest members the item is enabled for. */
  minMembers: number;
}

/** Clockwise from 12, sector i centred on i * 45 deg. */
export const MENU_ITEMS: readonly MenuItem[] = [
  { id: 'line', clock: 12, minMembers: 2 },
  { id: 'diamond', clock: 1.5, minMembers: 2 },
  { id: 'echelon_right', clock: 3, minMembers: 2 },
  { id: 'blizzard', clock: 4.5, minMembers: 2 },
  { id: 'column', clock: 6, minMembers: 2 },
  { id: 'hedgehog', clock: 7.5, minMembers: 2 },
  { id: 'echelon_left', clock: 9, minMembers: 2 },
  { id: 'all_forward', clock: 10.5, minMembers: 2 },
];

/** The squad's formation choice: Auto (terrain rules) or a manual item that holds until Auto is picked. */
export type MenuChoice = 'auto' | MenuItemId;

/**
 * The item under the cursor: null = Auto (inside the dead zone round the centre), else the sector item. The sector of an angle
 * a (deg clockwise from up, 0..360) is round(a / 45) mod 8, so each item owns +-22.5 deg round its clock position; an angle
 * exactly on a border goes to the next item clockwise (round half up).
 */
function ItemAt(dx: number, dy: number, deadZonePx: number): MenuItemId | null {
  if (Math.hypot(dx, dy) <= deadZonePx) return null;
  let a = (Math.atan2(dx, -dy) * 180) / Math.PI;
  if (a < 0) a += 360;
  return MENU_ITEMS[Math.floor(a / 45 + 0.5) % 8].id;
}

/** Enabled for this many members: every item needs at least 2 (one operative has no formation; Auto stays). */
function IsEnabled(id: MenuItemId, memberCount: number): boolean {
  return memberCount >= MENU_ITEMS.find((i) => i.id === id)!.minMembers;
}

export interface MenuState {
  open: boolean;
  /** The current choice (holds while the menu is closed; a manual item holds until Auto is picked). */
  choice: MenuChoice;
  /** What the cursor points at while the menu is open (null = Auto in the centre). */
  highlighted: MenuItemId | null;
}

export type MenuEffect = 'none' | 'opened' | 'picked' | 'auto' | 'cancelled' | 'disabled';

export const MENU_START: MenuState = { open: false, choice: 'auto', highlighted: null };

/** G pressed: the menu opens with the cursor at the centre (Auto highlighted); the choice is untouched until release. */
function Open(state: MenuState): { state: MenuState; effect: MenuEffect } {
  return { state: { ...state, open: true, highlighted: null }, effect: 'opened' };
}

/** Cursor moved (offset from the menu centre, px): the highlight follows; nothing is chosen yet. */
function Move(state: MenuState, dx: number, dy: number, deadZonePx: number): MenuState {
  return state.open ? { ...state, highlighted: ItemAt(dx, dy, deadZonePx) } : state;
}

/**
 * G released: on an enabled item -> that item (manual, holds until Auto); in the centre -> Auto; on a disabled item -> the
 * previous choice stays ('disabled': the UI flashes the item / plays the refusal sound). The menu closes in every case.
 */
function Release(state: MenuState, memberCount: number): { state: MenuState; effect: MenuEffect } {
  if (!state.open) return { state, effect: 'none' };
  const closed = { ...state, open: false, highlighted: null };
  if (state.highlighted === null) return { state: { ...closed, choice: 'auto' }, effect: 'auto' };
  if (!IsEnabled(state.highlighted, memberCount)) return { state: closed, effect: 'disabled' };
  return { state: { ...closed, choice: state.highlighted }, effect: 'picked' };
}

/** Esc / right mouse button while open: closes, the previous choice stays. */
function Cancel(state: MenuState): { state: MenuState; effect: MenuEffect } {
  if (!state.open) return { state, effect: 'none' };
  return { state: { ...state, open: false, highlighted: null }, effect: 'cancelled' };
}

/**
 * What a choice means for the formation logic: Auto -> the terrain rules; "All forward" -> the terrain rules' formation with
 * LookYaw mode 'forward'; any other item -> that formation fixed ('diamond' is the wedge with 3 members, the pair with 2).
 * A manual choice whose member count no longer allows it (members died) falls back to Auto.
 */
function Resolve(choice: MenuChoice, memberCount: number): { manualFormation: Formation | null; allForward: boolean } {
  if (choice === 'auto' || !IsEnabled(choice, memberCount)) return { manualFormation: null, allForward: false };
  if (choice === 'all_forward') return { manualFormation: null, allForward: true };
  if (choice === 'diamond') return { manualFormation: memberCount >= 4 ? 'diamond' : 'wedge', allForward: false };
  return { manualFormation: choice, allForward: false };
}

export const FormationMenuRules = { ItemAt, IsEnabled, Open, Move, Release, Cancel, Resolve };

/** Russian labels (UI); the diamond item names the shape the member count gives. */
export function menuLabel(id: MenuChoice, memberCount: number): string {
  switch (id) {
    case 'auto': return 'Авто';
    case 'line': return 'Цепь';
    case 'diamond': return memberCount >= 4 ? 'Ромб' : memberCount === 3 ? 'Клин' : 'Пара';
    case 'echelon_right': return 'Эшелон вправо';
    case 'blizzard': return 'Метельная сцепка';
    case 'column': return 'Колонна';
    case 'hedgehog': return 'Ёж';
    case 'echelon_left': return 'Эшелон влево';
    case 'all_forward': return 'Все вперёд';
  }
}
