// Training Ground / level generator - BUILDING STORIES engine (building generator S1 step 3a; docs/design/levelgen/gdd/building-stories.md
// sections 3 bricks, 5 application, 6 catalogue, F1 two-step pick, F3 role budget, F5 rollback budget).
// A story is a recipe of bricks applied to a COPY of a building; the copy must pass the traversal validator (buildingTraversal.ts,
// opening states honoured) with no error the untouched building did not already have, else every brick is undone (the original
// building object is returned) and a 'story_rolled_back' warning is logged. Catalogue for now: 1 'open' (no bricks),
// 3 'locked_door' (door_state(front, locked) + window_state(front:0, boarded)), 4 'rubble_escape' (block_flight(0->1, rubble) + add_balcony_escape) and
// 7 'roof_outside' (roof_access(ladder)), 9 'broken_back_windows' (window_state(back:0, broken), the front windows at their default)
// and 10 'snow_drift' (door_state(front, sealed) + snow_ramp(storey-1 window)); 4 and 7 are detours (objective / garrison / overwatch
// only), 3, 9 and 10 are choice stories: after apply they must pass route-model F4 (buildingRoutes.ts: fast and quiet enter by
// different openings, one of them the story's lever; story 9: quiet by a back window, fast by the front facade) or roll back. Bricks may add /
// remove layout ladders (fire escapes, facade ladders): the result then carries the new `ladders` list. applyStoriesToLayout
// writes key_route / fast_entry / quiet_entry per building. Pure and deterministic per (seed, building id); NOT wired into the
// generator pipeline yet (a later step does that).

import storiesJson from '../../../../Content/Data/LevelGen/building_stories.json' with { type: 'json' };
import { baySectionTemplate, BAY_SECTIONS } from './buildings/kitPlan.ts';
import type { Vec2 } from '../movement/geometry.ts';
import { closestOnSegment, dist, rng } from '../movement/geometry.ts';
import { fnv1a } from './props/rng.ts';
import { BUILDING_PLANS } from './buildings/bandPlan.ts';
import { inFootprint, normDeg, rotationOf, rotPoint, toBuildingFrame } from './rotation.ts';
import { withRoles } from './buildingRoles.ts';
import type { TraversalIssue } from './buildingTraversal.ts';
import { segHitsBox, TRAVERSAL_SEVERITY, validateBuilding } from './buildingTraversal.ts';
import { ESCAPE_BALCONY_DEPTH } from './fireEscapes.ts';
import { obstacleHeight } from './obstacleHeights.ts';
import { classifyOpening, effectiveOpeningState, isPassableState } from './openingStates.ts';
import type { Balcony, BuildingRole, DoorState, FireEscape, GenBuilding, GenLadder, GenLayout, GenRoom, Side, Stair, StoryInstance, WallOpening, WindowState } from './schema.ts';
import { DOOR_STATES, LEVEL_HEIGHT, ladderForLevels, OPENING_DEFAULTS, STAIR_RUN_PER_RISE, WINDOW_STATES, unifyOpenings } from './schema.ts';
import { computeBuildingRoutes } from './buildingRoutes.ts';
import { openingOutwardDir } from './tacticalEvaluate.ts';

// ---- catalogue ----------------------------------------------------------------------------------------------------------------------

export type StoryAuthor = StoryInstance['author'];
export type StoryId = 'open' | 'locked_door' | 'rubble_escape' | 'roof_outside' | 'broken_back_windows' | 'snow_drift';

/** The catalogue rows implemented so far (building-stories.md 6): number, allowed authors, type. */
export const STORY_CATALOGUE: Record<StoryId, { num: number; authors: readonly StoryAuthor[]; type: 'base' | 'choice' | 'detour' }> = {
  /** 1 Open: no bricks, author traces only (any author). */
  open: { num: 1, authors: ['survivors', 'pmc', 'cold', 'decay'], type: 'base' },
  /** 3 Locked door: door_state(front, locked) + window_state(front:0, boarded) - kick the front or go round to the back door. */
  locked_door: { num: 3, authors: ['pmc', 'decay', 'survivors'], type: 'choice' },
  /** 4 Rubble on the stair + fire escape: block_flight(0->1, rubble) + add_balcony_escape - block / office, >= 2 storeys. */
  rubble_escape: { num: 4, authors: ['decay', 'pmc'], type: 'detour' },
  /** 7 Roof only from outside: roof_access(facade ladder) - an enterable building with a walkable roof and a core / hatch to it. */
  roof_outside: { num: 7, authors: ['decay', 'cold'], type: 'detour' },
  /** 9 Broken back windows: window_state(back facade ground windows, broken), the front glazed (its default) - quiet in from the back. */
  broken_back_windows: { num: 9, authors: ['survivors', 'decay'], type: 'choice' },
  /** 10 Snow-drift entrance: door_state(front, sealed) + snow_ramp(storey-1 window on the front) - up the drift, in by the window. */
  snow_drift: { num: 10, authors: ['cold'], type: 'choice' },
};

/** F1: detour stories are eligible only for these roles (a detour needs a role payoff). building_stories.json `detour_roles`. */
export const DETOUR_ROLES: readonly BuildingRole[] = storiesJson.detour_roles as BuildingRole[];
/**
 * Story 4's archetypes "block, office": the generator builds both as set_piece 'facade' (blockTemplates two_storey `B_*_block`,
 * small_office `B_*_office`); the >= 2 storeys condition is checked separately. building_stories.json `rubble_escape_set_pieces`.
 */
export const RUBBLE_ESCAPE_SET_PIECES: readonly string[] = storiesJson.rubble_escape_set_pieces;

/**
 * The pick knobs (building-stories.md F1 / F3 / F5): `budget` = the share of buildings of a role that get a story other than open
 * (F3, *proposal*), `weights[role][story]` = F1 step-2 weights (archetype dimension not used yet), `t_max` = tries per building
 * before it keeps 'open' (F5), `p_two` = the chance of a second, compatible story (F2).
 */
export interface StoryPickKnobs {
  budget: Record<BuildingRole, number>;
  weights: Record<BuildingRole, Partial<Record<Exclude<StoryId, 'open'>, number>>>;
  t_max: number;
  p_two: number;
}

/** The pick knobs: building_stories.json `pick`. */
export const STORY_PICK_KNOBS: StoryPickKnobs = storiesJson.pick as StoryPickKnobs;

/**
 * F2 compatibility list (building-stories.md 5 step 5 / F2 / Edge Cases): the stories that may follow a first story on the same
 * building (building_stories.json `compatible`). Only 3 + 9 so far (the GDD example; designer 2026-11-19: story 9 needs a loud or
 * absent front, which story 3 gives). Pairs that touch the same opening (3 + 10: lock vs drift-seal of the front) are left out.
 */
export const STORY_COMPATIBLE: Partial<Record<StoryId, readonly Exclude<StoryId, 'open'>[]>> = storiesJson.compatible as Partial<Record<StoryId, Exclude<StoryId, 'open'>[]>>;

/**
 * Front-door states that make story 9 eligible (designer 2026-11-19): a kick (locked) or no way in (sealed / barricaded).
 * building_stories.json `loud_front_states`.
 */
export const LOUD_FRONT_STATES: readonly DoorState[] = storiesJson.loud_front_states as DoorState[];

/** The state a story gives the building's gable door(s) (building_stories.json `gable_door_state`; story 9: locked). */
export const GABLE_DOOR_STATE: Partial<Record<StoryId, DoorState>> = storiesJson.gable_door_state as Partial<Record<StoryId, DoorState>>;

// ---- bricks -------------------------------------------------------------------------------------------------------------------------

/** How a blocked flight is blocked (schema /2 Stair.blocked). */
export type FlightBlock = NonNullable<Stair['blocked']>;
export const FLIGHT_BLOCKS: readonly FlightBlock[] = ['rubble', 'burnt', 'barricade'];

/** One brick of a recipe (building-stories.md 3). */
export type Brick =
  | { kind: 'door_state'; door: string; state: DoorState }
  /** `floor` (optional) limits a side target to one storey (story 9: the back facade's ground windows). */
  | { kind: 'window_state'; target: Side | number; state: WindowState; floor?: number }
  | { kind: 'block_flight'; stair: string; block: FlightBlock }
  /** `room` = the S1 'flat': a room on the lowest storey of `storeys`, on an exterior wall. */
  | { kind: 'add_balcony_escape'; room: string; storeys: number[] }
  | { kind: 'roof_access'; access: 'ladder' }
  /** `window` = an exterior storey-1 window ref (id or "openings[i]"). */
  | { kind: 'snow_ramp'; window: string };

