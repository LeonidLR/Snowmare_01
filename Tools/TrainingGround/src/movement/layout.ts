// Training Ground / movement: layout JSON (schema codex.formation_layout/1, FORMATION_CONTRACT.md 1). One file feeds the
// simulator and the Unreal map generator: walls are straight segments (a gap = two segments, a corner = two segments sharing
// an end point), zones are labels for the checks, routes are the leader's click paths.

import type { Segment, Vec2 } from './geometry.ts';
import { pointInPoly } from './geometry.ts';

export const LAYOUT_SCHEMA = 'codex.formation_layout/1';

export const ZONE_KINDS = ['open', 'corridor', 'wall_run', 'gap_run', 'corner', 'wall_end'] as const;
export type ZoneKind = (typeof ZONE_KINDS)[number];

export const FORMATIONS = ['diamond', 'wedge', 'column', 'line', 'echelon_left', 'echelon_right', 'wall', 'hedgehog', 'blizzard'] as const;
export type Formation = (typeof FORMATIONS)[number];

export interface LayoutWall {
  id: string;
  a: Vec2;
  b: Vec2;
  height: number;
  thickness: number;
}

export interface LayoutZone {
  id: string;
  kind: ZoneKind;
  poly: Vec2[];
}

export interface RouteExpect {
  zone: string;
  formation: Formation;
}

/** v2.1: a threat known to the squad on this route (an enemy seen, or heard / last known position) from since_s on. */
export interface RouteThreat {
  pos: Vec2;
  /** Sim time of the contact, s (0 = known from the start); it counts while younger than bypass.threat_memory_s. */
  since_s: number;
}

/** v2.1, bot check 8: the side the squad must pass this wall on (the wall on its left / right as it goes by). */
export interface RouteBypassExpect {
  wall: string;
  wall_on: 'left' | 'right';
}

export interface LayoutRoute {
  id: string;
  pace: 'walk' | 'run';
  points: Vec2[];
  expect?: RouteExpect[];
  threats?: RouteThreat[];
  expect_bypass?: RouteBypassExpect[];
  /**
   * v2.2 wall slide: after the route ends at a wall (the wall layout), the leader's next destinations along that wall, one
   * after the other (the squad halts in the wall layout between them). Bot check 11.
   */
  slide?: Vec2[];
}

export const PROP_KINDS = ['heat_source'] as const;
export type PropKind = (typeof PROP_KINDS)[number];

/** Optional set dressing the map builder places (not read by the rules): a heat source for the hedgehog. */
export interface LayoutProp {
  id: string;
  kind: PropKind;
  pos: Vec2;
}

export interface Layout {
  schema: string;
  name: string;
  bounds: { min: Vec2; max: Vec2 };
  ground_z: number;
  spawn: { pos: Vec2; yaw: number };
  walls: LayoutWall[];
  zones: LayoutZone[];
  routes: LayoutRoute[];
  props?: LayoutProp[];
}

const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isVec = (v: unknown): v is Vec2 => Array.isArray(v) && v.length === 2 && isNum(v[0]) && isNum(v[1]);

