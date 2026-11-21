// Training Ground / level generator v3 - WINDOWS of a band plan (S2_ROOMS_PLAN.md sections 3.1 table and 3.6; step S2.2 piece A): one or two
// windows per room / end suite and exterior wall span (1 under 700, two at 200 from both span ends for 700-800, w / 400 evenly above),
// one in each corridor end on a wall without an exterior door, none in core or service cells. Every window keeps >= 120 from a partition
// axis / corner / door edge and >= 60 jamb to jamb; one that cannot is moved to the nearest legal 50-grid centre of its span, else dropped.
// PURE: integer cm, plan frame (see planDoors.ts), constants from building_plans.json `constants.openings`; the shared opening types are
// exported by planDoors.ts.
import type { BandPlan } from './bandPlan.ts';
import { kitTemplate } from './kitPlan.ts';
import { BUILDING_PLANS } from './bandPlan.ts';
import type { PlanOpening, WallKey } from './planDoors.ts';
import { WALL_ORDER, byId, gridUp, snap, wallInfo, wallSpan } from './planDoors.ts';

const C = BUILDING_PLANS.constants;
const BLOCK = BUILDING_PLANS.recipes.block;
const O = C.openings;
const GRID = C.opening_grid_cm;
const HALF = O.window_cm / 2;

export interface PlanWindowsOpts {
  storeys: number;
  /** The doors of planDoors (exterior ones carry `wall`); only exterior doors of the same storey and wall matter here. */
  doors: PlanOpening[];
  /** Intervals along an exterior wall where a neighbour building stands right outside: no window overlaps one. */
  blocked?: Partial<Record<WallKey, [number, number][]>>;
}

/** The preferred window centres of a span of length `w` starting at `s0` (3.1 table). */
function preferred(s0: number, w: number): number[] {
  if (w < O.window_two_min_cm) return [snap(s0 + w / 2)];
  if (w <= O.window_two_max_cm) return [s0 + O.window_two_inset_cm, s0 + w - O.window_two_inset_cm];
  const n = Math.round(w / O.window_pitch_cm);
  return Array.from({ length: n }, (_, i) => snap(s0 + (w * (i + 0.5)) / n));
}

/** Plans the windows of every storey; ids `f<storey>_w<k>` in emission order (cell id, wall v0 / vD / u0 / uL, along the wall). */
export function planWindows(plan: BandPlan, opts: PlanWindowsOpts): PlanOpening[] {
  if (plan.kind !== 'bands') return [];
  const cells = [...plan.cells].sort(byId);
  const out: PlanOpening[] = [];
  for (let storey = 0; storey < opts.storeys; ++storey) {
    let k = 0;
    const doorsOn = (wall: WallKey): PlanOpening[] => opts.doors.filter((d) => d.kind === 'door' && d.storey === storey && d.wall === wall);
    const placed: Record<WallKey, number[]> = { v0: [], vD: [], u0: [], uL: [] };
    for (const cell of cells) {
      if (cell.kind === 'core' || cell.kind === 'service') continue;
      // kit plans: the hall, the flat's corridor, the bath and the tambour are windowless (the kit pack; the window pass is a later step)
      if (cell.use === 'hall' || cell.use === 'corridor' || cell.use === 'bath' || cell.use === 'tambour') continue;
      for (const wall of WALL_ORDER) {
        const span = wallSpan(plan, cell, wall);
        if (!span) continue;
        const doors = doorsOn(wall);
        const corridor = cell.kind === 'corridor';
        if (corridor && doors.some((d) => d.links.includes(cell.id))) continue; // 3.6: a window in every corridor end that is not an exit door
        const [s0, s1] = span;
        const wants = corridor ? [snap((s0 + s1) / 2)] : preferred(s0, s1 - s0);
        for (const want of wants) {
          const c = place(want, s0, s1, corridor, doors, placed[wall], opts.blocked?.[wall] ?? [], plan.pattern === 'kit' ? kitTemplate(plan.kit?.template).window_clear_cm : O.window_clear_cm);
          if (c === null) continue;
          placed[wall].push(c);
          const w = wallInfo(plan, wall);
          out.push({ id: `f${storey}_w${k++}`, storey, kind: 'window', role: 'window', module: 'wall_window_200', axis: w.axis, at: w.at, c, width: O.window_cm, links: [cell.id, 'street'], wall });
        }
      }
    }
    if (plan.archetype === 'block' && storey >= BLOCK.slot_min_storey) markSlots(plan, out.filter((o) => o.storey === storey));
  }
  return out;
}

/**
 * S2.3 slots (S2_ROOMS_PLAN.md 3.6): in the front room B of every flat (street wall, v = D) the window nearest the room's middle is the flat's
 * balcony / fire-escape slot; the same column on every storey from `slot_min_storey`. Nothing is built from it until a story (4) turns it into a door.
 */
function markSlots(plan: BandPlan, windows: PlanOpening[]): void {
  for (const cell of plan.cells) {
    if (cell.part !== 'B') continue;
    const mid = (cell.u0 + cell.u1) / 2;
    let best: PlanOpening | null = null;
    for (const w of windows) {
      if (w.wall !== 'vD' || w.links[0] !== cell.id) continue;
      if (!best || Math.abs(w.c - mid) < Math.abs(best.c - mid) || (Math.abs(w.c - mid) === Math.abs(best.c - mid) && w.c < best.c)) best = w;
    }
    if (best) best.slot = 'balcony';
  }
}

/** The centre the window gets: `want` when legal, else the nearest legal 50-grid centre inside the span (ties: the smaller), else null (dropped). */
function place(
  want: number, s0: number, s1: number, exempt: boolean,
  doors: PlanOpening[], windows: number[], blocked: [number, number][], clearCm: number = O.window_clear_cm,
): number | null {
  const clear = exempt ? 0 : clearCm; // a corridor end is exempt from the 120 rule (a kit room keeps 60)
  const legal = (c: number): boolean => {
    if (c - HALF < s0 + clear || c + HALF > s1 - clear) return false;
    if (doors.some((d) => Math.abs(c - d.c) < HALF + d.width / 2 + (exempt ? O.window_jamb_cm : clear))) return false;
    if (windows.some((w) => Math.abs(c - w) < O.window_cm + O.window_jamb_cm)) return false;
    return !blocked.some(([b0, b1]) => c + HALF > b0 && c - HALF < b1);
  };
  if (legal(want)) return want;
  let best: number | null = null;
  for (let c = gridUp(s0); c <= s1; c += GRID) {
    if (!legal(c)) continue;
    if (best === null || Math.abs(c - want) < Math.abs(best - want)) best = c; // ascending c: a tie keeps the smaller
  }
  return best;
}