/** The brick as stored in StoryInstance.bricks, e.g. "door_state(D_B1_0,locked)". */
export function brickToString(b: Brick): string {
  switch (b.kind) {
    case 'door_state': return `door_state(${b.door},${b.state})`;
    case 'window_state': return `window_state(${b.target}${b.floor !== undefined ? `:${b.floor}` : ''},${b.state})`;
    case 'block_flight': return `block_flight(${b.stair},${b.block})`;
    case 'add_balcony_escape': return `add_balcony_escape(${b.room},${b.storeys.join('|')})`;
    case 'roof_access': return `roof_access(${b.access})`;
    case 'snow_ramp': return `snow_ramp(${b.window})`;
  }
}

/** The reference of an opening: its id, else its index in unifyOpenings ("openings[3]"). */
export function openingRef(b: GenBuilding, index: number): string {
  return unifyOpenings(b)[index]?.id ?? `openings[${index}]`;
}

/** Make b.openings the editable list (legacy door-only buildings get unifyOpenings' copies; nothing else changes). */
function editableOpenings(b: GenBuilding): WallOpening[] {
  if (!b.openings?.length) b.openings = unifyOpenings(b).map((o) => ({ ...o }));
  return b.openings;
}

/** The facade side an opening sits on (building frame: north = min y, south = max y, west = min x, east = max x). */
export function openingSide(b: GenBuilding, op: WallOpening): Side {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const q = toBuildingFrame(b, op.pos);
  const d: [Side, number][] = [['west', Math.abs(q[0] - x0)], ['east', Math.abs(q[0] - x1)], ['north', Math.abs(q[1] - y0)], ['south', Math.abs(q[1] - y1)]];
  return d.reduce((a, c) => (c[1] < a[1] ? c : a))[0];
}

/** door_state(door, state): set one door's state (b.openings id or "openings[i]" ref, partition doors by id). Mutates `b`. */
export function applyDoorState(b: GenBuilding, door: string, state: DoorState): boolean {
  if (!DOOR_STATES.includes(state)) return false;
  const m = /^openings\[(\d+)\]$/.exec(door);
  const ops = editableOpenings(b);
  const hit = m ? ops[Number(m[1])] : ops.find((o) => o.id === door) ?? (b.partitions ?? []).flatMap((pt) => pt.openings ?? []).find((o) => o.id === door);
  if (!hit || hit.kind !== 'door') return false;
  hit.state = state;
  return true;
}

/**
 * window_state(side or storey, state): set the state of every exterior window on a facade side (all storeys) or on a storey (all
 * sides); `floor` limits a side target to that storey. Loopholes stay 'fixed'. Returns the number of windows changed. Mutates `b`.
 */
export function applyWindowState(l: Pick<GenLayout, 'walls'>, b: GenBuilding, target: Side | number, state: WindowState, floor?: number): number {
  if (!WINDOW_STATES.includes(state)) return 0;
  let n = 0;
  for (const op of editableOpenings(b)) {
    if (op.kind !== 'window' || !classifyOpening(l, b, op).exterior) continue;
    if (typeof target === 'number' ? (op.floor ?? 0) !== target : openingSide(b, op) !== target) continue;
    if (floor !== undefined && (op.floor ?? 0) !== floor) continue;
    op.state = state;
    n++;
  }
  return n;
}

/** block_flight(stair, kind): the flight stays (geometry) but is impassable - no route edge (stories 4, 13, 15). Mutates `b`. */
export function applyBlockFlight(b: GenBuilding, stairId: string, block: FlightBlock): boolean {
  if (!FLIGHT_BLOCKS.includes(block)) return false;
  const st = (b.stairs ?? []).find((s) => s.id === stairId);
  if (!st || st.blocked) return false;
  st.blocked = block;
  return true;
}

// ---- facade geometry (building frame: north = min y, south = max y, west = min x, east = max x) ---------------------------------------

type Box = [Vec2, Vec2];

const SIDE_ORDER: readonly Side[] = ['north', 'east', 'south', 'west'];
const OPPOSITE_SIDE: Record<Side, Side> = { north: 'south', south: 'north', east: 'west', west: 'east' };
/** Ladder yaw per facade (LADDERS.md 3.1: +X away from the wall, towards the climber) - as addLadderAt in generate.ts. */
const OUT_YAW: Record<Side, number> = { north: 270, south: 90, west: 180, east: 0 };

/** Fire-escape / facade-ladder clearances, cm (building-archetypes.md 3; LADDERS.md 3.1 as addLadderAt). */
export const FACADE = {
  balconyWidth: 300,
  /** Free ground in front of the escape's foot (the approach box). */
  escapeApproach: 150,
  ladderGap: 10,
  /** The ladder's foot link point, out from the rungs. */
  ladderFoot: 110,
  /** The top link point of a roof ladder, in from the facade. */
  ladderTopIn: 100,
  /** A balcony door at least this far from its room's corners along the facade. */
  doorEdge: 80,
  /** Free wall between a new door and an existing opening. */
  openingClear: 60,
  /** A new door's inside point at least this far from a flight on its storey. */
  stairClear: 250,
  /** A ladder / escape at least this far (along the facade) from a ground door. */
  doorClear: 150,
  /** Candidate positions along a facade / room span (fractions, tried in order). */
  fractions: [0.5, 0.3, 0.7, 0.15, 0.85] as readonly number[],
};

const horizontalSide = (side: Side) => side === 'north' || side === 'south';
const alongOf = (side: Side, p: Vec2) => (horizontalSide(side) ? p[0] : p[1]);
const fpCentre = (b: GenBuilding): Vec2 => [(b.footprint[0][0] + b.footprint[1][0]) / 2, (b.footprint[0][1] + b.footprint[1][1]) / 2];
/** A building-frame point in world coordinates (the inverse of toBuildingFrame). */
const toWorld = (b: GenBuilding, p: Vec2): Vec2 => rotPoint(p, fpCentre(b), rotationOf(b));

/** The building-frame point on facade `side` at `along`, `out` cm outward (negative = inside). */
function facadePoint(b: GenBuilding, side: Side, along: number, out: number): Vec2 {
  const [[x0, y0], [x1, y1]] = b.footprint;
  switch (side) {
    case 'north': return [along, y0 - out];
    case 'south': return [along, y1 + out];
    case 'west': return [x0 - out, along];
    case 'east': return [x1 + out, along];
  }
}

function faceSpan(b: GenBuilding, side: Side): [number, number] {
  const [[x0, y0], [x1, y1]] = b.footprint;
  return horizontalSide(side) ? [x0, x1] : [y0, y1];
}

/** The building-frame box outside facade `side`: `halfW` either side of `along`, from the wall `depth` cm out. */
function outBox(b: GenBuilding, side: Side, along: number, halfW: number, depth: number): Box {
  const p = facadePoint(b, side, along - halfW, 0), q = facadePoint(b, side, along + halfW, depth);
  return [[Math.min(p[0], q[0]), Math.min(p[1], q[1])], [Math.max(p[0], q[0]), Math.max(p[1], q[1])]];
}

/** No other building stands on the outside box and no ground wall / fence crosses it. */
function outsideClear(l: GenLayout, b: GenBuilding, box: Box): boolean {
  const [[ax, ay], [bx, by]] = box;
  const nx = Math.max(1, Math.ceil((bx - ax) / 50)), ny = Math.max(1, Math.ceil((by - ay) / 50));
  for (let i = 0; i <= nx; i++) {
    for (let j = 0; j <= ny; j++) {
      const w = toWorld(b, [ax + 5 + ((bx - ax - 10) * i) / nx, ay + 5 + ((by - ay - 10) * j) / ny]);
      if (l.buildings.some((o) => o.id !== b.id && inFootprint(o, w))) return false;
    }
  }
  return !l.walls.some((w) => w.building !== b.id && (w.floor ?? 0) === 0 && segHitsBox(toBuildingFrame(b, w.a), toBuildingFrame(b, w.b), box));
}

