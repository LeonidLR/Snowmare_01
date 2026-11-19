// Training Ground / level generator v3 - BAND PLAN (S2_ROOMS_PLAN.md sections 3.1, 3.3, 3.4, 3.8; step S2.1): slices a rectangle into
// bays along its long side and bands of rooms across it from a recipe table (Content/Data/LevelGen/building_plans.json). Only the
// office recipe exists so far. PURE: integer cm, no draft / layout / engine types, nothing wired into generate.ts (S2.2 adds doors).
// Frame: u along the long side 0..L, v across 0..D, v = 0 is the CORE WALL; cells are axis-aligned rectangles that tile [0,L] x [0,D]
// exactly. All coordinates are multiples of 100 except the edge a core cell makes at its depth (350). The same plan serves every storey.
// Chain (3.8): per variant (double-loaded -> single-loaded -> corridor-less when no core is given) up to `max_rerolls` sub-seeds
// fnv1a(`${seed}|${k}`), each self-checked by validatePlanTiling; nothing fits -> { kind: 'fallback' } and the caller keeps today's BSP.
import cfg from '../../../../../Content/Data/LevelGen/building_plans.json' with { type: 'json' };
import { rng } from '../../movement/geometry.ts';
import { fnv1a } from '../props/rng.ts';

/** The plan constants and recipes (building_plans.json). */
export const BUILDING_PLANS = cfg;
const C = cfg.constants;
const OFFICE = cfg.recipes.office;
const GRID = C.grid_cm;
const COREBAY = C.core.bay_length_cm;

export type BayKind = 'core' | 'room' | 'end' | 'exit';
export type CellKind = 'room' | 'corridor' | 'core' | 'service' | 'end';
export type CellBand = 'back' | 'corridor' | 'front' | 'full';
export type PlanVariant = 'double' | 'single' | 'corridorless';

/** A bay: a stretch of the long side with one cross recipe. `exit` = a room bay whose corridor end is the building's exit slot. */
export interface PlanBay { kind: BayKind; u0: number; u1: number }
/** One rectangle of the tiling, cm, `[u0,u1] x [v0,v1]`. */
export interface PlanCell { id: string; kind: CellKind; u0: number; u1: number; v0: number; v1: number; band: CellBand }

export interface BandPlan {
  kind: 'bands' | 'fallback';
  /** Why no plan was made (kind 'fallback'). */
  fallback?: string;
  L: number;
  D: number;
  bays: PlanBay[];
  cells: PlanCell[];
  /** The recipe variant that produced the plan. */
  variant?: PlanVariant;
}

export interface BandPlanInput {
  /** Long side and depth, cm, multiples of 100 (snapFootprint). */
  L: number;
  D: number;
  archetype: 'office';
  /** The core bay (900 long, `depth` = 350 two lanes / 200 one lane) starting at `u0`, or no core. */
  core: { u0: number; depth: 350 | 200 } | null;
  seed: number;
}

/** Floors a footprint to the 100 cm grid and puts the long side first; `swapped` = the long side was the second one. */
export function snapFootprint(widthCm: number, depthCm: number): { L: number; D: number; swapped: boolean } {
  const a = Math.floor(widthCm / GRID) * GRID, b = Math.floor(depthCm / GRID) * GRID;
  return b > a ? { L: b, D: a, swapped: true } : { L: a, D: b, swapped: false };
}

type Rand = () => number;
type RawCell = Omit<PlanCell, 'id'>;
const pickInt = (r: Rand, n: number): number => Math.floor(r() * n);
const pick = <T>(r: Rand, xs: readonly T[]): T => xs[pickInt(r, xs.length)];
const steps = (lo: number, hi: number): number[] => { const o: number[] = []; for (let x = lo; x <= hi; x += GRID) o.push(x); return o; };
const cell = (kind: CellKind, band: CellBand, u0: number, u1: number, v0: number, v1: number): RawCell => ({ kind, band, u0, u1, v0, v1 });
const inRange = (x: number, lo: number, hi: number): boolean => x >= lo && x <= hi;

