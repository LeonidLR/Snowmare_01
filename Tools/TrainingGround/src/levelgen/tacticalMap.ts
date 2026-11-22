// Training Ground / level generator - the TACTICAL MAP for the game (docs/design/TACTICAL_MAP.md P1, designer 2026-11-17: "bots read
// the terrain"). The layout's danger analysis (`analysis`, chokepoints.ts analyzeLayout - LEVELGEN_LAYERS_REPORT.md §5-6) becomes the
// runtime file Content/Data/LevelGen/tactical/<Map>.tactical.json that the game reads (C++ UTacticalMapSubsystem, TacticalMapRules):
//   - every point in WORLD space [x, y, z] cm - the same layout -> world mapping as missionGoals.ts (x, y as on the plan; z = ground_z
//     + the layer's level x 300 cm, the builder's z_of / layer_level), with the walk LAYER of each item ("ground", "roof:<B>",
//     "floor:<B>:<n>"; a wall top is "wall:<W>" at ground_z + the wall's height);
//   - chokepoints (kind, tags, score, line / poly, width, the detour), drop ambush spots (edge on the roof / wall top, land on the street),
//     windows covering the lines (window point + the stand point inside, look yaw), overwatch roofs (access incl. ladder-only, spots +
//     look yaw), hiding spots (reveal distances, stealth-only, facing yaw), and every ladder-only roof (the P2 placement picks PART of
//     them with the ladder-only factor, seeded - C++ TacticalMapRules::PickLadderOnlyRoofs);
//   - firing gaps (firingGaps.ts, BUILDING_GENERATOR.md section 8): `firing_gaps[]` + a `gap` id on the overwatch spots standing behind one; written
//     only for a layout that has gaps (files without them stay byte-identical);
//   - geometry_hash: the layout geometry the analysis describes (chokepoints.ts geometryHash).
// The layout's own analysis is used when it is current (the hash matches); else it is computed here exactly as «Принять» does
// (analyzeLayout with the config's default conditions). Written next to the objectives file by «Принять» / «Собрать в Unreal»
// (node/runtimeFiles.ts, server/levelgenApi.ts) and for the existing maps by scripts/export-tactical-map.ts. Pure except writeTacticalMapFile.

import fs from 'node:fs';
import path from 'node:path';
import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dirOf, dist, distToWall, pointInPoly, segmentsIntersect, yawOf } from '../movement/geometry.ts';
import type { Conditions, LayoutAnalysis } from './chokepoints.ts';
import { analyzeLayout, CHOKE, geometryHash, samplePolyline, threatNumbers } from './chokepoints.ts';
import { coverPoly } from './layers.ts';
import { layerZ, LEVEL_HEIGHT_CM } from './missionGoals.ts';
import { gapForSpot } from './firingGaps.ts';
import { footprintPoly, inFootprint } from './rotation.ts';
import type { GenBuilding, GenCover, GenLayout } from './schema.ts';
import { NEVER_CLIMBABLE } from './schema.ts';

export const TACTICAL_MAP_SCHEMA = 'codex.tactical_map/2';
export const TACTICAL_MAP_SCHEMA_V1 = 'codex.tactical_map/1';
export const TACTICAL_MAP_SCHEMA_V2 = 'codex.tactical_map/2';
/** The window's stand point lies this far inside the building behind the window point, cm (a man at the sill, not in the wall). */
export const WINDOW_STAND_INSET_CM = 60;

/** Thresholds for tactical open crossings and goal sites. */
export const COVER_DISTANCE_THRESHOLD_CM = 200;
export const MIN_CROSSING_LENGTH_CM = 1000;
export const SEEING_COVER_MAX_DIST_CM = 4000;
export const GOAL_SITE_RADIUS_CM = 2500;
export const GOAL_SITE_SECTOR_ARC_DEG = 90;
export const ROUTE_SAMPLE_STEP_CM = 100;
export const BUILDING_ASSOCIATION_MARGIN_CM = 200;
/** A chokepoint side counts as a building wall only within this distance, m. */
export const SIDE_WALL_MAX_DIST_M = 3.0;
/** Ground walls taller than this block line of sight, cm. */
export const LOS_WALL_MIN_HEIGHT_CM = 150;
/** Cover height used when a cover has no size, cm. */
export const DEFAULT_COVER_HEIGHT_CM = 100;

export type Vec3 = [number, number, number];

export interface TacticalRoofAccess {
  id: string;
  kind: 'stairs' | 'ladder' | 'hatch';
  /** Landing point on the roof in 3D world cm. */
  top: Vec3;
}

