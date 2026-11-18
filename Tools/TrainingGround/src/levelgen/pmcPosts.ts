// Training Ground / level generator - PMC POSTS (docs/design/PMC_DEFENCE.md D2, designer 2026-11-18 evening; task 087): instead of a
// runtime setup phase the GENERATOR places 1-N PMC squads at the strategic places of the tactical map (tacticalMap.ts, TACTICAL_MAP.md
// 6) and - for the defend squads - their mines / tripwires (pmcCharges.ts). This file is the post choice:
//   - candidates (world xyz, layer, facing yaw from the analysis): ambush_exit = a hiding spot at a chokepoint (its `place`),
//     overwatch_roof = a roof spot, window = a window's stand point (60 cm inside) looking over a line, hiding_spot = a hiding spot off
//     the chokepoints;
//   - never inside a chokepoint outline (+ margin), never within min_from_start_m of the player start, never within route_clear_m of the
//     mandatory route on the post's own layer, never farther than max_from_route_m from it (a post the squad never meets is useless);
//   - seeded: the count in count_by_factor[expansion factor], each post a weighted draw (kind weight x quality), squad_spacing_m between
//     posts and one post per building; posture defend / hunt by posture_weights; squad size per kind.
// Constants: Content/Data/LevelGen/levelgen_v2.json `pmc_posts`. Pure and deterministic: same layout + tactical map + seed -> same plan.

import cfgJson from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { layerZ } from './missionGoals.ts';
import { PropsRng, streamSeed } from './props/rng.ts';
import type { GenLayout } from './schema.ts';
import { mandatoryRoute } from './schema.ts';
import type { TacticalMapFile, Vec3 } from './tacticalMap.ts';

export const PMC_PLAN_VERSION = 1;
export const PMC_POST_KINDS = ['ambush_exit', 'overwatch_roof', 'window', 'hiding_spot'] as const;
export type PmcPostKind = (typeof PMC_POST_KINDS)[number];
export const PMC_POSTURES = ['defend', 'hunt'] as const;
export type PmcPosture = (typeof PMC_POSTURES)[number];

export interface PmcChargesConfig {
  sappers_per_squad: number;
  per_sapper: { mines: number; tripwires: number };
  mine_spacing_m: number; first_mine_m: number; flank_m: number; retreat_m: number; retreat_clear_m: number;
  route_clear_m: number; start_clear_m: number; access_offset_m: number; door_inside_cm: number; tripwire_width_cm: number;
  mine: { variant: string; trigger: string }; tripwire: { variant: string; trigger: string };
  layer_id: string;
}
export interface PmcPostsConfig {
  count_by_factor: Record<string, [number, number]>;
  kinds: Record<PmcPostKind, { weight: number; squad: number }>;
  posture_weights: Record<PmcPosture, number>;
  delay_s: Record<PmcPosture, number>;
  weights_file: string;
  min_from_start_m: number; route_clear_m: number; max_from_route_m: number; chokepoint_margin_m: number; squad_spacing_m: number;
  charges: PmcChargesConfig;
}
export const PMC_POSTS: PmcPostsConfig = (cfgJson as unknown as { pmc_posts: PmcPostsConfig }).pmc_posts;

/** One generated PMC post = one squad (carried into the level JSON `pmc_squads` by build_levelgen_map.py). World cm. */
export interface PmcPost {
  id: string;
  kind: PmcPostKind;
  posture: PmcPosture;
  /** Squad size (level JSON pmc_squads.count). */
  count: number;
  delay_s: number;
  layer: string;
  /** Where the squad stands (world xyz) and looks (UE yaw, integer degrees). */
  pos: Vec3;
  yaw: number;
  /** The tactical-map item the post stands on: hiding spot id / window id / "<building>#s<n>" roof spot. */
  source: string;
  /** The building of a roof / window post; the chokepoint of an ambush_exit post. */
  building?: string;
  place?: string;
  /** The draw weight (kind weight x (0.5 + 0.5 x quality)). */
  score: number;
  /** The attack anchor: the mandatory-route point nearest the post (where the attackers come from), its layer and distance (m). */
  anchor: Vec3;
  anchor_layer: string;
  route_m: number;
}

