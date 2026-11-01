// Training Ground / level generator: a reference PROFILE (Content/Data/LevelGen/refs/*.json, schema codex.levelgen_ref/1) - the
// numbers read off a designer's reference map (docs/design/levelgen/REF_01_02_DEPOT.md). Metres, x east, y south, origin the
// map's west corner; `toUE` maps a point into the generated layout's UE frame (cm, X north = up, Y east = right, centred).

import type { Vec2 } from '../movement/geometry.ts';

export const PROFILE_SCHEMA = 'codex.levelgen_ref/1';

export interface ProfileZone { id: string; kind: string; poly: Vec2[]; cover_per_100m2: number; sightline_m: [number, number]; mechanics: string[]; entrances?: Vec2[] }
export interface ProfileBuilding {
  id: string; footprint: [Vec2, Vec2]; height_m: number; height_class: 'tall' | 'low' | 'platform' | 'fence' | 'tower'; floors: number;
  enterable: boolean; roof_access: boolean; doors?: Vec2[]; stairwell?: boolean; external_stair?: Vec2; roof_holes?: number; floor_holes?: number; ladders?: string[]; note?: string;
}
export interface ProfileLadder { id: string; pos: Vec2; height_m: number; rungs: number; connects: [string, string]; note?: string }
export interface ProfileRoute { id: string; points: Vec2[]; length_m: number; note?: string; requires?: string[] }

export interface Profile {
  schema: string;
  id: string;
  title: string;
  bounds: { min: Vec2; max: Vec2 };
  scale: { recommended: string; ue_ground_plane_m: { recommended: Vec2 } } & Record<string, unknown>;
  start: { pos: Vec2; yaw_deg: number };
  end: { pos: Vec2 };
  cut?: { axis: [Vec2, Vec2]; trench_width_m: number; wall_thickness_m: number; depth_m: number; crossings: { id: string; pos: Vec2; kind: string; width_m: number }[] };
  ladders: ProfileLadder[];
  zones: ProfileZone[];
  buildings: ProfileBuilding[];
  route: { main: ProfileRoute; variants: ProfileRoute[] };
  sightlines: { lanes: { id: string; a: Vec2; b: Vec2; length_m: number; kind: string }[]; stats_m: { p50: number; p90: number; max: number; short_zone_share: number } };
  densities: { covers_per_100m2: Record<string, number>; enterable_share: number; roof_access_share: number; ladders_per_ha: number } & Record<string, unknown>;
  set_pieces: Record<string, number>;
  targets: Record<string, [number, number]>;
}

export function validateProfile(data: unknown): string[] {
  const e: string[] = [];
  if (!data || typeof data !== 'object') return ['expected a JSON object'];
  const p = data as Partial<Profile>;
  if (p.schema !== PROFILE_SCHEMA) e.push(`schema must be "${PROFILE_SCHEMA}"`);
  if (!p.bounds || !Array.isArray(p.bounds.max)) e.push('bounds.max: expected [w, h] metres');
  if (!p.start || !Array.isArray(p.start.pos)) e.push('start.pos: expected [x, y]');
  if (!p.route || !p.route.main || !Array.isArray(p.route.main.points) || p.route.main.points.length < 2) e.push('route.main.points: at least 2 points');
  if (!Array.isArray(p.zones) || !p.zones.length) e.push('zones: at least one');
  if (!Array.isArray(p.buildings)) e.push('buildings: expected an array');
  if (!p.targets || typeof p.targets !== 'object') e.push('targets: expected { metric: [lo, hi] }');
  return e;
}

/** The profile frame (m, x east / y south of a w x h map) to the layout's UE frame (cm, centred, X north, Y east). */
export function toUE(p: Vec2, size_m: Vec2): Vec2 {
  return [Math.round((size_m[1] / 2 - p[1]) * 100), Math.round((p[0] - size_m[0] / 2) * 100)];
}

/** The reverse: a UE layout point (cm) back to profile metres of a w x h map. */
export function fromUE(p: Vec2, size_m: Vec2): Vec2 {
  return [p[1] / 100 + size_m[0] / 2, size_m[1] / 2 - p[0] / 100];
}

/** Scales a profile point from the profile's size to another (the generator's size parameter). */
export function scalePoint(p: Vec2, from: Vec2, to: Vec2): Vec2 {
  return [(p[0] * to[0]) / from[0], (p[1] * to[1]) / from[1]];
}

/** Is a metric value inside its target range? 1 inside, decaying to 0 one full range-width outside. */
export function targetScore(value: number, range: [number, number] | undefined): number {
  if (!range) return 1;
  const [lo, hi] = range;
  if (value >= lo && value <= hi) return 1;
  const width = Math.max(1e-9, hi - lo);
  const d = value < lo ? lo - value : value - hi;
  return Math.max(0, 1 - d / width);
}
