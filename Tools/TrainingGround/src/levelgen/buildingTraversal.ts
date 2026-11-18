// Building traversal validator (docs/design/levelgen/BUILDING_GENERATOR.md section 5, layer 1): instant, from the layout alone.
// Per enterable building: every room / storey / walkable roof reachable from an exterior door WITHOUT drops, every stairwell
// flight <= 35 deg with a free approach box on solid floor at its foot and after its top, and no slab gap past a flight's top
// (cause A of the 2026-11-18 audit). The page shows the result per building; «Принять» is blocked while one fails.
// Units: the layout's UE cm; points are world coordinates, checked in the building's own (unrotated) frame.

import type { Vec2 } from '../movement/geometry.ts';
import { toBuildingFrame } from './rotation.ts';
import type { GenBuilding, GenLayout, Stair } from './schema.ts';
import { LEVEL_HEIGHT, STAIR_RUN_PER_RISE } from './schema.ts';
import { checkBuildingReachability } from './tacticalEvaluate.ts';

/** Clearances, cm (BUILDING_GENERATOR.md 4.1 / 4.2). */
export const TRAVERSAL = {
  flightWidth: 150,
  approachDepth: 150,
  /** A slab gap may end at most this far past a flight's top (the arrival landing must be solid). */
  gapOvershootTol: 5,
  /** Run tolerance for the 35-degree rule. */
  runTol: 2,
};

export type TraversalCode =
  | 'unreachable_room'
  | 'unreachable_roof'
  | 'unreachable_level'
  | 'stair_steep'
  | 'stair_foot_blocked'
  | 'stair_top_blocked'
  | 'stair_foot_no_floor'
  | 'stair_top_no_floor'
  | 'gap_overshoot'
  | 'no_exterior_door';

export interface TraversalIssue {
  code: TraversalCode;
  message: string;
  /** Stair / room id the issue is about. */
  ref?: string;
}

export interface BuildingTraversal {
  id: string;
  set_piece?: string;
  ok: boolean;
  issues: TraversalIssue[];
  rooms: { total: number; reachable: number };
  levels: { walkable: number[]; reachable: number[] };
  roof: 'none' | 'reachable' | 'unreachable';
}

export interface TraversalReport {
  buildings: BuildingTraversal[];
  checked: number;
  failed: number;
}

type Box = [Vec2, Vec2];

const sub = (a: Vec2, b: Vec2): Vec2 => [a[0] - b[0], a[1] - b[1]];
const add = (a: Vec2, b: Vec2): Vec2 => [a[0] + b[0], a[1] + b[1]];
const mul = (a: Vec2, k: number): Vec2 => [a[0] * k, a[1] * k];
const len = (a: Vec2) => Math.hypot(a[0], a[1]);

/** The axis-aligned box (building frame) of a strip from `a` along unit `u` for `depth`, `width` wide. */
function strip(a: Vec2, u: Vec2, depth: number, width: number): Box {
  const n: Vec2 = [-u[1], u[0]];
  const b = add(a, mul(u, depth));
  const pts = [add(a, mul(n, width / 2)), sub(a, mul(n, width / 2)), add(b, mul(n, width / 2)), sub(b, mul(n, width / 2))];
  return [[Math.min(...pts.map((p) => p[0])), Math.min(...pts.map((p) => p[1]))], [Math.max(...pts.map((p) => p[0])), Math.max(...pts.map((p) => p[1]))]];
}

function boxPoints([a, b]: Box): Vec2[] {
  const cx = (a[0] + b[0]) / 2, cy = (a[1] + b[1]) / 2;
  return [[cx, cy], [a[0] + 5, a[1] + 5], [b[0] - 5, a[1] + 5], [b[0] - 5, b[1] - 5], [a[0] + 5, b[1] - 5]];
}

function inPoly(p: Vec2, poly: Vec2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, yi] = poly[i], [xj, yj] = poly[j];
    if ((yi > p[1]) !== (yj > p[1]) && p[0] < ((xj - xi) * (p[1] - yi)) / (yj - yi || 1e-9) + xi) inside = !inside;
  }
  return inside;
}

