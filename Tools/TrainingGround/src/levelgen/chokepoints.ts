// Training Ground / level generator - the CHOKEPOINT / danger analysis (designer 2026-11-16, docs/port/LEVELGEN_LAYERS_REPORT.md):
// where on a map ambushes and jams happen, with a score 0..1 and the reasons.
//   Lines: the routes' ground nav paths, the start -> goals path, the street centre lines, sampled every sampling.step_m.
//   Per sample: the passage WIDTH = the nearest obstacle + the obstacle opposite it (exact, cm), once for the structural obstacles
//   (walls, buildings, large covers, the canal, bridge sides, the map edge) and once with the clutter (covers); the clutter of the
//   street round it (covers per window, blocked share); tall objects on both sides (a canyon).
//   Threats (how the enemies attack; their numbers are READ from the game configs - threatNumbers):
//     1 drop-down ambush   - jumpers from roofs / thick wall tops within the drop-link height + gap (falls.json)
//     2 window fire        - PMC at windows of enterable buildings, rifle range + falloff (weapons_tuning.json)
//     3 marksmen on roofs  - reachable roofs, the marksman's rifle and height-boosted sight (weapons_tuning / enemy_perception)
//                            spots at the roof RIM (rim_stand_inset_m, clear of the corners) counting only targets OUTWARD of their edge (analysis v2; firingGaps.ts)
//     4 hidden enemies     - spots a short dash from the line that the squad sees only at close range; stealth (stealth_mode.json)
//   A PLACE = a stretch of flagged samples; no alternative route = the stretch is blocked and the way round is searched again.
//   Output: LayoutAnalysis (chokepoints + dropAmbushSpots + windowsCovering + overwatchRoofs + hidingSpots), saved into the layout
//   JSON (`analysis`) on «Принять» for the game / the PMC AI. All thresholds: Content/Data/LevelGen/chokepoints.json.
// Pure TS (browser + node), deterministic: no randomness, no clock, stable ordering, rounded output.
import cfgJson from '../../../../Content/Data/LevelGen/chokepoints.json' with { type: 'json' };
import fallsJson from '../../../../Content/Data/Movement/falls.json' with { type: 'json' };
import perceptionJson from '../../../../Content/Data/AI/enemy_perception.json' with { type: 'json' };
import stealthJson from '../../../../Content/Data/AI/stealth_mode.json' with { type: 'json' };
import weaponsJson from '../../../../Content/Data/Weapons/weapons_tuning.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { dirOf, pointInPoly, yawOf } from '../movement/geometry.ts';
import type { Hit, Obstacle } from './analysisGeom.ts';
import { layoutObstacles, ObstacleIndex, WalkGrid } from './analysisGeom.ts';
import { assignLayers, coverFootprintM2, layoutEntities, sizeClassOf } from './layers.ts';
import { checkObjectives, groundNav } from './objectives.ts';
import { buildingRectPoly, footprintBBox, footprintPoly, inFootprint } from './rotation.ts';
import type { GenBuilding, GenLayout } from './schema.ts';
import { ANALYSIS_VERSION, levelZ, NEVER_CLIMBABLE, OPENING_DEFAULTS, unifyOpenings } from './schema.ts';
import { buildSimNav } from './simNav.ts';
import { openingOutwardDir } from './tacticalEvaluate.ts';

// ---- config -------------------------------------------------------------------------------------------------------------------

export const PLACE_TAGS = ['narrow', 'clutter', 'canyon', 'only_way', 'drop_ambush', 'window_fire', 'sniper', 'hiding'] as const;
export type PlaceTag = (typeof PLACE_TAGS)[number];
export interface Conditions { weather: string; time_of_day: string }
export interface ChokeConfig {
  schema: string;
  sampling: { step_m: number; threat_step_m: number; max_probe_m: number; skip_building_margin_cm: number; edge_margin_cm: number; routes: boolean; objectives: boolean; streets: boolean };
  narrow: { width_m: number; full_width_m: number; fan_deg: number };
  clutter: { window_m: number; half_band_m: number; min_obstacles: number; blocked_fraction: number; full_obstacles: number; full_blocked_fraction: number; lane_width_m: number; lane_full_width_m: number; lane_min_length_m: number };
  canyon: { tall_min_height_cm: number; side_max_m: number; min_share: number };
  redundancy: { probe_m: number; block_radius_m: number; detour_critical_m: number; search_margin_m: number; tag_min_value: number };
  places: { merge_gap_m: number; pad_m: number; max_length_m: number; dedupe_m: number; exposure_min: number; min_score: number; max_items: number };
  threats: {
    conditions: Conditions;
    drop_ambush: { jumper_names?: Record<string, string>; strike_reach_cm_default: number; wall_top_min_thickness_cm_default: number; ladder_only_factor_default: number; max_spots: number };
    window_fire: { weapon: string; half_fov_deg_default: number; target_height_cm_default: number; require_enterable: boolean; full_windows: number };
    overwatch: { weapon: string; archetype: string; eye_height_cm_default: number; rim_stand_inset_m: number; rim_corner_clear_m: number; outward_only: boolean; spot_step_m: number; spots_per_roof: number; min_covered_m: number; max_roofs: number };
    hiding: { eye_height_cm_default: number; enemy_profile_cm_default: number; rush_max_m: number; min_offset_m: number; adjacent_m: number; min_concealment_cm: number; approach_window_m: number; reveal_close_m: number;
      stealth_reveal_threshold_m: number; stealth_score: number; candidate_step_m: number; spot_spacing_m: number; threat_span_m: number; full_spots: number; max_spots: number };
  };
  weights: Record<PlaceTag | 'on_route', number>;
  kind_priority: PlaceTag[];
  kind_names: Record<PlaceTag, string>;
}
export const CHOKE = cfgJson as unknown as ChokeConfig;
export const CHOKE_SCHEMA = 'codex.levelgen_chokepoints/1';

/** The config's own checks (empty = valid). */
export function validateChokeConfig(c: ChokeConfig): string[] {
  const e: string[] = [];
  if (c.schema !== CHOKE_SCHEMA) e.push(`schema must be "${CHOKE_SCHEMA}"`);
  if (!(c.sampling.step_m > 0) || !(c.sampling.threat_step_m >= c.sampling.step_m)) e.push('sampling: 0 < step_m <= threat_step_m');
  if (!(c.narrow.width_m > c.narrow.full_width_m)) e.push('narrow: width_m > full_width_m');
  for (const k of [...PLACE_TAGS, 'on_route'] as const) if (!(c.weights[k] >= 0)) e.push(`weights.${k}: expected a number >= 0`);
  for (const k of PLACE_TAGS) { if (!c.kind_priority.includes(k)) e.push(`kind_priority: "${k}" missing`); if (!c.kind_names[k]) e.push(`kind_names.${k} missing`); }
  return e;
}

// ---- the enemies' numbers, read from the game configs --------------------------------------------------------------------------

type Num = Record<string, number>;
const FALLS = (fallsJson as unknown as { params: Num }).params;
const PERC = perceptionJson as unknown as { height_sight: { level_step_m: number; bonus_per_level: number; max_multiplier: number };
  sight_conditions: { squad_base_range_m: number; human_enemy_range_multiplier: number; weather: Num; time_of_day: Num }; archetypes: Record<string, Num> };
const STEALTH = stealthJson as unknown as Num;
const WEAP = weaponsJson as unknown as { range_falloff: { floor_multiplier: number; exponent: number }; weapons: Record<string, Num>; enemy_weapons: Record<string, Num> };

export interface ThreatNumbers {
  conditions: Conditions & { multiplier: number };
  drop: { min_drop_cm: number; max_drop_cm: number; max_gap_cm: number; strike_reach_cm: number; reach_cm: number; jumpers: string[]; wall_top_min_thickness_cm: number; ladder_only_factor: number };
  window: { weapon: string; effective_m: number; attack_m: number; half_fov_deg: number; target_height_cm: number };
  overwatch: { weapon: string; archetype: string; effective_m: number; attack_m: number; preferred_min_m: number; sight_ground_m: number; level_step_m: number; bonus_per_level: number; max_multiplier: number; eye_cm: number };
  hiding: { squad_sight_m: number; stealth_multiplier: number; stealth_sight_m: number; eye_cm: number; profile_cm: number };
  falloff: { floor: number; exponent: number };
  /** Every number with where it comes from (the page and the report print this list). */
  sources: { name: string; value: number | string; source: string }[];
}

