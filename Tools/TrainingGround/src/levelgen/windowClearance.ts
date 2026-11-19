// Window clearances (docs/design/levelgen/BUILDING_GENERATOR.md 3 step 7 and 4.4): no window where the wall's inner side is the
// stair core (a flight, its slab gap, an approach box / landing, the core's lane wall), and no window within a jamb (60 cm) of a
// partition end. One source for the generator (generate.ts addBuilding, metres) and the validator (buildingTraversal.ts, UE cm):
// pure geometry in the building's own (unrotated) frame, `unit` = the length of 1 cm in the caller's units (1 = cm, 0.01 = m).

import type { Vec2 } from '../movement/geometry.ts';
import { STAIR_APPROACH_CM } from './schema.ts';

/** The solid jamb a window keeps to a partition end (cm, BUILDING_GENERATOR.md 4.4). */
export const WINDOW_JAMB_CM = 60;
/** How deep behind the wall line a window's inner side is probed for the stair core (cm; the wall is 30: 35 cm of floor). */
export const WINDOW_PROBE_CM = 50;
/** A window / partition end counts as on a perimeter wall within this distance of the footprint edge (cm). */
const ON_WALL_CM = 30;
/** Stair flight width (cm): generate.ts STAIR_FLIGHT_W x 100 == buildingTraversal.ts TRAVERSAL.flightWidth (pinned by a test). */
export const CORE_FLIGHT_W_CM = 150;

export type Box = [Vec2, Vec2];

/** A stairwell flight of the core (pos = foot, top = arrival). */
export interface CoreFlight { pos: readonly number[]; top: readonly number[]; from: number; to: number }
/** A wall of the core (the lane wall between a switchback's lanes); thickness in the caller's units. */
export interface CoreWall { a: readonly number[]; b: readonly number[]; floor: number; thickness: number }
/** A partition (start / end on the wall lines); thickness in the caller's units. */
export interface PartitionLine { start: readonly number[]; end: readonly number[]; thickness?: number }

/** The axis-aligned box of a strip from `a` along unit `u` for `depth`, `width` wide. */
function strip(a: readonly number[], u: Vec2, depth: number, width: number): Box {
  const n: Vec2 = [-u[1], u[0]], hw = width / 2;
  const b: Vec2 = [a[0] + u[0] * depth, a[1] + u[1] * depth];
  const xs = [a[0] + n[0] * hw, a[0] - n[0] * hw, b[0] + n[0] * hw, b[0] - n[0] * hw];
  const ys = [a[1] + n[1] * hw, a[1] - n[1] * hw, b[1] + n[1] * hw, b[1] - n[1] * hw];
  return [[Math.min(...xs), Math.min(...ys)], [Math.max(...xs), Math.max(...ys)]];
}

/** Do two boxes overlap by more than `eps` on both axes? */
export function boxesOverlap(a: Box, b: Box, eps: number): boolean {
  return Math.min(a[1][0], b[1][0]) - Math.max(a[0][0], b[0][0]) > eps && Math.min(a[1][1], b[1][1]) - Math.max(a[0][1], b[0][1]) > eps;
}

/**
 * The boxes the stair core occupies on `floor`: every flight leaving it (its foot's approach box + the run) or arriving on it
 * (the slab gap over the run + the landing after its top), and the core walls of that floor.
 */