/** The exterior ground-floor doors of the building (any state), each with its facade side and position along it. */
function groundDoors(l: GenLayout, b: GenBuilding): { side: Side; along: number }[] {
  return unifyOpenings(b)
    .filter((o) => o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior)
    .map((o) => {
      const side = openingSide(b, o);
      return { side, along: alongOf(side, toBuildingFrame(b, o.pos)) };
    });
}

/** Facade sides for an escape / outside ladder: fewest ground doors first, then the side opposite the front door, then N E S W. */
export function sidesAwayFromDoors(l: GenLayout, b: GenBuilding): Side[] {
  const doors = groundDoors(l, b);
  const front = frontDoor(l, b);
  const back = front ? OPPOSITE_SIDE[openingSide(b, front.opening)] : null;
  const key = (s: Side) => [doors.filter((d) => d.side === s).length, s === back ? 0 : 1, SIDE_ORDER.indexOf(s)];
  return [...SIDE_ORDER].sort((a, c) => {
    const ka = key(a), kc = key(c);
    return ka[0] - kc[0] || ka[1] - kc[1] || ka[2] - kc[2];
  });
}

const doorFree = (doors: readonly { side: Side; along: number }[], side: Side, along: number, half: number) =>
  doors.every((d) => d.side !== side || Math.abs(d.along - along) >= half + FACADE.doorClear);

function uniqueId(taken: ReadonlySet<string>, base: string): string {
  if (!taken.has(base)) return base;
  for (let k = 2; ; k++) if (!taken.has(`${base}_${k}`)) return `${base}_${k}`;
}

function ladderOf(b: GenBuilding, id: string, side: Side, levelFrom: number, levelTo: number, from: string, to: string, pos: Vec2, foot: Vec2, top: Vec2): GenLadder {
  const lad = ladderForLevels(levelTo - levelFrom);
  return {
    id, pos: toWorld(b, pos), yaw: normDeg(OUT_YAW[side] + rotationOf(b)), rungs: lad.rungs, plinth_cm: lad.plinth_cm,
    from, to, level_from: levelFrom, level_to: levelTo, foot: toWorld(b, foot), top: toWorld(b, top),
  };
}

// ---- add_balcony_escape ---------------------------------------------------------------------------------------------------------------

/** The rooms of a building (its `rooms`, else one virtual room per walkable storey, as the validator's graph). */
function roomsOf(b: GenBuilding): GenRoom[] {
  if (b.rooms?.length) return b.rooms;
  return Array.from({ length: b.hollow ? 1 : b.floors }, (_, f) => ({ id: `${b.id}_floor_${f}`, floor: f, bounds: { min: b.footprint[0], max: b.footprint[1] } }));
}

/** The room touches the facade `side` (its bounds reach the footprint edge, building frame). */
function roomOnFacade(b: GenBuilding, rm: GenRoom, side: Side, tol = 30): boolean {
  const [[x0, y0], [x1, y1]] = b.footprint;
  const { min, max } = rm.bounds;
  switch (side) {
    case 'north': return Math.abs(min[1] - y0) <= tol;
    case 'south': return Math.abs(max[1] - y1) <= tol;
    case 'west': return Math.abs(min[0] - x0) <= tol;
    case 'east': return Math.abs(max[0] - x1) <= tol;
  }
}

const roomSpan = (rm: GenRoom, side: Side): [number, number] => (horizontalSide(side) ? [rm.bounds.min[0], rm.bounds.max[0]] : [rm.bounds.min[1], rm.bounds.max[1]]);

/** The S1 'flat' for an escape on storey `floor`: the first room (by id) on a facade side away from the doors, or null. */
export function escapeRoom(l: GenLayout, b: GenBuilding, floor: number): GenRoom | null {
  const rooms = roomsOf(b).filter((rm) => rm.floor === floor).sort((a, c) => (a.id < c.id ? -1 : a.id > c.id ? 1 : 0));
  for (const side of sidesAwayFromDoors(l, b)) {
    const hit = rooms.find((rm) => roomOnFacade(b, rm, side));
    if (hit) return hit;
  }
  return null;
}

interface EscapeSpot { side: Side; along: number; rooms: GenRoom[]; /** A planned block: the slot windows (one per storey) the escape's doors replace. */ slots?: WallOpening[] }

/** The balcony slot windows of a planned block (S2.3: `slot: 'balcony'`, one per flat and storey from 1) with their room and the flat's column. */
function slotWindows(b: GenBuilding): { op: WallOpening; room: GenRoom; column: string }[] {
  const out: { op: WallOpening; room: GenRoom; column: string }[] = [];
  for (const op of b.openings ?? []) {
    if (op.kind !== 'window' || op.slot !== 'balcony') continue;
    const room = (b.rooms ?? []).find((rm) => rm.id === op.links?.[0]);
    const unit = room?.unit ? (b.units ?? []).find((u) => u.id === room.unit) : undefined;
    if (room && unit) out.push({ op, room, column: unit.column });
  }
  return out;
}

/**
 * The flat columns that can take the escape of story 4 (S2_ROOMS_PLAN.md 3.6): a slot window on every storey of `storeys`, the same spot on each.
 * Ordered by column id; each is the flat's room on the lowest storey with the spot checked by planEscapeSpot.
 */
export function escapeColumns(l: GenLayout, b: GenBuilding, storeys: readonly number[]): GenRoom[] {
  const all = slotWindows(b);
  const first = Math.min(...storeys);
  return all.filter((x) => (x.op.floor ?? 0) === first).sort((x, y) => (x.column < y.column ? -1 : x.column > y.column ? 1 : 0))
    .filter((x) => planSlotSpot(l, b, x.room, storeys, all) !== null).map((x) => x.room);
}

/**
 * The spot of a room's slot window, the same column's windows above it: undefined when the room has no slot (the generic spot search applies),
 * null when the spot does not fit (ground door too near, a wall opening beside it, a flight, something outside), else the spot.
 */
function planSlotSpot(l: GenLayout, b: GenBuilding, room: GenRoom, storeys: readonly number[], all = slotWindows(b)): EscapeSpot | null | undefined {
  const mine = all.find((x) => x.room.id === room.id);
  if (!mine) return undefined;
  const half = FACADE.balconyWidth / 2, side = openingSide(b, mine.op), along = alongOf(side, toBuildingFrame(b, mine.op.pos));
  const [flo, fhi] = faceSpan(b, side);
  if (along - half < flo || along + half > fhi || !doorFree(groundDoors(l, b), side, along, half)) return null;
  const rooms: GenRoom[] = [], slots: WallOpening[] = [];
  for (const f of storeys) {
    const hit = all.find((x) => x.column === mine.column && (x.op.floor ?? 0) === f && openingSide(b, x.op) === side && Math.abs(alongOf(side, toBuildingFrame(b, x.op.pos)) - along) <= 1);
    if (!hit) return null;
    const wallFree = unifyOpenings(b).every((o) => o.id === hit.op.id || (o.floor ?? 0) !== f || openingSide(b, o) !== side
      || Math.abs(alongOf(side, toBuildingFrame(b, o.pos)) - along) >= (o.width ?? OPENING_DEFAULTS[o.kind].width) / 2 + (hit.op.width ?? OPENING_DEFAULTS.door.width) / 2 + FACADE.openingClear);
    const inside = facadePoint(b, side, along, -75);
    const stairFree = (b.stairs ?? []).every((st) => (st.from !== f && st.to !== f)
      || dist(inside, closestOnSegment(inside, toBuildingFrame(b, st.pos), toBuildingFrame(b, st.top)).point) >= FACADE.stairClear);
    if (!wallFree || !stairFree) return null;
    rooms.push(hit.room);
    slots.push(hit.op);
  }
  if (!outsideClear(l, b, outBox(b, side, along, half, ESCAPE_BALCONY_DEPTH + FACADE.escapeApproach))) return null;
  return { side, along, rooms, slots };
}

/** Depth of a usable slot balcony (the kit balcony panel is ~102 cm deep: designer 2026-11-22 "the decorative balconies are too narrow"). */
export const SLOT_BALCONY_DEPTH = 110;

