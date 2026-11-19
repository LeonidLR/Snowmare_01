// The one building graph (docs/design/levelgen/S2_ROOMS_PLAN.md section 7 item 1, pre-S2 refactor #1): the passability rules
// of a building - opening states, ground window entries, blocked / down_only flights, external stairs, fire escapes and their
// balconies, snow ramps, roof hatches, layout ladders, floor / roof holes (drops) and breach panels (explosive) - written once.
// Consumers: the route model (buildingRoutes.ts: Dijkstra with costs on top), the tactical reachability (tacticalEvaluate.ts
// checkBuildingReachability: BFS) and the traversal validator (buildingTraversal.ts: rooms, roof, storeys via levelArcs).
// Nodes: street, approach cells, rooms, balconies, snow-ramp tops, roof; typed edges (mode, one-way pairs, length, rungs).
// Units: layout cm; positions in the building's own (unrotated) frame. Pure and deterministic (edge ids in creation order).

import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dist } from '../movement/geometry.ts';
import { toBuildingFrame } from './rotation.ts';
import { classifyOpening, effectiveOpeningState } from './openingStates.ts';
import { escapeLadderIds, usableEscapes, usableSnowRamps } from './fireEscapes.ts';
import type { DoorState, GenBuilding, GenLayout, GenRoom, WallOpening, WindowState } from './schema.ts';
import { isMantleable, LEVEL_HEIGHT, ladderForLevels, OPENING_DEFAULTS, unifyOpenings } from './schema.ts';

// ---- cost table (later building_route_costs.json; the graph reads its geometry part) -----------------------------------------------

/** Edge modes (route-model rule 3). */
export type EdgeMode = 'walk' | 'open' | 'kick' | 'shotgun' | 'vault' | 'shatter' | 'climb' | 'explosive' | 'drop';
export type RouteProfile = 'balanced' | 'quiet' | 'fast';

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

// ---- geometry helpers -----------------------------------------------------------------------------------------------------------------

type Box = [Vec2, Vec2];

