// Training Ground / level generator v3 - the PLAN VALIDATOR of a band-planned building (S2_ROOMS_PLAN.md 4.2; step S2.2 piece C): the checks
// buildingTraversal.ts validateBuilding runs on a building with `plan.kind === 'bands'` (generate.ts addPlannedOffice) on top of the traversal
// ones - tiling, grid, room sizes, doors / links, loops, corridor width, openings beside the stair core, the stair pads, window rules.
// Everything runs in the building's own (unrotated) frame (rotation.ts toBuildingFrame) in UE cm with a 1 cm tolerance; the grid origin is the
// footprint's min corner. Rooms are found through `links` (ids), never by proximity. Pure: no draft, no engine types.
import type { Vec2 } from '../../movement/geometry.ts';
import type { TraversalIssue, TraversalSeverity } from '../buildingTraversal.ts';
import { toBuildingFrame } from '../rotation.ts';
import type { GenBuilding, GenLayout, GenPartition, GenRoom, Stair, WallOpening } from '../schema.ts';
import { STAIR_APPROACH_CM } from '../schema.ts';
import type { Box, CoreFlight } from '../windowClearance.ts';
import { boxesOverlap, coreBoxesOn } from '../windowClearance.ts';
import { BUILDING_PLANS } from './bandPlan.ts';

const C = BUILDING_PLANS.constants;
const O = C.openings;
/** Every comparison tolerates 1 cm (the generator emits whole cm; a rotated building adds float noise). */
const TOL = 1;
/** The partition thickness the plan emits (S2_ROOMS_PLAN.md 3.1): a corridor's clear width is its short side minus one. */
const PARTITION_T = 20;
/** The clear width of a corridor below which it is an error / a warning (BUILDING_GENERATOR.md 4.1). */
const CLEAR_ERROR_CM = 120;
const CLEAR_WARN_CM = 180;
/** How far across its wall an opening's segment reaches for the stair-core test (the wall's half thickness plus a probe). */
const ACROSS_CM = 50;
/** An opening with a width of at least this (a portal) joins a stairwell pad. */
const PAD_OPENING_MIN_CM = 200;
/** The part of a pad an opening must overlap along the wall. */
const PAD_OVERLAP_CM = 150;

export type PlanCode =
  | 'room_tiling' | 'off_grid' | 'room_size' | 'room_no_door' | 'opening_no_links' | 'storey_no_loop' | 'corridor_narrow'
  | 'opening_into_core' | 'stair_pad_unlinked' | 'window_rule';

/**
 * The static severity of each code (buildingTraversal.ts spreads it into TRAVERSAL_SEVERITY). storey_no_loop is an error on the base
 * plan and a warning once a story was applied; corridor_narrow is a warning between 120 and 180 cm clear: both are set per issue.
 */
export const PLAN_SEVERITY: Record<PlanCode, TraversalSeverity> = {
  room_tiling: 'error',
  off_grid: 'error',
  room_size: 'error',
  room_no_door: 'error',
  opening_no_links: 'error',
  storey_no_loop: 'error',
  corridor_narrow: 'error',
  opening_into_core: 'error',
  stair_pad_unlinked: 'error',
  window_rule: 'warning',
};

type Rect = { x0: number; y0: number; x1: number; y1: number };
const rectOf = (r: GenRoom): Rect => ({ x0: r.bounds.min[0], y0: r.bounds.min[1], x1: r.bounds.max[0], y1: r.bounds.max[1] });
const near = (a: number, b: number, tol = TOL): boolean => Math.abs(a - b) <= tol;
/** Is `rel` (cm from the grid origin) a multiple of `step`? */
const onGrid = (rel: number, step: number): boolean => Math.abs(rel / step - Math.round(rel / step)) * step <= TOL;
const isRoomy = (r: GenRoom): boolean => r.kind === 'room' || r.kind === 'lobby' || r.kind === undefined;

/** One door / window of the building (exterior or on a partition) with its position in the building frame. */
interface Placed {
  op: WallOpening;
  part: GenPartition | null;
  ref: string;
  pos: Vec2;
  /** The coordinate the wall runs along: 0 = x, 1 = y. */
  along: 0 | 1;
}

