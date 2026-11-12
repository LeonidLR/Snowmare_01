// Training Ground / level generator: the EVALUATION of a candidate (docs/design/levelgen/LEVEL_GENERATOR.md section 4), 0 tokens:
//   (1) the mandatory route (and the variants) must be completable: every leg is pathed on its floor LAYER with the lab's nav
//       grid (walls widened by the agent radius) and every layer change goes through an existing link (ladder / stair / hole /
//       crossing) whose two ends the route touches - the first failure names the leg and why;
//   (2) mechanic coverage: the formation simulator walks the mandatory route's ground legs with 4 men and the share of time per
//       terrain class (open / corridor / wall_run / gap_run / corner / wall_end) and per formation is counted, plus corner
//       watches, wall layouts, doorways crossed, ladder / stair / hole uses, roofs / floors visited;
//   (3) sightlines: along the route the longest free line of sight per sample (36 rays against the layer's walls + the tall
//       covers) -> p50 / p90 / max, the share of short-sightline samples (< 40 m), the number of lanes >= 80 m; on a ROOF the eye
//       stands at the roof's level + 1.6 m and only obstacles reaching that height block it (ELEVATED vision); the view from every
//       walkable roof is reported on its own (roofViews: the halls on +3 = 9 m see over the 6-9 m blocks);
//   (4) ambush-spot quality (the AmbushSpotRules ideas): the layout's ambush points that are 14-30 m from the route AND concealed
//       from the nearest route sample, per 100 m of route, and how many route samples a coordinated plan (3+ concealed spots in
//       3+ sectors) could be laid round;
//   (5) profile similarity: every generator target of the reference profile scored 1 inside its range, decaying outside.
//   (6) kill zones: a route the designer marked intent 'kill_zone' (V3 along the cut floor) is reported as INTENDED - how far it
//       runs through the killing ground and how many ways out the ground has - never as a defect (game designer 2026-11-02).
// The report is <= 30 lines (the only thing an agent reads); the full Evaluation JSON feeds the department page and the critics.

import type { FormationConfig } from '../movement/config.ts';
import type { Segment, Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dirOf, dist, pointInPoly, raycastWalls, segmentBlocked, sub, yawOf } from '../movement/geometry.ts';
import type { Formation, Layout, LayoutRoute } from '../movement/layout.ts';
import { wallSegments } from '../movement/layout.ts';
import { AGENT_RADIUS, NavGrid } from '../movement/nav.ts';
import type { TerrainClass } from '../movement/rules.ts';
import { simulate } from '../movement/sim.ts';
import type { Profile } from './profile.ts';
import { targetScore } from './profile.ts';
import type { GenLayout, GenRoute, RouteIntent } from './schema.ts';
import { layerLevel, layersOf, levelsOf, levelZ, mandatoryRoute, NEVER_CLIMBABLE, rect, toFormationLayout } from './schema.ts';
import type { BlockedOpening, BuildingTacticalReport, FiringLine, TacticalReport } from './tacticalEvaluate.ts';
import { evaluateTactical } from './tacticalEvaluate.ts';

export type { BlockedOpening, BuildingTacticalReport, FiringLine, TacticalReport };

export interface LegReport { index: number; layer: string; ok: boolean; reason?: string; detour_cm?: number; link?: string }
export interface RouteReport { id: string; mandatory: boolean; completable: boolean; legs: LegReport[]; length_m: number; fail?: string; layers: string[]; intent?: RouteIntent; intent_note?: string }
/** The view from one walkable roof (elevated eye: the roof's level + 1.6 m; only obstacles reaching that height block). */
export interface RoofView { id: string; level: number; longest_m: number; mean_m: number; open_share: number }
/** A route the designer marked as a deliberate kill zone, reported as intended. */
export interface KillZoneReport { route: string; intended: true; note: string; in_zone_m: number; share: number; ways_out: number; completable: boolean }

