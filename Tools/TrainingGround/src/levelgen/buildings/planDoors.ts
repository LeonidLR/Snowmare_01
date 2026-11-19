// Training Ground / level generator v3 - DOOR GRAPH of a band plan (S2_ROOMS_PLAN.md sections 3.1, 3.5 and 3.2 step 7; step S2.2 piece A):
// turns a BandPlan (bandPlan.ts) into door / portal openings in the plan frame - spine doors to the corridor (second door on a wide edge,
// facing offset), enfilade loops between neighbouring rooms, the two core portals, and the exterior doors of storey 0 (slot nearest to the
// caller's legacy draw `t`, fallback wall + warning). PURE: integer cm, no draft / layout / engine types, nothing wired into generate.ts.
// Frame as in bandPlan.ts: u along the long side 0..L, v across 0..D, v = 0 = the core wall; an opening is a point `c` on the wall line
// (`axis` 'u' = the wall runs along u at v = `at`, 'v' = along v at u = `at`). Constants: building_plans.json `constants.openings`.
// Own randomness only: rng(fnv1a(`${seed}|doors`)); the caller's stream is never touched. The shared opening types live here;
// planWindows.ts (3.6) imports them.
import type { BandPlan, PlanCell } from './bandPlan.ts';
import { BUILDING_PLANS } from './bandPlan.ts';
import { rng } from '../../movement/geometry.ts';
import { fnv1a } from '../props/rng.ts';

const C = BUILDING_PLANS.constants;
const O = C.openings;
const GRID = C.opening_grid_cm;
const HALF = O.door_cm / 2;

/** The four walls of the plan frame: the core wall (v = 0), the far wall (v = D), the near end (u = 0) and the far end (u = L). */
export type WallKey = 'v0' | 'vD' | 'u0' | 'uL';
export type OpeningRole = 'entry' | 'unit_entry' | 'room' | 'enfilade' | 'wing' | 'service' | 'portal' | 'balcony' | 'shop_front' | 'shop_window' | 'window';

/** One door / portal / window of a plan, per storey. Centres are cm on the 50 grid. */
export interface PlanOpening {
  /** Unique per plan: `f<storey>_d<k>` doors / portals, `f<storey>_w<k>` windows (k counts per storey in emission order). */
  id: string;
  storey: number;
  kind: 'door' | 'window';
  role: OpeningRole;
  /** 'wall_door_300' (the first exterior door), 'wall_door_200' (every other door), 'portal_200', 'wall_window_200'. */
  module: string;
  /** 'u' = the wall runs along u (constant v = `at`); 'v' = along v (constant u = `at`). */
  axis: 'u' | 'v';
  at: number;
  c: number;
  width: number;
  /** Two cell ids, or [cellId, 'street'] for an exterior opening. */
  links: [string, string];
  /** Exterior openings only. */
  wall?: WallKey;
}

export interface PlanDoorsOpts {
  storeys: number;
  seed: number;
  core: { u0: number; depth: number } | null;
  /** One exterior door per entry, in order; `t` in [0,1) = the caller's legacy draw fraction along that wall (0.25 + 0.5 * random()). */
  doorWalls: { wall: WallKey; t: number }[];
  /** Test hook: counts the storeys' loop decisions (plans with eligible enfilade pairs, and how many needed the forced pair). */
  stats?: { loopPlans: number; loopForced: number };
}

