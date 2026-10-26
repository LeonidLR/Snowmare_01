// Training Ground / movement: the ENGLISH display strings the game shows (project rule: all in-game text is English; the
// Russian labels stay in the Training Ground UI). Display text never lives in the rules (FORMATION_CONTRACT.md 9 / 10).
// The C++ HUD / menu use the same strings (contract 10.1).

import type { Formation } from './layout.ts';
import type { MenuChoice } from './menu.ts';
import type { GuardCause, GuardLevel } from './rules.ts';

/** Formation names as the game shows them. */
export const FORMATION_LABEL_EN: Record<Formation, string> = {
  diamond: 'Diamond', wedge: 'Wedge', column: 'Column', line: 'Line', echelon_left: 'Echelon left', echelon_right: 'Echelon right',
  wall: 'Wall', hedgehog: 'Hedgehog', blizzard: 'Blizzard diamond',
};

/** Hold-G menu labels (the diamond item names the shape the member count gives). */
export function menuLabelEn(id: MenuChoice, memberCount: number): string {
  switch (id) {
    case 'auto': return 'Auto';
    case 'line': return 'Line';
    case 'diamond': return memberCount >= 4 ? 'Diamond' : memberCount === 3 ? 'Wedge' : 'Pair';
    case 'echelon_right': return 'Echelon right';
    case 'blizzard': return 'Blizzard diamond';
    case 'column': return 'Column';
    case 'hedgehog': return 'Hedgehog';
    case 'echelon_left': return 'Echelon left';
    case 'all_forward': return 'All forward';
  }
}

/**
 * v2.1 HUD line: the formation in force, and while the wall guard falls back, "requested -> in effect (wall)",
 * e.g. "Echelon left -> Column (wall)". memberCount picks Pair for the 2-man diamond / wedge.
 * v2.3 part 9 (user 2026-10-26): the turn fold ("5Б", GuardState.cause 'turn') says "(turn)": "Line -> Column (turn)"; a wall
 * (or no cause given) keeps "(wall)".
 */
export function guardHudText(requested: Formation, effective: Formation, level: GuardLevel, memberCount: number, cause: GuardCause | null = null): string {
  const name = (f: Formation) => ((f === 'diamond' || f === 'wedge') && memberCount === 2 ? 'Pair' : FORMATION_LABEL_EN[f]);
  return level === 0 || requested === effective ? name(effective) : `${name(requested)} -> ${name(effective)} (${cause === 'turn' ? 'turn' : 'wall'})`;
}
