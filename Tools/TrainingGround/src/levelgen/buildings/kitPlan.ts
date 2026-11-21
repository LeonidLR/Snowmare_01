// Training Ground / level generator v3 - KIT BLOCK PLAN (docs/design/levelgen/BGEN_KIT_PLANS_2026-11-21.md, S2 kit pattern step 1): the
// residential block cut like the bought Soviet panel-block pack (KIT_PILOT_2026-11-20.md, Saved/KitDump): sections of three flats round a
// half-landing switchback core on the court side, each flat a windowless hall + bath + kitchen + 1-3 living rooms. PURE: integer cm, no draft /
// layout / engine types; generate.ts addPlannedBlock wires it, planDoors.ts realises its door list, planUnits.ts its flats.
// Frame as bandPlan.ts: u along the long side 0..L, v across 0..D, v = 0 = the CORE (court) wall. Section = [A | T | K | C] along u (or mirrored):
// K = the stair core (core.width_cm x core.depth_cm, on the court wall), T = the tambour (a common corridor, tambour_cm x core depth), A / C =
// full-depth flats, B = the street flat over T + K. A section too shallow for B is [A | K | C]. Numbers: the template entry of building_plans.json recipes.block.templates (default block_khrushchevka, series 1-464).
// Determinism: rng(fnv1a(`${seed}|kit|${k}`)) per attempt; nothing is drawn from a caller's stream.
import cfg from '../../../../../Content/Data/LevelGen/building_plans.json' with { type: 'json' };
import type { BandPlan, PlanCell, CellKind, CellBand } from './bandPlan.ts';
import type { OpeningRole } from './planDoors.ts';
import { rng } from '../../movement/geometry.ts';
import { fnv1a } from '../props/rng.ts';
import type { DoorState } from '../schema.ts';
import { STAIR_RUN_PER_RISE } from '../schema.ts';

/** The residential block templates (building_plans.json recipes.block.templates): id -> entry; `default` names the default one. */
const TEMPLATES = cfg.recipes.block.templates as unknown as Record<string, unknown> & { default: string };
export type KitTemplate = typeof cfg.recipes.block.templates.block_khrushchevka;
/** The default block template id (building_plans.json recipes.block.templates.default). */
export const DEFAULT_BLOCK_TEMPLATE: string = TEMPLATES.default;
export interface BlockTemplateInfo { id: string; planner: string; series: string | null; era: string }
const isTemplate = (k: string): boolean => k !== 'default' && k !== 'comment' && typeof TEMPLATES[k] === 'object' && TEMPLATES[k] !== null;
/** A template entry by id ({ id, planner, series, era }), the default for an unknown / absent id. */
export function blockTemplate(id?: string): BlockTemplateInfo {
  const key = id && isTemplate(id) ? id : DEFAULT_BLOCK_TEMPLATE;
  const t = TEMPLATES[key] as { planner: string; series: string | null; era: string };
  return { id: key, planner: t.planner, series: t.series, era: t.era };
}
/** The template of a series (GenBuilding.series, e.g. '1-464'), or undefined. */
export function templateForSeries(series?: string): string | undefined {
  return series ? Object.keys(TEMPLATES).find((k) => isTemplate(k) && (TEMPLATES[k] as { series?: string | null }).series === series) : undefined;
}
/** The khrushchevka template (series 1-464): the kit planner's default numbers. */
export const KIT: KitTemplate = cfg.recipes.block.templates.block_khrushchevka;
const BUILDING_PLANS = cfg; // the JSON itself, not bandPlan.ts's re-export: bandPlan.ts imports this module (no cycle at load time)
/** The kit planner's numbers of template `id` (a non-kit template / absent id: the khrushchevka's). */
export const kitTemplate = (id?: string): KitTemplate => {
  const t = blockTemplate(id);
  return (t.planner === 'kit_sections' ? TEMPLATES[t.id] : KIT) as KitTemplate;
};
// The numbers of the template the current call plans with (set by every public entry point; plain single-threaded code).
let TT: KitTemplate = KIT;
let G = KIT.grid_cm;
let K = KIT.core;
let FF = KIT.full_flat;
let SF = KIT.street_flat;
let STOREY_CM = KIT.storey_cm;
/** Plan / validate with the numbers of template `id` (the default for an absent / unknown id). */
function useTemplate(id?: string): void {
  TT = kitTemplate(id); G = TT.grid_cm; K = TT.core; FF = TT.full_flat; SF = TT.street_flat; STOREY_CM = TT.storey_cm;
}

