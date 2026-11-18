// Training Ground / level generator - building TACTICAL ROLES (building generator S1 step 2; docs/design/levelgen/gdd/building-stories.md
// section 1 "Tactical role first", BUILDINGS_TECH_SPEC.md section 1 step 0). Every ENTERABLE building gets one role, the first that
// applies in the order objective > garrison > overwatch > transit > background:
//   - objective: a mission goal (layout `objectives.goals[].building`) names the building;
//   - garrison: the building holds PMC defender places (`placesByBuilding`, see below) or its footprint is within R_garrison of an
//     objective (an objective building's footprint or a goal point);
//   - overwatch: at most N_watch buildings with >= 2 storeys and a walkable roof, within R_watch of the mandatory route (R_main)
//     polyline or of an objective; the tallest first, ties by id;
//   - transit: the footprint is within 15 m of the mandatory route polyline;
//   - background: the rest.
// Non-enterable buildings get no role. Distances are 2D on the plan (layout cm), footprints rotated (rotation.ts footprintPoly).
// PMC defender places are not part of the layout on the TS side: the tactical file (tactical/*.json) only lists chokepoints /
// windows / roofs, so the caller passes `placesByBuilding` (countPlacesByBuilding over any `{ building }` list) when it has them.
// Pure; deterministic (no randomness, ties by id, independent of the building order). Not called by the generator pipeline yet.

import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dist } from '../movement/geometry.ts';
import { footprintPoly } from './rotation.ts';
import type { BuildingRole, GenBuilding, GenLayout } from './schema.ts';

/** The role knobs (building-stories.md Tuning Knobs; later building_stories.json). Distances in metres. */
export const BUILDING_ROLE_KNOBS = {
  /** A building within this of an objective is a garrison (R_garrison, range 10-80). */
  garrison_radius_m: 30,
  /** Overwatch candidates lie within this of R_main or an objective (R_watch, range 30-120). */
  watch_radius_m: 60,
  /** At most this many overwatch buildings per map (N_watch, range 1-6). */
  watch_max: 3,
  /** An overwatch building has at least this many storeys (and a walkable roof). */
  watch_min_storeys: 2,
  /** A building within this of R_main is transit. */
  transit_radius_m: 15,
} as const;

export type BuildingRoleKnobs = { -readonly [K in keyof typeof BUILDING_ROLE_KNOBS]: number };

export interface AssignRolesOptions {
  knobs?: Partial<BuildingRoleKnobs>;
  /** PMC defender places per building id (count); a building with >= 1 is a garrison. Absent = none known. */
  placesByBuilding?: Readonly<Record<string, number>>;
}

/** Count `{ building }` entries (PMC defender places, tactical `places[]`) per building id. */
export function countPlacesByBuilding(places: readonly { building?: string }[]): Record<string, number> {
  const out: Record<string, number> = {};
  for (const p of places) if (p.building) out[p.building] = (out[p.building] ?? 0) + 1;
  return out;
}

// ---- geometry (cm) ----------------------------------------------------------------------------------------------------------------

function pointInPoly(p: Vec2, poly: readonly Vec2[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, yi] = poly[i], [xj, yj] = poly[j];
    if (yi > p[1] !== yj > p[1] && p[0] < ((xj - xi) * (p[1] - yi)) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

const pointSegDist = (p: Vec2, a: Vec2, b: Vec2): number => dist(p, closestOnSegment(p, a, b).point);

function segmentsCross(a: Vec2, b: Vec2, c: Vec2, d: Vec2): boolean {
  const o = (p: Vec2, q: Vec2, r: Vec2) => Math.sign((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]));
  return o(a, b, c) !== o(a, b, d) && o(c, d, a) !== o(c, d, b);
}

function segSegDist(a: Vec2, b: Vec2, c: Vec2, d: Vec2): number {
  if (segmentsCross(a, b, c, d)) return 0;
  return Math.min(pointSegDist(a, c, d), pointSegDist(b, c, d), pointSegDist(c, a, b), pointSegDist(d, a, b));
}

const edgesOf = (poly: readonly Vec2[]): [Vec2, Vec2][] => poly.map((p, i) => [p, poly[(i + 1) % poly.length]]);

/** Distance from a closed polygon (area) to a point, cm; 0 inside. */
function polyToPoint(poly: readonly Vec2[], p: Vec2): number {
  if (pointInPoly(p, poly)) return 0;
  return Math.min(...edgesOf(poly).map(([a, b]) => pointSegDist(p, a, b)));
}

/** Distance from a closed polygon (area) to an open polyline, cm; 0 when they touch / overlap. */
function polyToPolyline(poly: readonly Vec2[], line: readonly Vec2[]): number {
  if (!line.length) return Infinity;
  if (line.length === 1) return polyToPoint(poly, line[0]);
  if (line.some((p) => pointInPoly(p, poly))) return 0;
  let best = Infinity;
  for (let i = 0; i + 1 < line.length; i++) for (const [a, b] of edgesOf(poly)) best = Math.min(best, segSegDist(line[i], line[i + 1], a, b));
  return best;
}

/** Distance between two closed polygons (areas), cm; 0 when they touch / overlap. */
function polyToPoly(p: readonly Vec2[], q: readonly Vec2[]): number {
  if (p.some((x) => pointInPoly(x, q)) || q.some((x) => pointInPoly(x, p))) return 0;
  let best = Infinity;
  for (const [a, b] of edgesOf(p)) for (const [c, d] of edgesOf(q)) best = Math.min(best, segSegDist(a, b, c, d));
  return best;
}

// ---- roles ------------------------------------------------------------------------------------------------------------------------

const byId = (a: GenBuilding, b: GenBuilding) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);

