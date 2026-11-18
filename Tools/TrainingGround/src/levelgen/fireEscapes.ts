// Fire escapes + balconies as traversal links (BUILDINGS_TECH_SPEC.md 2.1 "fire escapes, balconies as edges"; building-archetypes.md 3).
// An escape is a chain ground stop -> each balcony stop (by floor) -> the roof (to_roof); a balcony joins the room behind its
// `door_id`. A `down_only` escape (iced, story 11) is one-way, top -> ground. Shared by the room graph (tacticalEvaluate.ts) and the
// level graph (buildingTraversal.ts). Escape / balcony coordinates are in the building's own frame (rotation.ts toBuildingFrame).

import type { Balcony, FireEscape, GenBuilding, GenLayout, SnowRamp, WallOpening } from './schema.ts';
import { unifyOpenings } from './schema.ts';

/** Balcony depth where a fire escape lands (the 150 x 150 approach box must fit; building-archetypes.md 3). */
export const ESCAPE_BALCONY_DEPTH = 150;

/** One usable escape: its stops in floor order with their balconies. */
export interface EscapeChain {
  escape: FireEscape;
  /** The ground stop's approach cell lies outside the footprint (a street link). */
  ground: boolean;
  stops: { floor: number; balcony: Balcony }[];
  /** Reaches the walkable roof. */
  roof: boolean;
  downOnly: boolean;
}

/** The layout ladders that belong to this building's fire escapes (the escape chain replaces their generic ladder edge). */
export function escapeLadderIds(b: Pick<GenBuilding, 'fire_escapes'>): Set<string> {
  return new Set((b.fire_escapes ?? []).flatMap((e) => e.ladder_ids ?? []));
}

/** The building's escapes that give links: a ladder escape needs every listed ladder in layout.ladders; stops need their balcony. */
export function usableEscapes(l: Pick<GenLayout, 'ladders'>, b: GenBuilding): EscapeChain[] {
  const ladderIds = new Set((l.ladders ?? []).map((ld) => ld.id));
  const [[x0, y0], [x1, y1]] = b.footprint;
  const out: EscapeChain[] = [];
  for (const escape of b.fire_escapes ?? []) {
    if (escape.kind === 'ladder' && (escape.ladder_ids ?? []).some((id) => !ladderIds.has(id))) continue;
    const g = escape.ground;
    const ground = !(g[0] > x0 && g[0] < x1 && g[1] > y0 && g[1] < y1);
    const stops = [...escape.stops]
      .sort((a, c) => a.floor - c.floor)
      .map((s) => ({ floor: s.floor, balcony: (b.balconies ?? []).find((bl) => bl.id === s.balcony_id) }))
      .filter((s): s is { floor: number; balcony: Balcony } => !!s.balcony);
    out.push({ escape, ground, stops, roof: escape.to_roof && b.roof_walkable, downOnly: escape.down_only === true });
  }
  return out;
}

/** A snow ramp (story 10) with the upper-storey window it leads to (building-stories.md 3 brick `snow_ramp`). */
export interface SnowRampLink {
  ramp: SnowRamp;
  /** The window at the ramp's top (its state is checked by the caller: a blocked window gives no entry). */
  opening: WallOpening;
  /** The window's storey (>= 1). */
  floor: number;
}

/**
 * The building's snow ramps whose `to_opening` names an upper-storey window (by id, or "openings[i]" = index in unifyOpenings).
 * Ramp `from` (its foot on the ground) is in the building's own frame, like fire escapes and balconies.
 */
export function usableSnowRamps(b: GenBuilding): SnowRampLink[] {
  const ops = unifyOpenings(b);
  const out: SnowRampLink[] = [];
  for (const ramp of b.snow_ramps ?? []) {
    const m = /^openings\[(\d+)\]$/.exec(ramp.to_opening);
    const op = m ? ops[Number(m[1])] : ops.find((o) => o.id === ramp.to_opening);
    if (!op || op.kind !== 'window' || (op.floor ?? 0) < 1) continue;
    out.push({ ramp, opening: op, floor: op.floor });
  }
  return out;
}
