// Training Ground / level generator v2, stage 1 (board 066, task 070): REGION RE-ROLL and LOCKED DISTRICTS on a generated layout
// (schema codex.levelgen_layout/1 + its optional versioned `regen` block). Pure TS, no DOM - the TG page and the tests call it.
//
// How a re-roll works (docs/port/LEVELGEN_V2_STAGE1_REPORT.md):
//   1. The layout is split into GROUPS: a building with its walls, its ladders and its mantle edges (buildings move whole); every
//      other wall / ladder / cover / zone / spawn / ambush point / patrol route / mantle edge on its own. Each group has a CENTRE.
//   2. The zone (a rectangle or a polygon / lasso, UE cm) selects the groups whose centre is inside it - minus the groups inside a
//      LOCKED district (layout.regen.locks) and minus the INFRASTRUCTURE (the streets Z_ST_*, the canal: the cut, its walls, crossings
//      and ladders) that is never re-rolled, so roads and the canal never end at the zone border.
//   3. A fresh layout is generated with the same profile + params, the SAME skeleton seed (layout.seed: identical streets and cut)
//      and the click's seed for the blocks. Its groups whose centre is inside the zone (and outside every lock) replace the selected
//      ones, unless they would overlap a kept object, a lock or a road (dropped, reported). Everything else stays the same object
//      (byte-identical in the JSON). New obstacles are snapped to the measured height classes (obstacle_heights.json).
//   4. The routes are spliced at the zone border (old points outside + the fresh route's points inside) and the existing route
//      check (evaluate.ts checkLayoutRoutes) runs; a re-roll that breaks the mandatory route is rejected and retried with the next
//      seed (seed + k * RETRY_SEED_STEP), up to maxRetries times; the count is reported.
// Deterministic: same layout + same zone + same seed -> the same JSON.

import type { Vec2 } from '../movement/geometry.ts';
import { dirOf, pointInPoly, segSegDist } from '../movement/geometry.ts';
import type { RouteReport } from './evaluate.ts';
import { checkLayoutRoutes } from './evaluate.ts';
import { generateLayout } from './generate.ts';
import { classifyObstacleHeight, standardObstacleHeight } from './obstacleHeights.ts';
import type { Profile } from './profile.ts';
import type { GenBuilding, GenCover, GenLadder, GenLayout, GenRoute, GenWall, LockDistrict, MantleEdge, PatrolRoute, RegenState, SpawnPoint } from './schema.ts';
import { REGEN_STATE_VERSION } from './schema.ts';

// ---- shapes -----------------------------------------------------------------------------------------------------------------

/** A zone the designer draws on the 2D plan (UE cm). */
export type ZoneShape = { kind: 'rect'; a: Vec2; b: Vec2 } | { kind: 'poly'; points: Vec2[] };

/** The zone as a closed polygon (a rectangle -> its 4 corners). */
export function shapePolygon(s: ZoneShape): Vec2[] {
  if (s.kind === 'rect') {
    const x0 = Math.min(s.a[0], s.b[0]), x1 = Math.max(s.a[0], s.b[0]), y0 = Math.min(s.a[1], s.b[1]), y1 = Math.max(s.a[1], s.b[1]);
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
  }
  return s.points.map((p) => [p[0], p[1]] as Vec2);
}

/** |area| of a simple polygon (shoelace), cm^2. */
export function polygonArea(poly: readonly Vec2[]): number {
  let a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
  return Math.abs(a) / 2;
}

/** A zone worth re-rolling: >= 3 points and at least 1 m^2. */
export const isUsableZone = (poly: readonly Vec2[]): boolean => poly.length >= 3 && polygonArea(poly) >= 100 * 100;

/** A lasso stroke thinned to points at least `minStep` apart (the UI records every pointer move). */
export function simplifyLasso(points: readonly Vec2[], minStep: number): Vec2[] {
  const out: Vec2[] = [];
  for (const p of points) {
    const q = out[out.length - 1];
    if (!q || Math.hypot(p[0] - q[0], p[1] - q[1]) >= minStep) out.push([p[0], p[1]]);
  }
  return out;
}