/**
 * The tactical role of every enterable building (see the file header), keyed by building id in the layout's building order.
 * Non-enterable buildings are absent.
 */
export function assignRoles(layout: GenLayout, opts: AssignRolesOptions = {}): Record<string, BuildingRole> {
  const k: BuildingRoleKnobs = { ...BUILDING_ROLE_KNOBS, ...opts.knobs };
  const places = opts.placesByBuilding ?? {};
  const polys = new Map(layout.buildings.map((b) => [b.id, footprintPoly(b)] as const));
  const goals = layout.objectives?.goals ?? [];
  const objectiveIds = new Set(goals.map((g) => g.building).filter((x): x is string => !!x));
  const objectivePolys = layout.buildings.filter((b) => objectiveIds.has(b.id)).map((b) => polys.get(b.id)!);
  const goalPoints = goals.map((g) => g.pos as Vec2);
  const route = layout.routes.find((r) => r.mandatory)?.points ?? [];

  const toObjective = (b: GenBuilding): number => {
    const poly = polys.get(b.id)!;
    return Math.min(Infinity, ...objectivePolys.map((q) => polyToPoly(poly, q)), ...goalPoints.map((p) => polyToPoint(poly, p)));
  };
  const toRoute = (b: GenBuilding): number => (route.length ? polyToPolyline(polys.get(b.id)!, route) : Infinity);

  const roles = new Map<string, BuildingRole>();
  const enterable = layout.buildings.filter((b) => b.enterable).sort(byId);
  for (const b of enterable) {
    if (objectiveIds.has(b.id)) roles.set(b.id, 'objective');
    else if ((places[b.id] ?? 0) > 0 || toObjective(b) <= k.garrison_radius_m * 100) roles.set(b.id, 'garrison');
  }
  const watchCm = k.watch_radius_m * 100;
  const watch = enterable
    .filter((b) => !roles.has(b.id) && b.roof_walkable && b.floors >= k.watch_min_storeys && Math.min(toRoute(b), toObjective(b)) <= watchCm)
    .sort((a, b) => b.height - a.height || byId(a, b))
    .slice(0, Math.max(0, k.watch_max));
  for (const b of watch) roles.set(b.id, 'overwatch');
  for (const b of enterable) if (!roles.has(b.id)) roles.set(b.id, toRoute(b) <= k.transit_radius_m * 100 ? 'transit' : 'background');

  const out: Record<string, BuildingRole> = {};
  for (const b of layout.buildings) { const r = roles.get(b.id); if (r) out[b.id] = r; }
  return out;
}

/**
 * The layout with `role` written onto every enterable building (assignRoles) and dropped from the others; nothing else changes
 * and the input is not mutated.
 */
export function withRoles(layout: GenLayout, opts: AssignRolesOptions = {}): GenLayout {
  const roles = assignRoles(layout, opts);
  return {
    ...layout,
    buildings: layout.buildings.map((b) => {
      const role = roles[b.id];
      if (role) return { ...b, role };
      if (b.role === undefined) return b;
      const { role: _drop, ...rest } = b;
      return rest;
    }),
  };
}