export interface TacticalChokepoint {
  id: string; kind: string; kind_name: string; tags: string[]; score: number; layer: string;
  /** The narrowest point. */
  pos: Vec3;
  /** The stretch's centre line (world) and its outline (world XY on the layer's z). */
  line: Vec3[]; poly: Vec2[];
  length_m: number; width_m: number | null; lane_m: number | null; route: string; sources: string[];
  /** 'none' = no way round (the only path); 'detour' = a way round detour_m longer; 'n/a' = the stretch ends a line. */
  alternative: string; detour_m: number | null;
  drop_spots: string[]; windows: string[]; overwatch: string[]; hiding_spots: string[];
  reveal_m: number | null; reveal_stealth_m: number | null;
  building?: string; floor?: number;
}
export interface TacticalDropSpot {
  id: string; surface: string; kind: 'roof' | 'wall_top';
  /** The surface the jumper waits on ("roof:<B>" / "wall:<W>") and the layer he lands on. */
  layer: string; land_layer: string;
  edge: Vec3; land: Vec3; drop_cm: number; gap_cm: number; access: string;
  /** Only a ladder (or a hatch) leads up: the jumpers cannot climb, so they must be PLACED there. */
  ladder_only: boolean;
  jumpers: string[]; line: string; places: string[];
}
export interface TacticalWindow {
  id: string; building: string; floor: number; layer: string; kind: string;
  /** The window point on the wall (floor z), the stand point WINDOW_STAND_INSET_CM inside, the look yaw (outward, degrees). */
  pos: Vec3; stand: Vec3; yaw: number; covered_m: number; min_dist_m: number; places: string[];
}
export interface TacticalOverwatchSpot {
  pos: Vec3; facing_yaw: number; covered_m: number; longest_los_m: number;
  building: string; floor: number;
  /** The firing gap (`firing_gaps[].id`) the shooter stands behind; only on a layout with gaps (firingGaps.ts gapForSpot). */
  gap?: string;
}
/**
 * A firing gap in a roof parapet (firingGaps.ts): a span cut out of the parapet and refilled with a sill `sill_cm` high. `pos` = the gap
 * centre and `stand` = the shooter's point behind it, both on the roof floor (world); `outward` = unit XY out over the parapet.
 */
export interface TacticalFiringGap {
  id: string; building: string; layer: string; parapet: string;
  pos: Vec3; stand: Vec3; outward: Vec2; yaw: number; width_cm: number; sill_cm: number;
}
export interface TacticalOverwatchRoof {
  building: string; layer: string; level: number; floor: number; z: number; height_cm: number; range_m: number;
  /** "stair:<id>" / "ladder:<id>" / "hatch:<id>" entries; access_kind = stairs | ladder | hatch. */
  access: string[]; access_kind: string; access_points: TacticalRoofAccess[]; ladder_only: boolean;
  spots: TacticalOverwatchSpot[]; places: string[];
}
export interface TacticalHidingSpot {
  id: string; layer: string; pos: Vec3; facing_yaw: number; reveal_normal_m: number; reveal_stealth_m: number; stealth_only: boolean; blind: boolean;
  rush_m: number; cover: string; cover_height_cm: number; threatens: { line: string; route?: string; pos: Vec3 }; place?: string;
  building?: string; floor?: number;
}
export interface TacticalLadderRoof {
  building: string; layer: string; z: number;
  /** The roof's centre and outline (world). */
  center: Vec3; poly: Vec2[];
  /** The ladders onto it and their top points (where a climber steps off). */
  ladders: string[]; ladder_tops: Vec3[];
  access_points: TacticalRoofAccess[];
  /** It is an overwatch roof / the drop spots off it. */
  overwatch: boolean; drop_spots: string[];
}
export interface TacticalWalkableRoof {
  building: string; layer: string; floor: number; z: number;
  access: TacticalRoofAccess[]; ladder_only: boolean;
}
export interface TacticalOpenCrossing {
  id: string; route: string; start: Vec3; end: Vec3; length_m: number; nearest_cover_cm: number; seeing_covers: string[];
}
export interface TacticalGoalSite {
  goal_id: string; cover_id: string; pos: Vec3; dist_m: number; facing_yaw: number; sector_arc_deg: number;
  cover_kind: string; cover_height_cm: number;
}
/**
 * A designer-marked killing ground (T-02 gap 1): a zone or a route with intent 'kill_zone' (schema.ts RouteIntent) - the authored
 * place squad plays (T-08) and DangerAt pick, never a defect. Zone: its outline; route: its points on their layers.
 */
