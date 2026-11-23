// Legacy stair repair (2026-11-23; docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md section 4, class (b)): brings the straight
// stairwell flights of a layout ACCEPTED BEFORE the 2026-11-18 / 11-19 stair rules (generate.ts stage 13 "exact slab gaps", the 200 cm
// approach box, planStairCore's "never a core inside a neighbour") to those rules without re-rolling the layout. Two defects of the old
// files, both seen on L_Gen_Depot_X4 (accepted 2026-11-16) by PmcTraversalSmoke:
//   1. the stairwell slab gap of the arrival storey was 80 cm wide and reached 40 cm past the flight's top (and behind its foot):
//      the builder cuts it on top of its own opening, so the ramp's top edge meets the slab only in two 35 cm strips beside the gap -
//      the navmesh erosion (agent radius 35 cm) closes both and the flight is cut off from its arrival floor (partial path, the
//      walker stays on the ground floor under the goal: B_x1_8 / B_x1_16 / B_x2_25 *_block_stair_0). The upper flights pass: the
//      roof opening is the builder's own, which ends exactly at the top;
//   2. a flight (with its approach box) inside another building's overlapping footprint: B_hall_n_stair_0 starts inside B_office_w,
//      whose wall at y -2850 crosses the ramp and whose slabs cap it - the foot is a closed pocket (stuck at link start).
// repairLegacyStairs: (1) every stairwell gap over a straight flight on its arrival storey becomes EXACTLY the flight's footprint
// (generate.ts flightRect: foot -> top, the flight width + 5 cm each side); (2) when the traversal validator still reports one of
// the repaired codes for the building, all its straight flights slide together along their common axis by the smallest step that
// clears them without adding another error; route points linked via a moved flight move with it. The default codes
// (STAIR_REPAIR_CODES) are the defects that break the navmesh: a gap past the flight, a flight in another building, a wall in its
// approach / landing. `full: true` also clears the 200 cm approach-box codes (stair_*_no_floor: the 2026-11-19 rule; the old files
// keep ~105 cm in front of the foot, which the navmesh crosses - X4's upper flights pass with it), so every flight moves. A building
// it cannot fix is left with its exact gaps and reported 'unresolved'. Rotated buildings and switchback cores are not touched (no
// legacy file has them). Pure; the input layout is not mutated. Units: the layout's cm.

import type { Vec2 } from '../movement/geometry.ts';
import { validateBuilding } from './buildingTraversal.ts';
import type { TraversalCode } from './buildingTraversal.ts';
import type { FloorGap, GenBuilding, GenLayout, Stair } from './schema.ts';
import { isSwitchback } from './schema.ts';

/** The validator codes the repair clears by default: the flight defects that cut a flight off the navmesh. */
export const STAIR_REPAIR_CODES: ReadonlySet<TraversalCode> = new Set<TraversalCode>([
  'gap_overshoot', 'stair_in_other_building', 'stair_foot_blocked', 'stair_top_blocked',
]);

/** `full: true`: also the 200 cm approach / landing boxes on solid floor (STAIR_APPROACH_CM, 2026-11-19). */
export const STAIR_REPAIR_CODES_FULL: ReadonlySet<TraversalCode> = new Set<TraversalCode>([
  ...STAIR_REPAIR_CODES, 'stair_foot_no_floor', 'stair_top_no_floor',
]);

/** The exact gap's half width across the flight: generate.ts flightRect (STAIR_FLIGHT_W / 2 + 0.05 m). */
export const EXACT_GAP_HALF_W_CM = 80;

export interface StairRepairOptions {
  /** Slide step, cm (default 10). */
  stepCm?: number;
  /** The largest slide tried either way, cm (default: the building's length along the flights). */
  maxShiftCm?: number;
  /** Also clear the approach-box codes (STAIR_REPAIR_CODES_FULL); default false. */
  full?: boolean;
}

export interface StairRepairChange {
  building: string;
  kind: 'gap_exact' | 'flights_moved' | 'unresolved';
  /** The stair (gap_exact) or the flights (flights_moved) concerned. */
  refs: string[];
  /** flights_moved: the slide along the flights' axis, cm (sign = along +x / +y). */
  shift_cm?: number;
  message: string;
}

export interface StairRepairResult {
  layout: GenLayout;
  changes: StairRepairChange[];
}

