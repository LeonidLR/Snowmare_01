// Training Ground / level generator - the LAYER system (designer spec 2026-11-16, docs/port/LEVELGEN_LAYERS_REPORT.md).
// The map is split into layers (roads, buildings, bridges, the obstacle SIZE CLASSES large / medium / small, mines, loot, special
// items, goals, the fire lines, the chokepoint analysis and its threat sub-layers, plus the service layers the plan draws: the canal,
// the block zones, the routes, the mantle edges). The assignment is DATA: Content/Data/LevelGen/layers.json (id, name, colour, the
// match rules). Every entity of a layout (schema.ts) and every object of the map's props file (props/schema.ts, hand overrides
// applied) belongs to EXACTLY ONE layer - assignLayers reports any entity that matches two layers or none.
// The page shows / hides / solos layers (hiddenKeys -> LayoutView), counts them, and EDITS one active layer at a time: editLayout /
// editProps refuse every entity that is not on the active layer. Pure TS (browser + node), deterministic, nothing written.
import layersCfg from '../../../../Content/Data/LevelGen/layers.json' with { type: 'json' };
import balanceCfg from '../../../../Content/Data/Balance/props_balance.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { dirOf, distToWall, pointInPoly } from '../movement/geometry.ts';
import { footprintPoly } from './rotation.ts';
import type { CoverKind, GenCover, GenLayout, GenWall, WallKind } from './schema.ts';
import type { LevelPropsFile, PropLayer, PropLayerKind, PropObject, PropOverride } from './props/schema.ts';

// ---- the config ---------------------------------------------------------------------------------------------------------------

export const LAYERS_SCHEMA = 'codex.levelgen_layers/1';
export type EntityType = 'zone' | 'building' | 'wall' | 'cover' | 'crossing' | 'cut' | 'ladder' | 'route' | 'point' | 'mantle_edge' | 'objective' | 'prop' | 'analysis';
export const ENTITY_TYPES: readonly EntityType[] = ['zone', 'building', 'wall', 'cover', 'crossing', 'cut', 'ladder', 'route', 'point', 'mantle_edge', 'objective', 'prop', 'analysis'];
export type SizeClass = 'small' | 'medium' | 'large';
export const SIZE_CLASSES: readonly SizeClass[] = ['large', 'medium', 'small'];
export type LayerTool = 'select' | 'move' | 'delete' | 'add' | 'reroll';
export type LayerGroup = 'main' | 'analysis' | 'threats' | 'service';

export interface LayerRule { type: EntityType; kinds?: string[]; set_pieces?: string[]; id_regex?: string; variants?: string[]; size_class?: SizeClass; fallback?: boolean }
export interface AddTemplate {
  label: string;
  cover?: { kind: CoverKind; size: [number, number, number] };
  wall?: { kind: WallKind; length: number; height: number; thickness: number };
  prop?: { kind: PropLayerKind; variant: string; fields?: Record<string, unknown> };
}
export interface LayerDef {
  id: string; name: string; group: LayerGroup; color: string; edit: LayerTool[]; hint?: string;
  /** An obstacle size-class layer (the panel prints the rule next to it). */
  size_class?: SizeClass;
  /** A layer fed by the map's props file. */
  props?: boolean;
  match: LayerRule[];
  add?: AddTemplate[];
}
export interface SizeClassRule { small_max_height_cm: number; large_min_height_cm: number; large_min_footprint_m2: number; kind_class: Record<string, SizeClass> }
export interface LayersConfig { schema: string; size_classes: SizeClassRule; layers: LayerDef[] }

export const LAYERS = layersCfg as unknown as LayersConfig;
/**
 * Designer 2026-11-19: not listed in the «Управление картой» layers panel. Mines / loot / special items live in «Пропсы уровней»
 * (and are not drawn on this plan); the objectives row is gone too, but start + goals stay drawn and edited through
 * «Расставить старт и цели» / «+ цель».
 */
export const MAP_PANEL_EXCLUDED: readonly string[] = ['mines', 'loot', 'special', 'objectives'];
/** Of those, the layers that are not drawn on the plan either. */
export const MAP_PLAN_HIDDEN: readonly string[] = ['mines', 'loot', 'special'];