export interface TacticalKillZone {
  id: string; source: 'zone' | 'route'; intent: string; note?: string; set_piece?: string;
  /** The centre (world, on the first layer) and, for a zone, its outline (world XY); for a route, its points (world, per layer). */
  center: Vec3; poly?: Vec2[]; line?: Vec3[];
}
export interface TacticalMapFile {
  schema: typeof TACTICAL_MAP_SCHEMA | typeof TACTICAL_MAP_SCHEMA_V1;
  map: string;
  layout: string;
  geometry_hash: string;
  analysis_version: number;
  /** "layout" = the accepted layout's own analysis; "computed" = made by the export (the layout had none / a stale one). */
  analysis_source: 'layout' | 'computed';
  conditions: Conditions;
  ground_z: number;
  /** The analysis thresholds + the ladder-only factor (chokepoints.json drop_ambush.ladder_only_factor_default, designer 2026-11-17). */
  thresholds: LayoutAnalysis['thresholds'] & { ladder_only_factor: number };
  chokepoints: TacticalChokepoint[];
  drop_spots: TacticalDropSpot[];
  windows: TacticalWindow[];
  overwatch_roofs: TacticalOverwatchRoof[];
  hiding_spots: TacticalHidingSpot[];
  ladder_only_roofs: TacticalLadderRoof[];
  walkable_roofs?: TacticalWalkableRoof[];
  open_crossings?: TacticalOpenCrossing[];
  goal_sites?: TacticalGoalSite[];
  /** Only when the layout marks one (absent otherwise: older files stay byte-identical). */
  kill_zones?: TacticalKillZone[];
  /** Only when the layout has firing gaps (absent otherwise: files without gaps stay byte-identical). */
  firing_gaps?: TacticalFiringGap[];
}

const r0 = (v: number): number => { const x = Math.round(v); return Object.is(x, -0) ? 0 : x; };
const r1 = (v: number): number => { const x = Math.round(v * 10) / 10; return Object.is(x, -0) ? 0 : x; };
const at = (p: Vec2, z: number): Vec3 => [r0(p[0]), r0(p[1]), r0(z)];

/** Every walkable roof's access: "stair:<id>" (a stair to the top floor), "ladder:<id>", "hatch:<id>" (chokepoints.ts roofAccess). */
export function roofAccess(l: GenLayout, b: GenBuilding): string[] {
  return [
    ...(b.stairs ?? []).filter((s) => s.to >= b.floors).map((s) => `stair:${s.id}`),
    ...l.ladders.filter((k) => k.to === `roof:${b.id}`).map((k) => `ladder:${k.id}`),
    ...(b.hatches ?? []).map((h) => `hatch:${h.id}`),
  ];
}

/** Structured roof access entries (id, kind, top in world space). */
export function roofAccessDetails(l: GenLayout, b: GenBuilding): TacticalRoofAccess[] {
  const layer = `roof:${b.id}`;
  const z = layerZ(l, layer);
  const res: TacticalRoofAccess[] = [];
  for (const s of (b.stairs ?? []).filter((s) => s.to >= b.floors)) {
    const topPt: Vec2 | undefined = s.top ?? s.pos;
    if (!topPt) continue;
    res.push({ id: s.id, kind: 'stairs', top: at(topPt, z) });
  }
  for (const k of (l.ladders ?? []).filter((k) => k.to === layer)) {
    const topPt: Vec2 | undefined = k.top ?? k.pos;
    if (!topPt) continue;
    res.push({ id: k.id, kind: 'ladder', top: at(topPt, z) });
  }
  for (const h of (b.hatches ?? [])) {
    const topPt: Vec2 | undefined = h.pos;
    if (!topPt) continue;
    res.push({ id: h.id, kind: 'hatch', top: at(topPt, z) });
  }
  return res.sort((a, b) => a.kind.localeCompare(b.kind) || a.id.localeCompare(b.id));
}

/** "stairs" when a stair leads up, else "ladder" when a ladder does, else "hatch" / "none". */
export function accessKind(access: readonly string[]): 'stairs' | 'ladder' | 'hatch' | 'none' {
  if (access.some((a) => a.startsWith('stair:'))) return 'stairs';
  if (access.some((a) => a.startsWith('ladder:'))) return 'ladder';
  return access.length ? 'hatch' : 'none';
}