/** Segment a-b crosses (or lies inside) the box shrunk by `inset`. */
export function segHitsBox(a: Vec2, b: Vec2, [lo, hi]: Box, inset = 2): boolean {
  const x0 = lo[0] + inset, y0 = lo[1] + inset, x1 = hi[0] - inset, y1 = hi[1] - inset;
  let t0 = 0, t1 = 1;
  const d: Vec2 = [b[0] - a[0], b[1] - a[1]];
  for (const [p, q] of [[-d[0], a[0] - x0], [d[0], x1 - a[0]], [-d[1], a[1] - y0], [d[1], y1 - a[1]]] as const) {
    if (Math.abs(p) < 1e-9) { if (q < 0) return false; continue; }
    const t = q / p;
    if (p < 0) { if (t > t1) return false; if (t > t0) t0 = t; } else { if (t < t0) return false; if (t < t1) t1 = t; }
  }
  return t0 <= t1;
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

// ---- entries --------------------------------------------------------------------------------------------------------------------------

/** An exterior ground-floor window the operatives can vault through (sill in the mantle band); its state is checked separately. */
export function isVaultableGroundWindow(l: Pick<GenLayout, 'walls'>, b: Pick<GenBuilding, 'partitions'>, op: WallOpening): boolean {
  if (op.kind !== 'window' || (op.floor ?? 0) !== 0) return false;
  if (!isMantleable(op.sill_height ?? OPENING_DEFAULTS.window.sill)) return false;
  return classifyOpening(l, b, op).exterior;
}

/** An exterior entry of a building with its effective state (opening-states.md). */
export interface ExteriorEntry {
  opening: WallOpening;
  state: ReturnType<typeof effectiveOpeningState>;
}

/**
 * The exterior entries of a building (BUILDINGS_TECH_SPEC.md 2.1): its exterior ground-floor doors (b.openings, else the legacy
 * b.doors) and its vaultable exterior ground-floor windows, each with its effective state (blocked ones included).
 */
export function exteriorEntries(l: GenLayout, b: GenBuilding): ExteriorEntry[] {
  const out: ExteriorEntry[] = [];
  for (const op of unifyOpenings(b)) {
    const door = op.kind === 'door' && (op.floor ?? 0) === 0;
    if (!door && !isVaultableGroundWindow(l, b, op)) continue;
    out.push({ opening: op, state: effectiveOpeningState(l, b, op) });
  }
  // legacy b.doors next to a non-empty b.openings (unifyOpenings reads only the latter): still doors, as before
  // (a door already in b.openings at the same spot - e.g. materialised by a story brick - is not counted twice)
  if (b.openings?.length) {
    for (const d of b.doors ?? []) {
      if (d.floor !== 0 || b.openings.some((o) => o.kind === 'door' && (o.floor ?? 0) === 0 && o.pos[0] === d.pos[0] && o.pos[1] === d.pos[1])) continue;
      const op: WallOpening = { kind: 'door', pos: d.pos, floor: d.floor, wall: d.wall };
      out.push({ opening: op, state: effectiveOpeningState(l, b, op) });
    }
  }
  return out;
}

// ---- graph ----------------------------------------------------------------------------------------------------------------------------

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

export interface BuildingGraph {
  building: string;
  nodes: string[];
  edges: RouteEdge[];
  /** Node -> building-frame position (cm). */
  pos: Map<string, Vec2>;
  /** Node -> storey (rooms, balconies, roof = floors). */
  floorOf: Map<string, number>;
  rooms: string[];
  roof: string | null;
  /**
   * Storey -> storey climbs (no drops), independent of rooms: every unblocked flight (down_only reversed), layout ladder,
   * fire-escape leg, passable snow ramp and hatch with its ladder - the validator's "levels" check.
   */
  levelArcs: [number, number][];
}
/** The route model's name of the graph. */
export type RouteGraph = BuildingGraph;

export const STREET = 'street';
export const ROOF = 'roof';

export interface GraphOptions {
  costs?: RouteCostTable;
  /** The edge modes an opening gives (default: openingModes of its effective state - the route model). */
  modesOf?: (op: WallOpening) => EdgeMode[];
  /**
   * The tactical reachability variant (checkBuildingReachability). Keeps that graph's legacy differences from the route graph
   * until step R2: entries = ground doors of unifyOpenings (no legacy b.doors), vaultable ground windows and snow ramps only with
   * `windowEntries`; balcony doors also join rooms; a door-id group ignores storeys and drops on any blocked opening of that id;
   * a ladder from 'cut' reaches only the roof and none starts on the roof; holes are two-way; a non-enterable building still
   * links the ladders onto its walkable roof.
   */
  reach?: { windowEntries: boolean };
}

const centreOf = (rm: GenRoom): Vec2 => [(rm.bounds.min[0] + rm.bounds.max[0]) / 2, (rm.bounds.min[1] + rm.bounds.max[1]) / 2];
const inBounds = (p: Vec2, rm: GenRoom, tol = 60) =>
  p[0] >= rm.bounds.min[0] - tol && p[0] <= rm.bounds.max[0] + tol && p[1] >= rm.bounds.min[1] - tol && p[1] <= rm.bounds.max[1] + tol;

/** The building's rooms (b.rooms, else one virtual room per walkable storey). */
function roomsOf(b: GenBuilding): GenRoom[] {
  if (b.rooms?.length) return b.rooms;
  return Array.from({ length: b.hollow ? 1 : b.floors }, (_, f) => ({ id: `${b.id}_floor_${f}`, floor: f, bounds: { min: b.footprint[0], max: b.footprint[1] } }));
}

/** Build the graph of one building (route-model rules 1-4; the reachability variant with `opts.reach`). */
export function buildBuildingGraph(l: GenLayout, b: GenBuilding, opts: GraphOptions = {}): BuildingGraph {
  const costs = opts.costs ?? ROUTE_COSTS, R = opts.reach;
  const modesOf = opts.modesOf ?? ((op: WallOpening) => openingModes(op.kind, effectiveOpeningState(l, b, op), costs));
  const g: BuildingGraph = { building: b.id, nodes: [], edges: [], pos: new Map(), floorOf: new Map(), rooms: [], roof: null, levelArcs: [] };
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
  const posOf = (id: string) => g.pos.get(id)!;
  /** An approach node with its outside leg from the street (ground exposure). */
  const approach = (id: string, p: Vec2) => {
    if (g.pos.has(id)) return id;
    node(id, p, 0);
    add(STREET, id, 'walk', outsideLegCm(b, street, p, costs.approach_cm), { exposure: costs.exposure.ground, outside: true });
    return id;
  };
  const exAbove = costs.exposure.above_ground;
  const rooms = b.enterable ? roomsOf(b) : [];
  const roomsAt = (floor: number, p: Vec2, fallbackSingle = true): GenRoom[] => {
    const on = rooms.filter((rm) => rm.floor === floor);
    const hits = on.filter((rm) => inBounds(p, rm));
    return hits.length ? hits : fallbackSingle && on.length === 1 ? on : [];
  };
  const roofNode = () => { if (b.roof_walkable) { node(ROOF, centre, b.floors); g.roof = ROOF; } };

  // layout ladders of this building (a fire escape's ladders are its chain)
  const ladders = () => {
    const escLadders = escapeLadderIds(b);
    const storeyOf = (layer: string): number | null => {
      const m = /^floor:([^:]+):(\d+)$/.exec(layer);
      return m && m[1] === b.id ? Number(m[2]) : null;
    };
    const levelOf = (layer: string) => (layer === 'ground' || layer === 'cut' ? 0 : layer === `roof:${b.id}` ? b.floors : storeyOf(layer));
    const levelNodes = (layer: string, p: Vec2): string[] => {
      if (layer === `roof:${b.id}`) return b.roof_walkable ? [ROOF] : [];
      const f = storeyOf(layer);
      return f === null ? [] : roomsAt(f, p).map((rm) => rm.id);
    };
    for (const ld of l.ladders ?? []) {
      if (escLadders.has(ld.id)) continue;
      const a = levelOf(ld.from), c = levelOf(ld.to);
      if (a !== null && c !== null) g.levelArcs.push([a, c]);
      const foot = F(ld.foot), top = F(ld.top);
      const tos = levelNodes(ld.to, top);
      if (!tos.length) continue;
      if (ld.from === 'ground' || (ld.from === 'cut' && (!R || tos[0] === ROOF))) {
        const app = approach(`app:${ld.id}`, foot);
        for (const tn of tos) add(app, tn, 'climb', dist(top, posOf(tn)), { rungs: ld.rungs, single: true, exposure: exAbove, entry: ld.id });
      } else {
        for (const fn of R && ld.from === `roof:${b.id}` ? [] : levelNodes(ld.from, foot)) for (const tn of tos) {
          add(fn, tn, 'climb', dist(posOf(fn), foot) + dist(top, posOf(tn)), { rungs: ld.rungs, single: true });
        }
      }
    }
  };
  if (!b.enterable) {
    if (R) { roofNode(); ladders(); }
    return g;
  }
  for (const rm of rooms) { node(rm.id, centreOf(rm), rm.floor); g.rooms.push(rm.id); }
  roofNode();
  const roomsOfOpening = (op: WallOpening, floor: number): GenRoom[] =>
    rooms.filter((rm) => rm.floor === floor && ((op.id && rm.door_ids?.includes(op.id)) || inBounds(F(op.pos), rm)));
  const ops = unifyOpenings(b);
  const refOf = (op: WallOpening) => {
    const i = ops.indexOf(op);
    return op.id ?? (i >= 0 ? `openings[${i}]` : `door@${op.pos[0]},${op.pos[1]}`);
  };

  // entries: ground doors + vaultable ground windows (exteriorEntries; states honoured through modesOf)
  const entries = R
    ? ops.filter((op) => op.floor === 0 && (op.kind === 'door' || (R.windowEntries && isVaultableGroundWindow(l, b, op))))
    : exteriorEntries(l, b).map((e) => e.opening);
  for (const op of entries) {
    const modes = modesOf(op);
    if (!modes.length) continue;
    const q = F(op.pos), n = facadeNormal(b, q), ref = refOf(op);
    const app = approach(`app:${ref}`, [q[0] + n[0] * costs.approach_cm, q[1] + n[1] * costs.approach_cm]);
    const single = op.kind === 'window' || (op.width ?? OPENING_DEFAULTS.door.width) < OPENING_DEFAULTS.door.width;
    for (const rm of roomsOfOpening(op, 0)) for (const m of modes) add(app, rm.id, m, dist(posOf(app), q) + dist(q, posOf(rm.id)), { single, entry: ref });
  }
  // snow ramps (story 10): ground foot -> ramp top at the window (exposed) -> the room behind the window
  if (!R || R.windowEntries) {
    for (const { ramp, opening: op, floor } of usableSnowRamps(b)) {
      const modes = modesOf(op);
      if (!modes.length) continue;
      g.levelArcs.push([0, floor]);
      const q = F(op.pos), foot = ramp.from as Vec2;
      const app = approach(`app:ramp:${ramp.id}`, foot);
      const top = `ramp:${ramp.id}`;
      node(top, q, floor);
      add(app, top, 'walk', Math.hypot(dist(foot, q), floor * LEVEL_HEIGHT), { exposure: exAbove });
      for (const rm of roomsAt(floor, q)) for (const m of modes) add(top, rm.id, m, dist(q, posOf(rm.id)), { single: true, entry: refOf(op) });
    }
  }
  // doors on upper storeys joining rooms (the legacy rule) and interior partition doors
  const roomLink = (op: WallOpening, floor: number) => {
    const modes = modesOf(op);
    const hits = roomsOfOpening(op, floor), q = F(op.pos);
    for (let i = 1; i < hits.length; i++) for (const m of modes) add(hits[0].id, hits[i].id, m, dist(posOf(hits[0].id), q) + dist(q, posOf(hits[i].id)));
  };
  const balconyDoors = new Set((b.balconies ?? []).map((x) => x.door_id));
  for (const op of ops) if (op.kind === 'door' && (R ? op.floor !== 0 : (op.floor ?? 0) > 0 && !(op.id && balconyDoors.has(op.id)))) roomLink(op, op.floor);
  for (const pt of b.partitions ?? []) for (const op of pt.openings ?? []) if (op.kind === 'door') roomLink(op, pt.floor);
  // rooms sharing a door id (hand-made room graphs without partition records)
  const allOps = [...ops, ...(b.partitions ?? []).flatMap((pt) => pt.openings ?? [])];
  const byDoor = new Map<string, GenRoom[]>();
  for (const rm of rooms) for (const did of rm.door_ids ?? []) byDoor.set(did, [...(byDoor.get(did) ?? []), rm]);
  for (const [did, group] of byDoor) {
    const op = allOps.find((o) => o.id === did);
    const modes: EdgeMode[] = R ? (allOps.some((o) => o.id === did && !modesOf(o).length) ? [] : ['walk']) : op ? modesOf(op) : ['walk'];
    for (let i = 1; i < group.length; i++) {
      if (!R && group[i].floor !== group[0].floor) continue;
      for (const m of modes) add(group[0].id, group[i].id, m, dist(posOf(group[0].id), posOf(group[i].id)));
    }
  }
  // flights (blocked = no edge; down_only = top -> foot only); external stairs are entries from the street
  for (const st of b.stairs ?? []) {
    if (st.blocked) continue; // stories 4 / 13: the flight stays but gives no edge
    const footLevel = st.kind === 'external' ? 0 : st.from;
    g.levelArcs.push(st.down_only ? [st.to, footLevel] : [footLevel, st.to]);
    const pos = F(st.pos), top = F(st.top), mid = st.mid ? F(st.mid) : null;
    const rise = (st.to - footLevel) * LEVEL_HEIGHT;
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
    const escLadders = (ch.escape.ladder_ids ?? []).map((id) => (l.ladders ?? []).find((ld) => ld.id === id));
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
      g.levelArcs.push(ch.downOnly ? [hi.floor, lo.floor] : [lo.floor, hi.floor]);
      const ld = ch.escape.kind === 'ladder' ? escLadders[ch.ground ? i : i + 1] : undefined;
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
      for (const rm of behind) for (const m of modesOf(dr)) add(bid, rm.id, m, dist(posOf(bid), q) + dist(q, posOf(rm.id)), { entry: refOf(dr) });
    }
  }
  // roof hatches (floor -> roof, a ladder)
  if (b.roof_walkable) {
    for (const ht of b.hatches ?? []) {
      const q = F(ht.pos), ld = (l.ladders ?? []).find((x) => x.id === ht.ladder_id);
      if (ld) g.levelArcs.push([ht.floor, b.floors]);
      for (const rm of roomsAt(ht.floor, q)) add(rm.id, ROOF, 'climb', dist(posOf(rm.id), q), { rungs: ld?.rungs ?? ladderForLevels(b.floors - ht.floor).rungs, single: true });
    }
  }
  // drops (one way down): roof holes onto the top storey, floor holes one storey down
  for (const rh of b.roof_holes ?? []) {
    if (!b.roof_walkable || b.floors < 1) continue;
    const q = F(rh);
    for (const rm of roomsAt(b.hollow ? 0 : b.floors - 1, q, false)) add(ROOF, rm.id, 'drop', dist(centre, q) + dist(q, posOf(rm.id)), { oneWay: !R });
  }
  for (const fh of b.floor_holes ?? []) {
    const q = F(fh.pos);
    for (const u of roomsAt(fh.floor, q, false)) for (const lo of roomsAt(Math.max(0, fh.floor - 1), q, false)) {
      add(u.id, lo.id, 'drop', dist(posOf(u.id), q) + dist(q, posOf(lo.id)), { oneWay: !R });
    }
  }
  ladders();
  // breach panels (explosive; links = node ids)
  for (const bp of b.breach_panels ?? []) {
    const [a, c] = bp.links;
    if (g.pos.has(a) && g.pos.has(c)) add(a, c, 'explosive', dist(posOf(a), posOf(c)));
  }
  return g;
}