/** What a room of a kit flat is for (GenRoom.use). The core cell is the 'landing' (it holds the floor landing and the flights). */
export type RoomUse = 'hall' | 'corridor' | 'bath' | 'kitchen' | 'living' | 'tambour' | 'landing';
export type KitFlat = 'A' | 'B' | 'C';

/** One door the kit plan wants: the two cells it joins, its role and (optionally) the preferred centre along the shared wall. */
export interface KitDoor { a: string; b: string; role: OpeningRole; at?: number }

/** Which gables get the gable door (template gable_door.ends): 'low' = the end wall at u = 0, 'high' = at u = L. */
export type GableChoice = 'low' | 'high' | 'both' | 'none';
export const GABLE_CHOICES: readonly GableChoice[] = ['low', 'high', 'both', 'none'];

/**
 * A gable door of a kit block (designer decision 2026-11-22; template gable_door): an exterior door on the end wall `wall`, ground storey only,
 * into the end flat's court row (`cells` = its vestibule_uses cells, a common vestibule on the ground storey) that leads to the section's tambour
 * or floor landing through the flat's own entry door. `door` = the vestibule cell holding the door, `c` = the door centre along the gable (v, cm).
 */
export interface KitGable { wall: 'u0' | 'uL'; section: number; unit: string; cells: string[]; door: string; c: number; /** The door's opening state (absent = the default). */ state?: DoorState }

/** One section of a kit block. */
export interface KitSection {
  index: number;
  u0: number;
  u1: number;
  /** The flats it holds ('B' only in a three-flat section). */
  flats: KitFlat[];
  /** [A T K C] read from high u to low u. */
  mirrored: boolean;
  core: { u0: number; width: number; depth: number };
  tambour?: { u0: number; u1: number };
  /** Why the section has fewer flats or rooms than the kit's 3 flats / 2-1-3 living rooms. */
  notes: string[];
}

/** The switchback geometry of a kit core, plan cm (section 2 of the doc): lanes, the landing / mid-landing edges, the run of one half flight. */
export interface KitStairGeom {
  /** Lane centres along u: lane 0 (flight a, up from the floor landing) and lane 1 (flight b, down onto the next landing). */
  lane: [number, number];
  /** The floor landing's inner edge (= the foot of flight a and the top of flight b), v. */
  vLanding: number;
  /** The mid landing's edge (= the top of flight a and the foot of flight b), v. */
  vMid: number;
  /** The far face of the mid landing (the core's inner partition face), v. */
  vFar: number;
  /** The core's inner faces along u (the side partitions). */
  uIn: [number, number];
  /** The lane wall's centre along u. */
  uLaneWall: number;
  run: number;
}

/** The kit core's stair geometry for a core cell starting at `u0` (null when the recipe's numbers do not fit the core depth). */
export function kitStairGeom(u0: number, template?: string): KitStairGeom | null {
  if (template !== undefined) useTemplate(template);
  const run = Math.ceil((STOREY_CM / 2) * STAIR_RUN_PER_RISE);
  const vLanding = K.wall_inner_cm + K.landing_cm, vMid = vLanding + run, vFar = K.depth_cm - K.partition_half_cm;
  if (vFar - vMid < K.mid_landing_cm) return null;
  const lo = u0 + K.partition_half_cm, hi = u0 + K.width_cm - K.partition_half_cm;
  const margin = (hi - lo - 2 * K.flight_w_cm - K.lane_gap_cm) / 2; // 5 cm each side at the defaults
  if (margin < 0) return null;
  const l0 = lo + margin + K.flight_w_cm / 2, l1 = hi - margin - K.flight_w_cm / 2;
  return { lane: [l0, l1], vLanding, vMid, vFar, uIn: [lo, hi], uLaneWall: (l0 + l1) / 2, run };
}

export interface KitPlanInput {
  /** Long side and depth, cm, multiples of 100 (snapFootprint). */
  L: number;
  D: number;
  seed: number;
  /** The block template id (building_plans.json recipes.block.templates; absent = templates.default). */
  template?: string;
  /** Which gables get a gable door (absent = the template's gable_door.ends / both_min_sections). */
  gable?: GableChoice;
  /** The gable door's opening state (absent = the template's gable_door.state; null there = the default). */
  gableState?: DoorState;
}