export const pointInZone = (p: Vec2, zone: readonly Vec2[]): boolean => pointInPoly(p, zone);

const centroid = (pts: readonly Vec2[]): Vec2 => [pts.reduce((s, p) => s + p[0], 0) / pts.length, pts.reduce((s, p) => s + p[1], 0) / pts.length];
const bbox = (pts: readonly Vec2[]) => {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of pts) { x0 = Math.min(x0, p[0]); y0 = Math.min(y0, p[1]); x1 = Math.max(x1, p[0]); y1 = Math.max(y1, p[1]); }
  return { x0, y0, x1, y1 };
};

/** Two simple polygons share area or cross (a vertex inside the other, or two edges intersect). */
export function polygonsOverlap(a: readonly Vec2[], b: readonly Vec2[]): boolean {
  const A = bbox(a), B = bbox(b);
  if (A.x1 < B.x0 || B.x1 < A.x0 || A.y1 < B.y0 || B.y1 < A.y0) return false;
  if (a.some((p) => pointInPoly(p, b)) || b.some((p) => pointInPoly(p, a))) return true;
  for (let i = 0; i < a.length; i++) {
    const p = a[i], q = a[(i + 1) % a.length];
    for (let j = 0; j < b.length; j++) if (segSegDist(p, q, b[j], b[(j + 1) % b.length]) < 1e-6) return true;
  }
  return false;
}

/** Touching neighbours (a fence corner on a fence corner, a building on a street edge) are not an overlap: shrink by this, cm. */
const TOUCH_CM = 10;
const shrink = (poly: Vec2[], cm: number): Vec2[] => {
  const c = centroid(poly);
  return poly.map((p) => {
    const d = Math.hypot(p[0] - c[0], p[1] - c[1]);
    const k = d > cm * 2 ? (d - cm) / d : 0.5;
    return [c[0] + (p[0] - c[0]) * k, c[1] + (p[1] - c[1]) * k] as Vec2;
  });
};
const rectPoly = ([a, b]: [Vec2, Vec2]): Vec2[] => [[a[0], a[1]], [b[0], a[1]], [b[0], b[1]], [a[0], b[1]]];
const square = (p: Vec2, half: number): Vec2[] => rectPoly([[p[0] - half, p[1] - half], [p[0] + half, p[1] + half]]);
/** A wall's footprint: the segment shortened by TOUCH_CM at both ends, with its thickness (>= 10 cm). */
function wallQuad(w: GenWall): Vec2[] {
  const dx = w.b[0] - w.a[0], dy = w.b[1] - w.a[1], L = Math.hypot(dx, dy) || 1;
  const ux = dx / L, uy = dy / L, cut = Math.min(TOUCH_CM, L / 4), t = Math.max(10, w.thickness || 0) / 2;
  const a: Vec2 = [w.a[0] + ux * cut, w.a[1] + uy * cut], b: Vec2 = [w.b[0] - ux * cut, w.b[1] - uy * cut];
  return [[a[0] - uy * t, a[1] + ux * t], [b[0] - uy * t, b[1] + ux * t], [b[0] + uy * t, b[1] - ux * t], [a[0] + uy * t, a[1] - ux * t]];
}
/** A cover's yawed footprint (the evaluator's coverSegs corners), shrunk by TOUCH_CM. */
function coverQuad(c: GenCover): Vec2[] {
  const hx = c.size[0] / 2, hy = c.size[1] / 2, d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
  const corner = (sx: number, sy: number): Vec2 => [c.pos[0] + d[0] * hx * sx + r[0] * hy * sy, c.pos[1] + d[1] * hx * sx + r[1] * hy * sy];
  return shrink([corner(1, 1), corner(-1, 1), corner(-1, -1), corner(1, -1)], TOUCH_CM);
}

// ---- locks (the versioned `regen` block) ------------------------------------------------------------------------------------

/** The layout's regen state; an older file without the block = no locks, no re-rolls. A newer version is refused. */
export function regenStateOf(l: GenLayout): RegenState {
  if (!l.regen) return { version: REGEN_STATE_VERSION, locks: [], rerolls: [] };
  if (l.regen.version > REGEN_STATE_VERSION) throw new Error(`regen.version ${l.regen.version} is newer than this tool (${REGEN_STATE_VERSION})`);
  return { version: REGEN_STATE_VERSION, locks: l.regen.locks ?? [], rerolls: l.regen.rerolls ?? [] };
}