/**
 * Resolves building and storey for a 2D or 3D point or obstacle context.
 * Checks inside footprint (or within margin of building), obstacle walls, or chokepoint sides.
 */
export function buildingAndFloorAt(
  l: GenLayout,
  pos: Vec2 | Vec3,
  defaultZ: number,
  margin = BUILDING_ASSOCIATION_MARGIN_CM,
  coverSource?: string,
  sideA?: { src?: string; dist_m?: number } | null,
  sideB?: { src?: string; dist_m?: number } | null,
): { building: string; floor: number } | undefined {
  // 1. If explicit cover wall is given and belongs to a building
  if (coverSource && coverSource.startsWith('wall:')) {
    const wid = coverSource.slice(5);
    const w = (l.walls ?? []).find((x) => x.id === wid);
    if (w && w.building) {
      return { building: w.building, floor: w.floor ?? 0 };
    }
  }

  // 2. If sides of a chokepoint indicate building walls
  let sideBest: { building: string; floor: number; dist_m: number } | null = null;
  for (const s of [sideA, sideB]) {
    if (s && s.src && s.src.startsWith('wall:')) {
      const wid = s.src.slice(5);
      const w = (l.walls ?? []).find((x) => x.id === wid);
      if (w && w.building && (s.dist_m === undefined || s.dist_m <= SIDE_WALL_MAX_DIST_M)) {
        const d = s.dist_m ?? 0;
        if (!sideBest || d < sideBest.dist_m) {
          sideBest = { building: w.building, floor: w.floor ?? 0, dist_m: d };
        }
      }
    }
  }
  if (sideBest) {
    return { building: sideBest.building, floor: sideBest.floor };
  }

  // 3. Check building footprint (strict inside first, then within threshold margin)
  const p2: Vec2 = [pos[0], pos[1]];
  const bInside = (l.buildings ?? []).find((x) => inFootprint(x, p2, 0));
  if (bInside) {
    const z = pos.length > 2 && pos[2] !== undefined ? pos[2] : defaultZ;
    const floor = Math.max(0, Math.round((z - defaultZ) / LEVEL_HEIGHT_CM));
    return { building: bInside.id, floor };
  }

  if (margin > 0) {
    const bNear = (l.buildings ?? []).find((x) => inFootprint(x, p2, margin));
    if (bNear) {
      const z = pos.length > 2 && pos[2] !== undefined ? pos[2] : defaultZ;
      const floor = Math.max(0, Math.round((z - defaultZ) / LEVEL_HEIGHT_CM));
      return { building: bNear.id, floor };
    }
  }

  return undefined;
}

/**
 * Finds route segments >= 10 m with no cover within 2 m (200 cm) of the path,
 * along with the cover objects seeing them (within 40 m of the segment with clear LOS).
 */
