// Training Ground / level generator v2, stage 2 (board 066): BLOCK TEMPLATES. A map is split into LOTS (the blocks between the
// streets: the classic grammar's regions, generate.ts section 3, or the blocks of an expansion, expand.ts); every lot has a block
// TYPE. A region re-roll (regionRegen.ts) gives each lot whose centre is inside the zone ANOTHER type from the catalog
// (Content/Data/LevelGen/levelgen_v2.json templates.catalog: weights, minimum sizes, cover kinds) drawn with the click's seed, so a
// re-roll changes what stands in a block - a hall becomes a fenced compound, a junkyard a two-storey block - not only the covers.
// The templates are greybox set-pieces built with the generator's own helpers (addBuilding / fenceRect / addLadderAt) in the
// profile frame (metres, x east, y south) and converted to UE cm by finalizeDraftToLayout (heights snapped to the measured classes).
// Each template also returns a VISIT: the route fragment that takes the mandatory route into the new block (a door, a gate, a
// ladder onto the roof). Pure and deterministic: same lot + type + seed -> the same objects.

import cfg from '../../../../Content/Data/LevelGen/levelgen_v2.json' with { type: 'json' };
import type { Vec2 } from '../movement/geometry.ts';
import { dist, rng } from '../movement/geometry.ts';
import type { ZoneKind } from '../movement/layout.ts';
import type { Draft, DraftBuilding, Rect, Side } from './generate.ts';
import { COVER_SIZE, addBuilding, addLadderAt, fenceRect, finalizeDraftToLayout, generateLayoutWithLots, levelLayer, splitWall, stairChainUp } from './generate.ts';
import { obstacleHeight } from './obstacleHeights.ts';
import { drawAngle, fitScale, ROTATION, rotateContent, rotPoint } from './rotation.ts';
import type { Profile } from './profile.ts';
import { fromUE, toUE } from './profile.ts';
import type { BlockRecord, CoverKind, GenLayout, SetPiece } from './schema.ts';
import { DEFAULT_PARAMS, HALL_ROOF_LEVEL, LEVEL_HEIGHT, TEMPLATE_ARCHETYPE, rect } from './schema.ts';

/** The stage-2 configuration (templates, expansion, objectives). */
export const V2 = cfg;
const TC = cfg.templates;
export type TemplateId = keyof typeof cfg.templates.catalog;
export const TEMPLATE_IDS = Object.keys(TC.catalog) as TemplateId[];
export const isCatalogTemplate = (t: string): t is TemplateId => (TEMPLATE_IDS as string[]).includes(t);

// ---- frames -----------------------------------------------------------------------------------------------------------------

/** The plan frame of a layout (m): [west-east width, north-south depth] from its UE bounds (X north = depth, Y east = width). */
export const frameOf = (l: Pick<GenLayout, 'bounds'>): Vec2 => [(l.bounds.max[1] - l.bounds.min[1]) / 100, (l.bounds.max[0] - l.bounds.min[0]) / 100];
const norm = (a: Vec2, b: Vec2): [Vec2, Vec2] => [[Math.min(a[0], b[0]), Math.min(a[1], b[1])], [Math.max(a[0], b[0]), Math.max(a[1], b[1])]];
/** A metres rectangle (profile frame of `size`) as a UE [min, max] rectangle, cm. */
export const rectToUE = (r: Rect, size: Vec2): [Vec2, Vec2] => norm(toUE(r[0], size), toUE(r[1], size));
/** A UE [min, max] rectangle as a metres rectangle of the frame `size`. */
export const rectFromUE = (r: readonly [Vec2, Vec2], size: Vec2): Rect => norm(fromUE(r[0], size), fromUE(r[1], size));
export const rectCentre = (r: readonly [Vec2, Vec2]): Vec2 => [(r[0][0] + r[1][0]) / 2, (r[0][1] + r[1][1]) / 2];
export const inRect = (r: readonly [Vec2, Vec2], p: Vec2, margin = 0): boolean => p[0] >= r[0][0] - margin && p[0] <= r[1][0] + margin && p[1] >= r[0][1] - margin && p[1] <= r[1][1] + margin;
const area = (r: readonly [Vec2, Vec2]) => (r[1][0] - r[0][0]) * (r[1][1] - r[0][1]);
const bboxOf = (pts: readonly Vec2[]): [Vec2, Vec2] => [[Math.min(...pts.map((p) => p[0])), Math.min(...pts.map((p) => p[1]))], [Math.max(...pts.map((p) => p[0])), Math.max(...pts.map((p) => p[1]))]];