/** Segment a-b crosses (or lies inside) the box shrunk by `inset`. */
function segHitsBox(a: Vec2, b: Vec2, [lo, hi]: Box, inset = 2): boolean {
  const x0 = lo[0] + inset, y0 = lo[1] + inset, x1 = hi[0] - inset, y1 = hi[1] - inset;
  let t0 = 0, t1 = 1;
  const d = sub(b, a);
  for (const [p, q] of [[-d[0], a[0] - x0], [d[0], x1 - a[0]], [-d[1], a[1] - y0], [d[1], y1 - a[1]]] as const) {
    if (Math.abs(p) < 1e-9) { if (q < 0) return false; continue; }
    const t = q / p;
    if (p < 0) { if (t > t1) return false; if (t > t0) t0 = t; } else { if (t < t0) return false; if (t < t1) t1 = t; }
  }
  return t0 <= t1;
}

/** Is every probe point of `box` on solid walkable surface of `level` (building frame)? */
function solidAt(b: GenBuilding, level: number, box: Box): boolean {
  const f = b.footprint;
  const inside = (p: Vec2) => p[0] >= f[0][0] - 1 && p[0] <= f[1][0] + 1 && p[1] >= f[0][1] - 1 && p[1] <= f[1][1] + 1;
  const probes = boxPoints(box);
  if (!probes.every(inside)) return false;
  if (level === 0) return true;
  if (level >= b.floors) {
    const holes = [...(b.roof_holes ?? []).map((h) => toBuildingFrame(b, h)), ...(b.hatches ?? []).map((h) => toBuildingFrame(b, h.pos))];
    return probes.every((p) => holes.every((h) => Math.abs(p[0] - h[0]) > 60 || Math.abs(p[1] - h[1]) > 60));
  }
  const fd = (b.floors_detail ?? []).find((x) => x.floor === level);
  if (!fd) return !b.hollow; // legacy storey without detail: a full slab
  const gaps = (fd.gaps ?? []).map((g) => g.poly.map((p) => toBuildingFrame(b, p)));
  return probes.every((p) => !gaps.some((g) => inPoly(p, g)));
}

function blockedOn(b: GenBuilding, l: GenLayout, level: number, box: Box): string | null {
  for (const pt of b.partitions ?? []) {
    if (pt.floor !== level) continue;
    if (segHitsBox(toBuildingFrame(b, pt.start), toBuildingFrame(b, pt.end), box)) return pt.id;
  }
  for (const w of l.walls) {
    if (w.building !== b.id || (w.floor ?? 0) !== level || w.kind === 'building') continue;
    if (segHitsBox(toBuildingFrame(b, w.a), toBuildingFrame(b, w.b), box)) return w.id;
  }
  return null;
}

function checkFlight(l: GenLayout, b: GenBuilding, st: Stair, issues: TraversalIssue[]): void {
  const pos = toBuildingFrame(b, st.pos), top = toBuildingFrame(b, st.top);
  const d = sub(top, pos), L = len(d);
  if (L < 1) return;
  const u: Vec2 = [d[0] / L, d[1] / L];
  const rise = (st.to - st.from) * LEVEL_HEIGHT;
  if (L + TRAVERSAL.runTol < rise * STAIR_RUN_PER_RISE) {
    issues.push({ code: 'stair_steep', ref: st.id, message: `${st.id}: run ${Math.round(L)} cm for a ${rise} cm rise (> 35 deg)` });
  }
  const foot = strip(pos, mul(u, -1), TRAVERSAL.approachDepth, TRAVERSAL.flightWidth);
  const arrive = strip(top, u, TRAVERSAL.approachDepth, TRAVERSAL.flightWidth);
  if (!solidAt(b, st.from, foot)) issues.push({ code: 'stair_foot_no_floor', ref: st.id, message: `${st.id}: no solid floor in the 150 cm approach at its foot (level ${st.from})` });
  if (!solidAt(b, st.to, arrive)) issues.push({ code: 'stair_top_no_floor', ref: st.id, message: `${st.id}: no solid floor in the 150 cm landing after its top (level ${st.to})` });
  const bf = blockedOn(b, l, st.from, foot);
  if (bf) issues.push({ code: 'stair_foot_blocked', ref: st.id, message: `${st.id}: ${bf} stands in the approach at its foot` });
  const bt = st.to < b.floors ? blockedOn(b, l, st.to, arrive) : null;
  if (bt) issues.push({ code: 'stair_top_blocked', ref: st.id, message: `${st.id}: ${bt} stands in the landing after its top` });
  // cause A: the slab gap above the flight must not reach past its top
  const fd = (b.floors_detail ?? []).find((x) => x.floor === st.to);
  for (const g of fd?.gaps ?? []) {
    if (g.kind !== 'stairwell') continue;
    const poly = g.poly.map((p) => toBuildingFrame(b, p));
    const across = poly.map((p) => Math.abs((p[0] - pos[0]) * -u[1] + (p[1] - pos[1]) * u[0]));
    if (Math.min(...across) > TRAVERSAL.flightWidth) continue; // a gap of another flight
    const beyond = Math.max(...poly.map((p) => (p[0] - top[0]) * u[0] + (p[1] - top[1]) * u[1]));
    if (beyond > TRAVERSAL.gapOvershootTol) {
      issues.push({ code: 'gap_overshoot', ref: st.id, message: `${st.id}: the slab gap ${g.id ?? ''} reaches ${Math.round(beyond)} cm past the flight's top (the landing has a hole)` });
    }
  }
}