type Rand = () => number;
type RawCell = Omit<PlanCell, 'id'> & { key: string };
const pick = <T>(r: Rand, xs: readonly T[]): T => xs[Math.floor(r() * xs.length)];
const steps = (lo: number, hi: number, step = G): number[] => { const o: number[] = []; for (let x = Math.ceil(lo / step) * step; x <= hi; x += step) o.push(x); return o; };
const snapG = (x: number): number => Math.round(x / 50) * 50;

/** The section widths along u: n equal-ish sections of [min, max] on the kit grid; a string = infeasible. */
function sectionWidths(L: number, smin: number, smax: number, r: Rand): number[] | string {
  const ns: number[] = [];
  for (let n = 1; n * smin <= L; ++n) if (L <= n * smax) ns.push(n);
  if (!ns.length) return `kit_no_section_for_${L}`;
  const n = ns[0]; // the fewest sections: the kit's 12-15 m entrances are long, ours are 14.5-22.5 m
  const w = new Array<number>(n).fill(Math.floor(L / n / G) * G);
  for (let left = (L - w.reduce((a, b) => a + b, 0)) / G; left > 0; --left) w[pick(r, w.map((_, i) => i).filter((i) => w[i] < smax))] += G;
  return w;
}

/** Local -> plan mapper of one flat: x from its entry wall (into the flat), y = v (0 = court). */
interface FlatFrame { u: (x: number) => number }
const frameFrom = (entryU: number, dir: 1 | -1): FlatFrame => ({ u: (x) => entryU + dir * x });

interface Builder { cells: RawCell[]; doors: KitDoor[]; notes: string[] }

function add(bld: Builder, key: string, kind: CellKind, band: CellBand, f: FlatFrame, x0: number, x1: number, v0: number, v1: number,
  tag: Pick<PlanCell, 'section' | 'unit' | 'use'>): string {
  const a = f.u(x0), b = f.u(x1);
  bld.cells.push({ key, kind, band, u0: Math.min(a, b), u1: Math.max(a, b), v0, v1, ...tag });
  return key;
}

/**
 * A full-depth flat (A or C, and both flats of a two-flat section): a spine along its entry wall (hall at the court end + the flat's corridor,
 * an open seam between them), right of it kitchen (court window) / bath / living L1, and the street living room(s) over the whole width.
 */