/**
 * Usable balconies on the balcony SLOTS of a planned block (designer 2026-11-22: "balconies on every building - step out, a position behind
 * the parapet (low cover), vault the rail and drop by height; the decorative ones get a balcony door"). Up to `maxColumns` flat columns
 * (in column order, the spot checked like the escape's: wall, stairs, ground doors, outside clear) get, on every storey of that column's
 * slot windows, a balcony door in place of the window and a balcony slab SLOT_BALCONY_DEPTH deep with a low-vault rail - no fire escape
 * (the rail + the fall rules are the way down). A building with balconies already (story 4's escape) is left alone. Mutates `b`; returns how
 * many balconies were added.
 */
export function applySlotBalconies(l: GenLayout, b: GenBuilding, maxColumns = 2): number {
  if (!b.enterable || b.hollow || (b.balconies ?? []).length || maxColumns <= 0) return 0;
  const all = slotWindows(b);
  const columns = [...new Set(all.map((x) => x.column))].sort();
  const realRooms = new Set(b.rooms ?? []);
  const O = BUILDING_PLANS.constants.openings;
  const half = FACADE.balconyWidth / 2;
  let added = 0;
  let usedColumns = 0;
  for (const column of columns) {
    if (usedColumns >= maxColumns) break;
    const mine = all.filter((x) => x.column === column).sort((x, y) => (x.op.floor ?? 0) - (y.op.floor ?? 0));
    const storeys = [...new Set(mine.map((x) => x.op.floor ?? 0))].filter((f) => f >= 1 && f < b.floors);
    if (!storeys.length) continue;
    const lowest = mine.find((x) => (x.op.floor ?? 0) === storeys[0]);
    const spot = lowest ? planSlotSpot(l, b, lowest.room, storeys, all) : null;
    if (!spot || !spot.slots) continue;
    const taken = new Set<string>([...unifyOpenings(b).map((o) => o.id ?? ''), ...(b.balconies ?? []).map((x) => x.id)]);
    storeys.forEach((f, i) => {
      const slot = spot.slots![i], rm = spot.rooms[i];
      const balconyId = uniqueId(taken, `${b.id}_bal_${column}_f${f}`);
      taken.add(balconyId);
      const doorId = uniqueId(taken, `${balconyId}_door`);
      taken.add(doorId);
      b.openings = editableOpenings(b).filter((o) => o.id !== slot.id);
      b.mantle_edges = (b.mantle_edges ?? []).filter((m) => m.id !== `me_${slot.id}`);
      if (realRooms.has(rm)) rm.door_ids = (rm.door_ids ?? []).filter((x) => x !== slot.id);
      editableOpenings(b).push({
        id: doorId, kind: 'door', pos: slot.pos, floor: f, width: b.plan?.template && baySectionTemplate(b.plan.template) ? BAY_SECTIONS.windows_cm.balcony_door : O.door_cm,
        ...(slot.wall ? { wall: slot.wall } : {}),
        sill_height: 0, top_height: O.door_top_cm, links: [rm.id, `balcony:${balconyId}`] as [string, string], role: 'balcony' as const, module: 'wall_door_200',
      });
      if (realRooms.has(rm)) rm.door_ids = [...(rm.door_ids ?? []), doorId];
      (b.balconies ??= []).push({ id: balconyId, floor: f, rect: outBox(b, spot.side, spot.along, half, SLOT_BALCONY_DEPTH), door_id: doorId,
        rail_cm: obstacleHeight('low_vault') });
      added += 1;
    });
    usedColumns += 1;
  }
  return added;
}

/** A facade spot for the escape: `room` and a room above on every storey reach the facade there, wall and ground are free. */
function planEscapeSpot(l: GenLayout, b: GenBuilding, room: GenRoom, storeys: readonly number[]): EscapeSpot | null {
  const viaSlot = planSlotSpot(l, b, room, storeys);
  if (viaSlot !== undefined) return viaSlot;
  const rooms = roomsOf(b);
  const doors = groundDoors(l, b);
  const half = FACADE.balconyWidth / 2;
  for (const side of sidesAwayFromDoors(l, b)) {
    if (!roomOnFacade(b, room, side)) continue;
    const [lo, hi] = roomSpan(room, side), [flo, fhi] = faceSpan(b, side);
    for (const fr of FACADE.fractions) {
      const along = lo + (hi - lo) * fr;
      if (along - lo < FACADE.doorEdge || hi - along < FACADE.doorEdge || along - half < flo || along + half > fhi) continue;
      if (!doorFree(doors, side, along, half)) continue;
      const picked: GenRoom[] = [];
      for (const f of storeys) {
        const rm = f === room.floor ? room : rooms
          .filter((x) => x.floor === f && roomOnFacade(b, x, side))
          .sort((a, c) => (a.id < c.id ? -1 : a.id > c.id ? 1 : 0))
          .find((x) => { const [a, c] = roomSpan(x, side); return along - a >= FACADE.doorEdge && c - along >= FACADE.doorEdge; });
        if (!rm) break;
        const wallFree = unifyOpenings(b).every((o) => (o.floor ?? 0) !== f || openingSide(b, o) !== side
          || Math.abs(alongOf(side, toBuildingFrame(b, o.pos)) - along) >= (o.width ?? OPENING_DEFAULTS[o.kind].width) / 2 + OPENING_DEFAULTS.door.width / 2 + FACADE.openingClear);
        const inside = facadePoint(b, side, along, -75);
        const stairFree = (b.stairs ?? []).every((st) => (st.from !== f && st.to !== f)
          || dist(inside, closestOnSegment(inside, toBuildingFrame(b, st.pos), toBuildingFrame(b, st.top)).point) >= FACADE.stairClear);
        if (!wallFree || !stairFree) break;
        picked.push(rm);
      }
      if (picked.length !== storeys.length) continue;
      if (!outsideClear(l, b, outBox(b, side, along, half, ESCAPE_BALCONY_DEPTH + FACADE.escapeApproach))) continue;
      return { side, along, rooms: picked };
    }
  }
  return null;
}

/**
 * add_balcony_escape(flat, storeys): a balcony door + a 150-deep balcony on each storey of `storeys` and one ladder fire escape
 * (ground -> each balcony, `GenLadder`s with ladderForLevels rungs appended to `l.ladders`) on a facade side away from the
 * ground doors, stacked at one spot of `room`'s facade wall. S1 'flat' = any room on an exterior wall (building-stories.md 3).
 * Mutates `b` and `l.ladders`; false (nothing changed) when no facade spot fits.
 */