/** The config's own checks (empty = valid): unique ids, known types / tools, one fallback per type at most, compilable regexes. */
export function validateLayersConfig(c: LayersConfig): string[] {
  const e: string[] = [];
  if (c.schema !== LAYERS_SCHEMA) e.push(`schema must be "${LAYERS_SCHEMA}"`);
  const s = c.size_classes;
  if (!s || !(s.small_max_height_cm > 0) || !(s.large_min_height_cm > s.small_max_height_cm) || !(s.large_min_footprint_m2 > 0)) e.push('size_classes: 0 < small_max_height_cm < large_min_height_cm, large_min_footprint_m2 > 0');
  const ids = new Set<string>(), fallbacks = new Map<string, string>();
  for (const l of c.layers ?? []) {
    if (!l.id || ids.has(l.id)) e.push(`layers: duplicate or empty id "${l.id}"`);
    ids.add(l.id);
    if (!/^#[0-9a-fA-F]{6}$/.test(l.color ?? '')) e.push(`layer ${l.id}: color must be #rrggbb`);
    if (!['main', 'analysis', 'threats', 'service'].includes(l.group)) e.push(`layer ${l.id}: unknown group "${l.group}"`);
    for (const t of l.edit ?? []) if (!['select', 'move', 'delete', 'add', 'reroll'].includes(t)) e.push(`layer ${l.id}: unknown tool "${t}"`);
    if (!Array.isArray(l.match) || !l.match.length) e.push(`layer ${l.id}: needs at least one match rule`);
    for (const r of l.match ?? []) {
      if (!ENTITY_TYPES.includes(r.type)) e.push(`layer ${l.id}: unknown entity type "${r.type}"`);
      if (r.id_regex !== undefined) { try { new RegExp(r.id_regex); } catch { e.push(`layer ${l.id}: bad id_regex "${r.id_regex}"`); } }
      if (r.fallback) {
        if (fallbacks.has(r.type)) e.push(`two fallback rules for type "${r.type}": ${fallbacks.get(r.type)} and ${l.id}`);
        fallbacks.set(r.type, l.id);
      }
    }
  }
  return e;
}

// ---- size classes -------------------------------------------------------------------------------------------------------------

/**
 * The obstacle size class by height and footprint (layers.json size_classes): small = up to small_max_height_cm (vaulted on the
 * move); large = from large_min_height_cm, or a footprint of large_min_footprint_m2 and more when taller than small; medium = the
 * rest. `kind` (a cover kind / prop variant) may be overridden in kind_class.
 */
export function sizeClassOf(heightCm: number, footprintM2: number, kind?: string, rule: SizeClassRule = LAYERS.size_classes): SizeClass {
  const forced = kind ? rule.kind_class?.[kind] : undefined;
  if (forced) return forced;
  if (heightCm <= rule.small_max_height_cm) return 'small';
  if (heightCm >= rule.large_min_height_cm || footprintM2 >= rule.large_min_footprint_m2) return 'large';
  return 'medium';
}

/** The rule in the designer's words (the panel prints it under the obstacle layers). */
export function sizeClassRuleText(c: SizeClass, rule: SizeClassRule = LAYERS.size_classes): string {
  if (c === 'small') return `высота ≤ ${rule.small_max_height_cm} см (классы перелаза 85 / 95 см: перепрыгивается с ходу, укрытие пригнувшись)`;
  if (c === 'large') return `высота ≥ ${rule.large_min_height_cm} см (закрывает обзор стоящему; 230 / 310 / 330 см, силосы) или площадь ≥ ${rule.large_min_footprint_m2} м² при высоте > ${rule.small_max_height_cm} см`;
  return `высота ${rule.small_max_height_cm + 1}–${rule.large_min_height_cm - 1} см и площадь < ${rule.large_min_footprint_m2} м² (135 / 140 см: укрытие в рост, высокий перелаз)`;
}

export const wallFootprintM2 = (w: Pick<GenWall, 'a' | 'b' | 'thickness'>): number => (Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]) * Math.max(1, w.thickness)) / 1e4;
export const coverFootprintM2 = (c: Pick<GenCover, 'size'>): number => (c.size[0] * c.size[1]) / 1e4;

type BalanceKind = { size_cm?: number[]; radius_cm?: number; height_cm?: number };
const BALANCE_KINDS = (balanceCfg as unknown as { kinds: Record<string, BalanceKind> }).kinds;
/** A prop variant's [x, y, z] cm from props_balance.json (null: the balance names no size - mines, loot piles have no obstacle size). */
export function propSizeCm(variant: string): [number, number, number] | null {
  const k = BALANCE_KINDS[variant];
  if (!k) return null;
  if (Array.isArray(k.size_cm) && k.size_cm.length === 3) return [k.size_cm[0], k.size_cm[1], k.size_cm[2]];
  if (typeof k.radius_cm === 'number' && typeof k.height_cm === 'number') return [2 * k.radius_cm, 2 * k.radius_cm, k.height_cm];
  return null;
}

// ---- entities -----------------------------------------------------------------------------------------------------------------

export type EntityShape = { kind: 'poly'; poly: Vec2[] } | { kind: 'seg'; a: Vec2; b: Vec2; thickness: number } | { kind: 'point'; r: number };
export interface Entity {
  /** "<type>:<id>" - unique in a layout + its props. */
  key: string;
  type: EntityType;
  id: string;
  kind?: string;
  set_piece?: string;
  variant?: string;
  size_class?: SizeClass;
  height_cm?: number;
  footprint_m2?: number;
  /** The centre (zone tests, the panel's "zoom to"). */
  pos: Vec2;
  shape: EntityShape;
}

const centroid = (p: readonly Vec2[]): Vec2 => [p.reduce((s, q) => s + q[0], 0) / Math.max(1, p.length), p.reduce((s, q) => s + q[1], 0) / Math.max(1, p.length)];
const mid = (a: Vec2, b: Vec2): Vec2 => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];

/** A cover's footprint corners (its yawed rectangle: size[0] along the yaw, size[1] across). */
export function coverPoly(c: Pick<GenCover, 'pos' | 'yaw' | 'size'>): Vec2[] {
  const d = dirOf(c.yaw), r: Vec2 = [-d[1], d[0]], hx = c.size[0] / 2, hy = c.size[1] / 2;
  const at = (u: number, v: number): Vec2 => [c.pos[0] + d[0] * u + r[0] * v, c.pos[1] + d[1] * u + r[1] * v];
  return [at(hx, hy), at(-hx, hy), at(-hx, -hy), at(hx, -hy)];
}

