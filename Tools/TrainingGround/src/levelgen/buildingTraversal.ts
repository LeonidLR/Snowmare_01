// Building traversal validator (docs/design/levelgen/BUILDING_GENERATOR.md section 5, layer 1): instant, from the layout alone.
// Per enterable building: every room / storey / walkable roof reachable from an exterior door WITHOUT drops, every stairwell
// flight <= 35 deg with a free approach box on solid floor at its foot and after its top, and no slab gap past a flight's top
// (cause A of the 2026-11-18 audit). The page shows the result per building; «Принять» is blocked while one fails.
// Units: the layout's UE cm; points are world coordinates, checked in the building's own (unrotated) frame.

import type { Vec2 } from '../movement/geometry.ts';
import { toBuildingFrame } from './rotation.ts';
import { assignRoles } from './buildingRoles.ts';
import type { BuildingRole, GenBuilding, GenLayout, Stair } from './schema.ts';
import { LEVEL_HEIGHT, OPENING_DEFAULTS, STAIR_APPROACH_CM, STAIR_RUN_PER_RISE } from './schema.ts';
import { windowOntoCore, windowPartitionClash } from './windowClearance.ts';
import { effectiveOpeningState, isPassableState } from './openingStates.ts';
import { reachabilityGraph } from './tacticalEvaluate.ts';
import { usableEscapes, usableSnowRamps } from './fireEscapes.ts';
import type { RouteSummary } from './buildingRoutes.ts';
import { exteriorEntries, roomReachability, segHitsBox } from './buildingGraph.ts';

export type { ExteriorEntry } from './buildingGraph.ts';
export { exteriorEntries, segHitsBox } from './buildingGraph.ts';