export type PmcChargeRole = 'approach' | 'doorway' | 'flank' | 'roof_access';
/** One PMC-owned charge (a hostile trap of a defend post) - becomes an object of the props layer `charges.layer_id`. */
export interface PmcCharge {
  id: string;
  post: string;
  role: PmcChargeRole;
  type: 'mine' | 'tripwire';
  variant: string;
  trigger: string;
  pos: Vec2;
  z: number;
  walk_layer: string;
  yaw: number;
  door?: string;
  width_cm?: number;
}

export interface PmcPostsReport {
  candidates: Record<PmcPostKind, number>;
  rejected: { chokepoint: number; start: number; route: number; far: number };
  wanted: number;
  placed: number;
}
export interface PmcPlan {
  version: number;
  seed: number;
  /** The tactical map's geometry_hash the plan was made for (another hash = the map changed: re-roll the posts). */
  geometry_hash: string;
  posts: PmcPost[];
  charges: PmcCharge[];
  report: PmcPostsReport & { charges?: Record<string, unknown> };
}

// ---- geometry ------------------------------------------------------------------------------------------------------------------

const dist2 = (a: readonly number[], b: readonly number[]): number => Math.hypot(a[0] - b[0], a[1] - b[1]);
const r0 = (v: number): number => { const x = Math.round(v); return Object.is(x, -0) ? 0 : x; };
const r2 = (v: number): number => Math.round(v * 100) / 100;

/** Distance from p to the segment ab, the closest point and its parameter. */
export function segDist(p: readonly number[], a: readonly number[], b: readonly number[]): { d: number; q: Vec2; t: number } {
  const dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
  const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2));
  const q: Vec2 = [a[0] + dx * t, a[1] + dy * t];
  return { d: dist2(p, q), q, t };
}

export function pointInPoly(p: readonly number[], poly: readonly (readonly number[])[]): boolean {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i], b = poly[j];
    if (a[1] > p[1] !== b[1] > p[1] && p[0] < ((b[0] - a[0]) * (p[1] - a[1])) / (b[1] - a[1]) + a[0]) inside = !inside;
  }
  return inside;
}

/** Inside a chokepoint outline or within `marginCm` of its edge. */
export function inChokepoint(p: readonly number[], polys: readonly (readonly (readonly number[])[])[], marginCm: number): boolean {
  for (const poly of polys) {
    if (poly.length < 3) continue;
    if (pointInPoly(p, poly)) return true;
    for (let i = 0; i < poly.length; i++) if (segDist(p, poly[i], poly[(i + 1) % poly.length]).d < marginCm) return true;
  }
  return false;
}

/** The player start: objectives.start (the designer's placed start) else the spawn (build_levelgen_map.py start_of). */
export function startOf(l: GenLayout): Vec2 {
  const s = l.objectives?.start?.pos;
  return s && s.length === 2 ? [s[0], s[1]] : [l.spawn.pos[0], l.spawn.pos[1]];
}

export interface RouteSeg { a: Vec2; b: Vec2; la: string; lb: string }
/** The mandatory route as segments with the layers of their end points (default ground). */
export function mandatorySegments(l: GenLayout): RouteSeg[] {
  const r = mandatoryRoute(l);
  if (!r) return [];
  const pts = r.points as Vec2[], layers = r.layers ?? [];
  const out: RouteSeg[] = [];
  for (let i = 1; i < pts.length; i++) out.push({ a: pts[i - 1], b: pts[i], la: layers[i - 1] ?? 'ground', lb: layers[i] ?? 'ground' });
  if (pts.length === 1) out.push({ a: pts[0], b: pts[0], la: layers[0] ?? 'ground', lb: layers[0] ?? 'ground' });
  return out;
}