const ladderKind = (from: string, to: string): string => (from === 'cut' || to === 'cut' ? 'cut' : to.startsWith('roof:') ? 'roof' : to.startsWith('floor:') ? 'floor' : 'ground');

/** Every entity of a layout the layers know (analysis overlays are counted apart: analysisCounts). File order, stable. */
export function layoutEntities(l: GenLayout): Entity[] {
  const out: Entity[] = [];
  for (const z of l.zones) out.push({ key: `zone:${z.id}`, type: 'zone', id: z.id, kind: z.kind, set_piece: z.set_piece, pos: centroid(z.poly), shape: { kind: 'poly', poly: z.poly } });
  for (const b of l.buildings) { const p = footprintPoly(b); out.push({ key: `building:${b.id}`, type: 'building', id: b.id, set_piece: b.set_piece, height_cm: b.height, pos: centroid(p), shape: { kind: 'poly', poly: p } }); }
  for (const w of l.walls) {
    const fp = wallFootprintM2(w);
    out.push({ key: `wall:${w.id}`, type: 'wall', id: w.id, kind: w.kind ?? 'wall', height_cm: w.height, footprint_m2: fp, size_class: sizeClassOf(w.height, fp, w.kind ?? 'wall'), pos: mid(w.a, w.b),
      shape: { kind: 'seg', a: w.a, b: w.b, thickness: w.thickness } });
  }
  for (const c of l.covers) {
    const fp = coverFootprintM2(c);
    out.push({ key: `cover:${c.id}`, type: 'cover', id: c.id, kind: c.kind, height_cm: c.size[2], footprint_m2: fp, size_class: sizeClassOf(c.size[2], fp, c.kind), pos: c.pos, shape: { kind: 'poly', poly: coverPoly(c) } });
  }
  if (l.cut) {
    out.push({ key: 'cut:cut', type: 'cut', id: 'cut', pos: centroid(l.cut.poly), shape: { kind: 'poly', poly: l.cut.poly } });
    for (const x of l.cut.crossings) out.push({ key: `crossing:${x.id}`, type: 'crossing', id: x.id, kind: x.kind, pos: centroid(x.poly), shape: { kind: 'poly', poly: x.poly } });
  }
  for (const k of l.ladders) out.push({ key: `ladder:${k.id}`, type: 'ladder', id: k.id, kind: ladderKind(k.from, k.to), pos: k.pos, shape: { kind: 'point', r: 60 } });
  for (const r of l.routes) out.push({ key: `route:${r.id}`, type: 'route', id: r.id, kind: r.mandatory ? 'mandatory' : 'variant', pos: r.points[0] ?? [0, 0], shape: { kind: 'point', r: 0 } });
  out.push({ key: 'point:spawn', type: 'point', id: 'spawn', kind: 'spawn', pos: l.spawn.pos, shape: { kind: 'point', r: 90 } });
  out.push({ key: 'point:end', type: 'point', id: 'end', kind: 'end', pos: l.end, shape: { kind: 'point', r: 90 } });
  for (const s of l.spawn_points) out.push({ key: `point:${s.id}`, type: 'point', id: s.id, kind: `spawn_${s.kind}`, pos: s.pos, shape: { kind: 'point', r: 60 } });
  for (const a of l.ambush_points) out.push({ key: `point:${a.id}`, type: 'point', id: a.id, kind: 'ambush', pos: a.pos, shape: { kind: 'point', r: 60 } });
  for (const p of l.patrol_routes) out.push({ key: `point:${p.id}`, type: 'point', id: p.id, kind: 'patrol', pos: centroid(p.points), shape: { kind: 'point', r: 0 } });
  for (const m of l.mantle_edges ?? []) out.push({ key: `mantle_edge:${m.id}`, type: 'mantle_edge', id: m.id, kind: m.kind, pos: mid(m.start, m.end), shape: { kind: 'seg', a: m.start, b: m.end, thickness: 0 } });
  for (const b of l.buildings) for (const m of b.mantle_edges ?? []) out.push({ key: `mantle_edge:${m.id}`, type: 'mantle_edge', id: m.id, kind: m.kind, pos: mid(m.start, m.end), shape: { kind: 'seg', a: m.start, b: m.end, thickness: 0 } });
  if (l.objectives) {
    out.push({ key: 'objective:START', type: 'objective', id: 'START', kind: 'start', pos: l.objectives.start.pos, shape: { kind: 'point', r: 110 } });
    for (const g of l.objectives.goals) out.push({ key: `objective:${g.id}`, type: 'objective', id: g.id, kind: 'goal', pos: g.pos, shape: { kind: 'point', r: 100 } });
  }
  // one key per entity: a building's mantle edge may repeat a top-level one (LayoutView keeps one per id as well)
  const seen = new Set<string>();
  return out.filter((x) => (seen.has(x.key) ? false : (seen.add(x.key), true)));
}

// ---- props (the map's props file: codex.levelgen_props/1) ------------------------------------------------------------------------

/** A prop as the game spawns it: the layer objects with the hand overrides applied (PROPS_LAYERS.md 1.4). */
export interface ResolvedProp { object: PropObject; kind: string; layer: string; hand: boolean }