export function findOpenCrossings(l: GenLayout, groundZ: number): TacticalOpenCrossing[] {
  const out: TacticalOpenCrossing[] = [];
  if (!l.routes || !l.routes.length) return out;
  const covers = l.covers ?? [];
  const buildings = l.buildings ?? [];
  const walls = (l.walls ?? []).filter((w) => (w.floor ?? 0) === 0);

  const coverPolys = covers.map((c) => coverPoly(c));
  const buildingPolys = buildings.map((b) => ({ b, poly: footprintPoly(b) }));

  function distToPoly(p: Vec2, poly: readonly Vec2[]): number {
    if (pointInPoly(p, poly)) return 0;
    let minD = Infinity;
    for (let i = 0; i < poly.length; i++) {
      const a = poly[i], b = poly[(i + 1) % poly.length];
      const d = dist(p, closestOnSegment(p, a, b).point);
      if (d < minD) minD = d;
    }
    return minD;
  }

  function distToCover(p: Vec2, c: GenCover, poly: readonly Vec2[]): number {
    // Lower bound: every point of the cover box lies within half its diagonal of the centre.
    const halfDiag = Math.hypot(c.size[0], c.size[1]) / 2;
    if (dist(p, c.pos) > halfDiag) return dist(p, c.pos) - halfDiag;
    return distToPoly(p, poly);
  }

  function distToBuilding(p: Vec2, bp: { b: GenBuilding; poly: readonly Vec2[] }): number {
    if (inFootprint(bp.b, p)) return 0;
    return distToPoly(p, bp.poly);
  }

  function minCoverDistance(p: Vec2): number {
    let minD = Infinity;
    for (let i = 0; i < covers.length; i++) {
      const d = distToCover(p, covers[i], coverPolys[i]);
      if (d < minD) {
        minD = d;
        if (minD <= COVER_DISTANCE_THRESHOLD_CM) return minD;
      }
    }
    for (let i = 0; i < walls.length; i++) {
      const d = distToWall(p, walls[i]);
      if (d < minD) {
        minD = d;
        if (minD <= COVER_DISTANCE_THRESHOLD_CM) return minD;
      }
    }
    for (let i = 0; i < buildingPolys.length; i++) {
      const d = distToBuilding(p, buildingPolys[i]);
      if (d < minD) {
        minD = d;
        if (minD <= COVER_DISTANCE_THRESHOLD_CM) return minD;
      }
    }
    return minD;
  }

  // Blocker edges for line-of-sight checks (building footprints and tall ground walls > 150 cm)
  const blockerEdges: { a: Vec2; b: Vec2 }[] = [];
  for (const bp of buildingPolys) {
    for (let i = 0; i < bp.poly.length; i++) {
      blockerEdges.push({ a: bp.poly[i], b: bp.poly[(i + 1) % bp.poly.length] });
    }
  }
  for (const w of walls) {
    if (w.height > LOS_WALL_MIN_HEIGHT_CM) {
      blockerEdges.push({ a: w.a, b: w.b });
    }
  }

  function hasLos(p1: Vec2, p2: Vec2): boolean {
    for (const e of blockerEdges) {
      if (segmentsIntersect(p1, p2, e.a, e.b)) return false;
    }
    return true;
  }

  for (let rIdx = 0; rIdx < l.routes.length; rIdx++) {
    const route = l.routes[rIdx];
    if (!route.points || route.points.length < 2) continue;
    const rId = route.id || `R_${rIdx + 1}`;
    const samples = samplePolyline(route.points, ROUTE_SAMPLE_STEP_CM);
    if (samples.length < 2) continue;

    const minCoverDists: number[] = new Array(samples.length);
    const isOpen: boolean[] = new Array(samples.length);
    for (let i = 0; i < samples.length; i++) {
      const d = minCoverDistance(samples[i]);
      minCoverDists[i] = d;
      isOpen[i] = d > COVER_DISTANCE_THRESHOLD_CM;
    }

    let start = -1;
    let crossIdx = 1;

    const processRun = (sIdx: number, eIdx: number) => {
      const runSamples = samples.slice(sIdx, eIdx + 1);
      let runLenCm = 0;
      for (let k = 0; k < runSamples.length - 1; k++) {
        runLenCm += dist(runSamples[k], runSamples[k + 1]);
      }
      if (runLenCm < MIN_CROSSING_LENGTH_CM) return;

      let minC = Infinity;
      for (let k = sIdx; k <= eIdx; k++) {
        if (minCoverDists[k] < minC) minC = minCoverDists[k];
      }

      const seeing: string[] = [];
      for (const c of covers) {
        let minD = Infinity;
        let closestPt: Vec2 | null = null;
        for (let k = 0; k < runSamples.length - 1; k++) {
          const res = closestOnSegment(c.pos, runSamples[k], runSamples[k + 1]);
          const d = dist(c.pos, res.point);
          if (d < minD) {
            minD = d;
            closestPt = res.point;
          }
        }
        if (minD <= SEEING_COVER_MAX_DIST_CM && closestPt) {
          if (
            hasLos(c.pos, closestPt) ||
            hasLos(c.pos, runSamples[0]) ||
            hasLos(c.pos, runSamples[runSamples.length - 1])
          ) {
            seeing.push(c.id);
          }
        }
      }
      seeing.sort();

      out.push({
        id: `OC_${rId}_${crossIdx++}`,
        route: rId,
        start: at(runSamples[0], groundZ),
        end: at(runSamples[runSamples.length - 1], groundZ),
        length_m: r1(runLenCm / 100),
        nearest_cover_cm: r0(minC),
        seeing_covers: seeing,
      });
    };

    for (let i = 0; i < samples.length; i++) {
      if (isOpen[i]) {
        if (start < 0) start = i;
      } else {
        if (start >= 0) {
          processRun(start, i - 1);
          start = -1;
        }
      }
    }
    if (start >= 0) {
      processRun(start, samples.length - 1);
    }
  }

  return out.sort((a, b) => a.id.localeCompare(b.id));
}

/**
 * Finds cover objects within 25 m of each mission goal, exporting geometry and facing sector.
 * Stop rule: geometry only in TS; line-trace validation left to T-05.
 */