/** A new layout with one more locked district (id LOCK_<n>, unique). */
export function addLock(l: GenLayout, poly: Vec2[], name?: string): GenLayout {
  if (!isUsableZone(poly)) throw new Error('a lock needs a polygon of >= 3 points and >= 1 m^2');
  const s = regenStateOf(l);
  let n = s.locks.length + 1;
  while (s.locks.some((k) => k.id === `LOCK_${n}`)) n++;
  const id = `LOCK_${n}`;
  return { ...l, regen: { ...s, locks: [...s.locks, { id, name: name?.trim() || id, poly: poly.map((p) => [p[0], p[1]] as Vec2) }] } };
}

export function removeLock(l: GenLayout, id: string): GenLayout {
  const s = regenStateOf(l);
  return { ...l, regen: { ...s, locks: s.locks.filter((k) => k.id !== id) } };
}

export const isLockedAt = (p: Vec2, locks: readonly LockDistrict[]): boolean => locks.some((k) => pointInPoly(p, k.poly));

// ---- groups -----------------------------------------------------------------------------------------------------------------

export type RegenCategory = 'building' | 'wall' | 'ladder' | 'cover' | 'zone' | 'spawn_point' | 'ambush_point' | 'patrol_route' | 'mantle_edge';
type Zone = GenLayout['zones'][number];
type Ambush = GenLayout['ambush_points'][number];

export interface GroupMembers {
  buildings: GenBuilding[]; walls: GenWall[]; ladders: GenLadder[]; covers: GenCover[]; zones: Zone[];
  spawn_points: SpawnPoint[]; ambush_points: Ambush[]; patrol_routes: PatrolRoute[]; mantle_edges: MantleEdge[];
}
type ArrayKey = keyof GroupMembers;
const ARRAYS: ArrayKey[] = ['buildings', 'walls', 'ladders', 'covers', 'zones', 'spawn_points', 'ambush_points', 'patrol_routes', 'mantle_edges'];

/** One unit a re-roll replaces whole. */
export interface RegenGroup {
  key: string;
  category: RegenCategory;
  id: string;
  /** The point the zone / lock test uses (a building: its footprint centre). */
  centre: Vec2;
  /** Footprints that must not overlap a kept object (empty = a marker that never blocks). */
  solid: Vec2[][];
  /** Infrastructure (streets, the canal): never re-rolled - the seam rule. */
  fixed: boolean;
  members: Partial<GroupMembers>;
}

/** Street and canal zones: the skeleton every re-roll keeps (roads and the canal never end at a zone border). */
export const isInfrastructureZone = (z: Zone): boolean => z.id === 'Z_cut' || z.id.startsWith('Z_ST_') || z.intent === 'kill_zone';

const layerOwner = (layer: string): string | undefined =>
  layer.startsWith('roof:') ? layer.slice(5) : layer.startsWith('floor:') ? layer.split(':')[1] : undefined;

