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
//   - geometry_hash: the layout geometry the analysis describes (chokepoints.ts geometryHash).
// The layout's own analysis is used when it is current (the hash matches); else it is computed here exactly as «Принять» does
// (analyzeLayout with the config's default conditions). Written next to the objectives file by «Принять» / «Собрать в Unreal»
// (node/runtimeFiles.ts, server/levelgenApi.ts) and for the existing maps by scripts/export-tactical-map.ts. Pure except writeTacticalMapFile.

import fs from 'node:fs';
import path from 'node:path';
import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dirOf, dist, yawOf } from '../movement/geometry.ts';
import type { Conditions, LayoutAnalysis } from './chokepoints.ts';
import { analyzeLayout, CHOKE, geometryHash, samplePolyline, threatNumbers } from './chokepoints.ts';
import { layerZ } from './missionGoals.ts';
import { footprintPoly, inFootprint } from './rotation.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { NEVER_CLIMBABLE } from './schema.ts';

export const TACTICAL_MAP_SCHEMA = 'codex.tactical_map/2';
export const TACTICAL_MAP_SCHEMA_V1 = 'codex.tactical_map/1';
export const TACTICAL_MAP_SCHEMA_V2 = 'codex.tactical_map/2';
/** The window's stand point lies this far inside the building behind the window point, cm (a man at the sill, not in the wall). */
export const WINDOW_STAND_INSET_CM = 60;

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
    const topPt: Vec2 = s.top ?? s.pos ?? [0, 0];
    res.push({ id: s.id, kind: 'stairs', top: at(topPt, z) });
  }
  for (const k of l.ladders.filter((k) => k.to === layer)) {
    const topPt: Vec2 = k.top ?? k.pos ?? [0, 0];
    res.push({ id: k.id, kind: 'ladder', top: at(topPt, z) });
  }
  for (const h of (b.hatches ?? [])) {
    const topPt: Vec2 = h.pos ?? [0, 0];
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

/** Resolves building and storey for a 2D or 3D point if it falls within a building footprint. */
export function buildingAndFloorAt(l: GenLayout, pos: Vec2 | Vec3, defaultZ: number): { building: string; floor: number } | undefined {
  const p2: Vec2 = [pos[0], pos[1]];
  const b = l.buildings.find((x) => inFootprint(x, p2));
  if (!b) return undefined;
  const z = pos.length > 2 && pos[2] !== undefined ? pos[2] : defaultZ;
  const floor = Math.max(0, Math.round((z - defaultZ) / 300));
  return { building: b.id, floor };
}

/**
 * Finds route segments >= 10 m with no cover within 2 m (200 cm) of the path,
 * along with the cover objects seeing them (within 40 m of the segment).
 */
export function findOpenCrossings(l: GenLayout, groundZ: number): TacticalOpenCrossing[] {
  const out: TacticalOpenCrossing[] = [];
  if (!l.routes || !l.routes.length) return out;
  const covers = l.covers ?? [];

  for (let rIdx = 0; rIdx < l.routes.length; rIdx++) {
    const route = l.routes[rIdx];
    if (!route.points || route.points.length < 2) continue;
    const rId = route.id || `R_${rIdx + 1}`;
    const samples = samplePolyline(route.points, 100);
    if (samples.length < 2) continue;

    const minCoverDists: number[] = new Array(samples.length);
    const isOpen: boolean[] = new Array(samples.length);
    for (let i = 0; i < samples.length; i++) {
      let minD = Infinity;
      const p = samples[i];
      for (const c of covers) {
        const d = dist(p, c.pos);
        if (d < minD) minD = d;
      }
      minCoverDists[i] = minD;
      isOpen[i] = minD > 200;
    }

    let start = -1;
    let crossIdx = 1;
    for (let i = 0; i < samples.length; i++) {
      if (isOpen[i]) {
        if (start < 0) start = i;
      } else {
        if (start >= 0) {
          const runSamples = (i - 1) - start;
          const runCm = runSamples * 100;
          if (runCm >= 1000) {
            let minC = Infinity;
            for (let k = start; k < i; k++) {
              if (minCoverDists[k] < minC) minC = minCoverDists[k];
            }
            const pStart = samples[start], pEnd = samples[i - 1];
            const seeing: string[] = [];
            for (const c of covers) {
              const dSeg = dist(c.pos, closestOnSegment(c.pos, pStart, pEnd).point);
              if (dSeg <= 4000) seeing.push(c.id);
            }
            seeing.sort();
            out.push({
              id: `OC_${rId}_${crossIdx++}`,
              route: rId,
              start: at(pStart, groundZ),
              end: at(pEnd, groundZ),
              length_m: r1(runCm / 100),
              nearest_cover_cm: r0(minC),
              seeing_covers: seeing,
            });
          }
          start = -1;
        }
      }
    }
    if (start >= 0) {
      const runSamples = (samples.length - 1) - start;
      const runCm = runSamples * 100;
      if (runCm >= 1000) {
        let minC = Infinity;
        for (let k = start; k < samples.length; k++) {
          if (minCoverDists[k] < minC) minC = minCoverDists[k];
        }
        const pStart = samples[start], pEnd = samples[samples.length - 1];
        const seeing: string[] = [];
        for (const c of covers) {
          const dSeg = dist(c.pos, closestOnSegment(c.pos, pStart, pEnd).point);
          if (dSeg <= 4000) seeing.push(c.id);
        }
        seeing.sort();
        out.push({
          id: `OC_${rId}_${crossIdx++}`,
          route: rId,
          start: at(pStart, groundZ),
          end: at(pEnd, groundZ),
          length_m: r1(runCm / 100),
          nearest_cover_cm: r0(minC),
          seeing_covers: seeing,
        });
      }
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
      if (d <= 2500) {
        const dist_m = r1(d / 100);
        const facing_yaw = r1(yawOf([g.pos[0] - c.pos[0], g.pos[1] - c.pos[1]]));
        out.push({
          goal_id: g.id,
          cover_id: c.id,
          pos: at(c.pos, groundZ),
          dist_m,
          facing_yaw,
          sector_arc_deg: 90,
          cover_kind: c.kind,
          cover_height_cm: c.size ? c.size[2] : 100,
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
    const bf = buildingAndFloorAt(l, c.pos, groundZ);
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
      spots: r.spots.map((s) => ({ pos: at(s.pos, z), facing_yaw: s.facing_yaw, covered_m: s.covered_m, longest_los_m: s.longest_los_m, building: r.building, floor: r.level })),
      places: [...r.places] };
  });

  const hiding_spots = a.hidingSpots.map((h): TacticalHidingSpot => {
    const item: TacticalHidingSpot = {
      id: h.id, layer: 'ground', pos: at(h.pos, groundZ), facing_yaw: h.facing_yaw, reveal_normal_m: h.reveal_normal_m, reveal_stealth_m: h.reveal_stealth_m,
      stealth_only: h.stealth_only, blind: h.blind, rush_m: h.rush_m, cover: h.cover, cover_height_cm: h.cover_height_cm,
      threatens: { line: h.threatens.line, ...(h.threatens.route ? { route: h.threatens.route } : {}), pos: at(h.threatens.pos, groundZ) }, ...(h.place ? { place: h.place } : {}),
    };
    const bf = buildingAndFloorAt(l, h.pos, groundZ);
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

  const open_crossings = findOpenCrossings(l, groundZ);
  const goal_sites = findGoalSites(l, groundZ);

  return {
    schema: TACTICAL_MAP_SCHEMA, map: mapName, layout: layoutName, geometry_hash: a.geometry_hash, analysis_version: a.version,
    analysis_source: own ? 'layout' : 'computed', conditions: { weather: a.conditions.weather, time_of_day: a.conditions.time_of_day }, ground_z: r1(gz),
    thresholds: { ...a.thresholds, ladder_only_factor: factor },
    chokepoints, drop_spots, windows, overwatch_roofs, hiding_spots, ladder_only_roofs, walkable_roofs, open_crossings, goal_sites,
  };
}

/** Stable text (compact items, one per line - the X6 file stays small and diffs cleanly; LF + final newline). */
export function formatTacticalMap(f: TacticalMapFile): string {
  const lists = ['chokepoints', 'drop_spots', 'windows', 'overwatch_roofs', 'hiding_spots', 'ladder_only_roofs', 'walkable_roofs', 'open_crossings', 'goal_sites'] as const;
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