/** The threat model's numbers for the given conditions: game configs first, the analysis config's *_default only where they are silent. */
export function threatNumbers(c: ChokeConfig = CHOKE, cond: Conditions = c.threats.conditions): ThreatNumbers {
  const sc = PERC.sight_conditions;
  const mult = (sc.weather[cond.weather] ?? 1) * (sc.time_of_day[cond.time_of_day] ?? 1);
  const jumpers = Object.keys(FALLS).filter((k) => k.startsWith('enemy_drops.') && FALLS[k] === 2).map((k) => k.slice('enemy_drops.'.length)).sort();
  const D = c.threats.drop_ambush, Wn = c.threats.window_fire, O = c.threats.overwatch, H = c.threats.hiding;
  const rifle = WEAP.weapons[Wn.weapon] ?? {}, mk = WEAP.enemy_weapons[O.weapon] ?? {}, arch = PERC.archetypes[O.archetype] ?? {};
  const t: ThreatNumbers = {
    conditions: { ...cond, multiplier: mult },
    drop: { min_drop_cm: FALLS['link.min_drop_cm'], max_drop_cm: FALLS['link.max_drop_cm'], max_gap_cm: FALLS['link.max_gap_cm'], strike_reach_cm: D.strike_reach_cm_default,
      reach_cm: FALLS['link.max_gap_cm'] + D.strike_reach_cm_default, jumpers, wall_top_min_thickness_cm: D.wall_top_min_thickness_cm_default, ladder_only_factor: D.ladder_only_factor_default },
    window: { weapon: Wn.weapon, effective_m: rifle.effective_range_m, attack_m: rifle.attack_range_m, half_fov_deg: Wn.half_fov_deg_default, target_height_cm: Wn.target_height_cm_default },
    overwatch: { weapon: O.weapon, archetype: O.archetype, effective_m: mk.effective_range_m, attack_m: mk.attack_range_m, preferred_min_m: mk.preferred_min_range_m ?? 0,
      sight_ground_m: (arch.sight_range_m ?? 0) * sc.human_enemy_range_multiplier * mult, level_step_m: PERC.height_sight.level_step_m, bonus_per_level: PERC.height_sight.bonus_per_level,
      max_multiplier: PERC.height_sight.max_multiplier, eye_cm: O.eye_height_cm_default },
    hiding: { squad_sight_m: sc.squad_base_range_m * mult, stealth_multiplier: STEALTH.detection_range_multiplier, stealth_sight_m: sc.squad_base_range_m * mult * STEALTH.detection_range_multiplier,
      eye_cm: H.eye_height_cm_default, profile_cm: H.enemy_profile_cm_default },
    falloff: { floor: WEAP.range_falloff.floor_multiplier, exponent: WEAP.range_falloff.exponent },
    sources: [],
  };
  const F = 'Content/Data/Movement/falls.json', P = 'Content/Data/AI/enemy_perception.json', S = 'Content/Data/AI/stealth_mode.json', Wj = 'Content/Data/Weapons/weapons_tuning.json', A = 'chokepoints.json (значение по умолчанию: в конфигах игры нет)';
  t.sources = [
    { name: 'спрыгивание: высота от, см', value: t.drop.min_drop_cm, source: `${F} link.min_drop_cm` },
    { name: 'спрыгивание: высота до, см', value: t.drop.max_drop_cm, source: `${F} link.max_drop_cm` },
    { name: 'спрыгивание: вынос от края, см', value: t.drop.max_gap_cm, source: `${F} link.max_gap_cm` },
    { name: 'спрыгивание: кто прыгает', value: jumpers.join(', '), source: `${F} enemy_drops.* = 2` },
    { name: 'спрыгивание: удар после приземления, см', value: t.drop.strike_reach_cm, source: `${A} drop_ambush.strike_reach_cm_default` },
    { name: 'спрыгивание: верх стены от толщины, см', value: t.drop.wall_top_min_thickness_cm, source: `${A} drop_ambush.wall_top_min_thickness_cm_default` },
    { name: 'спрыгивание: крыша только по лестнице, множитель', value: t.drop.ladder_only_factor, source: `${A} drop_ambush.ladder_only_factor_default` },
    { name: `окна: ${Wn.weapon} эффективная дальность, м`, value: t.window.effective_m, source: `${Wj} weapons.${Wn.weapon}.effective_range_m` },
    { name: `окна: ${Wn.weapon} предельная дальность, м`, value: t.window.attack_m, source: `${Wj} weapons.${Wn.weapon}.attack_range_m` },
    { name: 'спад точности: пол / степень', value: `${t.falloff.floor} / ${t.falloff.exponent}`, source: `${Wj} range_falloff` },
    { name: 'окна: сектор огня, ± град', value: t.window.half_fov_deg, source: `${A} window_fire.half_fov_deg_default` },
    { name: 'окна и крыши: высота цели, см', value: t.window.target_height_cm, source: `${A} window_fire.target_height_cm_default (C++ SightRules::ProfileHeight стоя)` },
    { name: `снайпер: ${O.weapon} эффективная / предельная, м`, value: `${t.overwatch.effective_m} / ${t.overwatch.attack_m}`, source: `${Wj} enemy_weapons.${O.weapon}` },
    { name: 'снайпер: предпочитает от, м', value: t.overwatch.preferred_min_m, source: `${Wj} enemy_weapons.${O.weapon}.preferred_min_range_m` },
    { name: `снайпер: обзор ${O.archetype} с земли, м`, value: Math.round(t.overwatch.sight_ground_m * 10) / 10, source: `${P} archetypes.${O.archetype}.sight_range_m x sight_conditions.human_enemy_range_multiplier x погода x время` },
    { name: 'снайпер: прибавка обзора за уровень 3 м / максимум', value: `${t.overwatch.bonus_per_level} / x${t.overwatch.max_multiplier}`, source: `${P} height_sight` },
    { name: 'снайпер и отряд: высота глаз, см', value: t.overwatch.eye_cm, source: `${A} eye_height_cm_default (C++ SightRules::EyeHeight стоя)` },
    { name: 'скрытый противник: обзор отряда, м', value: Math.round(t.hiding.squad_sight_m * 10) / 10, source: `${P} sight_conditions.squad_base_range_m x погода x время` },
    { name: 'скрытый противник: множитель скрытности', value: t.hiding.stealth_multiplier, source: `${S} detection_range_multiplier` },
    { name: 'скрытый противник: виден в режиме скрытности с, м', value: Math.round(t.hiding.stealth_sight_m * 10) / 10, source: 'обзор отряда x множитель скрытности' },
    { name: 'скрытый противник: профиль пригнувшегося, см', value: t.hiding.profile_cm, source: `${A} hiding.enemy_profile_cm_default (C++ SightRules::ProfileHeight пригнувшись)` },
    { name: 'условия (погода / время), множитель обзора', value: `${cond.weather} / ${cond.time_of_day} = x${Math.round(mult * 100) / 100}`, source: `${P} sight_conditions.weather / time_of_day` },
  ];
  return t;
}

/** Hit-chance multiplier by distance: 1 up to the effective range, down to `floor` at the attack range (range_falloff), 0 beyond. */
export function rangeFalloff(distM: number, effectiveM: number, attackM: number, f: { floor: number; exponent: number }): number {
  if (distM <= effectiveM) return 1;
  if (distM >= attackM) return distM === attackM ? f.floor : 0;
  const u = (distM - effectiveM) / (attackM - effectiveM);
  return f.floor + (1 - f.floor) * Math.pow(1 - u, f.exponent);
}

// ---- output -------------------------------------------------------------------------------------------------------------------

export type RouteClass = 'mandatory' | 'variant' | 'objectives' | 'street';
export interface SideInfo { src: string; what: string; height_cm: number; dist_m: number }
export interface Chokepoint {
  id: string;
  kind: PlaceTag;
  kind_name: string;
  /** Everything that applies (the kind is the first of chokepoints.json kind_priority among them). */
  tags: PlaceTag[];
  score: number;
  /** The narrowest point (UE cm). */
  pos: Vec2;
  /** The stretch's centre line and its outline (the line widened by half the passage). */
  line: Vec2[];
  poly: Vec2[];
  length_m: number;
  /** The structural passage width at the narrowest point, m (null: open ground beyond sampling.max_probe_m). */
  width_m: number | null;
  /** The free passage between obstacles of any kind there, m. */
  lane_m: number | null;
  route: RouteClass;
  /** The lines the stretch lies on: "route:<id>", "objectives", "street:<zone>". */
  sources: string[];
  /** The 0..1 value of every score term (score = min(1, sum of weights x values)). */
  values: Record<PlaceTag | 'on_route', number>;
  numbers: {
    obstacles_per_window: number; blocked_fraction: number; canyon_share: number;
    side_a: SideInfo | null; side_b: SideInfo | null;
    /** 'none' = no way round inside the search box; 'detour' = a way round, detour_m longer; 'n/a' = the stretch ends a line. */
    alternative: 'none' | 'detour' | 'n/a'; detour_m: number | null;
    drop_spots: string[]; windows: string[]; overwatch: string[]; hiding_spots: string[];
    /** The shortest reveal distance among its hiding spots, m: of the spots that hide anyone (null: stealth-only spots) / under stealth. */
    reveal_m: number | null; reveal_stealth_m: number | null;
  };
  /** The reasons in the designer's language (the tooltip and the list print them). */
  reasons: string[];
}
export interface DropAmbushSpot { id: string; surface: string; kind: 'roof' | 'wall_top'; edge: Vec2; land: Vec2; drop_cm: number; gap_cm: number; access: 'stairs' | 'ladder' | 'none' | 'wall'; jumpers: string[]; line: string; places: string[] }
export interface WindowCover { id: string; building: string; floor: number; kind: string; pos: Vec2; yaw: number; covered_m: number; min_dist_m: number; places: string[] }
export interface OverwatchSpot { pos: Vec2; facing_yaw: number; covered_m: number; longest_los_m: number }
export interface OverwatchRoof { building: string; level: number; height_cm: number; range_m: number; access: string[]; spots: OverwatchSpot[]; places: string[] }
export interface HidingSpot {
  id: string; pos: Vec2;
  /** Yaw (degrees, UE) from the spot towards the route point it threatens. */
  facing_yaw: number;
  /** From how far the squad walking the line first sees a (crouched) enemy here, m; under stealth. */
  reveal_normal_m: number; reveal_stealth_m: number;
  /** Kept only because stealth cuts the reveal distance. */
  stealth_only: boolean;
  /** No sight line from the line at all inside the approach window: the enemy steps out from behind the corner. */
  blind: boolean;
  /** Walking distance to the line, m. */
  rush_m: number;
  cover: string; cover_height_cm: number;
  threatens: { line: string; route?: string; pos: Vec2 };
  place?: string;
}
export interface LayoutAnalysis {
  version: number;
  /** «Принять» forced with this many buildings failing traversal (buildingTraversal.ts, BUILDING_GENERATOR.md 5). Absent = none. */
  forced_buildings?: number;
  /** The hash of the geometry the analysis was made for (geometryHash): another hash = the map changed, the analysis is stale. */
  geometry_hash: string;
  conditions: Conditions;
  /** The thresholds that decide what a place is (for the reader of the JSON). */
  thresholds: { narrow_width_m: number; clutter_min_obstacles: number; clutter_blocked_fraction: number; lane_width_m: number; canyon_tall_cm: number; canyon_side_max_m: number; detour_critical_m: number;
    drop_reach_cm: number; drop_min_cm: number; drop_max_cm: number; window_range_m: number; sniper_range_m: number; reveal_close_m: number; stealth_sight_m: number; min_score: number };
  chokepoints: Chokepoint[];
  dropAmbushSpots: DropAmbushSpot[];
  windowsCovering: WindowCover[];
  overwatchRoofs: OverwatchRoof[];
  hidingSpots: HidingSpot[];
  stats: { lines: number; length_m: number; samples: number; places_found: number };
}