/** A string -> 32-bit hash (FNV-1a): per-lot random streams that do not depend on the lot order. */
export function strHash(s: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; }
  return h >>> 0;
}
export const lotSeed = (seed: number, key: string): number => (Math.imul(seed | 0, 1000003) + strHash(key)) >>> 0;

// ---- lots ---------------------------------------------------------------------------------------------------------------------

/** The lots of the classic grammar for a layout (generated again from its own seed + params; UE rectangles, classic types). */
export function classicLots(l: GenLayout, profile: Profile): BlockRecord[] {
  const base = l.expansion?.base_size_m ?? l.params.size_m;
  const g = generateLayoutWithLots({ profile, seed: l.seed, params: { ...l.params, size_m: base }, name: l.name });
  const classic = TC.classic as Record<string, string>;
  return g.lots.map((k) => ({ id: `LOT_${k.key}`, rect: rectToUE(k.rect_m, g.size_m), template: classic[k.key] ?? k.key }));
}

/** The map's lots: the stored `regen.blocks` (after a re-roll / an expansion), else the classic grammar's. */
export function lotsOf(l: GenLayout, profile: Profile): BlockRecord[] {
  const stored = l.regen?.blocks;
  if (stored && stored.length) return stored;
  return classicLots(l, profile);
}

/** The lot a point belongs to: the smallest lot containing it (lots of the classic grammar may overlap a little). */
export function lotAt(lots: readonly BlockRecord[], p: Vec2): BlockRecord | undefined {
  let best: BlockRecord | undefined;
  for (const k of lots) if (inRect(k.rect, p) && (!best || area(k.rect) < area(best.rect))) best = k;
  return best;
}

/** A lot that holds the map's spawn or end (the mandatory route's anchors) keeps its type. */
export function isAnchorLot(l: GenLayout, k: BlockRecord): boolean {
  return inRect(k.rect, l.spawn.pos) || inRect(k.rect, l.end);
}

// ---- picking ------------------------------------------------------------------------------------------------------------------

/** A template fits a lot (metres, any orientation) when its minimum size fits inside the lot minus the inset. */
export function templateFits(t: TemplateId, sizeM: Vec2): boolean {
  const [a, b] = TC.catalog[t].min_m;
  const lo = Math.min(sizeM[0], sizeM[1]) - 2 * TC.inset_m, hi = Math.max(sizeM[0], sizeM[1]) - 2 * TC.inset_m;
  return hi >= Math.max(a, b) && lo >= Math.min(a, b);
}

/**
 * The types a lot tries, in a seeded weighted order (without repeats), at most `candidates_per_lot`: every catalog type that fits,
 * minus the current one when `force_change` is set. Deterministic for (lot, current, seed).
 */
export function pickTemplates(lot: BlockRecord, current: string, seed: number, frame: Vec2): TemplateId[] {
  const r = rectFromUE(lot.rect, frame);
  const size: Vec2 = [r[1][0] - r[0][0], r[1][1] - r[0][1]];
  const pool = TEMPLATE_IDS.filter((t) => templateFits(t, size) && !(TC.force_change && t === current) && TC.catalog[t].weight > 0);
  const random = rng(lotSeed(seed, `${lot.id}:pick`));
  const out: TemplateId[] = [];
  const left = [...pool];
  while (left.length && out.length < TC.candidates_per_lot) {
    const total = left.reduce((s, t) => s + TC.catalog[t].weight, 0);
    let x = random() * total, k = 0;
    for (; k < left.length - 1; k++) { x -= TC.catalog[left[k]].weight; if (x <= 0) break; }
    out.push(left[k]);
    left.splice(k, 1);
  }
  return out;
}

// ---- building a template ------------------------------------------------------------------------------------------------------

/** A route fragment into a block (UE cm): the mandatory route visits it where its points in the old block were. */
export interface Visit { points: Vec2[]; layers: string[]; links: { at: number; via: string }[] }

export interface TemplateContext {
  /** The click's seed (re-roll) or the expansion seed. */
  seed: number;
  /** The plan frame (frameOf(layout)). */
  frame: Vec2;
  /** The street rectangles (metres, the same frame): doors and gates face the nearest one. */
  streets_m: Rect[];
  verticality: number;
  density: number;
  /** «Макс. угол поворота зданий» (rotation.ts): each template stands rotated within +-this (0 / absent = axis-aligned). */
  maxRotationDeg?: number;
  /** Scales the angle (the full re-roll's chaos when rotation.chaos_scales; default 1). */
  rotationScale?: number;
  /** The map's `building_plans` toggle (S2.2): the template's offices get band plans (generate.ts addPlannedOffice). */
  building_plans?: boolean;
}

