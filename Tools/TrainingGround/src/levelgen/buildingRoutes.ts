// Building route model (building generator S1 step 3c; docs/design/levelgen/gdd/building-route-model.md rules 1-10, F1-F4;
// opening-states.md F1 action costs). One weighted graph per building - street node, approach cells + outside legs, entries
// (ground doors / windows by state, escape ground stops, snow-ramp windows, external stairs, ground ladders), rooms, balconies,
// flights, ladders / hatches, fire escapes, drops and explosive edges - built from the same layout data the traversal validator
// reads (exteriorEntries, usableEscapes, usableSnowRamps, effectiveOpeningState). Dijkstra per profile on the WALKABLE graph
// (no explosive, no drop): key route (balanced), fast / quiet entries, F4 choice, D against a baseline (the building with its
// story removed). A TS generation-time proxy; the engine navmesh is the runtime truth (ADR-1). Pure and deterministic.
// Units: layout cm; costs in seconds-equivalent. Coordinates are worked in the building's own (unrotated) frame.

import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dist } from '../movement/geometry.ts';
import { fnv1a } from './props/rng.ts';
import { toBuildingFrame } from './rotation.ts';
import type { TraversalReport } from './buildingTraversal.ts';
import { exteriorEntries, segHitsBox } from './buildingTraversal.ts';
import { escapeLadderIds, usableEscapes, usableSnowRamps } from './fireEscapes.ts';
import { effectiveOpeningState } from './openingStates.ts';
import type { DoorState, GenBuilding, GenLayout, GenRoom, Route, WallOpening, WindowState } from './schema.ts';
import { LEVEL_HEIGHT, ladderForLevels, OPENING_DEFAULTS, unifyOpenings } from './schema.ts';

// ---- cost table (later building_route_costs.json) -----------------------------------------------------------------------------------

/** Edge modes (route-model rule 3). */
export type EdgeMode = 'walk' | 'open' | 'kick' | 'shotgun' | 'vault' | 'shatter' | 'climb' | 'explosive' | 'drop';
export type RouteProfile = 'balanced' | 'quiet' | 'fast';
export const ROUTE_PROFILES: readonly RouteProfile[] = ['balanced', 'quiet', 'fast'];

export interface RouteCostTable {
  /** Reference walk speed, m/s (BotWalkSpeedCm 300). */
  v_ref: number;
  /** Seconds per ladder rung (audit 1.4). */
  t_rung: number;
  /** Operatives moving together (solo primary = 1). */
  n_squad: number;
  /** Extra seconds per following operative on a wide edge. */
  t_follow: number;
  /** Approach cell distance outside an exterior opening, cm (rule 2). */
  approach_cm: number;
  /** Street node fallback distance off the street facade, cm (rule 2). */
  street_fallback_cm: number;
  /** Action time t (s), noise radius n (m), explosives x per mode (opening-states F1; climb / walk / drop carry none). */
  actions: Record<EdgeMode, { t: number; n: number; x: number }>;
  /** How a locked door is breached on the walkable graph (shotgun waits for the shotgun in the loadout; sapper = explosive). */
  locked_breach: EdgeMode[];
  /** Profile weights: a_n s per noise metre, a_x s per explosive, a_e weight on exposed seconds (route-model F1). */
  profiles: Record<RouteProfile, { a_n: number; a_x: number; a_e: number }>;
  /** Exposure factors of outside edges: above storey 0 / at ground; inside = 0. */
  exposure: { above_ground: number; ground: number };
  /** Detour-ratio bands (report only, F3). */
  detour_bands: { open: [number, number]; storied: [number, number]; warn: number };
}

/** The defaults of building-route-model.md F1 and opening-states.md F1 (all *proposal*). */
export const ROUTE_COSTS: RouteCostTable = {
  v_ref: 3.0,
  t_rung: 0.9,
  n_squad: 1,
  t_follow: 0.5,
  approach_cm: 150,
  street_fallback_cm: 1000,
  actions: {
    walk: { t: 0, n: 0, x: 0 },
    open: { t: 1, n: 0, x: 0 },
    kick: { t: 3, n: 15, x: 0 },
    shotgun: { t: 1, n: 30, x: 0 },
    vault: { t: 1.2, n: 0, x: 0 },
    shatter: { t: 1.7, n: 10, x: 0 },
    climb: { t: 0, n: 0, x: 0 },
    explosive: { t: 5, n: 25, x: 1 },
    drop: { t: 0, n: 0, x: 0 },
  },
  locked_breach: ['kick'],
  profiles: {
    balanced: { a_n: 0.5, a_x: 60, a_e: 1 },
    quiet: { a_n: 3, a_x: 120, a_e: 1 },
    fast: { a_n: 0.1, a_x: 20, a_e: 0.2 },
  },
  exposure: { above_ground: 1.0, ground: 0.5 },
  detour_bands: { open: [1.0, 1.3], storied: [1.5, 4.0], warn: 4.0 },
};