const KIND_BY_VARIANT: [RegExp, PropLayerKind][] = [[/^mine/, 'mines'], [/^loot/, 'loot'], [/^turret/, 'turrets'], [/^barrel/, 'barrels'], [/^crate/, 'crates']];
const handLayerOf = (id: string): string | undefined => /^hand_(.+)_\d+$/.exec(id)?.[1];

/** layers[].objects + overrides (delete / move / set / add) -> the objects on the map, in file order then the hand-added ones. */
export function resolveProps(file: LevelPropsFile | null | undefined): ResolvedProp[] {
  if (!file) return [];
  const out: ResolvedProp[] = [];
  for (const layer of file.layers ?? []) for (const o of layer.objects ?? []) out.push({ object: o, kind: layer.kind, layer: layer.id, hand: o.origin === 'hand' });
  for (const ov of file.overrides ?? []) {
    if (ov.op === 'add') {
      const o = (ov as { object: PropObject }).object;
      const lid = handLayerOf(o.id), layer = (file.layers ?? []).find((x) => x.id === lid);
      const kind = layer?.kind ?? KIND_BY_VARIANT.find(([re]) => re.test(o.variant))?.[1] ?? 'special';
      if (!out.some((x) => x.object.id === o.id)) out.push({ object: o, kind, layer: layer?.id ?? lid ?? kind, hand: true });
      continue;
    }
    const i = out.findIndex((x) => x.object.id === (ov as { id: string }).id);
    if (i < 0) continue;
    if (ov.op === 'delete') out.splice(i, 1);
    else if (ov.op === 'move') out[i] = { ...out[i], object: { ...out[i].object, pos: ov.pos, walk_layer: ov.walk_layer, ...(ov.yaw !== undefined ? { yaw: ov.yaw } : {}) } };
    else if (ov.op === 'set') out[i] = { ...out[i], object: { ...out[i].object, ...(ov as { fields: Record<string, unknown> }).fields } };
  }
  return out;
}

/** The props of a map as layer entities (hand overrides applied). */
export function propEntities(file: LevelPropsFile | null | undefined): Entity[] {
  return resolveProps(file).map(({ object: o, kind }) => {
    const size = propSizeCm(o.variant);
    const fp = size ? (size[0] * size[1]) / 1e4 : undefined;
    return { key: `prop:${o.id}`, type: 'prop' as const, id: o.id, kind, variant: o.variant, height_cm: size?.[2], footprint_m2: fp,
      size_class: size ? sizeClassOf(size[2], fp ?? 0, o.variant) : undefined, pos: [o.pos[0], o.pos[1]] as Vec2, shape: { kind: 'point' as const, r: size ? Math.max(size[0], size[1]) / 2 : 40 } };
  });
}

// ---- assignment ---------------------------------------------------------------------------------------------------------------

const regexCache = new Map<string, RegExp>();
const rx = (s: string): RegExp => { let r = regexCache.get(s); if (!r) { r = new RegExp(s); regexCache.set(s, r); } return r; };

/** A rule's own test (the fallback flag is handled by matchingLayers). */
export function ruleMatches(r: LayerRule, e: Entity): boolean {
  if (r.type !== e.type) return false;
  if (r.kinds && !(e.kind !== undefined && r.kinds.includes(e.kind))) return false;
  if (r.set_pieces && !(e.set_piece !== undefined && r.set_pieces.includes(e.set_piece))) return false;
  if (r.variants && !(e.variant !== undefined && r.variants.includes(e.variant))) return false;
  if (r.size_class && e.size_class !== r.size_class) return false;
  if (r.id_regex && !rx(r.id_regex).test(e.id)) return false;
  return true;
}

/** The layers whose rules take the entity: the explicit matches, else the type's fallback layer. More than one = a config error. */
export function matchingLayers(e: Entity, c: LayersConfig = LAYERS): string[] {
  const explicit = c.layers.filter((l) => l.match.some((r) => !r.fallback && ruleMatches(r, e))).map((l) => l.id);
  if (explicit.length) return explicit;
  return c.layers.filter((l) => l.match.some((r) => r.fallback && r.type === e.type)).map((l) => l.id);
}

export interface LayerAssignment {
  /** Entities per layer id (every layer of the config has a list, possibly empty), in entity order. */
  byLayer: Record<string, Entity[]>;
  /** The layer of every entity key. */
  layerOf: Record<string, string>;
  /** Entities that match two layers or none (a config error; empty on every shipped map - layers.test.ts). */
  problems: string[];
}

/** Every entity into exactly one layer (the first match when the config is ambiguous - reported in `problems`). */
export function assignLayers(entities: readonly Entity[], c: LayersConfig = LAYERS): LayerAssignment {
  const byLayer: Record<string, Entity[]> = Object.fromEntries(c.layers.map((l) => [l.id, [] as Entity[]]));
  const layerOf: Record<string, string> = {};
  const problems: string[] = [];
  for (const e of entities) {
    const m = matchingLayers(e, c);
    if (m.length !== 1) problems.push(`${e.key}: ${m.length ? `matches ${m.join(' + ')}` : 'matches no layer'}`);
    if (!m.length) continue;
    byLayer[m[0]].push(e);
    layerOf[e.key] = m[0];
  }
  return { byLayer, layerOf, problems };
}

