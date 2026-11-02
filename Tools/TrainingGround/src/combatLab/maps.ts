// Training Ground «Боевая лаборатория»: the maps the department works on. The lab map L_FormationLabCombat (the default) plus every GENERATED
// map (Content/Maps/Generated/<leaf>.umap) whose level JSON (Content/Data/LevelJson/<leaf>.json) has a "bot_route" object. Pure: no fs, no
// DOM - the server (server/combatLabApi.ts) passes directory listings and parsed JSON in. Tests: maps.test.ts.
//
// Presets are ONE shared file and carry the lab's own `pressure.ambush.points`. Rule: a preset applied to another map KEEPS that map's own
// ambush points; a preset saved from a generated map is stored WITHOUT ambush.points (so it never overwrites the lab's points).
import { LAB_MAP_ID, getAt, isJsonObject, presetSettings, setAt } from './pressureSettings.ts';
import type { CombatLabPreset, CombatLabSettings, JsonObject } from './pressureSettings.ts';

export { LAB_MAP_ID };
export const LAB_MAP_PATH = `/Game/Maps/${LAB_MAP_ID}`;
export const GENERATED_MAP_DIR = '/Game/Maps/Generated';
export const LEVEL_JSON_DIR = 'Content/Data/LevelJson';
export const ACCEPTED_LAYOUT_DIR = 'Content/Data/LevelGen/accepted/';

/** A map id is a plain asset leaf name: letters, digits, underscore (no separators, so no path traversal). */
export const MAP_ID = /^[A-Za-z0-9_]{1,80}$/;

export interface CombatLabMapInfo {
  id: string;
  /** The Unreal map path bot_run.ps1 -Map takes. */
  map: string;
  /** Repo-relative level JSON. */
  levelFile: string;
  kind: 'lab' | 'generated';
  hasRoute: boolean;
  routeName: string;
  routePoints: number;
}

export interface GeneratedMapInput {
  /** The leaf name of a Content/Maps/Generated/<leaf>.umap. */
  leaf: string;
  /** Its parsed level JSON (null = the file is missing / unreadable). */
  level: unknown;
}

const botRoute = (level: unknown): JsonObject | null => (isJsonObject(level) && isJsonObject(level.bot_route) ? (level.bot_route as JsonObject) : null);

function info(id: string, kind: CombatLabMapInfo['kind'], level: unknown): CombatLabMapInfo {
  const route = botRoute(level);
  return {
    id, map: kind === 'lab' ? LAB_MAP_PATH : `${GENERATED_MAP_DIR}/${id}`, levelFile: `${LEVEL_JSON_DIR}/${id}.json`, kind,
    hasRoute: route !== null, routeName: typeof route?.name === 'string' ? route.name : '', routePoints: Array.isArray(route?.points) ? (route.points as unknown[]).length : 0,
  };
}

/** The maps list: the lab first, then the generated maps with a bot_route (sorted by id; others are not listed). */
export function buildMapList(labLevel: unknown, generated: GeneratedMapInput[]): CombatLabMapInfo[] {
  const seen = new Set<string>([LAB_MAP_ID]);
  const out: CombatLabMapInfo[] = [info(LAB_MAP_ID, 'lab', labLevel)];
  for (const g of [...generated].sort((a, b) => a.leaf.localeCompare(b.leaf))) {
    if (!MAP_ID.test(g.leaf) || seen.has(g.leaf) || botRoute(g.level) === null) continue;
    seen.add(g.leaf);
    out.push(info(g.leaf, 'generated', g.level));
  }
  return out;
}

/** The map for a requested id (missing / empty = the lab). Strict: an id outside the list is an error, nothing is joined into a path. */
export function resolveMap(maps: CombatLabMapInfo[], requested: unknown): { map: CombatLabMapInfo; error?: undefined } | { map?: undefined; error: string } {
  if (requested === undefined || requested === null || requested === '') return { map: maps.find((m) => m.id === LAB_MAP_ID) ?? maps[0] };
  if (typeof requested !== 'string' || !MAP_ID.test(requested)) return { error: 'bad map id' };
  const map = maps.find((m) => m.id === requested);
  return map ? { map } : { error: `unknown map ${requested}` };
}

/** The accepted layout path a generated level JSON points to (repo-relative), or null when it is missing / not under the accepted folder. */
export function acceptedLayoutPath(level: unknown): string | null {
  const p = isJsonObject(level) ? level.levelgen_layout : undefined;
  if (typeof p !== 'string') return null;
  const clean = p.replace(/\\/g, '/');
  if (!clean.startsWith(ACCEPTED_LAYOUT_DIR) || clean.includes('..') || !/^[A-Za-z0-9_./-]+\.json$/.test(clean)) return null;
  return clean;
}