function fullFlat(bld: Builder, sec: number, flat: KitFlat, f: FlatFrame, W: number, D: number, livingWanted: number, entryFrom: string, entryAt: number, r: Rand): { cor: string; street: string; ys: number } {
  const unit = `s${sec}${flat}`, sp = FF.spine_cm, wr = W - sp;
  const kd = FF.kitchen_depth_cm, bd = FF.bath_depth_cm, hd = FF.hall_depth_cm, lmin = FF.living_min_cm;
  const tag = (use: PlanCell['use']): Pick<PlanCell, 'section' | 'unit' | 'use'> => ({ section: sec, unit, use });
  const k = (n: string): string => `${unit}_${n}`;
  const base = kd + bd; // the kitchen + bath rows right of the spine
  const lsMin = FF.street_living_cm.min, lsMax = FF.street_living_cm.max;
  let l1 = 0, ls = D - base;
  if (D - base >= lmin + lsMin) {
    const opts = steps(lsMin, Math.min(lsMax, D - base - lmin));
    ls = pick(r, opts);
    l1 = D - base - ls;
  }
  const ys = D - ls;
  const hall = add(bld, k('hall'), 'corridor', 'full', f, 0, sp, 0, hd, tag('hall'));
  const cor = add(bld, k('corridor'), 'corridor', 'full', f, 0, sp, hd, ys, tag('corridor'));
  const kit = add(bld, k('kitchen'), 'room', 'back', f, sp, W, 0, kd, tag('kitchen'));
  const bath = add(bld, k('bath'), 'service', 'back', f, sp, W, kd, base, tag('bath'));
  bld.doors.push({ a: entryFrom, b: hall, role: 'unit_entry', at: entryAt }, { a: hall, b: kit, role: 'room' }, { a: cor, b: bath, role: 'room' });
  let living = 0, street = '';
  let L1: string | null = null;
  if (l1) { L1 = add(bld, k('living1'), 'room', 'full', f, sp, W, base, ys, tag('living')); living++; bld.doors.push({ a: cor, b: L1, role: 'room' }); }
  const wantSplit = livingWanted - living >= 2 && W >= 2 * lmin;
  if (livingWanted - living >= 2 && !wantSplit) bld.notes.push(`${unit}: ${W} cm too narrow to split the street room (${living + 1} living rooms instead of ${livingWanted})`);
  if (wantSplit) {
    const w1 = pick(r, steps(Math.max(lmin, sp), W - lmin));
    const s1 = add(bld, k('living2'), 'room', 'front', f, 0, w1, ys, D, tag('living'));
    const s2 = add(bld, k('living3'), 'room', 'front', f, w1, W, ys, D, tag('living'));
    bld.doors.push({ a: cor, b: s1, role: 'room' }, { a: s1, b: s2, role: 'enfilade' });
    if (L1) bld.doors.push({ a: L1, b: s2, role: 'enfilade' });
    living += 2;
    street = s1;
  } else {
    const s1 = add(bld, k('living2'), 'room', 'front', f, 0, W, ys, D, tag('living'));
    bld.doors.push({ a: cor, b: s1, role: 'room' });
    if (L1) bld.doors.push({ a: L1, b: s1, role: 'enfilade' });
    living++;
    street = s1;
  }
  if (living < livingWanted && !(livingWanted - (l1 ? 1 : 0) >= 2 && !wantSplit)) bld.notes.push(`${unit}: depth ${D} leaves ${living} living rooms instead of ${livingWanted}`);
  void wr;
  return { cor, street, ys };
}

/** The street flat B over T + K: a wide hall at the tambour's end, the bath beside it, kitchen + living over the street band. */
function streetFlat(bld: Builder, sec: number, f: FlatFrame, W: number, v0: number, D: number, entryFrom: string, entryAt: number): void {
  const unit = `s${sec}B`, hw = SF.hall_width_cm, hd = SF.hall_depth_cm, kw = SF.kitchen_width_cm;
  const tag = (use: PlanCell['use']): Pick<PlanCell, 'section' | 'unit' | 'use'> => ({ section: sec, unit, use });
  const k = (n: string): string => `${unit}_${n}`;
  const hall = add(bld, k('hall'), 'corridor', 'back', f, 0, hw, v0, v0 + hd, tag('hall'));
  const bath = add(bld, k('bath'), 'service', 'back', f, hw, W, v0, v0 + hd, tag('bath'));
  // a deep street band: the kitchen keeps kitchen_max_depth_cm at the street and the hall runs on to it as the flat's corridor (an open seam)
  const cut = D - v0 - hd - SF.kitchen_max_depth_cm >= 200 ? D - SF.kitchen_max_depth_cm : v0 + hd;
  const kit = add(bld, k('kitchen'), 'room', 'front', f, 0, kw, cut, D, tag('kitchen'));
  if (cut > v0 + hd) add(bld, k('corridor'), 'corridor', 'back', f, 0, kw, v0 + hd, cut, tag('corridor'));
  const liv = add(bld, k('living1'), 'room', 'front', f, kw, W, v0 + hd, D, tag('living'));
  bld.doors.push({ a: entryFrom, b: hall, role: 'unit_entry', at: entryAt }, { a: hall, b: bath, role: 'room' },
    { a: cut > v0 + hd ? k('corridor') : hall, b: kit, role: 'room' }, { a: hall, b: liv, role: 'room' }, { a: kit, b: liv, role: 'enfilade' });
}