/** Layout + props entities assigned in one go. */
export const assignMap = (l: GenLayout, props?: LevelPropsFile | null, c: LayersConfig = LAYERS): LayerAssignment => assignLayers([...layoutEntities(l), ...propEntities(props)], c);

/** The analysis overlays' counts by kind (fire_line, chokepoint, drop_spot, window, overwatch, hiding) -> counts per layer id. */
export function analysisLayerCounts(counts: Record<string, number>, c: LayersConfig = LAYERS): Record<string, number> {
  const out: Record<string, number> = {};
  for (const l of c.layers) for (const r of l.match) if (r.type === 'analysis') for (const k of r.kinds ?? []) out[l.id] = (out[l.id] ?? 0) + (counts[k] ?? 0);
  return out;
}

/** Counts per layer id: the assigned entities + the analysis overlays. */
export function layerCounts(a: LayerAssignment, analysis: Record<string, number> = {}, c: LayersConfig = LAYERS): Record<string, number> {
  const extra = analysisLayerCounts(analysis, c);
  return Object.fromEntries(c.layers.map((l) => [l.id, a.byLayer[l.id].length + (extra[l.id] ?? 0)]));
}

export const layerById = (id: string, c: LayersConfig = LAYERS): LayerDef | undefined => c.layers.find((l) => l.id === id);

// ---- visibility ---------------------------------------------------------------------------------------------------------------

export type LayerVisibility = Record<string, boolean>;
/** Everything shown except the layers listed in `off` (the page hides the heavy threat sub-layers until asked). */
export const defaultVisibility = (c: LayersConfig = LAYERS, off: readonly string[] = []): LayerVisibility => Object.fromEntries(c.layers.map((l) => [l.id, !off.includes(l.id)]));
/** Solo: only this layer (the service layers stay as they were when `keepService`). */
export const soloVisibility = (id: string, c: LayersConfig = LAYERS): LayerVisibility => Object.fromEntries(c.layers.map((l) => [l.id, l.id === id]));
export const isSolo = (v: LayerVisibility, id: string): boolean => !!v[id] && Object.entries(v).every(([k, on]) => on === (k === id));

/** The entity keys the plan must not draw: everything on a hidden layer. */
export function hiddenKeys(a: LayerAssignment, v: LayerVisibility): Set<string> {
  const out = new Set<string>();
  for (const [id, list] of Object.entries(a.byLayer)) if (v[id] === false) for (const e of list) out.add(e.key);
  return out;
}

/** The plan colour of an entity when «цвета слоёв» is on: the obstacle classes (walls, covers) take their layer's colour. */
export function layerTints(a: LayerAssignment, c: LayersConfig = LAYERS): Map<string, string> {
  const out = new Map<string, string>();
  for (const l of c.layers) if (l.size_class) for (const e of a.byLayer[l.id]) out.set(e.key, l.color);
  return out;
}

// ---- picking ------------------------------------------------------------------------------------------------------------------

const shapeArea = (s: EntityShape): number => {
  if (s.kind === 'point') return Math.PI * s.r * s.r;
  if (s.kind === 'seg') return Math.hypot(s.b[0] - s.a[0], s.b[1] - s.a[1]) * Math.max(20, s.thickness);
  let a = 0;
  for (let i = 0; i < s.poly.length; i++) { const p = s.poly[i], q = s.poly[(i + 1) % s.poly.length]; a += p[0] * q[1] - q[0] * p[1]; }
  return Math.abs(a) / 2;
};

/** The point is on the entity (within `tol` cm of a point / a segment, inside a polygon). */
export function hitsEntity(e: Entity, p: Vec2, tol: number): boolean {
  const s = e.shape;
  if (s.kind === 'point') return Math.hypot(p[0] - e.pos[0], p[1] - e.pos[1]) <= s.r + tol;
  if (s.kind === 'seg') return distToWall(p, { a: s.a, b: s.b, thickness: s.thickness }) <= tol;
  return pointInPoly(p, s.poly) || s.poly.some((a, i) => distToWall(p, { a, b: s.poly[(i + 1) % s.poly.length], thickness: 0 }) <= tol / 2);
}

/** The entity of the ACTIVE layer under the point: the smallest one hit (a crate before the zone it stands in). Null: nothing. */
export function pickEntity(a: LayerAssignment, activeLayer: string, p: Vec2, tol: number): Entity | null {
  let best: Entity | null = null, bestA = Infinity;
  for (const e of a.byLayer[activeLayer] ?? []) {
    if (!hitsEntity(e, p, tol)) continue;
    const ar = shapeArea(e.shape);
    if (ar < bestA) { best = e; bestA = ar; }
  }
  return best;
}

// ---- editing a layout, scoped to the active layer --------------------------------------------------------------------------------

export type LayoutEdit =
  | { op: 'move'; key: string; delta: Vec2 }
  | { op: 'rotate'; key: string; deg: number }
  | { op: 'delete'; key: string }
  | { op: 'add'; template: AddTemplate; pos: Vec2; yaw?: number };

export interface LayoutEditResult { ok: boolean; layout: GenLayout; message: string; /** The key the edit made / moved (the page keeps it selected). */ key?: string }