export function applyBalconyEscape(l: GenLayout, b: GenBuilding, roomId: string, storeys: readonly number[]): boolean {
  const floors = [...new Set(storeys)].sort((a, c) => a - c);
  if (!b.enterable || b.hollow || !floors.length || floors[0] < 1 || floors[floors.length - 1] >= b.floors) return false;
  const room = roomsOf(b).find((rm) => rm.id === roomId && rm.floor === floors[0]);
  if (!room) return false;
  const spot = planEscapeSpot(l, b, room, floors);
  if (!spot) return false;
  const { side, along } = spot;
  const taken = new Set<string>([
    ...unifyOpenings(b).map((o) => o.id ?? ''), ...(b.balconies ?? []).map((x) => x.id), ...(b.fire_escapes ?? []).map((x) => x.id), ...(l.ladders ?? []).map((x) => x.id),
  ]);
  const escId = uniqueId(taken, `${b.id}_esc`);
  const D = ESCAPE_BALCONY_DEPTH, half = FACADE.balconyWidth / 2;
  const balconies: Balcony[] = [];
  const ladders: GenLadder[] = [];
  const realRooms = new Set(b.rooms ?? []);
  floors.forEach((f, i) => {
    const doorId = `${escId}_door_f${f}`, balconyId = `${escId}_bal_f${f}`;
    const pos = toWorld(b, facadePoint(b, side, along, 0));
    const wall = l.walls.find((w) => w.building === b.id && (w.floor ?? 0) === f && w.kind !== 'interior' && w.kind !== 'partition'
      && dist(pos, closestOnSegment(pos, w.a, w.b).point) < 30);
    const rm = spot.rooms[i], slot = spot.slots?.[i];
    const O = BUILDING_PLANS.constants.openings;
    // a planned building's doors carry links / role / module (validator opening_no_links); a slot window is replaced by the door at its point
    const planned = b.plan?.kind === 'bands';
    if (slot) {
      b.openings = editableOpenings(b).filter((o) => o.id !== slot.id);
      b.mantle_edges = (b.mantle_edges ?? []).filter((m) => m.id !== `me_${slot.id}`);
      if (realRooms.has(rm)) rm.door_ids = (rm.door_ids ?? []).filter((x) => x !== slot.id);
    }
    editableOpenings(b).push({
      id: doorId, kind: 'door', pos, floor: f, width: planned ? O.door_cm : OPENING_DEFAULTS.door.width, ...(wall ? { wall: wall.id } : {}),
      ...(planned ? { sill_height: 0, top_height: O.door_top_cm, links: [rm.id, `balcony:${balconyId}`] as [string, string], role: 'balcony' as const, module: 'wall_door_200' } : {}),
    });
    const r = outBox(b, side, along, half, D);
    balconies.push({ id: balconyId, floor: f, rect: r, door_id: doorId, rail_cm: obstacleHeight('low_vault') });
    if (realRooms.has(rm)) rm.door_ids = [...(rm.door_ids ?? []), doorId];
    // ground -> the first balcony: on the balcony's outer edge; balcony -> balcony: against the wall at its end, through a hatch above
    const lid = `LAD_${escId}_${i}`;
    if (i === 0) {
      ladders.push(ladderOf(b, lid, side, 0, f, 'ground', `floor:${b.id}:${f}`,
        facadePoint(b, side, along, D + FACADE.ladderGap), facadePoint(b, side, along, D + FACADE.ladderFoot), facadePoint(b, side, along, D - 100)));
    } else {
      const a2 = along + half - 60, prev = floors[i - 1];
      ladders.push(ladderOf(b, lid, side, prev, f, `floor:${b.id}:${prev}`, `floor:${b.id}:${f}`,
        facadePoint(b, side, a2, FACADE.ladderGap), facadePoint(b, side, a2, FACADE.ladderFoot), facadePoint(b, side, a2, 40)));
    }
  });
  const escape: FireEscape = {
    id: escId, kind: 'ladder', side, at: facadePoint(b, side, along, D), ground: facadePoint(b, side, along, D + FACADE.ladderFoot),
    stops: floors.map((f) => ({ floor: f, balcony_id: `${escId}_bal_f${f}` })), to_roof: false, ladder_ids: ladders.map((x) => x.id),
  };
  b.balconies = [...(b.balconies ?? []), ...balconies];
  b.fire_escapes = [...(b.fire_escapes ?? []), escape];
  (l.ladders ??= []).push(...ladders);
  return true;
}

// ---- roof_access ----------------------------------------------------------------------------------------------------------------------

/** The building's inside accesses to its walkable roof: the core's flights onto the roof, its hatches and their / inside ladders. */
export function insideRoofAccess(l: Pick<GenLayout, 'ladders'>, b: GenBuilding): { stairs: Stair[]; hatches: number; ladders: GenLadder[] } {
  const hatchLadders = new Set((b.hatches ?? []).map((h) => h.ladder_id).filter((x): x is string => !!x));
  return {
    stairs: (b.stairs ?? []).filter((st) => st.kind === 'stairwell' && st.to >= b.floors && !st.blocked),
    hatches: (b.hatches ?? []).length,
    ladders: (l.ladders ?? []).filter((ld) => ld.to === `roof:${b.id}` && (hatchLadders.has(ld.id) || ld.from.startsWith(`floor:${b.id}:`))),
  };
}

/**
 * roof_access(ladder): removes the core-to-roof access (every stairwell flight arriving on the roof, the roof hatches and their
 * ladders) and adds one external facade ladder ground -> roof (ladderForLevels(floors), as addLadderAt in generate.ts) on a side
 * away from the ground doors. Mutates `b` and `l.ladders`; false (nothing changed) without an inside access or a free facade spot.
 */
export function applyRoofAccess(l: GenLayout, b: GenBuilding, access: 'ladder'): boolean {
  if (access !== 'ladder' || !b.enterable || !b.roof_walkable) return false;
  const inner = insideRoofAccess(l, b);
  if (!inner.stairs.length && !inner.hatches && !inner.ladders.length) return false;
  const doors = groundDoors(l, b);
  const feet = (l.ladders ?? []).filter((ld) => !inner.ladders.includes(ld)).map((ld) => toBuildingFrame(b, ld.foot));
  let spot: { side: Side; along: number } | null = null;
  for (const side of sidesAwayFromDoors(l, b)) {
    const [flo, fhi] = faceSpan(b, side);
    for (const fr of FACADE.fractions) {
      const along = flo + (fhi - flo) * fr;
      if (along - flo < 60 || fhi - along < 60 || !doorFree(doors, side, along, 60)) continue;
      const foot = facadePoint(b, side, along, FACADE.ladderFoot);
      if (feet.some((p) => dist(p, foot) < FACADE.doorClear)) continue;
      if (!outsideClear(l, b, outBox(b, side, along, 60, FACADE.ladderFoot + 40))) continue;
      spot = { side, along };
      break;
    }
    if (spot) break;
  }
  if (!spot) return false;
  const { side, along } = spot;
  b.stairs = b.stairs.filter((st) => !inner.stairs.includes(st));
  if (b.hatches) b.hatches = [];
  const ladders = (l.ladders ??= []);
  for (const ld of inner.ladders) ladders.splice(ladders.indexOf(ld), 1);
  const id = uniqueId(new Set(ladders.map((x) => x.id)), `LAD_${b.id}_roof`);
  ladders.push(ladderOf(b, id, side, 0, b.floors, 'ground', `roof:${b.id}`,
    facadePoint(b, side, along, FACADE.ladderGap), facadePoint(b, side, along, FACADE.ladderFoot), facadePoint(b, side, along, -FACADE.ladderTopIn)));
  return true;
}

// ---- snow_ramp ------------------------------------------------------------------------------------------------------------------------

/** Snow ramp geometry, cm (building-stories.md 3 brick `snow_ramp`: walkable slope <= 35 deg, ends at the sill as a low_vault). */
export const SNOW_RAMP = {
  width: 150,
  /** Run per storey: 300 cm rise * 1.43 = 429 cm -> 34.97 deg. */
  runPerStorey: LEVEL_HEIGHT * STAIR_RUN_PER_RISE,
  /** Free ground beyond the ramp's foot (the approach cell). */
  approach: 150,
};

/** Resolve an opening ref (id, or "openings[i]" = index in unifyOpenings). */
function openingByRef(b: GenBuilding, ref: string): WallOpening | undefined {
  const m = /^openings\[(\d+)\]$/.exec(ref);
  const ops = unifyOpenings(b);
  return m ? ops[Number(m[1])] : ops.find((o) => o.id === ref);
}

/**
 * Where a snow ramp to window `ref` would stand: an exterior storey-1 window in a passable state, the ramp perpendicular to its
 * facade with a run of SNOW_RAMP.runPerStorey (<= 35 deg) plus the approach on free ground inside the layout bounds, clear of the
 * other ground doors of that facade (sealed ones and `exceptDoor` - the door the drift will seal - excepted). Null = not
 * eligible on that facade.
 */
export function planSnowRamp(l: GenLayout, b: GenBuilding, ref: string, exceptDoor?: WallOpening): { side: Side; along: number; foot: Vec2 } | null {
  const op = openingByRef(b, ref);
  if (!op || op.kind !== 'window' || op.floor !== 1 || b.hollow || b.floors < 2) return null;
  if (!classifyOpening(l, b, op).exterior || !isPassableState(effectiveOpeningState(l, b, op))) return null;
  const side = openingSide(b, op), along = alongOf(side, toBuildingFrame(b, op.pos));
  const half = SNOW_RAMP.width / 2, run = SNOW_RAMP.runPerStorey;
  const [flo, fhi] = faceSpan(b, side);
  if (along - half < flo || along + half > fhi) return null;
  const doors = unifyOpenings(b)
    .filter((o) => o !== exceptDoor && o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior && openingSide(b, o) === side
      && effectiveOpeningState(l, b, o) !== 'sealed')
    .map((o) => ({ side, along: alongOf(side, toBuildingFrame(b, o.pos)) }));
  if (!doorFree(doors, side, along, half)) return null;
  const box = outBox(b, side, along, half, run + SNOW_RAMP.approach);
  if (!outsideClear(l, b, box)) return null;
  if (l.bounds) {
    const far = toWorld(b, facadePoint(b, side, along, run + SNOW_RAMP.approach));
    if (far[0] < l.bounds.min[0] || far[0] > l.bounds.max[0] || far[1] < l.bounds.min[1] || far[1] > l.bounds.max[1]) return null;
  }
  return { side, along, foot: facadePoint(b, side, along, run) };
}