export interface Evaluation {
  layout: string;
  seed: number;
  profile: string;
  route: RouteReport;
  variants: RouteReport[];
  mechanics: {
    terrain: Partial<Record<TerrainClass, number>>;
    formation: Partial<Record<Formation, number>>;
    cornerWatches: number;
    wallLayouts: number;
    doorways: number;
    ladderUses: number;
    stairUses: number;
    holeUses: number;
    roofs: number;
    floors: number;
    /** The height levels (3 m steps) the mandatory route visits, lowest first, and the share of its points per level. */
    levels: number[];
    levelShare: Record<string, number>;
    simSeconds: number;
  };
  sightlines: { p50_m: number; p90_m: number; max_m: number; short_share: number; lanes_over_80m: number; samples: number; open_p50_m: number };
  /** The view from every walkable roof, best first (the elevated-vision check of the raised hall roofs). */
  roofViews: RoofView[];
  /** Routes marked intent 'kill_zone': intended exposure, not a defect. */
  killZones: KillZoneReport[];
  ambush: { points: number; concealed: number; per100m: number; coordinatedSamples: number; coordinatedShare: number };
  counts: Record<string, number>;
  similarity: { score: number; metrics: { key: string; value: number; target?: [number, number]; score: number }[] };
  tactical: TacticalReport;
  score: number;
  summary: string[];
}

export interface EvaluateOptions {
  layout: GenLayout;
  profile: Profile;
  config: FormationConfig;
  /** Members on the simulator walk (default 4) and its seed (default 1). */
  members?: number;
  simSeed?: number;
  /** Skip the simulator walk (fast structural checks only). */
  noSim?: boolean;
}

const SHORT_LOS_M = 40;
const LANE_M = 80;
const SAMPLE_CM = 200;
const RAYS = 36;
const MAX_LOS_CM = 20000;
/** A link's two ends must lie within this of the leg's ends (a cut crossing is a 5 m ramp; a ladder's foot / top points). */
const LINK_REACH_CM = 500;
/** A standing man's eye above the surface he stands on, cm (the elevated-vision rule). */
const EYE_CM = 160;
/** A roof ray counts as OPEN when it runs at least this far (m). */
const ROOF_OPEN_M = 60;