export function findGoalSites(l: GenLayout, groundZ: number): TacticalGoalSite[] {
  const out: TacticalGoalSite[] = [];
  const goals = l.objectives?.goals;
  if (!goals || !goals.length) return out;
  const covers = l.covers ?? [];

  for (const g of goals) {
    for (const c of covers) {
      const d = dist(g.pos, c.pos);
      if (d <= GOAL_SITE_RADIUS_CM) {
        const dist_m = r1(d / 100);
        const facing_yaw = r1(yawOf([g.pos[0] - c.pos[0], g.pos[1] - c.pos[1]]));
        out.push({
          goal_id: g.id,
          cover_id: c.id,
          pos: at(c.pos, groundZ),
          dist_m,
          facing_yaw,
          sector_arc_deg: GOAL_SITE_SECTOR_ARC_DEG,
          cover_kind: c.kind,
          cover_height_cm: c.size ? c.size[2] : DEFAULT_COVER_HEIGHT_CM,
        });
      }
    }
  }

  return out.sort((a, b) => a.goal_id.localeCompare(b.goal_id) || a.dist_m - b.dist_m || a.cover_id.localeCompare(b.cover_id));
}

/** The layout's analysis when it describes this geometry, else null. */
export function currentAnalysis(l: GenLayout): LayoutAnalysis | null {
  return l.analysis && l.analysis.geometry_hash === geometryHash(l) ? l.analysis : null;
}

/** The z of a drop surface: a roof's layer, a wall top = ground_z + the wall's height. */
function surfaceZ(l: GenLayout, surface: string, dropCm: number): { layer: string; z: number } {
  if (surface.startsWith('roof:')) return { layer: surface, z: layerZ(l, surface) };
  return { layer: surface, z: Number(l.ground_z ?? 0) + dropCm };
}

/**
 * The runtime tactical map of a layout. `analysis` overrides the layout's (tests); otherwise the layout's current analysis, else one
 * computed now with the config's default conditions (what «Принять» saves when nothing is shown on the page).
 */
