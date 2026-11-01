// Training Ground / level generator: the generated layout format (schema codex.levelgen_layout/1) - an EXTENSION of the lab's
// codex.formation_layout/1 (docs/design/levelgen/LEVEL_GENERATOR.md section 2). Everything the lab format has keeps its meaning
// (bounds, ground_z, spawn, walls, zones, routes, props); the extension adds floors on walls, buildings with floors / stairs /
// roof holes, the dividing cut with its crossings, ladders, covers, the mandatory route + variants, the end, enemy spawn /
// ambush points and patrol routes. `toFormationLayout(layout, floor)` flattens one floor LAYER into a plain lab layout, so the
// simulator, the audit, the bot and Scripts/Editor/build_formation_lab.py all read a generated layout unchanged.
// Units: UE cm, X forward (north = +X, drawn up), Y right (east = +Y). A reference profile's metres (x east, y south) map to
// UE as X = -y * 100, Y = x * 100 (profile.ts).

import type { Vec2 } from '../movement/geometry.ts';
import { pointInPoly } from '../movement/geometry.ts';
import type { Layout, LayoutRoute, LayoutWall, LayoutZone, ZoneKind } from '../movement/layout.ts';
import { LAYOUT_SCHEMA, validateLayout, ZONE_KINDS } from '../movement/layout.ts';

export const LEVELGEN_SCHEMA = 'codex.levelgen_layout/1';
/**
 * HEIGHT LEVELS (game designer decision 2026-11-01): every map is split into discrete levels 3 m apart - level -1 = -3 m (the rail
 * cut / canal, pits, trenches, basements), 0 = the ground, +1 = +3 m (first floors, low roofs, platforms, raised rims), +2 = +6 m
 * (second floors, hall roofs, tower tops), upward in 3 m steps when needed. A layer (a walkable surface: 'ground', 'cut',
 * 'floor:<b>:<n>', 'roof:<b>') lives on exactly one level; a building's floor n sits on level n and its roof on level `floors`;
 * links (ladders, stairs, holes, ramps) join surfaces on different levels. The UE builder puts the slabs / cut floor at
 * level * LEVEL_HEIGHT and sizes the ladders to the level difference.
 */
export const LEVEL_HEIGHT = 300;
export const levelZ = (level: number): number => level * LEVEL_HEIGHT;
/** The ladder clip contract (docs/design/LADDERS.md): rung pitch and the shortest climbable ladder. */
export const RUNG_PITCH = 36.4;
export const MIN_RUNGS = 7;
/** The cut's floor = level -1. */
export const CUT_LEVEL = -1;
export const CUT_DEPTH = LEVEL_HEIGHT;

/**
 * A ladder over `levels` height levels: rungs = the whole rungs that fit (8 for 3 m, 16 for 6 m), the rest (8.8 / 17.6 cm) is a
 * plinth under its foot, so the top lands exactly on the upper level (LADDERS.md 3.1: TopZ = rungs * pitch +- 8 cm).
 */
export function ladderForLevels(levels: number): { rungs: number; plinth_cm: number } {
  const h = levels * LEVEL_HEIGHT;
  const rungs = Math.floor(h / RUNG_PITCH);
  return { rungs, plinth_cm: Math.round((h - rungs * RUNG_PITCH) * 10) / 10 };
}

export const WALL_KINDS = ['wall', 'fence', 'curb', 'cut_wall', 'building', 'parapet', 'interior'] as const;
export type WallKind = (typeof WALL_KINDS)[number];

/** The set-piece vocabulary of the grammar (docs/design/levelgen/LEVEL_GENERATOR.md section 3). */
export const SET_PIECES = ['alley', 't_junction', 'facade', 'one_entry_compound', 'open_square', 'cut_crossing', 'ladder_overwatch',
  'tower', 'ruined_two_level', 'dead_end', 'junkyard_field', 'marksman_lane', 'industrial_hall', 'warehouse', 'tank_farm', 'street'] as const;
export type SetPiece = (typeof SET_PIECES)[number];

export const COVER_KINDS = ['jersey', 'sandbag', 'crate', 'wreck', 'barrel', 'silo', 'boiler'] as const;
export type CoverKind = (typeof COVER_KINDS)[number];