/** FNV-1a (two 32-bit lanes) of what the analysis reads: the same string for the same geometry, routes and goals. */
export function geometryHash(l: GenLayout): string {
  const s = JSON.stringify([l.bounds, l.walls, l.buildings, l.covers, l.cut ?? null, l.ladders, l.zones, l.routes, l.objectives ?? null]);
  let a = 0x811c9dc5, b = 0x01000193;
  for (let i = 0; i < s.length; i++) { const ch = s.charCodeAt(i); a = Math.imul(a ^ ch, 0x01000193) >>> 0; b = Math.imul(b ^ (ch + i), 0x85ebca6b) >>> 0; }
  return a.toString(16).padStart(8, '0') + b.toString(16).padStart(8, '0');
}

/** The layout without a stale analysis (a map edited after the analysis was made); the firing gaps planned from it go too (firingGaps.ts). */
export function withoutStaleAnalysis(l: GenLayout): GenLayout {
  if (!l.analysis || l.analysis.geometry_hash === geometryHash(l)) return l;
  const next = { ...l };
  delete next.analysis;
  delete next.firing_gaps;
  return next;
}

// ---- helpers ------------------------------------------------------------------------------------------------------------------

const clamp01 = (v: number): number => Math.max(0, Math.min(1, v));
const r0 = (v: number): number => Math.round(v);
const rN = (v: number, d = 1): number => { const k = 10 ** d; const x = Math.round(v * k) / k; return Object.is(x, -0) ? 0 : x; };
const dist = (a: Vec2, b: Vec2): number => Math.hypot(a[0] - b[0], a[1] - b[1]);
const rotDir = (d: Vec2, deg: number): Vec2 => { const r = dirOf(deg); return [d[0] * r[0] - d[1] * r[1], d[0] * r[1] + d[1] * r[0]]; };
/** Russian plural: 1 окно, 2 окна, 5 окон. */
export const plural = (n: number, one: string, few: string, many: string): string => { const a = Math.abs(n) % 100, b = a % 10; return a > 10 && a < 20 ? many : b === 1 ? one : b >= 2 && b <= 4 ? few : many; };
const COVER_RU: Record<string, string> = { jersey: 'бетонный блок', sandbag: 'мешки', crate: 'ящик', wreck: 'остов машины', barrel: 'бочка', silo: 'силос', boiler: 'котёл' };

function describe(o: Obstacle): string {
  if (o.src === 'bounds') return 'край карты';
  if (o.src === 'cut') return 'канал';
  if (o.src.startsWith('crossing:')) return 'край моста';
  if (o.src.startsWith('cover:')) return `${COVER_RU[o.kind] ?? o.kind} ${o.src.slice(6)}`;
  if (o.building) return `здание ${o.building}`;
  return `${o.kind === 'fence' ? 'забор' : o.kind === 'cut_wall' ? 'стена канала' : 'стена'} ${o.src.slice(5)}`;
}

/** Points every `step` cm along a polyline (the first point, then each multiple of the step, then the last point). */
export function samplePolyline(points: readonly Vec2[], step: number): Vec2[] {
  const out: Vec2[] = [];
  if (!points.length) return out;
  out.push(points[0]);
  let carry = 0;
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1], b = points[i], L = dist(a, b);
    if (L < 1e-6) continue;
    let s = step - carry;
    while (s <= L) { out.push([a[0] + ((b[0] - a[0]) * s) / L, a[1] + ((b[1] - a[1]) * s) / L]); s += step; }
    carry = L - (s - step);
  }
  const last = points[points.length - 1];
  if (dist(out[out.length - 1], last) > 1) out.push(last);
  return out;
}

const distToPolyEdge = (p: Vec2, poly: readonly Vec2[]): { d: number; q: Vec2 } => {
  let best = Infinity, q: Vec2 = poly[0];
  for (let i = 0; i < poly.length; i++) {
    const a = poly[i], b = poly[(i + 1) % poly.length], dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
    const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2));
    const c: Vec2 = [a[0] + dx * t, a[1] + dy * t], d = dist(p, c);
    if (d < best) { best = d; q = c; }
  }
  return { d: best, q };
};

interface Line { id: string; cls: RouteClass; route?: string; pts: Vec2[]; s: Float64Array }
interface Place { line: number; i0: number; i1: number; tier: 1 | 2; /** The whole flagged run this piece was cut from (the detour search goes round all of it). */ r0: number; r1: number }

// ---- the analysis ---------------------------------------------------------------------------------------------------------------

export interface AnalyzeOptions { config?: ChokeConfig; conditions?: Conditions }