/** One section's cells and doors. */
function buildSection(bld: Builder, index: number, s0: number, S: number, D: number, three: boolean, r: Rand): KitSection {
  const mirrored = r() < 0.5;
  const Wk = K.width_cm, T = three ? TT.tambour_cm : 0;
  const rest = S - Wk - T; // W_A + W_C
  const was = steps(Math.max(TT.full_flat_cm.min, rest - TT.full_flat_cm.max), Math.min(TT.full_flat_cm.max, rest - TT.full_flat_cm.min));
  // flat C (3 living rooms) wants the width to split its street room: prefer an A width that leaves C >= 2 living rooms wide
  const roomy = was.filter((w) => rest - w >= 2 * FF.living_min_cm);
  const wa = pick(r, roomy.length ? roomy : was);
  const wc = rest - wa;
  // positions along u in reading order (A side first), mirrored = read from s1 down
  const at = (x: number): number => (mirrored ? s0 + S - x : s0 + x);
  const span = (x0: number, x1: number): [number, number] => { const a = at(x0), b = at(x1); return [Math.min(a, b), Math.max(a, b)]; };
  const dir: 1 | -1 = mirrored ? -1 : 1;
  const [ku0] = span(wa + T, wa + T + Wk);
  const notes: string[] = [];
  const bld0 = bld.notes.length;
  const sec: KitSection = { index, u0: s0, u1: s0 + S, flats: three ? ['A', 'B', 'C'] : ['A', 'C'], mirrored, core: { u0: ku0, width: Wk, depth: K.depth_cm }, notes };
  const id = (n: string): string => `s${index}_${n}`;
  // the core cell (floor landing + flights + mid landing) and the tambour
  const core = id('core');
  bld.cells.push({ key: core, kind: 'core', band: 'back', u0: ku0, u1: ku0 + Wk, v0: 0, v1: K.depth_cm, section: index, use: 'landing' });
  const landingDoorV = snapG(K.wall_inner_cm + K.landing_cm / 2) - (snapG(K.wall_inner_cm + K.landing_cm / 2) + 75 > K.wall_inner_cm + K.landing_cm ? 50 : 0);
  // flat A: entered from the tambour (three flats) or the landing (two flats) on its wall shared with T / K
  let aFrom = core, aAt = landingDoorV;
  if (three) {
    const [tu0, tu1] = span(wa, wa + T);
    const tam = id('tambour');
    bld.cells.push({ key: tam, kind: 'corridor', band: 'back', u0: tu0, u1: tu1, v0: 0, v1: K.depth_cm, section: index, use: 'tambour' });
    sec.tambour = { u0: tu0, u1: tu1 };
    bld.doors.push({ a: core, b: tam, role: 'room', at: landingDoorV });
    aFrom = tam;
    aAt = snapG(FF.hall_depth_cm / 2) + (snapG(FF.hall_depth_cm / 2) === landingDoorV ? 50 : 0);
    // flat B over T + K: its entry from the tambour's inner end, x from the tambour's outer edge (the A side) towards the core
    streetFlat(bld, index, frameFrom(at(wa), dir), T + Wk, K.depth_cm, D, tam, at(wa) + dir * T / 2);
  }
  fullFlat(bld, index, 'A', frameFrom(at(wa), (-dir) as 1 | -1), wa, D, TT.living_rooms.A, aFrom, aAt, r);
  const fC = frameFrom(at(wa + T + Wk), dir);
  const c = fullFlat(bld, index, 'C', fC, wc, D, three ? TT.living_rooms.C : TT.living_rooms.C - 1, core, landingDoorV, r);
  if (!three) {
    // two flats: the street band over the core (no flat B) is one more living room of C, entered from C's corridor (or its street room)
    const over = add(bld, `s${index}C_living0`, 'room', 'front', fC, -Wk, 0, K.depth_cm, D, { section: index, unit: `s${index}C`, use: 'living' });
    bld.doors.push({ a: c.ys - K.depth_cm >= 200 ? c.cor : c.street, b: over, role: 'room' });
  }
  notes.push(...bld.notes.slice(bld0));
  return sec;
}

/** The gable walls of a block of `sections` sections: the caller's choice, else the template's ends (both from both_min_sections sections). */
function gableWalls(choice: GableChoice | undefined, sections: number): ('u0' | 'uL')[] {
  const gd = TT.gable_door;
  let ends: GableChoice = choice ?? (gd.ends as GableChoice);
  if (choice === undefined && ends !== 'none' && gd.both_min_sections && sections >= gd.both_min_sections) ends = 'both';
  return ends === 'low' ? ['u0'] : ends === 'high' ? ['uL'] : ends === 'both' ? ['u0', 'uL'] : [];
}

type RawGable = Omit<KitGable, 'cells' | 'door'> & { keys: string[]; doorKey: string };

