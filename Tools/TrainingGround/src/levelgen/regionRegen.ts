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
import { expandLayout } from './expand.ts';
import type { ObjectivesCheck } from './objectives.ts';
import { checkObjectives } from './objectives.ts';
import { generateLayout } from './generate.ts';
import { classifyObstacleHeight, standardObstacleHeight } from './obstacleHeights.ts';
import type { Profile } from './profile.ts';
import type { BuiltTemplate, TemplateContent, Visit } from './blockTemplates.ts';
import { buildTemplate, isAnchorLot, lotAt, lotsOf, pickTemplates, rectCentre, templateContext } from './blockTemplates.ts';
import type { BlockRecord, GenBuilding, GenCover, GenLadder, GenLayout, GenRoute, GenWall, LockDistrict, MantleEdge, PatrolRoute, RegenState, SpawnPoint } from './schema.ts';
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
  return { version: REGEN_STATE_VERSION, locks: l.regen.locks ?? [], rerolls: l.regen.rerolls ?? [], ...(l.regen.blocks ? { blocks: l.regen.blocks } : {}) };
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

/**
 * A saved map with the page's locks written in (the «Сохранить замки» button, stage 2): only `regen.locks` changes - the file's
 * objects, re-roll log and block types stay as saved. Pure; the server writes the result (node/locksFile.ts).
 */