export type TemplateContent = Pick<GenLayout, 'walls' | 'buildings' | 'ladders' | 'covers' | 'zones' | 'ambush_points'> & { mantle_edges: NonNullable<GenLayout['mantle_edges']> };

export interface BuiltTemplate { template: TemplateId; lot: string; content: TemplateContent; visit: Visit | null; /** Degrees the template stands rotated (0 = axis-aligned). */ rotation: number }

/** The street rectangles of a layout in its plan frame (the infrastructure zones Z_ST_*). */
export function streetRectsM(l: GenLayout): Rect[] {
  const f = frameOf(l);
  return l.zones.filter((z) => z.id.startsWith('Z_ST_')).map((z) => rectFromUE(bboxOf(z.poly), f));
}

const OPP: Record<Side, Side> = { north: 'south', south: 'north', east: 'west', west: 'east' };
const NORMAL: Record<Side, Vec2> = { north: [0, -1], south: [0, 1], west: [-1, 0], east: [1, 0] };
const wOf = (r: Rect) => r[1][0] - r[0][0];
const hOf = (r: Rect) => r[1][1] - r[0][1];
const horizontal = (s: Side) => s === 'north' || s === 'south';
/** The side of r a street lies closest to (the street must run along at least 2 m of it). */
export function streetSide(r: Rect, streets: readonly Rect[]): Side {
  let best: Side = 'south', gap = Infinity;
  const sides: Side[] = ['south', 'north', 'west', 'east'];
  for (const s of sides) {
    for (const st of streets) {
      const along = horizontal(s) ? Math.min(r[1][0], st[1][0]) - Math.max(r[0][0], st[0][0]) : Math.min(r[1][1], st[1][1]) - Math.max(r[0][1], st[0][1]);
      if (along < 2) continue;
      const g = s === 'north' ? r[0][1] - st[1][1] : s === 'south' ? st[0][1] - r[1][1] : s === 'west' ? r[0][0] - st[1][0] : st[0][0] - r[1][0];
      if (g >= -0.5 && g < gap) { gap = g; best = s; }
    }
  }
  return best;
}
/** The coordinate along side s at fraction t (x for north / south, y for west / east), kept `pad` from the corners. */
const along = (r: Rect, s: Side, t: number, pad: number) => {
  const lo = horizontal(s) ? r[0][0] : r[0][1], hi = horizontal(s) ? r[1][0] : r[1][1];
  return Math.max(lo + pad, Math.min(hi - pad, lo + (hi - lo) * t));
};
/** A point at `at` along side s, `inward` metres inside r (negative = outside). */
const sidePoint = (r: Rect, s: Side, at: number, inward: number): Vec2 => {
  const n = NORMAL[s];
  const base: Vec2 = horizontal(s) ? [at, s === 'north' ? r[0][1] : r[1][1]] : [s === 'west' ? r[0][0] : r[1][0], at];
  return [base[0] - n[0] * inward, base[1] - n[1] * inward];
};
/** r cut perpendicular to side s: the part of depth `depth` along s, and the rest. */
function splitAt(r: Rect, s: Side, depth: number): { near: Rect; far: Rect } {
  const [[x0, y0], [x1, y1]] = r;
  if (s === 'south') return { near: [[x0, y1 - depth], [x1, y1]], far: [[x0, y0], [x1, y1 - depth]] };
  if (s === 'north') return { near: [[x0, y0], [x1, y0 + depth]], far: [[x0, y0 + depth], [x1, y1]] };
  if (s === 'west') return { near: [[x0, y0], [x0 + depth, y1]], far: [[x0 + depth, y0], [x1, y1]] };
  return { near: [[x1 - depth, y0], [x1, y1]], far: [[x0, y0], [x1 - depth, y1]] };
}
const depthOf = (r: Rect, s: Side) => (horizontal(s) ? hOf(r) : wOf(r));
const lengthOf = (r: Rect, s: Side) => (horizontal(s) ? wOf(r) : hOf(r));
/** A sub-rectangle of r, `len` along side s and `dep` deep, flush with side `flush` (s or its opposite) `off` metres in, centred at t. */
function placeIn(r: Rect, s: Side, len: number, dep: number, flush: Side, off: number, t: number): Rect {
  const L = Math.min(len, lengthOf(r, s)), D = Math.min(dep, depthOf(r, s));
  const a0 = along(r, s, t, L / 2) - L / 2;
  if (horizontal(s)) {
    const y = flush === 'south' ? r[1][1] - off - D : r[0][1] + off;
    return [[a0, y], [a0 + L, y + D]];
  }
  const x = flush === 'east' ? r[1][0] - off - D : r[0][0] + off;
  return [[x, a0], [x + D, a0 + L]];
}
const doorOn = (b: DraftBuilding, s: Side): Vec2 | undefined => b.doors.find((d) => d.wall === `${b.id}_${s[0].toUpperCase()}`)?.pos;
const doorInside = (b: DraftBuilding, s: Side): Vec2 | undefined => {
  const p = doorOn(b, s);
  return p ? [p[0] - NORMAL[s][0] * TC.visit_inside_m, p[1] - NORMAL[s][1] * TC.visit_inside_m] : undefined;
};
const doorOutside = (b: DraftBuilding, s: Side): Vec2 | undefined => {
  const p = doorOn(b, s);
  return p ? [p[0] + NORMAL[s][0] * TC.visit_outside_m, p[1] + NORMAL[s][1] * TC.visit_outside_m] : undefined;
};