/**
 * The gable vestibule on `wall` (cell keys, before the ids): the end section's flat on that gable, its vestibule_uses cells (its court row: hall +
 * kitchen, and the flat's corridor that opens onto the hall), the door in the cell on the gable at the court corner. A string = why there is none.
 */
function gableVestibule(cells: readonly RawCell[], sections: readonly KitSection[], wall: 'u0' | 'uL', L: number): RawGable | string {
  const sec = wall === 'u0' ? sections[0] : sections[sections.length - 1];
  const touches = (c: RawCell): boolean => (wall === 'u0' ? c.u0 === 0 : c.u1 === L);
  const end = cells.find((c) => c.section === sec.index && c.unit && touches(c));
  if (!end?.unit) return `gable door (${wall}): no flat on the gable`;
  const unit = end.unit;
  const uses = TT.gable_door.vestibule_uses as string[];
  const ves = cells.filter((c) => c.unit === unit && c.use !== undefined && uses.includes(c.use));
  if (!ves.some((c) => c.use === 'hall')) return `gable door (${wall}): ${unit} has no hall among ${uses.join(' / ')}`;
  const door = ves.find((c) => touches(c) && c.v0 === 0);
  if (!door) return `gable door (${wall}): no ${uses.join(' / ')} cell of ${unit} on the gable at the court corner`;
  const half = BUILDING_PLANS.constants.openings.door_cm / 2;
  const opts = steps(door.v0 + half, door.v1 - half);
  if (!opts.length) return `gable door (${wall}): ${unit}'s ${door.use} gable span ${door.v1 - door.v0} cm < the door`;
  const mid = (door.v0 + door.v1) / 2;
  // nearest the span's middle; a tie goes to the centre farther from the court corner (the kit entrance module clears the corner)
  let c = opts[0];
  for (const x of opts) if (Math.abs(x - mid) < Math.abs(c - mid) - 1e-9 || (Math.abs(Math.abs(x - mid) - Math.abs(c - mid)) <= 1e-9 && x > c)) c = x;
  return { wall, section: sec.index, unit, keys: ves.map((x) => x.key), doorKey: door.key, c };
}

/**
 * Plans a residential block with the kit pattern. Deterministic: same input -> same plan. `kind: 'fallback'` (with the reason) when the footprint
 * fits no kit section; the caller then tries the S2.3 sections planner, then the BSP.
 */