interface WallOf { along: 0 | 1; /** The wall line (the constant coordinate). */ line: number; lo: number; hi: number; dist: number }

/** The footprint edge nearest to `p`. */
function wallOfPoint(fp: [Vec2, Vec2], p: Vec2): WallOf {
  const [[x0, y0], [x1, y1]] = fp;
  const c: WallOf[] = [
    { along: 0, line: y0, lo: x0, hi: x1, dist: Math.abs(p[1] - y0) },
    { along: 0, line: y1, lo: x0, hi: x1, dist: Math.abs(p[1] - y1) },
    { along: 1, line: x0, lo: y0, hi: y1, dist: Math.abs(p[0] - x0) },
    { along: 1, line: x1, lo: y0, hi: y1, dist: Math.abs(p[0] - x1) },
  ];
  return c.reduce((best, w) => (w.dist < best.dist ? w : best));
}

function placedOpenings(b: GenBuilding): Placed[] {
  const out: Placed[] = [];
  const fp = b.footprint;
  (b.openings ?? []).forEach((op, i) => {
    if (op.kind === 'loophole') return;
    const pos = toBuildingFrame(b, op.pos);
    out.push({ op, part: null, ref: op.id ?? `${b.id} opening #${i}`, pos, along: wallOfPoint(fp, pos).along });
  });
  for (const part of b.partitions ?? []) {
    if (!part.start || !part.end) continue;
    const a = toBuildingFrame(b, part.start), e = toBuildingFrame(b, part.end);
    const along: 0 | 1 = Math.abs(e[0] - a[0]) >= Math.abs(e[1] - a[1]) ? 0 : 1;
    (part.openings ?? []).forEach((op, i) => {
      if (op.kind === 'loophole') return;
      out.push({ op, part, ref: op.id ?? `${part.id} opening #${i}`, pos: toBuildingFrame(b, op.pos), along });
    });
  }
  return out;
}

const isDoorOrWindow = (p: Placed): boolean => p.op.kind === 'door' || p.op.kind === 'window';

// ---- the checks ----------------------------------------------------------------------------------------------------------------------

function roomTiling(b: GenBuilding, rooms: GenRoom[], issues: TraversalIssue[]): void {
  const [[fx0, fy0], [fx1, fy1]] = b.footprint;
  const fpArea = (fx1 - fx0) * (fy1 - fy0);
  for (let f = 0; f < b.floors; f++) {
    const rs = rooms.filter((r) => r.floor === f);
    if (!rs.length) { issues.push({ severity: PLAN_SEVERITY.room_tiling, code: 'room_tiling', ref: `${b.id} f${f}`, message: `${b.id}: storey ${f} has no rooms` }); continue; }
    let area = 0;
    for (const r of rs) {
      const a = rectOf(r);
      area += (a.x1 - a.x0) * (a.y1 - a.y0);
      if (a.x0 < fx0 - TOL || a.y0 < fy0 - TOL || a.x1 > fx1 + TOL || a.y1 > fy1 + TOL) {
        issues.push({ severity: PLAN_SEVERITY.room_tiling, code: 'room_tiling', ref: r.id, message: `${r.id}: reaches outside the footprint` });
      }
    }
    for (let i = 0; i < rs.length; i++) {
      for (let j = i + 1; j < rs.length; j++) {
        const a = rectOf(rs[i]), c = rectOf(rs[j]);
        const ox = Math.min(a.x1, c.x1) - Math.max(a.x0, c.x0), oy = Math.min(a.y1, c.y1) - Math.max(a.y0, c.y0);
        if (ox > TOL && oy > TOL) issues.push({ severity: PLAN_SEVERITY.room_tiling, code: 'room_tiling', ref: rs[i].id, message: `${rs[i].id} overlaps ${rs[j].id} by ${Math.round(ox)} x ${Math.round(oy)} cm` });
      }
    }
    if (Math.abs(area - fpArea) > fpArea * 0.01) {
      issues.push({ severity: PLAN_SEVERITY.room_tiling, code: 'room_tiling', ref: `${b.id} f${f}`, message: `${b.id}: the rooms of storey ${f} cover ${Math.round(area)} of ${Math.round(fpArea)} cm2` });
    }
  }
}