export function evaluateLayout(o: EvaluateOptions): Evaluation {
  const l = o.layout;
  const layers = layersOf(l);
  const flat = new Map<string, Layout>();
  const nav = new Map<string, NavGrid>();
  const layerLayout = (layer: string): Layout => {
    let f = flat.get(layer);
    if (!f) { f = toFormationLayout(l, layer); flat.set(layer, f); }
    return f;
  };
  const layerNav = (layer: string): NavGrid => {
    let g = nav.get(layer);
    if (!g) { const f = layerLayout(layer); g = new NavGrid(wallSegments(f), f.bounds.min, f.bounds.max); nav.set(layer, g); }
    return g;
  };
  // tall covers block sight (wrecks, silos, boilers); low ones (jersey, sandbags, barrels, crates) do not block a standing man's view
  const tallCoverSegs: Segment[] = l.covers.filter((c) => c.size[2] >= 140).flatMap(coverSegs);
  // ELEVATED vision: every obstacle with the height of its top (walls on their level, buildings, covers); an eye on a roof is blocked
  // only by what reaches its height (a wall below the eye does not stop the line - the in-game vision cone draws the dead zone
  // behind a roof's edge, the metric keeps the longest line). Chimneys (20 m) and silos (12 m) block even the +3 roofs.
  const tops: { seg: Segment; top: number }[] = [
    ...l.walls.map((w) => ({ seg: { a: w.a, b: w.b, thickness: w.thickness }, top: levelZ(w.floor ?? 0) + w.height })),
    ...l.covers.flatMap((c) => coverSegs(c).map((seg) => ({ seg, top: c.size[2] }))),
  ];
  const elevatedWalls = new Map<number, Segment[]>();
  const blockersAbove = (eyeZ: number): Segment[] => {
    let s = elevatedWalls.get(eyeZ);
    if (!s) { s = tops.filter((o) => o.top >= eyeZ).map((o) => o.seg); elevatedWalls.set(eyeZ, s); }
    return s;
  };
  const sightWalls = (layer: string): Segment[] => {
    if (layer.startsWith('roof:')) return blockersAbove(levelZ(layerLevel(l, layer)) + EYE_CM);
    return [...wallSegments(layerLayout(layer)), ...(layer === 'ground' ? tallCoverSegs : [])];
  };

  const routes = l.routes.map((r) => checkRoute(l, r, layers, layerNav));
  const main = routes.find((r) => r.mandatory) ?? routes[0];
  const variants = routes.filter((r) => r !== main);
  const mandatory = mandatoryRoute(l);

  // ---- mechanics: the simulator on the ground legs of the mandatory route (the lab's rules, 4 men) -----------------------
  const mech: Evaluation['mechanics'] = { terrain: {}, formation: {}, cornerWatches: 0, wallLayouts: 0, doorways: 0, ladderUses: 0, stairUses: 0, holeUses: 0, roofs: 0, floors: 0, levels: [], levelShare: {}, simSeconds: 0 };
  const mlayers = mandatory.layers ?? mandatory.points.map(() => 'ground');
  const mlevels = mlayers.map((x) => (layers.includes(x) ? layerLevel(l, x) : 0));
  mech.levels = [...new Set(mlevels)].sort((a, b) => a - b);
  for (const lv of mlevels) mech.levelShare[String(lv)] = round((mech.levelShare[String(lv)] ?? 0) + 1 / mlevels.length, 3);
  if (!o.noSim && main.completable) {
    const ground = layerLayout('ground');
    const legs = ground.routes.filter((r) => r.id === mandatory.id || r.id.startsWith(`${mandatory.id}_L`));
    const terrainT = new Map<string, number>(), formT = new Map<string, number>();
    let total = 0, prevCorner = new Set<string>(), prevWall = false;
    for (const leg of legs) {
      const route: LayoutRoute = { ...leg, pace: 'walk' };
      mech.simSeconds += simulate({
        layout: ground, route, memberCount: o.members ?? 4, config: o.config, seed: o.simSeed ?? 1, nav: layerNav('ground'), haltSeconds: 1,
        onFrame: (f) => {
          total += 1;
          terrainT.set(f.terrain, (terrainT.get(f.terrain) ?? 0) + 1);
          formT.set(f.formation, (formT.get(f.formation) ?? 0) + 1);
          const now = new Set(f.members.filter((m) => m.cornerWatch).map((m) => m.role));
          for (const r of now) if (!prevCorner.has(r)) mech.cornerWatches++;
          prevCorner = now;
          const wall = f.formation === 'wall';
          if (wall && !prevWall) mech.wallLayouts++;
          prevWall = wall;
        },
      });
    }
    for (const [k, v] of terrainT) mech.terrain[k as TerrainClass] = round(v / Math.max(1, total), 3);
    for (const [k, v] of formT) mech.formation[k as Formation] = round(v / Math.max(1, total), 3);
  }
  // doorways crossed by the mandatory route (a door within 120 cm of the polyline), links by kind, levels visited
  const doors = l.buildings.flatMap((b) => b.doors);
  mech.doorways = doors.filter((d) => nearestDist(d.pos, mandatory.points) < 120).length;
  for (const k of mandatory.links ?? []) {
    if (l.ladders.some((x) => x.id === k.via)) mech.ladderUses++;
    else if (l.buildings.some((b) => b.stairs.some((s) => s.id === k.via))) mech.stairUses++;
    else if (k.via.startsWith('hole')) mech.holeUses++;
  }
  mech.roofs = new Set(mlayers.filter((x) => x.startsWith('roof:'))).size;
  mech.floors = new Set(mlayers.filter((x) => x.startsWith('floor:'))).size;

  // ---- sightlines along the mandatory route -----------------------------------------------------------------------------
  // per sample: the MEAN free ray length (how open the place feels: p50 / p90 / the short share) and the LONGEST ray (the lanes)
  const samples = sampleRoute(mandatory, mlayers, SAMPLE_CM);
  const losMean: number[] = [], losMax: number[] = [];
  const { min: bmin, max: bmax } = l.bounds;
  // a ray ends at the map's edge at the latest (the ground plane ends there)
  const toEdge = (p: Vec2, dv: Vec2): number => {
    let t = MAX_LOS_CM;
    if (dv[0] > 1e-9) t = Math.min(t, (bmax[0] - p[0]) / dv[0]); else if (dv[0] < -1e-9) t = Math.min(t, (bmin[0] - p[0]) / dv[0]);
    if (dv[1] > 1e-9) t = Math.min(t, (bmax[1] - p[1]) / dv[1]); else if (dv[1] < -1e-9) t = Math.min(t, (bmin[1] - p[1]) / dv[1]);
    return Math.max(0, t);
  };
  const rays = (pos: Vec2, walls: Segment[]): number[] => {
    const out: number[] = [];
    for (let k = 0; k < RAYS; k++) {
      const dirv = dirOf((k * 360) / RAYS);
      const edge = toEdge(pos, dirv);
      const d = raycastWalls(pos, dirv, walls, edge);
      out.push(Number.isFinite(d) ? d : edge);
    }
    return out;
  };
  for (const s of samples) {
    const r = rays(s.pos, sightWalls(s.layer));
    losMean.push(r.reduce((a, b) => a + b, 0) / RAYS / 100); losMax.push(Math.max(...r) / 100);
  }
  // p50 / p90 / short share from the longest ray (how far the squad can see / be seen); the mean ray = openness
  const sorted = [...losMax].sort((a, b) => a - b);
  const q = (p: number) => (sorted.length ? sorted[Math.min(sorted.length - 1, Math.floor(p * sorted.length))] : 0);
  // a lane = a run of >= 12 samples (24 m) whose longest ray is >= LANE_M, gaps of < 5 samples merged
  let lanes = 0, run = 0, gap = 0;
  for (const v of losMax) {
    if (v >= LANE_M) { run++; gap = 0; } else if (run && ++gap >= 5) { if (run >= 12) lanes++; run = 0; gap = 0; }
  }
  if (run >= 12) lanes++;
  const meanSorted = [...losMean].sort((a, b) => a - b);
  const sight = { p50_m: round(q(0.5), 1), p90_m: round(q(0.9), 1), max_m: round(Math.max(0, ...losMax), 1), short_share: round(losMax.filter((v) => v < SHORT_LOS_M).length / Math.max(1, losMax.length), 3), lanes_over_80m: lanes, samples: losMax.length,
    open_p50_m: round(meanSorted[Math.floor(meanSorted.length / 2)] ?? 0, 1) };

  // ---- the view from every walkable roof: the centre + four points 1.5 m inside the corners, 36 rays each --------------------
  const roofViews: RoofView[] = l.buildings.filter((b) => b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece)).map((b) => {
    const level = layerLevel(l, `roof:${b.id}`);
    const walls = blockersAbove(levelZ(level) + EYE_CM);
    const [[x0, y0], [x1, y1]] = b.footprint;
    const inset = Math.min(150, (x1 - x0) / 4, (y1 - y0) / 4);
    const pts: Vec2[] = [rect.centre(b.footprint), [x0 + inset, y0 + inset], [x1 - inset, y0 + inset], [x0 + inset, y1 - inset], [x1 - inset, y1 - inset]];
    const all = pts.flatMap((p) => rays(p, walls));
    return { id: b.id, level, longest_m: round(Math.max(...all) / 100, 1), mean_m: round(all.reduce((a, v) => a + v, 0) / all.length / 100, 1),
      open_share: round(all.filter((v) => v >= ROOF_OPEN_M * 100).length / all.length, 3) };
  }).sort((a, b) => b.mean_m - a.mean_m);

  // ---- kill zones the designer marked: intended exposure (how far through the killing ground, how many ways out) ------------
  const killPolys = l.zones.filter((z) => z.intent === 'kill_zone').map((z) => z.poly);
  const waysOut = (l.cut?.crossings.filter((c) => c.kind === 'ramp').length ?? 0) + l.ladders.filter((x) => x.from === 'cut').length;
  const killZones: KillZoneReport[] = l.routes.filter((r) => r.intent === 'kill_zone').map((r) => {
    const lay = r.layers ?? r.points.map(() => 'ground');
    const smp = sampleRoute(r, lay, SAMPLE_CM);
    const inZone = smp.filter((s) => s.layer === 'cut' || killPolys.some((poly) => pointInPoly(s.pos, poly))).length;
    const rep = routes.find((x) => x.id === r.id);
    return { route: r.id, intended: true, note: r.intent_note ?? 'marked kill_zone by the designer', in_zone_m: round((inZone * SAMPLE_CM) / 100), share: round(inZone / Math.max(1, smp.length), 3),
      ways_out: waysOut, completable: rep?.completable ?? false };
  });

  // ---- ambush spots: concealed from the nearest route sample, 14-30 m off it ---------------------------------------------
  const groundWalls = sightWalls('ground');
  const routeLen = polyLength(mandatory.points) / 100;
  let concealed = 0;
  const concealedPts: Vec2[] = [];
  for (const a of l.ambush_points) {
    const near = samples.filter((s) => s.layer === 'ground').reduce((m, s) => (dist(s.pos, a.pos) < dist(m.pos, a.pos) ? s : m), samples[0]);
    const dm = dist(near.pos, a.pos) / 100;
    if (dm < 14 || dm > 30) continue;
    if (!segmentBlocked(near.pos, a.pos, groundWalls, 0)) continue;
    concealed++;
    concealedPts.push(a.pos);
  }
  let coordinated = 0;
  const coarse = samples.filter((s, i) => s.layer === 'ground' && i % 5 === 0);
  for (const s of coarse) {
    const sectors = new Set<string>();
    for (const p of concealedPts) {
      const dm = dist(s.pos, p) / 100;
      if (dm < 10 || dm > 30) continue;
      const rel = ((yawOf(sub(p, s.pos)) - s.heading + 540) % 360) - 180;
      sectors.add(Math.abs(rel) <= 45 ? 'front' : Math.abs(rel) >= 135 ? 'rear' : rel > 0 ? 'right' : 'left');
    }
    if (sectors.size >= 3) coordinated++;
  }
  const ambush = { points: l.ambush_points.length, concealed, per100m: round((concealed / Math.max(1, routeLen)) * 100, 2), coordinatedSamples: coordinated, coordinatedShare: round(coordinated / Math.max(1, coarse.length), 3) };

  // ---- counts + similarity ------------------------------------------------------------------------------------------------
  const area_m2 = ((l.bounds.max[0] - l.bounds.min[0]) / 100) * ((l.bounds.max[1] - l.bounds.min[1]) / 100);
  const counts: Record<string, number> = {
    buildings: l.buildings.filter((b) => b.set_piece !== 'tower' && b.set_piece !== 'chimney').length,
    towers: l.buildings.filter((b) => b.set_piece === 'tower').length,
    chimneys: l.buildings.filter((b) => b.set_piece === 'chimney').length,
    enterable_buildings: l.buildings.filter((b) => b.enterable).length,
    roofs_walkable: l.buildings.filter((b) => b.roof_walkable && b.set_piece !== 'tower').length,
    stairwells: l.buildings.filter((b) => b.stairs.some((s) => s.kind === 'stairwell')).length,
    external_stairs: l.buildings.reduce((n, b) => n + b.stairs.filter((s) => s.kind === 'external').length, 0),
    ladders: l.ladders.length,
    doorways_all_routes: doors.filter((d) => l.routes.some((r) => nearestDist(d.pos, r.points) < 120)).length,
    covers: l.covers.length,
    cover_per_100m2_mean: round((l.covers.length / area_m2) * 100 * 1.7, 2), // a greybox cover object ~ 1.7 of the reference's small covers
    walls_m: round(l.walls.filter((w) => (w.floor ?? 0) === 0 && w.kind !== 'fence').reduce((n, w) => n + dist(w.a, w.b) / 100, 0)),
    fences_m: round(l.walls.filter((w) => w.kind === 'fence').reduce((n, w) => n + dist(w.a, w.b) / 100, 0)),
    route_length_m: round(routeLen),
    variants: l.routes.length - 1,
    cut_crossings: l.cut?.crossings.length ?? 0,
    height_levels: levelsOf(l).length,
    route_levels: mech.levels.length,
    long_lanes_over_80m: sight.lanes_over_80m,
    short_zone_share: sight.short_share,
    ambush_points: l.ambush_points.length,
    patrol_routes: l.patrol_routes.length,
    spawn_points: l.spawn_points.length,
    area_ha: round(area_m2 / 10000, 2),
  };
  const metrics = Object.entries(o.profile.targets).map(([key, target]) => ({ key, value: counts[key] ?? 0, target, score: round(targetScore(counts[key] ?? 0, target), 3) }));
  const similarity = { score: round(metrics.reduce((s, m) => s + m.score, 0) / Math.max(1, metrics.length), 3), metrics };

  const terrainClasses = Object.values(mech.terrain).filter((v) => (v ?? 0) >= 0.05).length;
  const tactical = evaluateTactical(l, samples, coverSegs, tops);
  const score = round(0.4 * (main.completable ? 1 : 0) + 0.2 * similarity.score + 0.2 * Math.min(1, terrainClasses / 5) + 0.2 * Math.min(1, ambush.per100m / 3), 3);

  const ev: Evaluation = { layout: l.name, seed: l.seed, profile: l.profile, route: main, variants, mechanics: mech, sightlines: sight, roofViews, killZones, ambush, counts, similarity, tactical, score, summary: [] };
  ev.summary = renderSummary(ev);
  return ev;
}