/** Splits a stretch (0 or >= 400 long) into room bays of 400-800: a seeded count, then the 100 cm steps of the remainder go to the widest bay first. */
function splitStretch(S: number, r: Rand): number[] {
  if (S === 0) return [];
  const nMin = Math.ceil(S / C.room_cm.max), nMax = Math.floor(S / C.room_cm.min);
  const n = nMin + pickInt(r, nMax - nMin + 1);
  const w = new Array<number>(n).fill(C.room_cm.min);
  for (let left = (S - C.room_cm.min * n) / GRID; left > 0; --left) {
    const open = w.map((_, i) => i).filter((i) => w[i] < C.room_cm.max);
    const widest = Math.max(...open.map((i) => w[i]));
    w[pick(r, open.filter((i) => w[i] === widest))] += GRID;
  }
  return w;
}

/** The bays along u: the core bay, room bays, optionally one end suite or exit bay at a building end. A string = infeasible (reason). */
function planBaysU(L: number, core: BandPlanInput['core'], r: Rand, allowEnds: boolean): PlanBay[] | string {
  const stretches = (core ? [[0, core.u0], [core.u0 + COREBAY, L]] : [[0, L]]).filter(([a, b]) => b > a);
  for (const [a, b] of stretches) if (b - a < C.room_cm.min) return `stretch_${b - a}_too_short`;
  // Options: none, or (end | exit) at the left / right building end, whose stretch must stay 0 or >= 400 after the end bay.
  type Opt = { side: 'left' | 'right'; kind: 'end' | 'exit'; ends: number[] };
  const opts: Opt[] = [];
  if (allowEnds) {
    for (const side of ['left', 'right'] as const) {
      const st = stretches.find(([a, b]) => (side === 'left' ? a === 0 : b === L));
      if (!st) continue;
      const S = st[1] - st[0];
      const ends = steps(C.end_bay_cm.min, C.end_bay_cm.max).filter((e) => S - e === 0 || S - e >= C.room_cm.min);
      if (ends.length) opts.push({ side, kind: 'end', ends });
      opts.push({ side, kind: 'exit', ends: [] });
    }
  }
  const opt = pickInt(r, opts.length + 1) === opts.length ? null : pick(r, opts);
  const bays: PlanBay[] = [];
  let coreDone = !core;
  for (const [a, b] of stretches) {
    if (!coreDone && a > core!.u0) { bays.push({ kind: 'core', u0: core!.u0, u1: core!.u0 + COREBAY }); coreDone = true; }
    const here = opt && (opt.side === 'left' ? a === 0 : b === L);
    const e = here && opt!.kind === 'end' ? pick(r, opt!.ends) : 0;
    const lo = here && opt!.side === 'left' ? a + e : a, hi = here && opt!.side === 'right' ? b - e : b;
    if (here && opt!.kind === 'end' && opt!.side === 'left') bays.push({ kind: 'end', u0: a, u1: a + e });
    let u = lo;
    const rooms = splitStretch(hi - lo, r).map((w): PlanBay => { const bay: PlanBay = { kind: 'room', u0: u, u1: u + w }; u += w; return bay; });
    if (here && opt!.kind === 'exit' && rooms.length) rooms[opt!.side === 'left' ? 0 : rooms.length - 1].kind = 'exit';
    bays.push(...rooms);
    if (here && opt!.kind === 'end' && opt!.side === 'right') bays.push({ kind: 'end', u0: b - e, u1: b });
  }
  if (!coreDone) bays.push({ kind: 'core', u0: core!.u0, u1: core!.u0 + COREBAY });
  return bays;
}

/** Maximal runs of consecutive room / exit bays: the stretches one corridor cell runs along. */
function corridorRuns(bays: PlanBay[]): [number, number][] {
  const runs: [number, number][] = [];
  for (const b of bays) {
    if (b.kind !== 'room' && b.kind !== 'exit') continue;
    const last = runs[runs.length - 1];
    if (last && last[1] === b.u0) last[1] = b.u1; else runs.push([b.u0, b.u1]);
  }
  return runs;
}

/** The two front rooms across a core bay (a 900 cell would break the 800 room maximum): widths in 100 steps, seeded. */
function splitCoreFront(len: number, r: Rand): [number, number] {
  const w1 = pick(r, steps(Math.max(C.room_cm.min, len - C.room_cm.max), Math.min(C.room_cm.max, len - C.room_cm.min)));
  return [w1, len - w1];
}