const refuse = (layout: GenLayout, message: string): LayoutEditResult => ({ ok: false, layout, message });
const r1 = (v: number): number => Math.round(v);
const moved = (p: Vec2, d: Vec2): Vec2 => [r1(p[0] + d[0]), r1(p[1] + d[1])];
const freshId = (taken: Set<string>, prefix: string): string => { for (let n = 1; ; n++) { const id = `${prefix}${n}`; if (!taken.has(id)) return id; } };
const rotAbout = (p: Vec2, c: Vec2, deg: number): Vec2 => { const d = dirOf(deg), x = p[0] - c[0], y = p[1] - c[1]; return [r1(c[0] + x * d[0] - y * d[1]), r1(c[1] + x * d[1] + y * d[0])]; };
const layersOfBuilding = (id: string) => (layer: string) => layer === `roof:${id}` || layer.startsWith(`floor:${id}:`);

/**
 * One edit of the layout on the ACTIVE layer: an entity of another layer is refused (the tools act on the active layer only), so
 * is a tool the layer does not offer (layers.json `edit`). move / rotate: covers and free-standing walls; delete: covers, walls,
 * zones, buildings (with their walls, ladders and mantle edges - refused while a route uses the building); add: a cover or a wall
 * from the layer's `add` templates (its size class must be the layer's). Start + goals are edited with objectives.ts. Pure: the
 * input layout is not changed.
 */
export function editLayout(l: GenLayout, activeLayer: string, edit: LayoutEdit, c: LayersConfig = LAYERS): LayoutEditResult {
  const def = layerById(activeLayer, c);
  if (!def) return refuse(l, `нет слоя "${activeLayer}"`);
  const tool: LayerTool = edit.op === 'rotate' ? 'move' : edit.op;
  if (!def.edit.includes(tool)) return refuse(l, `слой «${def.name}» не поддерживает действие «${tool}»`);
  if (edit.op === 'add') {
    const t = edit.template;
    if (t.cover) {
      const cover: GenCover = { id: freshId(new Set(l.covers.map((x) => x.id)), 'COV_user_'), pos: [r1(edit.pos[0]), r1(edit.pos[1])], yaw: edit.yaw ?? 0, size: [...t.cover.size], kind: t.cover.kind };
      const e = layoutEntities({ ...l, covers: [cover], walls: [], zones: [], buildings: [], ladders: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], cut: undefined, objectives: undefined }).find((x) => x.type === 'cover')!;
      if (matchingLayers(e, c)[0] !== activeLayer) return refuse(l, `шаблон «${t.label}» не относится к слою «${def.name}»`);
      return { ok: true, layout: { ...l, covers: [...l.covers, cover] }, message: `добавлено: ${t.label} (${cover.id})`, key: `cover:${cover.id}` };
    }
    if (t.wall) {
      const d = dirOf(edit.yaw ?? 90), h = t.wall.length / 2;
      const wall: GenWall = { id: freshId(new Set(l.walls.map((x) => x.id)), 'W_user_'), a: [r1(edit.pos[0] - d[0] * h), r1(edit.pos[1] - d[1] * h)], b: [r1(edit.pos[0] + d[0] * h), r1(edit.pos[1] + d[1] * h)],
        height: t.wall.height, thickness: t.wall.thickness, kind: t.wall.kind };
      if (sizeClassOf(wall.height, wallFootprintM2(wall), wall.kind, c.size_classes) !== def.size_class) return refuse(l, `шаблон «${t.label}» не относится к слою «${def.name}»`);
      return { ok: true, layout: { ...l, walls: [...l.walls, wall] }, message: `добавлено: ${t.label} (${wall.id})`, key: `wall:${wall.id}` };
    }
    return refuse(l, 'этот шаблон — для файла пропсов (editProps)');
  }
  const a = assignLayers(layoutEntities(l), c);
  const owner = a.layerOf[edit.key];
  if (!owner) return refuse(l, `нет объекта ${edit.key}`);
  if (owner !== activeLayer) return refuse(l, `${edit.key} — на слое «${layerById(owner, c)?.name ?? owner}», активен «${def.name}»`);
  const [type, id] = [edit.key.slice(0, edit.key.indexOf(':')), edit.key.slice(edit.key.indexOf(':') + 1)];
  if (edit.op === 'move' || edit.op === 'rotate') {
    if (type === 'cover') {
      const covers = l.covers.map((x) => (x.id !== id ? x : edit.op === 'move' ? { ...x, pos: moved(x.pos, edit.delta) } : { ...x, yaw: (((x.yaw + edit.deg) % 360) + 360) % 360 }));
      return { ok: true, layout: { ...l, covers }, message: `${id}: ${edit.op === 'move' ? 'перемещено' : 'повёрнуто'}`, key: edit.key };
    }
    if (type === 'wall') {
      const walls = l.walls.map((x) => {
        if (x.id !== id) return x;
        if (edit.op === 'move') return { ...x, a: moved(x.a, edit.delta), b: moved(x.b, edit.delta) };
        const m = mid(x.a, x.b);
        return { ...x, a: rotAbout(x.a, m, edit.deg), b: rotAbout(x.b, m, edit.deg) };
      });
      return { ok: true, layout: { ...l, walls }, message: `${id}: ${edit.op === 'move' ? 'перемещено' : 'повёрнуто'}`, key: edit.key };
    }
    return refuse(l, `${type} нельзя двигать этим инструментом`);
  }
  // delete
  if (type === 'cover') return { ok: true, layout: { ...l, covers: l.covers.filter((x) => x.id !== id) }, message: `удалено: ${id}` };
  if (type === 'wall') return { ok: true, layout: { ...l, walls: l.walls.filter((x) => x.id !== id) }, message: `удалено: ${id}` };
  if (type === 'zone') return { ok: true, layout: { ...l, zones: l.zones.filter((x) => x.id !== id) }, message: `удалено: зона ${id}` };
  if (type === 'building') {
    const mine = layersOfBuilding(id);
    const users = l.routes.filter((r) => (r.layers ?? []).some(mine)).map((r) => r.id);
    if (users.length) return refuse(l, `здание ${id} стоит на маршруте (${users.join(', ')}) — удалить нельзя; перегенерируйте зону`);
    const goals = (l.objectives?.goals ?? []).filter((g) => g.building === id).map((g) => g.id);
    if (goals.length) return refuse(l, `в здании ${id} стоит цель (${goals.join(', ')}) — сначала перенесите её`);
    const next: GenLayout = { ...l, buildings: l.buildings.filter((b) => b.id !== id), walls: l.walls.filter((w) => w.building !== id),
      ladders: l.ladders.filter((k) => !mine(k.to) && !mine(k.from)) };
    if (l.mantle_edges) next.mantle_edges = l.mantle_edges.filter((m) => m.building !== id);
    return { ok: true, layout: next, message: `удалено: здание ${id} со стенами и лестницами` };
  }
  return refuse(l, `${type} нельзя удалить этим инструментом`);
}