/** The layout split into re-roll groups, in a stable order (buildings first, then each array in file order). */
export function layoutGroups(l: GenLayout): RegenGroup[] {
  const out: RegenGroup[] = [];
  const byBuilding = new Map<string, RegenGroup>();
  for (const b of l.buildings) {
    const g: RegenGroup = { key: `building:${b.id}`, category: 'building', id: b.id, centre: centroid(rectPoly(b.footprint)), solid: [shrink(rectPoly(b.footprint), TOUCH_CM)], fixed: false,
      members: { buildings: [b], walls: [], ladders: [], mantle_edges: [] } };
    byBuilding.set(b.id, g); out.push(g);
  }
  const single = (category: RegenCategory, id: string, centre: Vec2, solid: Vec2[][], fixed: boolean, members: Partial<GroupMembers>) =>
    out.push({ key: `${category}:${id}`, category, id, centre, solid, fixed, members });
  for (const w of l.walls) {
    const owner = w.building ? byBuilding.get(w.building) : undefined;
    if (owner) { owner.members.walls!.push(w); continue; }
    single('wall', w.id, [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2], [wallQuad(w)], w.kind === 'cut_wall', { walls: [w] });
  }
  for (const k of l.ladders) {
    const bid = layerOwner(k.to) ?? layerOwner(k.from);
    const owner = bid ? byBuilding.get(bid) : undefined;
    if (owner) { owner.members.ladders!.push(k); owner.solid.push(square(k.pos, 50)); continue; }
    single('ladder', k.id, k.pos, [], k.from === 'cut' || k.to === 'cut', { ladders: [k] });
  }
  for (const c of l.covers) single('cover', c.id, c.pos, [coverQuad(c)], false, { covers: [c] });
  for (const z of l.zones) single('zone', z.id, centroid(z.poly), [], isInfrastructureZone(z), { zones: [z] });
  for (const s of l.spawn_points) single('spawn_point', s.id, s.pos, [], false, { spawn_points: [s] });
  for (const a of l.ambush_points) single('ambush_point', a.id, a.pos, [], false, { ambush_points: [a] });
  for (const p of l.patrol_routes) single('patrol_route', p.id, centroid(p.points), [], false, { patrol_routes: [p] });
  for (const m of l.mantle_edges ?? []) {
    const owner = m.building ? byBuilding.get(m.building) : undefined;
    if (owner) { owner.members.mantle_edges!.push(m); continue; }
    single('mantle_edge', m.id, [(m.start[0] + m.end[0]) / 2, (m.start[1] + m.end[1]) / 2], [], false, { mantle_edges: [m] });
  }
  return out;
}

export interface ZoneSelection {
  /** Re-rolled: centre inside the zone, outside every lock, not infrastructure. */
  inside: RegenGroup[];
  /** Centre inside the zone AND inside a lock: kept. */
  locked: RegenGroup[];
  /** Centre inside the zone but infrastructure (streets / the canal): kept. */
  fixed: RegenGroup[];
}

/** Select by CENTRE: a group is re-rolled when its centre is inside the zone (buildings whole), never when locked or fixed. */
export function selectByCentre(groups: readonly RegenGroup[], zone: readonly Vec2[], locks: readonly LockDistrict[]): ZoneSelection {
  const sel: ZoneSelection = { inside: [], locked: [], fixed: [] };
  for (const g of groups) {
    if (!pointInPoly(g.centre, zone)) continue;
    if (g.fixed) sel.fixed.push(g);
    else if (isLockedAt(g.centre, locks)) sel.locked.push(g);
    else sel.inside.push(g);
  }
  return sel;
}

/** Shorthand for the UI: the selection on a layout with its own locks. */
export const selectZone = (l: GenLayout, zone: readonly Vec2[]): ZoneSelection => selectByCentre(layoutGroups(l), zone, regenStateOf(l).locks);

// ---- heights ----------------------------------------------------------------------------------------------------------------

/** Snap a new group's obstacle heights to the measured classes (obstacle_heights.json); returns how many changed. */
function snapHeights(m: Partial<GroupMembers>): number {
  let n = 0;
  const fix = (h: number): number => { if (classifyObstacleHeight(h)) return h; n++; return standardObstacleHeight(h); };
  for (const w of m.walls ?? []) w.height = fix(w.height);
  for (const c of m.covers ?? []) c.size = [c.size[0], c.size[1], fix(c.size[2])];
  for (const e of m.mantle_edges ?? []) e.height_cm = fix(e.height_cm);
  for (const b of m.buildings ?? []) {
    for (const p of b.partitions ?? []) if (p.height !== undefined) p.height = fix(p.height);
    for (const p of b.parapets ?? []) p.height_cm = fix(p.height_cm);
    for (const e of b.mantle_edges ?? []) e.height_cm = fix(e.height_cm);
  }
  return n;
}

// ---- routes -----------------------------------------------------------------------------------------------------------------

/**
 * Splice a route at the zone border: the old points up to the first point inside the zone, the fresh route's points inside the
 * zone (its first .. last inside point), the old points after the last inside point. Links inside each kept part move with it;
 * the two border legs carry no link. A route that never enters the zone is returned unchanged (the same object).
 */