function offGrid(b: GenBuilding, rooms: GenRoom[], placed: Placed[], issues: TraversalIssue[]): void {
  const fp = b.footprint;
  // the edge a stair-core room makes at its depth (200 / 350 from a building wall) is off the 100 grid by design; its neighbours share it
  const core: [number[], number[]] = [[], []];
  for (const r of rooms) {
    if (r.kind !== 'stairwell') continue;
    for (const a of [0, 1] as const) {
      for (const e of [r.bounds.min[a], r.bounds.max[a]]) {
        const d = Math.min(e - fp[0][a], fp[1][a] - e);
        if (near(d, C.core.depth_two_lane_cm) || near(d, C.core.depth_one_lane_cm)) core[a].push(e);
      }
    }
  }
  const axisName = ['x', 'y'];
  for (const r of rooms) {
    for (const a of [0, 1] as const) {
      for (const e of [r.bounds.min[a], r.bounds.max[a]]) {
        if (onGrid(e - fp[0][a], C.grid_cm) || core[a].some((c) => near(c, e))) continue;
        issues.push({ severity: PLAN_SEVERITY.off_grid, code: 'off_grid', ref: r.id, message: `${r.id}: its ${axisName[a]} edge at ${Math.round(e)} is ${Math.round(e - fp[0][a])} cm from the footprint corner, off the ${C.grid_cm} grid` });
      }
    }
  }
  for (const p of placed) {
    if (!isDoorOrWindow(p)) continue;
    const rel = p.pos[p.along] - fp[0][p.along];
    if (!onGrid(rel, C.opening_grid_cm)) issues.push({ severity: PLAN_SEVERITY.off_grid, code: 'off_grid', ref: p.ref, message: `${p.ref}: its centre is ${Math.round(rel * 10) / 10} cm along its wall, off the ${C.opening_grid_cm} grid` });
  }
}

function roomSize(b: GenBuilding, rooms: GenRoom[], issues: TraversalIssue[]): void {
  const [[fx0, fy0], [fx1, fy1]] = b.footprint;
  // u = the long axis of the footprint, v = across. The draft and the UE frames put a SQUARE footprint's u on different axes, so
  // both assignments are accepted there.
  const alongY = fy1 - fy0 >= fx1 - fx0, square = near(fy1 - fy0, fx1 - fx0);
  const bad = (r: GenRoom, what: string): void => { issues.push({ severity: PLAN_SEVERITY.room_size, code: 'room_size', ref: r.id, message: `${r.id}: ${what}` }); };
  const range = (x: number, lo: number, hi: number): boolean => x >= lo - TOL && x <= hi + TOL;
  const uv = (u: number, v: number, fits: (u: number, v: number) => boolean): boolean => fits(u, v) || (square && fits(v, u));
  const suiteFits = (u: number, v: number): boolean => range(u, C.end_bay_cm.min, C.end_bay_cm.max) && v >= C.room_cm.min - TOL;
  const roomFits = (u: number, v: number): boolean => range(u, C.room_cm.min, C.room_cm.max) && range(v, C.room_cm.min, C.room_cm.max);
  const serviceFits = (u: number, v: number): boolean => range(v, C.service_room.depth_min_cm, C.service_room.depth_max_cm) && range(u, C.service_room.width_min_cm, C.service_room.width_max_cm);
  for (const r of rooms) {
    const a = rectOf(r), w = a.x1 - a.x0, h = a.y1 - a.y0, u = alongY ? h : w, v = alongY ? w : h;
    const dims = `${Math.round(u)} x ${Math.round(v)} cm (u x v)`;
    const kind = r.kind ?? 'room';
    switch (kind) {
      case 'room': case 'lobby':
        if (r.unit === 'suite') {
          if (!uv(u, v, suiteFits)) bad(r, `an end suite is ${dims}, outside ${C.end_bay_cm.min}-${C.end_bay_cm.max} x >= ${C.room_cm.min}`);
        } else if (!uv(u, v, roomFits)) bad(r, `a ${r.kind ?? 'room'} is ${dims}, outside ${C.room_cm.min}-${C.room_cm.max}`);
        break;
      case 'service':
        if (!uv(u, v, serviceFits)) bad(r, `a service room is ${dims}, outside depth ${C.service_room.depth_min_cm}-${C.service_room.depth_max_cm} x width ${C.service_room.width_min_cm}-${C.service_room.width_max_cm}`);
        break;
      case 'corridor': {
        const short = Math.min(w, h);
        if (!range(short, C.corridor_cm, C.core.corridor_max_cm)) bad(r, `a corridor ${Math.round(short)} cm wide, outside ${C.corridor_cm}-${C.core.corridor_max_cm}`);
        break;
      }
      case 'stairwell':
        if (!uv(u, v, (uu, vv) => near(uu, C.core.bay_length_cm) && (near(vv, C.core.depth_two_lane_cm) || near(vv, C.core.depth_one_lane_cm)))) {
          bad(r, `a stair core is ${dims}, not ${C.core.bay_length_cm} x ${C.core.depth_two_lane_cm} / ${C.core.depth_one_lane_cm}`);
        }
        break;
      default: break;
    }
  }
}

