// Training Ground / level generator v3 - the PLAN OVERLAY data (S2_ROOMS_PLAN.md step S2.7): what the page's «Планировки» layer draws for ONE storey of a planned
// building - rooms by unit / zone (flat, suite, office, lobby, common ...), doors and windows by role, the loops of the door graph, dead-end flats and the validator's
// issues of the building. Pure data in WORLD cm (turned with the building): the SVG overlay (departments/PlansOverlay.tsx) only paints it, and a later goal-zone layer
// can reuse `rooms[].poly` / `groups[]` (a zone = the outline of its rooms). The glyphs and colours follow the legend of docs/design/levelgen/S2_3_PLANS.md (planAscii.ts):
// K stair core, L landing / corridor, O lobby, r office room, s service, e end suite, a lower-case letter = a flat with a second entry (a loop), an upper-case letter =
// a DEAD-END flat; doors E / U / d / e / P / b, windows w, S = a balcony / fire-escape slot. Triad flags are not here: the validator has no triad rule yet (S2.2 "open").
import type { Vec2 } from '../../movement/geometry.ts';
import { dirOf } from '../../movement/geometry.ts';
import type { TraversalIssue } from '../buildingTraversal.ts';
import { effectiveOpeningState } from '../openingStates.ts';
import { buildingRectPoly } from '../rotation.ts';
import type { BuildingUnit, GenBuilding, GenLayout, GenRoom, WallOpening } from '../schema.ts';
import { flatLetter, openingGlyph } from './planAscii.ts';

/** The page's floor selector (LayoutView.tsx FloorViewMode). */
export type OverlayFloorMode = 'all' | 'ground' | '1' | '2' | 'roof';

/** How a room is filled: by what it is (flat / suite / office room / lobby / service) or by its zone (common = stair core + landing, core, corridor). */
export type RoomFill = 'flat' | 'suite' | 'office' | 'lobby' | 'service' | 'core' | 'corridor';

/** Fill colours (hex) per kind; the flats take FLAT_HUES by their column so neighbouring flats read apart. */
export const FILL_COLOUR: Record<Exclude<RoomFill, 'flat'>, string> = {
  office: '#38bdf8', suite: '#a78bfa', lobby: '#f59e0b', service: '#a8a29e', core: '#475569', corridor: '#94a3b8',
};
export const FLAT_HUES: readonly string[] = ['#4ade80', '#2dd4bf', '#a3e635', '#fb923c', '#f472b6', '#facc15', '#60a5fa', '#c084fc'];
/** Door marker colours per role (planDoors.ts roles; `window` and slots separately). */
export const DOOR_COLOUR: Record<string, string> = {
  entry: '#ffffff', unit_entry: '#f87171', room: '#fde047', enfilade: '#22d3ee', portal: '#64748b', wing: '#fb7185', service: '#fbbf24', balcony: '#f472b6',
  window: '#7dd3fc', slot: '#f9a8d4',
};
export const LOOP_COLOUR = '#34d399';
export const DEAD_END_COLOUR = '#ef4444';