export function withLocks(file: GenLayout, locks: readonly LockDistrict[]): GenLayout {
  const s = regenStateOf(file);
  return { ...file, regen: { ...s, locks: locks.map((k) => ({ id: k.id, name: k.name, poly: k.poly.map((p) => [p[0], p[1]] as Vec2) })) } };
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

/**
 * The family of a free-standing wall: the segments of one fence ring (F_<name>_<N|S|E|W>_<k>, fenceRect / splitWall) or one
 * dead-end alley (W_<name>_<side letter>). A family is one re-roll group (stage 2 fix: a zone border no longer splits a fence).
 */
export function wallFamily(w: GenWall): string | undefined {
  if (w.building || w.kind === 'cut_wall') return undefined;
  return (/^(F_.+?)_[NSEW]_\d+$/.exec(w.id) ?? /^(W_[a-z]+)_[a-z]$/.exec(w.id))?.[1];
}

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
  const families = new Map<string, RegenGroup>();
  for (const w of l.walls) {
    const owner = w.building ? byBuilding.get(w.building) : undefined;
    if (owner) { owner.members.walls!.push(w); continue; }
    const fam = wallFamily(w);
    if (fam) {
      // a fence ring / a dead-end alley moves WHOLE (stage 2: no fence gap where a zone border crossed it)
      let g = families.get(fam);
      if (!g) { g = { key: `wall:${fam}`, category: 'wall', id: fam, centre: [0, 0], solid: [], fixed: false, members: { walls: [] } }; families.set(fam, g); out.push(g); }
      g.members.walls!.push(w); g.solid.push(wallQuad(w));
      continue;
    }
    single('wall', w.id, [(w.a[0] + w.b[0]) / 2, (w.a[1] + w.b[1]) / 2], [wallQuad(w)], w.kind === 'cut_wall', { walls: [w] });
  }
  for (const g of families.values()) {
    const b = bbox(g.members.walls!.flatMap((w) => [w.a, w.b]));
    g.centre = [(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2];
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
/** A block that got another type (stage 2). */
export interface Retemplated { id: string; from: string; to: string }
/**
 * 'types' (default, stage 2): every block (lot, blockTemplates.ts) whose centre is inside the zone is replaced WHOLE by another
 * block type drawn with the seed; the objects outside the blocks (street covers, fences along the streets) by their centre.
 * 'jitter': the stage-1 re-roll only (the same set-pieces re-drawn with the new seed).
 */
export type BlockMode = 'types' | 'jitter';

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
  /** Block mode (default 'types'). */
  blocks?: BlockMode;
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
  /** Selected buildings kept as they were because their fresh twin could not be placed (routes through them stay valid). */
  restored: string[];
  /** Groups inside the zone that a lock / the infrastructure kept. */
  locked: string[];
  fixed: string[];
  snappedHeights: number;
  routes: RouteStatus[];
  /** The fresh layout's streets / cut equal the old ones (false: an edited map - the overlap rules still keep it consistent). */
  skeletonMatch: boolean;
  /** Blocks that got another type (stage 2). */
  retemplated: Retemplated[];
  /** Blocks in the zone that no catalog type could fill (their old type re-drawn with the new seed instead). */
  templateFailed: string[];
  /** Blocks in the zone that hold the map's spawn / end: they keep their type (their objects re-roll by centre). */
  anchored: string[];
  /** Variant routes dropped (fewer than 2 points left once their blocks got other types). */
  removedRoutes: string[];
  /** The start + goals re-walked on the result (a map with `objectives` only): a goal in a re-typed block may need placing again. */
  objectives?: ObjectivesCheck;
  message: string;
}

export const RETRY_SEED_STEP = 7919;
export const DEFAULT_MAX_RETRIES = 6;

const routeStatus = (r: RouteReport): RouteStatus => ({ id: r.id, mandatory: r.mandatory, completable: r.completable, ...(r.fail ? { fail: r.fail } : {}) });

/** What a re-roll replaces: the zone, the locks, the blocks it gives other types, and the per-point test. */
export interface RegionPlan {
  zone: Vec2[];
  locks: LockDistrict[];
  mode: BlockMode;
  lots: BlockRecord[];
  /** Blocks re-typed (centre in the zone, not locked, not an anchor). */
  selLots: BlockRecord[];
  anchored: string[];
  /** Is this point (a group centre / a route point) re-rolled? */
  selected: (p: Vec2, category?: RegenCategory) => boolean;
  /** Is this point inside the zone's reach before the locks (the stage-1 "in the zone" + the selected blocks)? */
  inScope: (p: Vec2, category?: RegenCategory) => boolean;
}

export function planRegion(l: GenLayout, zone: Vec2[], lots: BlockRecord[], mode: BlockMode = 'types'): RegionPlan {
  const locks = regenStateOf(l).locks;
  const inZone = (p: Vec2) => pointInPoly(p, zone);
  const hit = mode === 'types' ? lots.filter((k) => inZone(rectCentre(k.rect))) : [];
  const anchored = hit.filter((k) => isAnchorLot(l, k));
  const selLots = hit.filter((k) => !isAnchorLot(l, k) && !isLockedAt(rectCentre(k.rect), locks));
  const selIds = new Set(selLots.map((k) => k.id));
  // an anchor block or a block whose centre is locked keeps its type: its objects follow the stage-1 rule (centre in the zone)
  const anchorIds = new Set(hit.filter((k) => !selIds.has(k.id)).map((k) => k.id));
  const inScope = (p: Vec2, category?: RegenCategory): boolean => {
    if (mode === 'jitter' || category === 'spawn_point') return inZone(p);
    const lot = lotAt(lots, p);
    return lot && !anchorIds.has(lot.id) ? selIds.has(lot.id) : inZone(p);
  };
  return { zone, locks, mode, lots, selLots, anchored: anchored.map((k) => k.id), inScope, selected: (p, c) => inScope(p, c) && !isLockedAt(p, locks) };
}

/** The selection of a plan: groups in its scope, split into re-rolled / locked / infrastructure. */
export function selectPlan(groups: readonly RegenGroup[], plan: RegionPlan): ZoneSelection {
  const sel: ZoneSelection = { inside: [], locked: [], fixed: [] };
  for (const g of groups) {
    if (!plan.inScope(g.centre, g.category)) continue;
    if (g.fixed) sel.fixed.push(g);
    else if (isLockedAt(g.centre, plan.locks)) sel.locked.push(g);
    else sel.inside.push(g);
  }
  return sel;
}

/** The UI's preview: what a 'types' re-roll of this zone would replace (blocks whole), with the blocks it re-types. */
export function selectRegion(l: GenLayout, zone: Vec2[], profile: Profile | null, mode: BlockMode = 'types'): { selection: ZoneSelection; plan: RegionPlan } {
  const lots = mode === 'types' && profile ? lotsOf(l, profile) : [];
  const plan = planRegion(l, zone, lots, mode);
  return { selection: selectPlan(layoutGroups(l), plan), plan };
}

/** The fresh layout of a re-roll attempt: the generator with the map's skeleton and the attempt's seed, + its expansions replayed. */
export function freshLayout(old: GenLayout, profile: Profile, seed: number): GenLayout {
  const base = old.expansion?.base_size_m ?? old.params.size_m;
  let fresh = generateLayout({ profile, seed, params: { ...old.params, size_m: base }, skeletonSeed: old.seed, name: old.name });
  // an expanded map: its expansions replayed on the fresh core - the same street plan (each step's skeleton seed), new blocks
  for (const step of old.expansion?.steps ?? []) fresh = expandLayout({ layout: fresh, factor: step.factor, seed, skeletonSeed: step.seed, profile, gate: false, lots: [] }).layout;
  return fresh;
}

const contentLayout = (c: TemplateContent): GenLayout => ({ buildings: c.buildings, walls: c.walls, ladders: c.ladders, covers: c.covers, zones: c.zones, spawn_points: [],
  ambush_points: c.ambush_points, patrol_routes: [], mantle_edges: c.mantle_edges } as unknown as GenLayout);

/**
 * A route with its points in the re-typed blocks removed (and every point on a layer of a removed building); the route visits each
 * new block where it used to enter the old one (the template's visit: a gate, a door, a ladder onto the roof), once per block.
 * Links survive when both ends of their leg survive side by side. Fewer than 2 points left -> null (the route is dropped).
 */
export function rerouteThroughBlocks(r: GenRoute, inBlock: (p: Vec2) => string | undefined, removedBuildings: ReadonlySet<string>, visitOf: (lot: string) => Visit | null): GenRoute | null {
  const lay = r.layers ?? r.points.map(() => 'ground');
  const keep = r.points.map((p, i) => !inBlock(p) && !removedBuildings.has(layerOwner(lay[i]) ?? ''));
  if (keep.every(Boolean)) return r;
  const pts: Vec2[] = [], layers: string[] = [], links: { at: number; via: string }[] = [];
  const idx: number[] = [];
  const visited = new Set<string>();
  for (let i = 0; i < r.points.length; i++) {
    if (keep[i]) { idx[i] = pts.length; pts.push(r.points[i]); layers.push(lay[i]); continue; }
    idx[i] = -1;
    if (i > 0 && !keep[i - 1]) continue;
    let lot: string | undefined;
    for (let k = i; k < r.points.length && !keep[k] && !lot; k++) lot = inBlock(r.points[k]);
    const v = lot && !visited.has(lot) ? visitOf(lot) : null;
    if (!lot || !v) continue;
    visited.add(lot);
    const at0 = pts.length;
    v.points.forEach((p, k) => { pts.push([p[0], p[1]]); layers.push(v.layers[k]); });
    for (const k of v.links) links.push({ at: at0 + k.at, via: k.via });
    // a visit ends where the next kept point picks the route up: no index of the old route maps into it
  }
  for (const k of r.links ?? []) {
    const a = idx[k.at - 1], b = idx[k.at];
    if (a >= 0 && b === a + 1) links.push({ at: b, via: k.via });
  }
  if (pts.length < 2) return null;
  links.sort((x, y) => x.at - y.at);
  const out: GenRoute = { ...r, points: pts };
  if (r.layers || layers.some((x) => x !== 'ground')) out.layers = layers; else delete out.layers;
  if (r.links || links.length) out.links = links;
  return out;
}

/** The fresh layout with the re-typed blocks' content replaced by their templates (and its routes taken through them). */
function applyTemplates(fresh: GenLayout, built: BuiltTemplate[], lots: readonly BlockRecord[]): GenLayout {
  const acc = new Set(built.map((b) => b.lot));
  const inBlock = (p: Vec2): string | undefined => { const k = lotAt(lots, p); return k && acc.has(k.id) ? k.id : undefined; };
  const drop = new Set<object>();
  const removedB = new Set<string>();
  for (const g of layoutGroups(fresh)) {
    if (g.fixed || g.category === 'spawn_point' || !inBlock(g.centre)) continue;
    for (const k of ARRAYS) for (const x of g.members[k] ?? []) drop.add(x);
    if (g.category === 'building') removedB.add(g.id);
  }
  const newB = built.flatMap((b) => b.content.buildings);
  const next = { ...fresh } as GenLayout;
  for (const k of ['buildings', 'walls', 'ladders', 'covers', 'zones', 'ambush_points'] as const) {
    (next as unknown as Record<string, object[]>)[k] = [...(fresh[k] as object[]).filter((x) => !drop.has(x)), ...built.flatMap((b) => b.content[k] as object[])];
  }
  next.patrol_routes = fresh.patrol_routes.filter((x) => !drop.has(x));
  next.mantle_edges = [...(fresh.mantle_edges ?? []).filter((x) => !drop.has(x)), ...built.flatMap((b) => b.content.mantle_edges)];
  next.spawn_points = fresh.spawn_points.filter((s) => !newB.some((b) => rectContainsPt(b.footprint, s.pos, 100)));
  const visits = new Map(built.map((b) => [b.lot, b.visit]));
  next.routes = fresh.routes.map((r) => rerouteThroughBlocks(r, inBlock, removedB, (lot) => visits.get(lot) ?? null)).filter((r): r is GenRoute => !!r);
  return next;
}
const rectContainsPt = (r: [Vec2, Vec2], p: Vec2, margin: number) => p[0] >= r[0][0] - margin && p[0] <= r[1][0] + margin && p[1] >= r[0][1] - margin && p[1] <= r[1][1] + margin;

interface Merge {
  layout: GenLayout; replaced: string[]; added: string[]; dropped: DroppedGroup[]; restored: string[]; snapped: number; skeletonMatch: boolean;
  retemplated: Retemplated[]; templateFailed: string[]; blocks: BlockRecord[];
}

function mergeAttempt(old: GenLayout, oldGroups: RegenGroup[], sel: ZoneSelection, plan: RegionPlan, seed: number, profile: Profile): Merge {
  const locks = plan.locks;
  let fresh = freshLayout(old, profile, seed);
  const removed = new Set<object>();
  for (const g of sel.inside) for (const k of ARRAYS) for (const x of g.members[k] ?? []) removed.add(x);
  const kept = oldGroups.filter((g) => !sel.inside.includes(g));
  // hard solids (buildings, walls, ladders) and cover footprints: a fence may graze a cover (a greybox crate against a fence is no
  // conflict), everything else may not
  const solids: Vec2[][] = kept.filter((g) => g.category !== 'cover').flatMap((g) => g.solid);
  const coverSolids: Vec2[][] = kept.filter((g) => g.category === 'cover').flatMap((g) => g.solid);
  const pushSolids = (g: RegenGroup) => (g.category === 'cover' ? coverSolids : solids).push(...g.solid);
  const roads: Vec2[][] = [...old.zones.filter(isInfrastructureZone).map((z) => z.poly), ...(old.cut ? [old.cut.poly] : [])];
  const lockPolys = locks.map((k) => k.poly);
  const overlapsAny = (g: RegenGroup, polys: readonly Vec2[][]) => g.solid.some((s) => polys.some((o) => polygonsOverlap(s, o)));
  // 1. block types (stage 2): each selected block tries the seeded catalog types; the first whose buildings and fences clear the
  //    locks, the kept objects, the roads and the blocks already placed replaces the block whole
  const retemplated: Retemplated[] = [];
  const templateFailed: string[] = [];
  const built: BuiltTemplate[] = [];
  if (plan.selLots.length) {
    const ctx = templateContext(old, seed);
    const placed: Vec2[][] = [];
    for (const lot of plan.selLots) {
      let ok: BuiltTemplate | null = null;
      let okSolids: Vec2[][] = [];
      for (const t of pickTemplates(lot, lot.template, seed, ctx.frame)) {
        const b = buildTemplate(t, lot, ctx);
        const gs = layoutGroups(contentLayout(b.content)).filter((g) => g.category === 'building' || g.category === 'wall');
        const bad = gs.some((g) => overlapsAny(g, lockPolys) || overlapsAny(g, solids) || overlapsAny(g, placed) || (g.category === 'building' && (overlapsAny(g, roads) || overlapsAny(g, coverSolids))));
        if (!bad) { ok = b; okSolids = gs.flatMap((g) => g.solid); break; }
      }
      if (ok) { built.push(ok); placed.push(...okSolids); retemplated.push({ id: lot.id, from: lot.template, to: ok.template }); } else templateFailed.push(lot.id);
    }
    if (built.length) fresh = applyTemplates(fresh, built, plan.lots);
  }
  const accLots = new Set(built.map((b) => b.lot));
  const inAccLot = (p: Vec2) => { const k = lotAt(plan.lots, p); return !!k && accLots.has(k.id); };
  const freshGroups = layoutGroups(fresh);
  const taken = new Map<RegenCategory, Set<string>>();
  const memberIds = new Set<string>();
  const take = (g: RegenGroup) => {
    if (!taken.has(g.category)) taken.set(g.category, new Set());
    taken.get(g.category)!.add(g.id);
    for (const w of g.members.walls ?? []) memberIds.add(`wall:${w.id}`);
    for (const k of g.members.ladders ?? []) memberIds.add(`ladder:${k.id}`);
  };
  kept.forEach(take);
  const inZone = (g: RegenGroup) => plan.selected(g.centre, g.category);
  const rename = new Map<string, string>();
  const added: RegenGroup[] = [];
  const dropped: DroppedGroup[] = [];
  let snapped = 0;
  const memberTakenOf = (g: RegenGroup) => [...(g.members.walls ?? []).map((w) => `wall:${w.id}`), ...(g.members.ladders ?? []).map((k) => `ladder:${k.id}`)].some((x) => memberIds.has(x));
  const rejectReason = (g: RegenGroup): DropReason | undefined => {
    const isTaken = (id: string) => (taken.get(g.category)?.has(id) ?? false) || memberIds.has(`${g.category}:${id}`);
    if ((g.category === 'building' || g.category === 'zone') && (isTaken(g.id) || memberTakenOf(g))) return 'id_taken';
    if (overlapsAny(g, lockPolys)) return 'overlaps_lock';
    if (overlapsAny(g, solids) || (g.category !== 'wall' && overlapsAny(g, coverSolids))) return 'overlaps_kept';
    if (g.category === 'building' && overlapsAny(g, roads)) return 'blocks_road';
    return undefined;
  };
  // BUILDINGS first. A selected old building that gets no fresh twin (same id) placed is RESTORED (kept as it was): the routes and
  // the links through its roof / floors stay valid, and the re-roll never leaves a hole where a set-piece stood. A restored building
  // wins over an overlapping fresh one (that one is dropped and its own old twin restored in turn). A building of a re-typed block
  // is replaced by design (it has no twin): never restored.
  const freshB = new Map<string, RegenGroup>();
  for (const g of freshGroups) {
    if (g.category !== 'building' || g.fixed || !inZone(g)) continue;
    const reason = rejectReason(g);
    if (reason) dropped.push({ id: g.id, category: g.category, reason }); else freshB.set(g.id, g);
  }
  const oldB = sel.inside.filter((g) => g.category === 'building' && !inAccLot(g.centre));
  const restored: RegenGroup[] = [];
  for (let changed = true; changed;) {
    changed = false;
    for (const o of oldB) {
      if (restored.includes(o) || freshB.has(o.id)) continue;
      restored.push(o); changed = true;
      for (const [id, f] of freshB) if (overlapsAny(f, o.solid)) { freshB.delete(id); dropped.push({ id, category: 'building', reason: 'overlaps_kept' }); }
    }
  }
  for (const o of restored) {
    for (const k of ARRAYS) for (const x of o.members[k] ?? []) removed.delete(x);
    take(o); pushSolids(o);
  }
  for (const g of freshB.values()) { snapped += snapHeights(g.members); take(g); pushSolids(g); added.push(g); }
  // FENCE RINGS next (stage 2): a selected old ring whose fresh twin cannot be placed is restored too - never a fence with a gap
  const isFamily = (g: RegenGroup) => g.category === 'wall' && (g.members.walls?.length ?? 0) > 1;
  const freshF = freshGroups.filter((g) => isFamily(g) && !g.fixed && inZone(g));
  const placedF = new Set<RegenGroup>();
  for (const g of freshF) {
    const reason = rejectReason(g);
    if (reason) { dropped.push({ id: g.id, category: g.category, reason }); continue; }
    placedF.add(g);
  }
  const placedFIds = new Set([...placedF].map((g) => g.id));
  for (const o of sel.inside.filter((g) => isFamily(g) && !inAccLot(g.centre) && !placedFIds.has(g.id))) {
    if (overlapsAny(o, added.flatMap((a) => a.solid))) continue;
    for (const k of ARRAYS) for (const x of o.members[k] ?? []) removed.delete(x);
    take(o); pushSolids(o); restored.push(o);
  }
  for (const g of freshGroups) {
    if (g.category === 'building' || g.fixed || !inZone(g)) continue;
    if (isFamily(g) && !placedF.has(g)) continue;
    const reason = placedF.has(g) ? undefined : rejectReason(g);
    if (reason) { dropped.push({ id: g.id, category: g.category, reason }); continue; }
    const isTaken = (id: string) => (taken.get(g.category)?.has(id) ?? false) || memberIds.has(`${g.category}:${id}`);
    if (isTaken(g.id) || memberTakenOf(g)) {
      // a counter id (COV_12, LAD_5, F_lot_N_0 ...) that a kept object already has: suffix the re-roll seed (+ a counter if needed);
      // a fence family renames every segment
      const fresh1 = (base: string, cat: string) => { let id = `${base}_r${seed}`, n = 1; while (memberIds.has(`${cat}:${id}`) || (taken.get(g.category)?.has(id) ?? false)) id = `${base}_r${seed}_${++n}`; return id; };
      const many = (g.members.walls?.length ?? 0) > 1;
      if (many) {
        for (const w of g.members.walls!) { const id = fresh1(w.id, 'wall'); rename.set(w.id, id); w.id = id; memberIds.add(`wall:${id}`); }
        g.id = fresh1(g.id, g.category);
      } else {
        const id = fresh1(g.id, g.category);
        rename.set(g.id, id);
        for (const k of ARRAYS) for (const x of g.members[k] ?? []) (x as { id: string }).id = id;
        g.id = id;
      }
    }
    snapped += snapHeights(g.members);
    take(g);
    pushSolids(g);
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
  const inRoute = (p: Vec2) => plan.selected(p);
  const routes = old.routes.map((r) => spliceRoute(r, freshRoutes.get(r.id), inRoute, rename)).filter((r) => r.mandatory || r.points.length >= 2);
  const layout: GenLayout = { ...old, ...(next as GroupMembers), routes };
  if (!old.mantle_edges && !next.mantle_edges!.length) delete layout.mantle_edges;
  const skeletonMatch = JSON.stringify(fresh.cut ?? null) === JSON.stringify(old.cut ?? null)
    && JSON.stringify(fresh.zones.filter(isInfrastructureZone)) === JSON.stringify(old.zones.filter(isInfrastructureZone));
  const types = new Map(retemplated.map((x) => [x.id, x.to]));
  const blocks = plan.lots.map((k) => (types.has(k.id) ? { ...k, template: types.get(k.id)! } : k));
  return { layout, replaced: sel.inside.filter((g) => !restored.includes(g)).map((g) => g.id), added: added.map((g) => g.id), dropped, restored: restored.map((g) => g.id), snapped, skeletonMatch,
    retemplated, templateFailed, blocks };
}

/** Per route: the spliced version, or - when only the old one still passes on the merged map - the old route unchanged. */
function pickRoutes(m: GenLayout, old: GenLayout, check: (l: GenLayout) => RouteReport[]): { layout: GenLayout; reports: RouteReport[] } {
  const reports = check(m);
  const oldById = new Map(old.routes.map((r) => [r.id, r]));
  const fallback = m.routes.map((r, i) => (reports[i]?.completable || r === oldById.get(r.id) ? r : oldById.get(r.id) ?? r));
  if (fallback.every((r, i) => r === m.routes[i])) return { layout: m, reports };
  const alt = check({ ...m, routes: fallback });
  const routes = m.routes.map((r, i) => (!reports[i]?.completable && alt[i]?.completable ? fallback[i] : r));
  return { layout: { ...m, routes }, reports: reports.map((r, i) => (routes[i] !== m.routes[i] ? alt[i] : r)) };
}

/** Re-roll the zone (see the file header). Pure and deterministic. */
export function regenerateRegion(o: RegionRegenOptions): RegionRegenResult {
  const old = o.layout;
  const state = regenStateOf(old);
  const mode: BlockMode = o.blocks ?? 'types';
  const groups = layoutGroups(old);
  const lots = mode === 'types' ? lotsOf(old, o.profile) : [];
  const plan = planRegion(old, o.zone, lots, mode);
  const sel = selectPlan(groups, plan);
  const check = o.checkRoutes ?? checkLayoutRoutes;
  const before = check(old);
  const beforeById = new Map(before.map((r) => [r.id, r]));
  const base = { locked: sel.locked.map((g) => g.id), fixed: sel.fixed.map((g) => g.id), anchored: plan.anchored };
  const none = { retemplated: [], templateFailed: [], removedRoutes: [] };
  if (!isUsableZone(o.zone)) return { ok: false, layout: old, seed: o.seed, retries: 0, attempts: [], replaced: [], added: [], dropped: [], restored: [], ...base, ...none, snappedHeights: 0, routes: before.map(routeStatus), skeletonMatch: true, message: 'зона слишком мала (нужно >= 3 точки и >= 1 м²)' };
  const maxRetries = Math.max(0, o.maxRetries ?? DEFAULT_MAX_RETRIES);
  const attempts: RegenAttempt[] = [];
  let lastRoutes = before;
  let last: Merge | null = null;
  for (let k = 0; k <= maxRetries; k++) {
    const seed = o.seed + k * RETRY_SEED_STEP;
    const m = mergeAttempt(old, groups, sel, plan, seed, o.profile);
    last = m;
    if (!m.replaced.length && !m.added.length) {
      return { ok: true, layout: old, seed, retries: k, attempts: [...attempts, { seed, ok: true, reason: 'nothing to re-roll' }], replaced: [], added: [], dropped: m.dropped, restored: m.restored, ...base, ...none, snappedHeights: 0, routes: before.map(routeStatus), skeletonMatch: m.skeletonMatch, message: 'в зоне нечего перегенерировать (всё закреплено, инфраструктура или пусто)' };
    }
    const picked = pickRoutes(m.layout, old, check);
    m.layout = picked.layout;
    const after = picked.reports;
    lastRoutes = after;
    const ids = new Set(m.layout.routes.map((r) => r.id));
    const removedRoutes = old.routes.filter((r) => !ids.has(r.id)).map((r) => r.id);
    const broken = after.filter((r) => {
      const was = beforeById.get(r.id)?.completable ?? false;
      if (!was || r.completable) return false;
      return o.gate === 'all' || r.mandatory;
    });
    if (broken.length) { attempts.push({ seed, ok: false, reason: broken.map((r) => `${r.id}: ${r.fail ?? 'broken'}`).join('; ') }); continue; }
    attempts.push({ seed, ok: true });
    const regen: RegenState = { ...state, rerolls: [...state.rerolls, { seed, zone: o.zone.map((p) => [p[0], p[1]] as Vec2), replaced: m.replaced.length, added: m.added.length, retries: k }] };
    if (mode === 'types' && m.blocks.length) regen.blocks = m.blocks;
    const layout: GenLayout = { ...m.layout, regen };
    const objectives = layout.objectives ? checkObjectives(layout) : undefined;
    const typed = m.retemplated.length ? `, кварталов с новым типом ${m.retemplated.length}` : '';
    return { ok: true, layout, seed, retries: k, attempts, replaced: m.replaced, added: m.added, dropped: m.dropped, restored: m.restored, ...base, snappedHeights: m.snapped, routes: after.map(routeStatus), skeletonMatch: m.skeletonMatch,
      retemplated: m.retemplated, templateFailed: m.templateFailed, removedRoutes, ...(objectives ? { objectives } : {}),
      message: `перегенерировано: убрано ${m.replaced.length}, добавлено ${m.added.length}, отброшено ${m.dropped.length}${typed}${m.restored.length ? `, оставлено старыми зданий ${m.restored.length}` : ''}, повторов ${k}` };
  }
  // all rejected: the map is unchanged; the last attempt's drops are reported for the diagnosis
  return { ok: false, layout: old, seed: o.seed + maxRetries * RETRY_SEED_STEP, retries: attempts.length, attempts, replaced: [], added: [], dropped: last?.dropped ?? [], restored: last?.restored ?? [], ...base, ...none, snappedHeights: 0, routes: lastRoutes.map(routeStatus), skeletonMatch: last?.skeletonMatch ?? true,
    message: `маршрут ломается во всех ${attempts.length} попытках - карта не изменена` };
}