/** The room ids a door links, per storey; and the seam ids two corridor rooms share (a seam is an open boundary: no opening record). */
function seamGroups(rooms: GenRoom[]): Map<string, GenRoom[]> {
  const seams = new Map<string, GenRoom[]>();
  for (const r of rooms) for (const id of r.door_ids ?? []) if (/_seam_\d+$/.test(id)) seams.set(id, [...(seams.get(id) ?? []), r]);
  return seams;
}

function roomNoDoor(rooms: GenRoom[], placed: Placed[], issues: TraversalIssue[]): void {
  const named = new Set<string>();
  for (const p of placed) if (p.op.kind === 'door') for (const id of p.op.links ?? []) named.add(id);
  for (const grp of seamGroups(rooms).values()) if (grp.length >= 2) for (const r of grp) named.add(r.id);
  for (const r of rooms) {
    if (r.kind === 'stairwell' || named.has(r.id)) continue;
    issues.push({ severity: PLAN_SEVERITY.room_no_door, code: 'room_no_door', ref: r.id, message: `${r.id}: no door links it` });
  }
}

function openingNoLinks(placed: Placed[], rooms: GenRoom[], issues: TraversalIssue[]): void {
  const ids = new Set(rooms.map((r) => r.id));
  for (const p of placed) {
    if (!isDoorOrWindow(p)) continue;
    const { id, links } = p.op;
    if (!id) { issues.push({ severity: PLAN_SEVERITY.opening_no_links, code: 'opening_no_links', ref: p.ref, message: `${p.ref}: an opening without an id` }); continue; }
    if (!Array.isArray(links) || links.length !== 2 || links.some((x) => typeof x !== 'string' || !x)) {
      issues.push({ severity: PLAN_SEVERITY.opening_no_links, code: 'opening_no_links', ref: id, message: `${id}: an opening without its two links` });
      continue;
    }
    // a link names a room of the building, 'street', or a balcony ('balcony:<id>', S2.3)
    const dangling = links.find((x) => x !== 'street' && !x.startsWith('balcony:') && !ids.has(x));
    if (dangling) issues.push({ severity: PLAN_SEVERITY.opening_no_links, code: 'opening_no_links', ref: id, message: `${id}: links ${dangling}, which is no room of the building` });
  }
}