/** Every leg of a route pathed on its layer; layer changes need a link the route touches at both ends. */
function checkRoute(l: GenLayout, r: GenRoute, layers: string[], layerNav: (layer: string) => NavGrid): RouteReport {
  const lay = r.layers ?? r.points.map(() => 'ground');
  const legs: LegReport[] = [];
  let fail: string | undefined;
  const linkAt = new Map((r.links ?? []).map((k) => [k.at, k.via]));
  for (let i = 1; i < r.points.length; i++) {
    const a = r.points[i - 1], b = r.points[i];
    const la = lay[i - 1], lb = lay[i];
    if (!layers.includes(la) || !layers.includes(lb)) { legs.push({ index: i, layer: lb, ok: false, reason: `unknown layer "${layers.includes(la) ? lb : la}"` }); fail ??= `leg ${i}: unknown layer`; continue; }
    if (la !== lb) {
      const via = linkAt.get(i);
      const ends = via ? linkEnds(l, via) : null;
      if (!via) { legs.push({ index: i, layer: lb, ok: false, reason: `layer ${la} -> ${lb} without a link` }); fail ??= `leg ${i}: no link ${la} -> ${lb}`; continue; }
      if (!ends) { legs.push({ index: i, layer: lb, ok: false, reason: `link "${via}" does not exist`, link: via }); fail ??= `leg ${i}: missing link ${via}`; continue; }
      const touch = Math.min(dist(a, ends[0]), dist(a, ends[1])) < LINK_REACH_CM && Math.min(dist(b, ends[0]), dist(b, ends[1])) < LINK_REACH_CM;
      if (!touch) { legs.push({ index: i, layer: lb, ok: false, reason: `link "${via}" is not at both ends of the leg`, link: via }); fail ??= `leg ${i}: link ${via} off the leg`; continue; }
      legs.push({ index: i, layer: lb, ok: true, link: via });
      continue;
    }
    const grid = layerNav(la);
    const inA = !grid.walkable(a), inB = !grid.walkable(b);
    if (inA || inB) { legs.push({ index: i, layer: la, ok: false, reason: `point ${inA ? i - 1 : i} inside a wall / off the layer` }); fail ??= `leg ${i}: point in a wall`; continue; }
    const path = grid.path(a, b);
    if (!path) { legs.push({ index: i, layer: la, ok: false, reason: 'unreachable: walls block the leg completely' }); fail ??= `leg ${i}: unreachable on ${la}`; continue; }
    const L = path.slice(1).reduce((acc, p, k) => acc + dist(path[k], p), 0);
    legs.push({ index: i, layer: la, ok: true, detour_cm: Math.round(L - dist(a, b)) });
  }
  return { id: r.id, mandatory: !!r.mandatory, completable: !fail, legs, length_m: round(polyLength(r.points) / 100), fail, layers: [...new Set(lay)],
    ...(r.intent ? { intent: r.intent, intent_note: r.intent_note } : {}) };
}