/** Clearances, cm (BUILDING_GENERATOR.md 4.1 / 4.2). */
export const TRAVERSAL = {
  flightWidth: 150,
  /** The approach box at a flight's foot / after its top: schema.ts STAIR_APPROACH_CM (one source with generate.ts). */
  approachDepth: STAIR_APPROACH_CM,
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
  | 'no_exterior_door'
  | 'unreachable_without_explosive'
  | 'story_rolled_back'
  | 'stair_in_other_building'
  | 'window_onto_stair'
  | 'window_partition_clash';

/** 'error' fails the building (blocks «Принять»); 'warning' is reported only (bands, D, shares - BUILDINGS_TECH_SPEC.md 2.1). */
export type TraversalSeverity = 'error' | 'warning';

/** The severity of every code (all current codes are errors). */
export const TRAVERSAL_SEVERITY: Record<TraversalCode, TraversalSeverity> = {
  unreachable_room: 'error',
  unreachable_roof: 'error',
  unreachable_level: 'error',
  stair_steep: 'error',
  stair_foot_blocked: 'error',
  stair_top_blocked: 'error',
  stair_foot_no_floor: 'error',
  stair_top_no_floor: 'error',
  gap_overshoot: 'error',
  no_exterior_door: 'error',
  /** Every exterior entry is blocked and at least one is 'barricaded': only an explosive gets in (BUILDINGS_TECH_SPEC.md 2.1). */
  unreachable_without_explosive: 'error',
  /** A story's recipe failed the validator and was undone (building-stories.md 5 step 4); reported, never fails the building. */
  story_rolled_back: 'warning',
  stair_in_other_building: 'error',
  /** A window whose inner side is the stair core: a flight, its slab gap, an approach box / landing, a lane wall (3 step 7). */
  window_onto_stair: 'warning',
  /** A partition end inside a window or within its 60 cm jamb (BUILDING_GENERATOR.md 4.4). */
  window_partition_clash: 'warning',
};

export interface TraversalIssue {
  severity: TraversalSeverity;
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
  /** The tactical role (enterable buildings): the stored `role`, else assignRoles (buildingRoles.ts) - shown on the page. */
  role?: BuildingRole;
  /** The applied story ids (b.stories), shown on the page. */
  stories?: string[];
  /** Key route summary (buildingRoutes.ts withRouteSummaries): the stored key_route, else computed live. */
  route?: RouteSummary;
}

/** A building passes when it has no 'error' issue (warnings do not fail it). */
export function traversalOk(issues: readonly TraversalIssue[]): boolean {
  return !issues.some((i) => i.severity === 'error');
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
    issues.push({ severity: TRAVERSAL_SEVERITY.stair_steep, code: 'stair_steep', ref: st.id, message: `${st.id}: run ${Math.round(L)} cm for a ${rise} cm rise (> 35 deg)` });
  }
  const foot = strip(pos, mul(u, -1), TRAVERSAL.approachDepth, TRAVERSAL.flightWidth);
  const arrive = strip(top, u, TRAVERSAL.approachDepth, TRAVERSAL.flightWidth);
  if (!solidAt(b, st.from, foot)) issues.push({ severity: TRAVERSAL_SEVERITY.stair_foot_no_floor, code: 'stair_foot_no_floor', ref: st.id, message: `${st.id}: no solid floor in the ${TRAVERSAL.approachDepth} cm approach at its foot (level ${st.from})` });
  if (!solidAt(b, st.to, arrive)) issues.push({ severity: TRAVERSAL_SEVERITY.stair_top_no_floor, code: 'stair_top_no_floor', ref: st.id, message: `${st.id}: no solid floor in the ${TRAVERSAL.approachDepth} cm landing after its top (level ${st.to})` });
  const bf = blockedOn(b, l, st.from, foot);
  if (bf) issues.push({ severity: TRAVERSAL_SEVERITY.stair_foot_blocked, code: 'stair_foot_blocked', ref: st.id, message: `${st.id}: ${bf} stands in the approach at its foot` });
  const bt = st.to < b.floors ? blockedOn(b, l, st.to, arrive) : null;
  if (bt) issues.push({ severity: TRAVERSAL_SEVERITY.stair_top_blocked, code: 'stair_top_blocked', ref: st.id, message: `${st.id}: ${bt} stands in the landing after its top` });
  // cause A: the slab gap above the flight must not reach past its top
  const fd = (b.floors_detail ?? []).find((x) => x.floor === st.to);
  for (const g of fd?.gaps ?? []) {
    if (g.kind !== 'stairwell') continue;
    const poly = g.poly.map((p) => toBuildingFrame(b, p));
    const across = poly.map((p) => Math.abs((p[0] - pos[0]) * -u[1] + (p[1] - pos[1]) * u[0]));
    if (Math.min(...across) > TRAVERSAL.flightWidth) continue; // a gap of another flight
    const beyond = Math.max(...poly.map((p) => (p[0] - top[0]) * u[0] + (p[1] - top[1]) * u[1]));
    const behind = Math.max(...poly.map((p) => (pos[0] - p[0]) * u[0] + (pos[1] - p[1]) * u[1]));
    const wide = Math.max(...across);
    if (beyond > TRAVERSAL.gapOvershootTol) {
      issues.push({ severity: TRAVERSAL_SEVERITY.gap_overshoot, code: 'gap_overshoot', ref: st.id, message: `${st.id}: the slab gap ${g.id ?? ''} reaches ${Math.round(beyond)} cm past the flight's top (the landing has a hole)` });
    }
    if (behind > TRAVERSAL.gapOvershootTol) {
      issues.push({ severity: TRAVERSAL_SEVERITY.gap_overshoot, code: 'gap_overshoot', ref: st.id, message: `${st.id}: the slab gap ${g.id ?? ''} reaches ${Math.round(behind)} cm behind the flight's foot` });
    }
    if (wide > TRAVERSAL.flightWidth / 2 + 5 + TRAVERSAL.gapOvershootTol) {
      issues.push({ severity: TRAVERSAL_SEVERITY.gap_overshoot, code: 'gap_overshoot', ref: st.id, message: `${st.id}: the slab gap ${g.id ?? ''} is ${Math.round(2 * wide)} cm wide for a ${TRAVERSAL.flightWidth} cm flight (cuts the next lane)` });
    }
  }
}