/** Cycle in the multigraph after pruning nodes of degree <= 1 repeatedly (a double edge is a cycle). */
function hasCycle(nodes: Iterable<string>, edges: [string, string][]): boolean {
  const alive = new Set(nodes);
  const deg = (n: string): number => edges.reduce((k, [a, c]) => k + (alive.has(a) && alive.has(c) ? (a === n ? 1 : 0) + (c === n ? 1 : 0) : 0), 0);
  for (let changed = true; changed;) {
    changed = false;
    for (const n of [...alive]) if (deg(n) <= 1) { alive.delete(n); changed = true; }
  }
  return alive.size > 0;
}

function storeyNoLoop(b: GenBuilding, rooms: GenRoom[], placed: Placed[], issues: TraversalIssue[]): void {
  const corridorless = b.plan?.variant === 'corridorless';
  const severity: TraversalSeverity = b.stories?.length ? 'warning' : PLAN_SEVERITY.storey_no_loop;
  for (let f = 0; f < b.floors; f++) {
    // the stair core joins its corridor by two portals: a loop through it needs no room, so it stays out of the graph
    const rs = rooms.filter((r) => r.floor === f && r.kind !== 'stairwell');
    const counted = rs.filter(isRoomy).length;
    if (counted < 3) continue;
    const ids = new Set(rs.map((r) => r.id));
    const nodes = new Set(ids);
    const edges: [string, string][] = [];
    for (const p of placed) {
      if (p.op.kind !== 'door' || p.op.floor !== f || !p.op.links) continue;
      const [a, c] = p.op.links;
      if (ids.has(a) && ids.has(c)) edges.push([a, c]);
      else if (corridorless && ids.has(a) && c === 'street') { nodes.add('street'); edges.push([a, 'street']); }
    }
    for (const grp of seamGroups(rs).values()) for (let i = 1; i < grp.length; i++) edges.push([grp[0].id, grp[i].id]);
    if (!hasCycle(nodes, edges)) {
      issues.push({ severity, code: 'storey_no_loop', ref: `${b.id} f${f}`, message: `${b.id}: storey ${f} has ${counted} rooms but no loop in its door graph` });
    }
  }
}

function corridorNarrow(rooms: GenRoom[], issues: TraversalIssue[]): void {
  for (const r of rooms) {
    if (r.kind !== 'corridor') continue;
    const a = rectOf(r), clear = Math.min(a.x1 - a.x0, a.y1 - a.y0) - PARTITION_T;
    if (clear + TOL < CLEAR_WARN_CM) {
      const error = clear + TOL < CLEAR_ERROR_CM;
      issues.push({ severity: error ? 'error' : 'warning', code: 'corridor_narrow', ref: r.id, message: `${r.id}: ${Math.round(clear)} cm clear (${error ? 'under' : 'below'} ${error ? CLEAR_ERROR_CM : CLEAR_WARN_CM})` });
    }
  }
}

const stairwellFlights = (b: GenBuilding): Stair[] => (b.stairs ?? []).filter((st) => st.kind === 'stairwell' && st.pos && st.top);

/** The segment of a door / window: its width along its wall, +-ACROSS_CM across it. */
function segmentBox(p: Placed): Box {
  const hw = (p.op.width ?? 0) / 2, [x, y] = p.pos;
  return p.along === 0 ? [[x - hw, y - ACROSS_CM], [x + hw, y + ACROSS_CM]] : [[x - ACROSS_CM, y - hw], [x + ACROSS_CM, y + hw]];
}

function openingIntoCore(b: GenBuilding, placed: Placed[], issues: TraversalIssue[]): void {
  const flights: CoreFlight[] = stairwellFlights(b).map((st) => ({ pos: toBuildingFrame(b, st.pos), top: toBuildingFrame(b, st.top), from: st.from, to: st.to }));
  if (!flights.length) return;
  const gaps = (f: number): Box[] => (b.floors_detail ?? []).filter((fd) => fd.floor === f).flatMap((fd) => (fd.gaps ?? []).filter((g) => g.kind === 'stairwell').map((g): Box => {
    const pts = g.poly.map((q) => toBuildingFrame(b, q));
    return [[Math.min(...pts.map((q) => q[0])), Math.min(...pts.map((q) => q[1]))], [Math.max(...pts.map((q) => q[0])), Math.max(...pts.map((q) => q[1]))]];
  }));
  const cache = new Map<number, Box[]>();
  const boxes = (f: number): Box[] => { let x = cache.get(f); if (!x) { x = [...coreBoxesOn(f, flights, [], 1), ...gaps(f)]; cache.set(f, x); } return x; };
  for (const p of placed) {
    if (!isDoorOrWindow(p) || p.op.role === 'portal') continue;
    const seg = segmentBox(p);
    if (boxes(p.op.floor).some((bx) => boxesOverlap(seg, bx, TOL))) {
      issues.push({ severity: PLAN_SEVERITY.opening_into_core, code: 'opening_into_core', ref: p.ref, message: `${p.ref}: its ${p.op.kind} opens onto a flight, its slab gap or an approach box on storey ${p.op.floor}` });
    }
  }
}