export function spliceRoute(old: GenRoute, fresh: GenRoute | undefined, inZone: (p: Vec2) => boolean, rename: ReadonlyMap<string, string> = new Map()): GenRoute {
  const first = old.points.findIndex(inZone);
  if (first < 0) return old;
  let last = first;
  for (let k = old.points.length - 1; k > first; k--) if (inZone(old.points[k])) { last = k; break; }
  const oldLayers = old.layers ?? old.points.map(() => 'ground');
  let nf = -1, nl = -1;
  if (fresh) {
    nf = fresh.points.findIndex(inZone);
    if (nf >= 0) { nl = nf; for (let k = fresh.points.length - 1; k > nf; k--) if (inZone(fresh.points[k])) { nl = k; break; } }
  }
  const freshLayers = fresh ? fresh.layers ?? fresh.points.map(() => 'ground') : [];
  const mid = nf >= 0 ? fresh!.points.slice(nf, nl + 1) : [];
  const midLayers = nf >= 0 ? freshLayers.slice(nf, nl + 1) : [];
  const points = [...old.points.slice(0, first), ...mid.map((p) => [p[0], p[1]] as Vec2), ...old.points.slice(last + 1)];
  const layers = [...oldLayers.slice(0, first), ...midLayers, ...oldLayers.slice(last + 1)];
  const links: { at: number; via: string }[] = [];
  for (const k of old.links ?? []) if (k.at < first) links.push(k);
  if (nf >= 0) for (const k of fresh!.links ?? []) if (k.at > nf && k.at <= nl) links.push({ at: k.at - nf + first, via: rename.get(k.via) ?? k.via });
  for (const k of old.links ?? []) if (k.at > last + 1) links.push({ at: k.at - (last + 1) + first + mid.length, via: k.via });
  const out: GenRoute = { ...old, points };
  if (old.layers || (fresh?.layers && mid.length)) out.layers = layers;
  if (old.links || links.length) out.links = links;
  return out;
}

// ---- the re-roll ------------------------------------------------------------------------------------------------------------

export type DropReason = 'id_taken' | 'overlaps_kept' | 'overlaps_lock' | 'blocks_road';
export interface DroppedGroup { id: string; category: RegenCategory; reason: DropReason }
export interface RegenAttempt { seed: number; ok: boolean; reason?: string }
export interface RouteStatus { id: string; mandatory: boolean; completable: boolean; fail?: string }

export interface RegionRegenOptions {
  layout: GenLayout;
  /** The zone polygon (UE cm): shapePolygon(rect | poly). */
  zone: Vec2[];
  /** This click's seed (a new one per click). Retries use seed + k * RETRY_SEED_STEP. */
  seed: number;
  /** The layout's reference profile (layout.profile). */
  profile: Profile;
  /** Rejected attempts allowed after the first (default 6). */
  maxRetries?: number;
  /** 'mandatory' (default): the mandatory route must stay completable; 'all': no completable route may break. */
  gate?: 'mandatory' | 'all';
  /** The route check (default: evaluate.ts checkLayoutRoutes). */
  checkRoutes?: (l: GenLayout) => RouteReport[];
}

export interface RegionRegenResult {
  /** True = `layout` is the re-rolled map; false = every attempt broke the route and `layout` is the input unchanged. */
  ok: boolean;
  layout: GenLayout;
  /** The seed of the accepted attempt (or of the last one tried). */
  seed: number;
  /** Rejected attempts before the accepted one (or all of them on failure). */
  retries: number;
  attempts: RegenAttempt[];
  /** Ids of the groups removed from the old layout / added from the fresh one (accepted attempt). */
  replaced: string[];
  added: string[];
  dropped: DroppedGroup[];
  /** Groups inside the zone that a lock / the infrastructure kept. */
  locked: string[];
  fixed: string[];
  snappedHeights: number;
  routes: RouteStatus[];
  /** The fresh layout's streets / cut equal the old ones (false: an edited map - the overlap rules still keep it consistent). */
  skeletonMatch: boolean;
  message: string;
}

export const RETRY_SEED_STEP = 7919;
export const DEFAULT_MAX_RETRIES = 6;