// ---- editing the props file, scoped to the active layer ----------------------------------------------------------------------------

export type PropsEdit =
  | { op: 'add'; template: AddTemplate; pos: Vec2; yaw?: number; walk_layer?: string }
  | { op: 'move'; id: string; pos: Vec2 }
  | { op: 'delete'; id: string };
export interface PropsEditResult { ok: boolean; props: LevelPropsFile; message: string; id?: string }

/**
 * One hand edit of the map's props on the ACTIVE layer, written as the format's `overrides` (PROPS_LAYERS.md 1.4: add = a full object
 * `hand_<layer>_<n>` with origin "hand", move, delete - never re-rolled); a hand-added object is moved / removed in its own `add`
 * record. A prop of another layer is refused. A frozen file is refused. The props layer of the kind is created (empty, locked: only
 * hand objects) when the file has none. Pure: the input file is not changed; the page saves through PUT /api/levelgen/props.
 */
export function editProps(file: LevelPropsFile, activeLayer: string, edit: PropsEdit, c: LayersConfig = LAYERS): PropsEditResult {
  const def = layerById(activeLayer, c);
  const fail = (message: string): PropsEditResult => ({ ok: false, props: file, message });
  if (!def) return fail(`нет слоя "${activeLayer}"`);
  if (!def.props && !def.match.some((r) => r.type === 'prop')) return fail(`слой «${def.name}» не из файла пропсов`);
  if (file.frozen) return fail('файл пропсов заморожен (frozen) — правки запрещены');
  if (!def.edit.includes(edit.op)) return fail(`слой «${def.name}» не поддерживает действие «${edit.op}»`);
  const overrides: PropOverride[] = [...(file.overrides ?? [])];
  const pos = (p: Vec2): [number, number] => [r1(p[0]), r1(p[1])];
  if (edit.op === 'add') {
    const t = edit.template.prop;
    if (!t) return fail('этот шаблон — для раскладки (editLayout)');
    let layers = file.layers;
    let layer = layers.find((x) => x.kind === t.kind);
    if (!layer) {
      const made: PropLayer = { id: layers.some((x) => x.id === t.kind) ? `${t.kind}_hand` : t.kind, kind: t.kind, enabled: true, seed: 1, locked: true, rules: {}, objects: [],
        x_note: 'made by the level generator layers tool: hand objects only (overrides add)' };
      layers = [...layers, made]; layer = made;
    }
    const taken = new Set(resolveProps(file).map((x) => x.object.id));
    for (const ov of overrides) if (ov.op === 'add') taken.add((ov as { object: PropObject }).object.id);
    let n = 0;
    while (taken.has(`hand_${layer.id}_${n}`)) n++;
    const object: PropObject = { id: `hand_${layer.id}_${n}`, origin: 'hand', variant: t.variant, pos: pos(edit.pos), walk_layer: edit.walk_layer ?? 'ground', yaw: r1(edit.yaw ?? 0), ...(t.fields ?? {}) };
    const probe: Entity = { key: `prop:${object.id}`, type: 'prop', id: object.id, kind: t.kind, variant: t.variant, size_class: propSizeCm(t.variant) ? sizeClassOf(propSizeCm(t.variant)![2], 0, t.variant, c.size_classes) : undefined, pos: edit.pos, shape: { kind: 'point', r: 40 } };
    if (matchingLayers(probe, c)[0] !== activeLayer) return fail(`шаблон «${edit.template.label}» не относится к слою «${def.name}»`);
    overrides.push({ op: 'add', object });
    return { ok: true, props: { ...file, layers, overrides }, message: `добавлено: ${edit.template.label} (${object.id})`, id: object.id };
  }
  const cur = resolveProps(file).find((x) => x.object.id === edit.id);
  if (!cur) return fail(`нет объекта ${edit.id}`);
  const owner = assignLayers(propEntities(file), c).layerOf[`prop:${edit.id}`];
  if (owner !== activeLayer) return fail(`${edit.id} — на слое «${layerById(owner ?? '', c)?.name ?? owner}», активен «${def.name}»`);
  const addIdx = overrides.findIndex((ov) => ov.op === 'add' && (ov as { object: PropObject }).object.id === edit.id);
  if (edit.op === 'move') {
    if (addIdx >= 0) { const ov = overrides[addIdx] as { op: 'add'; object: PropObject }; overrides[addIdx] = { ...ov, object: { ...ov.object, pos: pos(edit.pos) } }; }
    else {
      const rest = overrides.filter((ov) => !(ov.op === 'move' && ov.id === edit.id));
      rest.push({ op: 'move', id: edit.id, pos: pos(edit.pos), walk_layer: cur.object.walk_layer });
      return { ok: true, props: { ...file, overrides: rest }, message: `${edit.id}: перемещено`, id: edit.id };
    }
    return { ok: true, props: { ...file, overrides }, message: `${edit.id}: перемещено`, id: edit.id };
  }
  if (addIdx >= 0) { overrides.splice(addIdx, 1); return { ok: true, props: { ...file, overrides }, message: `удалено: ${edit.id}` }; }
  const rest = overrides.filter((ov) => !((ov.op === 'move' || ov.op === 'set') && ov.id === edit.id));
  rest.push({ op: 'delete', id: edit.id, pos: [cur.object.pos[0], cur.object.pos[1]] });
  return { ok: true, props: { ...file, overrides: rest }, message: `удалено: ${edit.id}` };
}