function stairPadUnlinked(b: GenBuilding, rooms: GenRoom[], placed: Placed[], issues: TraversalIssue[]): void {
  const doors = placed.filter((p) => p.op.kind === 'door' && p.op.links);
  for (const st of stairwellFlights(b)) {
    const pos = toBuildingFrame(b, st.pos), top = toBuildingFrame(b, st.top);
    const d: Vec2 = [top[0] - pos[0], top[1] - pos[1]];
    if (Math.hypot(d[0], d[1]) < 1) continue;
    const ax: 0 | 1 = Math.abs(d[0]) >= Math.abs(d[1]) ? 0 : 1, sign = Math.sign(d[ax]) || 1;
    // the foot's pad lies behind it on storey `from`, the arrival's pad past the top on storey `to` (none on the roof)
    const ends: { floor: number; at: Vec2; dir: number; what: string }[] = [{ floor: st.from, at: pos, dir: -sign, what: 'foot' }];
    if (st.to < b.floors) ends.push({ floor: st.to, at: top, dir: sign, what: 'top' });
    for (const e of ends) {
      const room = rooms.find((r) => r.kind === 'stairwell' && r.floor === e.floor && near(e.at[0], Math.min(Math.max(e.at[0], r.bounds.min[0]), r.bounds.max[0])) && near(e.at[1], Math.min(Math.max(e.at[1], r.bounds.min[1]), r.bounds.max[1])));
      const ref = `${st.id} ${e.what}`;
      if (!room) { issues.push({ severity: PLAN_SEVERITY.stair_pad_unlinked, code: 'stair_pad_unlinked', ref, message: `${st.id}: its ${e.what} on storey ${e.floor} lies in no stairwell room` }); continue; }
      const r = rectOf(room);
      const a = e.at[ax], c = a + e.dir * STAIR_APPROACH_CM, lo = Math.min(a, c), hi = Math.max(a, c);
      const pad: Box = ax === 0 ? [[lo, r.y0], [hi, r.y1]] : [[r.x0, lo], [r.x1, hi]];
      const linked = doors.some((p) => {
        if (p.op.floor !== e.floor || !p.op.links!.includes(room.id) || (p.op.width ?? 0) < PAD_OPENING_MIN_CM - TOL) return false;
        const touches = p.pos[0] >= pad[0][0] - TOL && p.pos[0] <= pad[1][0] + TOL && p.pos[1] >= pad[0][1] - TOL && p.pos[1] <= pad[1][1] + TOL;
        const half = (p.op.width ?? 0) / 2, c0 = p.pos[p.along];
        const overlap = Math.min(c0 + half, pad[1][p.along]) - Math.max(c0 - half, pad[0][p.along]);
        return touches && overlap >= PAD_OVERLAP_CM - TOL;
      });
      if (!linked) issues.push({ severity: PLAN_SEVERITY.stair_pad_unlinked, code: 'stair_pad_unlinked', ref, message: `${st.id}: the ${e.what} pad on storey ${e.floor} has no opening of >= ${PAD_OPENING_MIN_CM} cm onto it` });
    }
  }
}