/** Double-loaded (D >= 1000): back room B - corridor 200 - front room; one straight corridor line v = B..B+200 for the whole plan. */
function buildDouble(inp: BandPlanInput, bays: PlanBay[], r: Rand): RawCell[] | string {
  const { D, core } = inp, K = core?.depth ?? 0, Cw = C.corridor_cm;
  const back = steps(OFFICE.back_room_depth_cm.min, OFFICE.back_room_depth_cm.max).filter((B) => {
    const F = D - B - Cw;
    return F >= OFFICE.front_room_min_depth_cm && inRange(F, C.room_cm.min, C.room_cm.max) && (!core || B + Cw - K <= C.core.corridor_max_cm);
  });
  if (!back.length) return 'double_no_back_depth';
  const B = pick(r, back), out: RawCell[] = [];
  for (const b of bays) {
    if (b.kind === 'end') out.push(cell('end', 'full', b.u0, b.u1, 0, D));
    else if (b.kind === 'core') {
      out.push(cell('core', 'back', b.u0, b.u1, 0, K), cell('corridor', 'corridor', b.u0, b.u1, K, B + Cw));
      const [w1] = splitCoreFront(b.u1 - b.u0, r);
      out.push(cell('room', 'front', b.u0, b.u0 + w1, B + Cw, D), cell('room', 'front', b.u0 + w1, b.u1, B + Cw, D));
    } else out.push(cell('room', 'back', b.u0, b.u1, 0, B), cell('room', 'front', b.u0, b.u1, B + Cw, D));
  }
  for (const [u0, u1] of corridorRuns(bays)) out.push(cell('corridor', 'corridor', u0, u1, B, B + Cw));
  return out;
}

/** Single-loaded (800 <= D < 1000): service band S - corridor 200 - front room; the core replaces the service band (S = its depth). */
function buildSingle(inp: BandPlanInput, bays: PlanBay[], r: Rand): RawCell[] | string {
  const { D, core } = inp, Cw = C.corridor_cm;
  const fits = (S: number): boolean => inRange(D - S - Cw, Math.max(OFFICE.front_room_min_depth_cm, C.room_cm.min), C.room_cm.max);
  const depths = core ? [core.depth] : steps(C.service_room.depth_min_cm, C.service_room.depth_max_cm).filter(fits);
  if (!depths.length || !fits(depths[0])) return core ? 'single_core_does_not_fit' : 'single_no_service_depth';
  const S = pick(r, depths);
  const out: RawCell[] = [];
  for (const b of bays) {
    if (b.kind === 'end') out.push(cell('end', 'full', b.u0, b.u1, 0, D));
    else if (b.kind === 'core') {
      out.push(cell('core', 'back', b.u0, b.u1, 0, S));
      const [w1] = splitCoreFront(b.u1 - b.u0, r);
      out.push(cell('room', 'front', b.u0, b.u0 + w1, S + Cw, D), cell('room', 'front', b.u0 + w1, b.u1, S + Cw, D));
    } else {
      const w = b.u1 - b.u0, n = w > C.service_room.width_max_cm ? 2 : 1, w1 = n === 1 ? w : Math.floor(w / 2 / GRID) * GRID;
      out.push(cell('service', 'back', b.u0, b.u0 + w1, 0, S));
      if (n === 2) out.push(cell('service', 'back', b.u0 + w1, b.u1, 0, S));
      out.push(cell('room', 'front', b.u0, b.u1, S + Cw, D));
    }
  }
  // The core's corridor row is the neighbours' (S = K): one straight cell from the first to the last non-end bay.
  const run: [number, number][] = [];
  for (const b of bays) if (b.kind !== 'end') { const l = run[run.length - 1]; if (l && l[1] === b.u0) l[1] = b.u1; else run.push([b.u0, b.u1]); }
  for (const [u0, u1] of run) out.push(cell('corridor', 'corridor', u0, u1, S, S + Cw));
  return out;
}