// ---- re-roll scoped to obstacle layers --------------------------------------------------------------------------------------------

export interface LayerRerollResult { ok: boolean; layout: GenLayout; removed: string[]; added: string[]; dropped: string[]; message: string }

const segPointDist = (p: Vec2, w: GenWall): number => distToWall(p, { a: w.a, b: w.b, thickness: w.thickness });

/**
 * A region re-roll LIMITED to obstacle layers: `fresh` is the same zone re-rolled by regionRegen.ts ('jitter': the buildings stay,
 * the covers are re-dealt). Of it only the COVERS of the chosen layers inside the zone are taken; the old covers of those layers
 * inside the zone go; everything else of the old map stays (walls, buildings, the covers of the other layers, routes, zones).
 * A fresh cover that would stand in a kept cover, on a kept wall or under a lock is dropped. Deterministic.
 */
export function mergeLayerReroll(old: GenLayout, fresh: GenLayout, zone: readonly Vec2[], layerIds: readonly string[], c: LayersConfig = LAYERS): LayerRerollResult {
  const want = new Set(layerIds);
  const coverLayer = (cv: GenCover): string | undefined => matchingLayers({ key: `cover:${cv.id}`, type: 'cover', id: cv.id, kind: cv.kind, height_cm: cv.size[2], footprint_m2: coverFootprintM2(cv),
    size_class: sizeClassOf(cv.size[2], coverFootprintM2(cv), cv.kind, c.size_classes), pos: cv.pos, shape: { kind: 'point', r: 0 } }, c)[0];
  const locks = old.regen?.locks ?? [];
  const inZone = (p: Vec2) => pointInPoly(p, zone) && !locks.some((k) => pointInPoly(p, k.poly));
  const takes = (cv: GenCover) => inZone(cv.pos) && want.has(coverLayer(cv) ?? '');
  const removed = old.covers.filter(takes);
  const removedIds = new Set(removed.map((x) => x.id));
  const kept = old.covers.filter((x) => !removedIds.has(x.id));
  const oldJson = new Set(old.covers.map((x) => JSON.stringify(x)));
  const taken = new Set(kept.map((x) => x.id));
  const radius = (cv: GenCover) => Math.hypot(cv.size[0], cv.size[1]) / 2;
  const added: GenCover[] = [], dropped: string[] = [];
  for (const cv of fresh.covers) {
    if (!takes(cv) || oldJson.has(JSON.stringify(cv))) continue;
    const clash = kept.some((k) => Math.hypot(k.pos[0] - cv.pos[0], k.pos[1] - cv.pos[1]) < (radius(k) + radius(cv)) * 0.8) || added.some((k) => Math.hypot(k.pos[0] - cv.pos[0], k.pos[1] - cv.pos[1]) < (radius(k) + radius(cv)) * 0.6)
      || old.walls.some((w) => (w.floor ?? 0) === 0 && segPointDist(cv.pos, w) < Math.min(cv.size[0], cv.size[1]) / 2);
    if (clash) { dropped.push(cv.id); continue; }
    let id = cv.id;
    if (taken.has(id)) id = freshId(taken, `${cv.id}_r`);
    taken.add(id);
    added.push(id === cv.id ? cv : { ...cv, id });
  }
  const names = layerIds.map((id) => layerById(id, c)?.name ?? id).join(', ');
  if (!removed.length && !added.length) return { ok: false, layout: old, removed: [], added: [], dropped, message: `в зоне нет объектов слоёв: ${names}` };
  return { ok: true, layout: { ...old, covers: [...kept, ...added] }, removed: removed.map((x) => x.id), added: added.map((x) => x.id), dropped,
    message: `слои «${names}»: убрано ${removed.length}, добавлено ${added.length}${dropped.length ? `, отброшено ${dropped.length} (пересечения)` : ''}` };
}