const straightFlights = (b: GenBuilding): Stair[] => (b.stairs ?? []).filter((s) => s.kind === 'stairwell' && !isSwitchback(s));

/** The exact slab gap over a straight flight (axis-aligned): from its foot to its top, EXACT_GAP_HALF_W_CM either side of its line. */
export function exactStairGapPoly(st: Stair): Vec2[] {
  const alongX = Math.abs(st.top[0] - st.pos[0]) >= Math.abs(st.top[1] - st.pos[1]);
  const hw = EXACT_GAP_HALF_W_CM;
  const [x0, x1, y0, y1] = alongX
    ? [Math.min(st.pos[0], st.top[0]), Math.max(st.pos[0], st.top[0]), st.pos[1] - hw, st.pos[1] + hw]
    : [st.pos[0] - hw, st.pos[0] + hw, Math.min(st.pos[1], st.top[1]), Math.max(st.pos[1], st.top[1])];
  return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
}

/** Does stairwell gap `g` lie over flight `st` (the validator's rule: within a flight width of its line, overlapping its run)? */
function gapOverFlight(g: FloorGap, st: Stair): boolean {
  const d: Vec2 = [st.top[0] - st.pos[0], st.top[1] - st.pos[1]];
  const L = Math.hypot(d[0], d[1]);
  if (L < 1) return false;
  const u: Vec2 = [d[0] / L, d[1] / L];
  const across = g.poly.map((p) => Math.abs((p[0] - st.pos[0]) * -u[1] + (p[1] - st.pos[1]) * u[0]));
  if (Math.min(...across) > 150) return false;
  const along = g.poly.map((p) => (p[0] - st.pos[0]) * u[0] + (p[1] - st.pos[1]) * u[1]);
  return Math.min(...along) < L && Math.max(...along) > 0;
}

const samePoly = (a: Vec2[], b: Vec2[]): boolean => a.length === b.length && a.every((p, i) => Math.abs(p[0] - b[i][0]) < 0.5 && Math.abs(p[1] - b[i][1]) < 0.5);

/** The building with every stairwell gap over one of its straight flights (on the flight's arrival storey) set to the exact footprint. */
function withExactGaps(b: GenBuilding): { b: GenBuilding; fixed: string[] } {
  const flights = straightFlights(b);
  const fixed: string[] = [];
  const floors_detail = (b.floors_detail ?? []).map((fd) => {
    const gaps = (fd.gaps ?? []).map((g) => {
      if (g.kind !== 'stairwell') return g;
      const st = flights.find((s) => s.to === fd.floor && gapOverFlight(g, s));
      if (!st) return g;
      const poly = exactStairGapPoly(st);
      if (samePoly(g.poly, poly)) return g;
      fixed.push(st.id);
      return { ...g, poly };
    });
    return fd.gaps ? { ...fd, gaps } : fd;
  });
  return { b: fixed.length ? { ...b, floors_detail } : b, fixed };
}

const withBuilding = (l: GenLayout, b: GenBuilding): GenLayout => ({ ...l, buildings: l.buildings.map((x) => (x.id === b.id ? b : x)) });

/** The validator's issues of `b` inside `l`, split into the stair defects the repair owns (`codes`) and the other errors. */
function audit(l: GenLayout, b: GenBuilding, codes: ReadonlySet<TraversalCode>): { stair: string[]; other: string[] } {
  const issues = validateBuilding(l, b).issues;
  return {
    stair: issues.filter((i) => codes.has(i.code)).map((i) => `${i.code}:${i.ref ?? ''}`),
    other: issues.filter((i) => i.severity === 'error' && !codes.has(i.code)).map((i) => `${i.code}:${i.ref ?? ''}`),
  };
}

/** The building with its straight flights (and their gaps) slid by `shift` cm along `axis`. */
function slid(b: GenBuilding, axis: 0 | 1, shift: number): GenBuilding {
  const mv = (p: Vec2): Vec2 => (axis === 0 ? [p[0] + shift, p[1]] : [p[0], p[1] + shift]);
  const ids = new Set(straightFlights(b).map((s) => s.id));
  const stairs = (b.stairs ?? []).map((s) => (ids.has(s.id) ? { ...s, pos: mv(s.pos), top: mv(s.top) } : s));
  return withExactGaps({ ...b, stairs }).b;
}