/** The two ends of a link: a ladder (pos / top), a stair (pos / top), a cut crossing (its polygon centre twice), a hole (its position twice). */
function linkEnds(l: GenLayout, via: string): [Vec2, Vec2] | null {
  const ladder = l.ladders.find((x) => x.id === via);
  if (ladder) return [ladder.pos, ladder.top];
  for (const b of l.buildings) {
    const s = b.stairs.find((x) => x.id === via);
    if (s) return [s.pos, s.top];
  }
  const x = l.cut?.crossings.find((c) => c.id === via);
  if (x) { const c = centroid(x.poly); return [c, c]; }
  const m = /^hole_(roof|floor)_(\d+)$/.exec(via);
  if (m) {
    for (const b of l.buildings) {
      const hs = m[1] === 'roof' ? b.roof_holes : b.floor_holes.map((h) => h.pos);
      const h = hs[Number(m[2])];
      if (h) return [h, h];
    }
  }
  return null;
}

function sampleRoute(r: GenRoute, layers: string[], step: number): { pos: Vec2; layer: string; heading: number }[] {
  const out: { pos: Vec2; layer: string; heading: number }[] = [];
  for (let i = 1; i < r.points.length; i++) {
    const a = r.points[i - 1], b = r.points[i], L = dist(a, b), heading = yawOf(sub(b, a));
    for (let s = 0; s < L; s += step) out.push({ pos: [a[0] + ((b[0] - a[0]) * s) / L, a[1] + ((b[1] - a[1]) * s) / L], layer: layers[i], heading });
  }
  out.push({ pos: r.points[r.points.length - 1], layer: layers[layers.length - 1], heading: out.length ? out[out.length - 1].heading : 0 });
  return out;
}