export function tacticalMapFor(l: GenLayout, mapName: string, layoutName = l.name, analysis?: LayoutAnalysis): TacticalMapFile {
  const own = analysis ?? currentAnalysis(l);
  const a = own ?? analyzeLayout(l);
  const gz = Number(l.ground_z ?? 0);
  const groundZ = layerZ(l, 'ground');
  const factor = threatNumbers(CHOKE, a.conditions).drop.ladder_only_factor;

  const chokepoints = a.chokepoints.map((c): TacticalChokepoint => {
    const item: TacticalChokepoint = {
      id: c.id, kind: c.kind, kind_name: c.kind_name, tags: [...c.tags], score: c.score, layer: 'ground',
      pos: at(c.pos, groundZ), line: c.line.map((p) => at(p, groundZ)), poly: c.poly.map((p) => [r0(p[0]), r0(p[1])] as Vec2),
      length_m: c.length_m, width_m: c.width_m, lane_m: c.lane_m, route: c.route, sources: [...c.sources],
      alternative: c.numbers.alternative, detour_m: c.numbers.detour_m,
      drop_spots: [...c.numbers.drop_spots], windows: [...c.numbers.windows], overwatch: [...c.numbers.overwatch], hiding_spots: [...c.numbers.hiding_spots],
      reveal_m: c.numbers.reveal_m, reveal_stealth_m: c.numbers.reveal_stealth_m,
    };
    const bf = buildingAndFloorAt(l, c.pos, groundZ, BUILDING_ASSOCIATION_MARGIN_CM, undefined, c.numbers?.side_a, c.numbers?.side_b);
    if (bf) {
      item.building = bf.building;
      item.floor = bf.floor;
    }
    return item;
  });

  const drop_spots = a.dropAmbushSpots.map((d): TacticalDropSpot => {
    const s = surfaceZ(l, d.surface, d.drop_cm);
    return { id: d.id, surface: d.surface, kind: d.kind, layer: s.layer, land_layer: 'ground', edge: at(d.edge, s.z), land: at(d.land, groundZ),
      drop_cm: d.drop_cm, gap_cm: d.gap_cm, access: d.access, ladder_only: d.access === 'ladder', jumpers: [...d.jumpers], line: d.line, places: [...d.places] };
  });

  const windows = a.windowsCovering.map((w): TacticalWindow => {
    const layer = w.floor >= 1 ? `floor:${w.building}:${w.floor}` : 'ground';
    const z = w.floor >= 1 ? layerZ(l, layer) : groundZ;
    const d = dirOf(w.yaw);
    return { id: w.id, building: w.building, floor: w.floor, layer, kind: w.kind, pos: at(w.pos, z),
      stand: at([w.pos[0] - d[0] * WINDOW_STAND_INSET_CM, w.pos[1] - d[1] * WINDOW_STAND_INSET_CM], z), yaw: w.yaw, covered_m: w.covered_m, min_dist_m: w.min_dist_m, places: [...w.places] };
  });

  const overwatch_roofs = a.overwatchRoofs.map((r): TacticalOverwatchRoof => {
    const b = l.buildings.find((x) => x.id === r.building);
    const layer = `roof:${r.building}`, z = r0(layerZ(l, layer)), kind = accessKind(r.access);
    const access_points = b ? roofAccessDetails(l, b) : [];
    return { building: r.building, layer, level: r.level, floor: r.level, z, height_cm: r.height_cm, range_m: r.range_m,
      access: [...r.access], access_kind: kind, access_points, ladder_only: kind === 'ladder',
      spots: r.spots.map((s): TacticalOverwatchSpot => {
        const gap = gapForSpot(l.firing_gaps ?? [], r.building, s.pos);
        return { pos: at(s.pos, z), facing_yaw: s.facing_yaw, covered_m: s.covered_m, longest_los_m: s.longest_los_m, building: r.building, floor: r.level, ...(gap ? { gap: gap.id } : {}) };
      }),
      places: [...r.places] };
  });

  // firing gaps in the roof parapets (firingGaps.ts), on the roof layer of their building; absent when the layout has none
  const firing_gaps = (l.firing_gaps ?? []).map((g): TacticalFiringGap => {
    const layer = `roof:${g.building}`, z = layerZ(l, layer);
    return { id: g.id, building: g.building, layer, parapet: g.parapet, pos: at(g.center, z), stand: at(g.stand, z), outward: [g.outward[0], g.outward[1]], yaw: g.facing_yaw, width_cm: g.width_cm, sill_cm: g.sill_cm };
  });

  const hiding_spots = a.hidingSpots.map((h): TacticalHidingSpot => {
    const item: TacticalHidingSpot = {
      id: h.id, layer: 'ground', pos: at(h.pos, groundZ), facing_yaw: h.facing_yaw, reveal_normal_m: h.reveal_normal_m, reveal_stealth_m: h.reveal_stealth_m,
      stealth_only: h.stealth_only, blind: h.blind, rush_m: h.rush_m, cover: h.cover, cover_height_cm: h.cover_height_cm,
      threatens: { line: h.threatens.line, ...(h.threatens.route ? { route: h.threatens.route } : {}), pos: at(h.threatens.pos, groundZ) }, ...(h.place ? { place: h.place } : {}),
    };
    const bf = buildingAndFloorAt(l, h.pos, groundZ, BUILDING_ASSOCIATION_MARGIN_CM, h.cover);
    if (bf) {
      item.building = bf.building;
      item.floor = bf.floor;
    }
    return item;
  });

  // every walkable roof reached by ladder(s) and no stair (the climber never knows what waits on top) - the P2 placement candidates
  const overwatchSet = new Set(overwatch_roofs.map((r) => r.building));
  const ladder_only_roofs = l.buildings
    .filter((b) => b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece) && accessKind(roofAccess(l, b)) === 'ladder')
    .map((b): TacticalLadderRoof => {
      const layer = `roof:${b.id}`, z = r0(layerZ(l, layer)), poly = footprintPoly(b);
      const cx = poly.reduce((s, p) => s + p[0], 0) / poly.length, cy = poly.reduce((s, p) => s + p[1], 0) / poly.length;
      const ladders = l.ladders.filter((k) => k.to === layer).sort((x, y) => (x.id < y.id ? -1 : x.id > y.id ? 1 : 0));
      const access_points = roofAccessDetails(l, b);
      return { building: b.id, layer, z, center: at([cx, cy], z), poly: poly.map((p) => [r0(p[0]), r0(p[1])] as Vec2), ladders: ladders.map((k) => k.id),
        ladder_tops: ladders.map((k) => at(k.top, z)), access_points, overwatch: overwatchSet.has(b.id), drop_spots: drop_spots.filter((d) => d.surface === layer).map((d) => d.id) };
    })
    .sort((x, y) => (x.building < y.building ? -1 : x.building > y.building ? 1 : 0));

  // master list of all walkable roofs in the layout
  const walkable_roofs = l.buildings
    .filter((b) => b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece))
    .map((b): TacticalWalkableRoof => {
      const layer = `roof:${b.id}`, z = r0(layerZ(l, layer));
      const access = roofAccessDetails(l, b);
      const kind = accessKind(roofAccess(l, b));
      return { building: b.id, layer, floor: b.floors, z, access, ladder_only: kind === 'ladder' };
    })
    .sort((x, y) => (x.building < y.building ? -1 : x.building > y.building ? 1 : 0));

  // designer-marked killing grounds (zones / routes with an intent), in id order
  const kill_zones: TacticalKillZone[] = !l.export_intents ? [] : [
    ...l.zones.filter((z) => z.intent).map((z): TacticalKillZone => {
      const cx = z.poly.reduce((s, p) => s + p[0], 0) / z.poly.length, cy = z.poly.reduce((s, p) => s + p[1], 0) / z.poly.length;
      return { id: z.id, source: 'zone', intent: z.intent!, ...(z.set_piece ? { set_piece: z.set_piece } : {}), center: at([cx, cy], groundZ),
        poly: z.poly.map((p) => [r0(p[0]), r0(p[1])] as Vec2) };
    }),
    ...l.routes.filter((r) => r.intent).map((r): TacticalKillZone => {
      const layers = r.layers ?? r.points.map(() => 'ground');
      const line = r.points.map((p, i) => at(p, layerZ(l, layers[i] ?? 'ground')));
      const mid = line[Math.floor(line.length / 2)];
      return { id: r.id, source: 'route', intent: r.intent!, ...(r.intent_note ? { note: r.intent_note } : {}), center: mid, line };
    }),
  ].sort((x, y) => (x.id < y.id ? -1 : x.id > y.id ? 1 : 0));
  const open_crossings = findOpenCrossings(l, groundZ);
  const goal_sites = findGoalSites(l, groundZ);

  return {
    schema: TACTICAL_MAP_SCHEMA, map: mapName, layout: layoutName, geometry_hash: a.geometry_hash, analysis_version: a.version,
    analysis_source: own ? 'layout' : 'computed', conditions: { weather: a.conditions.weather, time_of_day: a.conditions.time_of_day }, ground_z: r1(gz),
    thresholds: { ...a.thresholds, ladder_only_factor: factor },
    chokepoints, drop_spots, windows, overwatch_roofs, hiding_spots, ladder_only_roofs, walkable_roofs, open_crossings, goal_sites,
    ...(kill_zones.length ? { kill_zones } : {}),
    ...(firing_gaps.length ? { firing_gaps } : {}),
  };
}