export function coreBoxesOn(floor: number, flights: readonly CoreFlight[], walls: readonly CoreWall[], unit: number): Box[] {
  const out: Box[] = [];
  for (const st of flights) {
    if (st.from !== floor && st.to !== floor) continue;
    const d: Vec2 = [st.top[0] - st.pos[0], st.top[1] - st.pos[1]], L = Math.hypot(d[0], d[1]);
    if (L < 1e-9) continue;
    const u: Vec2 = [d[0] / L, d[1] / L];
    out.push(strip(st.pos, u, L, (CORE_FLIGHT_W_CM + 10) * unit)); // the run / its gap (flightRect: + 5 cm a side)
    if (st.from === floor) out.push(strip(st.pos, [-u[0], -u[1]], STAIR_APPROACH_CM * unit, CORE_FLIGHT_W_CM * unit));
    if (st.to === floor) out.push(strip(st.top, u, STAIR_APPROACH_CM * unit, CORE_FLIGHT_W_CM * unit));
  }
  for (const w of walls) {
    if (w.floor !== floor) continue;
    const d: Vec2 = [w.b[0] - w.a[0], w.b[1] - w.a[1]], L = Math.hypot(d[0], d[1]);
    if (L < 1e-9) continue;
    out.push(strip(w.a, [d[0] / L, d[1] / L], L, w.thickness));
  }
  return out;
}

/** A window's wall in the footprint frame: along-axis ('x' for the north / south walls), the wall line and the inward sign. */
interface WallFrame { along: 0 | 1; line: number; inward: 1 | -1 }

function wallOf(footprint: Box, pos: readonly number[], unit: number): WallFrame | null {
  const [[x0, y0], [x1, y1]] = footprint, tol = ON_WALL_CM * unit;
  const c: [number, WallFrame][] = [
    [Math.abs(pos[1] - y0), { along: 0, line: y0, inward: 1 }],
    [Math.abs(pos[1] - y1), { along: 0, line: y1, inward: -1 }],
    [Math.abs(pos[0] - x0), { along: 1, line: x0, inward: 1 }],
    [Math.abs(pos[0] - x1), { along: 1, line: x1, inward: -1 }],
  ];
  c.sort((a, b) => a[0] - b[0]);
  return c[0][0] <= tol ? c[0][1] : null;
}

/** The box just behind a perimeter window (its width x WINDOW_PROBE_CM inward), or null when `pos` is not on a footprint edge. */
export function windowProbe(footprint: Box, pos: readonly number[], width: number, unit: number): Box | null {
  const w = wallOf(footprint, pos, unit);
  if (!w) return null;
  const t = pos[w.along], hw = width / 2, inner = w.line + w.inward * WINDOW_PROBE_CM * unit;
  const lo = Math.min(w.line, inner), hi = Math.max(w.line, inner);
  return w.along === 0 ? [[t - hw, lo], [t + hw, hi]] : [[lo, t - hw], [hi, t + hw]];
}

/** Does a perimeter window on `floor` open onto the stair core (BUILDING_GENERATOR.md 3 step 7)? */
export function windowOntoCore(footprint: Box, pos: readonly number[], width: number, floor: number,
  flights: readonly CoreFlight[], walls: readonly CoreWall[], unit: number): boolean {
  const probe = windowProbe(footprint, pos, width, unit);
  if (!probe) return false;
  return coreBoxesOn(floor, flights, walls, unit).some((b) => boxesOverlap(probe, b, 1 * unit));
}

/**
 * The index of the first partition with an end on the window's wall within a jamb (WINDOW_JAMB_CM from the window's edge to the
 * partition's face) of the window, or -1 (BUILDING_GENERATOR.md 4.4).
 */
export function windowPartitionClash(footprint: Box, pos: readonly number[], width: number, partitions: readonly PartitionLine[],
  unit: number, defaultThickness = 20 * unit): number {
  const w = wallOf(footprint, pos, unit);
  if (!w) return -1;
  const perp = 1 - w.along, t = pos[w.along];
  for (let i = 0; i < partitions.length; i++) {
    const p = partitions[i], half = (p.thickness ?? defaultThickness) / 2;
    for (const e of [p.start, p.end]) {
      if (Math.abs(e[perp] - w.line) > ON_WALL_CM * unit) continue;
      if (Math.abs(e[w.along] - t) < width / 2 + half + WINDOW_JAMB_CM * unit - 1e-6 * unit) return i;
    }
  }
  return -1;
}