export interface GenWall extends LayoutWall {
  /** The height level the wall stands on: 0 = ground (default); n = a building's floor n (its slab at levelZ(n)). */
  floor?: number;
  kind?: WallKind;
  /** The building this wall belongs to (perimeter / interior / parapet). */
  building?: string;
}

export interface Door { pos: Vec2; floor: number; /** The wall id the door is cut into. */ wall?: string }
/**
 * A stair = a walkable ramp at ~35 deg (run = 1.43 x rise) from `pos` (on `from`) to `top` (on `to`); an external one climbs along the
 * face outside the building to `mid` (the ramp's top end) and crosses onto the roof by a flat landing mid -> top. The builder cuts
 * the slab on `to` open above the ramp's last 3 m (the stairwell opening).
 */
export interface Stair { id: string; pos: Vec2; from: number; to: number; kind: 'stairwell' | 'external'; /** The top landing point (on `to`). */ top: Vec2; mid?: Vec2 }
/** The stair's horizontal run per metre of rise (35 deg; the characters walk up to 44 deg). */
export const STAIR_RUN_PER_RISE = 1.43;

export interface GenBuilding {
  id: string;
  /** Axis-aligned rectangle corners [min, max] (cm). */
  footprint: [Vec2, Vec2];
  /** Total height, cm = floors * LEVEL_HEIGHT (the roof sits on level `floors`). */
  height: number;
  /** Interior floors counting the ground floor: floor n (1 <= n < floors) sits on level n; the roof on level `floors`. */
  floors: number;
  /** No intermediate floor slabs (a hall, a tower): the stairwell / ladder climbs straight from the ground to the roof. */
  hollow?: boolean;
  enterable: boolean;
  roof_walkable: boolean;
  doors: Door[];
  stairs: Stair[];
  /** Holes in the roof (down onto the top floor) - the ruined factory. */
  roof_holes: Vec2[];
  /** Holes in an upper floor's slab (down one floor): { pos, floor }. */
  floor_holes: { pos: Vec2; floor: number }[];
  set_piece: SetPiece;
}

export interface GenCut {
  /** The trench polygon (walkable at floor_z). */
  poly: Vec2[];
  /** The trench floor's height level (CUT_LEVEL = -1) and its z relative to ground_z, cm (levelZ(level)). */
  level: number;
  floor_z: number;
  /** Walkable crossings at ground level (bridges / ramps): polygons across the trench. */
  crossings: { id: string; poly: Vec2[]; kind: 'bridge' | 'ramp' }[];
}

export interface GenLadder {
  id: string;
  /** The ladder actor's origin (the bottom centre of the rung plane, on the LOWER floor). */
  pos: Vec2;
  /** Actor yaw: +X points away from the wall, towards the climber (LADDERS.md 3.1). */
  yaw: number;
  rungs: number;
  /** The lower and upper LAYERS it links: 'ground' | 'cut' | 'roof:<building>' | 'floor:<building>:<n>'. */
  from: string;
  to: string;
  /** Their height levels (level_to - level_from >= 1; the ladder's height = that difference x 3 m). */
  level_from: number;
  level_to: number;
  /** cm of step under the ladder's foot so rungs x pitch + plinth = the level difference exactly (ladderForLevels). */
  plinth_cm: number;
  /** The bottom link point (on the lower level, ~1 m out from the rungs: where a route arrives; LADDERS.md 3.1). */
  foot: Vec2;
  /** The top link point (on the upper level). */
  top: Vec2;
}

export interface GenCover { id: string; pos: Vec2; yaw: number; size: [number, number, number]; kind: CoverKind }

export interface GenRoute extends LayoutRoute {
  /** The one route the squad must complete (exactly one per layout). */
  mandatory?: boolean;
  /** The floor / level each point stands on (same length as points; default all 0). 'cut' = the trench floor; 'roof:<b>' / 'floor:<b>:<n>'. */
  layers?: string[];
  /** Links taken between consecutive points on different layers: { at: index of the point AFTER the link, via: ladder / stair / hole id }. */
  links?: { at: number; via: string }[];
  /** Which set-pieces the route passes, in order (for the report). */
  set_pieces?: SetPiece[];
}

export interface SpawnPoint { id: string; pos: Vec2; kind: 'edge' | 'patrol' | 'ambush' }
export interface PatrolRoute { id: string; points: Vec2[] }