/** Stable text (compact items, one per line - the X6 file stays small and diffs cleanly; LF + final newline). */
export function formatTacticalMap(f: TacticalMapFile): string {
  const lists = ['chokepoints', 'drop_spots', 'windows', 'overwatch_roofs', 'hiding_spots', 'ladder_only_roofs', 'walkable_roofs', 'open_crossings', 'goal_sites', ...(f.kill_zones ? ['kill_zones'] as const : []), ...(f.firing_gaps ? ['firing_gaps'] as const : [])] as const;
  const head: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(f)) if (!(lists as readonly string[]).includes(k)) head[k] = v;
  const headText = JSON.stringify(head, null, 2);
  const body = lists.map((k) => {
    const items = (f[k as keyof TacticalMapFile] as unknown[]) ?? [];
    return items.length ? `  "${k}": [\n${items.map((it) => `    ${JSON.stringify(it)}`).join(',\n')}\n  ]` : `  "${k}": []`;
  });
  return `${headText.slice(0, -2)},\n${body.join(',\n')}\n}\n`;
}

/** <dir>/<Map>.tactical.json. */
export const tacticalMapPath = (dir: string, mapName: string): string => path.join(dir, `${mapName}.tactical.json`);

/**
 * Write (or refresh) the map's tactical file into `dir` (Content/Data/LevelGen/tactical). `made` = the file already made by tacticalMapFor
 * (the analysis is not run twice). Returns the written path.
 */
export function writeTacticalMapFile(l: GenLayout, dir: string, mapName: string, layoutName = l.name, made?: TacticalMapFile): string {
  const f = made ?? tacticalMapFor(l, mapName, layoutName);
  fs.mkdirSync(dir, { recursive: true });
  const file = tacticalMapPath(dir, mapName);
  fs.writeFileSync(file, formatTacticalMap(f), 'utf8');
  return file;
}