/** The legend (glyph, colour, Russian name): what the page prints under the plan, in the order of S2_3_PLANS.md. */
export const PLAN_LEGEND: { glyph: string; colour: string; name: string; group: 'room' | 'door' | 'mark' }[] = [
  { glyph: 'K', colour: FILL_COLOUR.core, name: 'лестничное ядро', group: 'room' },
  { glyph: 'L', colour: FILL_COLOUR.corridor, name: 'коридор / площадка', group: 'room' },
  { glyph: 'O', colour: FILL_COLOUR.lobby, name: 'вестибюль (земля)', group: 'room' },
  { glyph: 'r', colour: FILL_COLOUR.office, name: 'комната (контора)', group: 'room' },
  { glyph: 's', colour: FILL_COLOUR.service, name: 'служебная', group: 'room' },
  { glyph: 'e', colour: FILL_COLOUR.suite, name: 'тупиковый кабинет', group: 'room' },
  { glyph: 'a', colour: FLAT_HUES[0], name: 'квартира с кольцом (строчная буква)', group: 'room' },
  { glyph: 'A', colour: DEAD_END_COLOUR, name: 'тупиковая квартира (заглавная, красная рамка)', group: 'room' },
  { glyph: 'E', colour: DOOR_COLOUR.entry, name: 'наружная дверь', group: 'door' },
  { glyph: 'U', colour: DOOR_COLOUR.unit_entry, name: 'входная дверь квартиры (закрыта)', group: 'door' },
  { glyph: 'd', colour: DOOR_COLOUR.room, name: 'дверь комнаты', group: 'door' },
  { glyph: 'e', colour: DOOR_COLOUR.enfilade, name: 'анфилада', group: 'door' },
  { glyph: 'P', colour: DOOR_COLOUR.portal, name: 'портал площадки (без створки)', group: 'door' },
  { glyph: 'S', colour: DOOR_COLOUR.slot, name: 'окно-слот балкона / пожарной лестницы', group: 'door' },
  { glyph: 'w', colour: DOOR_COLOUR.window, name: 'окно', group: 'door' },
  { glyph: 'b', colour: DOOR_COLOUR.balcony, name: 'балконная дверь (после истории 4)', group: 'door' },
  { glyph: 'o', colour: LOOP_COLOUR, name: 'кольцо: двери и комнаты на цикле', group: 'mark' },
];

export interface OverlayRoom {
  id: string;
  floor: number;
  kind: GenRoom['kind'] | 'room';
  fill: RoomFill;
  /** The unit (flat id / 'suite') and zone ('common') the room belongs to, as the building records them. */
  unit?: string;
  zone?: string;
  /** The flat column (the stack of the same flat on every storey): the colour key of a flat. */
  column?: string;
  /** The fill colour (hex). */
  colour: string;
  /** The ASCII letter of the room (planAscii.ts). */
  glyph: string;
  /** World corners (the room turned with its building) and the centre of them. */
  poly: Vec2[];
  centre: Vec2;
  /** Size in the building frame, cm: [along x, along y]; area in m2. */
  size_cm: [number, number];
  area_m2: number;
  /** A flat with a single entry. */
  dead_end: boolean;
  /** The room lies on a loop of the storey's door graph. */
  loop: boolean;
  /** Ids of the openings joining the room (doors and windows). */
  openings: string[];
}

export interface OverlayDoor {
  id: string;
  kind: 'door' | 'window';
  /** The role as planned (entry, unit_entry, room, enfilade, portal, ...; a window = 'window'). */
  role: string;
  glyph: string;
  colour: string;
  /** The end points of the opening along its wall, world cm; `pos` = the centre. */
  a: Vec2;
  b: Vec2;
  pos: Vec2;
  width: number;
  links?: [string, string];
  /** The effective state (opening-states.md): 'closed' for a flat door, 'open' for a portal / room door, 'glazed' ... */
  state: string;
  /** The opening sits on a wall of the building (exterior door / window) and not on a partition. */
  exterior: boolean;
  /** A balcony / fire-escape slot window. */
  slot?: 'balcony' | 'escape';
  /** A door on a loop of the door graph. */
  loop: boolean;
}

/** One unit or zone of the storey: its rooms' ids and polygons (the data a later goal-zone layer outlines). */
export interface OverlayGroup {
  key: string;
  kind: 'unit' | 'zone';
  id: string;
  rooms: string[];
  polys: Vec2[][];
  dead_end?: boolean;
  run_cm?: number;
  column?: string;
}

export interface OverlayIssue {
  code: string;
  severity: 'error' | 'warning';
  message: string;
  ref?: string;
  /** Where the issue is on this storey (the centre of its room / opening); absent = about the building or another storey. */
  pos?: Vec2;
}

export interface PlanOverlay {
  building: string;
  floor: number;
  /** 'bands' = a band plan; 'bsp' = the plan fell back (rooms and doors are the legacy ones, there are no roles). */
  kind: 'bands' | 'bsp';
  fallback?: string;
  rooms: OverlayRoom[];
  doors: OverlayDoor[];
  groups: OverlayGroup[];
  /** Independent loops of the storey's door graph (E - V + C) and the rooms / doors on them. */
  loops: number;
  /** The issues of the whole building (the report's), placed on this storey where their ref resolves. */
  issues: OverlayIssue[];
}