/** The cost-table hash stored with every route (rule 10; a route with another hash is stale). */
export function routeCostHash(costs: RouteCostTable = ROUTE_COSTS): string {
  return fnv1a(JSON.stringify(costs)).toString(16).padStart(8, '0');
}

// ---- graph --------------------------------------------------------------------------------------------------------------------------

export interface RouteEdge {
  /** Creation-order id ("e0007"): the last tie-break of F2. */
  id: string;
  from: string;
  to: string;
  mode: EdgeMode;
  /** Walking length, m (flights along the slope; outside legs by rule 2). */
  len_m: number;
  /** Ladder / hatch rungs (time = rungs * t_rung instead of a walk). */
  rungs: number;
  /** Ladder, hatch, window vault, fire-escape ladder: the squad passes one by one (F1). */
  single_file: boolean;
  /** Exposure factor (0 inside, 0.5 outside at ground, 1.0 outside above storey 0). */
  exposure: number;
  /** An outside leg street -> approach cell (F2 tie-break "fewer outside legs"). */
  outside: boolean;
  /** Set on the edge that crosses into the building: the exterior opening ref (else the ladder / stair id). */
  entry?: string;
}

export interface RouteGraph {
  building: string;
  nodes: string[];
  edges: RouteEdge[];
  /** Node -> building-frame position (cm). */
  pos: Map<string, Vec2>;
  /** Node -> storey (rooms, balconies, roof = floors). */
  floorOf: Map<string, number>;
  rooms: string[];
  roof: string | null;
}

export const STREET = 'street';
export const ROOF = 'roof';

const ACTION_MODES: readonly EdgeMode[] = ['open', 'kick', 'shotgun', 'vault', 'shatter', 'explosive'];
const centreOf = (rm: GenRoom): Vec2 => [(rm.bounds.min[0] + rm.bounds.max[0]) / 2, (rm.bounds.min[1] + rm.bounds.max[1]) / 2];
const inBounds = (p: Vec2, rm: GenRoom, tol = 60) =>
  p[0] >= rm.bounds.min[0] - tol && p[0] <= rm.bounds.max[0] + tol && p[1] >= rm.bounds.min[1] - tol && p[1] <= rm.bounds.max[1] + tol;

/** The validator's rooms (b.rooms, else one virtual room per walkable storey). */
function roomsOf(b: GenBuilding): GenRoom[] {
  if (b.rooms?.length) return b.rooms;
  return Array.from({ length: b.hollow ? 1 : b.floors }, (_, f) => ({ id: `${b.id}_floor_${f}`, floor: f, bounds: { min: b.footprint[0], max: b.footprint[1] } }));
}

/** Modes a door / window in `state` gives (blocked = none; barricaded = explosive only). */
export function openingModes(kind: WallOpening['kind'], state: DoorState | WindowState, costs: RouteCostTable = ROUTE_COSTS): EdgeMode[] {
  if (kind === 'door') {
    switch (state) {
      case 'open': return ['walk'];
      case 'closed': return ['open'];
      case 'locked': return [...costs.locked_breach];
      case 'barricaded': return ['explosive'];
      default: return [];
    }
  }
  if (kind === 'window') return state === 'broken' ? ['vault'] : state === 'glazed' ? ['shatter'] : [];
  return [];
}

/** The outward normal (building frame) of the facade nearest to `q`. */
function facadeNormal(b: GenBuilding, q: Vec2): Vec2 {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const d: [Vec2, number][] = [[[-1, 0], Math.abs(q[0] - x0)], [[1, 0], Math.abs(q[0] - x1)], [[0, -1], Math.abs(q[1] - y0)], [[0, 1], Math.abs(q[1] - y1)]];
  return d.reduce((a, c) => (c[1] < a[1] ? c : a))[0];
}