/** The four sides of a cover's footprint (yawed rectangle). */
function coverSegs(c: GenLayout['covers'][number]): Segment[] {
  const hx = c.size[0] / 2, hy = c.size[1] / 2, d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]];
  const corner = (sx: number, sy: number): Vec2 => [c.pos[0] + d[0] * hx * sx + r[0] * hy * sy, c.pos[1] + d[1] * hx * sx + r[1] * hy * sy];
  const p = [corner(1, 1), corner(-1, 1), corner(-1, -1), corner(1, -1)];
  return p.map((a, i) => ({ a, b: p[(i + 1) % 4], thickness: 0 }));
}

const polyLength = (pts: Vec2[]) => pts.slice(1).reduce((n, p, i) => n + dist(pts[i], p), 0);
const nearestDist = (p: Vec2, pts: Vec2[]) => pts.slice(1).reduce((m, b, i) => Math.min(m, dist(p, closestOnSegment(p, pts[i], b).point)), Infinity);
const centroid = (poly: Vec2[]): Vec2 => [poly.reduce((s, p) => s + p[0], 0) / poly.length, poly.reduce((s, p) => s + p[1], 0) / poly.length];
const round = (v: number, digits = 0) => { const k = 10 ** digits; const r = Math.round(v * k) / k; return Object.is(r, -0) ? 0 : r; };