// ---- reachability ---------------------------------------------------------------------------------------------------------------------

/** The nodes reached from `start` over the edges `allow` admits (BFS). */
export function reachableFrom(g: BuildingGraph, allow: (e: RouteEdge) => boolean, start = STREET): Set<string> {
  const adj = new Map<string, string[]>();
  for (const e of g.edges) if (allow(e)) { const a = adj.get(e.from); if (a) a.push(e.to); else adj.set(e.from, [e.to]); }
  const seen = new Set<string>([start]);
  for (const queue = [start]; queue.length;) for (const v of adj.get(queue.shift()!) ?? []) if (!seen.has(v)) { seen.add(v); queue.push(v); }
  return seen;
}

/** Rooms and roof reached from the street without an explosive (the tactical / validator room check). */
export function roomReachability(g: BuildingGraph): { roomsTotal: number; roomsReachable: number; unreachableRooms: string[]; roofReachable: boolean } {
  const seen = reachableFrom(g, (e) => e.mode !== 'explosive');
  const unreachableRooms = g.rooms.filter((id) => !seen.has(id));
  return { roomsTotal: g.rooms.length, roomsReachable: g.rooms.length - unreachableRooms.length, unreachableRooms, roofReachable: g.roof === null || seen.has(g.roof) };
}