/** Position of `q` on the ring (footprint + `off`, rectilinear perimeter) as a perimeter parameter, plus the ring length. */
function ringParam(b: GenBuilding, off: number, q: Vec2): { s: number; p: Vec2; per: number } {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const X0 = x0 - off, Y0 = y0 - off, X1 = x1 + off, Y1 = y1 + off, W = X1 - X0, H = Y1 - Y0;
  const cx = Math.min(X1, Math.max(X0, q[0])), cy = Math.min(Y1, Math.max(Y0, q[1]));
  const cands: [Vec2, number][] = [
    [[cx, Y0], cx - X0],
    [[X1, cy], W + (cy - Y0)],
    [[cx, Y1], W + H + (X1 - cx)],
    [[X0, cy], 2 * W + H + (Y1 - cy)],
  ];
  const [p, s] = cands.reduce((a, c) => (dist(c[0], q) < dist(a[0], q) - 1e-9 ? c : a));
  return { s, p, per: 2 * (W + H) };
}

/** Outside leg length, cm (rule 2): straight when it clears the footprint, else onto the 150 cm ring, round it, and off. */
export function outsideLegCm(b: GenBuilding, street: Vec2, app: Vec2, off = ROUTE_COSTS.approach_cm): number {
  if (!segHitsBox(street, app, b.footprint, 2)) return dist(street, app);
  const a = ringParam(b, off, street), c = ringParam(b, off, app);
  const d = Math.abs(a.s - c.s);
  return dist(street, a.p) + Math.min(d, a.per - d) + dist(c.p, app);
}

/**
 * The street node, building frame (rule 2): the point of the mandatory route nearest to the footprint centre (tie-free for a
 * road parallel to the facade); without a route, 10 m out from the middle of the street facade (the side with the most exterior
 * ground doors, ties N E S W).
 */
export function streetPoint(l: GenLayout, b: GenBuilding, costs: RouteCostTable = ROUTE_COSTS): Vec2 {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const c: Vec2 = [(x0 + x1) / 2, (y0 + y1) / 2];
  const route = (l.routes ?? []).find((r) => r.mandatory)?.points ?? [];
  if (route.length) {
    let best: Vec2 = route[0] as Vec2, bd = dist(c, best);
    for (let i = 0; i + 1 < route.length; i++) {
      const p = closestOnSegment(c, route[i] as Vec2, route[i + 1] as Vec2).point, d = dist(c, p);
      if (d < bd - 1e-9) { best = p; bd = d; }
    }
    return toBuildingFrame(b, best);
  }
  const sides: [Vec2, Vec2][] = [[[c[0], y0], [0, -1]], [[x1, c[1]], [1, 0]], [[c[0], y1], [0, 1]], [[x0, c[1]], [-1, 0]]];
  const doors = unifyOpenings(b).filter((o) => o.kind === 'door' && (o.floor ?? 0) === 0).map((o) => facadeNormal(b, toBuildingFrame(b, o.pos)));
  let pick = sides[0], n = -1;
  for (const s of sides) {
    const k = doors.filter((d) => d[0] === s[1][0] && d[1] === s[1][1]).length;
    if (k > n) { pick = s; n = k; }
  }
  return [pick[0][0] + pick[1][0] * costs.street_fallback_cm, pick[0][1] + pick[1][1] * costs.street_fallback_cm];
}