/** Window clearances (windowClearance.ts, BUILDING_GENERATOR.md 3 step 7 / 4.4): warnings, the page shows every remaining case. */
function checkWindows(l: GenLayout, b: GenBuilding, issues: TraversalIssue[]): void {
  const windows = (b.openings ?? []).filter((op) => op.kind === 'window' && op.pos);
  if (!windows.length) return;
  const flights = (b.stairs ?? []).filter((st) => st.kind === 'stairwell' && st.pos && st.top)
    .map((st) => ({ pos: toBuildingFrame(b, st.pos), top: toBuildingFrame(b, st.top), from: st.from, to: st.to }));
  const laneWalls = l.walls.filter((w) => w.building === b.id && w.kind === 'interior' && /_lanewall_f\d+$/.test(w.id))
    .map((w) => ({ a: toBuildingFrame(b, w.a), b: toBuildingFrame(b, w.b), floor: w.floor ?? 0, thickness: w.thickness ?? 10 }));
  for (const op of windows) {
    const pos = toBuildingFrame(b, op.pos), floor = op.floor ?? 0, width = op.width ?? OPENING_DEFAULTS.window.width;
    const ref = op.id ?? `${b.id} window (${Math.round(op.pos[0])}, ${Math.round(op.pos[1])}) f${floor}`;
    if (windowOntoCore(b.footprint, pos, width, floor, flights, laneWalls, 1)) {
      issues.push({ severity: TRAVERSAL_SEVERITY.window_onto_stair, code: 'window_onto_stair', ref, message: `${ref}: opens onto the stair core (a flight, its gap or an approach box) on storey ${floor}` });
    }
    const parts = (b.partitions ?? []).filter((pt) => pt.floor === floor && pt.start && pt.end); // hand fixtures may omit the ends
    const k = windowPartitionClash(b.footprint, pos, width, parts.map((pt) => ({ start: toBuildingFrame(b, pt.start), end: toBuildingFrame(b, pt.end), thickness: pt.thickness })), 1);
    if (k >= 0) {
      issues.push({ severity: TRAVERSAL_SEVERITY.window_partition_clash, code: 'window_partition_clash', ref, message: `${ref}: the partition ${parts[k].id} ends inside the window or its 60 cm jamb` });
    }
  }
}