interface Ctx { d: Draft; r: Rect; s: Side; random: () => number; tag: string; V: number; density: number; spec: (typeof TC.catalog)[TemplateId] }
/** A visit in metres (converted to UE once the template is built). */
interface VisitM { points: Vec2[]; layers: string[]; links: { at: number; via: string }[] }
const groundVisit = (...pts: (Vec2 | undefined)[]): VisitM | null => {
  const p = pts.filter((x): x is Vec2 => !!x);
  return p.length ? { points: p, layers: p.map(() => 'ground'), links: [] } : null;
};

const FENCE_H = obstacleHeight('high_fence_over');
const FENCE_T = 10;

function zoneOf(c: Ctx, kind: ZoneKind, piece: SetPiece, r: Rect = c.r): void {
  c.d.zones.push({ id: `Z_${c.tag}`, kind, set_piece: piece, poly: rect.poly(r), density: c.spec.density, coverKinds: c.spec.covers as CoverKind[] });
}

/** Covers scattered over r by the template's density, out of `avoid` rectangles and `clear` points (gates, doors, the visit). */
function scatter(c: Ctx, r: Rect, avoid: Rect[], clear: Vec2[]): void {
  const want = Math.round(((wOf(r) * hOf(r)) / 100) * c.spec.density * TC.cover_factor * c.density);
  let placed = 0, tries = 0, n = 0;
  const kinds = c.spec.covers as CoverKind[];
  while (placed < want && tries++ < want * 30) {
    const p: Vec2 = [r[0][0] + 1.2 + c.random() * Math.max(0, wOf(r) - 2.4), r[0][1] + 1.2 + c.random() * Math.max(0, hOf(r) - 2.4)];
    const kind = kinds[Math.floor(c.random() * kinds.length)];
    const size = COVER_SIZE[kind];
    const rad = Math.max(size[0], size[1]) / 200;
    if (avoid.some((a) => rect.contains(a, p, rad + 1.2)) || clear.some((q) => dist(q, p) < rad + 2.5)) continue;
    if (c.d.covers.some((o) => dist(o.pos, p) < rad + Math.max(o.size[0], o.size[1]) / 200 + TC.cover_spacing_m)) continue;
    c.d.covers.push({ id: `COV_${c.tag}_${++n}`, pos: p, yaw: Math.round(c.random() * 4) * 90 + (kind === 'wreck' ? Math.round((c.random() * 2 - 1) * 30) : 0), size, kind });
    placed++;
  }
}
/** Ambush spots at the outside corners of a template building (1.6 m out diagonally), inside the lot. */
function cornerAmbush(c: Ctx, b: DraftBuilding): void {
  const [[x0, y0], [x1, y1]] = b.footprint;
  let k = c.d.ambush_points.length;
  for (const p of [[x0 - 1.6, y0 - 1.6], [x1 + 1.6, y0 - 1.6], [x0 - 1.6, y1 + 1.6], [x1 + 1.6, y1 + 1.6]] as Vec2[]) {
    if (rect.contains(c.r, p, -0.5)) c.d.ambush_points.push({ id: `AMB_${c.tag}_${++k}`, pos: p });
  }
}
/** A fence round r on 3 sides (the `open` side left to a building wall), gates { side, at, width }. */
function fence3(c: Ctx, r: Rect, open: Side, gates: { side: Side; at: number; width: number }[]): void {
  const [[x0, y0], [x1, y1]] = r;
  const g = (side: Side): [number, number][] => gates.filter((x) => x.side === side).map((x) => [x.at - x.width / 2, x.at + x.width / 2]);
  const id = `F_${c.tag}`;
  if (open !== 'north') splitWall(c.d, `${id}_N`, [x0, y0], [x1, y0], g('north'), FENCE_H, FENCE_T, 'fence');
  if (open !== 'south') splitWall(c.d, `${id}_S`, [x0, y1], [x1, y1], g('south'), FENCE_H, FENCE_T, 'fence');
  if (open !== 'west') splitWall(c.d, `${id}_W`, [x0, y0], [x0, y1], g('west'), FENCE_H, FENCE_T, 'fence');
  if (open !== 'east') splitWall(c.d, `${id}_E`, [x1, y0], [x1, y1], g('east'), FENCE_H, FENCE_T, 'fence');
}
const hallSpec = (c: Ctx, s: Side, piece: SetPiece) => ({ height: HALL_ROOF_LEVEL * LEVEL_HEIGHT, floors: HALL_ROOF_LEVEL, hollow: true, enterable: true, roof_walkable: c.V >= 1,
  stairwell: true, set_piece: piece, archetype: TEMPLATE_ARCHETYPE[piece], doorSides: [s, s], windowSides: [s, OPP[s]], roofHatch: c.V >= 2 });