/** Distance to the mandatory route on the given layer (segments touching it); Infinity when the route never uses that layer. */
export function routeDistOnLayer(p: readonly number[], segs: readonly RouteSeg[], layer: string): number {
  let best = Infinity;
  for (const s of segs) if (s.la === layer || s.lb === layer) best = Math.min(best, segDist(p, s.a, s.b).d);
  return best;
}

/** The nearest mandatory-route point on any layer (xy), its layer and the distance. */
export function routeAnchor(p: readonly number[], segs: readonly RouteSeg[]): { q: Vec2; layer: string; d: number } {
  let best = { q: [p[0], p[1]] as Vec2, layer: 'ground', d: Infinity };
  for (const s of segs) {
    const h = segDist(p, s.a, s.b);
    if (h.d < best.d) best = { q: h.q, layer: h.t < 0.5 ? s.la : s.lb, d: h.d };
  }
  return best;
}

/** The expansion factor of the map (1 = not expanded). */
export const expansionFactor = (l: GenLayout): number => {
  const steps = l.expansion?.steps ?? [];
  return steps.length ? Number(steps[steps.length - 1].factor) || 1 : 1;
};

// ---- candidates ----------------------------------------------------------------------------------------------------------------

export interface PmcCandidate {
  kind: PmcPostKind; source: string; layer: string; pos: Vec3; yaw: number; quality: number; building?: string; place?: string;
}

const normYaw = (y: number): number => { let v = Math.round(y) % 360; if (v > 180) v -= 360; if (v <= -180) v += 360; return v; };

/** Every strategic place of the tactical map as a post candidate (deterministic order: kind, then source id). */
export function postCandidates(t: TacticalMapFile): PmcCandidate[] {
  const out: PmcCandidate[] = [];
  const maxRoof = Math.max(1, ...t.overwatch_roofs.flatMap((r) => r.spots.map((s) => s.covered_m)));
  const maxWin = Math.max(1, ...t.windows.map((w) => w.covered_m));
  for (const h of t.hiding_spots) {
    const quality = 1 - Math.min(1, h.reveal_normal_m / 30);
    out.push({ kind: h.place ? 'ambush_exit' : 'hiding_spot', source: h.id, layer: h.layer, pos: h.pos, yaw: normYaw(h.facing_yaw), quality, ...(h.place ? { place: h.place } : {}) });
  }
  for (const r of t.overwatch_roofs) {
    r.spots.forEach((s, i) => out.push({ kind: 'overwatch_roof', source: `${r.building}#s${i}`, layer: r.layer, pos: s.pos, yaw: normYaw(s.facing_yaw), quality: s.covered_m / maxRoof, building: r.building }));
  }
  for (const w of t.windows) {
    out.push({ kind: 'window', source: w.id, layer: w.layer, pos: w.stand, yaw: normYaw(w.yaw), quality: w.covered_m / maxWin, building: w.building });
  }
  const order = (k: PmcPostKind) => PMC_POST_KINDS.indexOf(k);
  return out.sort((a, b) => order(a.kind) - order(b.kind) || (a.source < b.source ? -1 : a.source > b.source ? 1 : 0));
}

// ---- the choice ------------------------------------------------------------------------------------------------------------------

export interface PlanPostsOptions { config?: PmcPostsConfig; /** Force the post count (tests / the page); limited by the candidates. */ count?: number }

const drawWeighted = (rng: PropsRng, weights: readonly number[]): number => {
  const total = weights.reduce((s, w) => s + w, 0);
  if (total <= 0) return -1;
  let u = (rng.nextU32() / 4294967296) * total;
  for (let i = 0; i < weights.length; i++) { if (weights[i] <= 0) continue; u -= weights[i]; if (u < 0) return i; }
  for (let i = weights.length - 1; i >= 0; i--) if (weights[i] > 0) return i;
  return -1;
};