/** Corridor-less (no core): one room per bay over the whole depth, or a back / front pair when the depth passes 800. */
function buildCorridorless(inp: BandPlanInput, bays: PlanBay[], r: Rand): RawCell[] | string {
  const { D } = inp, out: RawCell[] = [];
  if (D > 2 * C.room_cm.max) return 'corridorless_too_deep';
  const cut = D <= C.room_cm.max ? 0 : pick(r, steps(Math.max(C.room_cm.min, D - C.room_cm.max), Math.min(C.room_cm.max, D - C.room_cm.min)));
  for (const b of bays) {
    if (!cut) out.push(cell('room', 'full', b.u0, b.u1, 0, D));
    else out.push(cell('room', 'back', b.u0, b.u1, 0, cut), cell('room', 'front', b.u0, b.u1, cut, D));
  }
  return out;
}

const finish = (raw: RawCell[]): PlanCell[] =>
  raw.sort((a, b) => a.u0 - b.u0 || a.v0 - b.v0 || a.u1 - b.u1 || a.v1 - b.v1).map((c, i) => ({ id: `c${i}`, ...c }));

/**
 * Plans one footprint. Deterministic: same input -> identical plan. `kind: 'fallback'` (no cells) = the caller keeps its BSP / hall / one room.
 */
export function planBands(inp: BandPlanInput): BandPlan {
  const { L, D, core } = inp;
  const fb = (reason: string): BandPlan => ({ kind: 'fallback', fallback: reason, L, D, bays: [], cells: [] });
  if (L % GRID || D % GRID) return fb('footprint_off_grid');
  if (L < OFFICE.min_length_cm || D < OFFICE.min_depth_single_loaded_cm) return fb('footprint_too_small');
  if (D > L) return fb('long_side_not_first');
  if (core) {
    if (core.u0 % GRID || core.u0 < 0 || core.u0 + COREBAY > L) return fb('core_does_not_fit');
    const a = core.u0, b = L - core.u0 - COREBAY;
    if ((a > 0 && a < C.room_cm.min) || (b > 0 && b < C.room_cm.min)) return fb(`core_stretch_${a > 0 && a < C.room_cm.min ? a : b}`);
  }
  const variants: PlanVariant[] = [];
  if (D >= OFFICE.min_depth_double_loaded_cm) variants.push('double');
  variants.push('single');
  if (!core) variants.push('corridorless');
  const why: string[] = [];
  for (const variant of variants) {
    let reason = '';
    for (let k = 0; k < C.max_rerolls; ++k) {
      const r = rng(fnv1a(`${inp.seed}|${k}`));
      const bays = planBaysU(L, core, r, variant !== 'corridorless');
      if (typeof bays === 'string') { reason = bays; continue; }
      const raw = variant === 'double' ? buildDouble(inp, bays, r) : variant === 'single' ? buildSingle(inp, bays, r) : buildCorridorless(inp, bays, r);
      if (typeof raw === 'string') { reason = raw; continue; }
      const plan: BandPlan = { kind: 'bands', L, D, bays, cells: finish(raw), variant };
      const problems = validatePlanTiling(plan);
      if (!problems.length) return plan;
      reason = `invalid: ${problems[0]}`;
    }
    why.push(`${variant}: ${reason}`);
  }
  return fb(why.join(' | '));
}