export function planKitBlock(inp: KitPlanInput): BandPlan {
  const { L, D } = inp;
  useTemplate(inp.template);
  const tid = blockTemplate(inp.template).id;
  const fb = (reason: string): BandPlan => ({ kind: 'fallback', fallback: reason, L, D, bays: [], cells: [], archetype: 'block', pattern: 'kit', kit: { sections: [], doors: [], notes: [], template: tid } });
  if (L % 100 || D % 100) return fb('footprint_off_grid');
  if (D > L) return fb('long_side_not_first');
  if (D < TT.min_depth_cm || D > TT.max_depth_cm) return fb(`kit_depth_${D}_outside_${TT.min_depth_cm}-${TT.max_depth_cm}`);
  if (!kitStairGeom(0)) return fb('kit_core_does_not_fit_its_stair');
  const ffMin = TT.full_flat_cm.min, ffMax = TT.full_flat_cm.max, Wk = K.width_cm;
  const threeMin = 2 * ffMin + Wk + TT.tambour_cm, threeMax = 2 * ffMax + Wk + TT.tambour_cm;
  const twoMin = 2 * ffMin + Wk, twoMax = 2 * ffMax + Wk;
  let reason = '';
  for (let k = 0; k < BUILDING_PLANS.constants.max_rerolls; ++k) {
    const r = rng(fnv1a(`${inp.seed}|kit|${k}`));
    const planNotes: string[] = [];
    let three = D >= TT.three_flat_min_depth_cm;
    if (!three) planNotes.push(`depth ${D} < ${TT.three_flat_min_depth_cm}: the street band is too shallow for flat B - two flats per landing`);
    let widths = three ? sectionWidths(L, threeMin, threeMax, r) : sectionWidths(L, twoMin, twoMax, r);
    if (typeof widths === 'string' && three) {
      const two = sectionWidths(L, twoMin, twoMax, r);
      if (typeof two !== 'string') { widths = two; three = false; planNotes.push(`length ${L} fits no three-flat section (${threeMin}-${threeMax}): two flats per landing`); }
    }
    if (typeof widths === 'string') { reason = widths; continue; }
    const bld: Builder = { cells: [], doors: [], notes: [] };
    const sections: KitSection[] = [];
    let s0 = 0;
    widths.forEach((S, i) => { sections.push(buildSection(bld, i, s0, S, D, three, r)); s0 += S; });
    // the gable doors (template gable_door) and the balcony columns (template balcony_slots.rate; own stream, the plan's draws stay as they were)
    const rawGables: RawGable[] = [];
    for (const wall of gableWalls(inp.gable, sections.length)) {
      const g = gableVestibule(bld.cells, sections, wall, L);
      if (typeof g === 'string') { planNotes.push(g); continue; }
      rawGables.push(g);
      const to = sections[g.section].tambour && !g.unit.endsWith('C') ? 'tambour' : 'floor landing';
      const uses = bld.cells.filter((c) => g.keys.includes(c.key)).map((c) => c.use).join(' + ');
      planNotes.push(`gable door (${wall}): on the ground storey ${g.unit}'s ${uses} are the gable vestibule (to the ${to}); its other rooms open onto it, no kitchen on that storey`);
    }
    const rb = rng(fnv1a(`${inp.seed}|kit|balcony`));
    const units = [...new Set(bld.cells.map((c) => c.unit).filter((u): u is string => !!u))].sort();
    const balconyColumns = units.filter(() => rb() < TT.balcony_slots.rate);
    // ids: sorted like bandPlan's finish (u0, v0, u1, v1), c<k>; the door list is rewritten from keys to ids
    const sorted = [...bld.cells].sort((a, b) => a.u0 - b.u0 || a.v0 - b.v0 || a.u1 - b.u1 || a.v1 - b.v1);
    const idOf = new Map<string, string>();
    const cells: PlanCell[] = sorted.map((c, i) => { idOf.set(c.key, `c${i}`); const { key: _k, ...rest } = c; void _k; return { id: `c${i}`, ...rest }; });
    const doors = bld.doors.map((d) => ({ ...d, a: idOf.get(d.a)!, b: idOf.get(d.b)! }));
    const gState = inp.gableState ?? ((TT.gable_door.state ?? undefined) as DoorState | undefined);
    const gables: KitGable[] = rawGables.map(({ keys, doorKey, ...g }) => ({ ...g, cells: keys.map((k) => idOf.get(k)!), door: idOf.get(doorKey)!, ...(gState ? { state: gState } : {}) }));
    const plan: BandPlan = { kind: 'bands', L, D, bays: [], cells, archetype: 'block', pattern: 'kit', kit: { sections, doors, notes: planNotes, template: tid, gables, balconyColumns } };
    const problems = validateKitPlan(plan);
    if (!problems.length) return plan;
    reason = `invalid: ${problems[0]}`;
  }
  return fb(`kit: ${reason}`);
}

const area = (c: PlanCell): number => ((c.u1 - c.u0) * (c.v1 - c.v0)) / 1e4;