/** Build the weighted route graph of one enterable building (rules 1-4). */
export function buildRouteGraph(l: GenLayout, b: GenBuilding, costs: RouteCostTable = ROUTE_COSTS): RouteGraph {
  const g: RouteGraph = { building: b.id, nodes: [], edges: [], pos: new Map(), floorOf: new Map(), rooms: [], roof: null };
  const F = (p: readonly number[]) => toBuildingFrame(b, p);
  const node = (id: string, p: Vec2, floor: number) => {
    if (g.pos.has(id)) return;
    g.nodes.push(id);
    g.pos.set(id, p);
    g.floorOf.set(id, floor);
  };
  let k = 0;
  const add = (from: string, to: string, mode: EdgeMode, lenCm: number, o: { rungs?: number; single?: boolean; exposure?: number; outside?: boolean; entry?: string; oneWay?: boolean } = {}) => {
    const base = { mode, len_m: lenCm / 100, rungs: o.rungs ?? 0, single_file: o.single ?? false, exposure: o.exposure ?? 0, outside: o.outside ?? false };
    g.edges.push({ id: `e${String(k++).padStart(4, '0')}`, from, to, ...base, ...(o.entry ? { entry: o.entry } : {}) });
    if (!o.oneWay) g.edges.push({ id: `e${String(k++).padStart(4, '0')}`, from: to, to: from, ...base });
  };
  const centre: Vec2 = [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
  const street = streetPoint(l, b, costs);
  node(STREET, street, 0);
  if (!b.enterable) return g;
  const rooms = roomsOf(b);
  for (const rm of rooms) { node(rm.id, centreOf(rm), rm.floor); g.rooms.push(rm.id); }
  if (b.roof_walkable) { node(ROOF, centre, b.floors); g.roof = ROOF; }
  const roomsAt = (floor: number, p: Vec2, fallbackSingle = true): GenRoom[] => {
    const on = rooms.filter((rm) => rm.floor === floor);
    const hits = on.filter((rm) => inBounds(p, rm));
    return hits.length ? hits : fallbackSingle && on.length === 1 ? on : [];
  };
  const roomsOfOpening = (op: WallOpening, floor: number): GenRoom[] =>
    rooms.filter((rm) => rm.floor === floor && ((op.id && rm.door_ids?.includes(op.id)) || inBounds(F(op.pos), rm)));
  const ops = unifyOpenings(b);
  const refOf = (op: WallOpening) => {
    const i = ops.indexOf(op);
    return op.id ?? (i >= 0 ? `openings[${i}]` : `door@${op.pos[0]},${op.pos[1]}`);
  };
  const stateOf = (op: WallOpening) => effectiveOpeningState(l, b, op);
  const posOf = (id: string) => g.pos.get(id)!;
  /** An approach node with its outside leg from the street (ground exposure). */
  const approach = (id: string, p: Vec2) => {
    if (g.pos.has(id)) return id;
    node(id, p, 0);
    add(STREET, id, 'walk', outsideLegCm(b, street, p, costs.approach_cm), { exposure: costs.exposure.ground, outside: true });
    return id;
  };
  const exAbove = costs.exposure.above_ground;

  // entries: ground doors + vaultable ground windows (the validator's list, states honoured)
  for (const { opening: op, state } of exteriorEntries(l, b)) {
    const modes = openingModes(op.kind, state, costs);
    if (!modes.length) continue;
    const q = F(op.pos), n = facadeNormal(b, q), ref = refOf(op);
    const app = approach(`app:${ref}`, [q[0] + n[0] * costs.approach_cm, q[1] + n[1] * costs.approach_cm]);
    const single = op.kind === 'window' || (op.width ?? OPENING_DEFAULTS.door.width) < OPENING_DEFAULTS.door.width;
    for (const rm of roomsOfOpening(op, 0)) for (const m of modes) add(app, rm.id, m, dist(posOf(app), q) + dist(q, posOf(rm.id)), { single, entry: ref });
  }
  // snow ramps (story 10): ground foot -> ramp top at the window (exposed) -> the room behind the window
  for (const { ramp, opening: op, floor } of usableSnowRamps(b)) {
    const modes = openingModes('window', stateOf(op), costs);
    if (!modes.length) continue;
    const q = F(op.pos), foot = ramp.from as Vec2;
    const app = approach(`app:ramp:${ramp.id}`, foot);
    const top = `ramp:${ramp.id}`;
    node(top, q, floor);
    add(app, top, 'walk', Math.hypot(dist(foot, q), floor * LEVEL_HEIGHT), { exposure: exAbove });
    for (const rm of roomsAt(floor, q)) for (const m of modes) add(top, rm.id, m, dist(q, posOf(rm.id)), { single: true, entry: refOf(op) });
  }
  // doors on upper storeys joining rooms (the validator's legacy rule) and interior partition doors
  const roomLink = (op: WallOpening, floor: number) => {
    const modes = openingModes(op.kind, stateOf(op), costs);
    const hits = roomsOfOpening(op, floor), q = F(op.pos);
    for (let i = 1; i < hits.length; i++) for (const m of modes) add(hits[0].id, hits[i].id, m, dist(posOf(hits[0].id), q) + dist(q, posOf(hits[i].id)));
  };
  const balconyDoors = new Set((b.balconies ?? []).map((x) => x.door_id));
  for (const op of ops) if (op.kind === 'door' && (op.floor ?? 0) > 0 && !(op.id && balconyDoors.has(op.id))) roomLink(op, op.floor);
  for (const pt of b.partitions ?? []) for (const op of pt.openings ?? []) if (op.kind === 'door') roomLink(op, pt.floor);
  // rooms sharing a door id (hand-made room graphs without partition records)
  const allOps = [...ops, ...(b.partitions ?? []).flatMap((pt) => pt.openings ?? [])];
  const byDoor = new Map<string, GenRoom[]>();
  for (const rm of rooms) for (const did of rm.door_ids ?? []) byDoor.set(did, [...(byDoor.get(did) ?? []), rm]);
  for (const [did, group] of byDoor) {
    const op = allOps.find((o) => o.id === did);
    const modes = op ? openingModes(op.kind, stateOf(op), costs) : (['walk'] as EdgeMode[]);
    for (let i = 1; i < group.length; i++) {
      if (group[i].floor !== group[0].floor) continue;
      for (const m of modes) add(group[0].id, group[i].id, m, dist(posOf(group[0].id), posOf(group[i].id)));
    }
  }
  // flights (blocked = no edge; down_only = top -> foot only); external stairs are entries from the street
  for (const st of b.stairs ?? []) {
    if (st.blocked) continue;
    const pos = F(st.pos), top = F(st.top), mid = st.mid ? F(st.mid) : null;
    const rise = (st.to - (st.kind === 'external' ? 0 : st.from)) * LEVEL_HEIGHT;
    const slope = Math.hypot(dist(pos, mid ?? top), rise) + (mid ? dist(mid, top) : 0);
    const toNodes = st.to >= b.floors && b.roof_walkable ? [ROOF] : roomsAt(st.to, top).map((rm) => rm.id);
    if (st.kind === 'external') {
      const app = approach(`app:${st.id}`, pos);
      for (const tn of toNodes) {
        if (st.down_only) add(tn, app, 'walk', slope + dist(top, posOf(tn)), { exposure: exAbove, oneWay: true });
        else add(app, tn, 'walk', slope + dist(top, posOf(tn)), { exposure: exAbove, entry: st.id });
      }
      continue;
    }
    for (const fr of roomsAt(st.from, pos)) for (const tn of toNodes) {
      const len = dist(posOf(fr.id), pos) + slope + dist(top, posOf(tn));
      if (st.down_only) add(tn, fr.id, 'walk', len, { oneWay: true });
      else add(fr.id, tn, 'walk', len);
    }
  }
  // fire escapes: ground stop -> balcony stops -> roof (exposed, ladders single file); balcony -> room by its door's state
  for (const ch of usableEscapes(l, b)) {
    const ladders = (ch.escape.ladder_ids ?? []).map((id) => (l.ladders ?? []).find((ld) => ld.id === id));
    const seq: { id: string; floor: number }[] = [];
    if (ch.ground) seq.push({ id: approach(`app:${ch.escape.id}`, ch.escape.ground as Vec2), floor: 0 });
    for (const s of ch.stops) {
      const r = s.balcony.rect;
      const id = `balcony:${s.balcony.id}`;
      node(id, [(r[0][0] + r[1][0]) / 2, (r[0][1] + r[1][1]) / 2], s.floor);
      seq.push({ id, floor: s.floor });
    }
    if (ch.roof) seq.push({ id: ROOF, floor: b.floors });
    for (let i = 0; i + 1 < seq.length; i++) {
      const lo = seq[i], hi = seq[i + 1], levels = hi.floor - lo.floor;
      const ld = ch.escape.kind === 'ladder' ? ladders[ch.ground ? i : i + 1] : undefined;
      const o = ch.escape.kind === 'ladder'
        ? { rungs: ld?.rungs ?? ladderForLevels(levels).rungs, single: true, exposure: exAbove }
        : { exposure: exAbove };
      const len = ch.escape.kind === 'ladder' ? 0 : Math.hypot(levels * LEVEL_HEIGHT * 1.43, levels * LEVEL_HEIGHT);
      const mode: EdgeMode = ch.escape.kind === 'ladder' ? 'climb' : 'walk';
      if (ch.downOnly) add(hi.id, lo.id, mode, len, { ...o, oneWay: true });
      else add(lo.id, hi.id, mode, len, o);
    }
    for (const { balcony } of ch.stops) {
      const dr = ops.find((o) => o.id === balcony.door_id && o.kind === 'door');
      if (!dr) continue;
      const q = F(dr.pos), bid = `balcony:${balcony.id}`;
      const behind = rooms.filter((rm) => rm.floor === balcony.floor && (rm.door_ids?.includes(balcony.door_id) || inBounds(q, rm)));
      for (const rm of behind) for (const m of openingModes('door', stateOf(dr), costs)) {
        add(bid, rm.id, m, dist(posOf(bid), q) + dist(q, posOf(rm.id)), { entry: refOf(dr) });
      }
    }
  }
  // roof hatches (floor -> roof, a ladder)
  if (b.roof_walkable) {
    for (const ht of b.hatches ?? []) {
      const q = F(ht.pos), ld = (l.ladders ?? []).find((x) => x.id === ht.ladder_id);
      for (const rm of roomsAt(ht.floor, q)) add(rm.id, ROOF, 'climb', dist(posOf(rm.id), q), { rungs: ld?.rungs ?? ladderForLevels(b.floors - ht.floor).rungs, single: true });
    }
  }
  // drops (one way down): roof holes onto the top storey, floor holes one storey down
  for (const rh of b.roof_holes ?? []) {
    if (!b.roof_walkable || b.floors < 1) continue;
    const q = F(rh);
    for (const rm of roomsAt(b.hollow ? 0 : b.floors - 1, q, false)) add(ROOF, rm.id, 'drop', dist(centre, q) + dist(q, posOf(rm.id)), { oneWay: true });
  }
  for (const fh of b.floor_holes ?? []) {
    const q = F(fh.pos);
    for (const u of roomsAt(fh.floor, q, false)) for (const lo of roomsAt(Math.max(0, fh.floor - 1), q, false)) {
      add(u.id, lo.id, 'drop', dist(posOf(u.id), q) + dist(q, posOf(lo.id)), { oneWay: true });
    }
  }
  // layout ladders of this building (a fire escape's ladders are its chain)
  const escLadders = escapeLadderIds(b);
  const levelNodes = (layer: string, p: Vec2): string[] => {
    if (layer === `roof:${b.id}`) return b.roof_walkable ? [ROOF] : [];
    const m = /^floor:([^:]+):(\d+)$/.exec(layer);
    return m && m[1] === b.id ? roomsAt(Number(m[2]), p).map((rm) => rm.id) : [];
  };
  for (const ld of l.ladders ?? []) {
    if (escLadders.has(ld.id)) continue;
    const foot = F(ld.foot), top = F(ld.top);
    const tos = levelNodes(ld.to, top);
    if (!tos.length) continue;
    if (ld.from === 'ground' || ld.from === 'cut') {
      const app = approach(`app:${ld.id}`, foot);
      for (const tn of tos) add(app, tn, 'climb', dist(top, posOf(tn)), { rungs: ld.rungs, single: true, exposure: exAbove, entry: ld.id });
    } else {
      for (const fn of levelNodes(ld.from, foot)) for (const tn of tos) {
        add(fn, tn, 'climb', dist(posOf(fn), foot) + dist(top, posOf(tn)), { rungs: ld.rungs, single: true });
      }
    }
  }
  // breach panels (explosive; links = node ids)
  for (const bp of b.breach_panels ?? []) {
    const [a, c] = bp.links;
    if (g.pos.has(a) && g.pos.has(c)) add(a, c, 'explosive', dist(posOf(a), posOf(c)));
  }
  return g;
}

// ---- costs + Dijkstra (F1 / F2) -----------------------------------------------------------------------------------------------------

/** C(e) of route-model F1 for one profile and `nSquad` operatives. */
export function edgeCost(e: RouteEdge, profile: RouteProfile, costs: RouteCostTable = ROUTE_COSTS, nSquad = costs.n_squad): number {
  const a = costs.actions[e.mode], p = costs.profiles[profile];
  const t1 = e.len_m / costs.v_ref + e.rungs * costs.t_rung + a.t;
  const t = e.single_file ? nSquad * t1 : t1 + (nSquad - 1) * costs.t_follow;
  return t + p.a_n * a.n + p.a_x * a.x + p.a_e * t * e.exposure;
}

/** Walkable = rule 5 edges only (no explosive, no drop); the breach graph allows explosives, never drops. */
export const isWalkableEdge = (e: RouteEdge) => e.mode !== 'explosive' && e.mode !== 'drop';
const isBreachEdge = (e: RouteEdge) => e.mode !== 'drop';

interface Label { c: number; o: number; a: number; path: RouteEdge[] }

/** F2 order: cost, then fewer outside legs, then fewer actions, then edge id order. */
function better(x: Label, y: Label): boolean {
  if (Math.abs(x.c - y.c) > 1e-9) return x.c < y.c;
  if (x.o !== y.o) return x.o < y.o;
  if (x.a !== y.a) return x.a < y.a;
  const n = Math.min(x.path.length, y.path.length);
  for (let i = 0; i < n; i++) if (x.path[i].id !== y.path[i].id) return x.path[i].id < y.path[i].id;
  return x.path.length < y.path.length;
}

/** Single-source Dijkstra from the street over the edges `allow` admits; best label per node. */
export function shortestFromStreet(g: RouteGraph, profile: RouteProfile, allow: (e: RouteEdge) => boolean, costs: RouteCostTable = ROUTE_COSTS, nSquad = costs.n_squad): Map<string, Label> {
  const out = new Map<string, Label>([[STREET, { c: 0, o: 0, a: 0, path: [] }]]);
  const done = new Set<string>();
  const adj = new Map<string, RouteEdge[]>();
  for (const e of g.edges) if (allow(e)) adj.set(e.from, [...(adj.get(e.from) ?? []), e]);
  for (;;) {
    let u: string | null = null;
    for (const [id, lab] of out) if (!done.has(id) && (u === null || better(lab, out.get(u)!))) u = id;
    if (u === null) break;
    done.add(u);
    const lu = out.get(u)!;
    for (const e of adj.get(u) ?? []) {
      if (done.has(e.to)) continue;
      const cand: Label = { c: lu.c + edgeCost(e, profile, costs, nSquad), o: lu.o + (e.outside ? 1 : 0), a: lu.a + (ACTION_MODES.includes(e.mode) ? 1 : 0), path: [...lu.path, e] };
      const cur = out.get(e.to);
      if (!cur || better(cand, cur)) out.set(e.to, cand);
    }
  }
  return out;
}

// ---- key route, entries, D, F4 --------------------------------------------------------------------------------------------------------

export interface ProfileRoute {
  edges: RouteEdge[];
  /** Unrounded C(P). */
  cost: number;
  /** entry(P): the first exterior opening on P (F4). */
  entry?: string;
}

export interface BuildingRoutes {
  building: string;
  /** "Top" (rule 6): the walkable roof if reachable, else the highest storey's room farthest from the street. */
  top: string | null;
  routes: Partial<Record<RouteProfile, ProfileRoute>>;
  /** The balanced key route on the walkable graph, with D (F3) and the cost hash. */
  key_route?: Route;
  /** Stored only when a route with explosive edges is cheaper (rule 6). */
  breach_route?: Route;
  fast_entry?: string;
  quiet_entry?: string;
  /** F4: fast and quiet enter by different exterior openings. */
  choice: boolean;
}

export interface RouteOptions {
  costs?: RouteCostTable;
  nSquad?: number;
  /** The same building with its story removed (and the layout before the story) for D (F3); absent = D 1. */
  baseline?: { layout: GenLayout; building: GenBuilding };
}

const round = (v: number, k: number) => Math.round(v * 10 ** k) / 10 ** k;
const entryOf = (edges: readonly RouteEdge[]) => edges.find((e) => e.entry !== undefined)?.entry;
const toLegs = (edges: readonly RouteEdge[]) => edges.map((e) => ({ from: e.from, to: e.to, mode: e.mode }));

function pickTop(g: RouteGraph, bal: Map<string, Label>): string | null {
  if (g.roof && bal.has(g.roof)) return g.roof;
  const reached = g.rooms.filter((id) => bal.has(id));
  if (!reached.length) return null;
  const hi = Math.max(...reached.map((id) => g.floorOf.get(id) ?? 0));
  return reached
    .filter((id) => (g.floorOf.get(id) ?? 0) === hi)
    .sort((a, c) => bal.get(c)!.c - bal.get(a)!.c || (a < c ? -1 : a > c ? 1 : 0))[0];
}

/** Key route (F2) per profile, fast / quiet entries, F4 and D (F3) for one building of `l`. */
export function computeBuildingRoutes(l: GenLayout, b: GenBuilding, opts: RouteOptions = {}): BuildingRoutes {
  const costs = opts.costs ?? ROUTE_COSTS, n = opts.nSquad ?? costs.n_squad;
  const res: BuildingRoutes = { building: b.id, top: null, routes: {}, choice: false };
  if (!b.enterable) return res;
  const g = buildRouteGraph(l, b, costs);
  const bal = shortestFromStreet(g, 'balanced', isWalkableEdge, costs, n);
  const top = pickTop(g, bal);
  if (!top) return res;
  res.top = top;
  for (const p of ROUTE_PROFILES) {
    const lab = p === 'balanced' ? bal.get(top)! : shortestFromStreet(g, p, isWalkableEdge, costs, n).get(top)!;
    res.routes[p] = { edges: lab.path, cost: lab.c, ...(entryOf(lab.path) ? { entry: entryOf(lab.path) } : {}) };
  }
  const key = res.routes.balanced!;
  let detour = 1;
  if (opts.baseline) {
    const base = computeBuildingRoutes(opts.baseline.layout, opts.baseline.building, { costs, nSquad: n });
    const bc = base.routes.balanced?.cost;
    if (bc && bc > 0) detour = round(key.cost / bc, 3);
  }
  const hash = routeCostHash(costs);
  res.key_route = { legs: toLegs(key.edges), cost: round(key.cost, 2), detour, cost_hash: hash };
  const breach = shortestFromStreet(g, 'balanced', isBreachEdge, costs, n).get(top);
  if (breach && breach.c < key.cost - 1e-6) res.breach_route = { legs: toLegs(breach.path), cost: round(breach.c, 2), detour: round(breach.c / key.cost, 3), cost_hash: hash };
  res.fast_entry = res.routes.fast?.entry;
  res.quiet_entry = res.routes.quiet?.entry;
  res.choice = res.fast_entry !== undefined && res.quiet_entry !== undefined && res.fast_entry !== res.quiet_entry;
  return res;
}

// ---- page summary -------------------------------------------------------------------------------------------------------------------

/** What the «Здания» panel shows per enterable building. */
export interface RouteSummary {
  /** The key route's leg modes, consecutive repeats collapsed (walk -> kick -> walk -> climb). */
  modes: string[];
  cost: number;
  detour: number;
  fast_entry?: string;
  quiet_entry?: string;
  /** F4 value. */
  choice: boolean;
  /** Read from the building's stored key_route (current cost hash) rather than computed live. */
  stored: boolean;
}

const collapse = (modes: readonly string[]) => modes.filter((m, i) => i === 0 || modes[i - 1] !== m);

/** The building's route summary: its stored key_route when the hash is current, else computed live (D 1, no baseline). */
export function routeSummary(l: GenLayout, b: GenBuilding): RouteSummary | undefined {
  if (!b.enterable) return undefined;
  if (b.key_route && b.key_route.cost_hash === routeCostHash()) {
    const choice = b.fast_entry !== undefined && b.quiet_entry !== undefined && b.fast_entry !== b.quiet_entry;
    return { modes: collapse(b.key_route.legs.map((x) => x.mode)), cost: b.key_route.cost, detour: b.key_route.detour, fast_entry: b.fast_entry, quiet_entry: b.quiet_entry, choice, stored: true };
  }
  const r = computeBuildingRoutes(l, b);
  if (!r.key_route) return undefined;
  return { modes: collapse(r.key_route.legs.map((x) => x.mode)), cost: r.key_route.cost, detour: r.key_route.detour, fast_entry: r.fast_entry, quiet_entry: r.quiet_entry, choice: r.choice, stored: false };
}

/** The traversal report with each building's story ids and route summary (the page's «Здания» panel). */
export function withRouteSummaries(l: GenLayout, report: TraversalReport): TraversalReport {
  return {
    ...report,
    buildings: report.buildings.map((r) => {
      const b = l.buildings.find((x) => x.id === r.id);
      if (!b) return r;
      const route = routeSummary(l, b), stories = b.stories?.map((s) => s.id);
      return { ...r, ...(stories?.length ? { stories } : {}), ...(route ? { route } : {}) };
    }),
  };
}
