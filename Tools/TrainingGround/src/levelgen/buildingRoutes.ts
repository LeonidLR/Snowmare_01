// Building route model (building generator S1 step 3c; docs/design/levelgen/gdd/building-route-model.md rules 1-10, F1-F4;
// opening-states.md F1 action costs). Costs and Dijkstra on top of the one building graph (buildingGraph.ts: street node,
// approach cells + outside legs, entries by state, rooms, balconies, flights, ladders / hatches, fire escapes, drops and explosive
// edges). Dijkstra per profile on the WALKABLE graph (no explosive, no drop): key route (balanced), fast / quiet entries, F4
// choice, D against a baseline (the building with its story removed). A TS generation-time proxy; the engine navmesh is the
// runtime truth (ADR-1). Pure and deterministic. Units: layout cm; costs in seconds-equivalent.

import { fnv1a } from './props/rng.ts';
import type { TraversalReport } from './buildingTraversal.ts';
import type { EdgeMode, RouteCostTable, RouteEdge, RouteGraph, RouteProfile } from './buildingGraph.ts';
import { buildBuildingGraph, ROUTE_COSTS, STREET } from './buildingGraph.ts';
import type { GenBuilding, GenLayout, Route } from './schema.ts';

export type { EdgeMode, RouteCostTable, RouteEdge, RouteGraph, RouteProfile } from './buildingGraph.ts';
export { openingModes, outsideLegCm, ROOF, ROUTE_COSTS, STREET, streetPoint } from './buildingGraph.ts';
export const ROUTE_PROFILES: readonly RouteProfile[] = ['balanced', 'quiet', 'fast'];

/** The cost-table hash stored with every route (rule 10; a route with another hash is stale). */
export function routeCostHash(costs: RouteCostTable = ROUTE_COSTS): string {
  return fnv1a(JSON.stringify(costs)).toString(16).padStart(8, '0');
}

const ACTION_MODES: readonly EdgeMode[] = ['open', 'kick', 'shotgun', 'vault', 'shatter', 'explosive'];

/** Build the weighted route graph of one enterable building (rules 1-4; buildingGraph.ts). */
export function buildRouteGraph(l: GenLayout, b: GenBuilding, costs: RouteCostTable = ROUTE_COSTS): RouteGraph {
  return buildBuildingGraph(l, b, { costs });
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