/** Schema problems of a layout (empty = valid). Messages name the element ("walls[3] (W4): ..."). */
export function validateLayout(data: unknown): string[] {
  const e: string[] = [];
  if (!data || typeof data !== 'object') return ['expected a JSON object'];
  const l = data as Partial<Layout>;
  if (l.schema !== LAYOUT_SCHEMA) e.push(`schema must be "${LAYOUT_SCHEMA}"`);
  if (typeof l.name !== 'string' || !l.name) e.push('name: expected a non-empty string');
  if (!l.bounds || !isVec(l.bounds.min) || !isVec(l.bounds.max)) e.push('bounds: expected { min: [x, y], max: [x, y] }');
  else if (l.bounds.min[0] >= l.bounds.max[0] || l.bounds.min[1] >= l.bounds.max[1]) e.push('bounds: min must be below max');
  if (!isNum(l.ground_z)) e.push('ground_z: expected a number');
  if (!l.spawn || !isVec(l.spawn.pos) || !isNum(l.spawn.yaw)) e.push('spawn: expected { pos: [x, y], yaw }');
  const inBounds = (p: Vec2) => !l.bounds || !isVec(l.bounds.min) || !isVec(l.bounds.max) ||
    (p[0] >= l.bounds.min[0] && p[0] <= l.bounds.max[0] && p[1] >= l.bounds.min[1] && p[1] <= l.bounds.max[1]);

  const ids = new Set<string>();
  const unique = (where: string, id: unknown) => {
    if (typeof id !== 'string' || !id) { e.push(`${where}: id must be a non-empty string`); return; }
    if (ids.has(id)) e.push(`${where}: duplicate id "${id}"`);
    ids.add(id);
  };

  if (!Array.isArray(l.walls)) e.push('walls: expected an array');
  else l.walls.forEach((w, i) => {
    const where = `walls[${i}]${w && typeof w.id === 'string' ? ` (${w.id})` : ''}`;
    unique(where, w?.id);
    if (!isVec(w?.a) || !isVec(w?.b)) { e.push(`${where}: a / b must be [x, y]`); return; }
    if (w.a[0] === w.b[0] && w.a[1] === w.b[1]) e.push(`${where}: zero length`);
    if (!inBounds(w.a) || !inBounds(w.b)) e.push(`${where}: outside bounds`);
    if (!isNum(w.height) || w.height <= 0) e.push(`${where}: height must be > 0`);
    if (!isNum(w.thickness) || w.thickness <= 0) e.push(`${where}: thickness must be > 0`);
  });

  const zoneIds = new Set<string>();
  if (!Array.isArray(l.zones)) e.push('zones: expected an array');
  else l.zones.forEach((z, i) => {
    const where = `zones[${i}]${z && typeof z.id === 'string' ? ` (${z.id})` : ''}`;
    unique(where, z?.id);
    if (typeof z?.id === 'string') zoneIds.add(z.id);
    if (!ZONE_KINDS.includes(z?.kind as ZoneKind)) e.push(`${where}: kind must be one of ${ZONE_KINDS.join(' | ')}`);
    if (!Array.isArray(z?.poly) || z.poly.length < 3 || !z.poly.every(isVec)) e.push(`${where}: poly needs at least 3 [x, y] points`);
  });

  if (l.props !== undefined) {
    if (!Array.isArray(l.props)) e.push('props: expected an array');
    else l.props.forEach((p, i) => {
      const where = `props[${i}]${p && typeof p.id === 'string' ? ` (${p.id})` : ''}`;
      unique(where, p?.id);
      if (!PROP_KINDS.includes(p?.kind as PropKind)) e.push(`${where}: kind must be one of ${PROP_KINDS.join(' | ')}`);
      if (!isVec(p?.pos)) e.push(`${where}: pos must be [x, y]`);
      else if (!inBounds(p.pos)) e.push(`${where}: outside bounds`);
    });
  }

  if (!Array.isArray(l.routes)) e.push('routes: expected an array');
  else l.routes.forEach((r, i) => {
    const where = `routes[${i}]${r && typeof r.id === 'string' ? ` (${r.id})` : ''}`;
    unique(where, r?.id);
    if (r?.pace !== 'walk' && r?.pace !== 'run') e.push(`${where}: pace must be walk | run`);
    if (!Array.isArray(r?.points) || r.points.length < 2 || !r.points.every(isVec)) e.push(`${where}: points needs at least 2 [x, y]`);
    else if (!r.points.every(inBounds)) e.push(`${where}: a point is outside bounds`);
    if (r?.expect !== undefined) {
      if (!Array.isArray(r.expect)) e.push(`${where}: expect must be an array`);
      else r.expect.forEach((x, j) => {
        if (!zoneIds.has(x?.zone)) e.push(`${where}.expect[${j}]: unknown zone "${x?.zone}"`);
        if (!FORMATIONS.includes(x?.formation as Formation)) e.push(`${where}.expect[${j}]: formation must be one of ${FORMATIONS.join(' | ')}`);
      });
    }
    if (r?.threats !== undefined) {
      if (!Array.isArray(r.threats)) e.push(`${where}: threats must be an array`);
      else r.threats.forEach((t, j) => {
        if (!isVec(t?.pos)) e.push(`${where}.threats[${j}]: pos must be [x, y]`);
        else if (!inBounds(t.pos)) e.push(`${where}.threats[${j}]: outside bounds`);
        if (!isNum(t?.since_s) || t.since_s < 0) e.push(`${where}.threats[${j}]: since_s must be a number >= 0`);
      });
    }
    if (r?.slide !== undefined) {
      if (!Array.isArray(r.slide) || !r.slide.every(isVec)) e.push(`${where}: slide must be an array of [x, y]`);
      else if (!r.slide.every(inBounds)) e.push(`${where}: a slide point is outside bounds`);
    }
    if (r?.expect_bypass !== undefined) {
      const wallIds = new Set(Array.isArray(l.walls) ? l.walls.map((w) => w?.id) : []);
      if (!Array.isArray(r.expect_bypass)) e.push(`${where}: expect_bypass must be an array`);
      else r.expect_bypass.forEach((x, j) => {
        if (!wallIds.has(x?.wall)) e.push(`${where}.expect_bypass[${j}]: unknown wall "${x?.wall}"`);
        if (x?.wall_on !== 'left' && x?.wall_on !== 'right') e.push(`${where}.expect_bypass[${j}]: wall_on must be left | right`);
      });
    }
  });
  return e;
}

/** Walls as geometry segments. */
export const wallSegments = (layout: Layout): Segment[] => layout.walls.map((w) => ({ a: w.a, b: w.b, thickness: w.thickness }));

/** The zone containing p (first match), if any. */
export function zoneAt(layout: Layout, p: Vec2): LayoutZone | undefined {
  return layout.zones.find((z) => pointInPoly(p, z.poly));
}