export interface GenLayout {
  schema: string;
  name: string;
  comment?: string;
  /** The reference profile id and the generation inputs (reproducible). */
  profile: string;
  seed: number;
  params: GenParams;
  bounds: { min: Vec2; max: Vec2 };
  ground_z: number;
  spawn: { pos: Vec2; yaw: number };
  end: Vec2;
  walls: GenWall[];
  buildings: GenBuilding[];
  cut?: GenCut;
  ladders: GenLadder[];
  covers: GenCover[];
  zones: (LayoutZone & { set_piece?: SetPiece })[];
  routes: GenRoute[];
  spawn_points: SpawnPoint[];
  ambush_points: { id: string; pos: Vec2 }[];
  patrol_routes: PatrolRoute[];
  /** The combat-lab style pressure block for the level JSON (COMBAT_LAB.md section 2). */
  pressure?: Record<string, unknown>;
}

export interface GenParams {
  /** Map size, m (default: the profile's). */
  size_m: [number, number];
  /** Cover density multiplier (1 = the profile's). */
  density: number;
  /** Route variants wanted. */
  variants: number;
  /** 0 = flat; 1 = ladders + roofs; 2 = + stairwells / floors; 3 = + the ruin's holes. */
  verticality: 0 | 1 | 2 | 3;
}

export const DEFAULT_PARAMS: GenParams = { size_m: [177, 124], density: 1, variants: 3, verticality: 3 };

const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
const isVec = (v: unknown): v is Vec2 => Array.isArray(v) && v.length === 2 && isNum(v[0]) && isNum(v[1]);

/** The lab validation of the ground layer + the extension's own checks (empty = valid). */
export function validateGenLayout(data: unknown): string[] {
  if (!data || typeof data !== 'object') return ['expected a JSON object'];
  const l = data as Partial<GenLayout>;
  const e: string[] = [];
  if (l.schema !== LEVELGEN_SCHEMA) e.push(`schema must be "${LEVELGEN_SCHEMA}"`);
  if (!isNum(l.seed)) e.push('seed: expected a number');
  if (typeof l.profile !== 'string') e.push('profile: expected the reference profile id');
  if (!isVec(l.end)) e.push('end: expected [x, y]');
  const ground = (() => { try { return toFormationLayout({ ...(l as GenLayout), schema: LEVELGEN_SCHEMA }, 'ground'); } catch (err) { e.push(`ground layer: ${err instanceof Error ? err.message : String(err)}`); return null; } })();
  if (ground) e.push(...validateLayout(ground).map((m) => `ground layer: ${m}`));
  const ids = new Set<string>();
  for (const b of l.buildings ?? []) {
    if (ids.has(b.id)) e.push(`buildings: duplicate id "${b.id}"`); ids.add(b.id);
    if (!Array.isArray(b.footprint) || b.footprint.length !== 2 || !isVec(b.footprint[0]) || !isVec(b.footprint[1])) e.push(`building ${b.id}: footprint must be [min, max]`);
    else if (b.footprint[0][0] >= b.footprint[1][0] || b.footprint[0][1] >= b.footprint[1][1]) e.push(`building ${b.id}: footprint min must be below max`);
    if (!isNum(b.floors) || b.floors < 1) e.push(`building ${b.id}: floors >= 1`);
    if (b.roof_walkable && isNum(b.floors) && b.height !== b.floors * LEVEL_HEIGHT) e.push(`building ${b.id}: a walkable roof sits on level ${b.floors} = ${b.floors * LEVEL_HEIGHT} cm, height is ${b.height}`);
    for (const s of b.stairs ?? []) if (s.to > b.floors) e.push(`building ${b.id}: stair ${s.id} leads to floor ${s.to} of ${b.floors}`);
  }
  for (const ld of l.ladders ?? []) {
    if (!isNum(ld.rungs) || ld.rungs < MIN_RUNGS) e.push(`ladder ${ld.id}: >= ${MIN_RUNGS} rungs (a shorter ladder is decoration, LADDERS.md)`);
    if (!isVec(ld.pos) || !isVec(ld.top) || !isVec(ld.foot)) e.push(`ladder ${ld.id}: pos / foot / top must be [x, y]`);
    if (!isNum(ld.level_from) || !isNum(ld.level_to) || ld.level_to - ld.level_from < 1) e.push(`ladder ${ld.id}: level_to must be above level_from`);
    else if (isNum(ld.rungs) && Math.abs(ld.rungs * RUNG_PITCH + (ld.plinth_cm ?? 0) - levelZ(ld.level_to - ld.level_from)) > 1) e.push(`ladder ${ld.id}: rungs x pitch + plinth must equal ${levelZ(ld.level_to - ld.level_from)} cm`);
  }
  if (l.cut && l.cut.level !== CUT_LEVEL) e.push(`cut: level must be ${CUT_LEVEL}`);
  const mandatory = (l.routes ?? []).filter((r) => r.mandatory);
  if (mandatory.length !== 1) e.push(`routes: exactly one mandatory route (found ${mandatory.length})`);
  for (const r of l.routes ?? []) {
    if (r.layers && r.layers.length !== r.points.length) e.push(`route ${r.id}: layers must match points (${r.layers.length} vs ${r.points.length})`);
    for (const k of r.links ?? []) if (k.at < 1 || k.at >= r.points.length) e.push(`route ${r.id}: link at ${k.at} out of range`);
  }
  return e;
}