/**
 * snow_ramp(window): a SnowRamp from the ground (its foot `from`, building frame) up to the storey-1 window `ref`; the window then
 * becomes an entry (validator + route model). Mutates `b`; false (nothing changed) when planSnowRamp finds no room.
 */
export function applySnowRamp(l: GenLayout, b: GenBuilding, ref: string): boolean {
  const spot = planSnowRamp(l, b, ref);
  if (!spot) return false;
  const taken = new Set((b.snow_ramps ?? []).map((r) => r.id));
  b.snow_ramps = [...(b.snow_ramps ?? []), { id: uniqueId(taken, `${b.id}_ramp`), from: spot.foot, to_opening: ref, width_cm: SNOW_RAMP.width }];
  return true;
}

/** Apply one brick to `b` (and `l.ladders` for the escape / roof bricks); false when it did not apply. */
function applyBrick(l: GenLayout, b: GenBuilding, brick: Brick): boolean {
  switch (brick.kind) {
    case 'door_state': return applyDoorState(b, brick.door, brick.state);
    case 'window_state': return applyWindowState(l, b, brick.target, brick.state, brick.floor) > 0;
    case 'block_flight': return applyBlockFlight(b, brick.stair, brick.block);
    case 'add_balcony_escape': return applyBalconyEscape(l, b, brick.room, brick.storeys);
    case 'roof_access': return applyRoofAccess(l, b, brick.access);
    case 'snow_ramp': return applySnowRamp(l, b, brick.window);
  }
}

// ---- recipes ------------------------------------------------------------------------------------------------------------------------

/** The exterior ground-floor doors of a building that are passable in their effective state, with their refs. */
export function passableExteriorDoors(l: GenLayout, b: GenBuilding): { ref: string; opening: WallOpening }[] {
  return unifyOpenings(b)
    .map((opening, i) => ({ ref: openingRef(b, i), opening }))
    .filter(({ opening: o }) => o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior && isPassableState(effectiveOpeningState(l, b, o)));
}

function routeDistance(route: readonly Vec2[], p: Vec2): { d: number; point: Vec2 } {
  if (route.length === 1) return { d: dist(p, route[0]), point: route[0] };
  let best = { d: Infinity, point: p };
  for (let i = 0; i + 1 < route.length; i++) {
    const c = closestOnSegment(p, route[i], route[i + 1]).point;
    const d = dist(p, c);
    if (d < best.d) best = { d, point: c };
  }
  return best;
}

/**
 * The front door of a building: among its passable exterior ground-floor doors, the one facing the mandatory route (its outward
 * normal points at the nearest route point) and nearest to it; without a route (or none facing it) the nearest / first one.
 * Null without a passable exterior door.
 */
export function frontDoor(l: GenLayout, b: GenBuilding): { ref: string; opening: WallOpening } | null {
  return pickFront(l, b, passableExteriorDoors(l, b));
}

/**
 * The front door whatever its state: as frontDoor, but among ALL exterior ground-floor doors (a sealed / barricaded front is still
 * the front). Story 9 reads its state and its facade. Null without an exterior ground-floor door.
 */
export function frontDoorAnyState(l: GenLayout, b: GenBuilding): { ref: string; opening: WallOpening } | null {
  const doors = unifyOpenings(b)
    .map((opening, i) => ({ ref: openingRef(b, i), opening }))
    .filter(({ opening: o }) => o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior);
  return pickFront(l, b, doors);
}

function pickFront(l: GenLayout, b: GenBuilding, doors: { ref: string; opening: WallOpening }[]): { ref: string; opening: WallOpening } | null {
  if (!doors.length) return null;
  const route = l.routes.find((r) => r.mandatory)?.points ?? [];
  if (!route.length) return doors[0];
  const scored = doors.map((d, i) => {
    const { d: dd, point } = routeDistance(route, d.opening.pos);
    const n = openingOutwardDir(b, d.opening);
    const facing = (point[0] - d.opening.pos[0]) * n[0] + (point[1] - d.opening.pos[1]) * n[1] > 0;
    return { d, i, dd, facing };
  });
  scored.sort((a, c) => Number(c.facing) - Number(a.facing) || a.dd - c.dd || a.i - c.i);
  return scored[0].d;
}

/**
 * Story 9's back facade: the ground-floor facade opposite the front door's side (frontDoorAnyState: the same door as story 3's
 * frontDoor while every door is passable; a sealed front stays the front), with the refs of its exterior ground windows that are not
 * 'broken' already (a ruin's are: the story would change nothing). Null without an exterior ground door.
 */
export function backWindows(l: GenLayout, b: GenBuilding): { front: { ref: string; opening: WallOpening }; side: Side; refs: string[] } | null {
  const front = frontDoorAnyState(l, b);
  if (!front) return null;
  const side = OPPOSITE_SIDE[openingSide(b, front.opening)];
  const refs = unifyOpenings(b)
    .map((o, i) => ({ o, ref: openingRef(b, i) }))
    .filter(({ o }) => o.kind === 'window' && (o.floor ?? 0) === 0 && openingSide(b, o) === side && classifyOpening(l, b, o).exterior && effectiveOpeningState(l, b, o) !== 'broken')
    .map(({ ref }) => ref);
  return { front, side, refs };
}

/**
 * The gable doors of a building: its exterior ground doors on a side that is neither the front door's side nor the opposite one (a khrushchevka's
 * end-wall door, template gable_door), that are not in `state` already. Empty without a front door.
 */
export function gableDoors(l: GenLayout, b: GenBuilding, state?: DoorState): { ref: string; opening: WallOpening }[] {
  const front = frontDoorAnyState(l, b);
  if (!front) return [];
  const side = openingSide(b, front.opening), back = OPPOSITE_SIDE[side];
  return unifyOpenings(b)
    .map((opening, i) => ({ ref: openingRef(b, i), opening }))
    .filter(({ opening: o }) => o.kind === 'door' && (o.floor ?? 0) === 0 && classifyOpening(l, b, o).exterior && openingSide(b, o) !== side && openingSide(b, o) !== back
      && (state === undefined || effectiveOpeningState(l, b, o) !== state));
}

/** Story 3's boarded facade: the exterior ground windows on `side` that are not boarded already. */
export function frontGroundWindows(l: GenLayout, b: GenBuilding, side: Side): WallOpening[] {
  return unifyOpenings(b).filter((o) => o.kind === 'window' && (o.floor ?? 0) === 0 && openingSide(b, o) === side && classifyOpening(l, b, o).exterior && effectiveOpeningState(l, b, o) !== 'boarded');
}