/** Validate one building. Non-enterable buildings (shells, chimneys, overwatch blocks) pass trivially. */
export function validateBuilding(l: GenLayout, b: GenBuilding): BuildingTraversal {
  const issues: TraversalIssue[] = [];
  const res: BuildingTraversal = { id: b.id, set_piece: b.set_piece, ok: true, issues, rooms: { total: 0, reachable: 0 }, levels: { walkable: [], reachable: [] }, roof: b.roof_walkable ? 'unreachable' : 'none' };
  if (!b.enterable) {
    // a walkable roof of a closed block (overwatch gallery) is reached from outside: a ladder onto it or an external stair
    if (b.roof_walkable) {
      const up = (l.ladders ?? []).some((ld) => ld.to === `roof:${b.id}`) || (b.stairs ?? []).some((st) => st.kind === 'external' && st.to >= b.floors);
      res.roof = up ? 'reachable' : 'unreachable';
      if (!up) issues.push({ severity: TRAVERSAL_SEVERITY.unreachable_roof, code: 'unreachable_roof', message: `${b.id}: walkable roof without a ladder or external stair onto it` });
    }
    res.ok = traversalOk(issues);
    return res;
  }
  // entries: exterior ground-floor doors + vaultable ground windows (BUILDINGS_TECH_SPEC.md 2.1) + external stairs + fire-escape
  // ground stops (not down_only); an entry in a blocked state (openingStates.ts) is no entry (building-route-model.md rule 3).
  // /1 defaults (doors open, windows broken) pass.
  const entries = exteriorEntries(l, b);
  const escapes = usableEscapes(l, b);
  const hasExternalStair = (b.stairs ?? []).some((s) => s.kind === 'external' && !s.blocked);
  const hasEscape = escapes.some((e) => e.ground && !e.downOnly && e.stops.length > 0);
  // snow ramps (story 10): the upper-storey window at the ramp's top is an entry when it is passable
  const ramps = usableSnowRamps(b).filter((r) => isPassableState(effectiveOpeningState(l, b, r.opening)));
  const hasDoor = hasExternalStair || hasEscape || ramps.length > 0 || entries.some((e) => isPassableState(e.state));
  if (!hasDoor) {
    if (entries.some((e) => e.state === 'barricaded')) {
      issues.push({ severity: TRAVERSAL_SEVERITY.unreachable_without_explosive, code: 'unreachable_without_explosive', message: `${b.id}: every entry is blocked and the only way in is through a barricade (explosive)` });
    } else {
      issues.push({ severity: TRAVERSAL_SEVERITY.no_exterior_door, code: 'no_exterior_door', message: `${b.id}: enterable but has no exterior door` });
    }
  }

  for (const st of b.stairs ?? []) if (st.kind === 'stairwell') checkFlight(l, b, st, issues);
  // a flight (with its approach / landing boxes) inside another building's footprint has no navmesh: that block is solid
  // (2026-11-18 engine test: B_hall_n's core stood inside the B_office_w facade, the TS graph called it reachable)
  for (const st of b.stairs ?? []) {
    if (st.kind !== 'stairwell') continue;
    const hw = TRAVERSAL.flightWidth / 2, ap = TRAVERSAL.approachDepth;
    const fx0 = Math.min(st.pos[0], st.top[0]) - (st.pos[0] === st.top[0] ? hw : ap), fx1 = Math.max(st.pos[0], st.top[0]) + (st.pos[0] === st.top[0] ? hw : ap);
    const fy0 = Math.min(st.pos[1], st.top[1]) - (st.pos[1] === st.top[1] ? hw : ap), fy1 = Math.max(st.pos[1], st.top[1]) + (st.pos[1] === st.top[1] ? hw : ap);
    for (const o of l.buildings) {
      if (o.id === b.id) continue;
      const [[ox0, oy0], [ox1, oy1]] = o.footprint;
      if (Math.min(fx1, ox1) - Math.max(fx0, ox0) > 1 && Math.min(fy1, oy1) - Math.max(fy0, oy0) > 1) {
        issues.push({ severity: TRAVERSAL_SEVERITY.stair_in_other_building, code: 'stair_in_other_building', ref: st.id, message: `${st.id}: the flight or its approach lies inside ${o.id}'s footprint` });
        break;
      }
    }
  }

  checkWindows(l, b, issues);

  // rooms + roof (no drops: floor holes / roof holes are excluded from the graph)
  const noDrops: GenBuilding = { ...b, floor_holes: [], roof_holes: [] };
  const graph = reachabilityGraph(l, noDrops, { openingStates: true, windowEntries: true });
  const reach = roomReachability(graph);
  res.rooms = { total: reach.roomsTotal, reachable: reach.roomsReachable };
  for (const r of reach.unreachableRooms) issues.push({ severity: TRAVERSAL_SEVERITY.unreachable_room, code: 'unreachable_room', ref: r, message: `${r}: not reachable from an exterior door without a drop` });
  if (b.roof_walkable) {
    res.roof = reach.roofReachable ? 'reachable' : 'unreachable';
    if (!reach.roofReachable) issues.push({ severity: TRAVERSAL_SEVERITY.unreachable_roof, code: 'unreachable_roof', message: `${b.id}: the walkable roof is not reachable without a drop` });
  }

  // levels: every storey with a slab is walkable; reachable = climbed from the ground by the graph's level arcs (flights, ladders,
  // escapes, snow ramps, hatch ladders; no drops) - independent of whether the storey has rooms (API review 2026-11-18)
  const walkable = new Set<number>([0]);
  for (const fd of b.floors_detail ?? []) if (fd.floor > 0 && fd.floor < b.floors) walkable.add(fd.floor);
  if (!b.hollow) for (let f = 1; f < b.floors; f++) walkable.add(f);
  const reached = new Set<number>(hasDoor ? [0] : []);
  for (let changed = true; changed;) {
    changed = false;
    for (const [a, c] of graph.levelArcs) if (reached.has(a) && !reached.has(c)) { reached.add(c); changed = true; }
  }
  const num = (a: number, c: number) => a - c;
  res.levels = { walkable: [...walkable].sort(num), reachable: [...reached].filter((lv) => lv < b.floors).sort(num) };
  for (const lv of walkable) {
    if (!reached.has(lv)) issues.push({ severity: TRAVERSAL_SEVERITY.unreachable_level, code: 'unreachable_level', message: `${b.id}: storey ${lv} is not reachable without a drop` });
  }
  res.ok = traversalOk(issues);
  return res;
}

/** Validate every building of a layout. */
export function validateBuildings(l: GenLayout): TraversalReport {
  const roles = assignRoles(l);
  const buildings = l.buildings.map((b) => {
    const r = validateBuilding(l, b), role = b.enterable ? (b.role ?? roles[b.id]) : undefined;
    return role ? { ...r, role } : r;
  });
  return { buildings, checked: buildings.length, failed: buildings.filter((x) => !x.ok).length };
}