/** Validate one building. Non-enterable buildings (shells, chimneys, overwatch blocks) pass trivially. */
export function validateBuilding(l: GenLayout, b: GenBuilding): BuildingTraversal {
  const issues: TraversalIssue[] = [];
  const res: BuildingTraversal = { id: b.id, set_piece: b.set_piece, ok: true, issues, rooms: { total: 0, reachable: 0 }, levels: { walkable: [], reachable: [] }, roof: b.roof_walkable ? 'unreachable' : 'none' };
  if (!b.enterable) {
    if (b.roof_walkable) res.roof = 'reachable'; // reached from outside (ladders) - checked by the route / ladder smokes
    return res;
  }
  const hasDoor = (b.openings ?? []).some((o) => o.kind === 'door' && o.floor === 0) || (b.doors ?? []).some((d) => d.floor === 0)
    || (b.stairs ?? []).some((s) => s.kind === 'external');
  if (!hasDoor) issues.push({ code: 'no_exterior_door', message: `${b.id}: enterable but has no exterior door` });

  for (const st of b.stairs ?? []) if (st.kind === 'stairwell') checkFlight(l, b, st, issues);

  // rooms + roof (no drops: floor holes / roof holes are excluded from the graph)
  const noDrops: GenBuilding = { ...b, floor_holes: [], roof_holes: [] };
  const reach = checkBuildingReachability(l, noDrops);
  res.rooms = { total: reach.roomsTotal, reachable: reach.roomsReachable };
  for (const r of reach.unreachableRooms) issues.push({ code: 'unreachable_room', ref: r, message: `${r}: not reachable from an exterior door without a drop` });
  if (b.roof_walkable) {
    res.roof = reach.roofReachable ? 'reachable' : 'unreachable';
    if (!reach.roofReachable) issues.push({ code: 'unreachable_roof', message: `${b.id}: the walkable roof is not reachable without a drop` });
  }

  // levels: a storey with a slab is walkable; reachable when some reachable room lies on it
  const walkable = new Set<number>([0]);
  for (const fd of b.floors_detail ?? []) if (fd.floor < b.floors) walkable.add(fd.floor);
  if (!b.hollow) for (let f = 1; f < b.floors; f++) walkable.add(f);
  const unreachable = new Set(reach.unreachableRooms);
  const reachableLevels = new Set((b.rooms ?? []).filter((r) => !unreachable.has(r.id)).map((r) => r.floor));
  if (!(b.rooms ?? []).length) reachableLevels.add(0);
  res.levels = { walkable: [...walkable].sort(), reachable: [...reachableLevels].sort() };
  for (const lv of walkable) {
    if ((b.rooms ?? []).some((r) => r.floor === lv) && !reachableLevels.has(lv)) {
      issues.push({ code: 'unreachable_level', message: `${b.id}: storey ${lv} is not reachable without a drop` });
    }
  }
  res.ok = issues.length === 0;
  return res;
}

/** Validate every building of a layout. */
export function validateBuildings(l: GenLayout): TraversalReport {
  const buildings = l.buildings.map((b) => validateBuilding(l, b));
  return { buildings, checked: buildings.length, failed: buildings.filter((x) => !x.ok).length };
}