/** The bricks of a story for this building, or null when the story is not eligible (F1 `eligible(s, b)`). */
export function storyRecipe(l: GenLayout, b: GenBuilding, story: StoryId): Brick[] | null {
  if (!b.enterable) return story === 'open' ? [] : null;
  switch (story) {
    case 'open':
      return [];
    case 'locked_door': {
      // the back door must stay: >= 2 passable exterior doors (building-stories.md 6 row 3 "round to the back door")
      if (passableExteriorDoors(l, b).length < 2) return null;
      const front = frontDoor(l, b);
      if (!front) return null;
      // designer 2026-11-19 (F4 rule A): the loud facade is boarded up - its ground windows 'boarded', so the fast route has to kick
      // the locked front instead of breaking a front window (fast = quiet entry made the 3 + 9 pair rare); no front window -> door only
      const side = openingSide(b, front.opening);
      const board = frontGroundWindows(l, b, side).length > 0;
      return [{ kind: 'door_state', door: front.ref, state: 'locked' }, ...(board ? [{ kind: 'window_state', target: side, state: 'boarded', floor: 0 } as Brick] : [])];
    }
    case 'rubble_escape': {
      // block / office with >= 2 storeys and a core flight 0 -> 1; the escape serves every upper storey (row 4: "balcony on each
      // storey"); whether the balcony fits is the brick's job (no spot -> rollback, Edge Cases)
      if (!RUBBLE_ESCAPE_SET_PIECES.includes(b.set_piece) || b.hollow || b.floors < 2) return null;
      const flight = (b.stairs ?? []).find((st) => st.kind === 'stairwell' && st.from === 0 && st.to === 1 && !st.blocked);
      const storeys = Array.from({ length: b.floors - 1 }, (_, i) => i + 1);
      // a planned block (flats with balcony slots, S2.3): the first flat column whose slots all fit; any other building: the S1 rule
      const room = !flight ? null : b.units?.length ? escapeColumns(l, b, storeys)[0] ?? null : escapeRoom(l, b, 1);
      if (!flight || !room) return null;
      return [{ kind: 'block_flight', stair: flight.id, block: 'rubble' }, { kind: 'add_balcony_escape', room: room.id, storeys }];
    }
    case 'roof_outside': {
      // a walkable roof reached from inside (a core flight onto it or a hatch); the facade ladder is the brick's job
      if (!b.roof_walkable) return null;
      const inner = insideRoofAccess(l, b);
      return inner.stairs.length || inner.hatches || inner.ladders.length ? [{ kind: 'roof_access', access: 'ladder' }] : null;
    }
    case 'broken_back_windows': {
      // designer 2026-11-19: only with a loud or absent front - the front door locked (a kick), sealed or barricaded (no passable
      // front door); in practice after story 3 (F2 "3 + 9"). >= 1 ground window on the opposite facade; the front windows keep their
      // default ('glazed' in /2)
      const back = backWindows(l, b);
      if (!back || !(LOUD_FRONT_STATES as readonly string[]).includes(effectiveOpeningState(l, b, back.front.opening))) return null;
      // a gable door the story would leave as the nearest quiet entry is locked by the story itself (building_stories.json gable_door_state)
      const gs = GABLE_DOOR_STATE.broken_back_windows;
      const gable: Brick[] = gs ? gableDoors(l, b, gs).map(({ ref }) => ({ kind: 'door_state', door: ref, state: gs })) : [];
      return back.refs.length ? [{ kind: 'window_state', target: back.side, state: 'broken', floor: 0 }, ...gable] : null;
    }
    case 'snow_drift': {
      // >= 2 storeys, a front door, and a storey-1 window on the front facade where the ramp fits (nearest the door first)
      if (b.hollow || b.floors < 2) return null;
      const front = frontDoor(l, b);
      if (!front) return null;
      const side = openingSide(b, front.opening), fa = alongOf(side, toBuildingFrame(b, front.opening.pos));
      const off = (o: WallOpening) => Math.abs(alongOf(side, toBuildingFrame(b, o.pos)) - fa);
      const hit = unifyOpenings(b)
        .map((o, i) => ({ o, ref: openingRef(b, i) }))
        .filter(({ o }) => o.kind === 'window' && o.floor === 1 && openingSide(b, o) === side)
        .sort((a, c) => off(a.o) - off(c.o) || (a.ref < c.ref ? -1 : a.ref > c.ref ? 1 : 0))
        .find((c) => planSnowRamp(l, b, c.ref, front.opening));
      return hit ? [{ kind: 'door_state', door: front.ref, state: 'sealed' }, { kind: 'snow_ramp', window: hit.ref }] : null;
    }
  }
}

/** The opening a choice story turns into the choice (F4 lever): the snow-ramp window (10), else the locked door (3). */
export function choiceLever(recipe: readonly Brick[]): string | undefined {
  for (const b of recipe) if (b.kind === 'snow_ramp') return b.window;
  for (const b of recipe) if (b.kind === 'door_state' && b.state === 'locked') return b.door;
  return undefined;
}

/** Is the story eligible for the building (F1)? With a role, a detour story needs a DETOUR_ROLES role. */
export function storyEligible(l: GenLayout, b: GenBuilding, story: StoryId, role: BuildingRole | undefined = b.role): boolean {
  if (role && STORY_CATALOGUE[story].type === 'detour' && !DETOUR_ROLES.includes(role)) return false;
  return storyRecipe(l, b, story) !== null;
}

// ---- pick (F1) ----------------------------------------------------------------------------------------------------------------------

export interface PickStoryOptions {
  /** The layout the building belongs to (eligibility reads its schema, walls and mandatory route); absent = a bare /1 layout. */
  layout?: GenLayout;
  /** Stories already tried and rolled back for this building: excluded, and step 1 is skipped (the building is storied). */
  exclude?: readonly StoryId[];
}

/**
 * F1 two-step pick: step 1 - storied with probability budget[role] (F3), else 'open'; step 2 - among the eligible stories other
 * than 'open', P(s) = w[role][s] / sum of the eligible weights; nothing eligible -> 'open'. Deterministic per seed.
 */
export function pickStory(
  building: GenBuilding,
  role: BuildingRole | undefined,
  seed: number,
  knobs: StoryPickKnobs = STORY_PICK_KNOBS,
  opts: PickStoryOptions = {},
): StoryId {
  if (!role || !building.enterable) return 'open';
  const r = rng(seed);
  const exclude = opts.exclude ?? [];
  if (!exclude.length && r() >= (knobs.budget[role] ?? 0)) return 'open';
  const l: GenLayout = opts.layout ?? ({ schema: 'codex.levelgen_layout/1', walls: [], routes: [], buildings: [building] } as unknown as GenLayout);
  const weights = knobs.weights[role] ?? {};
  const cands = (Object.keys(weights) as Exclude<StoryId, 'open'>[])
    .filter((s) => (weights[s] ?? 0) > 0 && !exclude.includes(s) && storyEligible(l, building, s, role))
    .sort();
  const total = cands.reduce((a, s) => a + (weights[s] ?? 0), 0);
  if (!cands.length || total <= 0) return 'open';
  let x = r() * total;
  for (const s of cands) {
    x -= weights[s] ?? 0;
    if (x < 0) return s;
  }
  return cands[cands.length - 1];
}

/**
 * F2 second story: with probability p_two (skipped on a retry, `exclude` non-empty) pick among compatible(first) \ {first} the
 * stories eligible on `building` AS IT IS after the first story (F1 step-2 weights of the role); null = no second story.
 * Deterministic per seed (the pass uses its own sub-seeds, so the first pick is unchanged).
 */
export function pickSecondStory(
  building: GenBuilding,
  first: StoryId,
  role: BuildingRole | undefined,
  seed: number,
  knobs: StoryPickKnobs = STORY_PICK_KNOBS,
  opts: PickStoryOptions = {},
): StoryId | null {
  if (!role || !building.enterable || first === 'open') return null;
  const compat = (STORY_COMPATIBLE[first] ?? []).filter((s) => s !== first && !(opts.exclude ?? []).includes(s));
  if (!compat.length) return null;
  const r = rng(seed);
  if (!(opts.exclude ?? []).length && r() >= (knobs.p_two ?? 0)) return null;
  const l: GenLayout = opts.layout ?? ({ schema: 'codex.levelgen_layout/1', walls: [], routes: [], buildings: [building] } as unknown as GenLayout);
  const weights = knobs.weights[role] ?? {};
  const cands = compat.filter((s) => (weights[s] ?? 0) > 0 && storyEligible(l, building, s, role)).sort();
  const total = cands.reduce((a, s) => a + (weights[s] ?? 0), 0);
  if (!cands.length || total <= 0) return null;
  let x = r() * total;
  for (const s of cands) {
    x -= weights[s] ?? 0;
    if (x < 0) return s;
  }
  return cands[cands.length - 1];
}

// ---- apply (section 5 steps 2-4) ----------------------------------------------------------------------------------------------------

export interface ApplyStoryResult {
  /** The story held: every brick applied and no new validator error. */
  ok: boolean;
  /** The storied copy (with `stories` appended) when ok, else the ORIGINAL building object, untouched. */
  building: GenBuilding;
  /** The story instance (applied when ok, attempted when not). */
  instance: StoryInstance;
  /** 'story_rolled_back' (warning) when not ok. */
  issues: TraversalIssue[];
  /** The layout's ladder list after the story, only when ok and a brick changed it (fire escape, facade ladder). */
  ladders?: GenLadder[];
}

const errorKeys = (issues: readonly TraversalIssue[]) => new Set(issues.filter((i) => i.severity === 'error').map((i) => `${i.code}|${i.ref ?? ''}`));