const routeStatus = (r: RouteReport): RouteStatus => ({ id: r.id, mandatory: r.mandatory, completable: r.completable, ...(r.fail ? { fail: r.fail } : {}) });

interface Merge { layout: GenLayout; replaced: string[]; added: string[]; dropped: DroppedGroup[]; snapped: number; skeletonMatch: boolean }

function mergeAttempt(old: GenLayout, oldGroups: RegenGroup[], sel: ZoneSelection, zone: Vec2[], locks: LockDistrict[], seed: number, profile: Profile): Merge {
  const fresh = generateLayout({ profile, seed, params: old.params, skeletonSeed: old.seed, name: old.name });
  const freshGroups = layoutGroups(fresh);
  const removed = new Set<object>();
  for (const g of sel.inside) for (const k of ARRAYS) for (const x of g.members[k] ?? []) removed.add(x);
  const kept = oldGroups.filter((g) => !sel.inside.includes(g));
  const solids: Vec2[][] = kept.flatMap((g) => g.solid);
  const taken = new Map<RegenCategory, Set<string>>();
  const memberIds = new Set<string>();
  const take = (g: RegenGroup) => {
    if (!taken.has(g.category)) taken.set(g.category, new Set());
    taken.get(g.category)!.add(g.id);
    for (const w of g.members.walls ?? []) memberIds.add(`wall:${w.id}`);
    for (const k of g.members.ladders ?? []) memberIds.add(`ladder:${k.id}`);
  };
  kept.forEach(take);
  const roads: Vec2[][] = [...old.zones.filter(isInfrastructureZone).map((z) => z.poly), ...(old.cut ? [old.cut.poly] : [])];
  const inZone = (p: Vec2) => pointInPoly(p, zone) && !isLockedAt(p, locks);
  const rename = new Map<string, string>();
  const added: RegenGroup[] = [];
  const dropped: DroppedGroup[] = [];
  let snapped = 0;
  for (const g of freshGroups) {
    if (g.fixed || !inZone(g.centre)) continue;
    const drop = (reason: DropReason) => dropped.push({ id: g.id, category: g.category, reason });
    const isTaken = (id: string) => (taken.get(g.category)?.has(id) ?? false) || memberIds.has(`${g.category}:${id}`);
    const idTaken = isTaken(g.id);
    const memberTaken = g.category === 'building' && [...(g.members.walls ?? []).map((w) => `wall:${w.id}`), ...(g.members.ladders ?? []).map((k) => `ladder:${k.id}`)].some((x) => memberIds.has(x));
    if ((g.category === 'building' || g.category === 'zone') && (idTaken || memberTaken)) { drop('id_taken'); continue; }
    if (g.solid.some((s) => locks.some((k) => polygonsOverlap(s, k.poly)))) { drop('overlaps_lock'); continue; }
    if (g.solid.some((s) => solids.some((o) => polygonsOverlap(s, o)))) { drop('overlaps_kept'); continue; }
    if (g.category === 'building' && g.solid.some((s) => roads.some((r) => polygonsOverlap(s, r)))) { drop('blocks_road'); continue; }
    if (idTaken) {
      // a counter id (COV_12, LAD_5, ...) that a kept object already has: suffix the re-roll seed (+ a counter if needed)
      let id = `${g.id}_r${seed}`, n = 1;
      while (isTaken(id)) id = `${g.id}_r${seed}_${++n}`;
      rename.set(g.id, id);
      for (const k of ARRAYS) for (const x of g.members[k] ?? []) (x as { id: string }).id = id;
      g.id = id;
    }
    snapped += snapHeights(g.members);
    take(g);
    solids.push(...g.solid);
    added.push(g);
  }
  for (const g of added) for (const m of g.members.mantle_edges ?? []) if (m.wall && rename.has(m.wall)) m.wall = rename.get(m.wall);
  const next: Partial<GroupMembers> = {};
  for (const k of ARRAYS) {
    const before = (k === 'mantle_edges' ? old.mantle_edges ?? [] : old[k]) as object[];
    const arr = [...before.filter((x) => !removed.has(x)), ...added.flatMap((g) => (g.members[k] ?? []) as object[])];
    (next as Record<string, object[]>)[k] = arr;
  }
  const freshRoutes = new Map(fresh.routes.map((r) => [r.id, r]));
  const routes = old.routes.map((r) => spliceRoute(r, freshRoutes.get(r.id), inZone, rename));
  const layout: GenLayout = { ...old, ...(next as GroupMembers), routes };
  if (!old.mantle_edges && !next.mantle_edges!.length) delete layout.mantle_edges;
  const skeletonMatch = JSON.stringify(fresh.cut ?? null) === JSON.stringify(old.cut ?? null)
    && JSON.stringify(fresh.zones.filter(isInfrastructureZone)) === JSON.stringify(old.zones.filter(isInfrastructureZone));
  return { layout, replaced: sel.inside.map((g) => g.id), added: added.map((g) => g.id), dropped, snapped, skeletonMatch };
}

