// Training Ground / level generator v3 - the FRAME of a planned building (S2_ROOMS_PLAN.md 3.1 / 3.2; step S2.2 piece B): maps the plan frame
// of bandPlan.ts (integer cm, u along the long side 0..L, v across 0..D, v = 0 on the core wall) to the generator's draft frame (metres,
// x east, y south) and back, names the four plan walls by the draft side they stand on, and lists the wall segments two cells share.
// PURE geometry, no draft / layout state: generate.ts addPlannedOffice does the emitting. The origin is kept in WHOLE cm so every plan
// point (a multiple of 50 cm, or the 350 core edge) maps to a whole-cm metre value: toUE's rounding cannot move anything.
import type { Vec2 } from '../../movement/geometry.ts';
import type { Side } from '../schema.ts';
import type { BandPlan, PlanCell } from './bandPlan.ts';
import type { WallKey } from './planDoors.ts';

export const OPPOSITE: Readonly<Record<Side, Side>> = { north: 'south', south: 'north', east: 'west', west: 'east' };
/** Outward unit normal of a side in the draft frame (x east, y south), as the legacy windows' mantle edges use it. */
export const SIDE_NORMAL: Readonly<Record<Side, Vec2>> = { north: [0, -1], south: [0, 1], west: [-1, 0], east: [1, 0] };
/** The yaw (degrees, draft frame) an exterior opening on a side faces: placeExteriorDoors / placeWindows. */
export const SIDE_YAW: Readonly<Record<Side, number>> = { north: 270, south: 90, west: 180, east: 0 };

/** The draft rectangle of a building (whole cm) and which way the plan runs in it. */
export interface PlanFrame {
  /** The long side runs along y (u = y - y0); else along x (u = x - x0). */
  alongY: boolean;
  /** The side of the footprint the core wall (v = 0) stands on; v grows from it. */
  coreSide: Side;
  /** The footprint corners in whole cm (draft frame). */
  cx0: number; cy0: number; cx1: number; cy1: number;
}

/** The draft side every plan wall key stands on. */
export function wallSides(f: PlanFrame): Record<WallKey, Side> {
  return { v0: f.coreSide, vD: OPPOSITE[f.coreSide], u0: f.alongY ? 'north' : 'west', uL: f.alongY ? 'south' : 'east' };
}

/** The plan wall key of a draft side. */
export function wallKeyOf(f: PlanFrame, side: Side): WallKey {
  const s = wallSides(f);
  return (Object.keys(s) as WallKey[]).find((k) => s[k] === side)!;
}

/** Is the v axis (across the building) reversed against the draft's x / y direction? (the core wall on the south / east side) */
export const vFlipped = (f: PlanFrame): boolean => f.coreSide === 'south' || f.coreSide === 'east';

/** A plan point (cm) in draft metres. */
export function toDraft(f: PlanFrame, u: number, v: number): Vec2 {
  if (!f.alongY) return [(f.cx0 + u) / 100, (f.coreSide === 'north' ? f.cy0 + v : f.cy1 - v) / 100];
  return [(f.coreSide === 'west' ? f.cx0 + v : f.cx1 - v) / 100, (f.cy0 + u) / 100];
}

/** A draft point (metres) in plan cm (rounded to a whole cm). */
export function toPlan(f: PlanFrame, x: number, y: number): Vec2 {
  const cx = Math.round(x * 100), cy = Math.round(y * 100);
  if (!f.alongY) return [cx - f.cx0, f.coreSide === 'north' ? cy - f.cy0 : f.cy1 - cy];
  return [cy - f.cy0, f.coreSide === 'west' ? cx - f.cx0 : f.cx1 - cx];
}

/** The draft unit vector of the plan's +u / +v direction. */
export function planDir(f: PlanFrame, axis: 'u' | 'v'): Vec2 {
  if (axis === 'u') return f.alongY ? [0, 1] : [1, 0];
  return f.alongY ? (f.coreSide === 'west' ? [1, 0] : [-1, 0]) : (f.coreSide === 'north' ? [0, 1] : [0, -1]);
}

/** The yaw (degrees, draft frame) of a unit axis vector: east 0, south 90, west 180, north 270. */
export const yawOfDir = (d: Vec2): number => (d[0] > 0 ? 0 : d[0] < 0 ? 180 : d[1] > 0 ? 90 : 270);

/** The footprint rectangle (draft metres) of a cell. */
export function cellRect(f: PlanFrame, c: PlanCell): [Vec2, Vec2] {
  const a = toDraft(f, c.u0, c.v0), b = toDraft(f, c.u1, c.v1);
  return [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])]];
}

/** A wall segment two cells share: `axis` 'u' = horizontal (const v = `at`) over u in [lo, hi]; 'v' = vertical (const u = `at`) over v in [lo, hi]. */
export interface SharedSeg { a: PlanCell; b: PlanCell; axis: 'u' | 'v'; at: number; lo: number; hi: number; /** Two corridor cells: an open boundary, no wall. */ seam: boolean }

/** Every shared edge of the plan, cell pairs in the plan's order. A seam (two corridor cells) is flagged: it has neither a partition nor a door. */
export function sharedSegs(plan: BandPlan): SharedSeg[] {
  const out: SharedSeg[] = [];
  const cs = plan.cells;
  for (let i = 0; i < cs.length; ++i) {
    for (let j = i + 1; j < cs.length; ++j) {
      const a = cs[i], b = cs[j];
      const lu = Math.max(a.u0, b.u0), hu = Math.min(a.u1, b.u1), lv = Math.max(a.v0, b.v0), hv = Math.min(a.v1, b.v1);
      const seam = a.kind === 'corridor' && b.kind === 'corridor';
      if ((a.v1 === b.v0 || b.v1 === a.v0) && hu > lu) out.push({ a, b, axis: 'u', at: a.v1 === b.v0 ? a.v1 : b.v1, lo: lu, hi: hu, seam });
      if ((a.u1 === b.u0 || b.u1 === a.u0) && hv > lv) out.push({ a, b, axis: 'v', at: a.u1 === b.u0 ? a.u1 : b.u1, lo: lv, hi: hv, seam });
    }
  }
  return out;
}