/**
 * Apply a story to a copy of `building` (a building of `layout`), validate the copy in the layout, and keep it only when it adds
 * no validator error the original did not have. The author is drawn from the story's allowed authors by `seed`; no outcome.
 */
export function applyStory(layout: GenLayout, building: GenBuilding, storyId: StoryId, seed: number): ApplyStoryResult {
  return applyRecipe(layout, building, storyId, storyRecipe(layout, building, storyId), seed);
}

/** applyStory with an explicit recipe (null = not eligible): the shared apply / validate / rollback step. */
export function applyRecipe(layout: GenLayout, building: GenBuilding, storyId: StoryId, recipe: readonly Brick[] | null, seed: number): ApplyStoryResult {
  const r = rng(seed);
  const authors = STORY_CATALOGUE[storyId].authors;
  const author = authors[Math.floor(r() * authors.length) % authors.length];
  const instance: StoryInstance = { id: storyId, bricks: (recipe ?? []).map(brickToString), seed, author };
  const rollback = (why: string): ApplyStoryResult => ({
    ok: false,
    building,
    instance,
    issues: [{ severity: TRAVERSAL_SEVERITY.story_rolled_back, code: 'story_rolled_back', ref: storyId, message: `${building.id}: story ${storyId} rolled back (${why})` }],
  });
  if (!recipe) return rollback('not eligible');
  const copy: GenBuilding = structuredClone(building);
  // bricks edit the copy and a copy of the layout's ladders (fire escapes, facade ladders)
  const work: GenLayout = { ...layout, ladders: structuredClone(layout.ladders ?? []) };
  for (const brick of recipe) if (!applyBrick(work, copy, brick)) return rollback(`brick ${brickToString(brick)} did not apply`);
  const swap = (base: GenLayout, b: GenBuilding): GenLayout => ({ ...base, buildings: base.buildings.map((x) => (x.id === building.id ? b : x)) });
  if (recipe.length) {
    const before = errorKeys(validateBuilding(swap(layout, building), building).issues);
    const after = validateBuilding(swap(work, copy), copy);
    const added = after.issues.filter((i) => i.severity === 'error' && !before.has(`${i.code}|${i.ref ?? ''}`));
    if (added.length) return rollback(added.map((i) => i.code).join(', '));
    // choice stories (rule 7 / F4): fast and quiet enter by different exterior openings, one of them the story's lever
    if (STORY_CATALOGUE[storyId].type === 'choice') {
      const lever = storyId === 'broken_back_windows' ? undefined : choiceLever(recipe); // story 9's lever is its back window (backOk), its gable lock is no lever
      const r = computeBuildingRoutes(swap(work, copy), copy);
      // story 9: the quiet entry is one of the broken back windows and the fast entry on the front facade (the front door, or a
      // glazed front window shattered - "front glazed")
      const back = storyId === 'broken_back_windows' ? backWindows(layout, building) : null;
      const fastOp = back ? unifyOpenings(copy).find((_, i) => openingRef(copy, i) === r.fast_entry) : undefined;
      const backOk = !back || (!!fastOp && openingSide(copy, fastOp) === OPPOSITE_SIDE[back.side] && back.refs.includes(r.quiet_entry ?? ''));
      if (!r.choice || !backOk || (lever !== undefined && r.fast_entry !== lever && r.quiet_entry !== lever)) {
        return rollback(`F4: fast enters by ${r.fast_entry ?? '-'}, quiet by ${r.quiet_entry ?? '-'}`);
      }
    }
  }
  copy.stories = [...(copy.stories ?? []), instance];
  const laddersChanged = JSON.stringify(work.ladders) !== JSON.stringify(layout.ladders ?? []);
  return { ok: true, building: copy, instance, issues: [], ...(laddersChanged ? { ladders: work.ladders } : {}) };
}

// ---- per map (section 5) ------------------------------------------------------------------------------------------------------------

export interface StoryLogEntry {
  building: string;
  story: StoryId;
  try: number;
  issue: TraversalIssue;
}

/** The deterministic sub-seed of building `id`, try `t` (section 5 step 4 "deterministic sub-seed"). */
export function storySubSeed(seed: number, id: string, t: number): number {
  return fnv1a(`${seed}:${id}:${t}`);
}

/**
 * Roles (withRoles) + per enterable building: pick (F1) and apply, up to t_max tries (a rolled-back story is excluded from the next
 * try), then 'open'; a held story may get a second compatible one (F2, p_two, STORY_COMPATIBLE: 3 + 9). Every enterable building
 * ends with one StoryInstance (two after F2); a building that already has `stories` (locked on the
 * page) is kept as it is. Deterministic per (seed, building id). The input layout is not mutated.
 */
export function applyStoriesToLayout(
  layout: GenLayout,
  seed: number,
  knobs: StoryPickKnobs = STORY_PICK_KNOBS,
): { layout: GenLayout; log: StoryLogEntry[] } {
  let cur = withRoles(layout);
  const log: StoryLogEntry[] = [];
  for (const id of cur.buildings.map((b) => b.id)) {
    const b = cur.buildings.find((x) => x.id === id)!;
    if (!b.enterable || !b.role || b.stories?.length) continue;
    const failed: StoryId[] = [];
    let done: ApplyStoryResult | null = null;
    for (let t = 0; t < knobs.t_max && !done; t++) {
      const sub = storySubSeed(seed, id, t);
      const story = pickStory(b, b.role, sub, knobs, { layout: cur, exclude: failed });
      if (story === 'open') break;
      const res = applyStory(cur, b, story, sub);
      if (res.ok) done = res;
      else {
        failed.push(story);
        for (const issue of res.issues) log.push({ building: id, story, try: t, issue });
      }
    }
    if (!done) done = applyStory(cur, b, 'open', storySubSeed(seed, id, 0));
    const first = done;
    const before = cur;
    cur = { ...cur, buildings: cur.buildings.map((x) => (x.id === id ? first.building : x)), ...(first.ladders ? { ladders: first.ladders } : {}) };
    // F2 (section 5 step 5): a second compatible story on the storied building, steps 2-4 for it alone (own sub-seeds `<id>#2`)
    let next = first;
    const firstId = first.instance.id as StoryId;
    if (first.ok && STORY_COMPATIBLE[firstId]?.length) {
      const failed2: StoryId[] = [];
      for (let t = 0; t < knobs.t_max; t++) {
        const sub = storySubSeed(seed, `${id}#2`, t);
        const second = pickSecondStory(first.building, firstId, b.role, sub, knobs, { layout: cur, exclude: failed2 });
        if (!second) break;
        const res = applyStory(cur, first.building, second, sub);
        if (res.ok) {
          next = res;
          const held = res;
          cur = { ...cur, buildings: cur.buildings.map((x) => (x.id === id ? held.building : x)), ...(held.ladders ? { ladders: held.ladders } : {}) };
          break;
        }
        failed2.push(second);
        for (const issue of res.issues) log.push({ building: id, story: second, try: t, issue });
      }
    }
    // route model (building-route-model.md rule 6, F3): key route + entries, D against the building before its story
    const routes = computeBuildingRoutes(cur, next.building, { baseline: { layout: before, building: b } });
    if (routes.key_route) next.building.key_route = routes.key_route;
    if (routes.breach_route) next.building.breach_route = routes.breach_route;
    if (routes.fast_entry !== undefined) next.building.fast_entry = routes.fast_entry;
    if (routes.quiet_entry !== undefined) next.building.quiet_entry = routes.quiet_entry;
  }
  return { layout: syncMantleEdges(cur), log };
}

/**
 * A brick that replaces a window by a door (story 4 on a slot window) drops the window's mantle edge from the building; the layout-level list
 * (`mantle_edges`, a copy made by finalizeDraftToLayout) loses it too. Only window edges of a building with its own list are checked; layouts where nothing went stay as they are.
 */
function syncMantleEdges(l: GenLayout): GenLayout {
  if (!l.mantle_edges?.length) return l;
  const own = new Map(l.buildings.filter((b) => b.mantle_edges).map((b) => [b.id, new Set(b.mantle_edges!.map((m) => m.id))]));
  const kept = l.mantle_edges.filter((m) => m.kind !== 'window' || !m.building || !own.has(m.building) || own.get(m.building)!.has(m.id));
  return kept.length === l.mantle_edges.length ? l : { ...l, mantle_edges: kept };
}