/** The problems of a kit plan (empty = fine): exact tiling, the 50 grid, room areas per use, doors joining touching cells, one core per section. */
export function validateKitPlan(plan: BandPlan): string[] {
  const out: string[] = [];
  if (plan.kind !== 'bands' || !plan.kit) return ['not a kit plan'];
  useTemplate(plan.kit.template);
  const { L, D } = plan;
  let total = 0;
  for (const c of plan.cells) {
    const w = c.u1 - c.u0, h = c.v1 - c.v0;
    if (![c.u0, c.u1, c.v0, c.v1].every((x) => Number.isInteger(x) && x % G === 0)) out.push(`${c.id}: off the ${G} grid`);
    if (w <= 0 || h <= 0) { out.push(`${c.id}: empty`); continue; }
    if (c.u0 < 0 || c.v0 < 0 || c.u1 > L || c.v1 > D) out.push(`${c.id}: outside ${L} x ${D}`);
    total += w * h;
    const range = c.use ? (TT.room_area_m2 as Record<string, number[]>)[c.use] : undefined;
    if (range && (area(c) < range[0] - 1e-9 || area(c) > range[1] + 1e-9)) out.push(`${c.id} ${c.use}: ${area(c)} m2 outside ${range[0]}-${range[1]}`);
    if (c.use === 'living' && Math.min(w, h) < FF.living_min_cm) out.push(`${c.id} living: ${Math.min(w, h)} cm narrow`);
    if (c.kind === 'core' && (w !== K.width_cm || h !== K.depth_cm || c.v0 !== 0)) out.push(`${c.id} core: ${w} x ${h} from v ${c.v0}`);
  }
  for (let i = 0; i < plan.cells.length; ++i) {
    for (let j = i + 1; j < plan.cells.length; ++j) {
      const a = plan.cells[i], b = plan.cells[j];
      if (a.u0 < b.u1 && b.u0 < a.u1 && a.v0 < b.v1 && b.v0 < a.v1) out.push(`${a.id} overlaps ${b.id}`);
    }
  }
  if (total !== L * D) out.push(`cells cover ${total} of ${L * D} cm2 (gap)`);
  const byId = new Map(plan.cells.map((c) => [c.id, c]));
  for (const d of plan.kit.doors) {
    const a = byId.get(d.a), b = byId.get(d.b);
    if (!a || !b) { out.push(`door ${d.a}-${d.b}: unknown cell`); continue; }
    const lu = Math.max(a.u0, b.u0), hu = Math.min(a.u1, b.u1), lv = Math.max(a.v0, b.v0), hv = Math.min(a.v1, b.v1);
    const along = (a.v1 === b.v0 || b.v1 === a.v0) ? hu - lu : (a.u1 === b.u0 || b.u1 === a.u0) ? hv - lv : 0;
    if (along < BUILDING_PLANS.constants.openings.door_cm + 2 * K.partition_half_cm) out.push(`door ${d.a}-${d.b}: shared wall ${along} cm (< door + jambs)`);
  }
  for (const sec of plan.kit.sections) {
    if (!plan.cells.some((c) => c.kind === 'core' && c.section === sec.index)) out.push(`section ${sec.index}: no core`);
  }
  for (const g of plan.kit.gables ?? []) {
    const c = byId.get(g.door);
    const half = BUILDING_PLANS.constants.openings.door_cm / 2;
    if (!c || !g.cells.includes(g.door)) { out.push(`gable ${g.wall}: door cell ${g.door} not in its vestibule`); continue; }
    if ((g.wall === 'u0' ? c.u0 !== 0 : c.u1 !== L) || g.c - half < c.v0 || g.c + half > c.v1 || g.c % G) out.push(`gable ${g.wall}: door at v ${g.c} off ${c.id}'s gable span`);
    if (!g.cells.some((id) => byId.get(id)?.use === 'hall')) out.push(`gable ${g.wall}: the vestibule has no hall`);
  }
  return out;
}

const USE_GLYPH: Record<RoomUse, string> = { hall: 'h', corridor: 'c', bath: 'b', kitchen: 'k', living: 'L', tambour: 'T', landing: 'S' };

/**
 * One char per 50 cm (the kit grid), v = D on the top row (the street), v = 0 at the bottom (the court). Each cell paints its use glyph (h hall,
 * c corridor, b bath, k kitchen, L living, T tambour, S stair core) and its flat letter in its first interior row; walls '|' / '-' / '+'.
 */
export function kitPlanToAscii(plan: BandPlan): string {
  if (plan.kind !== 'bands') return `fallback: ${plan.fallback}`;
  useTemplate(plan.kit?.template);
  const W = plan.L / G, H = plan.D / G;
  const g: string[][] = Array.from({ length: H }, () => new Array<string>(W).fill(' '));
  for (const c of plan.cells) {
    const x0 = c.u0 / G, x1 = c.u1 / G, y0 = c.v0 / G, y1 = c.v1 / G;
    const glyph = c.use ? USE_GLYPH[c.use] : '?';
    for (let y = y0; y < y1; ++y) for (let x = x0; x < x1; ++x) g[H - 1 - y][x] = y === y0 ? (x === x0 ? '+' : '-') : x === x0 ? '|' : glyph;
    const label = c.unit ? c.unit.slice(-1) : '';
    if (label && y1 - y0 >= 2 && x1 - x0 >= 3) g[H - 1 - (y0 + 1)][x0 + 1] = label;
  }
  return g.map((l) => l.join('')).join('\n');
}