/** The named layers (walkable surfaces) a layout has: 'ground', 'cut', 'roof:<b>', 'floor:<b>:<n>' (none inside a hollow building). */
export function layersOf(l: GenLayout): string[] {
  const out = ['ground'];
  if (l.cut) out.push('cut');
  for (const b of l.buildings) {
    if (!b.hollow) for (let n = 1; n < b.floors; n++) out.push(`floor:${b.id}:${n}`);
    if (b.roof_walkable) out.push(`roof:${b.id}`);
  }
  return out;
}

/** The height level of a layer: ground 0, the cut -1, floor n = n, a roof = the building's `floors`. */
export function layerLevel(l: GenLayout, layer: string): number {
  if (layer === 'ground') return 0;
  if (layer === 'cut') return l.cut?.level ?? CUT_LEVEL;
  const m = /^(roof|floor):([^:]+)(?::(\d+))?$/.exec(layer);
  if (!m) throw new Error(`unknown layer "${layer}"`);
  const b = l.buildings.find((x) => x.id === m[2]);
  if (!b) throw new Error(`layer "${layer}": no building "${m[2]}"`);
  return m[1] === 'roof' ? b.floors : Number(m[3] ?? 1);
}

/** Every height level the layout uses, lowest first. */
export const levelsOf = (l: GenLayout): number[] => [...new Set(layersOf(l).map((x) => layerLevel(l, x)))].sort((a, b) => a - b);

const rectPoly = ([a, b]: [Vec2, Vec2]): Vec2[] => [[a[0], a[1]], [b[0], a[1]], [b[0], b[1]], [a[0], b[1]]];

/** Walls of one layer as lab walls (the parapet of a roof, the interior walls of a floor, the ground's everything). */
export function layerWalls(l: GenLayout, layer: string): LayoutWall[] {
  if (layer === 'ground') {
    return l.walls.filter((w) => (w.floor ?? 0) === 0).map(({ floor: _f, kind: _k, building: _b, ...w }) => w);
  }
  if (layer === 'cut') {
    if (!l.cut) return [];
    // the trench walls as the trench polygon's edges (the floor is bounded by them); crossings are above the climber's head
    const p = l.cut.poly;
    return p.map((a, i) => ({ id: `cut_edge_${i}`, a, b: p[(i + 1) % p.length], height: CUT_DEPTH, thickness: 20 }));
  }
  const m = /^(roof|floor):([^:]+)(?::(\d+))?$/.exec(layer);
  if (!m) throw new Error(`unknown layer "${layer}"`);
  const b = l.buildings.find((x) => x.id === m[2]);
  if (!b) throw new Error(`layer "${layer}": no building "${m[2]}"`);
  const walls: LayoutWall[] = [];
  const fp = rectPoly(b.footprint);
  if (m[1] === 'roof') {
    // a parapet all round: nobody walks off the roof; the stair / ladder heads lie inside the footprint
    fp.forEach((a, i) => walls.push({ id: `${b.id}_parapet_${i}`, a, b: fp[(i + 1) % 4], height: 100, thickness: 20 }));
    return walls;
  }
  const floor = Number(m[3] ?? 1);
  if (b.hollow || floor >= b.floors) throw new Error(`layer "${layer}": ${b.id} has no floor ${floor}`);
  fp.forEach((a, i) => walls.push({ id: `${b.id}_f${floor}_perimeter_${i}`, a, b: fp[(i + 1) % 4], height: LEVEL_HEIGHT, thickness: 30 }));
  for (const w of l.walls) if (w.building === b.id && (w.floor ?? 0) === floor) walls.push({ id: w.id, a: w.a, b: w.b, height: w.height, thickness: w.thickness });
  return walls;
}