/** Does this window sit in a corridor end (the room it links is a corridor)? Such a window is exempt from the 120 rule (S2_ROOMS_PLAN.md 3.6). */
export const isCorridorEndWindow = (rooms: readonly GenRoom[], op: WallOpening): boolean => !!op.links && rooms.some((r) => r.id === op.links![0] && r.kind === 'corridor');

function windowRule(b: GenBuilding, rooms: GenRoom[], placed: Placed[], issues: TraversalIssue[]): void {
  const windows = placed.filter((p) => p.op.kind === 'window' && !p.part);
  // (a) a room touching an exterior wall has a window on its storey
  for (const r of rooms) {
    if (!isRoomy(r) || r.kind === 'lobby' || !(r.walls?.length)) continue; // a lobby's exterior door is its opening (a 400 lobby has no room for a window beside it)
    if (!windows.some((w) => w.op.floor === r.floor && w.op.links?.[0] === r.id)) {
      issues.push({ severity: PLAN_SEVERITY.window_rule, code: 'window_rule', ref: r.id, message: `${r.id}: touches an exterior wall but has no window` });
    }
  }
  // (b) clearances on the window's wall
  for (const w of windows) {
    if (isCorridorEndWindow(rooms, w.op)) continue;
    const wall = wallOfPoint(b.footprint, w.pos);
    if (wall.dist > 30) continue; // not on a perimeter wall (a hand-made opening)
    const c = w.pos[wall.along], half = (w.op.width ?? O.window_cm) / 2;
    const gap = (axis: number, other: number): number => Math.abs(c - axis) - half - other;
    const ends: [string, number, number][] = [['a corner', wall.lo, 0], ['a corner', wall.hi, 0]];
    for (const pt of b.partitions ?? []) {
      if (pt.floor !== w.op.floor || !pt.start || !pt.end) continue;
      for (const q of [toBuildingFrame(b, pt.start), toBuildingFrame(b, pt.end)]) if (near(q[1 - wall.along], wall.line)) ends.push([`the partition ${pt.id}`, q[wall.along], 0]);
    }
    for (const d of placed) {
      if (d.op.kind !== 'door' || d.part || d.op.floor !== w.op.floor || !near(d.pos[1 - wall.along], wall.line)) continue;
      ends.push([`the door ${d.ref}`, d.pos[wall.along], (d.op.width ?? O.door_cm) / 2]);
    }
    for (const [what, at, other] of ends) {
      if (gap(at, other) + TOL < O.window_clear_cm) issues.push({ severity: PLAN_SEVERITY.window_rule, code: 'window_rule', ref: w.ref, message: `${w.ref}: ${Math.round(gap(at, other))} cm from ${what} (< ${O.window_clear_cm})` });
    }
    for (const o of windows) {
      if (o === w || o.op.floor !== w.op.floor || !near(o.pos[1 - wall.along], wall.line)) continue;
      const jamb = Math.abs(c - o.pos[wall.along]) - half - (o.op.width ?? O.window_cm) / 2;
      if (o.ref > w.ref && jamb + TOL < O.window_jamb_cm) issues.push({ severity: PLAN_SEVERITY.window_rule, code: 'window_rule', ref: w.ref, message: `${w.ref}: ${Math.round(jamb)} cm from the window ${o.ref} (< ${O.window_jamb_cm})` });
    }
  }
}

/** The plan checks of one building (a no-op unless it carries a 'bands' plan): appends its issues. */
export function checkPlan(_l: GenLayout, b: GenBuilding, issues: TraversalIssue[]): void {
  if (b.plan?.kind !== 'bands') return;
  const rooms = b.rooms ?? [];
  const placed = placedOpenings(b);
  roomTiling(b, rooms, issues);
  offGrid(b, rooms, placed, issues);
  roomSize(b, rooms, issues);
  roomNoDoor(rooms, placed, issues);
  openingNoLinks(placed, rooms, issues);
  storeyNoLoop(b, rooms, placed, issues);
  corridorNarrow(rooms, issues);
  openingIntoCore(b, placed, issues);
  stairPadUnlinked(b, rooms, placed, issues);
  windowRule(b, rooms, placed, issues);
}