/** Re-roll the zone (see the file header). Pure and deterministic. */
export function regenerateRegion(o: RegionRegenOptions): RegionRegenResult {
  const old = o.layout;
  const state = regenStateOf(old);
  const groups = layoutGroups(old);
  const sel = selectByCentre(groups, o.zone, state.locks);
  const check = o.checkRoutes ?? checkLayoutRoutes;
  const before = check(old);
  const base = { locked: sel.locked.map((g) => g.id), fixed: sel.fixed.map((g) => g.id) };
  if (!isUsableZone(o.zone)) return { ok: false, layout: old, seed: o.seed, retries: 0, attempts: [], replaced: [], added: [], dropped: [], ...base, snappedHeights: 0, routes: before.map(routeStatus), skeletonMatch: true, message: 'зона слишком мала (нужно >= 3 точки и >= 1 м²)' };
  const maxRetries = Math.max(0, o.maxRetries ?? DEFAULT_MAX_RETRIES);
  const attempts: RegenAttempt[] = [];
  let lastRoutes = before;
  for (let k = 0; k <= maxRetries; k++) {
    const seed = o.seed + k * RETRY_SEED_STEP;
    const m = mergeAttempt(old, groups, sel, o.zone, state.locks, seed, o.profile);
    if (!m.replaced.length && !m.added.length) {
      return { ok: true, layout: old, seed, retries: k, attempts: [...attempts, { seed, ok: true, reason: 'nothing to re-roll' }], replaced: [], added: [], dropped: m.dropped, ...base, snappedHeights: 0, routes: before.map(routeStatus), skeletonMatch: m.skeletonMatch, message: 'в зоне нечего перегенерировать (всё закреплено, инфраструктура или пусто)' };
    }
    const after = check(m.layout);
    lastRoutes = after;
    const broken = after.filter((r, i) => {
      const was = before[i]?.completable ?? false;
      if (!was || r.completable) return false;
      return o.gate === 'all' || r.mandatory;
    });
    if (broken.length) { attempts.push({ seed, ok: false, reason: broken.map((r) => `${r.id}: ${r.fail ?? 'broken'}`).join('; ') }); continue; }
    attempts.push({ seed, ok: true });
    const layout: GenLayout = { ...m.layout, regen: { ...state, rerolls: [...state.rerolls, { seed, zone: o.zone.map((p) => [p[0], p[1]] as Vec2), replaced: m.replaced.length, added: m.added.length, retries: k }] } };
    return { ok: true, layout, seed, retries: k, attempts, replaced: m.replaced, added: m.added, dropped: m.dropped, ...base, snappedHeights: m.snapped, routes: after.map(routeStatus), skeletonMatch: m.skeletonMatch,
      message: `перегенерировано: убрано ${m.replaced.length}, добавлено ${m.added.length}, отброшено ${m.dropped.length}, повторов ${k}` };
  }
  return { ok: false, layout: old, seed: o.seed + maxRetries * RETRY_SEED_STEP, retries: attempts.length, attempts, replaced: [], added: [], dropped: [], ...base, snappedHeights: 0, routes: lastRoutes.map(routeStatus), skeletonMatch: true,
    message: `маршрут ломается во всех ${attempts.length} попытках - карта не изменена` };
}