/** The lab-compatible zone kind of a generated zone (the audit / the sim classify by the lab kinds only). */
export function labZoneKind(kind: string): ZoneKind {
  return (ZONE_KINDS as readonly string[]).includes(kind) ? (kind as ZoneKind) : 'open';
}

/**
 * One layer of a generated layout as a plain lab layout (schema codex.formation_layout/1): the simulator, the audit and the map
 * builder read it as they read formation_lab_01.json. Routes are cut to the legs on that layer (a route changing layers is
 * split: each on-layer run of points becomes "<route>_L<k>"); the spawn stays the layout's (ground) or the layer's first route point.
 */
export function toFormationLayout(l: GenLayout, layer = 'ground'): Layout {
  const walls = layerWalls(l, layer);
  const routes: LayoutRoute[] = [];
  for (const r of l.routes) {
    const layers = r.layers ?? r.points.map(() => 'ground');
    let run: Vec2[] = [];
    let k = 0;
    const flush = () => {
      if (run.length >= 2) {
        const id = layers.every((x) => x === layer) ? r.id : `${r.id}_L${k++}`;
        // a split route keeps only the expectations of the zones THIS run enters (sampled every metre along its legs)
        const samples: Vec2[] = [];
        for (let i = 1; i < run.length; i++) {
          const a = run[i - 1], b = run[i], L = Math.hypot(b[0] - a[0], b[1] - a[1]);
          for (let s = 0; s <= L; s += 100) samples.push([a[0] + ((b[0] - a[0]) * s) / L, a[1] + ((b[1] - a[1]) * s) / L]);
        }
        samples.push(run[run.length - 1]);
        const expect = layer === 'ground' ? r.expect?.filter((x) => { const z = l.zones.find((zz) => zz.id === x.zone); return !!z && samples.some((p) => pointInPoly(p, z.poly)); }) : undefined;
        routes.push({ id, pace: r.pace, points: run, expect: expect && expect.length ? expect : undefined, threats: r.threats, slide: r.slide, expect_bypass: r.expect_bypass });
      }
      run = [];
    };
    r.points.forEach((p, i) => { if (layers[i] === layer) run.push(p); else flush(); });
    flush();
  }
  const zones: LayoutZone[] = layer === 'ground' ? l.zones.map((z) => ({ id: z.id, kind: labZoneKind(z.kind), poly: z.poly })) : [];
  const first = routes[0]?.points[0];
  return {
    schema: LAYOUT_SCHEMA, name: layer === 'ground' ? l.name : `${l.name}@${layer}`, bounds: l.bounds, ground_z: l.ground_z,
    spawn: layer === 'ground' || !first ? l.spawn : { pos: first, yaw: 0 }, walls, zones, routes,
    props: layer === 'ground' ? l.covers.map((c) => ({ id: c.id, kind: 'heat_source' as const, pos: c.pos })).slice(0, 0) : undefined,
  };
}

/** The mandatory route (validateGenLayout guarantees exactly one). */
export const mandatoryRoute = (l: GenLayout): GenRoute => l.routes.find((r) => r.mandatory) ?? l.routes[0];

/** Axis-aligned rectangle helpers shared by the generator and the evaluator. */
export const rect = {
  poly: rectPoly,
  centre: ([a, b]: [Vec2, Vec2]): Vec2 => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2],
  contains: ([a, b]: [Vec2, Vec2], p: Vec2, margin = 0): boolean => p[0] >= a[0] - margin && p[0] <= b[0] + margin && p[1] >= a[1] - margin && p[1] <= b[1] + margin,
  overlaps: (r: [Vec2, Vec2], s: [Vec2, Vec2], gap = 0): boolean => !(r[1][0] + gap <= s[0][0] || s[1][0] + gap <= r[0][0] || r[1][1] + gap <= s[0][1] || s[1][1] + gap <= r[0][1]),
  size: ([a, b]: [Vec2, Vec2]): Vec2 => [b[0] - a[0], b[1] - a[1]],
};