/** The problems of a plan (empty = fine): exact tiling, grid, bay and cell sizes per section 3.1. */
export function validatePlanTiling(plan: BandPlan): string[] {
  const out: string[] = [];
  const { L, D } = plan;
  if (plan.kind === 'fallback') {
    if (plan.cells.length || plan.bays.length) out.push('fallback plan has cells');
    if (!plan.fallback) out.push('fallback plan names no reason');
    return out;
  }
  if (!plan.cells.length) return ['bands plan has no cells'];
  const coreEdges = new Set(plan.cells.filter((c) => c.kind === 'core').map((c) => c.v1));
  const onGrid = (x: number, v: boolean): boolean => x % GRID === 0 || (v && coreEdges.has(x));
  let area = 0;
  for (const c of plan.cells) {
    const w = c.u1 - c.u0, h = c.v1 - c.v0;
    if (![c.u0, c.u1, c.v0, c.v1].every(Number.isInteger)) { out.push(`${c.id}: not integer`); continue; }
    if (!onGrid(c.u0, false) || !onGrid(c.u1, false) || !onGrid(c.v0, true) || !onGrid(c.v1, true)) out.push(`${c.id}: off the ${GRID} grid`);
    if (w <= 0 || h <= 0) { out.push(`${c.id}: empty`); continue; }
    if (c.u0 < 0 || c.v0 < 0 || c.u1 > L || c.v1 > D) out.push(`${c.id}: outside ${L} x ${D}`);
    area += w * h;
    const need = (what: string, x: number, lo: number, hi: number): void => { if (!inRange(x, lo, hi)) out.push(`${c.id} ${c.kind}: ${what} ${x} outside ${lo}-${hi}`); };
    switch (c.kind) {
      case 'room': need('u', w, C.room_cm.min, C.room_cm.max); need('v', h, C.room_cm.min, C.room_cm.max); break;
      case 'end': need('u', w, C.end_bay_cm.min, C.end_bay_cm.max); need('v', h, C.room_cm.min, D); break; // a suite over the whole depth
      case 'core':
        if (w !== COREBAY) out.push(`${c.id} core: length ${w} is not ${COREBAY}`);
        if (c.v0 !== 0 || (h !== C.core.depth_two_lane_cm && h !== C.core.depth_one_lane_cm)) out.push(`${c.id} core: depth ${h} from ${c.v0}`);
        break;
      case 'service': need('depth', h, C.service_room.depth_min_cm, C.service_room.depth_max_cm); need('width', w, C.service_room.width_min_cm, C.service_room.width_max_cm); break;
      case 'corridor': {
        need('width', h, C.corridor_cm, C.core.corridor_max_cm); need('length', w, C.corridor_cm, L);
        const wide = h > C.corridor_cm;
        if (wide && !plan.cells.some((o) => o.kind === 'core' && o.u0 === c.u0 && o.u1 === c.u1 && o.v1 === c.v0)) out.push(`${c.id} corridor: ${h} wide but not beside a core`);
        break;
      }
    }
  }
  for (let i = 0; i < plan.cells.length; ++i) {
    for (let j = i + 1; j < plan.cells.length; ++j) {
      const a = plan.cells[i], b = plan.cells[j];
      if (a.u0 < b.u1 && b.u0 < a.u1 && a.v0 < b.v1 && b.v0 < a.v1) out.push(`${a.id} overlaps ${b.id}`);
    }
  }
  if (area !== L * D) out.push(`cells cover ${area} of ${L * D} cm2 (gap)`);
  // Bays: contiguous along u, sizes per kind.
  let u = 0;
  for (const b of plan.bays) {
    if (b.u0 !== u) out.push(`bay gap at u=${u}`);
    u = b.u1;
    const w = b.u1 - b.u0;
    if (b.kind === 'core' ? w !== COREBAY : b.kind === 'end' ? !inRange(w, C.end_bay_cm.min, C.end_bay_cm.max) : !inRange(w, C.room_cm.min, C.room_cm.max)) out.push(`bay ${b.kind} ${b.u0}..${b.u1}: width ${w}`);
  }
  if (u !== L) out.push(`bays end at ${u}, not ${L}`);
  if (plan.bays.filter((b) => b.kind === 'end' || b.kind === 'exit').length > 1) out.push('more than one end / exit bay');
  return out;
}

const LETTER: Record<CellKind, string> = { room: 'R', corridor: 'C', core: 'K', service: 'S', end: 'E' };

/**
 * One char per 100 cm, L/100 wide, D/100 tall, v = D on the top row. Every cell paints its letter, its left column '|', its bottom row '-'
 * and the corner '+' (the right and top building walls are not drawn). The 350 core edge is drawn at the nearest row (3.5 -> 4).
 */
export function planToAscii(plan: BandPlan): string {
  if (plan.kind === 'fallback') return `fallback: ${plan.fallback}`;
  const W = plan.L / GRID, H = plan.D / GRID;
  const g: string[][] = Array.from({ length: H }, () => new Array<string>(W).fill(' '));
  const row = (v: number): number => Math.round(v / GRID);
  for (const c of plan.cells) {
    const x0 = c.u0 / GRID, x1 = c.u1 / GRID, y0 = row(c.v0), y1 = row(c.v1);
    for (let y = y0; y < y1; ++y) {
      for (let x = x0; x < x1; ++x) g[H - 1 - y][x] = y === y0 ? (x === x0 ? '+' : '-') : x === x0 ? '|' : LETTER[c.kind];
    }
  }
  return g.map((l) => l.join('')).join('\n');
}