/** The <= 30-line report. */
export function renderSummary(ev: Evaluation): string[] {
  const pct = (v?: number) => `${Math.round((v ?? 0) * 100)} %`;
  const lines = [
    `# Level generator - ${ev.layout} (profile ${ev.profile}, seed ${ev.seed})`,
    `score ${ev.score} | route ${ev.route.completable ? 'COMPLETABLE' : `FAILS at ${ev.route.fail}`} | ${ev.route.length_m} m, ${ev.route.legs.length} legs, layers ${ev.route.layers.join(' / ')}`,
    `variants: ${ev.variants.map((v) => `${v.id} ${v.completable ? 'ok' : `FAILS (${v.fail})`} ${v.length_m} m${v.intent === 'kill_zone' ? ' [kill zone by design]' : ''}`).join('; ') || 'none'}`,
    `terrain: ${Object.entries(ev.mechanics.terrain).map(([k, v]) => `${k} ${pct(v)}`).join(', ') || '(no sim)'}`,
    `formation: ${Object.entries(ev.mechanics.formation).map(([k, v]) => `${k} ${pct(v)}`).join(', ') || '(no sim)'}`,
    `mechanics: corner watches ${ev.mechanics.cornerWatches}, wall layouts ${ev.mechanics.wallLayouts}, doorways ${ev.mechanics.doorways}, ladders ${ev.mechanics.ladderUses}, stairs ${ev.mechanics.stairUses}, holes ${ev.mechanics.holeUses}, roofs ${ev.mechanics.roofs}, floors ${ev.mechanics.floors}; height levels on the route ${ev.mechanics.levels.map((v) => (v > 0 ? `+${v}` : String(v))).join(' / ') || '0'} (${Object.entries(ev.mechanics.levelShare).map(([k, v]) => `${k}: ${pct(v)}`).join(', ')})`,
    `sightlines (longest ray per route sample): p50 ${ev.sightlines.p50_m} m, p90 ${ev.sightlines.p90_m} m, max ${ev.sightlines.max_m} m, short (< ${SHORT_LOS_M} m) ${pct(ev.sightlines.short_share)}, lanes >= ${LANE_M} m: ${ev.sightlines.lanes_over_80m}, openness (mean ray) p50 ${ev.sightlines.open_p50_m} m`,
    `roof views (eye on the roof + 1.6 m, lower obstacles do not block): ${ev.roofViews.map((r) => `${r.id} +${r.level} mean ${r.mean_m} m / longest ${r.longest_m} m / open ${pct(r.open_share)}`).join('; ') || 'no walkable roof'}`,
    ...ev.killZones.map((k) => `kill zone (INTENDED, not a defect): ${k.route} ${k.in_zone_m} m on the killing ground (${pct(k.share)} of it), ${k.ways_out} ways out - ${k.note}`),
    `ambush: ${ev.ambush.concealed} / ${ev.ambush.points} spots concealed 14-30 m off the route (${ev.ambush.per100m} per 100 m), coordinated plan possible at ${pct(ev.ambush.coordinatedShare)} of the route`,
    `counts: ${Object.entries(ev.counts).map(([k, v]) => `${k} ${v}`).join(', ')}`,
    `similarity to the profile ${ev.similarity.score}: ${ev.similarity.metrics.map((m) => `${m.key} ${m.value}${m.target ? ` [${m.target[0]}..${m.target[1]}]` : ''}${m.score < 1 ? ` x${m.score}` : ''}`).join(', ')}`,
    `tactical ${ev.tactical.score} | rooms ${ev.tactical.roomsReachable}/${ev.tactical.roomsTotal} reachable | blocked windows ${ev.tactical.blockedWindows.length} | firing lines ${ev.tactical.firingLines.count} (max ${ev.tactical.firingLines.maxLength_m} m, ${ev.tactical.firingLines.openingsWithLos}/${ev.tactical.firingLines.openingsTotal} apertures) | route cover ${pct(ev.tactical.routeCover.share)} | multi-level routes ${ev.tactical.multiLevelRoutes.count} | mantle ${ev.tactical.mantle.valid}`,
  ];
  if (ev.tactical.unreachableRooms.length) lines.push(`  unreachable rooms: ${ev.tactical.unreachableRooms.slice(0, 6).join(', ')}`);
  if (ev.tactical.blockedWindows.length) lines.push(`  blocked apertures: ${ev.tactical.blockedWindows.slice(0, 4).map((w) => `${w.id} (${w.reason})`).join('; ')}`);
  const bad = ev.route.legs.filter((x) => !x.ok).slice(0, 5);
  for (const x of bad) lines.push(`  leg ${x.index} (${x.layer}): ${x.reason}`);
  return lines.slice(0, 30);
}

/** True when a point lies inside any building footprint (for tests / the UI). */
export const insideBuilding = (l: GenLayout, p: Vec2) => l.buildings.some((b) => rect.contains(b.footprint, p));
export { AGENT_RADIUS };