/** The roof excursion of a hall with a stairwell: up the flight, across the roof, back down (links = the stair). */
function roofVisit(b: DraftBuilding, s: Side): VisitM | null {
  const chain = stairChainUp(b);
  const inside = doorInside(b, s);
  if (!chain || !b.roof_walkable) return groundVisit(inside);
  const roof = `roof:${b.id}`;
  const c = rect.centre(b.footprint);
  // up the switchback core flight by flight, across the roof, and back down the same flights
  const pts: Vec2[] = [...(inside ? [inside] : [])];
  const layers: string[] = [...(inside ? ['ground'] : [])];
  const links: { at: number; via: string }[] = [];
  for (let i = 0; i < chain.points.length; i++) {
    pts.push(chain.points[i]);
    layers.push(chain.layers[i]);
    const l = chain.links.find((x) => x.at === i);
    if (l) links.push({ at: pts.length - 1, via: l.via });
  }
  pts.push(c);
  layers.push(roof);
  for (let n = chain.flights.length - 1; n >= 0; n--) {
    const st = chain.flights[n];
    pts.push(st.top, st.pos);
    layers.push(levelLayer(b, st.to), levelLayer(b, st.from));
    links.push({ at: pts.length - 1, via: st.id });
  }
  return { points: pts, layers, links };
}