export interface PlanDoorsResult {
  openings: PlanOpening[];
  /** The cells holding an exterior door: the caller makes them kind 'lobby' on storey 0. */
  lobbies: string[];
  /** `exterior_door_fallback:<wall>-><wall>`, `exterior_door_none:<wall>`, `door_jamb_relaxed:<cell>`, `no_corridor_door:<cell>`, `no_door:<cell>`, `plan_fallback`. */
  warnings: string[];
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Shared geometry helpers (also used by planWindows.ts)

/** The windows' emission order of walls and the exterior-door fallback order (3.2 step 7). */
export const WALL_ORDER: WallKey[] = ['v0', 'vD', 'u0', 'uL'];
const DOOR_WALL_FALLBACK: WallKey[] = ['vD', 'v0', 'uL', 'u0'];

export const snap = (x: number): number => Math.round(x / GRID) * GRID;
export const gridUp = (x: number): number => Math.ceil(x / GRID) * GRID;
const idNum = (c: PlanCell): number => Number(c.id.slice(1));
/** Cells in their numeric id order (c2 before c10). */
export const byId = (a: PlanCell, b: PlanCell): number => idNum(a) - idNum(b);

/** Axis / constant coordinate / length of a wall of the plan frame. */
export function wallInfo(plan: BandPlan, wall: WallKey): { axis: 'u' | 'v'; at: number; length: number } {
  switch (wall) {
    case 'v0': return { axis: 'u', at: 0, length: plan.L };
    case 'vD': return { axis: 'u', at: plan.D, length: plan.L };
    case 'u0': return { axis: 'v', at: 0, length: plan.D };
    default: return { axis: 'v', at: plan.L, length: plan.D };
  }
}

/** The stretch a cell has on an exterior wall (along the wall), or null when it does not touch it. */
export function wallSpan(plan: BandPlan, c: PlanCell, wall: WallKey): [number, number] | null {
  switch (wall) {
    case 'v0': return c.v0 === 0 ? [c.u0, c.u1] : null;
    case 'vD': return c.v1 === plan.D ? [c.u0, c.u1] : null;
    case 'u0': return c.u0 === 0 ? [c.v0, c.v1] : null;
    default: return c.u1 === plan.L ? [c.v0, c.v1] : null;
  }
}

/** A wall segment two cells share: `axis` 'u' = horizontal at v = `at` over u in [lo, hi]; 'v' = vertical at u = `at` over v in [lo, hi]. */
interface Seg { a: PlanCell; b: PlanCell; axis: 'u' | 'v'; at: number; lo: number; hi: number }

function sharedSegs(plan: BandPlan): Seg[] {
  const out: Seg[] = [];
  const cs = plan.cells;
  for (let i = 0; i < cs.length; ++i) {
    for (let j = i + 1; j < cs.length; ++j) {
      const a = cs[i], b = cs[j];
      const lu = Math.max(a.u0, b.u0), hu = Math.min(a.u1, b.u1), lv = Math.max(a.v0, b.v0), hv = Math.min(a.v1, b.v1);
      if ((a.v1 === b.v0 || b.v1 === a.v0) && hu > lu) out.push({ a, b, axis: 'u', at: a.v1 === b.v0 ? a.v1 : b.v1, lo: lu, hi: hu });
      if ((a.u1 === b.u0 || b.u1 === a.u0) && hv > lv) out.push({ a, b, axis: 'v', at: a.u1 === b.u0 ? a.u1 : b.u1, lo: lv, hi: hv });
    }
  }
  return out;
}

/** True when the edge `e` of corridor cell `c` is only a seam to another corridor cell (an open boundary, no wall). */
function isSeam(plan: BandPlan, c: PlanCell, edgeAxis: 'u' | 'v', e: number): boolean {
  if (c.kind !== 'corridor') return false;
  return plan.cells.some((o) => o !== c && o.kind === 'corridor' && (edgeAxis === 'u'
    ? ((o.u1 === e && c.u0 === e) || (o.u0 === e && c.u1 === e)) && o.v0 < c.v1 && c.v0 < o.v1
    : ((o.v1 === e && c.v0 === e) || (o.v0 === e && c.v1 === e)) && o.u0 < c.u1 && c.u0 < o.u1));
}

/** The crossing wall axes of a wall line (3.5 rule 5): the edges of the cells on either side of it, minus corridor seams, plus the corners. */
function crossings(plan: BandPlan, axis: 'u' | 'v', at: number, cache: Map<string, number[]>): number[] {
  const key = `${axis}:${at}`;
  const hit = cache.get(key);
  if (hit) return hit;
  const out = new Set<number>(axis === 'u' ? [0, plan.L] : [0, plan.D]);
  for (const c of plan.cells) {
    if (!(axis === 'u' ? c.v0 === at || c.v1 === at : c.u0 === at || c.u1 === at)) continue;
    const edges = axis === 'u' ? [c.u0, c.u1] : [c.v0, c.v1];
    for (const e of edges) if (!isSeam(plan, c, axis === 'u' ? 'u' : 'v', e)) out.add(e);
  }
  const list = [...out].sort((x, y) => x - y);
  cache.set(key, list);
  return list;
}

/** The 50-grid door centres on [lo, hi] with the whole door inside and every crossing axis at least `jamb` away. */
function legalCentres(lo: number, hi: number, cross: number[], jamb: number = O.jamb_cm): number[] {
  const out: number[] = [];
  for (let c = gridUp(lo + HALF); c + HALF <= hi; c += GRID) if (cross.every((x) => Math.abs(c - x) >= jamb)) out.push(c);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Door records

type Rand = () => number;
const pick = <T>(r: Rand, xs: readonly T[]): T => xs[Math.floor(r() * xs.length)];

/** A planned interior door before it is copied per storey. `legal` = the centres it may sit on (a lone entry = fixed). */
interface Door {
  cell: PlanCell;
  other: PlanCell;
  axis: 'u' | 'v';
  at: number;
  legal: number[];
  c: number;
  /** The cell's other door on the same edge (second door): kept >= second_door_gap_cm away. */
  sib: Door | null;
  role: OpeningRole;
}

interface Ctx {
  plan: BandPlan;
  segs: Seg[];
  r: Rand;
  warnings: string[];
  cache: Map<string, number[]>;
  stats?: PlanDoorsOpts['stats'];
}

const mkDoor = (cell: PlanCell, other: PlanCell, seg: Seg, role: OpeningRole, legal: number[], c: number): Door =>
  ({ cell, other, axis: seg.axis, at: seg.at, legal, c, sib: null, role });

/** The segments `cell` shares with a cell passing `want`, the longest first (ties: the smaller other id). */
function neighbourSegs(ctx: Ctx, cell: PlanCell, want: (o: PlanCell) => boolean): { seg: Seg; other: PlanCell }[] {
  const out: { seg: Seg; other: PlanCell }[] = [];
  for (const seg of ctx.segs) {
    const other = seg.a === cell ? seg.b : seg.b === cell ? seg.a : null;
    if (other && want(other)) out.push({ seg, other });
  }
  return out.sort((x, y) => (y.seg.hi - y.seg.lo) - (x.seg.hi - x.seg.lo) || idNum(x.other) - idNum(y.other));
}

/** One door (or two on an edge >= second_door_edge_cm) on `seg`, positions drawn from the legal centres; null when none is legal. */
function doorsOnSeg(ctx: Ctx, cell: PlanCell, other: PlanCell, seg: Seg, role: OpeningRole, allowSecond: boolean): Door[] | null {
  const legal = legalCentres(seg.lo, seg.hi, crossings(ctx.plan, seg.axis, seg.at, ctx.cache));
  if (!legal.length) return null;
  if (allowSecond && seg.hi - seg.lo >= O.second_door_edge_cm) {
    const pairs: [number, number][] = [];
    for (let i = 0; i < legal.length; ++i) for (let j = i + 1; j < legal.length; ++j) if (legal[j] - legal[i] >= O.second_door_gap_cm) pairs.push([legal[i], legal[j]]);
    if (pairs.length) {
      const [c1, c2] = pick(ctx.r, pairs);
      const d1 = mkDoor(cell, other, seg, role, legal, c1), d2 = mkDoor(cell, other, seg, role, legal, c2);
      d1.sib = d2; d2.sib = d1;
      return [d1, d2];
    }
  }
  return [mkDoor(cell, other, seg, role, legal, pick(ctx.r, legal))];
}

/** A door at the middle of the segment, jambs not checked (an `end` cell: the corridor walls are its jambs; or a relaxed service cell). */
function middleDoor(seg: Seg, cell: PlanCell, other: PlanCell, role: OpeningRole): Door | null {
  const c = snap((seg.lo + seg.hi) / 2);
  return c - HALF >= seg.lo && c + HALF <= seg.hi ? mkDoor(cell, other, seg, role, [c], c) : null;
}

/** 3.5 rule 1-2: the door(s) of a non-corridor cell to the corridor it touches. */
function spineFor(ctx: Ctx, cell: PlanCell): Door[] {
  let cands = neighbourSegs(ctx, cell, (o) => o.kind === 'corridor');
  if (!cands.length) {
    ctx.warnings.push(`no_corridor_door:${cell.id}`);
    cands = neighbourSegs(ctx, cell, (o) => o.kind !== 'core' && o.kind !== 'corridor');
  }
  if (cell.kind === 'end') {
    for (const { seg, other } of cands) { const d = middleDoor(seg, cell, other, 'room'); if (d) return [d]; }
  } else {
    for (const { seg, other } of cands) { const ds = doorsOnSeg(ctx, cell, other, seg, 'room', true); if (ds) return ds; }
    // No centre keeps the 200 jamb (a 300 wide service room): the middle of the edge, with a warning.
    for (const { seg, other } of cands) {
      const d = middleDoor(seg, cell, other, 'room');
      if (d) { ctx.warnings.push(`door_jamb_relaxed:${cell.id}`); return [d]; }
    }
  }
  ctx.warnings.push(`no_door:${cell.id}`);
  return [];
}

/** 3.5 rule 3: a door facing another across the corridor moves to the nearest other legal centre that faces nothing. */
function resolveFacing(doors: Door[], corridors: PlanCell[]): void {
  const moveAway = (d: Door, opposite: Door[]): boolean => {
    const ok = d.legal.filter((c) => opposite.every((o) => Math.abs(c - o.c) >= O.facing_offset_cm) && (!d.sib || Math.abs(c - d.sib.c) >= O.second_door_gap_cm));
    if (!ok.length) return false;
    d.c = ok.reduce((best, c) => (Math.abs(c - d.c) < Math.abs(best - d.c) || (Math.abs(c - d.c) === Math.abs(best - d.c) && c < best) ? c : best), ok[0]);
    return true;
  };
  for (const q of corridors) {
    const low = doors.filter((d) => d.axis === 'u' && d.other === q && d.at === q.v0);
    const high = doors.filter((d) => d.axis === 'u' && d.other === q && d.at === q.v1);
    for (const h of high) {
      for (const l of low) {
        if (Math.abs(h.c - l.c) >= O.facing_offset_cm) continue;
        if (!moveAway(h, low)) moveAway(l, high);
      }
    }
  }
}

/** The pair of `a` / `b` in a stable order (lower id first). */
const pairKey = (s: Seg): number => idNum(s.a) * 1000 + idNum(s.b);

/** 3.5 rule 4: enfilade doors between neighbouring same-band rooms that both have corridor doors; p_enfilade each, the first forced. */
function enfiladeDoors(ctx: Ctx, cells: PlanCell[], withDoor: Set<string>): Door[] {
  if (cells.filter((c) => c.kind === 'room' || c.kind === 'end').length < 3) return [];
  const eligible = (s: Seg, withEnd: boolean): boolean => s.axis === 'v' && withDoor.has(s.a.id) && withDoor.has(s.b.id) && (
    (s.a.kind === 'room' && s.b.kind === 'room' && s.a.band === s.b.band && (s.a.band === 'back' || s.a.band === 'front'))
    || (withEnd && ((s.a.kind === 'end' && s.b.kind === 'room') || (s.a.kind === 'room' && s.b.kind === 'end'))));
  // No same-band pair (e.g. end suite + one back + one front room): the end suite joins the loop and stops being a dead end -
  // the loop rule (validity) beats the dead-end hook (story 17 picks another building).
  let pairs = ctx.segs.filter((s) => eligible(s, false)).sort((x, y) => pairKey(x) - pairKey(y));
  if (!pairs.length) pairs = ctx.segs.filter((s) => eligible(s, true)).sort((x, y) => pairKey(x) - pairKey(y));
  const drawn = pairs.filter(() => ctx.r() < O.p_enfilade);
  const order = drawn.length ? drawn : pairs; // none drawn: the first eligible pair is forced
  if (ctx.stats && pairs.length) { ++ctx.stats.loopPlans; if (!drawn.length) ++ctx.stats.loopForced; }
  const out: Door[] = [];
  for (const seg of order) {
    const ds = doorsOnSeg(ctx, seg.a, seg.b, seg, 'enfilade', false);
    if (ds) { out.push(...ds); if (!drawn.length) break; }
  }
  return out;
}

/** Corridor-less plans (3.5 rule 1): the front-row chain, every back cell to the front cell of its bay, one back-back door. */
function corridorlessDoors(ctx: Ctx, cells: PlanCell[]): Door[] {
  const out: Door[] = [];
  const between = (x: PlanCell, y: PlanCell): Seg | undefined => ctx.segs.find((s) => (s.a === x && s.b === y) || (s.a === y && s.b === x));
  const link = (x: PlanCell, y: PlanCell, role: OpeningRole): boolean => {
    const seg = between(x, y);
    const ds = seg ? doorsOnSeg(ctx, x, y, seg, role, false) : null;
    if (ds) out.push(...ds); else ctx.warnings.push(`no_door:${x.id}`);
    return !!ds;
  };
  const front = cells.filter((c) => c.band === 'front' || c.band === 'full').sort((x, y) => x.u0 - y.u0);
  const back = cells.filter((c) => c.band === 'back').sort((x, y) => x.u0 - y.u0);
  for (let i = 0; i + 1 < front.length; ++i) link(front[i], front[i + 1], 'room');
  for (const b of back) {
    const f = front.find((x) => x.u0 === b.u0 && x.u1 === b.u1);
    if (f) link(b, f, 'room');
  }
  if (cells.length >= 3 && back.length >= 2) {
    const k = Math.floor(ctx.r() * (back.length - 1));
    link(back[k], back[k + 1], 'room');
  }
  return out;
}

/**
 * 3.5 rule 5: the two pad portals on the core cell's open edge, centres u0 + 100 and u0 + 750. A caller whose pads sit off the cell's grid
 * (generate.ts: a core flush with the high end starts 41 cm into its 900 cell) passes the shifted `u0`; the centre is snapped to the 50
 * grid (the opening grid holds for portals too - planValidate off_grid) and still overlaps its pad by >= 150.
 */
function corePortals(ctx: Ctx, core: { u0: number; depth: number }): Door[] {
  const cell = ctx.plan.cells.find((c) => c.kind === 'core');
  if (!cell) return [];
  const out: Door[] = [];
  for (const off of O.portal_offsets_cm) {
    const c = snap(core.u0 + off);
    const q = ctx.plan.cells.find((o) => o.kind === 'corridor' && o.v0 === core.depth && o.u0 <= c && c <= o.u1);
    if (!q) continue;
    out.push({ cell, other: q, axis: 'u', at: core.depth, legal: [c], c, sib: null, role: 'portal' });
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Exterior doors

interface Slot { cell: PlanCell; c: number }

/** 3.2 step 7: the door slots of a wall; service cells only when nothing else is there. Cells already holding a door on this wall are skipped. */
function wallSlots(plan: BandPlan, wall: WallKey, taken: Set<string>): Slot[] {
  const main: Slot[] = [], service: Slot[] = [];
  for (const cell of plan.cells) {
    const span = wallSpan(plan, cell, wall);
    if (!span || cell.kind === 'core' || taken.has(`${wall}|${cell.id}`)) continue;
    if (cell.kind === 'corridor') {
      if (wall === 'v0' || wall === 'vD') continue; // corridors run along u: only their ends (u = 0 / L) are on a wall
      const c = snap((span[0] + span[1]) / 2); // a corridor end: jambs are the corridor walls
      if (c - HALF >= span[0] && c + HALF <= span[1]) main.push({ cell, c });
      continue;
    }
    const into = cell.kind === 'service' ? service : main;
    for (let c = gridUp(span[0] + O.jamb_cm); c <= span[1] - O.jamb_cm; c += GRID) into.push({ cell, c });
  }
  return main.length ? main : service;
}

function nearestSlot(slots: Slot[], target: number): Slot | null {
  let best: Slot | null = null;
  for (const s of slots) {
    if (!best || Math.abs(s.c - target) < Math.abs(best.c - target) || (Math.abs(s.c - target) === Math.abs(best.c - target) && s.c < best.c)) best = s;
  }
  return best;
}

// ---------------------------------------------------------------------------------------------------------------------------------

/**
 * Plans every door / portal of a bands plan on every storey. Deterministic for the same plan and opts; the same interior doors repeat on
 * each storey (ids per storey), the exterior doors exist on storey 0 only. A `fallback` plan returns nothing and the warning `plan_fallback`.
 */
export function planDoors(plan: BandPlan, opts: PlanDoorsOpts): PlanDoorsResult {
  const warnings: string[] = [];
  if (plan.kind !== 'bands') return { openings: [], lobbies: [], warnings: ['plan_fallback'] };
  const ctx: Ctx = { plan, segs: sharedSegs(plan), r: rng(fnv1a(`${opts.seed}|doors`)), warnings, cache: new Map(), stats: opts.stats };
  const cells = [...plan.cells].sort(byId);
  const interior: Door[] = [];

  if (plan.variant === 'corridorless') {
    interior.push(...corridorlessDoors(ctx, cells));
  } else {
    const spine: Door[] = [];
    for (const cell of cells) if (cell.kind === 'room' || cell.kind === 'service' || cell.kind === 'end') spine.push(...spineFor(ctx, cell));
    resolveFacing(spine, cells.filter((c) => c.kind === 'corridor'));
    interior.push(...spine);
    interior.push(...enfiladeDoors(ctx, cells, new Set(spine.map((d) => d.cell.id))));
    if (opts.core) interior.push(...corePortals(ctx, opts.core));
  }

  // Exterior doors (storey 0): the slot nearest to the target along the wall; none -> the next free wall + a warning.
  const taken = new Set<string>(), holding = new Set<WallKey>();
  const exterior: { slot: Slot; wall: WallKey }[] = [];
  for (const { wall, t } of opts.doorWalls) {
    const at = (w: WallKey): Slot | null => nearestSlot(wallSlots(plan, w, taken), wallInfo(plan, w).length * t);
    let slot = at(wall), used = wall;
    if (!slot) {
      for (const w of DOOR_WALL_FALLBACK) {
        if (w === wall || holding.has(w)) continue;
        slot = at(w);
        if (slot) { used = w; warnings.push(`exterior_door_fallback:${wall}->${w}`); break; }
      }
    }
    if (!slot) { warnings.push(`exterior_door_none:${wall}`); continue; }
    taken.add(`${used}|${slot.cell.id}`);
    holding.add(used);
    exterior.push({ slot, wall: used });
  }

  const openings: PlanOpening[] = [];
  for (let storey = 0; storey < opts.storeys; ++storey) {
    let k = 0;
    const id = (): string => `f${storey}_d${k++}`;
    for (const d of interior) {
      const portal = d.role === 'portal';
      openings.push({
        id: id(), storey, kind: 'door', role: d.role, module: portal ? 'portal_200' : 'wall_door_200', axis: d.axis, at: d.at, c: d.c,
        width: portal ? O.portal_cm : O.door_cm, links: [d.cell.id, d.other.id],
      });
    }
    if (storey > 0) continue;
    exterior.forEach(({ slot, wall }, i) => {
      const w = wallInfo(plan, wall);
      openings.push({
        id: id(), storey, kind: 'door', role: 'entry', module: i === 0 ? 'wall_door_300' : 'wall_door_200', axis: w.axis, at: w.at, c: slot.c,
        width: O.door_cm, links: [slot.cell.id, 'street'], wall,
      });
    });
  }
  const lobbies = [...new Set(exterior.filter((e) => e.slot.cell.kind !== 'corridor').map((e) => e.slot.cell.id))]
    .sort((a, b) => Number(a.slice(1)) - Number(b.slice(1))); // a corridor end stays a corridor, not a lobby
  return { openings, lobbies, warnings };
}