// --- The layout preview (what CombatLabMap draws) -------------------------------------------------------------------------------------

type V2 = [number, number];
export interface PreviewWall { id: string; a: V2; b: V2; thickness?: number }
export interface PreviewBuilding { id: string; min: V2; max: V2; floors?: number }
export interface PreviewLadder { id: string; pos: V2 }
export interface LayoutPreview {
  bounds: { min: V2; max: V2 } | null;
  walls: PreviewWall[];
  /** The lab's towers (always [] for a generated map: its buildings are drawn from `buildings`). */
  towers: { id: string; center: V2; size: V2 }[];
  spawn: { pos: V2 } | null;
  buildings: PreviewBuilding[];
  ladders: PreviewLadder[];
}

const isV2 = (v: unknown): v is V2 => Array.isArray(v) && v.length >= 2 && typeof v[0] === 'number' && typeof v[1] === 'number' && Number.isFinite(v[0]) && Number.isFinite(v[1]);
const v2 = (v: V2): V2 => [v[0], v[1]];

/** An accepted levelgen layout (codex.levelgen_layout/1) -> the preview shape: walls as drawn, buildings as footprint rectangles, ladders as markers. */
export function layoutToPreview(layout: unknown): LayoutPreview {
  const l = isJsonObject(layout) ? layout : {};
  const b = isJsonObject(l.bounds) ? l.bounds : null;
  const walls: PreviewWall[] = [];
  for (const [i, w] of (Array.isArray(l.walls) ? l.walls : []).entries()) {
    if (!isJsonObject(w) || !isV2(w.a) || !isV2(w.b)) continue;
    walls.push({ id: typeof w.id === 'string' ? w.id : `wall_${i}`, a: v2(w.a), b: v2(w.b), ...(typeof w.thickness === 'number' ? { thickness: w.thickness } : {}) });
  }
  const buildings: PreviewBuilding[] = [];
  for (const [i, bd] of (Array.isArray(l.buildings) ? l.buildings : []).entries()) {
    if (!isJsonObject(bd) || !Array.isArray(bd.footprint) || !isV2(bd.footprint[0]) || !isV2(bd.footprint[1])) continue;
    const [p, q] = bd.footprint as [V2, V2];
    buildings.push({ id: typeof bd.id === 'string' ? bd.id : `building_${i}`, min: [Math.min(p[0], q[0]), Math.min(p[1], q[1])], max: [Math.max(p[0], q[0]), Math.max(p[1], q[1])],
      ...(typeof bd.floors === 'number' ? { floors: bd.floors } : {}) });
  }
  const ladders: PreviewLadder[] = [];
  for (const [i, ld] of (Array.isArray(l.ladders) ? l.ladders : []).entries()) {
    if (isJsonObject(ld) && isV2(ld.pos)) ladders.push({ id: typeof ld.id === 'string' ? ld.id : `ladder_${i}`, pos: v2(ld.pos) });
  }
  const spawn = isJsonObject(l.spawn) && isV2(l.spawn.pos) ? { pos: v2(l.spawn.pos) } : null;
  return { bounds: b && isV2(b.min) && isV2(b.max) ? { min: v2(b.min), max: v2(b.max) } : null, walls, towers: [], spawn, buildings, ladders };
}

// --- Presets per map -------------------------------------------------------------------------------------------------------------------

const hasPoints = (pressure: unknown) => getAt(pressure, 'ambush.points') !== undefined;
const clone = <T,>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

/** A copy of the pressure block without ambush.points (the key "ambush" stays, even when it becomes empty). */
export function withoutAmbushPoints(pressure: JsonObject): JsonObject {
  return hasPoints(pressure) ? setAt(clone(pressure), 'ambush.points', undefined) : clone(pressure);
}

/**
 * The settings applying `preset` gives on map `mapId`: the preset's values, but the map's OWN ambush.points stay (the current ones) when the
 * map is not the lab, or when the preset has no points (a preset saved from a generated map). On the lab a preset WITH points sets them.
 */
export function presetSettingsForMap(preset: CombatLabPreset, current: CombatLabSettings, mapId: string): CombatLabSettings {
  const applied = presetSettings(preset);
  if (mapId === LAB_MAP_ID && hasPoints(preset.pressure)) return applied;
  const own = getAt(current.pressure, 'ambush.points');
  return { ...applied, pressure: setAt(withoutAmbushPoints(applied.pressure), 'ambush.points', own === undefined ? undefined : clone(own)) };
}

/** The preset body to store when saving from map `mapId`: generated maps drop ambush.points (the lab's points are never overwritten). */
export function presetForSave(preset: CombatLabPreset, mapId: string): CombatLabPreset {
  return mapId === LAB_MAP_ID ? preset : { ...preset, pressure: withoutAmbushPoints(preset.pressure) };
}