/** Route points linked via a moved flight (the point before the link at the old foot, the one after at the old top) follow it. */
function movedRoutes(l: GenLayout, before: GenBuilding, after: GenBuilding): GenLayout['routes'] {
  const moved = new Map<string, { from: Stair; to: Stair }>();
  for (const s of before.stairs ?? []) {
    const t = (after.stairs ?? []).find((x) => x.id === s.id);
    if (t && (t.pos[0] !== s.pos[0] || t.pos[1] !== s.pos[1])) moved.set(s.id, { from: s, to: t });
  }
  const near = (p: Vec2, q: Vec2) => Math.hypot(p[0] - q[0], p[1] - q[1]) < 1;
  return l.routes.map((r) => {
    const links = (r.links ?? []).filter((k) => moved.has(k.via));
    if (!links.length) return r;
    const points = r.points.map((p) => [p[0], p[1]] as Vec2);
    for (const k of links) {
      const m = moved.get(k.via)!;
      if (k.at - 1 >= 0 && near(points[k.at - 1], m.from.pos)) points[k.at - 1] = [m.to.pos[0], m.to.pos[1]];
      if (k.at < points.length && near(points[k.at], m.from.top)) points[k.at] = [m.to.top[0], m.to.top[1]];
    }
    return { ...r, points };
  });
}

/** The legacy stair repair of one layout (see the file header). */
export function repairLegacyStairs(layout: GenLayout, opts: StairRepairOptions = {}): StairRepairResult {
  const step = Math.max(1, opts.stepCm ?? 10);
  const codes = opts.full ? STAIR_REPAIR_CODES_FULL : STAIR_REPAIR_CODES;
  const changes: StairRepairChange[] = [];
  let l = layout;
  for (const orig of layout.buildings) {
    const flights = straightFlights(orig);
    if (!orig.enterable || !flights.length || (orig.rotation ?? 0) !== 0 || (orig.stairs ?? []).some(isSwitchback)) continue;
    const start = audit(l, orig, codes);
    if (!start.stair.length) continue;
    // 1. exact gaps
    const ex = withExactGaps(orig);
    let cur = ex.b;
    if (ex.fixed.length) {
      changes.push({ building: orig.id, kind: 'gap_exact', refs: ex.fixed, message: `${orig.id}: stairwell gap(s) of ${ex.fixed.join(', ')} set to the flight's exact footprint (was past its top / behind its foot)` });
    }
    let now = audit(withBuilding(l, cur), cur, codes);
    // 2. slide the flights when a stair defect remains
    if (now.stair.length) {
      const axes = new Set(flights.map((s) => (Math.abs(s.top[0] - s.pos[0]) >= Math.abs(s.top[1] - s.pos[1]) ? 0 : 1)));
      const [[fx0, fy0], [fx1, fy1]] = orig.footprint;
      let done = false;
      if (axes.size === 1) {
        const axis = [...axes][0] as 0 | 1;
        const maxShift = opts.maxShiftCm ?? (axis === 0 ? fx1 - fx0 : fy1 - fy0);
        const otherBefore = new Set(now.other);
        for (let k = 1; !done && k * step <= maxShift; k++) {
          for (const shift of [k * step, -k * step]) {
            const cand = slid(cur, axis, shift);
            const a = audit(withBuilding(l, cand), cand, codes);
            if (a.stair.length || a.other.some((o) => !otherBefore.has(o))) continue;
            changes.push({ building: orig.id, kind: 'flights_moved', refs: flights.map((s) => s.id), shift_cm: shift,
              message: `${orig.id}: ${flights.map((s) => s.id).join(', ')} slid ${shift > 0 ? '+' : ''}${shift} cm along ${axis === 0 ? 'x' : 'y'} (cleared ${now.stair.join(', ')})` });
            cur = cand;
            now = a;
            done = true;
            break;
          }
        }
      }
      if (!done) {
        changes.push({ building: orig.id, kind: 'unresolved', refs: flights.map((s) => s.id), message: `${orig.id}: no slide clears ${now.stair.join(', ')}${axes.size > 1 ? ' (flights on two axes)' : ''}` });
      }
    }
    if (cur !== orig) l = { ...withBuilding(l, cur), routes: movedRoutes(l, orig, cur) };
  }
  return { layout: l, changes };
}