// ---- the floor selector -----------------------------------------------------------------------------------------------------------

/**
 * The storey of `b` the overlay shows for the page's floor selector, or null for none: 'all' / 'ground' = the ground storey, '1' / '2' = that storey (when the building has it),
 * 'roof' = none; a layer filter 'floor:<id>:<n>' = that storey of that building (others none), 'ground' / 'cut' = the ground storey, 'roof:<id>' / anything else = none.
 */
export function overlayFloor(b: Pick<GenBuilding, 'id' | 'floors'>, mode: OverlayFloorMode, layer = 'all'): number | null {
  if (layer !== 'all') {
    if (layer === 'ground' || layer === 'cut') return 0;
    if (layer.startsWith('floor:')) { const [, id, n] = layer.split(':'); return id === b.id && Number(n) < b.floors ? Number(n) : null; }
    return null;
  }
  const f = mode === 'all' || mode === 'ground' ? 0 : mode === '1' ? 1 : mode === '2' ? 2 : null;
  return f !== null && f < b.floors ? f : null;
}

// ---- the door graph (loops) -------------------------------------------------------------------------------------------------------

/**
 * The cycles of an undirected multigraph: which edges are NOT bridges (removing a non-bridge keeps its ends connected) and the cyclomatic number E - V + C.
 * Small graphs (a storey has tens of rooms), so the plain "remove, search" test.
 */
export function graphLoops(nodes: readonly string[], edges: readonly (readonly [string, string])[]): { inLoop: boolean[]; cycles: number } {
  const adj = (skip: number): Map<string, string[]> => {
    const m = new Map<string, string[]>(nodes.map((n) => [n, []]));
    edges.forEach(([a, c], i) => { if (i !== skip && m.has(a) && m.has(c)) { m.get(a)!.push(c); m.get(c)!.push(a); } });
    return m;
  };
  const reach = (m: Map<string, string[]>, from: string, to: string): boolean => {
    const seen = new Set([from]), stack = [from];
    while (stack.length) { const n = stack.pop()!; if (n === to) return true; for (const k of m.get(n) ?? []) if (!seen.has(k)) { seen.add(k); stack.push(k); } }
    return false;
  };
  const inLoop = edges.map(([a, c], i) => a === c || reach(adj(i), a, c));
  const full = adj(-1), seen = new Set<string>();
  let comps = 0;
  for (const n of nodes) {
    if (seen.has(n)) continue;
    comps++;
    const stack = [n]; seen.add(n);
    while (stack.length) for (const k of full.get(stack.pop()!) ?? []) if (!seen.has(k)) { seen.add(k); stack.push(k); }
  }
  const live = edges.filter(([a, c]) => full.has(a) && full.has(c)).length;
  return { inLoop, cycles: Math.max(0, live - nodes.length + comps) };
}

// ---- the builder ------------------------------------------------------------------------------------------------------------------

const round = (v: number): number => Math.round(v * 100) / 100;
const centroid = (pts: readonly Vec2[]): Vec2 => [round(pts.reduce((s, p) => s + p[0], 0) / pts.length), round(pts.reduce((s, p) => s + p[1], 0) / pts.length)];

function fillOf(r: GenRoom, unit?: BuildingUnit): RoomFill {
  if (r.kind === 'stairwell') return 'core';
  if (r.kind === 'corridor') return 'corridor';
  if (r.kind === 'lobby') return 'lobby';
  if (r.kind === 'service') return 'service';
  if (unit) return 'flat';
  return r.unit === 'suite' ? 'suite' : 'office';
}

/**
 * The overlay of storey `floor` of building `b`, or null when it carries no plan or has no such storey. `issues` = the validator's issues of the building
 * (the page's report entry); they are placed by `ref` (a room id or an opening id), the rest stays in `issues` without a position.
 */