/** The chokepoint / threat analysis of a layout (see the file header). Deterministic: the same layout + config = the same JSON. */
export function analyzeLayout(l: GenLayout, o: AnalyzeOptions = {}): LayoutAnalysis {
  const c = o.config ?? CHOKE;
  const cond = o.conditions ?? c.threats.conditions;
  const T = threatNumbers(c, cond);
  const step = c.sampling.step_m * 100, probe = c.sampling.max_probe_m * 100;
  const kT = Math.max(1, Math.round(c.sampling.threat_step_m / c.sampling.step_m)), tStepM = kT * c.sampling.step_m;

  // -- obstacles, sight, the walk grid
  const items = layoutObstacles(l);
  const index = new ObstacleIndex(items, l.bounds.min, l.bounds.max);
  const sight = new ObstacleIndex(items.filter((it) => it.obstacle.height > 0), l.bounds.min, l.bounds.max);
  const conceal = new ObstacleIndex(items.filter((it) => it.obstacle.height >= c.threats.hiding.min_concealment_cm), l.bounds.min, l.bounds.max);
  const simNav = buildSimNav(l), wallsNav = groundNav(l);
  const cutPoly = l.cut?.poly, crossPolys = l.cut?.crossings.map((x) => x.poly) ?? [];
  const inCanal = (p: Vec2): boolean => !!cutPoly && pointInPoly(p, cutPoly) && !crossPolys.some((x) => pointInPoly(p, x));
  const grid = new WalkGrid(simNav, inCanal);
  const bBoxes = l.buildings.map((b) => footprintBBox(b));
  const inBuilding = (p: Vec2, margin: number): boolean => l.buildings.some((b, i) => p[0] >= bBoxes[i][0][0] - margin && p[0] <= bBoxes[i][1][0] + margin && p[1] >= bBoxes[i][0][1] - margin && p[1] <= bBoxes[i][1][1] + margin && inFootprint(b, p, margin));
  const skipMargin = c.sampling.skip_building_margin_cm, em = c.sampling.edge_margin_cm;
  const atEdge = (p: Vec2): boolean => p[0] < l.bounds.min[0] + em || p[0] > l.bounds.max[0] - em || p[1] < l.bounds.min[1] + em || p[1] > l.bounds.max[1] - em;

  // -- lines
  const lines: Line[] = [];
  const addRuns = (id: string, cls: RouteClass, route: string | undefined, poly: readonly Vec2[], needFree: boolean) => {
    const pts = samplePolyline(poly, step);
    let run: Vec2[] = [], k = 0;
    const flush = () => {
      if (run.length * step >= 200) {
        const s = new Float64Array(run.length);
        for (let i = 1; i < run.length; i++) s[i] = s[i - 1] + dist(run[i - 1], run[i]);
        lines.push({ id: k ? `${id}#${k}` : id, cls, route, pts: run, s }); k++;
      }
      run = [];
    };
    for (const p of pts) { if (!inBuilding(p, skipMargin) && !inCanal(p) && !atEdge(p) && (!needFree || grid.free(p))) run.push(p); else flush(); }
    flush();
  };
  if (c.sampling.routes) for (const r of l.routes) {
    const layers = r.layers ?? r.points.map(() => 'ground');
    let poly: Vec2[] = [], k = 0;
    const flush = () => { if (poly.length >= 2) addRuns(k ? `route:${r.id}@${k}` : `route:${r.id}`, r.mandatory ? 'mandatory' : 'variant', r.id, poly, false); if (poly.length >= 2) k++; poly = []; };
    r.points.forEach((p, i) => {
      if (layers[i] !== 'ground') { flush(); return; }
      if (!poly.length) { poly.push(p); return; }
      const a = poly[poly.length - 1];
      const path = simNav.path(a, p) ?? wallsNav.path(a, p) ?? [a, p];
      poly.push(...path.slice(1));
    });
    flush();
  }
  if (c.sampling.objectives && l.objectives) {
    let best: Vec2[] = [];
    try { best = checkObjectives(l, wallsNav).bestPath; } catch { best = []; }
    if (best.length >= 2) addRuns('objectives', 'objectives', undefined, best, false);
  }
  if (c.sampling.streets) {
    const roads = new Set(assignLayers(layoutEntities(l)).byLayer.roads?.map((e) => e.id) ?? []);
    for (const z of l.zones) {
      if (!roads.has(z.id) || z.poly.length < 3) continue;
      const xs = z.poly.map((p) => p[0]), ys = z.poly.map((p) => p[1]);
      const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys), cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      addRuns(`street:${z.id}`, 'street', undefined, x1 - x0 >= y1 - y0 ? [[x0, cy], [x1, cy]] : [[cx, y0], [cx, y1]], true);
    }
  }

  // -- per-sample geometry
  const fan = [0, -c.narrow.fan_deg, c.narrow.fan_deg, -c.narrow.fan_deg / 2, c.narrow.fan_deg / 2];
  const N = lines.map((L) => L.pts.length);
  const wHard = lines.map((_, li) => new Float32Array(N[li]).fill(Infinity)), wAll = lines.map((_, li) => new Float32Array(N[li]).fill(Infinity));
  const sideA = lines.map((_, li) => new Array<Hit | null>(N[li]).fill(null)), sideB = lines.map((_, li) => new Array<Hit | null>(N[li]).fill(null));
  const tall = lines.map((_, li) => new Uint8Array(N[li]));
  const cCount = lines.map((_, li) => new Uint16Array(N[li])), cFrac = lines.map((_, li) => new Float32Array(N[li]));
  const measure = (p: Vec2, hardOnly: boolean): { w: number; a: Hit | null; b: Hit | null } => {
    const n = index.nearest(p, probe, hardOnly);
    if (!n || n.dist < 1) return { w: Infinity, a: null, b: null };
    const d: Vec2 = [(p[0] - n.point[0]) / n.dist, (p[1] - n.point[1]) / n.dist];
    let best: Hit | null = null;
    for (const phi of fan) { const h = index.ray(p, phi ? rotDir(d, phi) : d, probe, hardOnly); if (h && (!best || h.dist < best.dist)) best = h; }
    return { w: best ? n.dist + best.dist : Infinity, a: n, b: best };
  };
  // clutter: the covers that are not structural, bucketed by their centre
  const soft = l.covers.filter((cv) => sizeClassOf(cv.size[2], coverFootprintM2(cv), cv.kind) !== 'large').map((cv) => ({ pos: cv.pos, area: coverFootprintM2(cv) }));
  const BK = 1000, bw = Math.ceil((l.bounds.max[0] - l.bounds.min[0]) / BK) + 1, bh = Math.ceil((l.bounds.max[1] - l.bounds.min[1]) / BK) + 1;
  const bkt = (p: Vec2): [number, number] => [Math.max(0, Math.min(bw - 1, Math.floor((p[0] - l.bounds.min[0]) / BK))), Math.max(0, Math.min(bh - 1, Math.floor((p[1] - l.bounds.min[1]) / BK)))];
  const softCells: number[][] = Array.from({ length: bw * bh }, () => []);
  soft.forEach((s, i) => { const [bi, bj] = bkt(s.pos); softCells[bj * bw + bi].push(i); });
  const band = c.clutter.half_band_m * 100, tallMin = c.canyon.tall_min_height_cm, sideMax = c.canyon.side_max_m * 100;
  lines.forEach((L, li) => {
    const near: number[][] = new Array(L.pts.length);
    for (let i = 0; i < L.pts.length; i++) {
      const p = L.pts[i];
      const mh = measure(p, true), ma = measure(p, false);
      wHard[li][i] = mh.w; wAll[li][i] = ma.w; sideA[li][i] = mh.a; sideB[li][i] = mh.b;
      if (mh.a && mh.b && index.obstacles[mh.a.obs].height >= tallMin && index.obstacles[mh.b.obs].height >= tallMin && mh.a.dist <= sideMax && mh.b.dist <= sideMax) tall[li][i] = 1;
      const [bi, bj] = bkt(p), list: number[] = [];
      for (let j = Math.max(0, bj - 1); j <= Math.min(bh - 1, bj + 1); j++) for (let i2 = Math.max(0, bi - 1); i2 <= Math.min(bw - 1, bi + 1); i2++) for (const si of softCells[j * bw + i2]) if (dist(soft[si].pos, p) <= band) list.push(si);
      near[i] = list;
    }
    const halfWin = (c.clutter.window_m * 100) / 2, bandArea = c.clutter.window_m * 2 * c.clutter.half_band_m;
    let lo = 0, hi = 0;
    for (let i = 0; i < L.pts.length; i++) {
      while (L.s[i] - L.s[lo] > halfWin) lo++;
      while (hi + 1 < L.pts.length && L.s[hi + 1] - L.s[i] <= halfWin) hi++;
      const set = new Set<number>();
      for (let j = lo; j <= hi; j++) for (const si of near[j]) set.add(si);
      let area = 0;
      for (const si of set) area += soft[si].area;
      cCount[li][i] = set.size; cFrac[li][i] = area / bandArea;
    }
  });

  // -- threat samples (every kT-th sample) in buckets
  interface TS { line: number; idx: number; p: Vec2 }
  const tsamples: TS[] = [];
  lines.forEach((L, li) => { for (let i = 0; i < L.pts.length; i += kT) tsamples.push({ line: li, idx: i, p: L.pts[i] }); });
  const tsCells: number[][] = Array.from({ length: bw * bh }, () => []);
  tsamples.forEach((t, i) => { const [bi, bj] = bkt(t.p); tsCells[bj * bw + bi].push(i); });
  const samplesNear = (p: Vec2, radius: number, visit: (t: TS, ti: number, d: number) => void) => {
    const r = Math.ceil(radius / BK), [bi, bj] = bkt(p);
    for (let j = Math.max(0, bj - r); j <= Math.min(bh - 1, bj + r); j++) for (let i = Math.max(0, bi - r); i <= Math.min(bw - 1, bi + r); i++) for (const ti of tsCells[j * bw + i]) {
      const d = dist(tsamples[ti].p, p);
      if (d <= radius) visit(tsamples[ti], ti, d);
    }
  };
  const vDrop = lines.map((_, li) => new Float32Array(N[li])), vWin = lines.map((_, li) => new Float32Array(N[li])), vSnip = lines.map((_, li) => new Float32Array(N[li])), vHide = lines.map((_, li) => new Float32Array(N[li]));
  const spread = (arr: Float32Array[], li: number, idx: number, v: number, half = Math.ceil(kT / 2)) => { for (let i = Math.max(0, idx - half); i <= Math.min(N[li] - 1, idx + half); i++) if (v > arr[li][i]) arr[li][i] = v; };

  // -- threat 1: drop-down ambush
  interface Surface { id: string; kind: 'roof' | 'wall_top'; poly: Vec2[]; height: number; access: DropAmbushSpot['access']; factor: number }
  const surfaces: Surface[] = [];
  const roofAccess = (b: GenBuilding): string[] => [
    ...b.stairs.filter((s) => s.to >= b.floors).map((s) => `stair:${s.id}`),
    ...l.ladders.filter((k) => k.to === `roof:${b.id}`).map((k) => `ladder:${k.id}`),
    ...(b.hatches ?? []).map((h) => `hatch:${h.id}`),
  ];
  for (const b of l.buildings) {
    if (!b.roof_walkable || NEVER_CLIMBABLE.includes(b.set_piece) || b.height < T.drop.min_drop_cm || b.height > T.drop.max_drop_cm) continue;
    const acc = roofAccess(b), access: Surface['access'] = acc.some((x) => x.startsWith('stair:')) ? 'stairs' : acc.length ? 'ladder' : 'none';
    surfaces.push({ id: `roof:${b.id}`, kind: 'roof', poly: footprintPoly(b), height: b.height, access, factor: access === 'stairs' ? 1 : T.drop.ladder_only_factor });
  }
  for (const w of l.walls) {
    if ((w.floor ?? 0) !== 0 || w.building || w.thickness < T.drop.wall_top_min_thickness_cm || w.height < T.drop.min_drop_cm || w.height > T.drop.max_drop_cm) continue;
    const L = Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]) || 1, n: Vec2 = [(-(w.b[1] - w.a[1]) / L) * (w.thickness / 2), ((w.b[0] - w.a[0]) / L) * (w.thickness / 2)];
    surfaces.push({ id: `wall:${w.id}`, kind: 'wall_top', poly: [[w.a[0] + n[0], w.a[1] + n[1]], [w.b[0] + n[0], w.b[1] + n[1]], [w.b[0] - n[0], w.b[1] - n[1]], [w.a[0] - n[0], w.a[1] - n[1]]], height: w.height, access: 'wall', factor: T.drop.ladder_only_factor });
  }
  interface RawDrop { surface: Surface; line: number; idx: number; gap: number; edge: Vec2; land: Vec2 }
  const rawDrops: RawDrop[] = [];
  for (const sf of surfaces) {
    const cx = sf.poly.reduce((s, p) => s + p[0], 0) / sf.poly.length, cy = sf.poly.reduce((s, p) => s + p[1], 0) / sf.poly.length;
    const rad = Math.max(...sf.poly.map((p) => Math.hypot(p[0] - cx, p[1] - cy))) + T.drop.reach_cm;
    const hits: RawDrop[] = [];
    samplesNear([cx, cy], rad, (t) => {
      if (pointInPoly(t.p, sf.poly)) return;
      const e = distToPolyEdge(t.p, sf.poly);
      if (e.d > T.drop.reach_cm) return;
      spread(vDrop, t.line, t.idx, sf.factor);
      hits.push({ surface: sf, line: t.line, idx: t.idx, gap: e.d, edge: e.q, land: t.p });
    });
    // one spot per run of consecutive threat samples of a line: where the gap is smallest
    hits.sort((a, b) => a.line - b.line || a.idx - b.idx);
    let run: RawDrop[] = [];
    const flush = () => { if (run.length) rawDrops.push(run.reduce((m, x) => (x.gap < m.gap ? x : m), run[0])); run = []; };
    for (const h of hits) { if (run.length && (h.line !== run[run.length - 1].line || h.idx - run[run.length - 1].idx > kT)) flush(); run.push(h); }
    flush();
  }

  // -- threat 2: window fire
  interface Win { id: string; building: string; floor: number; kind: string; pos: Vec2; yaw: number; count: number; minD: number }
  const wins: Win[] = [];
  const winAt = new Map<number, { w: number; v: number }[]>();
  const cosFov = Math.cos((T.window.half_fov_deg * Math.PI) / 180);
  for (const b of l.buildings) {
    if (c.threats.window_fire.require_enterable && !b.enterable) continue;
    unifyOpenings(b).forEach((op, oi) => {
      if (op.kind !== 'window' && op.kind !== 'loophole') return;
      if (op.floor < 0 || op.floor >= b.floors || (b.hollow && op.floor > 0)) return;
      const d = openingOutwardDir(b, op), origin: Vec2 = [op.pos[0] + d[0] * 40, op.pos[1] + d[1] * 40];
      const def = OPENING_DEFAULTS[op.kind], sill = op.sill_height ?? def.sill, top = op.top_height ?? def.top;
      const eyeZ = levelZ(op.floor) + Math.min(T.overwatch.eye_cm, (sill + top) / 2);
      const w: Win = { id: op.id ?? `${b.id}#${oi}`, building: b.id, floor: op.floor, kind: op.kind, pos: [r0(op.pos[0]), r0(op.pos[1])], yaw: rN(yawOf(d), 1), count: 0, minD: Infinity };
      const wi = wins.length;
      samplesNear(origin, T.window.attack_m * 100, (t, ti, dd) => {
        if (dd < 80) return;
        if (((t.p[0] - origin[0]) * d[0] + (t.p[1] - origin[1]) * d[1]) / dd < cosFov) return;
        const v = rangeFalloff(dd / 100, T.window.effective_m, T.window.attack_m, T.falloff);
        if (v <= 0 || !sight.los(origin, eyeZ, t.p, T.window.target_height_cm)) return;
        w.count++; if (dd < w.minD) w.minD = dd;
        let list = winAt.get(ti);
        if (!list) { list = []; winAt.set(ti, list); }
        list.push({ w: wi, v });
        spread(vWin, t.line, t.idx, v);
      });
      wins.push(w);
    });
  }

  // -- threat 3: marksmen on roofs
  interface Roof { b: GenBuilding; level: number; range: number; access: string[]; spots: (OverwatchSpot & { seen: number[] })[] }
  const roofs: Roof[] = [];
  const snipAt = new Map<number, number[]>();
  for (const b of l.buildings) {
    if (!b.roof_walkable || NEVER_CLIMBABLE.includes(b.set_piece)) continue;
    const access = roofAccess(b);
    if (!access.length) continue;
    const eyeZ = b.height + T.overwatch.eye_cm;
    const level = Math.floor(eyeZ / (T.overwatch.level_step_m * 100));
    const sightM = T.overwatch.sight_ground_m * Math.min(T.overwatch.max_multiplier, level >= 1 ? 1 + T.overwatch.bonus_per_level * level : 1);
    const rangeM = Math.min(sightM, T.overwatch.attack_m);
    const [[fx0, fy0], [fx1, fy1]] = b.footprint, ins = Math.min(c.threats.overwatch.rim_stand_inset_m * 100, (fx1 - fx0) / 4, (fy1 - fy0) / 4);
    const ring = buildingRectPoly(b, [[fx0 + ins, fy0 + ins], [fx1 - ins, fy1 - ins]]);
    // candidates on the ring at the rim (behind a firing gap); each knows the OUTWARD normal of its roof edge
    const rc: Vec2 = [ring.reduce((sum, q) => sum + q[0], 0) / ring.length, ring.reduce((sum, q) => sum + q[1], 0) / ring.length];
    const outN = (i: number): Vec2 => {
      const a = ring[i], q = ring[(i + 1) % ring.length], L = dist(a, q) || 1;
      let n: Vec2 = [(q[1] - a[1]) / L, -(q[0] - a[0]) / L];
      if (n[0] * ((a[0] + q[0]) / 2 - rc[0]) + n[1] * ((a[1] + q[1]) / 2 - rc[1]) < 0) n = [-n[0], -n[1]];
      return n;
    };
    // along each side, centred cells between the corner clear zones: a firing gap fits behind every candidate (a side too short for one has none)
    const cand: { p: Vec2; normal: Vec2 }[] = [];
    const clear = Math.max(0, c.threats.overwatch.rim_corner_clear_m * 100 - ins); // measured from the roof corner; the ring lies `ins` inside it
    ring.forEach((a, i) => {
      const q = ring[(i + 1) % ring.length], L = dist(a, q), usable = L - 2 * clear;
      if (usable < 0) return;
      const n = Math.max(1, Math.round(usable / (c.threats.overwatch.spot_step_m * 100)));
      for (let k = 0; k < n; k++) {
        const t = (clear + (usable * (k + 0.5)) / n) / L;
        cand.push({ p: [a[0] + (q[0] - a[0]) * t, a[1] + (q[1] - a[1]) * t], normal: outN(i) });
      }
    });
    const outwardOnly = c.threats.overwatch.outward_only;
    const spots = cand.map(({ p: sp, normal: n }) => {
      const seen: number[] = [];
      let longest = 0, sx = 0, sy = 0;
      samplesNear(sp, rangeM * 100, (t, ti, dd) => {
        if (dd < T.overwatch.preferred_min_m * 100) return;
        // outward only: a street across the spot's own slab is not covered from this rim (the sight line below ignores the own building)
        if (outwardOnly && (t.p[0] - sp[0]) * n[0] + (t.p[1] - sp[1]) * n[1] <= 0) return;
        if (!sight.los(sp, eyeZ, t.p, T.window.target_height_cm, b.id)) return;
        seen.push(ti); if (dd > longest) longest = dd; sx += t.p[0] - sp[0]; sy += t.p[1] - sp[1];
      });
      seen.sort((x, y) => x - y);
      return { pos: [r0(sp[0]), r0(sp[1])] as Vec2, facing_yaw: rN(yawOf([sx, sy]), 1), covered_m: rN(seen.length * tStepM, 1), longest_los_m: rN(longest / 100, 1), seen };
    }).filter((s) => s.covered_m >= c.threats.overwatch.min_covered_m);
    spots.sort((x, y) => y.covered_m - x.covered_m || x.pos[0] - y.pos[0] || x.pos[1] - y.pos[1]);
    const kept = spots.slice(0, c.threats.overwatch.spots_per_roof);
    if (!kept.length) continue;
    const ri = roofs.length;
    roofs.push({ b, level: b.floors, range: rangeM, access, spots: kept });
    for (const sp of kept) for (const ti of sp.seen) {
      const t = tsamples[ti], v = rangeFalloff(dist(t.p, sp.pos) / 100, T.overwatch.effective_m, T.overwatch.attack_m, T.falloff);
      spread(vSnip, t.line, t.idx, v);
      let list = snipAt.get(ti);
      if (!list) { list = []; snipAt.set(ti, list); }
      if (!list.includes(ri)) list.push(ri);
    }
  }
  const roofSorted = roofs.map((_, i) => i).sort((x, y) => roofs[y].spots[0].covered_m - roofs[x].spots[0].covered_m || (roofs[x].b.id < roofs[y].b.id ? -1 : 1));
  const roofRank = new Map(roofSorted.map((ri, rank) => [ri, rank]));

  // -- threat 4: hiding spots
  const H = c.threats.hiding;
  interface RawHide { pos: Vec2; ts: number; rush: number; g: number; blind: boolean; cover: Obstacle; q: number; stealthOnly: boolean; rn: number; rs: number }
  const rawHides: RawHide[] = [];
  {
    const flood = grid.flood(tsamples.map((t) => t.p), H.rush_max_m * 100);
    const kc = Math.max(1, Math.round((H.candidate_step_m * 100) / grid.cell)), winSamples = Math.ceil(H.approach_window_m / tStepM);
    const sightCm = T.hiding.squad_sight_m * 100, stealthCm = T.hiding.stealth_sight_m * 100;
    for (let j = 0; j < grid.h; j += kc) for (let i = 0; i < grid.w; i += kc) {
      const k = j * grid.w + i, dRush = flood.dist[k];
      if (dRush === Infinity || dRush < H.min_offset_m * 100) continue;
      const p = grid.center(i, j);
      if (inBuilding(p, 0)) continue;
      const cv = conceal.nearest(p, H.adjacent_m * 100);
      if (!cv) continue;
      const ts = tsamples[flood.src[k]], L = lines[ts.line];
      // the farthest point of the line in the approach window that sees a crouched enemy here (a route is walked from its start)
      let g = -1;
      for (const dir of L.cls === 'mandatory' || L.cls === 'variant' ? [-1] : [-1, 1]) {
        for (let n = winSamples; n >= 0; n--) {
          const idx = ts.idx + dir * n * kT;
          if (idx < 0 || idx >= L.pts.length) continue;
          const sp = L.pts[idx], dd = dist(sp, p);
          if (dd <= g || dd > sightCm) continue;
          if (sight.los(sp, T.hiding.eye_cm, p, T.hiding.profile_cm)) g = dd;
        }
      }
      const blind = g < 0;
      if (blind) g = dist(ts.p, p);
      const rn = Math.min(g, sightCm), rs = Math.min(g, stealthCm);
      const close = rn <= H.reveal_close_m * 100, stealthOnly = !close && rs < rn && rs <= H.stealth_reveal_threshold_m * 100;
      if (!close && !stealthOnly) continue;
      const q = close ? Math.max(0.1, 1 - rn / (H.reveal_close_m * 100)) : H.stealth_score * Math.max(0.1, 1 - rs / (H.stealth_reveal_threshold_m * 100));
      rawHides.push({ pos: p, ts: flood.src[k], rush: dRush, g, blind, cover: conceal.obstacles[cv.obs], q, stealthOnly, rn, rs });
    }
  }
  rawHides.sort((a, b) => b.q - a.q || a.rush - b.rush || a.pos[0] - b.pos[0] || a.pos[1] - b.pos[1]);
  const hides: RawHide[] = [];
  for (const h of rawHides) {
    if (hides.length >= H.max_spots) break;
    if (hides.some((x) => dist(x.pos, h.pos) < H.spot_spacing_m * 100)) continue;
    hides.push(h);
  }
  const hideSpan = Math.ceil((H.threat_span_m * 100) / step);
  hides.forEach((h) => { const t = tsamples[h.ts]; spread(vHide, t.line, t.idx, h.q, hideSpan); });

  // -- places: runs of flagged samples
  const places: Place[] = [];
  const narrowCm = c.narrow.width_m * 100, laneCm = c.clutter.lane_width_m * 100;
  const gapN = Math.round((c.places.merge_gap_m * 100) / step), padN = Math.round((c.places.pad_m * 100) / step), maxN = Math.max(2, Math.round((c.places.max_length_m * 100) / step));
  const laneRun = lines.map((L, li) => {
    const out = new Uint8Array(N[li]);
    let s0 = -1;
    for (let i = 0; i <= N[li]; i++) {
      const on = i < N[li] && wAll[li][i] < laneCm;
      if (on && s0 < 0) s0 = i;
      if (!on && s0 >= 0) { if (L.s[i - 1] - L.s[s0] >= c.clutter.lane_min_length_m * 100) out.fill(1, s0, i); s0 = -1; }
    }
    return out;
  });
  const cluttered = (li: number, i: number): boolean => cCount[li][i] >= c.clutter.min_obstacles && cFrac[li][i] >= c.clutter.blocked_fraction;
  const taken = lines.map((_, li) => new Uint8Array(N[li]));
  const runsOf = (li: number, flag: (i: number) => boolean, tier: 1 | 2) => {
    const n = N[li];
    let i = 0;
    while (i < n) {
      if (!flag(i)) { i++; continue; }
      let j = i, last = i;
      while (j + 1 < n && j + 1 - last <= gapN + 1) { j++; if (flag(j)) last = j; }
      let a = Math.max(0, i - padN), b = Math.min(n - 1, last + padN);
      if (tier === 2) { while (a <= b && taken[li][a]) a++; while (b >= a && taken[li][b]) b--; }
      if (b > a) {
        // a long run is cut into equal pieces (no stub at its end)
        const pieces = Math.ceil((b - a + 1) / maxN), size = Math.ceil((b - a + 1) / pieces);
        for (let s = a; s <= b; s += size) places.push({ line: li, i0: s, i1: Math.min(b, s + size - 1), tier, r0: a, r1: b });
        if (tier === 1) taken[li].fill(1, a, b + 1);
      }
      i = last + 1;
    }
  };
  lines.forEach((_, li) => runsOf(li, (i) => wHard[li][i] < narrowCm || !!laneRun[li][i] || cluttered(li, i) || vDrop[li][i] > 0 || vHide[li][i] > 0, 1));
  lines.forEach((_, li) => runsOf(li, (i) => !taken[li][i] && Math.max(vWin[li][i], vSnip[li][i]) >= c.places.exposure_min, 2));

  // -- aggregate every place
  const lineName = (li: number): string => lines[li].id.replace(/[@#]\d+/g, '');
  interface Agg { p: Place; pos: Vec2; iMin: number; wH: number; wA: number; len: number; values: Record<PlaceTag | 'on_route', number>; cls: RouteClass; sources: string[]; canyon: number; count: number; frac: number;
    a: SideInfo | null; b: SideInfo | null; alt: 'none' | 'detour' | 'n/a'; detour: number | null; drops: number[]; winIds: Map<number, number>; roofIds: Set<number>; hideIds: number[]; pre: number }
  const routeVal: Record<RouteClass, number> = { mandatory: 1, variant: 0.7, objectives: 0.7, street: 0 };
  const side = (h: Hit | null): SideInfo | null => (h ? { src: index.obstacles[h.obs].src, what: describe(index.obstacles[h.obs]), height_cm: index.obstacles[h.obs].height, dist_m: rN(h.dist / 100, 2) } : null);
  const aggs: Agg[] = places.map((p) => {
    const L = lines[p.line], li = p.line;
    let iMin = p.i0, wH = Infinity, wA = Infinity, tl = 0, cnt = 0, fr = 0, lane = Infinity, dropV = 0, snipV = 0, isCl = false;
    for (let i = p.i0; i <= p.i1; i++) {
      if (wHard[li][i] < wH) { wH = wHard[li][i]; iMin = i; }
      if (wAll[li][i] < wA) wA = wAll[li][i];
      tl += tall[li][i];
      if (cluttered(li, i)) { isCl = true; if (cCount[li][i] > cnt) cnt = cCount[li][i]; if (cFrac[li][i] > fr) fr = cFrac[li][i]; }
      if (laneRun[li][i] && wAll[li][i] < lane) lane = wAll[li][i];
      if (vDrop[li][i] > dropV) dropV = vDrop[li][i];
      if (vSnip[li][i] > snipV) snipV = vSnip[li][i];
    }
    if (wH === Infinity) { let best = Infinity; for (let i = p.i0; i <= p.i1; i++) if (wAll[li][i] < best) { best = wAll[li][i]; iMin = i; } if (best === Infinity) iMin = (p.i0 + p.i1) >> 1; }
    const n = p.i1 - p.i0 + 1, share = tl / n;
    const winIds = new Map<number, number>(), roofIds = new Set<number>();
    tsamples.forEach((t, ti) => {
      if (t.line !== li || t.idx < p.i0 || t.idx > p.i1) return;
      for (const x of winAt.get(ti) ?? []) winIds.set(x.w, Math.max(winIds.get(x.w) ?? 0, x.v));
      for (const ri of snipAt.get(ti) ?? []) roofIds.add(ri);
    });
    const drops = rawDrops.map((d, i) => (d.line === li && d.idx >= p.i0 - kT && d.idx <= p.i1 + kT ? i : -1)).filter((i) => i >= 0);
    const hideIds = hides.map((h, i) => { const t = tsamples[h.ts]; return t.line === li && t.idx >= p.i0 - hideSpan && t.idx <= p.i1 + hideSpan ? i : -1; }).filter((i) => i >= 0);
    const winV = clamp01([...winIds.values()].sort((x, y) => y - x).slice(0, 8).reduce((s, v) => s + v, 0) / c.threats.window_fire.full_windows);
    const narrowV = wH < narrowCm ? clamp01((narrowCm - wH) / (narrowCm - c.narrow.full_width_m * 100)) : 0;
    const clutterV = Math.max(isCl ? Math.max(clamp01(cnt / c.clutter.full_obstacles), clamp01(fr / c.clutter.full_blocked_fraction)) : 0,
      lane < Infinity ? Math.max(0.3, clamp01((laneCm - lane) / (laneCm - c.clutter.lane_full_width_m * 100))) : 0);
    const values: Agg['values'] = { narrow: narrowV, clutter: clutterV, canyon: share >= c.canyon.min_share ? share : 0, only_way: 0, on_route: routeVal[L.cls], drop_ambush: drops.length ? dropV : 0, window_fire: winV, sniper: snipV,
      hiding: clamp01(hideIds.reduce((s, i) => s + hides[i].q, 0) / H.full_spots) };
    return { p, pos: L.pts[iMin], iMin, wH, wA, len: L.s[p.i1] - L.s[p.i0], values, cls: L.cls, sources: [lineName(li)], canyon: share, count: isCl ? cnt : cCount[li][iMin], frac: isCl ? fr : cFrac[li][iMin],
      a: side(sideA[li][iMin]), b: side(sideB[li][iMin]), alt: 'n/a' as const, detour: null, drops, winIds, roofIds, hideIds, pre: 0 };
  });

  // -- no alternative route (tier 1): block the stretch, search the way round again
  const R = c.redundancy;
  const runAlt = new Map<string, { alt: 'none' | 'detour' | 'n/a'; detour: number | null }>();
  for (const a of aggs) {
    if (a.p.tier !== 1 || !(a.values.narrow > 0 || a.values.clutter > 0)) continue;
    const L = lines[a.p.line], key = `${a.p.line}:${a.p.r0}`;
    let res = runAlt.get(key);
    if (!res) {
      res = { alt: 'n/a', detour: null };
      runAlt.set(key, res);
      // only the narrow / cluttered part of the run is blocked (a run may also hold samples flagged by a threat alone)
      let b0 = -1, b1 = -1;
      for (let i = a.p.r0; i <= a.p.r1; i++) if (wHard[a.p.line][i] < narrowCm || laneRun[a.p.line][i] || cluttered(a.p.line, i)) { if (b0 < 0) b0 = i; b1 = i; }
      if (b0 < 0) continue;
      let iA = b0, iB = b1;
      while (iA > 0 && L.s[b0] - L.s[iA] < R.probe_m * 100) iA--;
      while (iB < L.pts.length - 1 && L.s[iB] - L.s[b1] < R.probe_m * 100) iB++;
      const rad = R.block_radius_m * 100;
      if (L.s[b0] - L.s[iA] < rad + 100 || L.s[iB] - L.s[b1] < rad + 100) continue; // the stretch ends its line: nothing to go round to
      const A = L.pts[iA], B = L.pts[iB], block = L.pts.slice(b0, b1 + 1);
      if (block.some((q) => dist(q, A) <= rad + grid.cell || dist(q, B) <= rad + grid.cell)) continue;
      const base = grid.pathLength(A, B, R.search_margin_m * 100, false);
      if (base === null) continue;
      grid.setPatch(block, rad);
      const round = grid.pathLength(A, B, R.search_margin_m * 100, true);
      grid.clearPatch();
      if (round === null) res.alt = 'none';
      else { res.alt = 'detour'; res.detour = Math.max(0, round - base); }
    }
    a.alt = res.alt; a.detour = res.detour;
    a.values.only_way = res.alt === 'none' ? 1 : res.alt === 'detour' ? clamp01((res.detour ?? 0) / (R.detour_critical_m * 100)) : 0;
  }

  // -- one place per spot: a route and the street under it are the same stretch
  const W = c.weights;
  const scoreOf = (v: Agg['values']): number => Math.min(1, ([...PLACE_TAGS, 'on_route'] as const).reduce((s, k) => s + W[k] * v[k], 0));
  for (const a of aggs) a.pre = scoreOf(a.values);
  const order = aggs.map((_, i) => i).sort((x, y) => aggs[y].pre - aggs[x].pre || routeVal[aggs[y].cls] - routeVal[aggs[x].cls] || aggs[x].p.line - aggs[y].p.line || aggs[x].p.i0 - aggs[y].p.i0);
  const kept: Agg[] = [];
  const nearLine = (p: Vec2, a: Agg, lim: number): boolean => { const L = lines[a.p.line]; for (let i = a.p.i0; i <= a.p.i1; i += 2) if (dist(L.pts[i], p) <= lim) return true; return dist(L.pts[a.p.i1], p) <= lim; };
  for (const oi of order) {
    const a = aggs[oi];
    const twin = kept.find((k) => nearLine(a.pos, k, c.places.dedupe_m * 100) || nearLine(k.pos, a, c.places.dedupe_m * 100));
    if (!twin) { kept.push(a); continue; }
    for (const s of a.sources) if (!twin.sources.includes(s)) twin.sources.push(s);
    if (routeVal[a.cls] > routeVal[twin.cls]) { twin.cls = a.cls; twin.values.on_route = routeVal[a.cls]; }
    for (const k of ['only_way', 'drop_ambush', 'window_fire', 'sniper', 'hiding'] as const) if (a.values[k] > twin.values[k]) twin.values[k] = a.values[k];
    if (a.alt === 'none' || (a.alt === 'detour' && twin.alt === 'n/a')) { twin.alt = a.alt; twin.detour = a.detour; }
    for (const d of a.drops) if (!twin.drops.includes(d)) twin.drops.push(d);
    for (const [w, v] of a.winIds) twin.winIds.set(w, Math.max(twin.winIds.get(w) ?? 0, v));
    for (const r of a.roofIds) twin.roofIds.add(r);
    for (const h of a.hideIds) if (!twin.hideIds.includes(h)) twin.hideIds.push(h);
  }
  const final = kept.map((a) => ({ a, score: rN(scoreOf(a.values), 3) })).filter((x) => x.score >= c.places.min_score)
    .sort((x, y) => y.score - x.score || x.a.pos[0] - y.a.pos[0] || x.a.pos[1] - y.a.pos[1]).slice(0, c.places.max_items);

  // -- output
  const jumperNames = T.drop.jumpers.map((j) => c.threats.drop_ambush.jumper_names?.[j] ?? j).join(', ');
  const dropIdOf = new Map<number, string>(), hideIdOf = new Map<number, string>();
  rawDrops.slice(0, c.threats.drop_ambush.max_spots).forEach((_, i) => dropIdOf.set(i, `DA_${String(i + 1).padStart(2, '0')}`));
  hides.forEach((_, i) => hideIdOf.set(i, `HS_${String(i + 1).padStart(2, '0')}`));
  const dropPlaces = new Map<number, string[]>(), winPlaces = new Map<number, string[]>(), roofPlaces = new Map<number, string[]>(), hidePlace = new Map<number, string>();
  const push = (m: Map<number, string[]>, k: number, id: string) => { const x = m.get(k); if (x) x.push(id); else m.set(k, [id]); };
  const chokepoints: Chokepoint[] = final.map(({ a, score }, n) => {
    const id = `CP_${String(n + 1).padStart(2, '0')}`, L = lines[a.p.line], v = a.values;
    const tags = PLACE_TAGS.filter((k) => (k === 'only_way' ? v[k] >= R.tag_min_value : v[k] > 0));
    const kind = c.kind_priority.find((k) => tags.includes(k)) ?? 'narrow';
    const pts: Vec2[] = [];
    const stride = Math.max(1, Math.round(200 / step));
    for (let i = a.p.i0; i <= a.p.i1; i += stride) pts.push(L.pts[i]);
    if (dist(pts[pts.length - 1], L.pts[a.p.i1]) > 1) pts.push(L.pts[a.p.i1]);
    const half = Math.max(100, Math.min(400, (a.wH < Infinity ? a.wH : a.wA < Infinity ? a.wA : 400) / 2));
    const left: Vec2[] = [], right: Vec2[] = [];
    pts.forEach((p, i) => {
      const q0 = pts[Math.max(0, i - 1)], q1 = pts[Math.min(pts.length - 1, i + 1)], dl = dist(q0, q1) || 1, nx = -(q1[1] - q0[1]) / dl, ny = (q1[0] - q0[0]) / dl;
      left.push([r0(p[0] + nx * half), r0(p[1] + ny * half)]); right.push([r0(p[0] - nx * half), r0(p[1] - ny * half)]);
    });
    const drops = a.drops.filter((d) => dropIdOf.has(d)).sort((x, y) => x - y), winList = [...a.winIds.entries()].sort((x, y) => y[1] - x[1] || x[0] - y[0]);
    const roofsOf = [...a.roofIds].sort((x, y) => (roofRank.get(x) ?? 0) - (roofRank.get(y) ?? 0)), hid = [...a.hideIds].sort((x, y) => x - y);
    drops.forEach((d) => push(dropPlaces, d, id)); winList.forEach(([w]) => push(winPlaces, w, id)); roofsOf.forEach((r) => push(roofPlaces, r, id)); hid.forEach((h) => { if (!hidePlace.has(h)) hidePlace.set(h, id); });
    const reasons: string[] = [];
    if (v.narrow > 0) reasons.push(`узко: ${rN(a.wH / 100, 1)} м${a.a && a.b ? ` между «${a.a.what}» и «${a.b.what}»` : ''} (порог ${c.narrow.width_m} м)`);
    if (v.clutter > 0) reasons.push(a.count >= c.clutter.min_obstacles && a.frac >= c.clutter.blocked_fraction ? `завал: ${a.count} ${plural(a.count, 'преграда', 'преграды', 'преград')} на ${c.clutter.window_m} м улицы, под ними ${Math.round(a.frac * 100)} % полосы${a.wA < laneCm ? `; свободный проход ${rN(a.wA / 100, 1)} м` : ''}`
      : `затор: свободный проход между преградами ${rN(a.wA / 100, 1)} м (порог ${c.clutter.lane_width_m} м)`);
    if (v.canyon > 0 && a.a && a.b) reasons.push(`каньон: высокие стены с обеих сторон (${rN(a.a.height_cm / 100, 1)} м и ${rN(a.b.height_cm / 100, 1)} м) на ${Math.round(a.canyon * 100)} % длины`);
    if (a.alt === 'none') reasons.push(`единственный путь: обхода нет в пределах ${R.search_margin_m} м`);
    else if (a.alt === 'detour' && (a.detour ?? 0) >= 500) reasons.push(`обход есть, длиннее на ${rN((a.detour ?? 0) / 100, 0)} м (критично от ${R.detour_critical_m} м)`);
    if (v.drop_ambush > 0 && drops.length) { const d = rawDrops[drops[0]]; reasons.push(`засада сверху: ${jumperNames} спрыгивают с ${d.surface.kind === 'roof' ? 'крыши' : 'верха стены'} ${d.surface.id.split(':')[1]} (${rN(d.surface.height / 100, 1)} м), до края ${rN(d.gap / 100, 1)} м при досягаемости ${rN(T.drop.reach_cm / 100, 1)} м${d.surface.access === 'ladder' ? '; на крышу только по лестнице' : ''}`); }
    if (v.window_fire > 0) reasons.push(`огонь из окон: ${winList.length} ${plural(winList.length, 'окно', 'окна', 'окон')} (${winList.slice(0, 3).map(([w]) => wins[w].id).join(', ')}${winList.length > 3 ? ', …' : ''}), ${T.window.weapon} до ${T.window.effective_m} / ${T.window.attack_m} м`);
    if (v.sniper > 0 && roofsOf.length) reasons.push(`снайпер на крыше: ${roofsOf.slice(0, 3).map((r) => `${roofs[r].b.id} (+${roofs[r].level})`).join(', ')}, дальность ${rN(roofs[roofsOf[0]].range, 0)} м`);
    const hidN = hid.filter((h) => !hides[h].stealthOnly), hidS = hid.filter((h) => hides[h].stealthOnly);
    const revN = hidN.length ? Math.min(...hidN.map((h) => hides[h].rn)) : null, revS = hid.length ? Math.min(...hid.map((h) => hides[h].rs)) : null;
    if (v.hiding > 0 && hid.length) {
      const parts: string[] = [];
      if (hidN.length) parts.push(`${hidN.length} ${plural(hidN.length, 'укрытие', 'укрытия', 'укрытий')} рядом, виден только с ${rN((revN ?? 0) / 100, 1)} м`);
      if (hidS.length) parts.push(`${hidS.length} ${plural(hidS.length, 'место', 'места', 'мест')} только для бойца в режиме скрытности — виден с ${rN(Math.min(...hidS.map((h) => hides[h].rs)) / 100, 1)} м вместо ${rN(Math.min(...hidS.map((h) => hides[h].rn)) / 100, 1)} м`);
      reasons.push(`скрытый противник: ${parts.join('; ')}`);
    }
    reasons.push(a.cls === 'mandatory' ? 'на основном маршруте' : a.cls === 'variant' ? 'на варианте маршрута' : a.cls === 'objectives' ? 'на пути к целям' : 'улица вне маршрутов');
    return { id, kind, kind_name: c.kind_names[kind], tags, score, pos: [r0(a.pos[0]), r0(a.pos[1])] as Vec2, line: pts.map((p) => [r0(p[0]), r0(p[1])] as Vec2), poly: [...left, ...right.reverse()], length_m: rN(a.len / 100, 1),
      width_m: a.wH < Infinity ? rN(a.wH / 100, 2) : null, lane_m: a.wA < Infinity ? rN(a.wA / 100, 2) : null, route: a.cls, sources: [...a.sources].sort(),
      values: Object.fromEntries(([...PLACE_TAGS, 'on_route'] as const).map((k) => [k, rN(v[k], 3)])) as Chokepoint['values'],
      numbers: { obstacles_per_window: a.count, blocked_fraction: rN(a.frac, 3), canyon_share: rN(a.canyon, 2), side_a: a.a, side_b: a.b, alternative: a.alt, detour_m: a.detour === null ? null : rN(a.detour / 100, 1),
        drop_spots: drops.map((d) => dropIdOf.get(d)!), windows: winList.map(([w]) => wins[w].id), overwatch: roofsOf.map((r) => roofs[r].b.id), hiding_spots: hid.map((h) => hideIdOf.get(h)!),
        reveal_m: revN === null ? null : rN(revN / 100, 1), reveal_stealth_m: revS === null ? null : rN(revS / 100, 1) },
      reasons };
  });

  return {
    version: ANALYSIS_VERSION, geometry_hash: geometryHash(l), conditions: { weather: cond.weather, time_of_day: cond.time_of_day },
    thresholds: { narrow_width_m: c.narrow.width_m, clutter_min_obstacles: c.clutter.min_obstacles, clutter_blocked_fraction: c.clutter.blocked_fraction, lane_width_m: c.clutter.lane_width_m, canyon_tall_cm: tallMin, canyon_side_max_m: c.canyon.side_max_m,
      detour_critical_m: R.detour_critical_m, drop_reach_cm: T.drop.reach_cm, drop_min_cm: T.drop.min_drop_cm, drop_max_cm: T.drop.max_drop_cm, window_range_m: T.window.attack_m, sniper_range_m: T.overwatch.attack_m,
      reveal_close_m: H.reveal_close_m, stealth_sight_m: rN(T.hiding.stealth_sight_m, 1), min_score: c.places.min_score },
    chokepoints,
    dropAmbushSpots: rawDrops.slice(0, c.threats.drop_ambush.max_spots).map((d, i) => ({ id: dropIdOf.get(i)!, surface: d.surface.id, kind: d.surface.kind, edge: [r0(d.edge[0]), r0(d.edge[1])] as Vec2, land: [r0(d.land[0]), r0(d.land[1])] as Vec2,
      drop_cm: d.surface.height, gap_cm: r0(d.gap), access: d.surface.access, jumpers: T.drop.jumpers, line: lineName(d.line), places: dropPlaces.get(i) ?? [] })),
    windowsCovering: wins.map((w, i) => ({ w, i })).filter(({ i }) => winPlaces.has(i)).map(({ w, i }) => ({ id: w.id, building: w.building, floor: w.floor, kind: w.kind, pos: w.pos, yaw: w.yaw, covered_m: rN(w.count * tStepM, 1), min_dist_m: rN(w.minD / 100, 1), places: winPlaces.get(i)! })),
    overwatchRoofs: roofSorted.slice(0, c.threats.overwatch.max_roofs).map((ri) => { const r = roofs[ri]; return { building: r.b.id, level: r.level, height_cm: r.b.height, range_m: rN(r.range, 1), access: r.access,
      spots: r.spots.map((s) => ({ pos: s.pos, facing_yaw: s.facing_yaw, covered_m: s.covered_m, longest_los_m: s.longest_los_m })), places: roofPlaces.get(ri) ?? [] }; }),
    hidingSpots: hides.map((h, i) => { const t = tsamples[h.ts], L = lines[t.line]; return { id: hideIdOf.get(i)!, pos: [r0(h.pos[0]), r0(h.pos[1])] as Vec2, facing_yaw: rN(yawOf([t.p[0] - h.pos[0], t.p[1] - h.pos[1]]), 1),
      reveal_normal_m: rN(h.rn / 100, 1), reveal_stealth_m: rN(h.rs / 100, 1), stealth_only: h.stealthOnly, blind: h.blind, rush_m: rN(h.rush / 100, 1), cover: h.cover.src, cover_height_cm: h.cover.height,
      threatens: { line: lineName(t.line), ...(L.route ? { route: L.route } : {}), pos: [r0(t.p[0]), r0(t.p[1])] as Vec2 }, ...(hidePlace.has(i) ? { place: hidePlace.get(i)! } : {}) }; }),
    stats: { lines: lines.length, length_m: rN(lines.reduce((s, L) => s + L.s[L.s.length - 1], 0) / 100, 0), samples: lines.reduce((s, L) => s + L.pts.length, 0), places_found: kept.length },
  };
}