/** The catalog: each fills c.r (metres) and returns its visit (null = the route only passes by). */
const BUILD: Record<TemplateId, (c: Ctx) => VisitM | null> = {
  open_square: (c) => {
    zoneOf(c, 'open', 'open_square');
    const centre = rect.centre(c.r);
    scatter(c, c.r, [], [centre]);
    return groundVisit(centre);
  },
  junkyard_field: (c) => {
    const pad = TC.gate_w_m;
    const gates = [0.25, 0.75].map((t) => ({ side: c.s, at: along(c.r, c.s, t + (c.random() * 2 - 1) * 0.05, pad), width: TC.gate_w_m }));
    fenceRect(c.d, `F_${c.tag}`, c.r, gates);
    zoneOf(c, 'open', 'junkyard_field');
    const inside = gates.map((g) => sidePoint(c.r, c.s, g.at, TC.visit_inside_m));
    scatter(c, c.r, [], inside);
    return groundVisit(inside[0]);
  },
  one_entry_compound: (c) => {
    const sp = c.spec as typeof TC.catalog.one_entry_compound;
    const gateAt = along(c.r, c.s, 0.5 + (c.random() * 2 - 1) * 0.15, TC.gate_w_m);
    fenceRect(c.d, `F_${c.tag}`, c.r, [{ side: c.s, at: gateAt, width: TC.gate_w_m }]);
    const shedR = placeIn(c.r, c.s, Math.min(sp.shed_m[0], lengthOf(c.r, c.s) * 0.6), Math.min(sp.shed_m[1], depthOf(c.r, c.s) * 0.45), OPP[c.s], 1.5, 0.5 + (c.random() * 2 - 1) * 0.1);
    const shed = addBuilding(c.d, `B_${c.tag}_shed`, shedR, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'one_entry_compound', archetype: TEMPLATE_ARCHETYPE.one_entry_compound, doorSides: [c.s], windowSides: [c.s], partitionRooms: true }, c.random);
    zoneOf(c, 'open', 'one_entry_compound');
    const gateIn = sidePoint(c.r, c.s, gateAt, TC.visit_inside_m);
    scatter(c, c.r, [shedR], [gateIn, ...(doorOutside(shed, c.s) ? [doorOutside(shed, c.s)!] : [])]);
    cornerAmbush(c, shed);
    return groundVisit(gateIn, doorOutside(shed, c.s));
  },
  industrial_hall: (c) => {
    const sp = c.spec as typeof TC.catalog.industrial_hall;
    const k = () => c.random() * sp.shrink / 2;
    const r: Rect = [[c.r[0][0] + wOf(c.r) * k(), c.r[0][1] + hOf(c.r) * k()], [c.r[1][0] - wOf(c.r) * k(), c.r[1][1] - hOf(c.r) * k()]];
    const b = addBuilding(c.d, `B_${c.tag}_hall`, r, hallSpec(c, c.s, 'industrial_hall'), c.random);
    zoneOf(c, 'wall_run', 'industrial_hall');
    cornerAmbush(c, b);
    return roofVisit(b, c.s);
  },
  warehouse: (c) => {
    const sp = c.spec as typeof TC.catalog.warehouse;
    const { near, far } = splitAt(c.r, c.s, depthOf(c.r, c.s) * (1 - sp.building_share));
    const b = addBuilding(c.d, `B_${c.tag}_store`, far, hallSpec(c, c.s, 'warehouse'), c.random);
    const gateAt = along(near, c.s, 0.5 + (c.random() * 2 - 1) * 0.2, TC.gate_w_m);
    fence3(c, near, OPP[c.s], [{ side: c.s, at: gateAt, width: TC.gate_w_m }]);
    zoneOf(c, 'open', 'warehouse');
    const gateIn = sidePoint(near, c.s, gateAt, TC.visit_inside_m);
    scatter(c, near, [far], [gateIn, ...b.doors.map((x) => x.pos)]);
    const v = roofVisit(b, c.s);
    return v ? { points: [gateIn, ...v.points], layers: ['ground', ...v.layers], links: v.links.map((x) => ({ at: x.at + 1, via: x.via })) } : groundVisit(gateIn);
  },
  small_office: (c) => {
    const sp = c.spec as typeof TC.catalog.small_office;
    const j = () => 0.9 + c.random() * 0.1;
    const r = placeIn(c.r, c.s, Math.min(sp.max_m[0], lengthOf(c.r, c.s)) * j(), Math.min(sp.max_m[1], depthOf(c.r, c.s)) * j(), c.s, 0.5, 0.5 + (c.random() * 2 - 1) * 0.1);
    const b = addBuilding(c.d, `B_${c.tag}_office`, r, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'facade', archetype: TEMPLATE_ARCHETYPE.small_office, doorSides: [c.s], windowSides: [c.s, OPP[c.s]], partitionRooms: true }, c.random);
    zoneOf(c, 'open', 'facade');
    scatter(c, c.r, [r], b.doors.map((x) => x.pos));
    cornerAmbush(c, b);
    return groundVisit(doorInside(b, c.s));
  },
  facade: (c) => {
    const sp = c.spec as typeof TC.catalog.facade;
    const k = () => c.random() * 0.05;
    const r: Rect = [[c.r[0][0] + wOf(c.r) * k(), c.r[0][1] + hOf(c.r) * k()], [c.r[1][0] - wOf(c.r) * k(), c.r[1][1] - hOf(c.r) * k()]];
    addBuilding(c.d, `B_${c.tag}_facade`, r, { height: sp.height_cm, floors: sp.floors, hollow: true, enterable: false, roof_walkable: false, set_piece: 'facade', archetype: TEMPLATE_ARCHETYPE.facade }, c.random);
    zoneOf(c, 'wall_run', 'facade');
    return null;
  },
  ladder_overwatch: (c) => {
    const sp = c.spec as typeof TC.catalog.ladder_overwatch;
    const r = placeIn(c.r, c.s, Math.min(sp.max_m[0], lengthOf(c.r, c.s) * 0.7), Math.min(sp.max_m[1], depthOf(c.r, c.s) * 0.6), OPP[c.s], 1.0, 0.5 + (c.random() * 2 - 1) * 0.15);
    const b = addBuilding(c.d, `B_${c.tag}_gallery`, r, { height: LEVEL_HEIGHT, floors: 1, enterable: false, roof_walkable: c.V >= 1, set_piece: 'ladder_overwatch', archetype: TEMPLATE_ARCHETYPE.ladder_overwatch }, c.random);
    zoneOf(c, 'open', 'ladder_overwatch');
    let visit: VisitM | null = null;
    if (c.V >= 1) {
      addLadderAt(c.d, b, c.s, c.random);
      const lad = c.d.ladders[c.d.ladders.length - 1];
      const roof = `roof:${b.id}`;
      visit = { points: [lad.foot, lad.top, rect.centre(r), lad.top, lad.foot], layers: ['ground', roof, roof, roof, 'ground'], links: [{ at: 1, via: lad.id }, { at: 4, via: lad.id }] };
      scatter(c, c.r, [r], [lad.foot]);
    } else {
      const front = sidePoint(r, c.s, along(r, c.s, 0.5, 0), -TC.visit_outside_m);
      scatter(c, c.r, [r], [front]);
      visit = groundVisit(front);
    }
    cornerAmbush(c, b);
    return visit;
  },
  tank_farm: (c) => {
    const sp = c.spec as typeof TC.catalog.tank_farm;
    const { near, far } = splitAt(c.r, c.s, Math.min(depthOf(c.r, c.s) * 0.5, sp.pump_m[1] + 4));
    const pumpR = placeIn(near, c.s, Math.min(sp.pump_m[0], lengthOf(near, c.s) * 0.7), Math.min(sp.pump_m[1], depthOf(near, c.s) - 2), c.s, 1.0, 0.5 + (c.random() * 2 - 1) * 0.15);
    const pump = addBuilding(c.d, `B_${c.tag}_pump`, pumpR, { height: LEVEL_HEIGHT, floors: 1, enterable: true, roof_walkable: false, set_piece: 'tank_farm', archetype: TEMPLATE_ARCHETYPE.tank_farm, doorSides: [c.s], windowSides: [c.s, OPP[c.s]] }, c.random);
    const silo = COVER_SIZE.silo, rad = silo[0] / 200;
    const lenF = lengthOf(far, c.s), depF = depthOf(far, c.s);
    const yard: Rect = [[far[0][0] + 0.5, far[0][1] + 0.5], [far[1][0] - 0.5, far[1][1] - 0.5]];
    fenceRect(c.d, `F_${c.tag}`, yard, [{ side: c.s, at: along(yard, c.s, 0.3, TC.gate_w_m), width: TC.gate_w_m }, { side: c.s, at: along(yard, c.s, 0.7, TC.gate_w_m), width: TC.gate_w_m }]);
    let n = 0;
    if (depF >= 2 * rad + 4) {
      const count = Math.max(1, Math.min(3, Math.floor((lenF - 4) / (2 * rad + 4))));
      for (let k = 0; k < count; k++) {
        const at = along(far, c.s, (k + 1) / (count + 1), rad + 2);
        const mid = sidePoint(far, OPP[c.s], at, depF / 2);
        c.d.covers.push({ id: `COV_${c.tag}_silo${++n}`, pos: mid, yaw: 0, size: silo, kind: 'silo' });
      }
    }
    zoneOf(c, 'open', 'tank_farm');
    scatter(c, near, [pumpR], pump.doors.map((x) => x.pos));
    cornerAmbush(c, pump);
    return groundVisit(doorInside(pump, c.s));
  },
  fenced_lot: (c) => {
    const g1 = along(c.r, c.s, 0.5 + (c.random() * 2 - 1) * 0.2, TC.gate_w_m), g2 = along(c.r, OPP[c.s], 0.5 + (c.random() * 2 - 1) * 0.2, TC.gate_w_m);
    fenceRect(c.d, `F_${c.tag}`, c.r, [{ side: c.s, at: g1, width: TC.gate_w_m }, { side: OPP[c.s], at: g2, width: TC.gate_w_m }]);
    zoneOf(c, 'corridor', 'alley');
    const gin = sidePoint(c.r, c.s, g1, TC.visit_inside_m);
    scatter(c, c.r, [], [gin, sidePoint(c.r, OPP[c.s], g2, TC.visit_inside_m), rect.centre(c.r)]);
    return groundVisit(gin, rect.centre(c.r));
  },
  two_storey: (c) => {
    const sp = c.spec as typeof TC.catalog.two_storey;
    const r = placeIn(c.r, c.s, Math.min(sp.max_m[0], lengthOf(c.r, c.s)), Math.min(sp.max_m[1], depthOf(c.r, c.s)), c.s, 0.5, 0.5 + (c.random() * 2 - 1) * 0.1);
    const b = addBuilding(c.d, `B_${c.tag}_block`, r, { height: 2 * LEVEL_HEIGHT, floors: 2, enterable: true, roof_walkable: c.V >= 1, stairwell: true, set_piece: 'facade', archetype: TEMPLATE_ARCHETYPE.two_storey, doorSides: [c.s], windowSides: [c.s, OPP[c.s]], partitionRooms: true, roofHatch: c.V >= 2 }, c.random);
    zoneOf(c, 'open', 'facade');
    scatter(c, c.r, [r], b.doors.map((x) => x.pos));
    cornerAmbush(c, b);
    return groundVisit(doorInside(b, c.s));
  },
  boiler_yard: (c) => {
    const gateAt = along(c.r, c.s, 0.5 + (c.random() * 2 - 1) * 0.2, TC.gate_w_m);
    fenceRect(c.d, `F_${c.tag}`, c.r, [{ side: c.s, at: gateAt, width: TC.gate_w_m }]);
    zoneOf(c, 'open', 'one_entry_compound');
    const gin = sidePoint(c.r, c.s, gateAt, TC.visit_inside_m);
    scatter(c, c.r, [], [gin]);
    return groundVisit(gin);
  },
};