export function planOverlayFor(layout: Pick<GenLayout, 'schema' | 'walls'>, b: GenBuilding, floor: number, issues: readonly TraversalIssue[] = []): PlanOverlay | null {
  if (!b.plan || !b.rooms?.length || floor < 0 || floor >= b.floors) return null;
  const units = new Map<string, BuildingUnit>((b.units ?? []).map((u) => [u.id, u]));
  const here = [...b.rooms].filter((r) => r.floor === floor).sort((p, q) => (p.id < q.id ? -1 : 1));
  const ids = new Set(here.map((r) => r.id));

  // the storey's door graph: rooms but the stair core; interior doors; corridor cells joined by an open seam
  const opens: { op: WallOpening; interior: boolean; a?: Vec2; b?: Vec2 }[] = [];
  for (const op of b.openings ?? []) if (op.floor === floor && op.kind !== 'loophole') opens.push({ op, interior: false });
  for (const pt of b.partitions ?? []) if (pt.floor === floor) for (const op of pt.openings ?? []) if (op.kind !== 'loophole') opens.push({ op, interior: true, a: pt.start, b: pt.end });
  const graphIds = new Set(here.filter((r) => r.kind !== 'stairwell').map((r) => r.id));
  const edgeOpen: (WallOpening | null)[] = [], edges: [string, string][] = [];
  for (const { op, interior } of opens) {
    if (!interior || op.kind !== 'door' || !op.links || !graphIds.has(op.links[0]) || !graphIds.has(op.links[1])) continue;
    edges.push([op.links[0], op.links[1]]); edgeOpen.push(op);
  }
  const seams = new Map<string, string[]>();
  for (const r of here) for (const d of r.door_ids ?? []) if (d.includes('_seam_')) seams.set(d, [...(seams.get(d) ?? []), r.id]);
  for (const grp of seams.values()) for (let i = 1; i < grp.length; i++) { edges.push([grp[0], grp[i]]); edgeOpen.push(null); }
  // a loop is a cycle through >= 3 rooms: two doors between the same two rooms (a second corridor door of a wide room) are one edge here
  const pairKey = ([x, y]: readonly [string, string]): string => (x < y ? `${x}|${y}` : `${y}|${x}`);
  const pairs = [...new Map(edges.map((e) => [pairKey(e), e] as const)).values()];
  const loopInfo = graphLoops([...graphIds], pairs);
  const loopPairs = new Set(pairs.filter((_, i) => loopInfo.inLoop[i]).map(pairKey));
  const loopDoors = new Set<string>(), loopRooms = new Set<string>();
  edges.forEach((e, i) => { if (!loopPairs.has(pairKey(e))) return; loopRooms.add(e[0]); loopRooms.add(e[1]); const id = edgeOpen[i]?.id; if (id) loopDoors.add(id); });

  // letters of the flats, in the order planAscii.ts gives them (by room id, first room of a column)
  const letters = new Map<string, string>();
  const rooms: OverlayRoom[] = here.map((r) => {
    const unit = r.unit ? units.get(r.unit) : undefined;
    const fill = fillOf(r, unit);
    let glyph = fill === 'core' ? 'K' : fill === 'corridor' ? 'L' : fill === 'lobby' ? 'O' : fill === 'service' ? 's' : fill === 'suite' ? 'e' : 'r';
    let column: string | undefined;
    if (unit) {
      column = unit.column;
      if (!letters.has(unit.column)) letters.set(unit.column, String(letters.size));
      glyph = flatLetter(Number(letters.get(unit.column)), !!unit.dead_end);
    }
    const poly = buildingRectPoly(b, [r.bounds.min, r.bounds.max]);
    const w = r.bounds.max[0] - r.bounds.min[0], h = r.bounds.max[1] - r.bounds.min[1];
    const colour = fill === 'flat' ? FLAT_HUES[(column ? [...letters.keys()].indexOf(column) : 0) % FLAT_HUES.length] : FILL_COLOUR[fill];
    return {
      id: r.id, floor, kind: r.kind ?? 'room', fill, ...(r.unit ? { unit: r.unit } : {}), ...(r.zone ? { zone: r.zone } : {}), ...(column ? { column } : {}), colour, glyph,
      poly, centre: centroid(poly), size_cm: [round(w), round(h)], area_m2: Math.round((w * h) / 100) / 100, dead_end: !!unit?.dead_end, loop: loopRooms.has(r.id),
      openings: [...(r.door_ids ?? [])].filter((d) => !d.includes('_seam_')),
    };
  });

  // doors and windows with their end points
  const doors: OverlayDoor[] = opens.map(({ op, interior, a, b: e }) => {
    const width = op.width ?? 150;
    const along: Vec2 = interior && a && e
      ? (() => { const L = Math.hypot(e[0] - a[0], e[1] - a[1]) || 1; return [(e[0] - a[0]) / L, (e[1] - a[1]) / L] as Vec2; })()
      : (() => { const n = dirOf(op.yaw ?? 0); return [-n[1], n[0]] as Vec2; })();
    const half = width / 2;
    const role = op.kind === 'window' ? 'window' : op.role ?? 'room';
    const colour = op.slot ? DOOR_COLOUR.slot : DOOR_COLOUR[role] ?? DOOR_COLOUR.room;
    return {
      id: op.id ?? `${b.id}_op_${op.pos[0]}_${op.pos[1]}`, kind: op.kind === 'window' ? 'window' : 'door', role, glyph: openingGlyph(op), colour,
      a: [round(op.pos[0] - along[0] * half), round(op.pos[1] - along[1] * half)], b: [round(op.pos[0] + along[0] * half), round(op.pos[1] + along[1] * half)], pos: op.pos, width,
      ...(op.links ? { links: op.links } : {}), state: String(effectiveOpeningState(layout, b, op)), exterior: !interior, ...(op.slot ? { slot: op.slot } : {}),
      loop: !!op.id && loopDoors.has(op.id),
    };
  });

  // groups: the flats of the storey and the zones
  const groups: OverlayGroup[] = [];
  const polyOf = new Map(rooms.map((r) => [r.id, r.poly]));
  for (const u of b.units ?? []) {
    if (u.floor !== floor) continue;
    groups.push({ key: `unit:${u.id}`, kind: 'unit', id: u.id, rooms: u.rooms.filter((x) => ids.has(x)), polys: u.rooms.filter((x) => ids.has(x)).map((x) => polyOf.get(x)!),
      ...(u.dead_end ? { dead_end: true, ...(u.run_cm !== undefined ? { run_cm: u.run_cm } : {}) } : {}), column: u.column });
  }
  const zones = new Map<string, string[]>();
  for (const r of here) if (r.zone) zones.set(r.zone, [...(zones.get(r.zone) ?? []), r.id]);
  for (const [z, rs] of [...zones].sort(([p], [q]) => (p < q ? -1 : 1))) groups.push({ key: `zone:${z}`, kind: 'zone', id: z, rooms: rs, polys: rs.map((x) => polyOf.get(x)!) });

  // the validator's issues: placed on this storey by ref
  const roomAt = new Map(rooms.map((r) => [r.id, r.centre]));
  const doorAt = new Map(doors.map((d) => [d.id, d.pos]));
  const placed: OverlayIssue[] = issues.map((i) => {
    const ref = i.ref?.split(' ')[0];
    const pos = ref ? roomAt.get(ref) ?? doorAt.get(ref) : undefined;
    return { code: i.code, severity: i.severity, message: i.message, ...(i.ref ? { ref: i.ref } : {}), ...(pos ? { pos } : {}) };
  });
  return { building: b.id, floor, kind: b.plan.kind === 'bands' ? 'bands' : 'bsp', ...(b.plan.fallback ? { fallback: b.plan.fallback } : {}), rooms, doors, groups, loops: loopInfo.cycles, issues: placed };
}

/** The overlays of every planned building for the page's floor selector (floor mode + layer filter); `issuesOf` = the validator's issues by building id. */
export function planOverlays(layout: GenLayout, mode: OverlayFloorMode, layer = 'all', issuesOf?: (id: string) => readonly TraversalIssue[]): PlanOverlay[] {
  const out: PlanOverlay[] = [];
  for (const b of layout.buildings) {
    if (!b.plan) continue;
    const f = overlayFloor(b, mode, layer);
    const o = f === null ? null : planOverlayFor(layout, b, f, issuesOf?.(b.id) ?? []);
    if (o) out.push(o);
  }
  return out;
}
