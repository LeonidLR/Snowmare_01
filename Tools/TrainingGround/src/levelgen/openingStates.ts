// Opening states (docs/design/levelgen/gdd/opening-states.md 1-3, ADR-0002 decision 1): the effective state of a door /
// window / loophole. An explicit `state` always wins; otherwise the default depends on the layout's schema - /1 keeps the
// legacy meaning (doors open, windows broken), /2 uses the opening-states default table (section 3). Pure, no DOM.

import type { DoorState, GenBuilding, GenLayout, WallOpening, WindowState } from './schema.ts';
import { LEVELGEN_SCHEMA_V2 } from './schema.ts';

/** Set-pieces that are ruins: already wrecked - doors 'open', windows 'broken' (opening-states.md 3). */
export const RUIN_SET_PIECES: readonly string[] = ['ruined_two_level'];

/** Where an opening sits, as far as the layout fields tell (opening-states.md 3 table rows). */
export interface OpeningPlacement {
  /** In the building's facade (b.openings / b.doors) vs. in an interior partition / interior wall. */
  exterior: boolean;
  /** floor 0 = ground floor; above = an upper storey. */
  ground: boolean;
}

/**
 * Classify an opening. Interior = it is one of a partition's openings (by identity or id), or its `wall` names a partition or a
 * layout wall of kind 'interior' / 'partition'. Everything else (b.openings, the legacy b.doors, facade walls) is exterior.
 * Street vs. back facade and shop windows are NOT derivable from the /1 fields - the /2 table gives them the same values anyway
 * (street / back), and a shop window has no marker yet, so it falls back to a plain window ('glazed').
 */
export function classifyOpening(
  layout: Partial<Pick<GenLayout, 'walls'>>,
  building: Pick<GenBuilding, 'partitions'>,
  opening: WallOpening,
): OpeningPlacement {
  const ground = (opening.floor ?? 0) <= 0;
  for (const pt of building.partitions ?? []) {
    if (opening.wall !== undefined && opening.wall === pt.id) return { exterior: false, ground };
    for (const op of pt.openings ?? []) {
      if (op === opening || (opening.id !== undefined && op.id === opening.id)) return { exterior: false, ground };
    }
  }
  if (opening.wall !== undefined) {
    const w = (layout.walls ?? []).find((x) => x.id === opening.wall);
    if (w && (w.kind === 'interior' || w.kind === 'partition')) return { exterior: false, ground };
  }
  return { exterior: true, ground };
}

/** The /1 default: doors 'open', windows 'broken' (the legacy meaning); loopholes are always 'fixed'. */
function defaultStateV1(opening: WallOpening): DoorState | WindowState {
  if (opening.kind === 'loophole') return 'fixed';
  return opening.kind === 'door' ? 'open' : 'broken';
}

/**
 * The /2 default table (opening-states.md 3): exterior door 'closed' (hall / warehouse gates too), interior door 'open',
 * ground-floor and upper-storey windows 'glazed', loopholes 'fixed'; ruins: doors 'open', windows 'broken'.
 */
function defaultStateV2(
  layout: Partial<Pick<GenLayout, 'walls'>>,
  building: Pick<GenBuilding, 'set_piece' | 'partitions'>,
  opening: WallOpening,
): DoorState | WindowState {
  if (opening.kind === 'loophole') return 'fixed';
  const ruin = RUIN_SET_PIECES.includes(building.set_piece);
  if (opening.kind === 'door') {
    if (ruin) return 'open';
    return classifyOpening(layout, building, opening).exterior ? 'closed' : 'open';
  }
  // window: ground floor and upper storeys share 'glazed'; a shop window ('fixed') is not marked in the data yet
  return ruin ? 'broken' : 'glazed';
}

/**
 * The effective state of an opening: its explicit `state` if set; otherwise the default of the layout's schema (/2 -> the
 * opening-states table, anything else -> the /1 legacy defaults).
 */
export function effectiveOpeningState(
  layout: Pick<GenLayout, 'schema'> & Partial<Pick<GenLayout, 'walls'>>,
  building: Pick<GenBuilding, 'set_piece' | 'partitions'>,
  opening: WallOpening,
): DoorState | WindowState {
  if (opening.state !== undefined) return opening.state;
  return layout.schema === LEVELGEN_SCHEMA_V2 ? defaultStateV2(layout, building, opening) : defaultStateV1(opening);
}
