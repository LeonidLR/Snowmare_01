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
import { dirOf } from '../movement/geometry.ts';
import type { Conditions, LayoutAnalysis } from './chokepoints.ts';
import { analyzeLayout, CHOKE, geometryHash, threatNumbers } from './chokepoints.ts';
import { layerZ } from './missionGoals.ts';
import { footprintPoly } from './rotation.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { NEVER_CLIMBABLE } from './schema.ts';

export const TACTICAL_MAP_SCHEMA = 'codex.tactical_map/1';
/** The window's stand point lies this far inside the building behind the window point, cm (a man at the sill, not in the wall). */
export const WINDOW_STAND_INSET_CM = 60;

export type Vec3 = [number, number, number];

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
export interface TacticalOverwatchSpot { pos: Vec3; facing_yaw: number; covered_m: number; longest_los_m: number }
export interface TacticalOverwatchRoof {
  building: string; layer: string; level: number; z: number; height_cm: number; range_m: number;
  /** "stair:<id>" / "ladder:<id>" / "hatch:<id>" entries; access_kind = stairs | ladder | hatch. */
  access: string[]; access_kind: string; ladder_only: boolean;
  spots: TacticalOverwatchSpot[]; places: string[];
}
export interface TacticalHidingSpot {
  id: string; layer: string; pos: Vec3; facing_yaw: number; reveal_normal_m: number; reveal_stealth_m: number; stealth_only: boolean; blind: boolean;
  rush_m: number; cover: string; cover_height_cm: number; threatens: { line: string; route?: string; pos: Vec3 }; place?: string;
}
export interface TacticalLadderRoof {
  building: string; layer: string; z: number;
  /** The roof's centre and outline (world). */
  center: Vec3; poly: Vec2[];
  /** The ladders onto it and their top points (where a climber steps off). */
  ladders: string[]; ladder_tops: Vec3[];
  /** It is an overwatch roof / the drop spots off it. */
  overwatch: boolean; drop_spots: string[];
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
  schema: typeof TACTICAL_MAP_SCHEMA;
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
  /** Only when the layout marks one (absent otherwise: older files stay byte-identical). */
  kill_zones?: TacticalKillZone[];
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

/** "stairs" when a stair leads up, else "ladder" when a ladder does, else "hatch" / "none". */
export function accessKind(access: readonly string[]): 'stairs' | 'ladder' | 'hatch' | 'none' {
  if (access.some((a) => a.startsWith('stair:'))) return 'stairs';
  if (access.some((a) => a.startsWith('ladder:'))) return 'ladder';
  return access.length ? 'hatch' : 'none';
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

  const chokepoints = a.chokepoints.map((c): TacticalChokepoint => ({
    id: c.id, kind: c.kind, kind_name: c.kind_name, tags: [...c.tags], score: c.score, layer: 'ground',
    pos: at(c.pos, groundZ), line: c.line.map((p) => at(p, groundZ)), poly: c.poly.map((p) => [r0(p[0]), r0(p[1])] as Vec2),
    length_m: c.length_m, width_m: c.width_m, lane_m: c.lane_m, route: c.route, sources: [...c.sources],
    alternative: c.numbers.alternative, detour_m: c.numbers.detour_m,
    drop_spots: [...c.numbers.drop_spots], windows: [...c.numbers.windows], overwatch: [...c.numbers.overwatch], hiding_spots: [...c.numbers.hiding_spots],
    reveal_m: c.numbers.reveal_m, reveal_stealth_m: c.numbers.reveal_stealth_m,
  }));

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
    const layer = `roof:${r.building}`, z = r0(layerZ(l, layer)), kind = accessKind(r.access);
    return { building: r.building, layer, level: r.level, z, height_cm: r.height_cm, range_m: r.range_m, access: [...r.access], access_kind: kind, ladder_only: kind === 'ladder',
      spots: r.spots.map((s) => ({ pos: at(s.pos, z), facing_yaw: s.facing_yaw, covered_m: s.covered_m, longest_los_m: s.longest_los_m })), places: [...r.places] };
  });

  const hiding_spots = a.hidingSpots.map((h): TacticalHidingSpot => ({
    id: h.id, layer: 'ground', pos: at(h.pos, groundZ), facing_yaw: h.facing_yaw, reveal_normal_m: h.reveal_normal_m, reveal_stealth_m: h.reveal_stealth_m,
    stealth_only: h.stealth_only, blind: h.blind, rush_m: h.rush_m, cover: h.cover, cover_height_cm: h.cover_height_cm,
    threatens: { line: h.threatens.line, ...(h.threatens.route ? { route: h.threatens.route } : {}), pos: at(h.threatens.pos, groundZ) }, ...(h.place ? { place: h.place } : {}),
  }));

  // every walkable roof reached by ladder(s) and no stair (the climber never knows what waits on top) - the P2 placement candidates
  const overwatchSet = new Set(overwatch_roofs.map((r) => r.building));
  const ladder_only_roofs = l.buildings
    .filter((b) => b.roof_walkable && !NEVER_CLIMBABLE.includes(b.set_piece) && accessKind(roofAccess(l, b)) === 'ladder')
    .map((b): TacticalLadderRoof => {
      const layer = `roof:${b.id}`, z = r0(layerZ(l, layer)), poly = footprintPoly(b);
      const cx = poly.reduce((s, p) => s + p[0], 0) / poly.length, cy = poly.reduce((s, p) => s + p[1], 0) / poly.length;
      const ladders = l.ladders.filter((k) => k.to === layer).sort((x, y) => (x.id < y.id ? -1 : x.id > y.id ? 1 : 0));
      return { building: b.id, layer, z, center: at([cx, cy], z), poly: poly.map((p) => [r0(p[0]), r0(p[1])] as Vec2), ladders: ladders.map((k) => k.id),
        ladder_tops: ladders.map((k) => at(k.top, z)), overwatch: overwatchSet.has(b.id), drop_spots: drop_spots.filter((d) => d.surface === layer).map((d) => d.id) };
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

  return {
    schema: TACTICAL_MAP_SCHEMA, map: mapName, layout: layoutName, geometry_hash: a.geometry_hash, analysis_version: a.version,
    analysis_source: own ? 'layout' : 'computed', conditions: { weather: a.conditions.weather, time_of_day: a.conditions.time_of_day }, ground_z: r1(gz),
    thresholds: { ...a.thresholds, ladder_only_factor: factor },
    chokepoints, drop_spots, windows, overwatch_roofs, hiding_spots, ladder_only_roofs,
    ...(kill_zones.length ? { kill_zones } : {}),
  };
}

/** Stable text (compact items, one per line - the X6 file stays small and diffs cleanly; LF + final newline). */
export function formatTacticalMap(f: TacticalMapFile): string {
  const lists = ['chokepoints', 'drop_spots', 'windows', 'overwatch_roofs', 'hiding_spots', 'ladder_only_roofs', ...(f.kill_zones ? ['kill_zones'] as const : [])] as const;
  const head: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(f)) if (!(lists as readonly string[]).includes(k)) head[k] = v;
  const headText = JSON.stringify(head, null, 2);
  const body = lists.map((k) => {
    const items = f[k] as unknown[];
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