const emptyDraft = (): Draft => ({ walls: [], buildings: [], ladders: [], covers: [], zones: [], routes: [], spawn_points: [], ambush_points: [], patrol_routes: [], mantle_edges: [], keepFree: [] });

/** The id tag of a lot's template objects: unique per lot and seed (B_<tag>_hall, F_<tag>_N_0, COV_<tag>_3 ...). */
export const templateTag = (lotId: string, seed: number) => `${lotId.replace(/^LOT_/, '')}_t${seed}`;

/**
 * Fill a lot with a template (UE content + the visit). Deterministic for (template, lot, ctx.seed). `place` (the 'full' re-roll,
 * fullReroll.ts): the template's rectangle inside the lot (metres; the inset still applies) and the street side it faces.
 */
export function buildTemplate(template: TemplateId, lot: BlockRecord, ctx: TemplateContext, place?: { side: Side; inner_m: Rect }): BuiltTemplate {
  const outer0 = place?.inner_m ?? rectFromUE(lot.rect, ctx.frame);
  // rotated buildings (rotation.ts): a seeded angle per (lot, template, seed) - its own random stream, drawn only when the max is > 0;
  // the template is built axis-aligned in its rectangle shrunk so the rotated rectangle fits, then turned about the centre
  let angle = drawAngle(rng(lotSeed(ctx.seed, `${lot.id}:${template}:rot`)), ctx.maxRotationDeg ?? 0, ctx.rotationScale ?? 1);
  let outer: Rect = outer0;
  // a template too big to turn that far in its lot turns less (half the angle, a quarter...) and below rotation.min_deg stays square
  const w0 = outer0[1][0] - outer0[0][0], h0 = outer0[1][1] - outer0[0][1];
  while (angle && !templateFits(template, [w0 * fitScale(w0, h0, angle), h0 * fitScale(w0, h0, angle)])) angle = Math.abs(angle / 2) < ROTATION.min_deg ? 0 : Math.round(angle * 5) / 10;
  if (angle) {
    const k = fitScale(w0, h0, angle), cx = (outer0[0][0] + outer0[1][0]) / 2, cy = (outer0[0][1] + outer0[1][1]) / 2;
    outer = [[cx - (w0 * k) / 2, cy - (h0 * k) / 2], [cx + (w0 * k) / 2, cy + (h0 * k) / 2]];
  }
  const inset = TC.inset_m;
  const r: Rect = [[outer[0][0] + inset, outer[0][1] + inset], [outer[1][0] - inset, outer[1][1] - inset]];
  const d = emptyDraft();
  if (ctx.building_plans) d.building_plans = true;
  const c: Ctx = { d, r, s: place?.side ?? streetSide(r, ctx.streets_m), random: rng(lotSeed(ctx.seed, `${lot.id}:${template}`)), tag: templateTag(lot.id, ctx.seed),
    V: ctx.verticality, density: ctx.density, spec: TC.catalog[template] };
  const vm = BUILD[template](c);
  const l = finalizeDraftToLayout(d, ctx.frame, { name: 'template', profile: 'template', seed: ctx.seed, params: DEFAULT_PARAMS, start: rect.centre(r), end: rect.centre(r) });
  const pivot = toUE(rect.centre(r), ctx.frame);
  const U = (p: Vec2): Vec2 => { const q = toUE(p, ctx.frame); return angle ? rotPoint(q, pivot, angle).map((v) => Math.round(v * 100) / 100) as Vec2 : q; };
  const content: TemplateContent = { walls: l.walls, buildings: l.buildings, ladders: l.ladders, covers: l.covers, zones: l.zones, ambush_points: l.ambush_points, mantle_edges: l.mantle_edges ?? [] };
  return {
    template,
    lot: lot.id,
    content: rotateContent(content, pivot, angle),
    visit: vm ? { points: vm.points.map(U), layers: vm.layers, links: vm.links } : null,
    rotation: angle,
  };
}

/** The layout's block-template context (frame, streets, the generation params). */
export function templateContext(l: GenLayout, seed: number): TemplateContext {
  return { seed, frame: frameOf(l), streets_m: streetRectsM(l), verticality: l.params.verticality, density: l.params.density, maxRotationDeg: l.params.max_rotation_deg ?? ROTATION.max_deg_default,
    ...(l.params.building_plans ? { building_plans: true } : {}) };
}