/** Seeded choice of the PMC posts (see the file header). The plan's `charges` stay empty (pmcCharges.ts fills them). */
export function planPmcPosts(l: GenLayout, t: TacticalMapFile, seed: number, o: PlanPostsOptions = {}): PmcPlan {
  const c = o.config ?? PMC_POSTS;
  const rng = new PropsRng(streamSeed(seed, 'pmc_posts'));
  const start = startOf(l), segs = mandatorySegments(l);
  const polys = t.chokepoints.map((cp) => cp.poly);
  const report: PmcPostsReport = {
    candidates: { ambush_exit: 0, overwatch_roof: 0, window: 0, hiding_spot: 0 }, rejected: { chokepoint: 0, start: 0, route: 0, far: 0 }, wanted: 0, placed: 0,
  };

  const pool: { c: PmcCandidate; w: number; anchor: ReturnType<typeof routeAnchor> }[] = [];
  for (const cand of postCandidates(t)) {
    const kc = c.kinds[cand.kind];
    if (!kc || kc.weight <= 0) continue;
    report.candidates[cand.kind]++;
    if (inChokepoint(cand.pos, polys, c.chokepoint_margin_m * 100)) { report.rejected.chokepoint++; continue; }
    if (dist2(cand.pos, start) < c.min_from_start_m * 100) { report.rejected.start++; continue; }
    if (routeDistOnLayer(cand.pos, segs, cand.layer) < c.route_clear_m * 100) { report.rejected.route++; continue; }
    const anchor = routeAnchor(cand.pos, segs);
    if (anchor.d > c.max_from_route_m * 100) { report.rejected.far++; continue; }
    pool.push({ c: cand, w: kc.weight * (0.5 + 0.5 * Math.max(0, Math.min(1, cand.quality))), anchor });
  }

  const [lo, hi] = c.count_by_factor[String(expansionFactor(l))] ?? c.count_by_factor['1'] ?? [1, 1];
  const wanted = o.count ?? lo + rng.nextBelow(Math.max(0, hi - lo) + 1);
  report.wanted = wanted;

  const posts: PmcPost[] = [];
  const weights = pool.map((p) => p.w);
  const usedBuildings = new Set<string>();
  const postureKeys = PMC_POSTURES.filter((k) => (c.posture_weights[k] ?? 0) > 0);
  while (posts.length < wanted) {
    const i = drawWeighted(rng, weights);
    if (i < 0) break;
    weights[i] = 0;
    const p = pool[i];
    if (p.c.building && usedBuildings.has(p.c.building)) continue;
    if (posts.some((q) => dist2(q.pos, p.c.pos) < c.squad_spacing_m * 100)) continue;
    const posture: PmcPosture = postureKeys[Math.max(0, drawWeighted(rng, postureKeys.map((k) => c.posture_weights[k])))] ?? 'defend';
    if (p.c.building) usedBuildings.add(p.c.building);
    const anchorZ = layerZ(l, p.anchor.layer);
    posts.push({
      id: `PMC_${String(posts.length + 1).padStart(2, '0')}`, kind: p.c.kind, posture, count: c.kinds[p.c.kind].squad, delay_s: c.delay_s[posture] ?? 0,
      layer: p.c.layer, pos: p.c.pos, yaw: p.c.yaw, source: p.c.source,
      ...(p.c.building ? { building: p.c.building } : {}), ...(p.c.place ? { place: p.c.place } : {}),
      score: r2(p.w), anchor: [r0(p.anchor.q[0]), r0(p.anchor.q[1]), r0(anchorZ)], anchor_layer: p.anchor.layer, route_m: Math.round(p.anchor.d) / 100,
    });
  }
  report.placed = posts.length;
  return { version: PMC_PLAN_VERSION, seed: seed >>> 0, geometry_hash: t.geometry_hash, posts, charges: [], report };
}
